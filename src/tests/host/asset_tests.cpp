// ================================================================================================
// File: asset_tests.cpp
// Brief: Exercise the host asset library's codecs, palettes, packing and GSTX validation, the parts
// a corrupt or mis-converted texture would otherwise reach the target through.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/bimage.h"
#include "assets/bytes.h"
#include "assets/glyph_set.h"
#include "assets/gs_texture.h"
#include "assets/gstx.h"
#include "assets/json.h"
#include "assets/palette.h"
#include "assets/rect_pack.h"
#include "assets/rle.h"
#include "assets/texture_convert.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace
{

using namespace assets;

static bool Check(bool passed, const char * name)
{
    if (!passed)
    {
        std::fprintf(stderr, "Asset library failed: %s\n", name);
    }

    return passed;
}

static int GsAlpha(int alpha)
{
    return (alpha * 128 + 127) / 255;
}

static bool RunLengths()
{
    std::mt19937 random(7);
    bool passed = true;

    for (int round = 0; round < 200; ++round)
    {
        // Mixed runs and literals of every length class, including the 128/130 boundaries.
        std::vector<std::uint8_t> input;
        const int pieces = static_cast<int>(random() % 40);

        for (int piece = 0; piece < pieces; ++piece)
        {
            const std::size_t length = random() % 300;
            const bool run = (random() & 1U) != 0;
            const std::uint8_t value = static_cast<std::uint8_t>(random());

            for (std::size_t index = 0; index < length; ++index)
            {
                input.push_back(run ? value : static_cast<std::uint8_t>(random()));
            }
        }

        const std::vector<std::uint8_t> encoded = RleEncode(input);
        std::vector<std::uint8_t> decoded(input.size());
        passed = passed && RleDecode(encoded, decoded) && decoded == input;
    }

    const std::vector<std::uint8_t> flat(130, 9);
    const std::vector<std::uint8_t> flatCode = RleEncode(flat);
    std::vector<std::uint8_t> output(4);
    const std::vector<std::uint8_t> literal = { 2, 1, 2, 3 };
    passed = Check(passed, "random RLE round trips") && Check(flatCode.size() == 2 && flatCode[0] == 255, "130-byte run is one code") &&
             Check(RleEncode(std::vector<std::uint8_t>(131, 9)).size() == 4, "131-byte run splits");
    std::vector<std::uint8_t> three(3);
    passed = passed && Check(RleDecode(literal, std::span<std::uint8_t>(three)) && three == std::vector<std::uint8_t>({ 1, 2, 3 }), "literal decode");
    passed = passed && Check(!RleDecode(literal, output), "short output is refused");
    std::vector<std::uint8_t> two(2);
    passed = passed && Check(!RleDecode(literal, two), "overflowing literal is refused");
    passed = passed && Check(!RleDecode(std::vector<std::uint8_t>{ 2, 1, 2 }, three), "truncated literal is refused");
    passed = passed && Check(!RleDecode(std::vector<std::uint8_t>{ 128 }, three), "run without its value is refused");
    passed = passed && Check(!RleDecode(std::vector<std::uint8_t>{ 128, 5, 0, 1 }, three), "trailing input is refused");
    return passed;
}

static bool Checksums()
{
    const std::string a = "a";
    const std::string foobar = "foobar";
    return Check(Fnv1a32({}) == 0x811C9DC5U, "FNV-1a32 basis") &&
           Check(Fnv1a32(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(a.data()), a.size())) == 0xE40C292CU, "FNV-1a32 a") &&
           Check(Fnv1a32(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(foobar.data()), foobar.size())) == 0xBF9CF968U,
                 "FNV-1a32 foobar");
}

