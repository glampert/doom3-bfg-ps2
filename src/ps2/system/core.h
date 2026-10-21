// ================================================================================================
// File: core.h
// Brief: Initialize the retained Doom core behind bounded headless platform services.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::system::lifecycle
{

// The core test boot: Common on the loose-file fixture filesystem core.cpp provides.
void InitCore();
void ShutdownCore();

} // namespace ps2::system::lifecycle
