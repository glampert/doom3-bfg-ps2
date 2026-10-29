// ================================================================================================
// File: offline_session.h
// Brief: Expose the one-user offline campaign session without desktop networking or save workers.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

namespace ps2::system::session
{

PS2_COLD_FUNC void Init();
PS2_COLD_FUNC void Shutdown();
bool IsInitialized();
void Frame();

} // namespace ps2::system::session
