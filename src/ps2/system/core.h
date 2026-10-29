// ================================================================================================
// File: core.h
// Brief: Initialize the retained Doom core behind bounded headless platform services.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

namespace ps2::system::lifecycle
{

// The core test boot: Common on the loose-file fixture filesystem core.cpp provides.
PS2_COLD_FUNC void InitCore();
PS2_COLD_FUNC void ShutdownCore();

} // namespace ps2::system::lifecycle
