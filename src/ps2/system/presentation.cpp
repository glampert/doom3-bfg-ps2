// ================================================================================================
// File: presentation.cpp
// Brief: Present the native console without desktop frame services or advancing simulation on redraw.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/capture.h"
#include "ps2/renderer/render_system.h"
#include "ps2/system/filesystem.h"
#include "ps2/system/log.h"
#include "ps2/system/runtime.h"
#include "ps2/ui/draw.h"
#include "ps2/ui/font_backend.h"
#include "ps2/ui/movie.h"
#include "ps2/ui/retail_movie.h"
#include <cerrno>
#include <idlib/precompiled.h>

namespace
{
enum class Presentation
{
    Headless,
    Ntsc,
    Pal
};
static Presentation s_policy = Presentation::Headless;
static bool s_configured = false;
static bool s_inFrame = false;
static bool s_drawing = false;
static int s_frames = 0;
static u32 s_redraws = 0;
static char s_captureName[64] = {};

void RequireConsole(const idCmdArgs & args, int count)
{
    if (!ps2::system::runtime::HasPresentation() || args.Argc() != count)
    {
        ps2::FatalError("%s requires a visible console and %d argument(s)", args.Argv(0), count - 1);
    }
}

void ConsoleText(const idCmdArgs & args)
{
    RequireConsole(args, 2);
    const char * text = args.Argv(1);
    const size_t length = strlen(text);
    if (!console->Active() || length > 255)
    {
        ps2::FatalError("ps2_consoleText requires an active console and at most 255 ASCII characters");
    }
    // Validate the entire message before publishing events. Native console reserves grave and tilde.
    for (size_t index = 0; index < length; ++index)
    {
        if (text[index] < ' ' || text[index] > '~' || text[index] == '`' || text[index] == '~')
        {
            ps2::FatalError("ps2_consoleText requires printable console ASCII");
        }
    }
    for (size_t index = 0; index < length; ++index)
    {
        Sys_QueEvent(SE_CHAR, text[index], 0, 0, nullptr, 0);
        // Drain per character, without recursively executing the command buffer.
        eventLoop->RunEventLoop(false);
    }
}

void ConsoleKey(const idCmdArgs & args)
{
    RequireConsole(args, 2);

    struct Key
    {
        const char * name;
        int code;
    };

    static constexpr Key kKeys[] = {
        { "enter", K_ENTER }, { "up", K_UPARROW }, { "down", K_DOWNARROW }, { "left", K_LEFTARROW }, { "right", K_RIGHTARROW }, { "toggle", K_GRAVE }, { "escape", K_ESCAPE }, { "home", K_HOME }, { "end", K_END }, { "delete", K_DEL }
    };
    if (strcmp(args.Argv(1), "backspace") == 0)
    {
        Sys_QueEvent(SE_CHAR, 8, 0, 0, nullptr, 0);
        eventLoop->RunEventLoop(false);
        return;
    }
    for (const auto & key : kKeys)
    {
        if (strcmp(args.Argv(1), key.name) == 0)
        {
            // Only validated named keys reach KeyInput's unchecked key-state array.
            Sys_QueEvent(SE_KEY, key.code, 1, 0, nullptr, 0);
            Sys_QueEvent(SE_KEY, key.code, 0, 0, nullptr, 0);
            eventLoop->RunEventLoop(false);
            return;
        }
    }
    ps2::FatalError("unknown ps2_consoleKey: %s", args.Argv(1));
}

void Capture(const idCmdArgs & args)
{
    RequireConsole(args, 2);
    const char * name = args.Argv(1);
    const size_t length = strlen(name);
    if (s_captureName[0] != '\0' || length <= 4 || length >= sizeof(s_captureName) ||
        strcmp(name + length - 4, ".tga") != 0)
    {
        ps2::FatalError("ps2_capture requires one pending capture and a basename ending in .tga");
    }
    for (size_t index = 0; index < length - 4; ++index)
    {
        const char ch = name[index];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '-' || ch == '_'))
        {
            ps2::FatalError("ps2_capture requires an ASCII alphanumeric, dash or underscore basename");
        }
    }
    memcpy(s_captureName, name, length + 1);
}

void Present()
{
    if (s_drawing || ps2::renderer::FrameStarted())
    {
        ps2::FatalError("nested native console presentation");
    }
    s_drawing = true;
    ps2::ui::retail::AdvanceScreen(s_inFrame);
    ps2::renderer::BeginFrame({ 16, 24, 40, 255 });
    renderSystem->SetGLState(0);
    renderSystem->SetColor(colorWhite);
    ps2::ui::authored::Render(s_inFrame);
    ps2::ui::retail::RenderScreen(s_inFrame);
    console->Draw(false);
    ps2::renderer::EndFrame();
    if (s_captureName[0] != '\0')
    {
        char path[sizeof(s_captureName) + 5];
        idStr::snPrintf(path, static_cast<int>(sizeof(path)), "host:%s", s_captureName);
        if (!ps2::renderer::gs::CaptureTga(path, ps2::renderer::LastPresentedContext()))
        {
            ps2::FatalError("native console capture failed: %s", s_captureName);
        }
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] CONSOLE capture=%s frame=%d presents=%u redraws=%u\n",
                 s_captureName, s_frames, ps2::renderer::FrameCount(), s_redraws);
        s_captureName[0] = '\0';
    }
    s_drawing = false;
}
} // namespace

