// ================================================================================================
// File: ui_geometry.h
// Brief: Clip native virtual-space UI rectangles before producing bounded GS vertex and UV fields.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::ui
{

struct Rect
{
    float x;
    float y;
    float width;
    float height;
    float s0;
    float t0;
    float s1;
    float t1;
};

struct Viewport
{
    int width;
    int height;
};

struct FixedRect
{
    int x0;
    int y0;
    int x1;
    int y1;
    int u0;
    int v0;
    int u1;
    int v1;
};
enum class Result
{
    Draw,
    Clipped,
    Invalid
};

// Native coordinates use 640x480; UVs address the logical texture rather than its padded extent.
// Outputs are nearest-rounded 1/16 screen pixels (without XYOFFSET) and 1/16 texels.
// The first profile accepts normal/zero floats, nonnegative sizes/UVs and increasing UV endpoints.
// Coordinates/sizes are bounded to 32768, viewport axes to 1024, textures to 512 and UVs to 1023 texels.
// Invalid and fully clipped rectangles leave output untouched.
Result MapRect(const Rect & rect, Viewport viewport, int textureWidth, int textureHeight,
               FixedRect & output);

} // namespace ps2::ui
