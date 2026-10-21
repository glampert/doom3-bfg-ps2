// ================================================================================================
// File: page_geometry.h
// Brief: Split native UI triangles into bounded atlas pages without altering logical atlas UVs.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/ui/triangles.h"

namespace ps2::ui
{

static constexpr int kMaxPageTriangles = 9;

// Clip one full-atlas triangle to a 254-texel page interior and the 640x480 screen.
// Pages are 256x256 with one gutter texel on every edge. UVs must be normalized 0..1;
// atlas axes are <=16384, base RGBA <=64 MiB, and the complete atlas uses <=1024 pages.
// outerGutter admits native font half-texel UV expansion at the atlas outside edges,
// preserving geometry while sampling the replicated outer gutters. Internal seams stay unchanged.
// Constant UVs exactly on an internal upper page boundary belong to the following page.
// Numerically singular/nonportable constraint intersections fail safely as Invalid.
// Invalid leaves output and count untouched. Clipped only writes count=0. Draw writes
// count triangles with 1/16 screen-pixel/page-texel coordinates and interpolated byte RGBA.
Result MapPageTriangle(const Vertex (&input)[3], Viewport viewport, int atlasWidth, int atlasHeight,
                       int column, int row, FixedTriangle (&output)[kMaxPageTriangles], int & triangleCount, bool outerGutter = false);

} // namespace ps2::ui
