// ================================================================================================
// File: texture_format_tests.cpp
// Brief: Check the GS memory emulation against TimeSplitters' published PSMT8 swizzle and Black's
// mip layout, GSTX v2 round trips through the cook's reader and the target validator, and their
// refusals of corrupted files.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/bytes.h"
#include "assets/gs_memory.h"
#include "assets/gstx_v2.h"
#include "ps2/renderer/texture_format.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace
{

using namespace assets;

alignas(64) static std::uint8_t s_aligned[ps2::renderer::tex::kImageMaxFileBytes];

static bool Check(bool passed, const char * name)
{
    if (!passed)
    {
        std::fprintf(stderr, "FAIL: %s\n", name);
    }

    return passed;
}

static const char * TargetValidate(const std::vector<std::uint8_t> & file, ps2::renderer::tex::Image & image)
{
    if (file.size() > sizeof(s_aligned))
    {
        return "file larger than the test buffer";
    }

    std::memcpy(s_aligned, file.data(), file.size());
    return ps2::renderer::tex::ValidateImage(s_aligned, static_cast<std::uint32_t>(file.size()), image);
}

// TimeSplitters: Future Perfect's PSMT8 levels are the bytes a w/2 x h/2 PSMCT32 transfer writes;
// this is its unswizzle, from the file format write-up (docs/05-textures.md in timesplitters-re).
static std::vector<std::uint8_t> TimeSplittersUnswizzle(const std::vector<std::uint8_t> & source, int width, int height)
{
    std::vector<std::uint8_t> output(static_cast<std::size_t>(width * height));

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const int block = (y & ~0xF) * width + (x & ~0xF) * 2;
            const int swap = (((y + 2) >> 2) & 1) * 4;
            const int row = (((y & ~3) >> 1) + (y & 1)) & 7;
            const int column = row * width * 2 + ((x + swap) & 7) * 4;
            const int byte = ((y >> 1) & 1) + ((x >> 2) & 2);
            output[static_cast<std::size_t>(y * width + x)] = source[static_cast<std::size_t>(block + column + byte)];
        }
    }

    return output;
}

static bool TimeSplittersSwizzle()
{
    bool passed = true;
    static constexpr int kSizes[][2] = { { 64, 64 }, { 128, 64 }, { 256, 128 }, { 128, 256 } };

    for (const auto & size : kSizes)
    {
        const int width = size[0];
        const int height = size[1];
        const std::uint32_t tbw = static_cast<std::uint32_t>(std::max(2, width / 64));
        GsMemory memory(256);
        std::vector<std::uint8_t> texels(static_cast<std::size_t>(width * height));

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::uint8_t value = static_cast<std::uint8_t>((x * 7 + y * 13 + (x >> 4) * 5 + (y >> 3) * 11) & 0xFF);
                texels[static_cast<std::size_t>(y * width + x)] = value;
                passed = passed && memory.Write(GsPsm::T8, 0, tbw, x, y, value);
            }
        }

        // The PSMCT32 transfer of the same rectangle, at half the width and height and buffer width.
        std::vector<std::uint8_t> upload;

        for (int y = 0; y < height / 2; ++y)
        {
            for (int x = 0; x < width / 2; ++x)
            {
                std::uint32_t word = 0;
                passed = passed && memory.Read(GsPsm::Ct32, 0, tbw / 2, x, y, word);

                for (int index = 0; index < 4; ++index)
                {
                    upload.push_back(static_cast<std::uint8_t>(word >> (8 * index)));
                }
            }
        }

        passed = passed && TimeSplittersUnswizzle(upload, width, height) == texels;
    }

    return Check(passed, "PSMT8 through a PSMCT32 transfer matches TimeSplitters' unswizzle");
}

static std::vector<std::uint32_t> Pattern(int width, int height, std::uint32_t limit, std::uint32_t seed)
{
    std::mt19937 random(seed);
    std::vector<std::uint32_t> values(static_cast<std::size_t>(width * height));

    for (std::uint32_t & value : values)
    {
        value = static_cast<std::uint32_t>(random()) % (limit + 1U);
    }

    return values;
}