static bool GsLayouts()
{
    const std::vector<std::uint8_t> indices = { 1, 2, 3, 15 };
    const std::vector<std::uint8_t> packed = PackIndices(indices, 4, 1, IndexFormat::T4);
    bool passed = Check(packed == std::vector<std::uint8_t>({ 0x21, 0xF3 }), "PSMT4 stores the even texel low") &&
                  Check(UnpackIndices(packed, 4, 1, IndexFormat::T4) == indices, "PSMT4 unpack") &&
                  Check(PackIndices(indices, 4, 1, IndexFormat::T8) == indices, "PSMT8 is one byte per texel") &&
                  Check(IndexBytes(IndexFormat::T4, 128, 128) == 8192 && IndexBytes(IndexFormat::T8, 128, 64) == 8192, "one GS page is 8 KiB") &&
                  Check(ClutSwizzle(8) == 16 && ClutSwizzle(16) == 8 && ClutSwizzle(15) == 23 && ClutSwizzle(24) == 24, "CSM1 swaps middle blocks");

    for (std::uint32_t index = 0; index < 256; ++index)
    {
        passed = passed && ClutSwizzle(ClutSwizzle(index)) == index;
    }

    std::vector<PaletteColor> palette;

    for (int index = 0; index < 256; ++index)
    {
        palette.push_back({ static_cast<std::uint8_t>(index), 0, 0, 255 });
    }

    const std::vector<PaletteColor> texels = ClutTexels(palette, IndexFormat::T8);
    const std::vector<PaletteColor> back = PaletteFromClut(texels, IndexFormat::T8);
    bool same = texels[16].r == 8 && texels[8].r == 16;

    for (std::size_t index = 0; index < 256; ++index)
    {
        same = same && back[index].r == palette[index].r;
    }

    return Check(passed, "CSM1 swizzle is an involution") && Check(same, "CLUT texel order round trips");
}

static bool Palettes()
{
    bool passed = true;

    // Every palette alpha must survive the GS conversion as the level its source would get.
    for (int alpha = 0; alpha < 256; ++alpha)
    {
        ColorHistogram histogram;
        histogram.Add({ 10, 20, 30, static_cast<std::uint8_t>(alpha) }, 1.0, 1.0);
        const std::vector<PaletteColor> palette = BuildPalette(histogram, 256);
        passed = passed && palette.size() == 1 && GsAlpha(palette[0].a) == GsAlpha(alpha);
    }

    passed = Check(passed, "palette alpha matches the GS level");
    // Few unique colors with GS-exact alpha map without error.
    RgbaImage image;
    image.Allocate(16, 16);

    for (int y = 0; y < 16; ++y)
    {
        for (int x = 0; x < 16; ++x)
        {
            std::uint8_t * texel = image.At(x, y);
            texel[0] = static_cast<std::uint8_t>((x / 4) * 60);
            texel[1] = static_cast<std::uint8_t>((y / 4) * 60);
            texel[2] = 7;
            texel[3] = (x + y) % 3 == 0 ? 0 : 255;
        }
    }

    ColorHistogram histogram;
    histogram.Add(image);
    std::vector<std::uint8_t> indices;
    const QuantizeStats exact = MapToPalette(image, BuildPalette(histogram, 256), indices);
    passed = passed && Check(exact.rmse == 0.0 && exact.maxError == 0, "palette with spare entries is lossless");
    const std::vector<PaletteColor> alpha = BuildAlphaPalette(histogram, 16);
    passed = passed && Check(alpha.size() <= 16 && alpha[0].a == 0 && alpha[1].a == 255 && alpha[2].r == 255, "alpha ramp keeps both ends");
    // Two images with the same colors share one palette; a disjoint palette stays separate.
    RgbaImage red;
    red.Allocate(8, 8);
    RgbaImage blue = red;

    for (std::size_t index = 0; index < red.pixels.size(); index += 4)
    {
        red.pixels[index] = 255;
        red.pixels[index + 3] = 255;
        blue.pixels[index + 2] = 255;
        blue.pixels[index + 3] = 255;
    }

    ColorHistogram first;
    ColorHistogram second;
    ColorHistogram third;
    first.Add(red);
    second.Add(red);
    third.Add(blue);
    const std::vector<int> shared = GroupPalettes({ &first, &second, &third }, { 1, 0.5, 0.1 });
    return passed && Check(shared.size() == 3 && shared[0] == shared[1] && shared[2] != shared[0], "identical palettes are grouped");
}

