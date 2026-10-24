// ================================================================================================
// File: headless.cpp
// Brief: Own native game/console startup, synchronous frames and live HostFs commands.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/input/devices.h"
#include "ps2/renderer/null_render.h"
#include "ps2/system/filesystem.h"
#include "ps2/system/heap.h"
#include "ps2/system/lifecycle.h"
#include "ps2/system/log.h"
#include "ps2/system/runtime.h"
#include <idlib/precompiled.h>

// Native Common/game declarations depend on the complete engine precompiled header.
#include <d3xp/Game_local.h>
#include <framework/Common_local.h>
#include <framework/ConsoleHistory.h>

namespace
{
static idCVar s_maxFrames("ps2_maxFrames", "0", CVAR_SYSTEM | CVAR_INTEGER,
                          "quit after this many completed frames; zero runs until quit", 0.0f, 1000000.0f);
static bool s_quitRequested = false;
static bool s_initialized = false;
static int s_frames = 0;
static uint64 s_lastConsolePoll = 0;
static int s_consoleOffset = 0;
static constexpr uint64 kConsolePollMicroseconds = 100000U;
static constexpr size_t kConsoleReadBudget = 1024;
static constexpr size_t kConsoleLineCapacity = 1024;
static char s_consoleLine[kConsoleLineCapacity] = {};
static size_t s_consoleLineLength = 0;
static bool s_discardConsoleLine = false;

void Status(const idCmdArgs &)
{
    const auto stats = ps2::system::heap::GetTotalStats();
    ps2::Log(ps2::LogLevel::Info,
             "[D3BFG] HEADLESS frames=%d game_frames=%d state=console console=%d game_nomap=%d render_device=%d sound_output=%d heap=%zu/%zu/%zu\n",
             s_frames, gameLocal.GetFrameNum(), static_cast<int>(console->Active()),
             static_cast<int>(gameLocal.GameState() == GAMESTATE_NOMAP),
             static_cast<int>(ps2::system::runtime::HasPresentation()),
             static_cast<int>(soundSystem->GetPlayingSoundWorld() != nullptr),
             stats.requestedBytes, stats.backingBytes, stats.allocationCount);
    ps2::system::runtime::LogPresentationStatus(s_frames);
    ps2::input::LogDeviceStatus();
}

void SubmitConsoleLine()
{
    if (!s_discardConsoleLine && s_consoleLineLength != 0)
    {
        s_consoleLine[s_consoleLineLength] = '\0';
        common->Printf("]%s\n", s_consoleLine);
        idCmdArgs args;
        args.TokenizeString(s_consoleLine, false);
        // Host control messages must not displace commands entered in the native edit field.
        if (idStr::Icmpn(args.Argv(0), "ps2_", 4) != 0)
        {
            consoleHistory.AddToHistory(s_consoleLine, false);
        }
        const size_t size = s_consoleLineLength + 1;
        void * text = ps2::system::heap::Alloc(size, TAG_EVENTS);
        memcpy(text, s_consoleLine, size);
        Sys_QueEvent(SE_CONSOLE, 0, 0, static_cast<int>(size), text, 0);
        // Drain each line so a file full of short commands cannot overrun the event queue.
        eventLoop->RunEventLoop(false);
    }
    s_consoleLineLength = 0;
    s_discardConsoleLine = false;
}

void PollConsole()
{
    const uint64 now = Sys_Microseconds();
    if (now - s_lastConsolePoll < kConsolePollMicroseconds)
    {
        return;
    }
    s_lastConsolePoll = now;
    // Reopen each poll: HostFs writers may append while the engine runs, and stdio can cache EOF.
    FILE * file = ps2::system::fs::Open("host:console.txt", ps2::system::fs::OpenMode::Read);
    if (file == nullptr)
    {
        return;
    }
    const int length = ps2::system::fs::FileLength(file);
    if (length < 0)
    {
        fclose(file);
        ps2::FatalError("cannot determine headless console input length");
    }
    if (length < s_consoleOffset)
    {
        // A truncated stream starts a new input lifetime, including any unfinished line.
        s_consoleOffset = 0;
        s_consoleLineLength = 0;
        s_discardConsoleLine = false;
    }
    const size_t available = static_cast<size_t>(length - s_consoleOffset);
    const size_t count = available < kConsoleReadBudget ? available : kConsoleReadBudget;
    char bytes[kConsoleReadBudget];
    const bool read = fseek(file, s_consoleOffset, SEEK_SET) == 0 &&
                      ps2::system::fs::ReadExact(file, bytes, count);
    const bool closed = fclose(file) == 0;
    if (!read || !closed)
    {
        ps2::FatalError("cannot read headless console input");
    }
    s_consoleOffset += static_cast<int>(count);
    for (size_t index = 0; index < count; ++index)
    {
        const unsigned char value = static_cast<unsigned char>(bytes[index]);
        if (value == '\n')
        {
            SubmitConsoleLine();
        }
        else if (value != '\r' && !s_discardConsoleLine)
        {
            if (s_consoleLineLength == kConsoleLineCapacity - 1 || (value < ' ' && value != '\t'))
            {
                ps2::Log(ps2::LogLevel::Warning, "discarding invalid or oversized headless console line\n");
                s_discardConsoleLine = true;
            }
            else
            {
                s_consoleLine[s_consoleLineLength++] = static_cast<char>(value);
            }
        }
    }
}
} // namespace

