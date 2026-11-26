// ================================================================================================
// File: presentation.cpp
// Brief: Present the native console without desktop frame services or advancing simulation on redraw.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/input/devices.h"
#include "ps2/input/pad.h"
#include "ps2/renderer/capture.h"
#include "ps2/renderer/image_backend.h"
#include "ps2/renderer/render_system.h"
#include "ps2/renderer/scene.h"
#include "ps2/renderer/vram.h"
#include "ps2/renderer/world.h"
#include "ps2/renderer/world_textures.h"
#include "ps2/system/aio.h"
#include "ps2/system/filesystem.h"
#include "ps2/system/frame_log.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include "ps2/system/runtime.h"
#include "ps2/ui/draw.h"
#include "ps2/ui/font_backend.h"
#include "ps2/ui/assets.h"
#include "ps2/ui/movie.h"

#include <cerrno>
#include <idlib/precompiled.h>
#include <libpad.h>

// The sound world's portal walk (idSoundWorldLocal::ResolveOrigin), for ps2_world sound.
#include <sound/snd_local.h>

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

// A Common frame's chain draws while the EE builds the next frame, which presents it: a frame takes
// the longer of the two rather than their sum, shown one frame later. Captures and redraws between
// frames present at once.
static idCVar s_pipelineFrames("ps2_pipelineFrames", "1", CVAR_SYSTEM | CVAR_BOOL,
                               "build the next frame while the GS draws this one (one frame more latency); 0 shows each frame "
                               "before the next begins");

// The display mode the cvars ask for; the front end may draw it in a smaller profile (Effective).
static ps2::renderer::DisplayMode s_preferred;

// A render world loaded by ps2_world stands in for a game map until production loads maps.
static bool s_worldLoaded = false;

// The map ps2_world loaded last and how long its load took, for the frame log's notes.
static idStrStatic<MAX_OSPATH> s_worldName;
static int s_worldLoadMs = 0;

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

// Starts the world view at 'eye' looking along 'axis' (forward, left, up) with a 90-degree vertical
// field of view on the TV's 4:3 picture: tan(fov_x / 2) = 4/3 x tan(45).
void SetView(const idVec3 & eye, const idMat3 & axis)
{
    memset(static_cast<void *>(&s_view), 0, sizeof(s_view));
    s_view.vieworg = eye;
    s_view.viewaxis = axis;
    s_view.fov_y = 90.0f;
    s_view.fov_x = RAD2DEG(2.0f * idMath::ATan(ps2::system::video::kDisplayAspect, 1.0f));
    s_worldView = true;
}

// ------------------------------------------------------------------------------------------------
// Camera path (ps2_world path)
// ------------------------------------------------------------------------------------------------

// A fixed camera path for the frame log: keyframes read from a file under base/, one per line as
// FRAMES X Y Z YAW PITCH (degrees, pitch positive looking down), each reached FRAMES frames after the
// one before by straight steps (yaw the short way round), 0 cutting to it. The path advances one
// keyframe step per Common frame, whatever the frame took, so every run draws the same frames;
// it plays 1 + the sum of max(FRAMES, 1) over the keyframes after the first.
struct PathKey
{
    idVec3 eye;
    float yaw;
    float pitch;
    int frames;
};

static constexpr int kMaxPathKeys = 4096;
static constexpr int kMaxPathSegmentFrames = 3600;

static PathKey * s_path = nullptr;
static int s_pathKeys = 0;
static int s_pathTarget = 0; // the keyframe the path moves toward
static int s_pathStep = 0;   // frames into the segment that ends there
static int s_pathFrame = 0;  // frames played
static int s_pathLimit = 0;  // frames to play
static bool s_pathDone = false;
// The frame the path starts in, from its command, draws its first pose without playing a frame: the
// path's frames are the ones after, which a frame log started with it records from their start.
static bool s_pathStarting = false;

PS2_COLD_FUNC void FreePath()
{
    ps2::system::heap::Free(s_path);
    s_path = nullptr;
    s_pathKeys = 0;
    s_pathDone = false;
}

PS2_COLD_FUNC void LogPath(const char * event)
{
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] WORLD path %s keys=%d frames=%d eye=%.2f,%.2f,%.2f\n", event, s_pathKeys, s_pathFrame,
             static_cast<double>(s_view.vieworg.x), static_cast<double>(s_view.vieworg.y), static_cast<double>(s_view.vieworg.z));
}

// The frames a path of 'count' keyframes plays.
PS2_COLD_FUNC int PathFrames(const PathKey * keys, int count)
{
    int frames = count > 0 ? 1 : 0;

    for (int index = 1; index < count; ++index)
    {
        frames += keys[index].frames > 0 ? keys[index].frames : 1;
    }

    return frames;
}

