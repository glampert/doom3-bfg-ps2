// ================================================================================================
// File: triangles.h
// Brief: Clip colored native UI triangles before generating bounded GS position and UV fields.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/ui/geometry.h"
#include <cstdint>

namespace ps2::ui
{

struct Vertex
{
    float x;
    float y;
    float s;
    float t;
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};

struct FixedVertex
{
    int x;
    int y;
    int u;
    int v;
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};

struct FixedTriangle
{
    FixedVertex vertices[3];
};

// A clipped line segment; UV fields are unused and zero.
struct FixedLine
{
    FixedVertex vertices[2];
};

static constexpr int kMaxClippedTriangles = 5;

// Clip to 640x480, preserving either winding and interpolating UVs and RGBA before fixed rounding.
// Limits match MapRect, except UVs may run in either direction. Positions/UVs accept normal/zero
// float representations only. Invalid leaves both outputs untouched; Clipped only sets count to 0.
// Draw writes count triangles, with 1/16 screen pixel/texel coordinates and unscaled byte colors.
Result MapTriangle(const Vertex (&input)[3], Viewport viewport, int textureWidth, int textureHeight,
                   FixedTriangle (&output)[kMaxClippedTriangles], int & triangleCount);

// Clip a segment to 640x480, interpolating RGBA, then map it to 1/16 screen pixels. UVs are ignored.
// Positions accept normal/zero floats within +/-32768. A segment with no length after mapping is
// Clipped; Invalid leaves the output untouched.
Result MapLine(const Vertex (&input)[2], Viewport viewport, FixedLine & output);

} // namespace ps2::ui