static bool Resampling()
{
    RgbaImage checker;
    checker.Allocate(4, 4);

    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            const std::uint8_t value = (x + y) % 2 == 0 ? 255 : 0;
            std::uint8_t * texel = checker.At(x, y);
            texel[0] = texel[1] = texel[2] = value;
            texel[3] = 255;
        }
    }

    const RgbaImage half = ResampleArea(checker, 2, 2);
    // A transparent neighbor must not darken the visible color it is averaged with.
    RgbaImage edge;
    edge.Allocate(2, 1);
    edge.At(0, 0)[0] = 255;
    edge.At(0, 0)[3] = 255;
    const RgbaImage blended = ResampleArea(edge, 1, 1);
    const RgbaImage cropped = Crop(checker, { 3, 3, 3, 3 });
    return Check(half.width == 2 && half.At(1, 1)[0] == 128 && half.At(1, 1)[3] == 255, "2x2 box average") &&
           Check(blended.At(0, 0)[0] == 255 && blended.At(0, 0)[3] == 128, "premultiplied average keeps color") &&
           Check(cropped.At(2, 2)[0] == checker.At(3, 3)[0], "crop clamps to the edge");
}

static bool Packing()
{
    std::mt19937 random(11);
    std::vector<PackItem> items;

    for (int index = 0; index < 120; ++index)
    {
        items.push_back({ static_cast<int>(1 + random() % 200), static_cast<int>(1 + random() % 200) });
    }

    std::vector<PackedPage> pages;
    bool passed = PackPages(items, 512, 512, 128, 64, pages) && !pages.empty();

    for (std::size_t first = 0; passed && first < items.size(); ++first)
    {
        const PackItem & item = items[first];
        const PackedPage & page = pages[static_cast<std::size_t>(item.page)];
        passed = item.x >= 0 && item.y >= 0 && item.x + item.width <= page.width && item.y + item.height <= page.height &&
                 page.width % 128 == 0 && page.height % 64 == 0 && page.width <= 512 && page.height <= 512;

        for (std::size_t second = 0; passed && second < first; ++second)
        {
            const PackItem & other = items[second];
            passed = other.page != item.page || other.x >= item.x + item.width || item.x >= other.x + other.width ||
                     other.y >= item.y + item.height || item.y >= other.y + other.height;
        }
    }

    std::vector<PackItem> oversized = { { 513, 4 } };
    return Check(passed, "packed rectangles stay disjoint inside their pages") &&
           Check(!PackPages(oversized, 512, 512, 128, 64, pages), "oversized items are refused");
}

static RgbaImage Synthetic()
{
    RgbaImage image;
    image.Allocate(96, 64);

    for (int y = 0; y < 64; ++y)
    {
        for (int x = 0; x < 96; ++x)
        {
            std::uint8_t * texel = image.At(x, y);

            if (x < 48)
            {
                // A smooth translucent gradient: needs 8-bit indices.
                texel[0] = static_cast<std::uint8_t>(x * 5);
                texel[1] = static_cast<std::uint8_t>(y * 4);
                texel[2] = static_cast<std::uint8_t>(255 - x * 5);
                texel[3] = static_cast<std::uint8_t>(64 + y * 3);
            }
            else
            {
                // Two flat colors: 4-bit indices, losslessly.
                texel[0] = 200;
                texel[1] = y < 32 ? 10 : 90;
                texel[2] = 30;
                texel[3] = 255;
            }
        }
    }

    return image;
}