// Reads the keyframes of 'name' (relative to base/); false, logged, when the file is missing or
// malformed.
PS2_COLD_FUNC bool LoadPath(const char * name)
{
    void * buffer = nullptr;
    const int length = fileSystem->ReadFile(name, &buffer);

    if (length <= 0 || buffer == nullptr)
    {
        ps2::Log(ps2::LogLevel::Error, "[D3BFG] WORLD path cannot read %s\n", name);
        return false;
    }

    idLexer lexer(LEXFL_NOERRORS | LEXFL_NOWARNINGS | LEXFL_NOFATALERRORS);
    lexer.LoadMemory(static_cast<const char *>(buffer), length, name);
    auto * keys = static_cast<PathKey *>(ps2::system::heap::TryAlloc(sizeof(PathKey) * kMaxPathKeys, TAG_DEBUG));
    int count = 0;
    bool valid = keys != nullptr;
    idToken token;

    while (valid && lexer.ReadToken(&token))
    {
        if (count == kMaxPathKeys || token.type != TT_NUMBER || (token.subtype & TT_INTEGER) == 0) [[unlikely]]
        {
            valid = false;
            break;
        }

        PathKey & key = keys[count];
        key.frames = token.GetIntValue();
        key.eye.x = lexer.ParseFloat();
        key.eye.y = lexer.ParseFloat();
        key.eye.z = lexer.ParseFloat();
        key.yaw = lexer.ParseFloat();
        key.pitch = lexer.ParseFloat();
        valid = !lexer.HadError() && key.frames >= 0 && key.frames <= kMaxPathSegmentFrames && (count > 0 || key.frames == 0) &&
                idMath::Fabs(key.pitch) < 90.0f;
        ++count;
    }

    fileSystem->FreeFile(buffer);

    if (!valid || count < 2) [[unlikely]]
    {
        ps2::system::heap::Free(keys);
        ps2::Log(ps2::LogLevel::Error, "[D3BFG] WORLD path %s refused: keyframe %d is not FRAMES X Y Z YAW PITCH (the first with FRAMES 0, "
                 "at most %d frames and %d keyframes, pitch within 90 degrees) or the path has fewer than two\n",
                 name, count, kMaxPathSegmentFrames, kMaxPathKeys);
        return false;
    }

    FreePath();
    s_path = keys;
    s_pathKeys = count;
    return true;
}

// The pose of the path's next frame, before the view draws; the frame after the last ends it.
void UpdatePath()
{
    if (s_pathStarting)
    {
        s_pathStarting = false;
        return;
    }

    if (s_pathDone)
    {
        LogPath("done");
        const int frames = s_pathFrame;
        FreePath();

        // The console shows how the run ended: a console's only screen.
        if (ps2::system::frame_log::Recording())
        {
            const bool written = ps2::system::frame_log::Stop();
            common->Printf("Camera path done after %d frames; frame log %s framelog.txt in %s\n", frames, written ? "written to" : "NOT written to",
                           ps2::system::runtime::PackageRoot());
        }
        else
        {
            common->Printf("Camera path done after %d frames\n", frames);
        }

        return;
    }

    idVec3 eye = s_path[0].eye;
    float yaw = s_path[0].yaw;
    float pitch = s_path[0].pitch;

    if (s_pathFrame > 0)
    {
        const PathKey & from = s_path[s_pathTarget - 1];
        const PathKey & to = s_path[s_pathTarget];
        const int frames = to.frames > 0 ? to.frames : 1;
        ++s_pathStep;
        const float t = static_cast<float>(s_pathStep) / static_cast<float>(frames);
        eye = from.eye + t * (to.eye - from.eye);
        yaw = from.yaw + t * idMath::AngleNormalize180(to.yaw - from.yaw);
        pitch = from.pitch + t * (to.pitch - from.pitch);

        if (s_pathStep == frames)
        {
            ++s_pathTarget;
            s_pathStep = 0;
        }
    }

    s_view.vieworg = eye;
    s_view.viewaxis = idAngles(pitch, yaw, 0.0f).ToMat3();
    ++s_pathFrame;
    s_pathDone = s_pathTarget == s_pathKeys || s_pathFrame == s_pathLimit;
}

// ------------------------------------------------------------------------------------------------
// Debug free-flight camera (ps2_world fly)
// ------------------------------------------------------------------------------------------------

#if PS2_D3BFG_DEBUG

// Walks through walls: it moves the viewer's camera, not a player. The left stick moves along the
// view and sideways, the right stick turns (no inversion), R1 and L1 rise and sink, cross holds 4x
// speed and square a quarter, and triangle logs the pose as a ps2_world view line.
static constexpr float kFlySpeed = 256.0f;    // world units a second at full deflection
static constexpr float kFlyTurnRate = 120.0f; // degrees a second at full deflection
static constexpr int kFlyDeadZone = 24;       // of the stick's 128
static constexpr float kFlyMaxPitch = 89.0f;

