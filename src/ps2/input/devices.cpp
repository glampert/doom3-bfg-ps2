// ================================================================================================
// File: devices.cpp
// Brief: Bring the pad and keyboard up with the visible presentation and poll them per frame.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/input/devices.h"
#include "ps2/input/keyboard.h"
#include "ps2/input/pad.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>

namespace
{
static bool s_initialized = false;
static u32 s_polls = 0;
} // namespace

namespace ps2::input
{
void InitDevices(const char * irxDirectory)
{
    if (s_initialized)
    {
        FatalError("input devices initialized twice");
    }

    s_initialized = true;
    s_polls = 0;
    const bool padUp = pad::Init();
    const bool keyboardUp = keyboard::Init(irxDirectory);
    Log(LogLevel::Info, "[D3BFG] INPUT devices pad=%d keyboard=%d\n", padUp ? 1 : 0, keyboardUp ? 1 : 0);
}

void ShutdownDevices()
{
    if (!s_initialized)
    {
        return;
    }

    keyboard::Shutdown();
    pad::Shutdown();
    s_initialized = false;
}

void PollDevices()
{
    if (!s_initialized)
    {
        return;
    }

    ++s_polls;
    pad::Poll();
    keyboard::Poll(Sys_Milliseconds());
}

void LogDeviceStatus()
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
} // namespace ps2::input
