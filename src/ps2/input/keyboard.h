// ================================================================================================
// File: keyboard.h
// Brief: Debug USB keyboard over the ps2kbd IOP driver, polled as sparingly as the hardware allows.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <tamtypes.h>

namespace ps2::input::keyboard
{
// Starts usbd and ps2kbd from the package's IRX directory and opens the driver in raw mode. False
// leaves the keyboard silent for the process; true means the driver runs, not that a keyboard is
// attached, which the driver cannot tell.
bool Init(const char * irxDirectory);
void Shutdown();

// Drains the driver's queue into Common's event queue: one key event per transition, plus a
// character event for a printable key or backspace. Each read is an IOP file round trip, so an
// idle keyboard is read every kIdlePollMsec and a keyboard in use (a key held, or a key within
// kActiveMsec) every frame.
void Poll(int nowMsec);

struct Stats
{
    bool available = false;
    u32 polls = 0;
    u32 rawKeys = 0;
    u32 events = 0;
    u32 held = 0;
};

Stats GetStats();
} // namespace ps2::input::keyboard
