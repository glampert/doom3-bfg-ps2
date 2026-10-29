// ================================================================================================
// File: retail_menu.cpp
// Brief: Open, update and close the native shell menu, whose handler owns the admitted shell movie.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/retail_menu.h"
#include "ps2/renderer/image_backend.h"
#include "ps2/renderer/render_system.h"
#include "ps2/system/log.h"
#include "ps2/ui/retail_assets.h"
#include "ps2/ui/retail_owner.h"
#include "ps2/ui/swf_runtime.h"
#include <idlib/precompiled.h>

// The native shell handler and the game's shell interface need the game's menu declarations.
#include <d3xp/Game_local.h>

namespace
{

// Borrowed from the shell handler, which deletes it.
static idSWF * s_movie = nullptr;
static bool s_opening = false;

// Events the open menu received from Common, and those its handler reported handled.
static u32 s_events = 0;
static u32 s_handled = 0;

// Borrowed from Common's dialog manager, which deletes it; the open menu initializes the manager.
static idSWF * s_dialog = nullptr;
static bool s_dialogOpening = false;

// The clock retail movies activate on, once the presentation advances them; real time before.
static bool s_clockSet = false;
static int s_clock = 0;

// Native idSWF names its atlas material after the binary movie; screen passes need its
// per-channel CLUTs before the first frame.
void EnableMovieScreenBlending(int index, const char * atlasMaterial)
{
    if (!ps2::ui::retail::MovieUsesScreenBlending(index))
    {
        return;
    }

    const idMaterial * const atlas = declManager->FindMaterial(atlasMaterial);

    if (atlas == nullptr || atlas->GetNumStages() != 1 || atlas->GetStage(0)->texture.image == nullptr)
    {
        ps2::FatalError("retail menu atlas image missing for screen blending: %s", atlasMaterial);
    }

    ps2::renderer::EnableScreenBlending(atlas->GetStage(0)->texture.image);
}
} // namespace