namespace ps2::system::runtime
{
void RequestQuit()
{
    s_quitRequested = true;
}

bool IsQuitRequested()
{
    return s_quitRequested;
}
} // namespace ps2::system::runtime

void idCommonLocal::InitHeadless()
{
    PS2_Assert(IsInitialized());
    if (!IsInitialized() || s_initialized)
    {
        ps2::FatalError("invalid headless game startup");
    }
    // Tagged ledgers after each stage record what the startup data costs on the EE.
    ps2::system::lifecycle::PrintMemory("startup-foundation");
    idKeyInput::Init();
    console->Init();
    consoleHistory.ClearHistory();
    eventLoop->Init();
    declManager->Init();
    ps2::system::lifecycle::PrintMemory("startup-decls");
    // This bootstrap never executes desktop device bindings or starts an audio device.
    cvarSystem->SetCVarBool("g_xp_bind_run_once", true);
    cvarSystem->SetCVarBool("s_noSound", true);
    soundSystem->Init();
    LoadGameDLL(); // Validates the real static game ABI and calls native idGameLocal::Init.
    ps2::system::lifecycle::PrintMemory("startup-game");
    ps2::renderer::InitLogical("_headless_default");
    renderWorld = renderSystem->AllocRenderWorld();
    soundWorld = soundSystem->AllocSoundWorld(renderWorld);
    menuSoundWorld = soundSystem->AllocSoundWorld(nullptr);
    userCmdMgr.SetDefaults();
    cmdSystem->AddCommand("ps2_status", Status, CMD_FL_SYSTEM, "print headless startup state and completed frame counts");
    cvarSystem->SetCVarBool("com_allowConsole", true);
    consoleUsed = true;
    const sysEvent_t open{ SE_KEY, K_GRAVE, 1, 0, nullptr, 0 };
    if (!console->ProcessEvent(&open, false) || !console->Active() || gameLocal.GameState() != GAMESTATE_NOMAP)
    {
        ps2::FatalError("native game did not reach the headless console");
    }
    s_initialized = true;
    ps2::system::runtime::InitPresentation();
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] HEADLESS console ready; append commands to host:console.txt\n");
    Status(idCmdArgs{});
}

void idCommonLocal::FrameHeadless()
{
    PS2_Assert(s_initialized);
    if (!s_initialized || s_frames == INT_MAX || idLib::frameNumber == INT_MAX)
    {
        ps2::FatalError("invalid headless game frame");
    }
    ps2::system::runtime::BeginPresentationFrame();
    PollConsole();
    Sys_GenerateEvents();
    eventLoop->RunEventLoop(false);
    ps2::system::lifecycle::Frame();
    ps2::renderer::AdvanceLogicalFrame();
    gameReturn_t result{};
    // The native constructor omits this array, and NOMAP returns without filling the result.
    result.sessionCommand[0] = '\0';
    game->RunFrame(userCmdMgr, result); // Native NOMAP frames return without advancing map simulation.
    if (result.sessionCommand[0] != '\0')
    {
        ps2::FatalError("headless console cannot process a map/session transition");
    }
    ++s_frames;
    // Complete and display the last frame even when input requested quit or a capture.
    ps2::system::runtime::PresentConsoleFrame(s_frames);
    if (s_maxFrames.GetInteger() > 0 && s_frames >= s_maxFrames.GetInteger())
    {
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] HEADLESS frame limit reached frames=%d\n", s_frames);
        ps2::system::runtime::RequestQuit();
    }
}

void idCommonLocal::ShutdownHeadless()
{
    if (!s_initialized)
    {
        return;
    }
    Status(idCmdArgs{});
    ps2::system::runtime::ShutdownPresentation(s_frames);
    UnloadGameDLL();
    soundSystem->FreeSoundWorld(menuSoundWorld);
    soundSystem->FreeSoundWorld(soundWorld);
    menuSoundWorld = soundWorld = nullptr;
    renderSystem->FreeRenderWorld(renderWorld);
    renderWorld = nullptr;
    ps2::renderer::ShutdownLogical();
    soundSystem->Shutdown();
    declManager->Shutdown();
    CloseLogFile();
    cmdSystem->RemoveCommand("ps2_status");
    console->Close();
    console->Shutdown();
    consoleHistory.ClearHistory();
    eventLoop->Shutdown();
    idKeyInput::Shutdown();
    s_initialized = false;
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] HEADLESS shutdown complete frames=%d\n", s_frames);
}
