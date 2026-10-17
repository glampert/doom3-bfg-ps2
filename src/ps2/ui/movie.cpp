// ================================================================================================
// File: movie.cpp
// Brief: Exercise native SWF loading, scripts, timeline, font rendering and key focus in production.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/movie.h"
#include "ps2/renderer/image_backend.h"
#include "ps2/renderer/render_system.h"
#include "ps2/renderer/ui_draw.h"
#include "ps2/renderer/ui_page_image.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include "ps2/ui/authored_assets.h"
#include "ps2/ui/retail_assets.h"
#include <idlib/precompiled.h>
#include <swf/SWF.h>

namespace
{
static idSWF * s_movie = nullptr;
static bool s_initialized = false;
static u32 s_cycle = 0;
static u32 s_frames = 0;
static int s_time = 0;

void Status()
{
    const auto pages = ps2::render::GetUiPageStats();
    ps2::Log(ps2::LogLevel::Info,
             "[D3BFG] UI pages images=%u slots=%u cache=%u scratch=%u metadata=%u loads=%u hits=%u evictions=%u triangles=%u\n",
             pages.images, static_cast<u32>(pages.cacheBytes / ps2::ui::kPageBytes), pages.cacheBytes, pages.scratchBytes,
             pages.metadataBytes, pages.loads, pages.hits, pages.evictions, pages.triangles);
    const auto stats = ps2::heap::GetTotalStats();
    if (s_movie == nullptr)
    {
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] SWF state=unloaded cycle=%u heap=%zu/%zu/%zu\n",
                 s_cycle, stats.requestedBytes, stats.backingBytes, stats.allocationCount);
        return;
    }
    auto & root = s_movie->GetRootObject();
    idSWFSpriteInstance * const pulse = root.GetNestedSprite("pulse");
    if (pulse == nullptr)
    {
        ps2::FatalError("authored SWF pulse instance missing");
    }
    ps2::Log(ps2::LogLevel::Info,
             "[D3BFG] SWF state=loaded cycle=%u frames=%u pulse=%d focus=%d selected=%d selects=%d heap=%zu/%zu/%zu\n",
             s_cycle, s_frames, static_cast<int>(pulse->GetCurrentFrame()), root.Get("focusIndex").ToInteger(),
             root.Get("selected").ToInteger(), root.Get("selectCount").ToInteger(),
             stats.requestedBytes, stats.backingBytes, stats.allocationCount);
}

void OpenConsole()
{
    if (!console->Active())
    {
        const sysEvent_t open{ SE_KEY, K_GRAVE, 1, 0, nullptr, 0 };
        if (!console->ProcessEvent(&open, false))
        {
            ps2::FatalError("cannot reopen native console");
        }
    }
}

void Unload(bool restoreConsole)
{
    if (ps2::rs::FrameStarted())
    {
        ps2::FatalError("cannot unload native SWF during drawing");
    }
    delete s_movie;
    s_movie = nullptr;
    if (restoreConsole)
    {
        OpenConsole();
    }
    Status();
}

void Load()
{
    if (s_movie != nullptr || ps2::rs::FrameStarted() || s_cycle == UINT_MAX || ps2::ui::retail::IsInitialized())
    {
        ps2::FatalError("invalid authored SWF load ownership");
    }
    // Material declarations are already indexed by native DeclManager; admit their authored text too.
    delete ps2::ui::OpenAuthoredAsset("materials/ps2_ui.mtr");
    s_movie = new (TAG_SWF) idSWF("ps2_demo", nullptr);
    // Activation rebuilds frame-one children and executes their authored initialization actions.
    s_movie->Activate(true);
    s_movie->ForceInhibitControl(true);
    auto & root = s_movie->GetRootObject();
    const auto focus = root.Get("focusNext");
    const auto select = root.Get("select");
    auto keys = s_movie->GetGlobal("shortcutKeys");
    if (!s_movie->IsLoaded() || !focus.IsFunction() || !select.IsFunction() || !keys.IsObject() ||
        root.GetNestedSprite("pulse") == nullptr || root.GetNestedText("status") == nullptr)
    {
        ps2::FatalError("authored SWF native timeline or script contract missing");
    }
    // Native HandleEvent invokes the authored AVM1 functions; no host-side substitute handles focus.
    keys.GetObject()->Set("TAB", focus);
    keys.GetObject()->Set("ENTER", select);
    console->Close();
    ++s_cycle;
    s_frames = 0;
    s_time = Sys_Milliseconds();
    Status();
}

void Control(const idCmdArgs & args)
{
    if (!s_initialized || args.Argc() != 2)
    {
        ps2::FatalError("ps2_ui requires load, unload, reload, status, pause or resume");
    }
    const char * const action = args.Argv(1);
    if (strcmp(action, "status") == 0)
    {
        Status();
    }
    else if (strcmp(action, "load") == 0)
    {
        Load();
    }
    else if (strcmp(action, "unload") == 0)
    {
        Unload(true);
    }
    else if (strcmp(action, "reload") == 0)
    {
        Unload(false);
        Load();
    }
    else if (s_movie != nullptr && strcmp(action, "pause") == 0)
    {
        s_movie->Pause();
        Status();
    }
    else if (s_movie != nullptr && strcmp(action, "resume") == 0)
    {
        s_movie->Resume();
        Status();
    }
    else
    {
        ps2::FatalError("invalid authored SWF command/state: %s", action);
    }
}

void Key(const idCmdArgs & args)
{
    if (!s_initialized || s_movie == nullptr || console->Active() || args.Argc() != 2)
    {
        ps2::FatalError("ps2_uiKey requires an active focused SWF and one named key");
    }
    int code = 0;
    if (strcmp(args.Argv(1), "tab") == 0)
    {
        code = K_TAB;
    }
    else if (strcmp(args.Argv(1), "enter") == 0)
    {
        code = K_ENTER;
    }
    else
    {
        ps2::FatalError("unknown authored SWF key: %s", args.Argv(1));
    }
    Sys_QueEvent(SE_KEY, code, 1, 0, nullptr, 0);
    Sys_QueEvent(SE_KEY, code, 0, 0, nullptr, 0);
    eventLoop->RunEventLoop(false);
}
} // namespace

namespace ps2::movie
{
bool IsLoaded()
{
    return s_movie != nullptr;
}

void Init()
{
    if (s_initialized || !ui::IsInitialized())
    {
        FatalError("invalid authored SWF startup");
    }
    cmdSystem->AddCommand("ps2_ui", Control, CMD_FL_SYSTEM, "load/unload/reload/status/pause/resume the authored native SWF");
    cmdSystem->AddCommand("ps2_uiKey", Key, CMD_FL_SYSTEM, "send tab or enter through native SWF focus handling");
    s_initialized = true;
}

void Shutdown()
{
    if (!s_initialized)
    {
        return;
    }
    if (s_movie != nullptr)
    {
        Unload(false);
    }
    cmdSystem->RemoveCommand("ps2_uiKey");
    cmdSystem->RemoveCommand("ps2_ui");
    s_initialized = false;
}

bool ProcessEvent(const sysEvent_t * event)
{
    return s_initialized && s_movie != nullptr && s_movie->HandleEvent(event);
}

void Render(bool advance)
{
    if (s_movie == nullptr)
    {
        return;
    }
    if (advance)
    {
        if (s_frames == UINT_MAX)
        {
            FatalError("native SWF frame counter overflow");
        }
        s_time = Sys_Milliseconds();
        ++s_frames;
    }
    ui::BeginSwfDraw();
    s_movie->Render(renderSystem, s_time, false);
    ui::EndSwfDraw();
}
} // namespace ps2::movie
