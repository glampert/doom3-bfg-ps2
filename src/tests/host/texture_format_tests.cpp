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
#include "assets/material.h"
#include "assets/texture_pack.h"
#include "ps2/renderer/texture_format.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
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

static bool Materials()
{
    static constexpr const char * kText = R"(
// a comment { not a brace
table sintable { { 0, 1 } }
textures/a/plain
{
    qer_editorimage textures/a/plain.tga
    bumpmap         textures/a/plain_local.tga
    diffusemap      textures/a/plain_d.tga
}
textures/a/staged
{
    /* stages */
    { blend bumpmap  map textures/a/staged_local }
    { blend diffusemap map textures/a/staged_d alphaTest 0.5 }
    { blend add map "textures/a/glow" rgb 0.5 }
}
material textures/a/program
{
    { blend gl_one, gl_one map textures/a/glow }
    diffusemap addnormals( textures/a/x_local, heightmap( textures/a/x_bmp, 4 ) )
}
textures/a/plain { diffusemap textures/a/second_definition }
)";

    std::map<std::string, MaterialInfo> materials;
    std::string error;
    bool passed = ScanMaterials(kText, materials, error) && materials.size() == 3;
    passed = passed && materials["textures/a/plain"].diffuse == "textures/a/plain_d.tga" && !materials["textures/a/plain"].alphaTest &&
             materials["textures/a/plain"].otherStages == 0;
    passed = passed && materials["textures/a/staged"].diffuse == "textures/a/staged_d" && materials["textures/a/staged"].alphaTest &&
             materials["textures/a/staged"].otherStages == 1;
    passed = passed && materials["textures/a/program"].diffuse == "addnormals(textures/a/x_local,heightmap(textures/a/x_bmp,4))" &&
             materials["textures/a/program"].otherStages == 1;

    // The names retail ships (texture_census.py lists both).
    passed = passed && GeneratedImageName("Textures\\Base_Door\\deldoorgutter_d.tga", kTextureUsageDiffuse) ==
                           "generated/images/textures/base_door/deldoorgutter_d#__0100.bimage";
    passed = passed && GeneratedImageName("addnormals( textures/decals/pipecap2a_local, heightmap( textures/decals/pipecap2_bmp, 1 ) )", 3) ==
                           "generated/images/addnormals/textures/decals/pipecap2a_local/heightmap/textures/decals/pipecap2_bmp/1#__0300.bimage";
    passed = passed && GeneratedImageName("textures/a/b_d", kTextureUsageCoverage) == "generated/images/textures/a/b_d#__1000.bimage";

    std::map<std::string, MaterialInfo> refused;
    passed = passed && !ScanMaterials("textures/a { { blend diffusemap ", refused, error) && !ScanMaterials("/* open", refused, error) &&
             !ScanMaterials("lonely_name", refused, error);
    return Check(passed, "material scan finds diffuse stages and id's generated image names");
}

static RgbaImage Noise(int width, int height, bool twoColors)
{
    RgbaImage image;
    image.Allocate(width, height);
    std::mt19937 random(9);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            std::uint8_t * texel = image.At(x, y);
            const bool first = twoColors ? ((x / 8 + y / 8) & 1) != 0 : false;

            for (int channel = 0; channel < 3; ++channel)
            {
                texel[channel] = twoColors ? static_cast<std::uint8_t>(first ? 200 : 40) : static_cast<std::uint8_t>(random() & 0xFF);
            }

            texel[3] = 255;
        }
    }

    return image;
}

static bool TextureCook()
{
    TexturePolicy policy;
    CookedTexture flat;
    CookedTexture noisy;
    CookedTexture small;
    std::string error;
    std::vector<std::uint8_t> file;
    GstxV2Texture texture;

    // 512x256 halves to 256x128 with mips to 8 texels; two colours fit PSMT4 exactly.
    bool passed = CookTexture(Noise(512, 256, true), policy, GstxUsage::Diffuse, flat, error) && flat.image.width == 256 &&
                  flat.image.height == 128 && flat.image.levels.size() == 5 && flat.image.psm == GsPsm::T4 && flat.quality.rmse < 0.5 &&
                  WriteGstxV2(flat.image, file, error) && ReadGstxV2(file, texture, error);
    std::uint32_t texel = 0;
    passed = passed && SampleGstxV2(texture, 0, 0, 0, texel) && (texel & 0xFFU) >= 38U && (texel & 0xFFU) <= 42U && (texel >> 24) == 0x80U;
    // Noise needs PSMT8; 100x60 keeps 64x32 (the 64-texel floor), three levels.
    passed = passed && CookTexture(Noise(100, 60, false), policy, GstxUsage::Diffuse, noisy, error) && noisy.image.psm == GsPsm::T8 &&
             noisy.image.width == 64 && noisy.image.height == 32 && noisy.image.levels.size() == 3;
    // Small sources keep their size.
    passed = passed && CookTexture(Noise(16, 16, true), policy, GstxUsage::Diffuse, small, error) && small.image.width == 16 &&
             small.image.levels.size() == 2;
    return Check(passed, "textures cook to power-of-two GSTX v2 levels in PSMT4 or PSMT8");
}