static bool Textures()
{
    const RgbaImage image = Synthetic();
    const std::vector<PlacementRequest> requests = { { { 1, 1, 46, 62 }, { 0, 0, 48, 64 }, 0.0 },
                                                     { { 49, 1, 46, 62 }, { 48, 0, 48, 64 }, 0.0 } };
    ConvertPolicy policy;
    ConvertResult result;
    std::string error;
    std::vector<std::uint8_t> file;
    GstxSizes sizes;
    GstxTexture read;
    bool passed = Check(ConvertTexture(image, requests, policy, result, error), "conversion succeeds") &&
                  Check(WriteGstx(result.texture, file, sizes, error), "GSTX writes") && Check(ReadGstx(file, read, error), "GSTX reads back") &&
                  Check(sizes.fileBytes == file.size() && file.size() % 16 == 0, "GSTX size is aligned");

    if (!passed)
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return false;
    }

    passed = Check(result.placements[0].format == IndexFormat::T8 && result.placements[1].format == IndexFormat::T4, "format follows measured error") &&
             Check(result.placements[1].quality.maxError == 0, "flat colors convert losslessly") &&
             Check(result.placements[0].placement.dest.width == 24 && result.placements[0].placement.dest.height == 32, "half scale") &&
             Check(read.placements.size() == 2 && read.pages.size() == result.texture.pages.size(), "tables round trip");

    for (std::size_t page = 0; passed && page < read.pages.size(); ++page)
    {
        passed = read.pages[page].data == result.texture.pages[page].data;
    }

    passed = Check(passed, "page data round trips");
    // Corruption anywhere is refused.
    const auto Refused = [&](std::size_t offset, const char * name)
    {
        std::vector<std::uint8_t> damaged = file;
        damaged[offset] ^= 0x40;
        GstxTexture ignored;
        return Check(!ReadGstx(damaged, ignored, error), name);
    };

    passed = passed && Refused(0, "bad magic") && Refused(60, "table checksum") && Refused(file.size() - 17, "page data");
    std::vector<std::uint8_t> truncated(file.begin(), file.end() - 16);
    passed = passed && Check(!ReadGstx(truncated, read, error), "truncated file");
    std::vector<std::uint8_t> padded = file;
    padded.resize(file.size() + 16, 0);
    passed = passed && Check(!ReadGstx(padded, read, error), "trailing data");
    // Overlapping matches cannot be written.
    GstxTexture overlapping = result.texture;
    overlapping.placements[1].match = overlapping.placements[0].match;
    passed = passed && Check(!WriteGstx(overlapping, file, sizes, error), "overlapping matches");
    // At scale 1 a lossless placement renders back exactly.
    ConvertPolicy full;
    full.scale = 1.0;
    ConvertResult exact;
    passed = passed && Check(ConvertTexture(image, { requests[1] }, full, exact, error), "full-scale conversion");
    const RgbaImage rendered = RenderGstx(exact.texture);
    bool same = true;

    for (int y = 1; y < 63; ++y)
    {
        for (int x = 49; x < 95; ++x)
        {
            for (int channel = 0; channel < 4; ++channel)
            {
                same = same && rendered.At(x, y)[channel] == image.At(x, y)[channel];
            }
        }
    }

    // Coverage-only sources share one 4-bit alpha ramp.
    RgbaImage coverage = image;

    for (std::size_t index = 0; index < coverage.pixels.size(); index += 4)
    {
        coverage.pixels[index] = coverage.pixels[index + 1] = coverage.pixels[index + 2] = 255;
    }

    ConvertResult fonts;
    passed = passed && Check(same, "lossless placement renders exactly") &&
             Check(ConvertTexture(coverage, requests, policy, fonts, error) && fonts.alphaOnly && fonts.texture.cluts.size() == 1 &&
                       fonts.placements[0].format == IndexFormat::T4,
                   "coverage images share one alpha ramp");
    return passed;
}