// A frame longer than this (a load, a GS restart) moves the camera as if it took this long.
static constexpr float kFlyMaxSeconds = 0.1f;

// Scripted input (ps2_world fly input) advances this much each frame, whatever the frame took, so
// a gate can predict the pose.
static constexpr float kFlyScriptSeconds = 1.0f / 30.0f;

struct FlyInput
{
    int leftX = 0;
    int leftY = 0;
    int rightX = 0;
    int rightY = 0;
    u16 buttons = 0;
};

static bool s_fly = false;
static float s_flyYaw = 0.0f;
static float s_flyPitch = 0.0f;
static u64 s_flyLastMicroseconds = 0;
static bool s_flyReportHeld = false;
static FlyInput s_flyScript;
static int s_flyScriptFrames = 0;

// A stick's deflection past the dead zone, rescaled to [-1, 1].
float FlyAxis(int value)
{
    if (value > kFlyDeadZone)
    {
        return idMath::ClampFloat(0.0f, 1.0f, static_cast<float>(value - kFlyDeadZone) / static_cast<float>(128 - kFlyDeadZone));
    }

    if (value < -kFlyDeadZone)
    {
        return idMath::ClampFloat(-1.0f, 0.0f, static_cast<float>(value + kFlyDeadZone) / static_cast<float>(128 - kFlyDeadZone));
    }

    return 0.0f;
}

PS2_COLD_FUNC void LogFly(const char * event)
{
    const idVec3 & eye = s_view.vieworg;
    const idVec3 target = eye + 100.0f * s_view.viewaxis[0];

    ps2::Log(ps2::LogLevel::Info, "[D3BFG] WORLD fly %s on=%d eye=%.2f,%.2f,%.2f yaw=%.2f pitch=%.2f view=%.0f %.0f %.0f %.0f %.0f %.0f\n", event,
             static_cast<int>(s_fly), static_cast<double>(eye.x), static_cast<double>(eye.y), static_cast<double>(eye.z),
             static_cast<double>(s_flyYaw), static_cast<double>(s_flyPitch), static_cast<double>(eye.x), static_cast<double>(eye.y),
             static_cast<double>(eye.z), static_cast<double>(target.x), static_cast<double>(target.y), static_cast<double>(target.z));
}

PS2_COLD_FUNC void StartFly()
{
    const idAngles angles = s_view.viewaxis[0].ToAngles();
    s_flyYaw = idMath::AngleNormalize360(angles.yaw);
    s_flyPitch = idMath::ClampFloat(-kFlyMaxPitch, kFlyMaxPitch, idMath::AngleNormalize180(angles.pitch));
    s_flyLastMicroseconds = Sys_Microseconds();
    s_flyReportHeld = false;
    s_flyScriptFrames = 0;
    s_fly = true;
    // The pad steers the camera; its buttons must not also reach the console or a screen.
    ps2::input::pad::SetKeyEvents(false);
}

void StopFly()
{
    if (!s_fly)
    {
        return;
    }

    s_fly = false;
    s_flyScriptFrames = 0;
    ps2::input::pad::SetKeyEvents(true);
}

// One frame of flight, from the pad or the scripted input, before the view draws.
void UpdateFly()
{
    const u64 now = Sys_Microseconds();
    FlyInput input;
    float seconds = 0.0f;

    if (s_flyScriptFrames > 0)
    {
        input = s_flyScript;
        seconds = kFlyScriptSeconds;
        --s_flyScriptFrames;
    }
    else
    {
        const ps2::input::pad::Stats & pad = ps2::input::pad::GetStats();
        input.leftX = pad.leftX;
        input.leftY = pad.leftY;
        input.rightX = pad.rightX;
        input.rightY = pad.rightY;
        input.buttons = pad.buttons;
        seconds = idMath::ClampFloat(0.0f, kFlyMaxSeconds, static_cast<float>(now - s_flyLastMicroseconds) * 1.0e-6f);
    }

    s_flyLastMicroseconds = now;

    // Turn first, then move along the new axis.
    s_flyYaw = idMath::AngleNormalize360(s_flyYaw - FlyAxis(input.rightX) * kFlyTurnRate * seconds);
    s_flyPitch = idMath::ClampFloat(-kFlyMaxPitch, kFlyMaxPitch, s_flyPitch + FlyAxis(input.rightY) * kFlyTurnRate * seconds);

    const idMat3 axis = idAngles(s_flyPitch, s_flyYaw, 0.0f).ToMat3();
    float speed = kFlySpeed * seconds;

    if ((input.buttons & PAD_CROSS) != 0)
    {
        speed *= 4.0f;
    }

    if ((input.buttons & PAD_SQUARE) != 0)
    {
        speed *= 0.25f;
    }

    const float rise = ((input.buttons & PAD_R1) != 0 ? 1.0f : 0.0f) - ((input.buttons & PAD_L1) != 0 ? 1.0f : 0.0f);
    const idVec3 move = -FlyAxis(input.leftY) * axis[0] - FlyAxis(input.leftX) * axis[1] + idVec3(0.0f, 0.0f, rise);

    s_view.vieworg += speed * move;
    s_view.viewaxis = axis;

    const bool report = (input.buttons & PAD_TRIANGLE) != 0;

    if (report && !s_flyReportHeld)
    {
        LogFly("pose");
    }

    s_flyReportHeld = report;
}

