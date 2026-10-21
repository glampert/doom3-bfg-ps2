// ================================================================================================
// File: palette.h
// Brief: Build GS-exact color palettes for paletted textures, map images onto them with measured
// error, and group images that can share one palette within an error budget.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "assets/image.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace assets
{

// Straight 8-bit color. Palette alpha is always a value whose GS conversion, (a * 128 + 127) / 255,
// lands exactly on the 0..128 level the quantizer chose.
struct PaletteColor
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;
};

// Weighted unique colors. Each texel adds one to its color's count and, to its RGB weight, the
// squared maximum alpha of its 3x3 neighborhood: transparent texels beside visible ones keep the
// RGB that bilinear filtering blends into the edge, while isolated transparent texels cost nothing.
class ColorHistogram final
{
public:
    struct Entry
    {
        PaletteColor color;
        double count = 0.0;
        double rgbWeight = 0.0;
    };

    void Add(const RgbaImage & image);
    void Add(const PaletteColor & color, double count, double rgbWeight);
    void Merge(const ColorHistogram & other);
    const std::vector<Entry> & Entries() const;
    double Texels() const;
    bool WhiteRgb() const;

private:
    std::unordered_map<std::uint32_t, std::size_t> m_index;
    std::vector<Entry> m_entries;
    double m_texels = 0.0;
};

// Squared distance under the histogram weighting: rgbWeight/count scales the RGB terms.
double ColorDistance(const ColorHistogram::Entry & sample, const PaletteColor & color);

// Variance-splitting boxes refined by weighted Lloyd iterations. Returns at most `colors` entries
// (fewer when the histogram has fewer unique colors), in no particular order.
std::vector<PaletteColor> BuildPalette(const ColorHistogram & histogram, int colors, int iterations = 12);

// White RGB with `levels` alpha values chosen by one-dimensional Lloyd iterations; transparent and
// opaque are always exact. For coverage-only images such as font atlases.
std::vector<PaletteColor> BuildAlphaPalette(const ColorHistogram & histogram, int levels);

// Weighted squared error of mapping every histogram color to its nearest entry.
double HistogramError(const ColorHistogram & histogram, const std::vector<PaletteColor> & palette);

struct QuantizeStats
{
    // Premultiplied RGB plus alpha, in 8-bit units, over every texel and four channels.
    double rmse = 0.0;
    int maxError = 0;
    // Share of texels whose worst channel is off by more than 8 (one 16-bit framebuffer step).
    double over8 = 0.0;
    std::size_t texels = 0;
};

// Maps every texel to its nearest entry under the histogram weighting and measures the result.
QuantizeStats MapToPalette(const RgbaImage & image, const std::vector<PaletteColor> & palette,
                           std::vector<std::uint8_t> & indices);

// Premultiplied error between two images of the same size.
QuantizeStats CompareImages(const RgbaImage & reference, const RgbaImage & candidate);

struct GroupPolicy
{
    int colors = 256;
    // A merge is accepted while every member's estimated RMSE stays within
    // max(maxRmse, its own palette's RMSE + maxIncrease), and no color the member's own palette
    // kept moves further than maxError (8-bit units; zero disables that check).
    double maxRmse = 2.5;
    double maxIncrease = 0.5;
    int maxError = 0;
};

// Greedy agglomerative grouping of images whose palettes can be shared. Returns a group number
// per image, numbered from zero in order of first appearance.
std::vector<int> GroupPalettes(const std::vector<const ColorHistogram *> & images, const GroupPolicy & policy);

} // namespace assets