static GstxV2Image MakeImage(GsPsm psm, int width, int height, int levels, std::uint32_t seed)
{
    GstxV2Image image;
    image.width = width;
    image.height = height;
    image.psm = psm;
    image.wrapT = GstxWrap::Clamp;
    const std::uint32_t limit = psm == GsPsm::T8 ? 0xFFU : psm == GsPsm::T4 ? 0xFU : psm == GsPsm::Ct16 ? 0xFFFFU : 0x80FFFFFFU;

    for (int level = 0; level < levels; ++level)
    {
        auto texels = Pattern(std::max(1, width >> level), std::max(1, height >> level), limit, seed + static_cast<std::uint32_t>(level));

        if (psm == GsPsm::Ct32)
        {
            for (std::uint32_t & texel : texels)
            {
                texel = (texel & 0x00FFFFFFU) | ((texel >> 24) % 0x81U) << 24;
            }
        }

        image.levels.push_back(std::move(texels));
    }

    const std::size_t entries = psm == GsPsm::T8 ? 256U : psm == GsPsm::T4 ? 16U : 0U;

    for (std::size_t entry = 0; entry < entries; ++entry)
    {
        image.palette.push_back(static_cast<std::uint32_t>(entry * 0x010203U) | (static_cast<std::uint32_t>(entry % 0x81U) << 24));
    }

    return image;
}

// Black's GLOBDATA bg1_asr_shl: a 64x64 PSMT4 texture with three mips at blocks 0, 8, 10, 11.
static bool BlackLayout()
{
    std::vector<std::uint8_t> file;
    std::string error;
    GstxV2Texture texture;
    const bool written = WriteGstxV2(MakeImage(GsPsm::T4, 64, 64, 4, 1), file, error) && ReadGstxV2(file, texture, error);
    const std::uint64_t mip = texture.registers.miptbp1;

    return Check(written && (texture.registers.tex0 & 0x3FFFU) == 0 && ((texture.registers.tex0 >> 14) & 0x3FU) == 2 &&
                 (mip & 0x3FFFU) == 8 && ((mip >> 14) & 0x3FU) == 2 && ((mip >> 20) & 0x3FFFU) == 10 &&
                 ((mip >> 40) & 0x3FFFU) == 11 && ((texture.registers.tex0 >> 37) & 0x3FFFU) == 12 && texture.blocks == 13,
                 "PSMT4 mips land where Black's texture has them");
}

static bool Rectangles()
{
    bool passed = true;

    for (std::uint32_t blocks = 1; blocks <= kGstxV2MaxBlocks; ++blocks)
    {
        std::vector<std::uint32_t> covered;

        for (const GstxV2Rectangle & rectangle : GstxV2Rectangles(blocks))
        {
            passed = passed && rectangle.y + rectangle.height <= 2048 && rectangle.width * rectangle.height / 4 <= 32767;

            for (int y = rectangle.y; y < rectangle.y + rectangle.height; y += 8)
            {
                for (int x = rectangle.x; x < rectangle.x + rectangle.width; x += 8)
                {
                    covered.push_back(GsBlockNumber(GsPsm::Ct32, x, y, 0, 1));
                }
            }
        }

        std::sort(covered.begin(), covered.end());
        std::vector<std::uint32_t> expected(blocks);

        for (std::uint32_t block = 0; block < blocks; ++block)
        {
            expected[block] = block;
        }

        passed = passed && covered == expected && GstxV2Rectangles(blocks).size() <= kGstxV2MaxRectangles;
    }

    return Check(passed, "upload rectangles cover every footprint once, in block order");
}

static bool RoundTrip(GsPsm psm, int width, int height, int levels, const char * name)
{
    const GstxV2Image image = MakeImage(psm, width, height, levels, 7);
    std::vector<std::uint8_t> file;
    std::string error;
    GstxV2Texture texture;
    bool passed = WriteGstxV2(image, file, error) && ReadGstxV2(file, texture, error);

    for (int level = 0; passed && level < levels; ++level)
    {
        const int levelWidth = std::max(1, width >> level);
        const int levelHeight = std::max(1, height >> level);

        for (int y = 0; passed && y < levelHeight; ++y)
        {
            for (int x = 0; passed && x < levelWidth; ++x)
            {
                std::uint32_t value = 0;
                const std::uint32_t texel = image.levels[static_cast<std::size_t>(level)][static_cast<std::size_t>(y * levelWidth + x)];
                const std::uint32_t expected = image.palette.empty() ? texel : image.palette[texel];
                passed = SampleGstxV2(texture, level, x, y, value) && value == expected;
            }
        }
    }

    ps2::renderer::tex::Image target{};
    const char * targetError = TargetValidate(file, target);
    passed = passed && targetError == nullptr && target.blocks == texture.blocks && target.tex0 == texture.registers.tex0 &&
             target.miptbp1 == texture.registers.miptbp1 && target.miptbp2 == texture.registers.miptbp2 &&
             target.tex1 == texture.registers.tex1 && target.levels == static_cast<std::uint32_t>(levels);

    if (!passed)
    {
        std::fprintf(stderr, "  %s: %s\n", name, targetError != nullptr ? targetError : error.c_str());
    }

    return Check(passed, name);
}

