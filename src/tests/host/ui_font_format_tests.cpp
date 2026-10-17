// ================================================================================================
// File: ui_font_format_tests.cpp
// Brief: Exercise native font admission bounds and last-match Unicode semantics without engine IO.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/ui_font_format.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{

using ps2::fonts::FontFormatSummary;
using ps2::fonts::ValidateFontBytes;

static constexpr FontFormatSummary kSentinel{ 101, -102, 103, 104, 105, 106, 107 };

static bool Check(bool passed, const char * message)
{
    if (!passed)
    {
        std::fprintf(stderr, "UI font format failed: %s\n", message);
    }

    return passed;
}

static bool Equal(const FontFormatSummary & first, const FontFormatSummary & second)
{
    return first.glyphCount == second.glyphCount && first.ascender == second.ascender &&
           first.descender == second.descender && first.maxAdvance == second.maxAdvance &&
           first.maxHeight == second.maxHeight && first.duplicateCodepoints == second.duplicateCodepoints &&
           first.samplingGutterGlyphs == second.samplingGutterGlyphs;
}

static void WriteBigShort(std::uint8_t * bytes, std::uint32_t value)
{
    bytes[0] = static_cast<std::uint8_t>(value >> 8U);
    bytes[1] = static_cast<std::uint8_t>(value);
}

static void WriteLittleShort(std::uint8_t * bytes, std::uint32_t value)
{
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8U);
}

static void WriteLittleWord(std::uint8_t * bytes, std::uint32_t value)
{
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8U);
    bytes[2] = static_cast<std::uint8_t>(value >> 16U);
    bytes[3] = static_cast<std::uint8_t>(value >> 24U);
}

static std::vector<std::uint8_t> MakeFont(std::uint32_t count)
{
    std::vector<std::uint8_t> bytes(12U + count * 14U);
    bytes[0] = 'i';
    bytes[1] = 'd';
    bytes[2] = 'f';
    bytes[3] = 42;
    WriteBigShort(bytes.data() + 4, 48);
    WriteBigShort(bytes.data() + 6, 49);
    WriteBigShort(bytes.data() + 8, 65522);
    WriteBigShort(bytes.data() + 10, count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        std::uint8_t * const glyph = bytes.data() + 12U + index * 10U;
        glyph[0] = 90;
        glyph[1] = 48;
        glyph[2] = 255;
        glyph[3] = 252;
        glyph[4] = 92;
        WriteLittleShort(glyph + 6, 1);
        WriteLittleShort(glyph + 8, 1);
        WriteLittleWord(bytes.data() + 12U + count * 10U + index * 4U, 32U + index);
    }

    return bytes;
}

static void WriteIndex(std::vector<std::uint8_t> & bytes, std::uint32_t index, std::uint32_t codepoint)
{
    const std::size_t count = (bytes.size() - 12U) / 14U;
    WriteLittleWord(bytes.data() + 12U + count * 10U + index * 4U, codepoint);
}

static bool Rejected(const void * bytes, std::size_t length, std::uint32_t width = 640,
                     std::uint32_t height = 768)
{
    FontFormatSummary summary = kSentinel;
    return !ValidateFontBytes(bytes, length, width, height, summary) && Equal(summary, kSentinel);
}

