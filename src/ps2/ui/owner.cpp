// ================================================================================================
// File: owner.cpp
// Brief: Bind real Shell widget owners and native dialog state without online, save or Classic startup.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/owner.h"
#include "ps2/system/log.h"
#include "ps2/ui/assets.h"
#include "ps2/ui/menu.h"
#include "ps2/ui/swf_runtime.h"
#include <idlib/precompiled.h>

// Native menu contracts require the game's widget, SWF and save-description declarations.
#include <d3xp/Game_local.h>

namespace
{

static constexpr int kVisibleCarouselItems = 5;

// The shell handler's widgets and bound screens hold about thirty widgets, two levels deep.
static constexpr unsigned kMaxWidgetDepth = 8;
static constexpr unsigned kMaxWidgets = 1024;
static constexpr char kDoom3Cover[] = "guis/assets/mainmenu/doom3_cover";

// The native callbacks the shell handler and its start, root and credits screens install.
static constexpr const char * kMenuCallbacks[] = { "updateMenuDisplay", "activateMenus", "closeMenu", "refreshCarousel", "updateCredits" };

// The choices idCommonDialog::ShowDialog installs for the shown dialog.
static constexpr const char * kDialogChoices[] = { "acceptCallBack", "cancelCallBack", "altCBOne", "altCBTwo" };

PS2_COLD_FUNC idSWFScriptVar PinCallback(idSWF & movie, const char * name)
{
    auto function = movie.GetGlobal(name);

    if (!function.IsFunction() || function.GetFunction()->IsScript()) [[unlikely]]
    {
        ps2::FatalError("retail Shell native widget callback missing: %s", name);
    }

    return function;
}

#if PS2_D3BFG_ASSERTS
// Compares pointers only, so a wrapper whose widget no longer exists is never dereferenced.
bool ContainsWidget(const idMenuWidget * widget, const idMenuWidget * target, unsigned depth, unsigned & visited)
{
    if (widget == nullptr)
    {
        return false;
    }

    if (depth > kMaxWidgetDepth || ++visited > kMaxWidgets) [[unlikely]]
    {
        ps2::FatalError("retail menu widget tree exceeds admitted limits");
    }

    if (widget == target)
    {
        return true;
    }

    const auto & children = widget->GetChildren();

    for (int index = 0; index < children.Num(); ++index)
    {
        if (ContainsWidget(children[index], target, depth + 1, visited))
        {
            return true;
        }
    }

    return false;
}

// A carousel refresh rebinds its item sprites, and seeks the options sprite while it moves; a
// credits refresh rewrites the credits screen. None may retire an executing script's ancestor,
// even when the current function runs from a different sprite.
void CheckRefreshTargets(idSWF & movie, const char * name, idSWFSpriteInstance * executing, idMenuWidget_Carousel * carousel,
                         bool credits)
{
    ps2::ui::swf::Require(executing != nullptr && executing->sprite->GetSWF() == &movie, "widget callback movie ownership");
    ps2::ui::swf::Trace("owner-caller", executing->GetName());

    idSWFSpriteInstance * targets[kVisibleCarouselItems + 1] = {};

    if (idStr::Cmp(name, "refreshCarousel") == 0)
    {
        ps2::ui::swf::Require(carousel != nullptr, "carousel refresh owner");

        for (int index = 0; index < kVisibleCarouselItems; ++index)
        {
            targets[index] = movie.GetRootObject().GetNestedSprite("menuStart", "info", "options", va("item%d", index));
        }

        if (carousel->GetMoveDiff() != 0)
        {
            targets[kVisibleCarouselItems] = movie.GetRootObject().GetNestedSprite("menuStart", "info", "options");
        }
    }
    else if (credits)
    {
        targets[0] = movie.GetRootObject().GetNestedSprite("menuCredits");
    }

    for (auto * const target : targets)
    {
        if (target != nullptr)
        {
            ps2::ui::swf::CheckSeek(target, "ancestor widget refresh unavailable");
        }
    }
}
#endif // PS2_D3BFG_ASSERTS

class Owner final : public idMenuHandler
{
public:
    explicit Owner(idSWF & movie)
    {
        gui = &movie;
        platform = ps2::ui::kMenuPlatform;
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

        if (!ps2::ui::MaterialText(kDoom3Cover, admittedDefinition, sizeof(admittedDefinition))) [[unlikely]]
        {
            ps2::FatalError("retail Shell cover is outside the admitted material catalog");
        }

        // Catalog definitions are generated lazily when native DeclManager first creates the material.
        const idMaterial * const cover = declManager->FindMaterial(kDoom3Cover);

        if (cover == nullptr || cover->GetState() == DS_DEFAULTED) [[unlikely]]
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

#if PS2_D3BFG_ASSERTS
    bool OwnsCallback(const char * name, const idSWFScriptFunction * function, const idSWF * movie)
    {
        if (movie != gui || name == nullptr || function == nullptr)
        {
            return false;
        }

        return (idStr::Cmp(name, "refreshCarousel") == 0 && function == m_refreshCarousel.GetFunction()) ||
               (idStr::Cmp(name, "updateCredits") == 0 && function == m_updateCredits.GetFunction());
    }
#endif // PS2_D3BFG_ASSERTS

    void PrintStatus() const
    {
        ps2::Log(ps2::LogLevel::Info,
                 "[D3BFG] RETAIL owner screen=%d next=%d carousel_items=%d carousel_slots=%d carousel_view=%d credits=1 callbacks=2 logo=%d\n",
                 activeScreen, nextScreen, m_carousel->GetTotalNumberOfOptions(),
                 m_carousel->GetNumVisibleOptions(), m_carousel->GetViewIndex(), m_logoShowing ? 1 : 0);
    }

    // idMenuHandler_Shell::ShowLogo, which the native shell's background update calls on leaving
    // the start screen: the logo rolls on as the background rolls off, and back.
    void ShowLogo(bool show)
    {
        if (show == m_logoShowing)
        {
            return;
        }

        idSWFSpriteInstance * const logo = gui->GetRootObject().GetNestedSprite("logoInfo");
        idSWFSpriteInstance * const background = gui->GetRootObject().GetNestedSprite("background");

        if (logo == nullptr || background == nullptr) [[unlikely]]
        {
            ps2::FatalError("retail Shell logo or background sprite missing");
        }

        background->stereoDepth = STEREO_DEPTH_TYPE_MID;
        logo->PlayFrame(show ? "rollOn" : "rollOff");
        background->PlayFrame(show ? "rollOff" : "rollOn");
        m_logoShowing = show;
    }

#if PS2_D3BFG_ASSERTS
    void CheckCallbackOrigin(const char * name, idSWFSpriteInstance * executing)
    {
        CheckRefreshTargets(*gui, name, executing, m_carousel, activeScreen == SHELL_AREA_CREDITS || nextScreen == SHELL_AREA_CREDITS);
    }
#endif // PS2_D3BFG_ASSERTS

private:
    idSWFScriptVar PinCallback(const char * name)
    {
        return ::PinCallback(*gui, name);
    }

    idMenuWidget_Carousel * m_carousel = nullptr;
    idSWFScriptVar m_refreshCarousel;
    idSWFScriptVar m_updateCredits;
    bool m_logoShowing = false;
};

static Owner * s_owner = nullptr;

// The native shell handler owns its widgets, screens and movie; this only records the callbacks
// it installed, so a script cannot alias another native service under one of their names.
class MenuOwner final
{
public:
    explicit MenuOwner(idMenuHandler & handler) : m_handler(handler), m_movie(*handler.GetGUI())
    {
        for (int index = 0; index < ps2::ArrayLength(kMenuCallbacks); ++index)
        {
            m_callbacks[index] = PinCallback(m_movie, kMenuCallbacks[index]);
        }
    }