static bool TexturePacks()
{
    TexturePolicy policy;
    CookedTexture first;
    CookedTexture second;
    std::vector<std::uint8_t> firstFile;
    std::vector<std::uint8_t> secondFile;
    std::string error;
    bool passed = CookTexture(Noise(64, 64, true), policy, GstxUsage::Diffuse, first, error) && WriteGstxV2(first.image, firstFile, error) &&
                  CookTexture(Noise(32, 128, false), policy, GstxUsage::Diffuse, second, error) && WriteGstxV2(second.image, secondFile, error);

    // A Light8 page: an identity ramp whose alpha reaches 2x (above the diffuse limit of 0x80).
    std::vector<std::uint8_t> texels(64U * 64U);

    for (std::size_t index = 0; index < texels.size(); ++index)
    {
        texels[index] = static_cast<std::uint8_t>(index * 7U);
    }

    GstxV2Image page;
    std::vector<std::uint8_t> pageFile;
    GstxV2Texture pageTexture;
    passed = passed && CookLightmapPage(64, texels, page, error) && page.usage == GstxUsage::Lightmap && page.palette[255] >> 24 == 255U &&
             page.wrapS == GstxWrap::Clamp && WriteGstxV2(page, pageFile, error) && ReadGstxV2(pageFile, pageTexture, error) &&
             !CookLightmapPage(48, std::vector<std::uint8_t>(48U * 48U), page, error);
    GstxV2Image diffuse = page;
    diffuse.usage = GstxUsage::Diffuse;
    std::vector<std::uint8_t> refused;
    passed = passed && !WriteGstxV2(diffuse, refused, error);

    TexturePack pack;
    pack.materialsHash = MaterialNamesHash({ "textures/a", "textures/b", "textures/c", "textures/d" });
    pack.materialTextures = { 1, kD3txNoTexture, 0, kD3txHidden };
    pack.materialUvDensity = { -7.0f, 0.0f, -6.5f, 0.0f };
    pack.textureFlags = { 0, kD3txFlagAlphaTest, kD3txFlagLightmap };
    pack.textures = { firstFile, secondFile, pageFile };
    pack.lightmapPages = { 2, 2 };
    std::vector<std::uint8_t> file;
    TexturePack read;
    passed = passed && WriteD3tx(pack, file, error) && ReadD3tx(file, read, error) && read.materialTextures == pack.materialTextures &&
             read.textures == pack.textures && read.textureFlags == pack.textureFlags && read.materialsHash == pack.materialsHash &&
             read.materialUvDensity == pack.materialUvDensity && read.lightmapPages == pack.lightmapPages;
    passed = passed && MaterialNamesHash({ "ab" }) != MaterialNamesHash({ "a", "b" });

    // The target admits the same file and sees the same tables.
    ps2::renderer::tex::Pack target{};
    ps2::renderer::tex::Image images[3]{};
    std::memcpy(s_aligned, file.data(), file.size());
    passed = passed && ps2::renderer::tex::ValidatePack(s_aligned, static_cast<std::uint32_t>(file.size()), target, images, 3) == nullptr &&
             target.materialCount == 4 && target.textureCount == 3 && target.pageCount == 2 && target.pages[1] == 2 &&
             target.materialsHash == pack.materialsHash && target.materials[0].texture == 1 &&
             target.materials[1].texture == ps2::renderer::tex::kPackNoTexture && target.materials[3].texture == ps2::renderer::tex::kPackHidden &&
             target.materials[2].uvDensityLog2 == -6.5f && target.textures[1].flags == ps2::renderer::tex::kPackFlagAlphaTest &&
             target.textures[2].flags == ps2::renderer::tex::kPackFlagLightmap && images[2].usage == ps2::renderer::tex::ImageUsage::Lightmap &&
             images[0].width == static_cast<std::uint32_t>(first.image.width) && images[1].height == static_cast<std::uint32_t>(second.image.height);
    passed = passed && ps2::renderer::tex::ValidatePack(s_aligned, static_cast<std::uint32_t>(file.size()), target, images, 2) != nullptr;

    // Flipped bytes anywhere in the tables or a texture, and a dangling material, are refused by both.
    for (const std::size_t offset : { std::size_t(8), std::size_t(16), std::size_t(33), std::size_t(62), file.size() - 100 })
    {
        std::vector<std::uint8_t> bad = file;
        bad[offset] ^= 0x10U;
        std::memcpy(s_aligned, bad.data(), bad.size());
        passed = passed && !ReadD3tx(bad, read, error) &&
                 ps2::renderer::tex::ValidatePack(s_aligned, static_cast<std::uint32_t>(bad.size()), target, images, 3) != nullptr;
    }

    // A material may not name a page, a page only a lightmap, and the flag must match the usage.
    TexturePack dangling = pack;
    dangling.materialTextures[1] = 3;
    TexturePack namesPage = pack;
    namesPage.materialTextures[1] = 2;
    TexturePack pageNamesDiffuse = pack;
    pageNamesDiffuse.lightmapPages[0] = 0;
    TexturePack unflagged = pack;
    unflagged.textureFlags[2] = 0;
    TexturePack misflagged = pack;
    misflagged.textureFlags[0] = kD3txFlagLightmap;
    passed = passed && !WriteD3tx(dangling, file, error) && !WriteD3tx(namesPage, file, error) && !WriteD3tx(pageNamesDiffuse, file, error) &&
             !WriteD3tx(unflagged, file, error) && !WriteD3tx(misflagged, file, error);
    return Check(passed, "D3TX packs round trip and refuse corruption");
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
    passed = Materials() && passed;
    passed = TextureCook() && passed;
    passed = TexturePacks() && passed;

    if (!passed)
    {
        return 1;
    }

    std::puts("Texture format tests passed.");
    return 0;
}
