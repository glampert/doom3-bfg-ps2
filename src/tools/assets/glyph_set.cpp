// ================================================================================================
// File: glyph_set.cpp
// Brief: Pre-render a font's glyphs at the exact pixel sizes a screen draws them, so the GS copies
// texels instead of minifying the 48-point atlas: sub-pixel phases keep stems sharp, and stroke
// outlines are rendered beside each glyph.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/glyph_set.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace assets
{
namespace
{

// Sub-pixel phases tried for each glyph's left edge and each size's baseline: eighths of a pixel.
static constexpr int kPhases = 8;
// Coverage samples per pixel on each axis: fewer while searching phases, more for the bitmaps kept.
static constexpr int kSearchSamples = 4;
static constexpr int kFinalSamples = 8;
// Bitmaps are shelved left to right on a logical sheet this wide before conversion.
static constexpr int kSheetWidth = 1024;
// The engine draws a stroke as four copies of the glyph offset diagonally by the stroke weight.
static constexpr double kStrokeOffsets[4][2] = { { -1.0, -1.0 }, { 1.0, -1.0 }, { -1.0, 1.0 }, { 1.0, 1.0 } };

// One glyph's full-resolution coverage: its atlas texels and the one-texel ring the engine's
// half-texel UVs reach, transparent beyond, so atlas neighbors never leak in.
class GlyphCoverage final
{
public:
    GlyphCoverage(const RgbaImage & atlas, const GlyphMetrics & glyph)
        : m_atlas(atlas),
          m_glyph(glyph)
    {
    }

    // Coverage 0..1 at glyph-local texel coordinates. Atlas texels already hold the coverage of
    // their squares, so averaging samples of them is the exact area filter, with no extra blur.
    double Sample(double u, double v) const
    {
        return Texel(static_cast<int>(std::floor(u)), static_cast<int>(std::floor(v))) / 255.0;
    }

    // The four stroke copies, each blended over the others as the GS draws them.
    double Outline(double u, double v, double weight) const
    {
        double keep = 1.0;

        for (const auto & offset : kStrokeOffsets)
        {
            keep *= 1.0 - Sample(u - offset[0] * weight, v - offset[1] * weight);
        }

        return 1.0 - keep;
    }

private:
    double Texel(int x, int y) const
    {
        if (x < -1 || y < -1 || x > m_glyph.width || y > m_glyph.height)
        {
            return 0.0;
        }

        const int atlasX = m_glyph.s + x;
        const int atlasY = m_glyph.t + y;

        if (atlasX < 0 || atlasY < 0 || atlasX >= m_atlas.width || atlasY >= m_atlas.height)
        {
            return 0.0;
        }

        return m_atlas.At(atlasX, atlasY)[3];
    }

    const RgbaImage & m_atlas;
    const GlyphMetrics & m_glyph;
};

// A glyph rendered at one size: coverage per pixel and the ink origin, the glyph's left edge on its
// baseline, in pixels from the bitmap's corner.
struct Bitmap
{
    int width = 0;
    int height = 0;
    double originX = 0.0;
    double originY = 0.0;
    std::vector<double> glyph;
    std::vector<double> outline;
};

// Each output pixel averages samples spread over its area. The glyph's left edge falls at
// `phaseX` and its baseline at `phaseY` within their pixels.
static Bitmap Render(const GlyphCoverage & coverage, const GlyphMetrics & glyph, const GlyphSizeRequest & size, double phaseX,
                     double phaseY, bool outline, int samples)
{
    const int margin = 2 + static_cast<int>(std::ceil(size.strokeWeight * std::max(size.scaleX, size.scaleY)));
    const int above = static_cast<int>(std::ceil(glyph.top * size.scaleY)) + margin;
    const int below = static_cast<int>(std::ceil(std::max(0, glyph.height - glyph.top) * size.scaleY)) + margin + 1;
    Bitmap bitmap;
    bitmap.width = static_cast<int>(std::ceil(glyph.width * size.scaleX + phaseX)) + 2 * margin + 1;
    bitmap.height = above + below;
    bitmap.originX = margin + phaseX;
    bitmap.originY = above + phaseY;
    bitmap.glyph.assign(static_cast<std::size_t>(bitmap.width) * static_cast<std::size_t>(bitmap.height), 0.0);

    if (outline)
    {
        bitmap.outline = bitmap.glyph;
    }

    const double weight = 1.0 / (static_cast<double>(samples) * static_cast<double>(samples));

    for (int y = 0; y < bitmap.height; ++y)
    {
        for (int x = 0; x < bitmap.width; ++x)
        {
            double glyphSum = 0.0;
            double outlineSum = 0.0;

            for (int b = 0; b < samples; ++b)
            {
                const double v = (y - bitmap.originY + (b + 0.5) / samples) / size.scaleY + glyph.top;

                for (int a = 0; a < samples; ++a)
                {
                    const double u = (x - bitmap.originX + (a + 0.5) / samples) / size.scaleX;
                    glyphSum += coverage.Sample(u, v);

                    if (outline)
                    {
                        outlineSum += coverage.Outline(u, v, size.strokeWeight);
                    }
                }
            }

            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(bitmap.width) + static_cast<std::size_t>(x);
            bitmap.glyph[index] = glyphSum * weight;

            if (outline)
            {
                bitmap.outline[index] = outlineSum * weight;
            }
        }
    }

    return bitmap;
}

// Coverage concentrated in few pixels scores higher: stems on whole pixels rather than split.
static double Sharpness(const Bitmap & bitmap)
{
    return std::accumulate(bitmap.glyph.begin(), bitmap.glyph.end(), 0.0, [](double sum, double value)
                           {
                               return sum + value * value;
                           });
}

static std::uint8_t Alpha(double coverage)
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(coverage, 0.0, 1.0) * 255.0));
}