    idSWF & Movie() const
    {
        return m_movie;
    }

#if PS2_D3BFG_ASSERTS
    bool OwnsCallback(const char * name, const idSWFScriptFunction * function, const idSWF * movie)
    {
        if (movie != &m_movie || name == nullptr || function == nullptr)
        {
            return false;
        }

        for (int index = 0; index < ps2::ArrayLength(kMenuCallbacks); ++index)
        {
            if (idStr::Cmp(name, kMenuCallbacks[index]) == 0)
            {
                return function == m_callbacks[index].GetFunction();
            }
        }

        return false;
    }

    // Native widgets bind wrappers of their own events to shortcut keys and button objects; any other
    // native function there was put by a script.
    bool OwnsEventHandler(const idSWFScriptFunction * function)
    {
        const auto * const wrapper = ps2::DynCast<const idMenuWidget::WrapWidgetSWFEvent *>(function);

        if (wrapper == nullptr)
        {
            return false;
        }

        const idMenuWidget * const target = wrapper->GetTargetWidget();
        unsigned visited = 0;

        for (int index = 0; index < m_handler.GetNumChildren(); ++index)
        {
            if (ContainsWidget(m_handler.GetChildFromIndex(index), target, 0, visited))
            {
                return true;
            }
        }

        for (int area = 0; area < SHELL_NUM_AREAS; ++area)
        {
            if (ContainsWidget(m_handler.GetMenuScreen(area), target, 0, visited))
            {
                return true;
            }
        }

        return false;
    }

