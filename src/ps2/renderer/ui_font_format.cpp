// ================================================================================================
// File: ui_font_format.cpp
// Brief: Check native glyph counts, metrics, Unicode indices and atlas bounds without allocation.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/ui_font_format.h"

namespace ps2::fonts
{
namespace
{

static constexpr std::uint32_t kFontMagic = 0x6964662A;
static constexpr std::uint32_t kMaxAtlasExtent = 16384;
static constexpr std::int32_t kMaxVerticalMetric = 4096;

static std::uint32_t ReadBigShort(const std::uint8_t * bytes)
{
    return (static_cast<std::uint32_t>(bytes[0]) << 8U) | static_cast<std::uint32_t>(bytes[1]);
}

static std::int32_t ReadBigSignedShort(const std::uint8_t * bytes)
{
    const std::uint32_t value = ReadBigShort(bytes);
    return value < 32768U ? static_cast<std::int32_t>(value) : static_cast<std::int32_t>(value) - 65536;
}

static std::uint32_t ReadLittleShort(const std::uint8_t * bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U);
}

static std::uint32_t ReadLittleWord(const std::uint8_t * bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

static std::uint32_t ReadBigWord(const std::uint8_t * bytes)
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) | (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) | static_cast<std::uint32_t>(bytes[3]);
}

static bool IsScalar(std::uint32_t value)
{
    return value <= 0x10FFFFU && (value < 0xD800U || value > 0xDFFFU);
}

} // namespace

bool ValidateFontBytes(const void * bytes, std::size_t length, std::uint32_t atlasWidth,
                       std::uint32_t atlasHeight, FontFormatSummary & output)
{
    if (bytes == nullptr || length < kFontHeaderBytes || length > kMaxFontBytes || atlasWidth == 0 ||
        atlasHeight == 0 || atlasWidth > kMaxAtlasExtent || atlasHeight > kMaxAtlasExtent)
    {
        return false;
    }

    const auto * const data = static_cast<const std::uint8_t *>(bytes);
    const std::uint32_t count = ReadBigShort(data + 10);
    const std::int32_t ascender = ReadBigSignedShort(data + 6);
    const std::int32_t descender = ReadBigSignedShort(data + 8);
    if (ReadBigWord(data) != kFontMagic || ReadBigShort(data + 4) != 48 || count == 0 || count > kMaxFontGlyphs ||
        ascender < 0 || ascender > kMaxVerticalMetric || descender < -kMaxVerticalMetric ||
        descender > kMaxVerticalMetric || length != kFontHeaderBytes + count * (kFontGlyphBytes + kFontIndexBytes))
    {
        return false;
    }

    FontFormatSummary summary{ count, ascender, descender, 0, 0, 0, 0 };
    const std::uint8_t * const indices = data + kFontHeaderBytes + count * kFontGlyphBytes;
    std::uint32_t previousCodepoint = 0;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const std::uint32_t codepoint = ReadLittleWord(indices + index * kFontIndexBytes);
        if (!IsScalar(codepoint) || (index != 0 && codepoint < previousCodepoint) || (codepoint < 128 && index > 127))
        {
            return false;
        }

        if (index != 0 && codepoint == previousCodepoint)
        {
            ++summary.duplicateCodepoints;
        }

        previousCodepoint = codepoint;
        const std::uint8_t * const glyph = data + kFontHeaderBytes + index * kFontGlyphBytes;
        const std::uint32_t width = glyph[0];
        const std::uint32_t height = glyph[1];
        const std::uint32_t advance = glyph[4];
        const std::uint32_t s = ReadLittleShort(glyph + 6);
        const std::uint32_t t = ReadLittleShort(glyph + 8);
        if (s + width > atlasWidth || t + height > atlasHeight)
        {
            return false;
        }

        if (advance > summary.maxAdvance)
        {
            summary.maxAdvance = advance;
        }

        if (height > summary.maxHeight)
        {
            summary.maxHeight = height;
        }

        if (width != 0 && height != 0 && (s == 0 || t == 0 || s + width == atlasWidth || t + height == atlasHeight))
        {
            ++summary.samplingGutterGlyphs;
        }
    }

    output = summary;
    return true;
}

} // namespace ps2::fonts