namespace ps2::ui::retail
{

idSWF * OpenMenu()
{
    if (s_movie != nullptr || s_opening || renderer::FrameStarted() || ::game == nullptr)
    {
        FatalError("native menu requires an idle presentation without a menu");
    }

    // idCommonLocal::PerformGameSwitch returns Doom 3 to press-start before the menu appears.
    if (::session->GetState() <= idSession::IDLE)
    {
        ::session->MoveToPressStart();
    }

    swf::ResetStats();
    s_events = 0;
    s_handled = 0;
    s_opening = true;
    ::game->Shell_CreateMenu(false);

    {
        const swf::ExecutionScope scope("shell");
        ::game->Shell_Show(true);
        ::game->Shell_SyncWithSession();
    }

    s_opening = false;

    if (s_movie == nullptr || !s_movie->IsResourceRender() || !::game->Shell_IsActive())
    {
        FatalError("native shell handler did not start the admitted movie");
    }

    // idCommonLocal::Init then initializes Common's dialog manager, which opens the dialog movie.
    {
        const swf::ExecutionScope scope("dialog");
        s_dialogOpening = true;
        ::common->Dialog().Init();
        s_dialogOpening = false;
    }

    if (s_dialog == nullptr || ::common->Dialog().IsDialogActive())
    {
        FatalError("Common's dialog manager did not open the admitted dialog movie");
    }

    return s_movie;
}

void UpdateMenu()
{
    if (s_movie == nullptr || !swf::Active() || renderer::FrameStarted())
    {
        FatalError("native menu update outside its execution scope");
    }

    // idCommonLocal::GuiFrameEvents, then the handler's update. Its Render half is the
    // presentation's: the timeline advances after this, and drawing happens in the GS frame.
    ::game->Shell_SyncWithSession();
    ::game->Shell_Render();
}

void CloseMenu()
{
    if (s_movie == nullptr || renderer::FrameStarted())
    {
        FatalError("native menu release outside an idle presentation");
    }

    // Common's dialog manager deletes the dialog movie, and the handler its widgets and the shell;
    // the game keeps a fresh empty handler.
    {
        const swf::ExecutionScope scope("dialog");
        ::common->Dialog().Shutdown();
    }

    if (s_dialog != nullptr)
    {
        FatalError("Common's dialog manager kept the admitted dialog movie");
    }

    const swf::ExecutionScope scope("shell");
    DetachMenu(*s_movie);
    ::game->Shell_ResetMenu();
    s_movie = nullptr;
}

idSWF * MenuDialog()
{
    return s_dialog;
}

void UpdateMenuDialog(int time)
{
    if (s_dialog == nullptr || swf::Active() || renderer::FrameStarted())
    {
        FatalError("native menu dialog update outside an idle presentation");
    }

    // idCommonDialog::Render's state half drops cleared dialogs and shows the next one, which
    // activates the movie on this clock; its timeline then advances as the shell's does.
    const swf::ExecutionScope scope("dialog");
    ::common->Dialog().Render(false);
    s_dialog->AdvanceResourceExecution(time);
}

void SetMovieClock(int time)
{
    s_clock = time;
    s_clockSet = true;
}

void ClearMovieClock()
{
    s_clockSet = false;
    s_clock = 0;
}

int MovieClock()
{
    return s_clockSet ? s_clock : Sys_Milliseconds();
}

bool ProcessMenuEvent(const sysEvent_t * event)
{
    if (s_movie == nullptr)
    {
        return false;
    }

    if (s_opening || event == nullptr || renderer::FrameStarted() || s_events == UINT_MAX)
    {
        FatalError("native menu event outside an idle presentation");
    }

    ++s_events;

    // idCommonLocal::ProcessEvent: an active dialog takes every event before the menu.
    if (::common->Dialog().IsDialogActive())
    {
        const swf::ExecutionScope scope("dialog");
        ::common->Dialog().HandleDialogEvent(event);
        ++s_handled;
        return true;
    }

    // idCommonLocal::MenuEvent: the sign-in manager sees every event first, then the active shell.
    if (::session->GetSignInManager().ProcessInputEvent(event))
    {
        ++s_handled;
        return true;
    }

    const swf::ExecutionScope scope("shell");
    const bool handled = ::game->Shell_IsActive() && ::game->Shell_HandleGuiEvent(event);
    s_handled += handled ? 1U : 0U;
    return handled;
}

void MenuInputStatus()
{
    if (s_movie != nullptr)
    {
        bool quitShown = false;
        const bool quitQueued = ::common->Dialog().HasDialogMsg(GDM_QUIT_GAME, &quitShown);
        Log(LogLevel::Info, "[D3BFG] RETAIL menu input events=%u handled=%u dialog=%d quit=%d\n", s_events, s_handled,
            ::common->Dialog().IsDialogActive() ? 1 : 0, quitQueued ? (quitShown ? 2 : 1) : 0);
    }
}

idSWF * CreateMenuMovie(const char * name)
{
    if (!s_opening || s_movie != nullptr || name == nullptr || idStr::Cmp(name, "shell") != 0)
    {
        FatalError("native menu movie outside the admitted shell profile: %s", name != nullptr ? name : "(null)");
    }

    PrepareMovie(0, true, true);
    s_movie = new (TAG_SWF) idSWF("shell", nullptr, true);
    return s_movie;
}

idSWF * CreateDialogMovie()
{
    if (!s_dialogOpening || s_dialog != nullptr || !swf::Active() || renderer::FrameStarted())
    {
        FatalError("native dialog movie outside the menu's dialog manager");
    }

    // The dialog is shown on demand: its execution, drawing and key events are admitted while it
    // stays inactive, and showing a dialog activates it.
    PrepareMovie(1, true, true);
    s_dialog = new (TAG_SWF) idSWF("dialog", nullptr, true);
    s_dialog->EnableResourceExecution(false);
    s_dialog->EnableResourceRender();
    s_dialog->EnableResourceEvents();
    EnableMovieScreenBlending(1, "generated/swf/dialog.tga");
    AttachDialogChoices(*s_dialog);
    return s_dialog;
}

void PinDialogChoices(idSWF & movie)
{
    if (&movie != s_dialog || !swf::Active())
    {
        FatalError("native dialog choices outside the menu's dialog movie");
    }

    PinShownChoices(movie);
}

void ReleaseDialogMovie(idSWF * movie)
{
    // The manager's Init shuts down first, with no movie yet.
    if (movie == nullptr)
    {
        return;
    }

    if (movie != s_dialog || !swf::Active() || renderer::FrameStarted())
    {
        FatalError("native dialog movie release outside the menu's dialog manager");
    }

    DetachDialogChoices(*movie);
    s_dialog = nullptr;
}

void ActivateMenuMovie(idMenuHandler & handler, bool show)
{
    idSWF * const movie = handler.GetGUI();

    if (s_movie == nullptr || movie != s_movie || !swf::Active() || renderer::FrameStarted())
    {
        FatalError("native menu activation outside the admitted shell");
    }

    if (!show)
    {
        movie->Activate(false);
        return;
    }

    // Showing a hidden menu again installs fresh callbacks, which would need pinning anew; no
    // admitted screen hides the menu yet.
    if (movie->IsResourceExecution())
    {
        FatalError("native menu re-activation unavailable");
    }

    // The handler installed its callbacks; admit them, then start the checked execution, drawing and
    // key events.
    AttachMenu(handler);
    movie->EnableResourceExecution();
    movie->EnableResourceRender();
    movie->EnableResourceEvents();

    EnableMovieScreenBlending(0, "generated/swf/shell.tga");
}

} // namespace ps2::ui::retail