static void PutBe32(std::vector<std::uint8_t> & bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

// One 4x4 level: big-endian header and level record, then the little-endian block.
static std::vector<std::uint8_t> Bimage(int format, int colorFormat, const std::vector<std::uint8_t> & block)
{
    std::vector<std::uint8_t> bytes(8, 0);
    PutBe32(bytes, 0x0A4D4942);
    PutBe32(bytes, 1);
    PutBe32(bytes, static_cast<std::uint32_t>(format));
    PutBe32(bytes, static_cast<std::uint32_t>(colorFormat));
    PutBe32(bytes, 4);
    PutBe32(bytes, 4);
    PutBe32(bytes, 1);
    PutBe32(bytes, 0);
    PutBe32(bytes, 0);
    PutBe32(bytes, 4);
    PutBe32(bytes, 4);
    PutBe32(bytes, static_cast<std::uint32_t>(block.size()));
    bytes.insert(bytes.end(), block.begin(), block.end());
    return bytes;
}

static bool Images()
{
    // DXT1: red/blue endpoints in four-color mode; texel indices 0, 1, 2, 3 on the first row.
    const std::vector<std::uint8_t> dxt1 = { 0x00, 0xF8, 0x1F, 0x00, 0xE4, 0x00, 0x00, 0x00 };
    RgbaImage image;
    std::string error;
    bool passed = Check(DecodeBimage(Bimage(kBimageFormatDxt1, kBimageColorDefault, dxt1), 0, image, error), "DXT1 decodes");
    passed = passed && Check(image.At(0, 0)[0] == 255 && image.At(1, 0)[2] == 255 && image.At(2, 0)[0] == 170 && image.At(2, 0)[2] == 85 &&
                                 image.At(3, 0)[0] == 85 && image.At(0, 1)[0] == 255,
                             "DXT1 four-color interpolation");
    // DXT5: alpha 255/0 in eight-level mode, alpha index 1 on texel 0, color index 0.
    const std::vector<std::uint8_t> dxt5 = { 255, 0, 0x01, 0, 0, 0, 0, 0, 0x00, 0xF8, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00 };
    passed = passed && Check(DecodeBimage(Bimage(kBimageFormatDxt5, kBimageColorDefault, dxt5), 0, image, error) && image.At(0, 0)[3] == 0 &&
                                 image.At(1, 0)[3] == 255,
                             "DXT5 alpha levels");
    // Green-alpha: white RGB with the decoded green as coverage.
    const std::vector<std::uint8_t> green = { 0xE0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    passed = passed && Check(DecodeBimage(Bimage(kBimageFormatDxt1, kBimageColorGreenAlpha, green), 0, image, error) && image.At(0, 0)[0] == 255 &&
                                 image.At(0, 0)[3] == 255,
                             "green-alpha decodes white coverage");
    std::vector<std::uint8_t> bad = Bimage(kBimageFormatDxt1, kBimageColorDefault, dxt1);
    bad[8] ^= 1;
    std::vector<std::uint8_t> trailing = Bimage(kBimageFormatDxt1, kBimageColorDefault, dxt1);
    trailing.push_back(0);
    return passed && Check(!DecodeBimage(bad, 0, image, error), "bad BIMAGE magic") &&
           Check(!DecodeBimage(trailing, 0, image, error), "trailing BIMAGE bytes") &&
           Check(!DecodeBimage(Bimage(1, kBimageColorDefault, std::vector<std::uint8_t>(64)), 0, image, error), "uncompressed decode refused");
}

static bool Json()
{
    JsonValue value;
    std::string error;
    const bool parsed = ParseJson(R"({"a": [1, 2.5, -3e2], "b": {"c": "x\"y\u0041"}, "d": true, "e": null})", value, error);
    bool passed = Check(parsed && value.Find("a")->items.size() == 3 && value.Find("a")->items[2].number == -300.0 &&
                            value.Find("b")->Find("c")->text == "x\"yA" && value.Find("d")->boolean && value.Find("a")->items[0].IsInteger(0, 1),
                        "JSON document parses");

    for (const char * invalid : { "{\"a\": 1,}", "{\"a\": 1, \"a\": 2}", "[1 2]", "\"\\u00e9\"", "01x", "{\"a\" 1}", "[\"\n\"]", "[] []" })
    {
        passed = passed && Check(!ParseJson(invalid, value, error), invalid);
    }

    std::string deep;

    for (int depth = 0; depth < 70; ++depth)
    {
        deep += "[";
    }

    return passed && Check(!ParseJson(deep, value, error), "nesting bound") && Check(JsonString("a\"\\\n") == "\"a\\\"\\\\\\u000a\"", "JSON escaping");
}

