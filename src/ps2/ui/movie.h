// ================================================================================================
// File: movie.h
// Brief: Own English resource screens, checked retail timelines and the natively driven shell menu.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::ui
{

// Presentation owns this command after fonts initialize and releases it before fonts shut down.
void InitScreen();
void ShutdownScreen();
bool IsScreenLoaded();

// The package's startup policy opens the native menu before the first frame, on real time.
void OpenBootMenu();

// Advance native retail timelines once per Common frame before drawing; redraws never execute them.
void AdvanceScreen(bool advance);
void RenderScreen(bool advance);

} // namespace ps2::ui