// ps2_world fly on | off | status | input LX LY RX RY BUTTONS FRAMES: the input line holds the
// sticks (-128 to 127) and a libpad button mask for FRAMES frames of kFlyScriptSeconds each.
PS2_COLD_FUNC void Fly(const idCmdArgs & args)
{
    const char * verb = args.Argc() >= 3 ? args.Argv(2) : "";

    if (strcmp(verb, "on") == 0 && args.Argc() == 3)
    {
        if (!s_worldView) [[unlikely]]
        {
            ps2::FatalError("ps2_world fly on starts from a view: set one with ps2_world view first");
        }

        StartFly();
        LogFly("on");
        return;
    }

    if (strcmp(verb, "off") == 0 && args.Argc() == 3)
    {
        StopFly();
        LogFly("off");
        return;
    }

    if (strcmp(verb, "status") == 0 && args.Argc() == 3)
    {
        LogFly("status");
        return;
    }

    if (strcmp(verb, "input") == 0 && args.Argc() == 9)
    {
        int sticks[4] = {};
        bool sticksValid = true;

        for (int index = 0; index < 4; ++index)
        {
            sticks[index] = atoi(args.Argv(3 + index));
            sticksValid = sticksValid && sticks[index] >= -128 && sticks[index] <= 127;
        }

        const int buttons = atoi(args.Argv(7));
        const int frames = atoi(args.Argv(8));

        if (!s_fly || !sticksValid || buttons < 0 || buttons > 0xFFFF || frames < 1 || frames > 600) [[unlikely]]
        {
            ps2::FatalError("ps2_world fly input needs flight on, sticks from -128 to 127, a 16-bit button mask and 1 to 600 frames");
        }

        s_flyScript.leftX = sticks[0];
        s_flyScript.leftY = sticks[1];
        s_flyScript.rightX = sticks[2];
        s_flyScript.rightY = sticks[3];
        s_flyScript.buttons = static_cast<u16>(buttons);
        s_flyScriptFrames = frames;
        return;
    }

    ps2::FatalError("ps2_world fly needs on, off, status or input LX LY RX RY BUTTONS FRAMES");
}

#else // PS2_D3BFG_DEBUG

void StopFly()
{
}

#endif // PS2_D3BFG_DEBUG

// ------------------------------------------------------------------------------------------------
// World queries (ps2_world area, connected, sound, loader, path)
// ------------------------------------------------------------------------------------------------

static constexpr int kMaxAreaPoints = 16;

// PointInArea for each point of the command (X Y Z from argument 2 on), on one line.
PS2_COLD_FUNC void LogAreas(const idRenderWorld & world, const idCmdArgs & args)
{
    const int points = (args.Argc() - 2) / 3;
    char areas[kMaxAreaPoints * 8] = {};
    int used = 0;

    for (int point = 0; point < points; ++point)
    {
        used += idStr::snPrintf(areas + used, static_cast<int>(sizeof(areas)) - used, point == 0 ? "%d" : ",%d",
                                world.PointInArea(Point(args, 2 + 3 * point)));
    }

    ps2::Log(ps2::LogLevel::Info, "[D3BFG] WORLD area points=%d areas=%s\n", points, areas);
}

// The areas AreasAreConnected joins for each portal attribute, as each area's group: the lowest
// area it is connected to. A line holds 64 areas.
PS2_COLD_FUNC void LogConnectedAreas(const idRenderWorld & world)
{
    struct Kind
    {
        const char * name;
        portalConnection_t connection;
    };

    static constexpr Kind kKinds[] = { { "view", PS_BLOCK_VIEW }, { "location", PS_BLOCK_LOCATION }, { "air", PS_BLOCK_AIR } };
    static constexpr int kLineAreas = 64;
    const int areas = world.NumAreas();

    for (const Kind & kind : kKinds)
    {
        for (int first = 0; first < areas; first += kLineAreas)
        {
            char groups[kLineAreas * 6] = {};
            int used = 0;

            for (int area = first; area < areas && area < first + kLineAreas; ++area)
            {
                int group = area;

                for (int other = 0; other < area; ++other)
                {
                    if (world.AreasAreConnected(area, other, kind.connection))
                    {
                        group = other;
                        break;
                    }
                }

                used += idStr::snPrintf(groups + used, static_cast<int>(sizeof(groups)) - used, area == first ? "%d" : ",%d", group);
            }

            ps2::Log(ps2::LogLevel::Info, "[D3BFG] WORLD connected kind=%s first=%d groups=%s\n", kind.name, first, groups);
        }
    }
}

