// ================================================================================================
// File: presentation.cpp
// Brief: Present the native console without desktop frame services or advancing simulation on redraw.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/input/devices.h"
#include "ps2/renderer/capture.h"
#include "ps2/renderer/image_backend.h"
#include "ps2/renderer/render_system.h"
#include "ps2/renderer/scene.h"
#include "ps2/renderer/vram.h"
#include "ps2/system/filesystem.h"
#include "ps2/system/log.h"
#include "ps2/system/runtime.h"
#include "ps2/ui/draw.h"
#include "ps2/ui/font_backend.h"
#include "ps2/ui/assets.h"
#include "ps2/ui/movie.h"

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

// A visible package may open the native menu at startup, as idCommonLocal::Init does.
static bool s_bootMenu = false;
static bool s_configured = false;
static bool s_inFrame = false;
static bool s_drawing = false;
static int s_frames = 0;
static u32 s_redraws = 0;
static char s_captureName[64] = {};

// The world viewer (ps2_world): one fixed camera over Common's render world, drawn under the
// console and screens until the game draws its own views.
static bool s_worldView = false;
static renderView_t s_view;

// The display mode (RENDERING_ARCHITECTURE.md 4.2 and 4.3): the frame-buffer resolution, how each
// field reads it and the frame-buffer profile. The device starts in the cvars' values, and a change
// takes effect before the next frame begins.
// 640 wide is the fidelity target production ships with; 512 is the fallback.
static idCVar s_resolution("ps2_resolution", "1", CVAR_SYSTEM | CVAR_INTEGER,
                           "frame-buffer resolution, an index into the standard's list: 0 is 512x448 (NTSC) or 512x512 (PAL), 1 is 640 wide; "
                           "changes between frames",
                           0.0f, 15.0f);
static idCVar s_interlace("ps2_interlace", "field", CVAR_SYSTEM,
                          "how each field reads the frame buffer: field shows every other line of the full-height buffer, frame a half-height "
                          "buffer whole (SMODE2 FFMD); changes between frames");
static idCVar s_frameBufferBits("ps2_frameBufferBits", "32/32", CVAR_SYSTEM,
                                "frame-buffer draw/display bits in game: 16/16, 32/16 (32-bit drawing shown through a dithered "
                                "16-bit copy) or 32/32; the front end uses 32/16 for 32/32; changes between frames");

// The display mode the cvars ask for; the front end may draw it in a smaller profile (Effective).
static ps2::renderer::DisplayMode s_preferred;

// A render world loaded by ps2_world stands in for a game map until production loads maps.
static bool s_worldLoaded = false;

void RequireConsole(const idCmdArgs & args, int count)
{
    if (!ps2::system::runtime::HasPresentation() || args.Argc() != count) [[unlikely]]
    {
        ps2::FatalError("%s requires a visible console and %d argument(s)", args.Argv(0), count - 1);
    }
}

