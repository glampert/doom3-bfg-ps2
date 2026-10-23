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

    // The handler deletes its widgets and the movie; the game keeps a fresh empty handler.
    const swf::ExecutionScope scope("shell");
    DetachMenu(*s_movie);
    ::game->Shell_ResetMenu();
    s_movie = nullptr;
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

    // The handler installed its callbacks; admit them, then start the checked execution and drawing.
    AttachMenu(handler);
    movie->EnableResourceExecution();
    movie->EnableResourceRender();

    // Native idSWF names its atlas material after the binary movie; screen passes need its
    // per-channel CLUTs before the first frame.
    if (MovieUsesScreenBlending())
    {
        const idMaterial * const atlas = declManager->FindMaterial("generated/swf/shell.tga");

        if (atlas == nullptr || atlas->GetNumStages() != 1 || atlas->GetStage(0)->texture.image == nullptr)
        {
            FatalError("retail menu atlas image missing for screen blending");
        }

        renderer::EnableScreenBlending(atlas->GetStage(0)->texture.image);
    }
}
} // namespace ps2::ui::retail
