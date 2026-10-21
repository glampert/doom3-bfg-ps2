// ================================================================================================
// File: font_format.h
// Brief: Validate the native modern font layout before glyph/index allocation.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2::ui::fonts
{

static constexpr std::size_t kFontHeaderBytes = 12;
static constexpr std::size_t kFontGlyphBytes = 10;
static constexpr std::size_t kFontIndexBytes = 4;
static constexpr std::uint32_t kMaxFontGlyphs = 2048;
static constexpr std::size_t kMaxFontBytes = kFontHeaderBytes + kMaxFontGlyphs * (kFontGlyphBytes + kFontIndexBytes);

struct FontFormatSummary
{
    std::uint32_t glyphCount;
    std::int32_t ascender;
    std::int32_t descender;
    std::uint32_t maxAdvance;
    std::uint32_t maxHeight;
    std::uint32_t duplicateCodepoints;
    std::uint32_t samplingGutterGlyphs;
};

// Native idf42 uses a big-endian header, padded little-endian glyph records and a
// sorted little-endian Unicode index. Duplicate codepoints retain their order and
// last-match lookup semantics. ASCII cache indices must fit the native signed byte.
// Glyph rectangles may touch an atlas edge: its clamp sampler must handle the native
// half-texel expansion, reported separately by samplingGutterGlyphs. No allocation or
// floating-point arithmetic occurs here. Invalid input leaves output unchanged.
bool ValidateFontBytes(const void * bytes, std::size_t length, std::uint32_t atlasWidth,
                       std::uint32_t atlasHeight, FontFormatSummary & output);

} // namespace ps2::ui::fonts
