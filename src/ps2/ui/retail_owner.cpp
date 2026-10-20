// ================================================================================================
// File: retail_owner.cpp
// Brief: Bind real Shell widget owners and native dialog state without online, save or Classic startup.
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

// Dialog choices exist so the movie sees native callback objects, as idCommonDialog::ShowDialog
// supplies them. Routing a choice needs dialog events and Common's deferred clear; until then a call
// fails explicitly, either at the interpreter's signature check or here outside a script scope.
class DialogChoice final : public idSWFScriptFunction_RefCounted
{
public:
    explicit DialogChoice(const char * name) : m_name(name)
    {
    }

    idSWFScriptVar Call(idSWFScriptObject *, const idSWFParmList &) override
    {
        ps2::FatalError("retail dialog choice unavailable: %s", m_name);
    }

private:
    const char * m_name;
};

class DialogOwner final
{
public:
    explicit DialogOwner(idSWF & movie) : m_movie(movie)
    {
        // idCommonDialog::Init binds the dialog type constants before any message is shown.
        static constexpr struct
        {
            const char * name;
            dialogType_t value;
        } kTypes[] = {
            { "DIALOG_ACCEPT", DIALOG_ACCEPT }, { "DIALOG_CONTINUE", DIALOG_CONTINUE },
            { "DIALOG_ACCEPT_CANCEL", DIALOG_ACCEPT_CANCEL }, { "DIALOG_YES_NO", DIALOG_YES_NO },
            { "DIALOG_CANCEL", DIALOG_CANCEL }, { "DIALOG_WAIT", DIALOG_WAIT },
            { "DIALOG_WAIT_BLACKOUT", DIALOG_WAIT_BLACKOUT }, { "DIALOG_WAIT_CANCEL", DIALOG_WAIT_CANCEL },
            { "DIALOG_DYNAMIC", DIALOG_DYNAMIC }, { "DIALOG_QUICK_SAVE", DIALOG_QUICK_SAVE },
            { "DIALOG_TIMER_ACCEPT_REVERT", DIALOG_TIMER_ACCEPT_REVERT }, { "DIALOG_CRAWL_SAVE", DIALOG_CRAWL_SAVE },
            { "DIALOG_CONTINUE_LARGE", DIALOG_CONTINUE_LARGE }, { "DIALOG_BENCHMARK", DIALOG_BENCHMARK }
        };

        for (const auto & type : kTypes)
        {
            m_movie.SetGlobal(type.name, static_cast<int>(type.value));
        }

        // The root menu's quit request: idMenuScreen_Shell_Root::HandleExitGameBtn adds a dynamic
        // dialog with accept, cancel and change-game choices; ShowDialog publishes it to the movie.
        // Common's dialog manager stays uninitialized here, and the message table reads none of its
        // state, so a zero-initialized local manager supplies the native title and text keys.
        idCommonDialog messages{};
        messages.GetDialogMsg(GDM_QUIT_GAME, m_message, m_title);
        m_options[0] = idStrId("#STR_SWF_ACCEPT").GetLocalizedString();
        m_options[1] = idStrId("#STR_SWF_CANCEL").GetLocalizedString();
        m_options[2] = idStrId("#str_swf_change_game").GetLocalizedString();
        m_options[3] = idStrId().GetLocalizedString();

        for (int option = 0; option < 3; ++option)
        {
            if (m_options[option].IsEmpty())
            {
                ps2::FatalError("retail quit dialog option text missing: %d", option);
            }
        }

        m_accept = new (TAG_SWF) DialogChoice("accept");
        m_cancel = new (TAG_SWF) DialogChoice("cancel");
        m_alternate = new (TAG_SWF) DialogChoice("change game");
        m_movie.SetGlobal("titleVal", m_title);
        m_movie.SetGlobal("messageInfo", m_message);
        m_movie.SetGlobal("Infotype", static_cast<int>(DIALOG_DYNAMIC));
        m_movie.SetGlobal("acceptCallBack", m_accept);
        m_movie.SetGlobal("cancelCallBack", m_cancel);
        m_movie.SetGlobal("altCBOne", m_alternate);
        m_movie.SetGlobal("altCBTwo", static_cast<idSWFScriptFunction *>(nullptr));
        m_movie.SetGlobal("opt1Txt", m_options[0]);
        m_movie.SetGlobal("opt2Txt", m_options[1]);
        m_movie.SetGlobal("opt3Txt", m_options[2]);
        m_movie.SetGlobal("opt4Txt", m_options[3]);
    }

    ~DialogOwner()
    {
        // Script objects may still reference the choices; the movie releases those references itself.
        for (const char * name : { "acceptCallBack", "cancelCallBack", "altCBOne" })
        {
            m_movie.SetGlobal(name, idSWFScriptVar());
        }
    }

    idSWF & Movie() const
    {
        return m_movie;
    }

    bool OwnsCallback(const char * name, const idSWFScriptFunction * function, const idSWF * movie)
    {
        if (movie != &m_movie || name == nullptr || function == nullptr)
        {
            return false;
        }

        return (idStr::Cmp(name, "acceptCallBack") == 0 && function == m_accept.GetFunction()) ||
               (idStr::Cmp(name, "cancelCallBack") == 0 && function == m_cancel.GetFunction()) ||
               (idStr::Cmp(name, "altCBOne") == 0 && function == m_alternate.GetFunction());
    }

    void PrintStatus() const
    {
        ps2::Log(ps2::LogLevel::Info,
                 "[D3BFG] RETAIL dialog owner message=%d type=%d title=%s text=%s options=%s|%s|%s choices=3\n",
                 static_cast<int>(GDM_QUIT_GAME), static_cast<int>(DIALOG_DYNAMIC), m_title.c_str(), m_message.c_str(),
                 m_options[0].c_str(), m_options[1].c_str(), m_options[2].c_str());
    }

private:
    idSWF & m_movie;
    idStr m_title;
    idStr m_message;
    idStr m_options[4];
    idSWFScriptVar m_accept;
    idSWFScriptVar m_cancel;
    idSWFScriptVar m_alternate;
};

static DialogOwner * s_dialog = nullptr;
} // namespace

namespace ps2::ui::retail_owner
{
void Init(idSWF & movie, unsigned movieIndex)
{
    if (s_owner != nullptr || s_dialog != nullptr || movieIndex >= 2 || !movie.IsResourceOnly() ||
        movie.IsResourceExecution())
    {
        FatalError("invalid retail native widget owner startup");
    }

    if (movieIndex == 0)
    {
        s_owner = new (TAG_SWF) Owner(movie);
    }
}

void ShowQuitDialog(idSWF & movie)
{
    if (s_owner != nullptr || s_dialog != nullptr || !movie.IsResourceOnly() || movie.IsResourceExecution())
    {
        FatalError("invalid retail dialog owner startup");
    }

    s_dialog = new (TAG_SWF) DialogOwner(movie);
}

void Shutdown(idSWF & movie)
{
    if (s_dialog != nullptr)
    {
        if (&s_dialog->Movie() != &movie)
        {
            FatalError("retail dialog owner does not own the departing movie");
        }

        delete s_dialog;
        s_dialog = nullptr;
    }

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
    return (s_owner != nullptr && s_owner->OwnsCallback(name, function, movie)) ||
           (s_dialog != nullptr && s_dialog->OwnsCallback(name, function, movie));
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

    if (s_dialog != nullptr)
    {
        s_dialog->PrintStatus();
    }
}
} // namespace ps2::ui::retail_owner
