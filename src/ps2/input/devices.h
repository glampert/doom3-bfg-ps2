// ================================================================================================
// File: devices.h
// Brief: Own the visible presentation's physical input devices and feed Common's event queue.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::input
{
// Brings the pad and the keyboard up; either failing only leaves that device silent. The IRX
// directory holds the keyboard driver's modules (usbd, ps2kbd).
void InitDevices(const char * irxDirectory);
void ShutdownDevices();
// Sys_GenerateEvents: polls both devices into the event queue, once per Common frame.
void PollDevices();
void LogDeviceStatus();
} // namespace ps2::input