namespace ps2::system::runtime
{
void ConfigurePresentation()
{
    if (s_configured)
    {
        FatalError("presentation policy already configured");
    }
    char policy[16] = {};
    errno = 0;
    FILE * file = system::filesystem::Open("host:presentation.cfg", system::filesystem::OpenMode::Read);
    if (file == nullptr)
    {
        // HostFs reports a missing file as -5/EIO in PCSX2; absence must keep old packages headless.
        if (errno != ENOENT && errno != EIO)
        {
            FatalError("cannot open presentation.cfg: errno=%d", errno);
        }
        Log(LogLevel::Warning, "presentation.cfg unavailable (errno=%d); defaulting to headless\n", errno);
    }
    else
    {
        const size_t length = fread(policy, 1, sizeof(policy) - 1, file);
        const bool read = ferror(file) == 0 && length < sizeof(policy) - 1;
        const bool closed = fclose(file) == 0;
        if (!read || !closed)
        {
            FatalError("cannot read bounded presentation.cfg");
        }
        // Do not let an embedded NUL hide extra boot policy bytes.
        if (strlen(policy) != length)
        {
            FatalError("invalid presentation.cfg");
        }
        size_t end = length;
        if (end != 0 && policy[end - 1] == '\n')
        {
            policy[--end] = '\0';
        }
        if (end != 0 && policy[end - 1] == '\r')
        {
            policy[--end] = '\0';
        }
        if (strcmp(policy, "headless") == 0)
        {
            s_policy = Presentation::Headless;
        }
        else if (strcmp(policy, "ntsc") == 0)
        {
            s_policy = Presentation::Ntsc;
        }
        else if (strcmp(policy, "pal") == 0)
        {
            s_policy = Presentation::Pal;
        }
        else
        {
            FatalError("presentation.cfg must select headless, ntsc or pal");
        }
    }
    s_configured = true;
    Log(LogLevel::Info, "[D3BFG] BOOT presentation=%s\n",
        s_policy == Presentation::Headless ? "headless" : s_policy == Presentation::Pal ? "pal"
                                                                                        : "ntsc");
}

bool HasPresentation()
{
    return ui::IsInitialized();
}

void InitPresentation()
{
    if (!s_configured || HasPresentation())
    {
        FatalError("invalid presentation startup");
    }
    if (s_policy == Presentation::Headless)
    {
        return;
    }
    renderer::Config config;
    config.standard = s_policy == Presentation::Pal ? system::video::Standard::Pal : system::video::Standard::Ntsc;
    if (!renderer::Init(config))
    {
        FatalError("cannot initialize console display");
    }
    if (!ui::Init())
    {
        renderer::Shutdown();
        FatalError("cannot initialize native console images");
    }
    ui::fonts::Init();
    ui::authored::Init();
    ui::retail::InitScreen();
    cmdSystem->AddCommand("ps2_consoleText", ConsoleText, CMD_FL_SYSTEM, "type quoted ASCII into the native console edit field");
    cmdSystem->AddCommand("ps2_consoleKey", ConsoleKey, CMD_FL_SYSTEM, "send a named native console key");
    cmdSystem->AddCommand("ps2_capture", Capture, CMD_FL_SYSTEM, "capture the next completed console frame to a HostFs .tga basename");
}

void BeginPresentationFrame()
{
    if (!HasPresentation())
    {
        return;
    }
    if (s_inFrame || s_drawing)
    {
        FatalError("nested console Common frame");
    }
    s_inFrame = true;
}

void PresentConsoleFrame(int frames)
{
    if (!HasPresentation())
    {
        return;
    }
    if (!s_inFrame || frames <= s_frames)
    {
        FatalError("invalid console frame boundary");
    }
    s_frames = frames;
    Present();
    s_inFrame = false;
}

void UpdateScreen(bool captureToImage)
{
    if (captureToImage)
    {
        FatalError("console offscreen image capture unavailable; use ps2_capture");
    }
    if (!HasPresentation() || s_drawing)
    {
        return;
    }
    if (s_redraws == UINT_MAX)
    {
        FatalError("console redraw counter overflow");
    }
    ++s_redraws;
    // Every Common frame presents. Enter/refresh-on-print requests coalesce into its final draw.
    // Outside a Common frame, a synchronous redraw preserves the clock and logical/game stamps.
    if (!s_inFrame)
    {
        Present();
    }
}

void LogPresentationStatus(int frames)
{
    if (!HasPresentation())
    {
        return;
    }
    Log(LogLevel::Info, "[D3BFG] CONSOLE frames=%d presents=%u redraws=%u active=%d\n",
        frames, renderer::FrameCount(), s_redraws, static_cast<int>(console->Active()));
}

void ShutdownPresentation(int frames)
{
    if (!HasPresentation())
    {
        return;
    }
    if (s_inFrame || s_drawing || s_captureName[0] != '\0')
    {
        FatalError("console shutdown with pending frame work");
    }
    Log(LogLevel::Info, "[D3BFG] CONSOLE shutdown complete frames=%d presents=%u redraws=%u\n",
        frames, renderer::FrameCount(), s_redraws);
    cmdSystem->RemoveCommand("ps2_capture");
    cmdSystem->RemoveCommand("ps2_consoleKey");
    cmdSystem->RemoveCommand("ps2_consoleText");
    ui::authored::Shutdown();
    ui::retail::ShutdownScreen();
    ui::fonts::Shutdown();
    ui::Shutdown();
    renderer::Shutdown();
}
} // namespace ps2::system::runtime
