// ================================================================================================
// File: devices.cpp
// Brief: Bring the pad up with the visible presentation, the debug keyboard when ps2_keyboard asks
//        for it, and poll them per frame.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/input/devices.h"
#include "ps2/input/keyboard.h"
#include "ps2/input/pad.h"
#include "ps2/system/aio.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>

namespace ps2::input
{
namespace
{

// The debug USB keyboard costs nothing unless a boot config (d3bfg.cfg) or the console sets this:
// every read is an IOP file call, which waits behind the loader's reads. Its driver starts the
// first frame the cvar is set, once per process; clearing it stops the polls.
static idCVar s_keyboard("ps2_keyboard", "0", CVAR_SYSTEM | CVAR_BOOL,
                         "read the debug USB keyboard; its driver starts the first frame this is set");

static bool s_initialized = false;
static bool s_keyboardTried = false;
static bool s_keyboardUp = false;
static bool s_keyboardPolling = false;
static u32 s_polls = 0;

// Starts usbd and ps2kbd from the package's irx/ and opens the device. The loader is drained first,
// so the module loads never share the IOP with its reads.
PS2_COLD_FUNC void StartKeyboard()
{
    s_keyboardTried = true;
    system::aio::Drain();
    s_keyboardUp = keyboard::Init();
    Log(LogLevel::Info, "[D3BFG] INPUT keyboard started=%d\n", s_keyboardUp ? 1 : 0);
}

} // namespace

PS2_COLD_FUNC void InitDevices()
{
    if (s_initialized) [[unlikely]]
    {
        FatalError("input devices initialized twice");
    }

    s_initialized = true;
    s_polls = 0;

    const bool padUp = pad::Init();
    Log(LogLevel::Info, "[D3BFG] INPUT devices pad=%d keyboard=%s\n", padUp ? 1 : 0, s_keyboard.GetBool() ? "on" : "off");
}

PS2_COLD_FUNC void ShutdownDevices()
{
    if (!s_initialized)
    {
        return;
    }

    keyboard::Shutdown();
    pad::Shutdown();

    s_initialized = false;
    s_keyboardTried = false;
    s_keyboardUp = false;
    s_keyboardPolling = false;
}

PS2_COLD_FUNC void LogDeviceStatus()
{
    if (!s_initialized)
    {
        return;
    }

    const pad::Stats padStats = pad::GetStats();
    const keyboard::Stats keyboardStats = keyboard::GetStats();

    Log(LogLevel::Info,
        "[D3BFG] INPUT frames=%u pad=%d ready=%d analog=%d buttons=0x%04x pad_events=%u keyboard=%d polls=%u keys=%u held=%u keyboard_events=%u\n",
        s_polls, padStats.available ? 1 : 0, padStats.ready ? 1 : 0, padStats.analog ? 1 : 0, padStats.buttons, padStats.events,
        keyboardStats.available ? 1 : 0, keyboardStats.polls, keyboardStats.rawKeys, keyboardStats.held, keyboardStats.events);
}

void PollDevices()
{
    if (!s_initialized)
    {
        return;
    }

    ++s_polls;
    pad::Poll();

    const bool wanted = s_keyboard.GetBool();

    if (wanted && !s_keyboardTried) [[unlikely]]
    {
        StartKeyboard();
    }

    const bool polling = wanted && s_keyboardUp;

    if (polling)
    {
        keyboard::Poll(Sys_Milliseconds());
    }
    else if (s_keyboardPolling)
    {
        keyboard::ReleaseKeys();
    }

    s_keyboardPolling = polling;
}

} // namespace ps2::input