PS2_COLD_FUNC void ConsoleText(const idCmdArgs & args)
{
    RequireConsole(args, 2);

    const char * text = args.Argv(1);
    const size_t length = strlen(text);

    if (!console->Active() || length > 255) [[unlikely]]
    {
        ps2::FatalError("ps2_consoleText requires an active console and at most 255 ASCII characters");
    }

    // Validate the entire message before publishing events. Native console reserves grave and tilde.
    for (size_t index = 0; index < length; ++index)
    {
        if (text[index] < ' ' || text[index] > '~' || text[index] == '`' || text[index] == '~') [[unlikely]]
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

PS2_COLD_FUNC void ConsoleKey(const idCmdArgs & args)
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

PS2_COLD_FUNC void Capture(const idCmdArgs & args)
{
    RequireConsole(args, 2);

    const char * name = args.Argv(1);
    const size_t length = strlen(name);

    if (s_captureName[0] != '\0' || length <= 4 || length >= sizeof(s_captureName) ||
        strcmp(name + length - 4, ".tga") != 0) [[unlikely]]
    {
        ps2::FatalError("ps2_capture requires one pending capture and a basename ending in .tga");
    }

    for (size_t index = 0; index < length - 4; ++index)
    {
        const char ch = name[index];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')) [[unlikely]]
        {
            ps2::FatalError("ps2_capture requires an ASCII alphanumeric, dash or underscore basename");
        }
    }

    memcpy(s_captureName, name, length + 1);
}

PS2_COLD_FUNC void LogWorldStatus(const char * event)
{
    const ps2::renderer::scene::Stats stats = ps2::renderer::scene::GetStats();
    const idRenderWorld * world = common->RW();

    ps2::Log(ps2::LogLevel::Info, "[D3BFG] WORLD %s areas=%d portals=%d viewing=%d frame=%u views=%u area=%d entities=%u drawn=%u batches=%u arena=%u\n",
             event, world->NumAreas(), world->NumPortals(), static_cast<int>(s_worldView), static_cast<u32>(stats.frame),
             static_cast<u32>(stats.views), stats.area, static_cast<u32>(stats.entities), static_cast<u32>(stats.areas),
             static_cast<u32>(stats.batches), static_cast<u32>(stats.arenaBytes));
}

// Three command arguments from 'first' on as a point.
PS2_COLD_FUNC idVec3 Point(const idCmdArgs & args, int first)
{
    return idVec3(static_cast<float>(atof(args.Argv(first))), static_cast<float>(atof(args.Argv(first + 1))),
                  static_cast<float>(atof(args.Argv(first + 2))));
}

// ps2_world load MAP | unload | view EX EY EZ TX TY TZ | trace SX SY SZ EX EY EZ |
// portal HANDLE open|closed | off | status. A view looks from the eye at the target with a
// 90-degree vertical field of view; a trace reports the first world surface between two points.
PS2_COLD_FUNC void World(const idCmdArgs & args)
{
    if (!ps2::system::runtime::HasPresentation() || args.Argc() < 2) [[unlikely]]
    {
        ps2::FatalError("ps2_world requires a visible console and a subcommand");
    }

    const char * verb = args.Argv(1);
    idRenderWorld * world = common->RW();

    if (strcmp(verb, "load") == 0 && args.Argc() == 3)
    {
        s_worldView = false;

        if (!world->InitFromMap(args.Argv(2)))
        {
            s_worldLoaded = false;
            ps2::Log(ps2::LogLevel::Error, "[D3BFG] WORLD load failed map=%s\n", args.Argv(2));
            return;
        }

        s_worldLoaded = true;
        LogWorldStatus("loaded");
        return;
    }

    if (strcmp(verb, "unload") == 0 && args.Argc() == 2)
    {
        s_worldView = false;
        s_worldLoaded = false;
        world->InitFromMap(nullptr);
        LogWorldStatus("unloaded");
        return;
    }

    if (strcmp(verb, "view") == 0 && args.Argc() == 8)
    {
        const idVec3 eye = Point(args, 2);
        idVec3 forward = Point(args, 5) - eye;
        idVec3 left = idVec3(0.0f, 0.0f, 1.0f).Cross(forward);

        if (forward.Normalize() < 1.0f || left.Normalize() < 0.001f) [[unlikely]]
        {
            ps2::FatalError("ps2_world view needs a target at least one unit away and not straight up or down");
        }

        // id's camera looks down its forward axis, with left and up completing it.
        memset(static_cast<void *>(&s_view), 0, sizeof(s_view));
        s_view.vieworg = eye;
        s_view.viewaxis[0] = forward;
        s_view.viewaxis[1] = left;
        s_view.viewaxis[2] = forward.Cross(left);
        // A 90 degree vertical field of view on the TV's 4:3 picture: tan(fov_x / 2) = 4/3 x tan(45).
        s_view.fov_y = 90.0f;
        s_view.fov_x = RAD2DEG(2.0f * idMath::ATan(ps2::system::video::kDisplayAspect, 1.0f));
        s_worldView = true;
        return;
    }

    if (strcmp(verb, "trace") == 0 && args.Argc() == 8)
    {
        modelTrace_t trace = {};
        const bool hit = world->Trace(trace, Point(args, 2), Point(args, 5), 0.0f, true, false);

        ps2::Log(ps2::LogLevel::Info, "[D3BFG] WORLD trace hit=%d fraction=%.3f normal=%.3f,%.3f,%.3f material=%s\n", static_cast<int>(hit),
                 static_cast<double>(trace.fraction), static_cast<double>(trace.normal.x), static_cast<double>(trace.normal.y),
                 static_cast<double>(trace.normal.z), hit && trace.material != nullptr ? trace.material->GetName() : "none");
        return;
    }

    if (strcmp(verb, "portal") == 0 && args.Argc() == 4)
    {
        const int portal = atoi(args.Argv(2));
        const bool closed = strcmp(args.Argv(3), "closed") == 0;

        if (portal < 1 || portal > world->NumPortals() || (!closed && strcmp(args.Argv(3), "open") != 0)) [[unlikely]]
        {
            ps2::FatalError("ps2_world portal needs a handle from 1 to %d and open or closed", world->NumPortals());
        }

        world->SetPortalState(portal, closed ? PS_BLOCK_VIEW : PS_BLOCK_NONE);
        return;
    }

    if (strcmp(verb, "off") == 0 && args.Argc() == 2)
    {
        s_worldView = false;
        return;
    }

    if (strcmp(verb, "status") == 0 && args.Argc() == 2)
    {
        LogWorldStatus("status");
        return;
    }

    ps2::FatalError("unknown ps2_world use: %s", args.Args());
}

ps2::system::video::Standard PolicyStandard()
{
    return s_policy == Presentation::Pal ? ps2::system::video::Standard::Pal : ps2::system::video::Standard::Ntsc;
}

// The display mode the three cvars ask for, each refused with a warning and reset to 'mode's
// value when it names nothing the standard has. The cvars then hold the effective values.
PS2_COLD_FUNC void ReadDisplayCvars(ps2::system::video::Standard standard, ps2::renderer::DisplayMode & mode)
{
    const int resolution = s_resolution.GetInteger();
    const int count = ps2::system::video::ResolutionCount(standard);

    if (resolution < 0 || resolution >= count)
    {
        ps2::Log(ps2::LogLevel::Warning, "ps2_resolution accepts 0 to %d; keeping %d\n", count - 1, mode.resolution);
    }
    else
    {
        mode.resolution = resolution;
    }

    const char * interlace = s_interlace.GetString();

    if (idStr::Icmp(interlace, "field") == 0)
    {
        mode.interlace = ps2::system::video::Interlace::Field;
    }
    else if (idStr::Icmp(interlace, "frame") == 0)
    {
        mode.interlace = ps2::system::video::Interlace::Frame;
    }
    else
    {
        ps2::Log(ps2::LogLevel::Warning, "ps2_interlace accepts field or frame; keeping %s\n", ps2::system::video::InterlaceName(mode.interlace));
    }

    static constexpr ps2::renderer::gs::FrameFormat kFormats[] = {
        ps2::renderer::gs::FrameFormat::Color16, ps2::renderer::gs::FrameFormat::Color32Display16, ps2::renderer::gs::FrameFormat::Color32
    };
    bool found = false;

    // The console tokenizes 32/16 as three words and joins them with spaces: compare without them.
    idStr label = s_frameBufferBits.GetString();
    label.Replace(" ", "");

    for (const ps2::renderer::gs::FrameFormat format : kFormats)
    {
        if (label.Cmp(ps2::renderer::gs::FrameFormatLabel(format)) == 0)
        {
            mode.format = format;
            found = true;
        }
    }

    if (!found)
    {
        ps2::Log(ps2::LogLevel::Warning, "ps2_frameBufferBits accepts 16/16, 32/16 or 32/32; keeping %s\n",
                 ps2::renderer::gs::FrameFormatLabel(mode.format));
    }

    s_resolution.SetInteger(mode.resolution);
    s_interlace.SetString(mode.interlace == ps2::system::video::Interlace::Frame ? "frame" : "field");
    s_frameBufferBits.SetString(ps2::renderer::gs::FrameFormatLabel(mode.format));
    s_resolution.ClearModified();
    s_interlace.ClearModified();
    s_frameBufferBits.ClearModified();
}

PS2_COLD_FUNC void LogDisplayMode()
{
    const ps2::renderer::gs::FrameFormat format = ps2::renderer::gs::GetFrameFormat();

    ps2::Log(ps2::LogLevel::Info, "[D3BFG] GS display %dx%d %s bits=%s %s fixed_vram=%u texture_vram=%u\n", ps2::renderer::gs::Width(),
             ps2::renderer::gs::Height(), ps2::system::video::InterlaceName(ps2::renderer::gs::GetInterlace()),
             ps2::renderer::gs::FrameFormatLabel(format), ps2::renderer::gs::FrameFormatName(format), ps2::renderer::gs::FixedVramBytes(),
             static_cast<u32>(ps2::renderer::gs::GetVramStats().capacityBytes));
}

// A map is loaded: the game's, or a render world the viewer loaded.
bool InGame()
{
    return s_worldLoaded || (game != nullptr && game->IsInGame());
}

// The front end (no map loaded) draws the retail menus, whose textures do not fit beside two 640-wide
// 32-bit buffers (the quit dialog over the shell evicted every frame in NTSC's 1,327,104 bytes):
// it draws in 32 bits but shows a dithered 16-bit copy, which leaves 1,900,544 bytes NTSC and
// 1,572,864 PAL. A map switches to the preferred profile.
ps2::renderer::DisplayMode Effective(const ps2::renderer::DisplayMode & preferred)
{
    ps2::renderer::DisplayMode mode = preferred;

    if (!InGame() && mode.format == ps2::renderer::gs::FrameFormat::Color32)
    {
        mode.format = ps2::renderer::gs::FrameFormat::Color32Display16;
    }

    return mode;
}

// Applies the cvars and the front-end rule before a frame begins; a change restarts the GS.
void UpdateDisplayMode()
{
    if (s_resolution.IsModified() || s_interlace.IsModified() || s_frameBufferBits.IsModified()) [[unlikely]]
    {
        ReadDisplayCvars(PolicyStandard(), s_preferred);
    }

    const ps2::renderer::DisplayMode current = ps2::renderer::GetDisplayMode();
    const ps2::renderer::DisplayMode mode = Effective(s_preferred);

    if (mode.resolution != current.resolution || mode.interlace != current.interlace || mode.format != current.format) [[unlikely]]
    {
        ps2::renderer::SetDisplayMode(mode);
        LogDisplayMode();
    }
}

void Present()
{
    PS2_AssertMsg(!s_drawing && !ps2::renderer::FrameStarted(), "nested native console presentation");

    UpdateDisplayMode();

    s_drawing = true;

    ps2::ui::AdvanceScreen(s_inFrame);
    ps2::renderer::BeginFrame({ 16, 24, 40, 255 });
    ps2::renderer::scene::BeginFrame();
    {
        if (s_worldView)
        {
            common->RW()->RenderScene(&s_view);
        }

        renderSystem->SetGLState(0);
        renderSystem->SetColor(colorWhite);
        ps2::ui::RenderScreen(s_inFrame);
        console->Draw(false);
    }
    ps2::renderer::EndFrame();

    if (s_captureName[0] != '\0')
    {
        char path[sizeof(s_captureName) + 5];
        idStr::snPrintf(path, static_cast<int>(sizeof(path)), "host:%s", s_captureName);

        if (!ps2::renderer::gs::CaptureTga(path, ps2::renderer::LastPresentedContext())) [[unlikely]]
        {
            ps2::FatalError("native console capture failed: %s", s_captureName);
        }

        ps2::Log(ps2::LogLevel::Info, "[D3BFG] CONSOLE capture=%s frame=%d presents=%u redraws=%u bits=%d\n",
                 s_captureName, s_frames, ps2::renderer::FrameCount(), s_redraws,
                 ps2::renderer::gs::FrameFormatBits(ps2::renderer::gs::GetFrameFormat()));

        s_captureName[0] = '\0';
    }

    s_drawing = false;
}

} // namespace

namespace ps2::system::runtime
{

PS2_COLD_FUNC void ConfigurePresentation()
{
    if (s_configured) [[unlikely]]
    {
        FatalError("presentation policy already configured");
    }

    char policy[16] = {};
    errno = 0;
    FILE * file = system::fs::Open("host:presentation.cfg", system::fs::OpenMode::Read);

    if (file == nullptr)
    {
        // HostFs reports a missing file as -5/EIO in PCSX2; absence must keep old packages headless.
        if (errno != ENOENT && errno != EIO) [[unlikely]]
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

        if (!read || !closed) [[unlikely]]
        {
            FatalError("cannot read bounded presentation.cfg");
        }

        // Do not let an embedded NUL hide extra boot policy bytes.
        if (strlen(policy) != length) [[unlikely]]
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

        // An optional second word, "menu", opens the native menu at startup in a visible mode.
        char * const option = strchr(policy, ' ');
        if (option != nullptr)
        {
            *option = '\0';
            if (strcmp(option + 1, "menu") != 0 || strcmp(policy, "headless") == 0) [[unlikely]]
            {
                FatalError("presentation.cfg accepts only ntsc menu or pal menu");
            }
            s_bootMenu = true;
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
    Log(LogLevel::Info, "[D3BFG] BOOT presentation=%s menu=%d\n",
        s_policy == Presentation::Headless ? "headless" : s_policy == Presentation::Pal ? "pal" : "ntsc",
        s_bootMenu ? 1 : 0);
}

bool HasPresentation()
{
    return ui::IsInitialized();
}

PS2_COLD_FUNC void InitPresentation()
{
    if (!s_configured || HasPresentation()) [[unlikely]]
    {
        FatalError("invalid presentation startup");
    }

    if (s_policy == Presentation::Headless)
    {
        return;
    }

    renderer::Config config;
    config.standard = PolicyStandard();

    // No config file has run yet: the device starts in the cvars' defaults, as the front end draws
    // them, and later values apply at the frame boundary.
    s_preferred = {};
    s_preferred.format = renderer::gs::FrameFormat::Color32;
    ReadDisplayCvars(config.standard, s_preferred);
    config.display = Effective(s_preferred);

    if (!renderer::Init(config)) [[unlikely]]
    {
        FatalError("cannot initialize console display");
    }

    renderer::scene::Init();

    // PAL runs the engine at 50 Hz, so a 40 ms frame is two ticks as a 33 ms NTSC frame is two
    // at 60 (RENDERING_ARCHITECTURE.md 4.2). Both the cvar, which every map load latches from,
    // and the values already latched change. A console's standard never changes, so the archived
    // value is right for it.
    if (config.standard == system::video::Standard::Pal)
    {
        cvarSystem->SetCVarFloat("com_engineHz", 50.0f);
        com_engineHz_latched = 50.0f;
        com_engineHz_denominator = 100LL * 50LL;
    }

    if (!ui::Init()) [[unlikely]]
    {
        renderer::scene::Shutdown();
        renderer::Shutdown();
        FatalError("cannot initialize native console images");
    }

    // Every visible package stages the cooked retail catalog: the console draws the original
    // charset from it, and the retail screens open their fonts and movies through it later.
    ui::InitCatalog();
    ui::RegisterMaterials();
    renderer::OpenConsoleCharset();
    ui::fonts::Init();
    ui::InitScreen();

    cmdSystem->AddCommand("ps2_consoleText", ConsoleText, CMD_FL_SYSTEM, CMD_DESC("type quoted ASCII into the native console edit field"));
    cmdSystem->AddCommand("ps2_consoleKey", ConsoleKey, CMD_FL_SYSTEM, CMD_DESC("send a named native console key"));
    cmdSystem->AddCommand("ps2_capture", Capture, CMD_FL_SYSTEM, CMD_DESC("capture the next completed console frame to a HostFs .tga basename"));
    cmdSystem->AddCommand("ps2_world", World, CMD_FL_SYSTEM, CMD_DESC("load, view and query a render world under the console"));

    // The pad and the debug keyboard feed the same event queue the HostFs commands do; the
    // package stages the keyboard driver's modules beside the ELF.
    input::InitDevices("host:irx");
    SetEventGenerator(input::PollDevices);

    // idCommonLocal::Init creates the main menu and starts it before the startup commands run;
    // the menu closes the console, as StartMenu does.
    if (s_bootMenu)
    {
        ui::OpenBootMenu();
    }
}

void BeginPresentationFrame()
{
    if (!HasPresentation())
    {
        return;
    }
    PS2_AssertMsg(!s_inFrame && !s_drawing, "nested console Common frame");
    s_inFrame = true;
}

void PresentConsoleFrame(int frames)
{
    if (!HasPresentation())
    {
        return;
    }
    PS2_AssertMsg(s_inFrame && frames > s_frames, "invalid console frame boundary");

    s_frames = frames;
    Present();
    s_inFrame = false;
}

void UpdateScreen(bool captureToImage)
{
    PS2_AssertMsg(!captureToImage, "console offscreen image capture unavailable; use ps2_capture");

    if (!HasPresentation() || s_drawing)
    {
        return;
    }

    PS2_AssertMsg(s_redraws != UINT_MAX, "console redraw counter overflow");
    ++s_redraws;

    // Every Common frame presents. Enter/refresh-on-print requests coalesce into its final draw.
    // Outside a Common frame, a synchronous redraw preserves the clock and logical/game stamps.
    if (!s_inFrame)
    {
        Present();
    }
}

PS2_COLD_FUNC void LogPresentationStatus(int frames)
{
    if (!HasPresentation())
    {
        return;
    }

    Log(LogLevel::Info, "[D3BFG] CONSOLE frames=%d presents=%u redraws=%u active=%d\n",
        frames, renderer::FrameCount(), s_redraws, static_cast<int>(console->Active()));
}

PS2_COLD_FUNC void ShutdownPresentation(int frames)
{
    if (!HasPresentation())
    {
        return;
    }

    if (s_inFrame || s_drawing || s_captureName[0] != '\0') [[unlikely]]
    {
        FatalError("console shutdown with pending frame work");
    }

    Log(LogLevel::Info, "[D3BFG] CONSOLE shutdown complete frames=%d presents=%u redraws=%u\n",
        frames, renderer::FrameCount(), s_redraws);

    SetEventGenerator(nullptr);
    input::ShutdownDevices();
    cmdSystem->RemoveCommand("ps2_world");
    cmdSystem->RemoveCommand("ps2_capture");
    cmdSystem->RemoveCommand("ps2_consoleKey");
    cmdSystem->RemoveCommand("ps2_consoleText");
    ui::ShutdownScreen();
    ui::fonts::Shutdown();
    ui::Shutdown();
    renderer::scene::Shutdown();
    renderer::Shutdown();
}

} // namespace ps2::system::runtime