    void CheckCallbackOrigin(const char * name, idSWFSpriteInstance * executing)
    {
        const int active = m_handler.ActiveScreen();
        const int next = m_handler.NextScreen();
        CheckRefreshTargets(m_movie, name, executing, StartCarousel(), active == SHELL_AREA_CREDITS || next == SHELL_AREA_CREDITS);
    }
#endif // PS2_D3BFG_ASSERTS

    void PrintStatus()
    {
        auto * const root = ps2::DynCast<idMenuScreen_Shell_Root *>(m_handler.GetMenuScreen(SHELL_AREA_ROOT));
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] RETAIL menu screen=%d next=%d session=%s platform=%d callbacks=%d root_focus=%d\n",
                 m_handler.ActiveScreen(), m_handler.NextScreen(), session->GetStateString(), m_handler.GetPlatform(),
                 ps2::ArrayLength(kMenuCallbacks), root != nullptr ? root->GetRootIndex() : -1);
    }

private:
    // The start screen's carousel, found among its widgets.
    idMenuWidget_Carousel * StartCarousel()
    {
        idMenuScreen * const screen = m_handler.GetMenuScreen(SHELL_AREA_START);

        if (screen != nullptr)
        {
            for (int index = 0; index < screen->GetChildren().Num(); ++index)
            {
                if (auto * const carousel = ps2::DynCast<idMenuWidget_Carousel *>(screen->GetChildren()[index]))
                {
                    return carousel;
                }
            }
        }

        return nullptr;
    }

    idMenuHandler & m_handler;
    idSWF & m_movie;
    idSWFScriptVar m_callbacks[static_cast<std::size_t>(ps2::ArrayLength(kMenuCallbacks))];
};

static MenuOwner * s_menu = nullptr;

// The shown dialog's native choices, pinned when Common's dialog manager installs them. The movie
// copies them into button properties and calls them as methods, so identity, not the name,
// authorizes a call; a later dialog's choices replace them.
class DialogChoices final
{
public:
    explicit DialogChoices(idSWF & movie) : m_movie(movie)
    {
    }

    idSWF & Movie() const
    {
        return m_movie;
    }

    void Pin()
    {
        for (int index = 0; index < ps2::ArrayLength(kDialogChoices); ++index)
        {
            idSWFScriptVar choice = m_movie.GetGlobal(kDialogChoices[index]);
            m_choices[index] = choice.IsFunction() && !choice.GetFunction()->IsScript() ? choice : idSWFScriptVar();
        }
    }

#if PS2_D3BFG_ASSERTS
    bool Owns(const idSWFScriptFunction * function, const idSWF * movie)
    {
        if (movie != &m_movie || function == nullptr)
        {
            return false;
        }

        for (auto & choice : m_choices)
        {
            if (choice.IsFunction() && choice.GetFunction() == function)
            {
                return true;
            }
        }

        return false;
    }

    bool OwnsCallback(const char * name, const idSWFScriptFunction * function, const idSWF * movie)
    {
        for (int index = 0; index < ps2::ArrayLength(kDialogChoices); ++index)
        {
            if (name != nullptr && idStr::Cmp(name, kDialogChoices[index]) == 0)
            {
                return Owns(function, movie) && m_choices[index].GetFunction() == function;
            }
        }

        return false;
    }
#endif // PS2_D3BFG_ASSERTS

private:
    idSWF & m_movie;
    idSWFScriptVar m_choices[static_cast<std::size_t>(ps2::ArrayLength(kDialogChoices))];
};

static DialogChoices * s_choices = nullptr;

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
            if (m_options[option].IsEmpty()) [[unlikely]]
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

#if PS2_D3BFG_ASSERTS
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
#endif // PS2_D3BFG_ASSERTS

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