// The distance the sound world gives a sound at 'source' heard from 'listener', as an emitter's
// Update works it out: the straight line in the listener's area, otherwise the shortest walk through
// the portals (ResolveOrigin), each closed one adding s_doorDistanceAdd, or 'maxDistance' when no
// walk is shorter. Units, not the emitter's metres.
PS2_COLD_FUNC void LogSoundPath(const idVec3 & listener, const idVec3 & source, float maxDistance)
{
    if (!(maxDistance > 0.0f)) [[unlikely]]
    {
        ps2::FatalError("ps2_world sound needs a positive maximum distance");
    }

    // The one idSoundWorld; an emitter of its own on the stack holds the walk's result alone.
    auto * soundWorld = static_cast<idSoundWorldLocal *>(common->SW());
    soundWorld->PlaceListener(listener, mat3_identity, 0);
    idSoundEmitterLocal emitter;
    const int listenerArea = soundWorld->listener.area;
    const int sourceArea = soundWorld->renderWorld != nullptr ? soundWorld->renderWorld->PointInArea(source) : -1;
    emitter.spatializedDistance = (source - listener).LengthFast();
    emitter.spatializedOrigin = source;

    if (listenerArea != -1 && sourceArea != -1 && sourceArea != listenerArea)
    {
        emitter.spatializedDistance = maxDistance;
        soundWorld->ResolveOrigin(0, nullptr, sourceArea, 0.0f, source, &emitter);
    }

    const idVec3 & origin = emitter.spatializedOrigin;
    ps2::Log(ps2::LogLevel::Info,
             "[D3BFG] WORLD sound listener_area=%d source_area=%d direct=%.2f spatialized=%.2f origin=%.2f,%.2f,%.2f door_add=%.2f\n",
             listenerArea, sourceArea, static_cast<double>((source - listener).LengthFast()), static_cast<double>(emitter.spatializedDistance),
             static_cast<double>(origin.x), static_cast<double>(origin.y), static_cast<double>(origin.z),
             static_cast<double>(cvarSystem->GetCVarFloat("s_doorDistanceAdd")));
}

// The background loader's totals since boot, and its read rate: bytes over the time spent inside the
// reads and seeks.
PS2_COLD_FUNC void LogLoader()
{
    const ps2::system::aio::Stats stats = ps2::system::aio::GetStats();
    const u64 microseconds = stats.ioCycles * 1000000U / ps2::system::aio::kCyclesPerSecond;

    ps2::Log(ps2::LogLevel::Info, "[D3BFG] LOADER bytes=%u chunks=%u seeks=%u io_us=%llu kib_per_s=%llu queue=%u device=%s\n", stats.bytes,
             stats.chunks, stats.seeks, static_cast<unsigned long long>(microseconds),
             static_cast<unsigned long long>(microseconds != 0 ? static_cast<u64>(stats.bytes) * 1000000U / 1024U / microseconds : 0U),
             stats.queueDepth, ps2::system::runtime::PackageRoot());
}

// Plays the path in 'name' from its first keyframe, 'limit' frames of it (all when 0).
PS2_COLD_FUNC void StartPath(const char * name, int limit)
{
    StopFly();

    if (limit < 0 || !LoadPath(name))
    {
        ps2::Log(ps2::LogLevel::Error, "[D3BFG] WORLD path not started: %s\n", name);
        return;
    }

    const int frames = PathFrames(s_path, s_pathKeys);
    SetView(s_path[0].eye, idAngles(s_path[0].pitch, s_path[0].yaw, 0.0f).ToMat3());
    s_pathTarget = 1;
    s_pathStep = 0;
    s_pathFrame = 0;
    s_pathLimit = limit > 0 && limit < frames ? limit : frames;
    s_pathDone = false;
    s_pathStarting = true;
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] WORLD path started file=%s keys=%d frames=%d playing=%d\n", name, s_pathKeys, frames, s_pathLimit);
}