// Alpha of one bitmap (the glyph or the outline half of its rectangle) read back through the GSTX.
static std::vector<int> BitmapAlpha(const GstxTexture & texture, const PrerenderedGlyphResult & glyph, bool outline, int & width, int & height)
{
    width = glyph.outline ? glyph.rect.width / 2 : glyph.rect.width;
    height = glyph.rect.height;
    std::vector<int> alpha;

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            PaletteColor color;
            SampleGstx(texture, glyph.rect.x + (outline ? width : 0) + x + 0.5, glyph.rect.y + y + 0.5, color);
            alpha.push_back(color.a);
        }
    }

    return alpha;
}

// Every placement is one 1:1 band, one per page, and every glyph rectangle lies inside one band
// without overlapping another glyph.
static bool GlyphBands(const GlyphSetResult & result)
{
    if (result.texture.placements.size() != result.texture.pages.size())
    {
        return false;
    }

    for (const GstxPlacement & placement : result.texture.placements)
    {
        if (placement.match.x != placement.source.x || placement.match.y != placement.source.y ||
            placement.match.width != placement.source.width || placement.match.height != placement.source.height ||
            placement.dest.width != placement.match.width || placement.dest.height != placement.match.height)
        {
            return false;
        }
    }

    std::vector<Rect> rects;

    for (const GlyphSizeResult & size : result.sizes)
    {
        for (const PrerenderedGlyphResult & glyph : size.glyphs)
        {
            if (glyph.rect.width == 0)
            {
                continue;
            }

            const Rect & rect = glyph.rect;
            int bands = 0;

            for (const GstxPlacement & placement : result.texture.placements)
            {
                const Rect & band = placement.match;
                bands += rect.x >= band.x && rect.y >= band.y && rect.x + rect.width <= band.x + band.width &&
                                 rect.y + rect.height <= band.y + band.height
                             ? 1
                             : 0;
            }

            for (const Rect & other : rects)
            {
                if (rect.x < other.x + other.width && other.x < rect.x + rect.width && rect.y < other.y + other.height &&
                    other.y < rect.y + rect.height)
                {
                    return false;
                }
            }

            if (bands != 1)
            {
                return false;
            }

            rects.push_back(rect);
        }
    }

    return true;
}

