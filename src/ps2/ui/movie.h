// ================================================================================================
// File: movie.h
// Brief: Own the authored native SWF independently of the deferred retail shell and headless game.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once
struct sysEvent_t;

namespace ps2::movie
{
void Init();
void Shutdown();
bool IsLoaded();
bool ProcessEvent(const sysEvent_t * event);
void Render(bool advance);
} // namespace ps2::movie