// ps2_world load MAP | unload | view EX EY EZ TX TY TZ | trace SX SY SZ EX EY EZ |
// decal OX OY OZ DX DY DZ SIZE DEPTH MATERIAL | decals | portal HANDLE open|closed | area X Y Z... |
// connected | sound LX LY LZ SX SY SZ MAX | loader | path FILE [FRAMES] | off | status, and in debug
// builds fly (the free-flight camera above). The queries (area, connected, sound) answer as the
// game's would: PointInArea, AreasAreConnected and the sound world's portal walk.
// A view looks from the eye at the target with a 90-degree vertical field of view; a trace reports
// the first world surface between two points. A decal projects a SIZE square of MATERIAL along
// the direction D onto the surfaces around O, as the game's idGameLocal::ProjectDecal does it
// unrotated and parallel; the entities build it the next time a view sees them, and decals
// reports what they hold.
PS2_COLD_FUNC void World(const idCmdArgs & args)
{
    if (!ps2::system::runtime::HasPresentation() || args.Argc() < 2) [[unlikely]]
    {
        ps2::FatalError("ps2_world requires a visible console and a subcommand");
    }

    const char * verb = args.Argv(1);
    idRenderWorld * world = common->RW();

#if PS2_D3BFG_DEBUG
    if (strcmp(verb, "fly") == 0)
    {
        Fly(args);
        return;
    }
#endif // PS2_D3BFG_DEBUG

    if (strcmp(verb, "load") == 0 && args.Argc() == 3)
    {
        s_worldView = false;
        StopFly();
        FreePath();

        const int start = Sys_Milliseconds();

        if (!world->InitFromMap(args.Argv(2)))
        {
            s_worldLoaded = false;
            ps2::Log(ps2::LogLevel::Error, "[D3BFG] WORLD load failed map=%s\n", args.Argv(2));
            return;
        }

        s_worldLoaded = true;
        s_worldName = args.Argv(2);
        s_worldLoadMs = Sys_Milliseconds() - start;
        LogWorldStatus("loaded");
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] WORLD load_time map=%s ms=%d\n", s_worldName.c_str(), s_worldLoadMs);
        return;
    }

    if (strcmp(verb, "unload") == 0 && args.Argc() == 2)
    {
        s_worldView = false;
        StopFly();
        FreePath();
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
        FreePath();
        SetView(eye, idMat3(forward, left, forward.Cross(left)));

#if PS2_D3BFG_DEBUG
        // A view set while flying moves the flight there.
        if (s_fly)
        {
            StartFly();
        }
#endif // PS2_D3BFG_DEBUG

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

    if (strcmp(verb, "decal") == 0 && args.Argc() == 11)
    {
        const idVec3 origin = Point(args, 2);
        idVec3 direction = Point(args, 5);
        const float half = 0.5f * static_cast<float>(atof(args.Argv(8)));
        const float depth = static_cast<float>(atof(args.Argv(9)));
        const idMaterial * material = declManager->FindMaterial(args.Argv(10), false);

        if (direction.Normalize() < 0.001f || !(half > 0.0f) || !(depth > 0.0f) || material == nullptr) [[unlikely]]
        {
            ps2::FatalError("ps2_world decal needs a direction, a positive size and depth, and a declared material");
        }

        idVec3 across;
        idVec3 down;
        direction.NormalVectors(across, down);

        // idGameLocal::ProjectDecal at angle 0: axis 0 is the first normal vector, axis 1 the second negated.
        const idVec3 windingOrigin = origin + depth * direction;
        idFixedWinding winding;
        winding += idVec5(windingOrigin + (across - down) * half, idVec2(1.0f, 1.0f));
        winding += idVec5(windingOrigin + (-across - down) * half, idVec2(0.0f, 1.0f));
        winding += idVec5(windingOrigin + (-across + down) * half, idVec2(0.0f, 0.0f));
        winding += idVec5(windingOrigin + (across + down) * half, idVec2(1.0f, 0.0f));
        world->ProjectDecalOntoWorld(winding, origin - depth * direction, true, depth * 0.5f, material, s_view.time[0]);
        return;
    }

    if (strcmp(verb, "decals") == 0 && args.Argc() == 2)
    {
        const ps2::renderer::scene::DecalStats decals = ps2::renderer::scene::MeasureDecals(*world);

        ps2::Log(ps2::LogLevel::Info,
                 "[D3BFG] WORLD decals entities=%u decals=%u vertices=%u triangles=%u area=%.2f bounds=%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
                 static_cast<u32>(decals.entities), static_cast<u32>(decals.decals), static_cast<u32>(decals.vertices),
                 static_cast<u32>(decals.triangles), static_cast<double>(decals.area), static_cast<double>(decals.boundsMin[0]),
                 static_cast<double>(decals.boundsMin[1]), static_cast<double>(decals.boundsMin[2]), static_cast<double>(decals.boundsMax[0]),
                 static_cast<double>(decals.boundsMax[1]), static_cast<double>(decals.boundsMax[2]));
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

        // A closed door blocks everything (idMover_Binary, func_portal): the view, locations and air.
        world->SetPortalState(portal, closed ? PS_BLOCK_ALL : PS_BLOCK_NONE);
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] WORLD portal handle=%d blocking=%d\n", portal, world->GetPortalState(portal));
        return;
    }

    if (strcmp(verb, "area") == 0 && args.Argc() >= 5 && (args.Argc() - 2) % 3 == 0 && args.Argc() - 2 <= 3 * kMaxAreaPoints)
    {
        LogAreas(*world, args);
        return;
    }

    if (strcmp(verb, "connected") == 0 && args.Argc() == 2)
    {
        LogConnectedAreas(*world);
        return;
    }

    if (strcmp(verb, "sound") == 0 && args.Argc() == 9)
    {
        LogSoundPath(Point(args, 2), Point(args, 5), static_cast<float>(atof(args.Argv(8))));
        return;
    }

    if (strcmp(verb, "loader") == 0 && args.Argc() == 2)
    {
        LogLoader();
        return;
    }

    if (strcmp(verb, "path") == 0 && (args.Argc() == 3 || args.Argc() == 4))
    {
        StartPath(args.Argv(2), args.Argc() == 4 ? atoi(args.Argv(3)) : 0);
        return;
    }

    if (strcmp(verb, "off") == 0 && args.Argc() == 2)
    {
        s_worldView = false;
        StopFly();
        FreePath();
        return;
    }

    if (strcmp(verb, "status") == 0 && args.Argc() == 2)
    {
        LogWorldStatus("status");
        return;
    }

    if (strcmp(verb, "textures") == 0 && args.Argc() == 3)
    {
        const bool on = strcmp(args.Argv(2), "on") == 0;

        if (!on && strcmp(args.Argv(2), "off") != 0 && strcmp(args.Argv(2), "status") != 0) [[unlikely]]
        {
            ps2::FatalError("ps2_world textures needs on, off or status");
        }

        if (strcmp(args.Argv(2), "status") != 0)
        {
            ps2::renderer::world::SetTexturesEnabled(on);
            return;
        }

        const ps2::renderer::world::TextureStats & stats = ps2::renderer::world::LastPassTextureStats();
        ps2::Log(ps2::LogLevel::Info,
                 "[D3BFG] WORLD textures enabled=%d arena=%u resident=%u resident_bytes=%u uploads=%u evictions=%u fallbacks=%u "
                 "lightmaps=%d pages=%u unlit=%u passes=%u pass_fallbacks=%u sky_faces=%u sky_fallbacks=%u repacks=%u "
                 "stream_loads=%u stream_bytes=%u copies=%u copy_bytes=%u copy_peak=%u stream_failures=%u stream_misses=%u "
                 "stream_waits=%u stream_wait_ms=%u overflows=%u pass_evictions=%u moved_states=%u moved_fallbacks=%u upload_bytes=%u "
                 "vram_yields=%u yielded_bytes=%u regrows=%u stream_reads=%u\n",
                 ps2::renderer::world::TexturesEnabled() ? 1 : 0, stats.arenaBytes, stats.resident, stats.residentBytes, stats.uploads,
                 stats.evictions, stats.untexturedFallbacks, ps2::renderer::world::LightmapsEnabled() ? 1 : 0, stats.pagesResident,
                 stats.unlitFallbacks, stats.passes, stats.passFallbacks, stats.skyFaces, stats.skyFallbacks, stats.repacks, stats.streamLoads,
                 stats.streamBytes, stats.copies, stats.copyBytes, stats.copyPeakBytes, stats.streamFailures, stats.streamMisses,
                 stats.streamWaits, stats.streamWaitMs, stats.overflows, stats.passEvictions, stats.movedStates, stats.movedFallbacks,
                 stats.uploadBytes, stats.vramYields, stats.yieldedBytes, stats.regrows, stats.streamReads);
        return;
    }

    if (strcmp(verb, "lightmaps") == 0 && args.Argc() == 3)
    {
        const bool on = strcmp(args.Argv(2), "on") == 0;

        if (!on && strcmp(args.Argv(2), "off") != 0) [[unlikely]]
        {
            ps2::FatalError("ps2_world lightmaps needs on or off");
        }

        ps2::renderer::world::SetLightmapsEnabled(on);
        return;
    }

    ps2::FatalError("unknown ps2_world use: %s", args.Args());
}