namespace ps2::ui
{

PS2_COLD_FUNC void AttachOwner(idSWF & movie, unsigned movieIndex)
{
    if (s_owner != nullptr || s_dialog != nullptr || s_menu != nullptr || s_choices != nullptr || movieIndex >= 2 ||
        !movie.IsResourceOnly() || movie.IsResourceExecution()) [[unlikely]]
    {
        FatalError("invalid retail native widget owner startup");
    }

    if (movieIndex == 0)
    {
        s_owner = new (TAG_SWF) Owner(movie);
    }
}

PS2_COLD_FUNC void ShowQuitDialog(idSWF & movie)
{
    if (s_owner != nullptr || s_dialog != nullptr || s_menu != nullptr || s_choices != nullptr || !movie.IsResourceOnly() ||
        movie.IsResourceExecution()) [[unlikely]]
    {
        FatalError("invalid retail dialog owner startup");
    }

    s_dialog = new (TAG_SWF) DialogOwner(movie);
}

PS2_COLD_FUNC void AttachMenu(idMenuHandler & handler)
{
    idSWF * const movie = handler.GetGUI();

    if (s_owner != nullptr || s_dialog != nullptr || s_menu != nullptr || movie == nullptr || !movie->IsResourceOnly() ||
        movie->IsResourceExecution()) [[unlikely]]
    {
        FatalError("invalid retail native menu owner startup");
    }

    s_menu = new (TAG_SWF) MenuOwner(handler);
}

PS2_COLD_FUNC void DetachMenu(idSWF & movie)
{
    if (s_menu == nullptr || &s_menu->Movie() != &movie) [[unlikely]]
    {
        FatalError("retail native menu owner does not own the departing movie");
    }

    delete s_menu;
    s_menu = nullptr;
}

PS2_COLD_FUNC void DetachOwner(idSWF & movie)
{
    if (s_dialog != nullptr)
    {
        if (&s_dialog->Movie() != &movie) [[unlikely]]
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

    if (s_owner->GetGUI() != &movie) [[unlikely]]
    {
        FatalError("retail native widgets do not own the departing movie");
    }

    delete s_owner;
    s_owner = nullptr;
}

#if PS2_D3BFG_ASSERTS
bool IsOwnerCallback(const char * name, const idSWFScriptFunction * function, const idSWF * movie)
{
    return (s_owner != nullptr && s_owner->OwnsCallback(name, function, movie)) ||
           (s_dialog != nullptr && s_dialog->OwnsCallback(name, function, movie)) ||
           (s_menu != nullptr && s_menu->OwnsCallback(name, function, movie)) ||
           (s_choices != nullptr && s_choices->OwnsCallback(name, function, movie));
}
#endif // PS2_D3BFG_ASSERTS

PS2_COLD_FUNC void AttachDialogChoices(idSWF & movie)
{
    if (s_choices != nullptr || s_dialog != nullptr || s_owner != nullptr || !movie.IsResourceExecution()) [[unlikely]]
    {
        FatalError("invalid retail dialog choice owner startup");
    }

    s_choices = new (TAG_SWF) DialogChoices(movie);
}

void PinShownChoices(idSWF & movie)
{
    if (s_choices == nullptr || &s_choices->Movie() != &movie) [[unlikely]]
    {
        FatalError("retail dialog choices outside their movie");
    }

    s_choices->Pin();
}

PS2_COLD_FUNC void DetachDialogChoices(idSWF & movie)
{
    if (s_choices == nullptr || &s_choices->Movie() != &movie) [[unlikely]]
    {
        FatalError("retail dialog choice owner does not own the departing movie");
    }

    delete s_choices;
    s_choices = nullptr;
}

#if PS2_D3BFG_ASSERTS
bool IsDialogChoice(const idSWFScriptFunction * function, const idSWF * movie)
{
    return s_choices != nullptr && s_choices->Owns(function, movie);
}

bool IsEventHandler(const idSWFScriptFunction * function, const idSWF * movie)
{
    return s_menu != nullptr && movie == &s_menu->Movie() && function != nullptr && s_menu->OwnsEventHandler(function);
}

void CheckCallbackOrigin(const char * name, idSWFSpriteInstance * executing, idSWF * movie)
{
    if (idStr::Cmp(name, "refreshCarousel") != 0 && idStr::Cmp(name, "updateCredits") != 0)
    {
        return;
    }

    if (s_menu != nullptr && &s_menu->Movie() == movie)
    {
        s_menu->CheckCallbackOrigin(name, executing);
        return;
    }

    if (s_owner == nullptr || s_owner->GetGUI() != movie) [[unlikely]]
    {
        FatalError("retail widget callback owner missing");
    }

    s_owner->CheckCallbackOrigin(name, executing);
}
#endif // PS2_D3BFG_ASSERTS

void ShowShellLogo(idSWF & movie, bool show)
{
    PS2_AssertMsg(s_owner != nullptr && s_owner->GetGUI() == &movie && movie.IsResourceExecution() && swf::Active(),
                  "retail Shell logo outside its executing owner");
    s_owner->ShowLogo(show);
}

PS2_COLD_FUNC void OwnerStatus()
{
    if (s_owner != nullptr)
    {
        s_owner->PrintStatus();
    }

    if (s_dialog != nullptr)
    {
        s_dialog->PrintStatus();
    }

    if (s_menu != nullptr)
    {
        s_menu->PrintStatus();
    }
}

} // namespace ps2::ui
