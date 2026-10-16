// ================================================================================================
// File: charset.cpp
// Brief: Keep readable native console glyphs authored and reproducible from code.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/charset.h"

#include <cstdint>

namespace ps2::render
{
namespace
{
// Rows run left-to-right through bits 4..0. Three-wide columns survive the 512/640 console scale.
static constexpr std::uint8_t kAscii[95][7] = {
    { 0, 0, 0, 0, 0, 0, 0 },        // space
    { 4, 4, 4, 4, 4, 0, 4 },        // !
    { 10, 10, 10, 0, 0, 0, 0 },     // "
    { 10, 10, 31, 10, 31, 10, 10 }, // #
    { 4, 15, 20, 14, 5, 30, 4 },    // $
    { 24, 25, 2, 4, 8, 19, 3 },     // %
    { 12, 18, 20, 8, 21, 18, 13 },  // &
    { 4, 4, 8, 0, 0, 0, 0 },        // '
    { 2, 4, 8, 8, 8, 4, 2 },        // (
    { 8, 4, 2, 2, 2, 4, 8 },        // )
    { 0, 4, 21, 14, 21, 4, 0 },     // *
    { 0, 4, 4, 31, 4, 4, 0 },       // +
    { 0, 0, 0, 0, 4, 4, 8 },        // ,
    { 0, 0, 0, 31, 0, 0, 0 },       // -
    { 0, 0, 0, 0, 0, 4, 4 },        // .
    { 1, 2, 2, 4, 8, 8, 16 },       // /
    { 14, 17, 19, 21, 25, 17, 14 }, // 0
    { 4, 12, 4, 4, 4, 4, 14 },      // 1
    { 14, 17, 1, 2, 4, 8, 31 },     // 2
    { 30, 1, 1, 14, 1, 1, 30 },     // 3
    { 2, 6, 10, 18, 31, 2, 2 },     // 4
    { 31, 16, 16, 30, 1, 1, 30 },   // 5
    { 6, 8, 16, 30, 17, 17, 14 },   // 6
    { 31, 1, 2, 4, 8, 8, 8 },       // 7
    { 14, 17, 17, 14, 17, 17, 14 }, // 8
    { 14, 17, 17, 15, 1, 2, 12 },   // 9
    { 0, 4, 4, 0, 4, 4, 0 },        // :
    { 0, 4, 4, 0, 4, 4, 8 },        // ;
    { 1, 2, 4, 8, 4, 2, 1 },        // <
    { 0, 0, 31, 0, 31, 0, 0 },      // =
    { 16, 8, 4, 2, 4, 8, 16 },      // >
    { 14, 17, 1, 2, 4, 0, 4 },      // ?
    { 14, 17, 23, 21, 23, 16, 14 }, // @
    { 14, 17, 17, 31, 17, 17, 17 }, // A
    { 30, 17, 17, 30, 17, 17, 30 }, // B
    { 14, 17, 16, 16, 16, 17, 14 }, // C
    { 30, 17, 17, 17, 17, 17, 30 }, // D
    { 31, 16, 16, 30, 16, 16, 31 }, // E
    { 31, 16, 16, 30, 16, 16, 16 }, // F
    { 14, 17, 16, 23, 17, 17, 15 }, // G
    { 17, 17, 17, 31, 17, 17, 17 }, // H
    { 14, 4, 4, 4, 4, 4, 14 },      // I
    { 7, 2, 2, 2, 2, 18, 12 },      // J
    { 17, 18, 20, 24, 20, 18, 17 }, // K
    { 16, 16, 16, 16, 16, 16, 31 }, // L
    { 17, 27, 21, 21, 17, 17, 17 }, // M
    { 17, 25, 25, 21, 19, 19, 17 }, // N
    { 14, 17, 17, 17, 17, 17, 14 }, // O
    { 30, 17, 17, 30, 16, 16, 16 }, // P
    { 14, 17, 17, 17, 21, 18, 13 }, // Q
    { 30, 17, 17, 30, 20, 18, 17 }, // R
    { 15, 16, 16, 14, 1, 1, 30 },   // S
    { 31, 4, 4, 4, 4, 4, 4 },       // T
    { 17, 17, 17, 17, 17, 17, 14 }, // U
    { 17, 17, 17, 17, 17, 10, 4 },  // V
    { 17, 17, 17, 21, 21, 27, 17 }, // W
    { 17, 17, 10, 4, 10, 17, 17 },  // X
    { 17, 17, 10, 4, 4, 4, 4 },     // Y
    { 31, 1, 2, 4, 8, 16, 31 },     // Z
    { 14, 8, 8, 8, 8, 8, 14 },      // [
    { 16, 8, 8, 4, 2, 2, 1 },       // backslash
    { 14, 2, 2, 2, 2, 2, 14 },      // ]
    { 4, 10, 17, 0, 0, 0, 0 },      // ^
    { 0, 0, 0, 0, 0, 0, 31 },       // _
    { 8, 4, 2, 0, 0, 0, 0 },        // `
    { 0, 0, 14, 1, 15, 17, 15 },    // a
    { 16, 16, 30, 17, 17, 17, 30 }, // b
    { 0, 0, 14, 17, 16, 17, 14 },   // c
    { 1, 1, 15, 17, 17, 17, 15 },   // d
    { 0, 0, 14, 17, 31, 16, 14 },   // e
    { 6, 8, 8, 30, 8, 8, 8 },       // f
    { 0, 15, 17, 17, 15, 1, 14 },   // g
    { 16, 16, 30, 17, 17, 17, 17 }, // h
    { 4, 0, 12, 4, 4, 4, 14 },      // i
    { 2, 0, 6, 2, 2, 18, 12 },      // j
    { 16, 16, 18, 20, 24, 20, 18 }, // k
    { 12, 4, 4, 4, 4, 4, 14 },      // l
    { 0, 0, 26, 21, 21, 17, 17 },   // m
    { 0, 0, 30, 17, 17, 17, 17 },   // n
    { 0, 0, 14, 17, 17, 17, 14 },   // o
    { 0, 30, 17, 17, 30, 16, 16 },  // p
    { 0, 15, 17, 17, 15, 1, 1 },    // q
    { 0, 0, 22, 25, 16, 16, 16 },   // r
    { 0, 0, 15, 16, 14, 1, 30 },    // s
    { 8, 8, 30, 8, 8, 9, 6 },       // t
    { 0, 0, 17, 17, 17, 19, 13 },   // u
    { 0, 0, 17, 17, 17, 10, 4 },    // v
    { 0, 0, 17, 17, 21, 21, 10 },   // w
    { 0, 0, 17, 10, 4, 10, 17 },    // x
    { 0, 17, 17, 17, 15, 1, 14 },   // y
    { 0, 0, 31, 2, 4, 8, 31 },      // z
    { 2, 4, 4, 8, 4, 4, 2 },        // {
    { 4, 4, 4, 4, 4, 4, 4 },        // |
    { 8, 4, 4, 2, 4, 4, 8 },        // }
    { 0, 0, 8, 21, 2, 0, 0 },       // ~
};
static constexpr std::uint8_t kBox[7] = { 31, 17, 17, 17, 17, 17, 31 };

bool GlyphPixel(int code, int x, int y)
{
    if (code == 10)
    {
        return x >= 2 && x < 14 && y >= 13 && y < 15;
    }
    if (code == 11)
    {
        return x >= 2 && x < 14 && y >= 1 && y < 15;
    }
    if (x >= 15 || y < 1 || y >= 15)
    {
        return false;
    }
    const auto * const rows = code >= 32 && code <= 126 ? kAscii[code - 32] : kBox;
    const auto bit = static_cast<std::uint8_t>(1U << static_cast<unsigned int>(4 - x / 3));
    return (rows[(y - 1) / 2] & bit) != 0;
}
} // namespace

bool MakeConsoleCharset(void * rgba, size_t capacity)
{
    if (rgba == nullptr || capacity < kConsoleAtlasBytes)
    {
        return false;
    }
    auto * const pixels = static_cast<std::uint8_t *>(rgba);
    for (int code = 0; code < 256; ++code)
    {
        const int cellX = (code & 15) * kConsoleCellSize;
        const int cellY = (code >> 4) * kConsoleCellSize;
        for (int y = 0; y < kConsoleCellSize; ++y)
        {
            for (int x = 0; x < kConsoleCellSize; ++x)
            {
                const size_t offset = static_cast<size_t>((cellY + y) * kConsoleAtlasSize + cellX + x) * 4U;
                pixels[offset + 0U] = 255;
                pixels[offset + 1U] = 255;
                pixels[offset + 2U] = 255;
                pixels[offset + 3U] = GlyphPixel(code, x, y) ? 255 : 0;
            }
        }
    }
    return true;
}

bool MakeAuthoredFontCharset(void * rgba, size_t capacity)
{
    if (rgba == nullptr || capacity < kAuthoredFontAtlasBytes)
    {
        return false;
    }
    auto * const pixels = static_cast<std::uint8_t *>(rgba);
    for (size_t i = 0; i < kAuthoredFontAtlasBytes; i += 4U)
    {
        pixels[i + 0U] = pixels[i + 1U] = pixels[i + 2U] = 255;
        pixels[i + 3U] = 0;
    }
    for (int code = 32; code <= 126; ++code)
    {
        const int cellX = (code & 15) * 32 + 1;
        const int cellY = (code >> 4) * 16 + 1;
        for (int y = 0; y < 14; ++y)
        {
            for (int x = 0; x < 15; ++x)
            {
                const size_t offset = static_cast<size_t>((cellY + y) * kAuthoredFontAtlasWidth + cellX + x) * 4U;
                pixels[offset + 3U] = GlyphPixel(code, x, y + 1) ? 255 : 0;
            }
        }
    }
    return true;
}
} // namespace ps2::render