static bool GlyphSets()
{
    // A 12x24 solid block and a 4x24 bar standing on their baselines, and an empty space.
    RgbaImage atlas;
    atlas.Allocate(64, 64);

    for (int y = 8; y < 32; ++y)
    {
        for (int x = 0; x < 64; ++x)
        {
            atlas.At(x, y)[3] = (x >= 20 && x < 32) || (x >= 40 && x < 44) ? 255 : 0;
        }
    }

    const std::vector<GlyphMetrics> glyphs = { { 32, 60, 60, 0, 0, 1, 0 }, { 65, 20, 8, 12, 24, 1, 24 }, { 73, 40, 8, 4, 24, 1, 24 } };
    const std::vector<GlyphSizeRequest> sizes = { { 0.25, 0.25, 0.0 }, { 0.25, 0.25, 2.0 } };
    GlyphSetResult result;
    GlyphSetResult again;
    std::string error;
    std::vector<std::uint8_t> file;
    std::vector<std::uint8_t> repeated;
    GstxSizes fileSizes;
    GstxTexture read;
    bool passed = Check(RenderGlyphSet(atlas, glyphs, sizes, 512, result, error) && WriteGstx(result.texture, file, fileSizes, error) &&
                            ReadGstx(file, read, error),
                        "glyph set renders, writes and reads back") &&
                  Check(RenderGlyphSet(atlas, glyphs, sizes, 512, again, error) && WriteGstx(again.texture, repeated, fileSizes, error) &&
                            repeated == file,
                        "glyph set conversion is deterministic") &&
                  Check(result.sizes.size() == 2 && result.sizes[0].glyphs.size() == 3 && result.sizes[0].glyphs[0].rect.width == 0 &&
                            result.sizes[1].glyphs[0].rect.width == 0 && result.sizes[0].glyphs[1].rect.width != 0 &&
                            result.texture.pages.size() == 1,
                        "an empty glyph takes no rectangle") &&
                  Check(GlyphBands(result), "every glyph bitmap lies in one band mapping 1:1 onto its page");

    if (!passed)
    {
        return false;
    }

    // At a quarter scale the block is exactly 3x6 pixels: the sharpest phases leave no partial pixel.
    int width = 0;
    int height = 0;
    const PrerenderedGlyphResult & block = result.sizes[0].glyphs[1];
    const std::vector<int> plain = BitmapAlpha(result.texture, block, false, width, height);
    int opaque = 0;
    int partial = 0;

    for (const int value : plain)
    {
        opaque += value == 255 ? 1 : 0;
        partial += value != 0 && value != 255 ? 1 : 0;
    }

    passed = Check(!block.outline && width == 3 && height == 6 && opaque == 18 && partial == 0 && block.originX == 0 && block.originY == 96,
                   "phases align a whole-pixel glyph exactly, its origin on the left edge and baseline");
    const PrerenderedGlyphResult & stroked = result.sizes[1].glyphs[1];
    int strokedWidth = 0;
    int strokedHeight = 0;
    int outlineWidth = 0;
    int outlineHeight = 0;
    const std::vector<int> glyphHalf = BitmapAlpha(result.texture, stroked, false, strokedWidth, strokedHeight);
    const std::vector<int> outlineHalf = BitmapAlpha(result.texture, stroked, true, outlineWidth, outlineHeight);
    long glyphInk = 0;
    long outlineInk = 0;

    for (std::size_t index = 0; index < glyphHalf.size(); ++index)
    {
        glyphInk += glyphHalf[index];
        outlineInk += outlineHalf[index];
    }

    passed = passed && Check(stroked.outline && stroked.rect.width % 2 == 0 && strokedWidth > 3 && outlineInk > glyphInk && glyphInk > 0,
                             "an outlined glyph pairs its bitmap with a wider outline of the same size");
    // Four times enlarged, the bitmaps outgrow one 128x128 page: each page becomes its own band.
    GlyphSetResult large;
    const bool spread = RenderGlyphSet(atlas, glyphs, { { 4.0, 4.0, 0.0 }, { 4.0, 4.0, 2.0 } }, 128, large, error);
    int largeWidth = 0;
    int largeHeight = 0;
    const std::vector<int> largeBlock = spread ? BitmapAlpha(large.texture, large.sizes[0].glyphs[1], false, largeWidth, largeHeight)
                                               : std::vector<int>();
    passed = passed && Check(spread && large.texture.pages.size() >= 2 && GlyphBands(large) && largeWidth == 48 && largeHeight == 96 &&
                                 std::count(largeBlock.begin(), largeBlock.end(), 255) == 48 * 96,
                             "a glyph set spanning several pages keeps every bitmap whole inside one band");
    std::vector<GlyphMetrics> unordered = glyphs;
    std::swap(unordered[1], unordered[2]);
    std::vector<GlyphMetrics> outside = glyphs;
    outside[1].s = 60;
    GlyphSetResult refused;
    return passed && Check(!RenderGlyphSet(atlas, unordered, sizes, 512, refused, error), "glyphs out of codepoint order are refused") &&
           Check(!RenderGlyphSet(atlas, outside, sizes, 512, refused, error), "a glyph outside the atlas is refused") &&
           Check(!RenderGlyphSet(atlas, glyphs, { { 0.0, 0.25, 0.0 } }, 512, refused, error), "a zero scale is refused") &&
           Check(!RenderGlyphSet(atlas, {}, sizes, 512, refused, error), "an empty glyph list is refused");
}

} // namespace

int main()
{
    if (!RunLengths() || !Checksums() || !GsLayouts() || !Palettes() || !Resampling() || !Packing() || !Textures() || !Images() || !Json() ||
        !GlyphSets())
    {
        return 1;
    }

    std::puts("Asset library tests passed.");
    return 0;
}
