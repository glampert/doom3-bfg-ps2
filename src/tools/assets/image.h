// ================================================================================================
// File: image.h
// Brief: Straight-alpha RGBA8 images, edge-clamped crops and exact-coverage area downscaling in
// premultiplied space, the reference operations every texture conversion starts from.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace assets
{

struct Rect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// Rows are tightly packed, top-down, four bytes per texel; alpha is straight 0..255.
struct RgbaImage
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;

    void Allocate(int newWidth, int newHeight);
    const std::uint8_t * At(int x, int y) const;
    std::uint8_t * At(int x, int y);
};

// Copies a rectangle that may extend past the source; outside texels repeat the nearest edge.
RgbaImage Crop(const RgbaImage & source, const Rect & rect);

// Averages exact fractional footprints in premultiplied space, so transparent texels never darken
// visible ones. A fully transparent footprint keeps the average straight RGB, which bilinear
// filtering still blends into its visible neighbors. Never enlarges; sizes must be positive.
RgbaImage ResampleArea(const RgbaImage & source, int width, int height);

} // namespace assets