ps2::system::video::Standard PolicyStandard()
{
    return s_policy == Presentation::Pal ? ps2::system::video::Standard::Pal : ps2::system::video::Standard::Ntsc;
}

// ps2_framelog start [FRAMES] | stop | status: records a row per Common frame (FRAMES of them, 3600
// when left out) and writes them to the package's framelog.txt when it stops, is full, or the camera
// path it records ends (frame_log.h).
PS2_COLD_FUNC void FrameLog(const idCmdArgs & args)
{
    const char * verb = args.Argc() >= 2 ? args.Argv(1) : "";

    if (strcmp(verb, "start") == 0 && (args.Argc() == 2 || args.Argc() == 3))
    {
        const int frames = args.Argc() == 3 ? atoi(args.Argv(2)) : 3600;
        char label[160];
        idStr::snPrintf(label, sizeof(label), "map=%s load_ms=%d standard=%s", s_worldLoaded ? s_worldName.c_str() : "none",
                        s_worldLoaded ? s_worldLoadMs : 0, ps2::system::video::Name(PolicyStandard()));
        ps2::system::frame_log::Start(frames > 0 ? static_cast<u32>(frames) : 0U, label);
        return;
    }

    if (strcmp(verb, "stop") == 0 && args.Argc() == 2)
    {
        ps2::system::frame_log::Stop();
        return;
    }

    if (strcmp(verb, "status") == 0 && args.Argc() == 2)
    {
        ps2::system::frame_log::LogStatus();
        return;
    }

    ps2::Log(ps2::LogLevel::Error, "usage: ps2_framelog start [FRAMES] | stop | status\n");
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
        ps2::renderer::gs::FrameFormat::Color16,
        ps2::renderer::gs::FrameFormat::Color32Display16,
        ps2::renderer::gs::FrameFormat::Color32
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

    // A redraw between Common frames is no frame of the log's.
    if (s_inFrame)
    {
        ps2::system::frame_log::Mark(ps2::system::frame_log::Point::Present);
    }

    ps2::ui::AdvanceScreen(s_inFrame);
    ps2::renderer::BeginFrame({ 16, 24, 40, 255 });
    ps2::renderer::scene::BeginFrame();
    {
        if (s_worldView)
        {
            // Once per Common frame: a redraw between frames shows the same pose.
            if (s_path != nullptr && s_inFrame)
            {
                UpdatePath();
            }

#if PS2_D3BFG_DEBUG
            if (s_fly && s_inFrame)
            {
                UpdateFly();
            }
#endif // PS2_D3BFG_DEBUG

            common->RW()->RenderScene(&s_view);

            if (s_inFrame)
            {
                const idVec3 & eye = s_view.vieworg;
                ps2::system::frame_log::SetView(eye.x, eye.y, eye.z, s_view.viewaxis[0].ToYaw());
            }
        }

        if (s_inFrame)
        {
            ps2::system::frame_log::Mark(ps2::system::frame_log::Point::View);
        }

        renderSystem->SetGLState(0);
        renderSystem->SetColor(colorWhite);
        ps2::ui::RenderScreen(s_inFrame);
        console->Draw(false);

        if (s_inFrame)
        {
            ps2::system::frame_log::Mark(ps2::system::frame_log::Point::Ui);
        }
    }

    // A capture reads the frame once it is on screen, and a redraw between frames has no next frame
    // to present it.
    const bool pipelined = s_inFrame && s_captureName[0] == '\0' && s_pipelineFrames.GetBool();
    ps2::renderer::EndFrame(pipelined ? ps2::renderer::gs::PresentMode::Pipelined : ps2::renderer::gs::PresentMode::Immediate);

    if (s_inFrame)
    {
        ps2::system::frame_log::Mark(ps2::system::frame_log::Point::Presented);
    }

    if (s_captureName[0] != '\0')
    {
        char path[ps2::system::fs::kPathCapacity];
        ps2::system::runtime::PackageFile(path, sizeof(path), s_captureName);

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
    char path[system::fs::kPathCapacity];
    PackageFile(path, sizeof(path), "presentation.cfg");
    FILE * file = system::fs::Open(path, system::fs::OpenMode::Read);

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
    cmdSystem->AddCommand("ps2_framelog", FrameLog, CMD_FL_SYSTEM, CMD_DESC("record per-frame timings and counters to the package's framelog.txt"));

    // The pad and the debug keyboard feed the same event queue the HostFs commands do; the
    // package stages the keyboard driver's modules in its irx/ directory.
    input::InitDevices();
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
    ps2::system::frame_log::BeginFrame();
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

    StopFly();
    FreePath();

    // A log still recording is written: the run ended before its frames did.
    if (frame_log::Recording())
    {
        frame_log::Stop();
    }

    Log(LogLevel::Info, "[D3BFG] CONSOLE shutdown complete frames=%d presents=%u redraws=%u\n",
        frames, renderer::FrameCount(), s_redraws);

    SetEventGenerator(nullptr);
    input::ShutdownDevices();
    cmdSystem->RemoveCommand("ps2_framelog");
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
