// ================================================================================================
// File: devices.h
// Brief: Own the visible presentation's physical input devices and feed Common's event queue.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

namespace ps2::input
{

// Brings the pad and the keyboard up; either failing only leaves that device silent. The IOP boot
// starts their drivers (iop_boot.h).
PS2_COLD_FUNC void InitDevices();
PS2_COLD_FUNC void ShutdownDevices();
PS2_COLD_FUNC void LogDeviceStatus();

// Sys_GenerateEvents: polls both devices into the event queue, once per Common frame.
void PollDevices();

} // namespace ps2::input
