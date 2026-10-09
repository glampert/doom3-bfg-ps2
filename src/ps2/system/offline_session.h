// ================================================================================================
// File: offline_session.h
// Brief: Expose the one-user offline campaign session without desktop networking or save workers.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::offline
{

void Init();
void Shutdown();
void Frame();
bool IsInitialized();

} // namespace ps2::offline
