// ================================================================================================
// File: pad.h
// Brief: DualShock pad on either controller port over the ROM's PADMAN through libpad, posting the
//        engine's joystick keys.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include <tamtypes.h>

namespace ps2::input::pad
{

// Has the IOP boot start the pad driver and libpad, and opens both controller ports (slot 0 of
// each: no multitap). False, when no port opened, leaves the pad silent for the process; connection
// and the analog mode switch complete over later polls.
PS2_COLD_FUNC bool Init();
PS2_COLD_FUNC void Shutdown();

// Advances the connection state and, once ready, reads the pad. While no pad is in use the ports
// are checked in connector order and the first with a pad takes it, keeping it until it is
// unplugged; then both are checked again. Every changed button posts a key event (cross is K_JOY1,
// as on the PS3 layout the menus use), and each stick past half its range posts the engine's stick
// keys. The reads are copies of padman's buffers, not IOP round trips.
void Poll();

// False stops the pad posting key events (releasing what it holds) while something else reads
// GetStats directly, as the debug free-flight camera does; true resumes them.
void SetKeyEvents(bool enabled);

struct Stats
{
    bool available = false;
    bool ready = false;
    bool analog = false;
    // libpad's button mask (PAD_CROSS and the like), set bits pressed.
    u16 buttons = 0;
    // Each stick's deflection from its centre, -128 to 127 (x rightwards, y downwards); zero
    // unless the pad is in an analog mode.
    s8 leftX = 0;
    s8 leftY = 0;
    s8 rightX = 0;
    s8 rightY = 0;
    u32 events = 0;
};

const Stats & GetStats();

} // namespace ps2::input::pad
