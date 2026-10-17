// ================================================================================================
// File: retail_owner.cpp
// Brief: Bind real carousel and credits owners without importing online, save or Classic menu startup.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/retail_owner.h"
#include "ps2/system/log.h"
#include "ps2/ui/retail_assets.h"
#include "ps2/ui/swf_runtime.h"
#include <idlib/precompiled.h>

// Native menu contracts require the game's widget, SWF and save-description declarations.
#include <d3xp/Game_local.h>

namespace
{
static constexpr int kVisibleCarouselItems = 5;
static constexpr char kDoom3Cover[] = "guis/assets/mainmenu/doom3_cover";

class Owner final : public idMenuHandler
{
public:
    explicit Owner(idSWF & movie)
    {
        gui = &movie;
        platform = 2;
        activeScreen = nextScreen = SHELL_AREA_START;

        m_carousel = new (TAG_SWF) idMenuWidget_Carousel;
        m_carousel->SetSpritePath("menuStart", "info", "options");
        m_carousel->SetNumVisibleOptions(kVisibleCarouselItems);

        for (int index = 0; index < kVisibleCarouselItems; ++index)
        {
            auto * const button = new (TAG_SWF) idMenuWidget_Button;
            button->Initialize(this);
            m_carousel->AddChild(button);
        }

        // Preserve the original five-slot layout, with Doom 3 as the only carousel entry.
        char admittedDefinition[1024];

        if (!ps2::ui::retail::MaterialText(kDoom3Cover, admittedDefinition, sizeof(admittedDefinition)))
        {
            ps2::FatalError("retail Shell cover is outside the admitted material catalog");
        }

        // Catalog definitions are generated lazily when native DeclManager first creates the material.
        const idMaterial * const cover = declManager->FindMaterial(kDoom3Cover);

        if (cover == nullptr || cover->GetState() == DS_DEFAULTED)
        {
            ps2::FatalError("retail Shell owner requires its admitted Doom 3 cover material");
        }

        idList<const idMaterial *> images;
        images.Append(cover);
        m_carousel->SetListImages(images);
        m_carousel->Initialize(this);
        AddChild(m_carousel);
        m_refreshCarousel = PinCallback("refreshCarousel");

        // The complete native credit list and screen state own updateCredits, including its idle gate.
        auto * const credits = new (TAG_SWF) idMenuScreen_Shell_Credits;
        credits->Initialize(this);
        credits->AddRef();
        menuScreens[SHELL_AREA_CREDITS] = credits;
        m_updateCredits = PinCallback("updateCredits");
    }

    ~Owner() override
    {
        // Globals can alias callbacks, so keep the real functions pinned until timeline execution ends.
        gui->SetGlobal("refreshCarousel", idSWFScriptVar());
        gui->SetGlobal("updateCredits", idSWFScriptVar());
        m_refreshCarousel = idSWFScriptVar();
        m_updateCredits = idSWFScriptVar();

        // The caller owns the movie. Base cleanup releases widgets and screens before returning to it.
        gui = nullptr;
    }

    bool OwnsCallback(const char * name, const idSWFScriptFunction * function, const idSWF * movie)
    {
        if (movie != gui || name == nullptr || function == nullptr)
        {
            return false;
        }

        return (idStr::Cmp(name, "refreshCarousel") == 0 && function == m_refreshCarousel.GetFunction()) ||
               (idStr::Cmp(name, "updateCredits") == 0 && function == m_updateCredits.GetFunction());
    }

    void PrintStatus() const
    {
        ps2::Log(ps2::LogLevel::Info,
                 "[D3BFG] RETAIL owner screen=%d next=%d carousel_items=%d carousel_slots=%d carousel_view=%d credits=1 callbacks=2\n",
                 activeScreen, nextScreen, m_carousel->GetTotalNumberOfOptions(),
                 m_carousel->GetNumVisibleOptions(), m_carousel->GetViewIndex());
    }

    void CheckCallbackOrigin(const char * name, idSWFSpriteInstance * executing)
    {
        ps2::ui::swf::Require(executing != nullptr && executing->sprite->GetSWF() == gui,
                              "widget callback movie ownership");
        ps2::ui::swf::Trace("owner-caller", executing->GetName());

        idSWFSpriteInstance * targets[kVisibleCarouselItems + 1] = {};

        if (idStr::Cmp(name, "refreshCarousel") == 0)
        {
            for (int index = 0; index < kVisibleCarouselItems; ++index)
            {
                targets[index] = gui->GetRootObject().GetNestedSprite("menuStart", "info", "options", va("item%d", index));
            }

            if (m_carousel->GetMoveDiff() != 0)
            {
                targets[kVisibleCarouselItems] = gui->GetRootObject().GetNestedSprite("menuStart", "info", "options");
            }
        }
        else if (activeScreen == SHELL_AREA_CREDITS || nextScreen == SHELL_AREA_CREDITS)
        {
            targets[0] = gui->GetRootObject().GetNestedSprite("menuCredits");
        }

        // A refresh may retire an outer caller even when the current function uses a different sprite.
        for (auto * const target : targets)
        {
            if (target != nullptr)
            {
                ps2::ui::swf::CheckSeek(target, "ancestor widget refresh unavailable");
            }
        }
    }

private:
    idSWFScriptVar PinCallback(const char * name)
    {
        auto function = gui->GetGlobal(name);

        if (!function.IsFunction() || function.GetFunction()->IsScript())
        {
            ps2::FatalError("retail Shell native widget callback missing: %s", name);
        }

        return function;
    }

    idMenuWidget_Carousel * m_carousel = nullptr;
    idSWFScriptVar m_refreshCarousel;
    idSWFScriptVar m_updateCredits;
};

static Owner * s_owner = nullptr;
} // namespace

namespace ps2::ui::retail_owner
{
void Init(idSWF & movie, unsigned movieIndex)
{
    if (s_owner != nullptr || movieIndex >= 2 || !movie.IsResourceOnly() || movie.IsResourceExecution())
    {
        FatalError("invalid retail native widget owner startup");
    }

    if (movieIndex == 0)
    {
        s_owner = new (TAG_SWF) Owner(movie);
    }
}

void Shutdown(idSWF & movie)
{
    if (s_owner == nullptr)
    {
        return;
    }

    if (s_owner->GetGUI() != &movie)
    {
        FatalError("retail native widgets do not own the departing movie");
    }

    delete s_owner;
    s_owner = nullptr;
}

bool IsCallback(const char * name, const idSWFScriptFunction * function, const idSWF * movie)
{
    return s_owner != nullptr && s_owner->OwnsCallback(name, function, movie);
}

void CheckCallbackOrigin(const char * name, idSWFSpriteInstance * executing, idSWF * movie)
{
    if (idStr::Cmp(name, "refreshCarousel") != 0 && idStr::Cmp(name, "updateCredits") != 0)
    {
        return;
    }

    if (s_owner == nullptr || s_owner->GetGUI() != movie)
    {
        FatalError("retail widget callback owner missing");
    }

    s_owner->CheckCallbackOrigin(name, executing);
}

void Status()
{
    if (s_owner != nullptr)
    {
        s_owner->PrintStatus();
    }
}
} // namespace ps2::ui::retail_owner