// Rewrites both checksums, so a test reaches the field checks behind them.
static void Reseal(std::vector<std::uint8_t> & file)
{
    WriteLe32(file.data() + 24, Fnv1a32({ file.data() + kGstxV2HeaderBytes, file.size() - kGstxV2HeaderBytes }));
    WriteLe32(file.data() + 28, 0);
    WriteLe32(file.data() + 28, Fnv1a32({ file.data(), kGstxV2HeaderBytes }));
}

static bool Refusals()
{
    std::vector<std::uint8_t> file;
    std::string error;
    bool passed = WriteGstxV2(MakeImage(GsPsm::T8, 128, 64, 3, 3), file, error);

    struct Corruption
    {
        std::size_t offset;
        std::uint8_t value;
        bool targetRefuses;
        const char * name;
    };

    // The target cannot see overlapping levels or a moved CLUT that stays inside the footprint; the
    // cook's canonical check does.
    const Corruption corruptions[] = {
        { 8, 0x60, true, "width" },
        { 12, 0x14, true, "format" },
        { 13, 0x08, true, "level count" },
        { 14, 0x09, true, "usage" },
        { 15, 0x02, true, "rectangle count" },
        { 16, 0xFF, true, "blocks" },
        { 32 + 1, 0x80, true, "TEX0 TBW" },
        { 32 + 4, 0xFF, true, "TEX0 CBP past the footprint" },
        { 32 + 4, 0x40, false, "TEX0 CBP inside the footprint" },
        { 40, 0x04, true, "TEX1 MXL" },
        { 48, 0x01, false, "MIPTBP1 TBP1 moved" },
        { 48 + 3, 0xF0, true, "MIPTBP1 past the footprint" },
        { 60, 0x01, true, "MIPTBP2 unused level" },
        { 64, 0x02, true, "CLAMP region" },
        { 80, 0x01, true, "reserved" },
        { kGstxV2HeaderBytes + 16, 0x08, true, "TRXPOS" },
        { kGstxV2HeaderBytes + 64, 0x01, true, "IMAGE size" },
        { kGstxV2HeaderBytes + 64 + 1, 0x00, true, "IMAGE EOP" },
    };

    for (const Corruption & corruption : corruptions)
    {
        std::vector<std::uint8_t> bad = file;
        bad[corruption.offset] = static_cast<std::uint8_t>(bad[corruption.offset] ^ corruption.value);

        if (corruption.offset == kGstxV2HeaderBytes + 64 + 1)
        {
            bad[corruption.offset] = static_cast<std::uint8_t>(bad[corruption.offset] | 0x80U);
        }

        Reseal(bad);
        GstxV2Texture texture;
        ps2::renderer::tex::Image image{};
        const bool cookRefuses = !ReadGstxV2(bad, texture, error);
        const bool targetRefuses = TargetValidate(bad, image) != nullptr;

        if (!cookRefuses || targetRefuses != corruption.targetRefuses)
        {
            std::fprintf(stderr, "  corruption %s: cook %s, target %s\n", corruption.name, cookRefuses ? "refuses" : "admits",
                         targetRefuses ? "refuses" : "admits");
            passed = false;
        }
    }

    // Checksums, truncation and a changed texel the checksum still covers.
    std::vector<std::uint8_t> texel = file;
    texel[kGstxV2HeaderBytes + 80] ^= 1U;
    std::vector<std::uint8_t> header = file;
    header[9] ^= 1U;
    GstxV2Texture texture;
    ps2::renderer::tex::Image image{};
    passed = passed && !ReadGstxV2(texel, texture, error) && TargetValidate(texel, image) != nullptr;
    passed = passed && !ReadGstxV2(header, texture, error) && TargetValidate(header, image) != nullptr;
    passed = passed && !ReadGstxV2({ file.data(), file.size() - 16 }, texture, error) &&
             TargetValidate(std::vector<std::uint8_t>(file.begin(), file.end() - 16), image) != nullptr;
    Reseal(texel);
    passed = passed && ReadGstxV2(texel, texture, error) && TargetValidate(texel, image) == nullptr;

    // Images the writer must refuse.
    GstxV2Image odd = MakeImage(GsPsm::T8, 96, 64, 1, 1);
    GstxV2Image large = MakeImage(GsPsm::T8, 1024, 8, 1, 1);
    GstxV2Image tiny = MakeImage(GsPsm::T8, 64, 4, 4, 1);
    GstxV2Image palette = MakeImage(GsPsm::T4, 16, 16, 1, 1);
    palette.palette.push_back(0);
    GstxV2Image alpha = MakeImage(GsPsm::T8, 16, 16, 1, 1);
    alpha.palette[3] |= 0x81000000U;
    GstxV2Image range = MakeImage(GsPsm::T4, 16, 16, 1, 1);
    range.levels[0][5] = 16;
    GstxV2Image big = MakeImage(GsPsm::Ct32, 512, 512, 1, 1);
    std::vector<std::uint8_t> unused;

    for (const GstxV2Image * refused : { &odd, &large, &tiny, &palette, &alpha, &range, &big })
    {
        passed = passed && !WriteGstxV2(*refused, unused, error);
    }

    return Check(passed, "corrupted files and invalid images are refused");
}

