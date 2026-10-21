// ================================================================================================
// File: retail_movie.h
// Brief: Draw an opt-in native English font proof without activating retail SWF or Shell code.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::ui::retail
{
// Presentation owns this command after fonts initialize and releases it before fonts shut down.
void InitScreen();
void ShutdownScreen();
bool IsScreenLoaded();
// Advance native retail timelines once per Common frame before drawing; redraws never execute them.
void AdvanceScreen(bool advance);
void RenderScreen(bool advance);
} // namespace ps2::ui::retail