// A kept bitmap pair, trimmed to its ink, before it is shelved on the sheet.
struct Pair
{
    std::size_t size = 0;
    std::size_t glyph = 0;
    RgbaImage pixels;
    Rect rect;
};

} // namespace

bool RenderGlyphSet(const RgbaImage & atlas, const std::vector<GlyphMetrics> & glyphs, const std::vector<GlyphSizeRequest> & sizes,
                    int maxPageExtent, GlyphSetResult & output, std::string & error)
{
    if (glyphs.empty() || sizes.empty())
    {
        error = "a glyph set needs glyphs and sizes";
        return false;
    }

    for (std::size_t index = 0; index < glyphs.size(); ++index)
    {
        const GlyphMetrics & glyph = glyphs[index];

        if (glyph.width < 0 || glyph.height < 0 || glyph.s < 0 || glyph.t < 0 || glyph.s + glyph.width > atlas.width ||
            glyph.t + glyph.height > atlas.height || (index != 0 && glyph.codepoint <= glyphs[index - 1].codepoint))
        {
            error = "glyph " + std::to_string(glyph.codepoint) + " is outside the atlas or out of codepoint order";
            return false;
        }
    }

    for (const GlyphSizeRequest & size : sizes)
    {
        if (!(size.scaleX > 0.0 && size.scaleX <= 4.0 && size.scaleY > 0.0 && size.scaleY <= 4.0 && size.strokeWeight >= 0.0 &&
              size.strokeWeight <= 16.0))
        {
            error = "glyph size out of range";
            return false;
        }
    }

    GlyphSetResult result;
    std::vector<Pair> pairs;

    for (std::size_t sizeIndex = 0; sizeIndex < sizes.size(); ++sizeIndex)
    {
        const GlyphSizeRequest & size = sizes[sizeIndex];
        const bool outline = size.strokeWeight > 0.0;
        GlyphSizeResult sizeResult;
        sizeResult.request = size;
        // One baseline phase for the size keeps every glyph on the same baseline.
        double bestBaseline = 0.0;
        double bestScore = -1.0;

        for (int phase = 0; phase < kPhases; ++phase)
        {
            const double phaseY = static_cast<double>(phase) / kPhases;
            double score = 0.0;

            for (const GlyphMetrics & glyph : glyphs)
            {
                score += Sharpness(Render(GlyphCoverage(atlas, glyph), glyph, size, 0.0, phaseY, false, kSearchSamples));
            }

            if (score > bestScore)
            {
                bestScore = score;
                bestBaseline = phaseY;
            }
        }

        for (std::size_t glyphIndex = 0; glyphIndex < glyphs.size(); ++glyphIndex)
        {
            const GlyphMetrics & glyph = glyphs[glyphIndex];
            const GlyphCoverage coverage(atlas, glyph);
            double bestPhase = 0.0;
            double bestGlyphScore = -1.0;

            for (int phase = 0; phase < kPhases; ++phase)
            {
                const double phaseX = static_cast<double>(phase) / kPhases;
                const double score = Sharpness(Render(coverage, glyph, size, phaseX, bestBaseline, false, kSearchSamples));

                if (score > bestGlyphScore)
                {
                    bestGlyphScore = score;
                    bestPhase = phaseX;
                }
            }

            const Bitmap bitmap = Render(coverage, glyph, size, bestPhase, bestBaseline, outline, kFinalSamples);
            // Trim to the pixels that keep any ink, in the glyph or its outline.
            int left = bitmap.width;
            int top = bitmap.height;
            int right = -1;
            int bottom = -1;

            for (int y = 0; y < bitmap.height; ++y)
            {
                for (int x = 0; x < bitmap.width; ++x)
                {
                    const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(bitmap.width) + static_cast<std::size_t>(x);

                    if (Alpha(bitmap.glyph[index]) != 0 || (outline && Alpha(bitmap.outline[index]) != 0))
                    {
                        left = std::min(left, x);
                        top = std::min(top, y);
                        right = std::max(right, x);
                        bottom = std::max(bottom, y);
                    }
                }
            }

            PrerenderedGlyphResult glyphResult;
            glyphResult.codepoint = glyph.codepoint;

            if (right >= left)
            {
                const int width = right - left + 1;
                const int height = bottom - top + 1;
                Pair pair;
                pair.size = sizeIndex;
                pair.glyph = glyphIndex;
                pair.pixels.Allocate(outline ? width * 2 : width, height);

                for (int y = 0; y < height; ++y)
                {
                    for (int x = 0; x < pair.pixels.width; ++x)
                    {
                        const bool outlineHalf = x >= width;
                        const int sourceX = left + (outlineHalf ? x - width : x);
                        const std::size_t index = static_cast<std::size_t>(top + y) * static_cast<std::size_t>(bitmap.width) + static_cast<std::size_t>(sourceX);
                        std::uint8_t * texel = pair.pixels.At(x, y);
                        texel[0] = texel[1] = texel[2] = 255;
                        texel[3] = Alpha(outlineHalf ? bitmap.outline[index] : bitmap.glyph[index]);
                    }
                }

                glyphResult.placement = static_cast<int>(pairs.size());
                glyphResult.outline = outline;
                glyphResult.originX = static_cast<int>(std::lround((bitmap.originX - left) * 16.0));
                glyphResult.originY = static_cast<int>(std::lround((bitmap.originY - top) * 16.0));
                pairs.push_back(std::move(pair));
            }

            sizeResult.glyphs.push_back(glyphResult);
        }

        result.sizes.push_back(std::move(sizeResult));
    }

    if (pairs.empty())
    {
        error = "the glyph set has no ink";
        return false;
    }

    // Shelve the pairs, tallest first, on a logical sheet; each becomes a 1:1 placement.
    std::vector<std::size_t> order(pairs.size());
    std::iota(order.begin(), order.end(), std::size_t{ 0 });
    std::stable_sort(order.begin(), order.end(), [&](std::size_t first, std::size_t second)
                     {
                         return pairs[first].pixels.height > pairs[second].pixels.height;
                     });
    int shelfX = 0;
    int shelfY = 0;
    int shelfHeight = 0;

    for (const std::size_t index : order)
    {
        Pair & pair = pairs[index];

        if (pair.pixels.width > kSheetWidth)
        {
            error = "a glyph bitmap exceeds the sheet width";
            return false;
        }

        if (shelfX + pair.pixels.width > kSheetWidth)
        {
            shelfY += shelfHeight;
            shelfX = 0;
            shelfHeight = 0;
        }

        pair.rect = { shelfX, shelfY, pair.pixels.width, pair.pixels.height };
        shelfX += pair.pixels.width;
        shelfHeight = std::max(shelfHeight, pair.pixels.height);
    }

    const int sheetHeight = shelfY + shelfHeight;

    if (sheetHeight > kGstxMaxLogicalExtent)
    {
        error = "the glyph set sheet exceeds the logical extent";
        return false;
    }

    RgbaImage sheet;
    sheet.Allocate(kSheetWidth, sheetHeight);

    for (int y = 0; y < sheet.height; ++y)
    {
        for (int x = 0; x < sheet.width; ++x)
        {
            std::uint8_t * texel = sheet.At(x, y);
            texel[0] = texel[1] = texel[2] = 255;
        }
    }

    std::vector<PlacementRequest> requests;

    for (const Pair & pair : pairs)
    {
        for (int y = 0; y < pair.rect.height; ++y)
        {
            std::copy(pair.pixels.At(0, y), pair.pixels.At(0, y) + pair.rect.width * 4, sheet.At(pair.rect.x, pair.rect.y + y));
        }

        requests.push_back({ pair.rect, pair.rect, 1.0 });
    }

    ConvertPolicy policy;
    policy.scale = 1.0;
    policy.maxPageExtent = maxPageExtent;
    ConvertResult converted;

    if (!ConvertTexture(sheet, requests, policy, converted, error))
    {
        return false;
    }

    if (!converted.alphaOnly)
    {
        error = "glyph set conversion lost its alpha-only palette";
        return false;
    }

    result.texture = std::move(converted.texture);
    result.placements = std::move(converted.placements);
    output = std::move(result);
    return true;
}

} // namespace assets