static bool Footprints()
{
    // The footprints the plan quotes: every level and the CLUT, with no block left unused.
    struct Case
    {
        GsPsm psm;
        int width;
        int height;
        int levels;
        std::uint32_t blocks;
    };

    static constexpr Case kCases[] = {
        { GsPsm::T8, 64, 64, 4, 26 },
        { GsPsm::T8, 128, 128, 5, 90 },
        { GsPsm::T8, 256, 256, 6, 346 },
        { GsPsm::T4, 128, 128, 5, 45 },
        { GsPsm::T4, 256, 256, 6, 173 },
        { GsPsm::T8, 512, 512, 7, 1370 },
    };

    bool passed = true;

    for (const Case & item : kCases)
    {
        std::vector<std::uint8_t> file;
        std::string error;
        GstxV2Texture texture;
        const bool ok = WriteGstxV2(MakeImage(item.psm, item.width, item.height, item.levels, 5), file, error) && ReadGstxV2(file, texture, error);

        if (!ok || texture.blocks != item.blocks)
        {
            std::fprintf(stderr, "  %dx%d %s: %u blocks\n", item.width, item.height, item.psm == GsPsm::T8 ? "PSMT8" : "PSMT4", texture.blocks);
            passed = false;
        }
    }

    return Check(passed, "footprints are exactly the levels' and the CLUT's blocks");
}

} // namespace

int main()
{
    bool passed = TimeSplittersSwizzle();
    passed = BlackLayout() && passed;
    passed = Rectangles() && passed;
    passed = RoundTrip(GsPsm::T8, 256, 128, 6, "PSMT8 256x128 with mips round trips") && passed;
    passed = RoundTrip(GsPsm::T8, 512, 512, 7, "PSMT8 512x512 with mips round trips") && passed;
    passed = RoundTrip(GsPsm::T4, 64, 64, 4, "PSMT4 64x64 with mips round trips") && passed;
    passed = RoundTrip(GsPsm::T4, 32, 256, 6, "PSMT4 32x256 with mips round trips") && passed;
    passed = RoundTrip(GsPsm::T8, 8, 8, 1, "PSMT8 8x8 round trips") && passed;
    passed = RoundTrip(GsPsm::Ct32, 64, 32, 3, "PSMCT32 64x32 with mips round trips") && passed;
    passed = RoundTrip(GsPsm::Ct16, 128, 16, 4, "PSMCT16 128x16 with mips round trips") && passed;
    passed = Refusals() && passed;
    passed = Footprints() && passed;

    if (!passed)
    {
        return 1;
    }

    std::puts("Texture format tests passed.");
    return 0;
}