static bool ValidFonts()
{
    auto bytes = MakeFont(95);
    FontFormatSummary summary{};
    if (!Check(ValidateFontBytes(bytes.data(), bytes.size(), 640, 768, summary) &&
               Equal(summary, { 95, 49, -14, 92, 48, 0, 0 }),
               "mixed-endian metrics, padded records and ASCII indices"))
    {
        return false;
    }

    // Retail files preserve repeated non-ASCII entries. Last match is a native lookup concern.
    bytes = MakeFont(100);
    WriteIndex(bytes, 95, 169);
    WriteIndex(bytes, 96, 169);
    WriteIndex(bytes, 97, 174);
    WriteIndex(bytes, 98, 174);
    WriteIndex(bytes, 99, 0x10FFFF);
    bytes[12U + 5U] = 0xAA; // Padding is not semantic data in glyphInfo_t.
    WriteLittleShort(bytes.data() + 12U + 6U, 0);
    WriteLittleShort(bytes.data() + 12U + 10U + 6U, 640U - 90U);
    WriteLittleShort(bytes.data() + 12U + 20U + 8U, 0);
    WriteLittleShort(bytes.data() + 12U + 30U + 8U, 768U - 48U);
    if (!Check(ValidateFontBytes(bytes.data(), bytes.size(), 640, 768, summary) &&
               Equal(summary, { 100, 49, -14, 92, 48, 2, 4 }),
               "sorted duplicate Unicode, ignored padding and outer half-texel gutters"))
    {
        return false;
    }

    bytes = MakeFont(128);
    for (std::uint32_t index = 0; index < 128; ++index)
    {
        WriteIndex(bytes, index, 32);
    }

    if (!Check(ValidateFontBytes(bytes.data(), bytes.size(), 640, 768, summary) &&
               summary.duplicateCodepoints == 127,
               "last ASCII cache index127 fits the signed byte"))
    {
        return false;
    }

    bytes = MakeFont(ps2::fonts::kMaxFontGlyphs);
    for (std::uint32_t index = 0; index < ps2::fonts::kMaxFontGlyphs; ++index)
    {
        WriteIndex(bytes, index, 128U + index);
    }

    if (!Check(ValidateFontBytes(bytes.data(), bytes.size(), 640, 768, summary) &&
               summary.glyphCount == ps2::fonts::kMaxFontGlyphs,
               "maximum admitted count and complete bounded payload"))
    {
        return false;
    }

    bytes = MakeFont(1);
    bytes[12] = 0;
    bytes[13] = 0;
    WriteLittleShort(bytes.data() + 18, 16384);
    WriteLittleShort(bytes.data() + 20, 16384);
    return Check(ValidateFontBytes(bytes.data(), bytes.size(), 16384, 16384, summary) &&
                 summary.samplingGutterGlyphs == 0,
                 "blank glyph at maximum atlas extent does not request sampling");
}

static bool InvalidFonts()
{
    const auto valid = MakeFont(95);
    if (!Check(Rejected(nullptr, 0) && Rejected(nullptr, valid.size()) &&
               Rejected(valid.data(), ps2::fonts::kMaxFontBytes + 1U),
               "null and excessive buffers"))
    {
        return false;
    }

    for (std::size_t length = 0; length < valid.size(); ++length)
    {
        if (!Check(Rejected(valid.data(), length), "every truncated buffer leaves summary unchanged"))
        {
            return false;
        }
    }

    auto bytes = valid;
    bytes.push_back(0);
    if (!Check(Rejected(bytes.data(), bytes.size()), "trailing bytes"))
    {
        return false;
    }

    struct Change
    {
        std::size_t offset;
        std::uint32_t value;
    };

    static constexpr Change kChanges[] = {
        { 0, 0 }, { 4, 47 }, { 6, 0xFFFF }, { 6, 4097 }, { 8, 4097 }, { 8, 65536U - 4097U }, { 10, 0 }, { 10, 94 }, { 10, 96 }, { 10, 2049 }, { 10, 32768 }, { 18, 640 }, { 20, 768 }, { 18, 65535 }
    };
    for (const Change & change : kChanges)
    {
        bytes = valid;
        if (change.offset < 12)
        {
            WriteBigShort(bytes.data() + change.offset, change.value);
        }
        else
        {
            WriteLittleShort(bytes.data() + change.offset, change.value);
        }

        if (!Check(Rejected(bytes.data(), bytes.size()), "magic, point size, metrics, count and glyph extent"))
        {
            return false;
        }
    }

    if (!Check(Rejected(valid.data(), valid.size(), 0, 768) && Rejected(valid.data(), valid.size(), 640, 0) &&
               Rejected(valid.data(), valid.size(), 16385, 768) &&
               Rejected(valid.data(), valid.size(), 640, 16385),
               "atlas dimensions"))
    {
        return false;
    }

    static constexpr std::uint32_t kInvalidScalars[] = { 0xD800, 0xDFFF, 0x110000, 0xFFFFFFFF };
    for (const std::uint32_t codepoint : kInvalidScalars)
    {
        bytes = valid;
        WriteIndex(bytes, 94, codepoint);
        if (!Check(Rejected(bytes.data(), bytes.size()), "Unicode scalar limits"))
        {
            return false;
        }
    }

    bytes = valid;
    WriteIndex(bytes, 93, 0);
    if (!Check(Rejected(bytes.data(), bytes.size()), "unsorted character indices"))
    {
        return false;
    }

    bytes = MakeFont(129);
    for (std::uint32_t index = 0; index < 129; ++index)
    {
        WriteIndex(bytes, index, 32);
    }

    return Check(Rejected(bytes.data(), bytes.size()), "ASCII last-match index128 overflows the native signed byte");
}

} // namespace

int main()
{
    if (!ValidFonts() || !InvalidFonts())
    {
        return 1;
    }

    std::puts("UI font format tests passed");
    return 0;
}
