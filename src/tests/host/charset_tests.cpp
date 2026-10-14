// ================================================================================================
// File: charset_tests.cpp
// Brief: Check native character addressing, transparent glyph margins and bounded atlas generation.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/charset.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{
using Pixel = std::array<std::uint8_t, 4>;
using Buffer = std::vector<std::uint8_t>;

static constexpr size_t kGuardBytes = 32;
static constexpr std::uint8_t kGuard = 0xA5;
static constexpr int kCell = ps2::render::kConsoleCellSize;
static constexpr int kAtlas = ps2::render::kConsoleAtlasSize;

bool Check(bool passed, const char * name)
{
    if (!passed) { std::fprintf(stderr, "Console charset failed: %s\n", name); }
    return passed;
}

Pixel Texel(const Buffer & pixels, int code, int x, int y)
{
    const int atlasX = (code & 15) * kCell + x;
    const int atlasY = (code >> 4) * kCell + y;
    const size_t index = static_cast<size_t>(atlasY * kAtlas + atlasX) * 4U;
    return {pixels[index], pixels[index + 1U], pixels[index + 2U], pixels[index + 3U]};
}

bool SameGlyph(const Buffer & pixels, int first, int second)
{
    for (int y = 0; y < kCell; ++y)
    {
        for (int x = 0; x < kCell; ++x)
        {
            if (Texel(pixels, first, x, y) != Texel(pixels, second, x, y)) { return false; }
        }
    }
    return true;
}

int OpaqueCount(const Buffer & pixels, int code)
{
    int count = 0;
    for (int y = 0; y < kCell; ++y)
    {
        for (int x = 0; x < kCell; ++x) { count += Texel(pixels, code, x, y)[3] == 255 ? 1 : 0; }
    }
    return count;
}

bool Bounds()
{
    Buffer guarded(ps2::render::kConsoleAtlasBytes + kGuardBytes * 2U, kGuard);
    auto * const output = guarded.data() + kGuardBytes;
    if (!Check(!ps2::render::MakeConsoleCharset(nullptr, ps2::render::kConsoleAtlasBytes), "null buffer") ||
        !Check(!ps2::render::MakeConsoleCharset(output, ps2::render::kConsoleAtlasBytes - 1U), "short buffer") ||
        !Check(std::all_of(guarded.begin(), guarded.end(), [](std::uint8_t byte) { return byte == kGuard; }),
               "rejected generation changes no bytes") ||
        !Check(ps2::render::MakeConsoleCharset(output, ps2::render::kConsoleAtlasBytes), "exact capacity"))
    {
        return false;
    }
    for (size_t i = 0; i < kGuardBytes; ++i)
    {
        if (!Check(guarded[i] == kGuard && guarded[guarded.size() - 1U - i] == kGuard, "prefix/tail guards")) { return false; }
    }
    return true;
}

bool Pixels(const Buffer & pixels)
{
    for (size_t i = 0; i < pixels.size(); i += 4U)
    {
        if (!Check(pixels[i] == 255 && pixels[i + 1U] == 255 && pixels[i + 2U] == 255,
                   "straight white RGB, including transparent texels") ||
            !Check(pixels[i + 3U] == 0 || pixels[i + 3U] == 255, "binary glyph coverage"))
        {
            return false;
        }
    }
    for (int code = 0; code < 256; ++code)
    {
        for (int x = 0; x < kCell; ++x)
        {
            if (!Check(Texel(pixels, code, x, 0)[3] == 0 && Texel(pixels, code, x, 15)[3] == 0,
                       "top/bottom transparent atlas gutter")) { return false; }
        }
        for (int y = 0; y < kCell; ++y)
        {
            if (!Check(Texel(pixels, code, 15, y)[3] == 0, "right transparent atlas gutter")) { return false; }
        }
    }
    return true;
}

bool Characters(const Buffer & pixels)
{
    if (!Check(OpaqueCount(pixels, ' ') == 0, "space is fully transparent")) { return false; }
    for (int code = 33; code <= 126; ++code)
    {
        if (!Check(OpaqueCount(pixels, code) > 0 && !SameGlyph(pixels, code, 255), "printable ASCII is authored")) { return false; }
    }
    for (int code = 0; code < 256; ++code)
    {
        if (code == 10 || code == 11 || (code >= 32 && code <= 126)) { continue; }
        if (!Check(SameGlyph(pixels, code, 255), "unsupported bytes use the explicit box fallback")) { return false; }
    }
    for (int code = 'A'; code <= 'Z'; ++code)
    {
        if (!Check(!SameGlyph(pixels, code, code - 'A' + 'a'), "uppercase/lowercase remain distinct")) { return false; }
    }
    if (!Check(!SameGlyph(pixels, '0', 'O') && !SameGlyph(pixels, '1', 'I') && !SameGlyph(pixels, '1', 'l'),
               "ambiguous console digits retain distinct glyphs")) { return false; }
    // Native DrawSmallChar addresses byte row/column directly. A sits in row 4, column 1,
    // and its crossbar must span all five columns after expansion for a readable small glyph.
    for (int x = 0; x < 15; ++x)
    {
        if (!Check(Texel(pixels, 'A', x, 7)[3] == 255 && Texel(pixels, 'A', x, 8)[3] == 255,
                   "native byte addressing and full-width A crossbar")) { return false; }
    }
    return true;
}

bool Cursors(const Buffer & pixels)
{
    for (int y = 0; y < kCell; ++y)
    {
        for (int x = 0; x < kCell; ++x)
        {
            const bool column = x >= 2 && x < 14;
            const bool underline = column && y >= 13 && y < 15;
            const bool block = column && y >= 1 && y < 15;
            if (!Check((Texel(pixels, 10, x, y)[3] == 255) == underline, "native insert cursor underline") ||
                !Check((Texel(pixels, 11, x, y)[3] == 255) == block, "native overwrite cursor block"))
            {
                return false;
            }
        }
    }
    return true;
}
} // namespace

int main()
{
    Buffer pixels(ps2::render::kConsoleAtlasBytes, kGuard);
    Buffer reload(ps2::render::kConsoleAtlasBytes, 0);
    if (!Bounds() || !Check(ps2::render::MakeConsoleCharset(pixels.data(), pixels.size()), "atlas generation") ||
        !Pixels(pixels) || !Characters(pixels) || !Cursors(pixels) ||
        !Check(ps2::render::MakeConsoleCharset(reload.data(), reload.size()) && pixels == reload, "reload is deterministic"))
    {
        return 1;
    }
    return 0;
}
