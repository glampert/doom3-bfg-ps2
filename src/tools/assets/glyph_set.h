// ================================================================================================
// File: glyph_set.h
// Brief: Pre-render a font's glyphs at the exact pixel sizes a screen draws them, so the GS copies
// texels instead of minifying the 48-point atlas: sub-pixel phases keep stems sharp, and stroke
// outlines are rendered beside each glyph.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "assets/gstx.h"
#include "assets/image.h"
#include "assets/texture_convert.h"

#include <cstdint>
#include <string>
#include <vector>

namespace assets
{

// One glyph of the native 48-point font, in atlas texels: its atlas rectangle, the offset from the
// pen to its left edge, and its height above the baseline.
struct GlyphMetrics
{
    std::uint32_t codepoint = 0;
    int s = 0;
    int t = 0;
    int width = 0;
    int height = 0;
    int left = 0;
    int top = 0;
};

struct GlyphSizeRequest
{
    // GS pixels per atlas texel on each axis.
    double scaleX = 0.0;
    double scaleY = 0.0;
    // The engine's stroke offset in atlas texels; zero renders no outline.
    double strokeWeight = 0.0;
};

struct PrerenderedGlyphResult
{
    std::uint32_t codepoint = 0;
    // The bitmap's texels in the texture's logical space, empty for a glyph without ink. With an
    // outline the rectangle holds the glyph on its left half and the outline on its right half.
    Rect rect;
    bool outline = false;
    // The glyph's left edge on its baseline inside a bitmap, in 1/16 GS pixels.
    int originX = 0;
    int originY = 0;
};

struct GlyphSizeResult
{
    GlyphSizeRequest request;
    std::vector<PrerenderedGlyphResult> glyphs;
};

struct GlyphSetResult
{
    GstxTexture texture;
    // One placement per page, each a band of the logical space holding many glyph bitmaps, with
    // its quantization against the rendered bitmaps.
    std::vector<PlacementResult> placements;
    std::vector<GlyphSizeResult> sizes;
};

// `atlas` holds the font's coverage in alpha at full resolution. Glyphs are rendered per size in
// codepoint order and packed onto PSMT4 pages; the logical space stacks the pages, each one band
// that maps 1:1 onto its page, all on one shared 16-level alpha CLUT.
bool RenderGlyphSet(const RgbaImage & atlas, const std::vector<GlyphMetrics> & glyphs, const std::vector<GlyphSizeRequest> & sizes,
                    int maxPageExtent, GlyphSetResult & output, std::string & error);

} // namespace assets
