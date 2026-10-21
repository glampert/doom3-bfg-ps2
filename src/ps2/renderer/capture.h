// ================================================================================================
// File: capture.h
// Brief: Stream a completed native GS framebuffer to a checked top-down TGA through bounded strips.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once
#include "ps2/renderer/gs.h"

namespace ps2::renderer::gs
{
// Only the most recently presented frame may be captured, between frames. A failed capture
// can leave a partial file; callers must publish success only when this returns true.
bool CaptureTga(const char * path, DrawContext context);

#if PS2_D3BFG_TESTS
void InjectReadbackTimeoutOnce();
#endif
} // namespace ps2::renderer::gs
