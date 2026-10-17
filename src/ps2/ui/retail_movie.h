// ================================================================================================
// File: retail_movie.h
// Brief: Draw an opt-in native English font proof without activating retail SWF or Shell code.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::retail_movie
{
// Presentation owns this command after fonts initialize and releases it before fonts shut down.
void Init();
void Shutdown();
bool IsLoaded();
void Render(bool advance);
} // namespace ps2::retail_movie
