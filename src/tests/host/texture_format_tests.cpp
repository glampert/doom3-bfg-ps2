// ================================================================================================
// File: texture_format_tests.cpp
// Brief: Check the GS memory emulation against TimeSplitters' published PSMT8 swizzle and Black's
// mip layout, GSTX v2 and D3TX v3 round trips through the cook's reader and the target validator,
// their refusals of corrupted files, the material pass planner against the target's register
// evaluator and id's expression semantics, and the cavity bake.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/bytes.h"
#include "assets/gs_memory.h"
#include "assets/gstx_v2.h"
#include "assets/material.h"
#include "assets/material_passes.h"
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
    // The alpha test's value, the diffuse stage's index and the bump stage, in either spelling.
    const MaterialInfo & staged = materials["textures/a/staged"];
    passed = passed && staged.diffuseStage == 1 && staged.bump == "textures/a/staged_local" &&
             EvaluateExpression(staged.stages[1].alphaTestValue, ExpressionInputs{}) == 0.5f && materials["textures/a/plain"].bump == "textures/a/plain_local.tga" &&
             materials["textures/a/plain"].diffuseStage == 1 && !staged.stages[2].special;
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
    pack.materialAlphaRef = { 0x40, 0, 0, 0 };
    pack.materialFirstPass = { 0, 1, 0, 1 };
    pack.materialPassCount = { 0, 0, 1, 0 };
    pack.textureFlags = { 0, kD3txFlagAlphaTest, kD3txFlagLightmap };
    pack.textures = { firstFile, secondFile, pageFile };
    pack.lightmapPages = { 2, 2 };
    // One pass: time times a table, read through a clamped table.
    D3txPass glow;
    glow.texture = 0;
    glow.color = { 0x80, 0x40, 0x20, 0x80 };
    glow.nodeCount = 4;
    glow.colorNodes[1] = 3;
    glow.condition = 0;
    pack.passes = { glow };
    pack.nodes = { D3txNode{ 0, 0, 0, 0, 0, 1.0f }, D3txNode{ 1, 0, 0, 0, 0, 0.0f }, D3txNode{ 8, 0, 0, 1, 0, 0.0f },
                   D3txNode{ 5, kD3txTableClamp, 2, 0, 3, 0.0f } };
    pack.tableValues = { 0.0f, 1.0f, 0.0f };
    std::vector<std::uint8_t> file;
    TexturePack read;
    passed = passed && WriteD3tx(pack, file, error) && ReadD3tx(file, read, error) && read.materialTextures == pack.materialTextures &&
             read.textures == pack.textures && read.textureFlags == pack.textureFlags && read.materialsHash == pack.materialsHash &&
             read.materialUvDensity == pack.materialUvDensity && read.lightmapPages == pack.lightmapPages &&
             read.materialAlphaRef == pack.materialAlphaRef && read.materialFirstPass == pack.materialFirstPass &&
             read.materialPassCount == pack.materialPassCount && read.passes == pack.passes && read.nodes == pack.nodes &&
             read.tableValues == pack.tableValues;
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
             images[0].width == static_cast<std::uint32_t>(first.image.width) && images[1].height == static_cast<std::uint32_t>(second.image.height) &&
             target.passCount == 1 && target.nodeCount == 4 && target.valueCount == 3 && target.materials[0].alphaRef == 0x40 &&
             target.materials[2].passCount == 1 && target.passes[0].color[2] == 0x20 && target.passes[0].colorNodes[1] == 3;
    passed = passed && ps2::renderer::tex::ValidatePack(s_aligned, static_cast<std::uint32_t>(file.size()), target, images, 2) != nullptr;

    // The files may follow the tables in any order (the cook's streaming order); a streaming target
    // reads the tables alone, then each texture from where its record says.
    TexturePack shuffled = pack;
    shuffled.fileOrder = { 2, 0, 1 };
    std::vector<std::uint8_t> shuffledFile;
    passed = passed && WriteD3tx(shuffled, shuffledFile, error) && ReadD3tx(shuffledFile, read, error) && read.fileOrder == shuffled.fileOrder &&
             read.textures == pack.textures && shuffledFile.size() == file.size() && shuffledFile != file;
    std::memcpy(s_aligned, shuffledFile.data(), shuffledFile.size());
    const auto shuffledBytes = static_cast<std::uint32_t>(shuffledFile.size());
    const std::uint32_t tables = ps2::renderer::tex::PackTablesBytes(s_aligned, shuffledBytes);
    passed = passed && tables > 0 && tables < shuffledBytes &&
             ps2::renderer::tex::ValidatePackTables(s_aligned, tables, shuffledBytes, target) == nullptr &&
             target.textures[2].offset < target.textures[0].offset && target.textures[0].offset < target.textures[1].offset;

    for (std::uint32_t index = 0; index < 3; ++index)
    {
        passed = passed && ps2::renderer::tex::ValidatePackTexture(target, index, s_aligned + target.textures[index].offset, images[index]) == nullptr;
    }

    // A texture read from the wrong place, the tables short of their end or against another file size,
    // and a file order that is not a permutation are refused.
    passed = passed && ps2::renderer::tex::ValidatePackTexture(target, 2, s_aligned + target.textures[0].offset, images[2]) != nullptr &&
             ps2::renderer::tex::ValidatePackTables(s_aligned, tables - 4U, shuffledBytes, target) != nullptr &&
             ps2::renderer::tex::ValidatePackTables(s_aligned, tables, shuffledBytes + 64U, target) != nullptr &&
             ps2::renderer::tex::PackTablesBytes(s_aligned, shuffledBytes + 64U) == 0;
    shuffled.fileOrder = { 0, 0, 1 };
    passed = passed && !WriteD3tx(shuffled, shuffledFile, error);
    shuffled.fileOrder = { 0, 1 };
    passed = passed && !WriteD3tx(shuffled, shuffledFile, error);

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

    // Passes: an operand that is not earlier, a node outside its pass, a table past the values, a
    // folded input, a pass over an untextured material, a lightmap texture and an alpha reference
    // above 1.0 are all refused.
    TexturePack forward = pack;
    forward.nodes[2].b = 3;
    TexturePack outside = pack;
    outside.passes[0].colorNodes[0] = 4;
    TexturePack pastValues = pack;
    pastValues.nodes[3].count = 4;
    TexturePack parm = pack;
    parm.nodes[1].op = static_cast<std::uint8_t>(ExpressionOp::Parm);
    TexturePack untexturedPass = pack;
    untexturedPass.materialPassCount = { 0, 1, 0, 0 };
    TexturePack lightmapPass = pack;
    lightmapPass.passes[0].texture = 2;
    TexturePack bright = pack;
    bright.materialAlphaRef[0] = 0x81;
    passed = passed && !WriteD3tx(forward, file, error) && !WriteD3tx(outside, file, error) && !WriteD3tx(pastValues, file, error) &&
             !WriteD3tx(parm, file, error) && !WriteD3tx(untexturedPass, file, error) && !WriteD3tx(lightmapPass, file, error) &&
             !WriteD3tx(bright, file, error);

    // The target refuses the same passes when the cook's checks are bypassed: patch a valid file.
    if (WriteD3tx(pack, file, error))
    {
        const std::size_t nodesAt = kD3txHeaderBytes + 4 * kD3txMaterialRecordBytes + kD3txPassRecordBytes + 3 * kD3txTextureRecordBytes +
                                    2 * kD3txPageRecordBytes;
        std::vector<std::uint8_t> bad = file;
        bad[nodesAt + 2 * kD3txNodeRecordBytes + 4] = 3; // node 2's b: itself
        WriteLe32(bad.data() + 24, 0);
        const std::size_t tablesEnd = nodesAt + 4 * kD3txNodeRecordBytes + 3 * kD3txValueRecordBytes;
        WriteLe32(bad.data() + 24, Fnv1a32({ bad.data(), tablesEnd }));
        std::memcpy(s_aligned, bad.data(), bad.size());
        passed = passed && ps2::renderer::tex::ValidatePack(s_aligned, static_cast<std::uint32_t>(bad.size()), target, images, 3) != nullptr;
    }

    // A translucent material (no base pass) draws passes of every blend over the frame, a vertex-coloured
    // add among them; an opaque material's passes only add, and only an add takes vertex colours.
    D3txPass multiply;
    multiply.texture = 0;
    multiply.blend = kD3txBlendMultiply;
    D3txPass litPass;
    litPass.texture = 1;
    litPass.flags = kD3txPassVertexColor;
    D3txPass mix;
    mix.texture = 0;
    mix.blend = kD3txBlendAlpha;
    TexturePack translucent = pack;
    translucent.passes = { glow, multiply, litPass, mix };
    translucent.materialFirstPass[3] = 1;
    translucent.materialPassCount[3] = 3;
    passed = passed && WriteD3tx(translucent, file, error) && ReadD3tx(file, read, error) && read.passes == translucent.passes;
    std::memcpy(s_aligned, file.data(), file.size());
    passed = passed && ps2::renderer::tex::ValidatePack(s_aligned, static_cast<std::uint32_t>(file.size()), target, images, 3) == nullptr &&
             target.materials[3].passCount == 3 && target.passes[1].blend == ps2::renderer::tex::kPackBlendMultiply &&
             target.passes[2].flags == ps2::renderer::tex::kPackPassVertexColor && target.passes[3].blend == ps2::renderer::tex::kPackBlendAlpha;
    TexturePack opaqueMultiply = translucent;
    opaqueMultiply.materialFirstPass[2] = 1;
    TexturePack litMultiply = translucent;
    litMultiply.passes[1].flags = kD3txPassVertexColor;
    TexturePack unknownBlend = translucent;
    unknownBlend.passes[3].blend = 4;
    TexturePack unknownFlag = translucent;
    unknownFlag.passes[2].flags = 2;
    passed = passed && !WriteD3tx(opaqueMultiply, file, error) && !WriteD3tx(litMultiply, file, error) && !WriteD3tx(unknownBlend, file, error) &&
             !WriteD3tx(unknownFlag, file, error);

    // The target refuses them too when the cook's checks are bypassed: patch a valid file.
    if (WriteD3tx(translucent, file, error))
    {
        const std::size_t passesAt = kD3txHeaderBytes + 4 * kD3txMaterialRecordBytes;
        const std::size_t tablesEnd = passesAt + 4 * kD3txPassRecordBytes + 3 * kD3txTextureRecordBytes + 2 * kD3txPageRecordBytes +
                                      4 * kD3txNodeRecordBytes + 3 * kD3txValueRecordBytes;
        const auto Patched = [&](std::size_t offset, std::uint8_t value)
        {
            std::vector<std::uint8_t> bad = file;
            bad[offset] = value;
            WriteLe32(bad.data() + 24, 0);
            WriteLe32(bad.data() + 24, Fnv1a32({ bad.data(), tablesEnd }));
            std::memcpy(s_aligned, bad.data(), bad.size());
            return ps2::renderer::tex::ValidatePack(s_aligned, static_cast<std::uint32_t>(bad.size()), target, images, 3) != nullptr;
        };

        // Material 2 (textured) over the multiply pass; the multiply with vertex colours; blend 4.
        passed = passed && Patched(kD3txHeaderBytes + 2 * kD3txMaterialRecordBytes + 8, 1) &&
                 Patched(passesAt + kD3txPassRecordBytes + 9, kD3txPassVertexColor) && Patched(passesAt + 3 * kD3txPassRecordBytes + 8, 4);
    }

    // A sky: a fifth material draws it, its faces name the two diffuse textures.
    TexturePack sky = pack;
    sky.materialsHash = MaterialNamesHash({ "textures/a", "textures/b", "textures/c", "textures/d", "textures/skies/e" });
    sky.materialTextures.push_back(kD3txSky);
    sky.materialUvDensity.push_back(0.0f);
    sky.materialAlphaRef.push_back(0);
    sky.materialFirstPass.push_back(1);
    sky.materialPassCount.push_back(0);
    D3txSky box;
    box.faces = { 0, 1, 0, 1, 1, 0 };
    box.color = { 0x60, 0x60, 0x60, 0x80 };
    sky.skies = { box };
    passed = passed && WriteD3tx(sky, file, error) && ReadD3tx(file, read, error) && read.skies == sky.skies &&
             read.materialTextures == sky.materialTextures;
    std::memcpy(s_aligned, file.data(), file.size());
    passed = passed && ps2::renderer::tex::ValidatePack(s_aligned, static_cast<std::uint32_t>(file.size()), target, images, 3) == nullptr &&
             target.skyCount == 1 && target.sky != nullptr && target.sky->faces[1] == 1 && target.sky->faces[5] == 0 && target.sky->color[0] == 0x60 &&
             target.materials[4].texture == ps2::renderer::tex::kPackSky;

    // No sky for a sky material, a face on a lightmap or a missing texture, two skies, and a sky that
    // alpha-tests or draws passes are refused.
    TexturePack noSky = sky;
    noSky.skies.clear();
    TexturePack faceOnPage = sky;
    faceOnPage.skies[0].faces[3] = 2;
    TexturePack faceMissing = sky;
    faceMissing.skies[0].faces[0] = 3;
    TexturePack twoSkies = sky;
    twoSkies.skies.push_back(box);
    TexturePack testedSky = sky;
    testedSky.materialAlphaRef[4] = 0x40;
    TexturePack skyPass = sky;
    skyPass.materialFirstPass[4] = 0;
    skyPass.materialPassCount[4] = 1;
    passed = passed && !WriteD3tx(noSky, file, error) && !WriteD3tx(faceOnPage, file, error) && !WriteD3tx(faceMissing, file, error) &&
             !WriteD3tx(twoSkies, file, error) && !WriteD3tx(testedSky, file, error) && !WriteD3tx(skyPass, file, error);

    // The target refuses a face on a lightmap when the cook's checks are bypassed.
    if (WriteD3tx(sky, file, error))
    {
        const std::size_t skyAt = kD3txHeaderBytes + 5 * kD3txMaterialRecordBytes + kD3txPassRecordBytes + 3 * kD3txTextureRecordBytes +
                                  2 * kD3txPageRecordBytes + 4 * kD3txNodeRecordBytes + 3 * kD3txValueRecordBytes;
        std::vector<std::uint8_t> bad = file;
        WriteLe32(bad.data() + skyAt + 12, 2); // face 3: the lightmap page
        WriteLe32(bad.data() + 24, 0);
        WriteLe32(bad.data() + 24, Fnv1a32({ bad.data(), skyAt + kD3txSkyRecordBytes }));
        std::memcpy(s_aligned, bad.data(), bad.size());
        passed = passed && ps2::renderer::tex::ValidatePack(s_aligned, static_cast<std::uint32_t>(bad.size()), target, images, 3) != nullptr;
    }

    return Check(passed, "D3TX packs round trip and refuse corruption");
}

static bool MaterialPasses()
{
    static constexpr const char * kText = R"(
table pulse { { 0, 1, 0.5 } }
table steps { snap { 0.25, 1, 0.5, 2 } }
textures/p/glow
{
    bumpmap textures/p/glow_local
    { blend diffusemap map textures/p/glow_d alphaTest 0.25 rgb 0.5 }
    { blend add map textures/p/pulse_add rgb pulse[ time * 0.5 ] }
    { if ( parm7 == 0 ) blend gl_one, gl_one map textures/p/on_add red parm0 green parm1 * 2 blue 0.5 }
    { if ( parm7 != 0 ) blend add map textures/p/off_add }
    { blend add map textures/p/env cubeMap env/x }
    { blend add map textures/p/dark rgb 0 }
    { if ( time > 2 ) blend add map textures/p/late red steps[ time * -0.3 ] % 3 green ( time / 0 ) + 0.5 blue parm0 }
}
textures/p/glass { translucent { blend add map textures/p/glass_add } }
textures/p/decal
{
    translucent
    { blend diffusemap map textures/p/decal_d rgb 0.5 }
    { blend filter map textures/p/stain rgb 0.5 }
    { blend gl_zero, gl_one_minus_src_color map textures/p/splat }
    { blend blend map textures/p/snacks alpha 0.5 }
    { blend add cubeMap env/x texgen reflect }
    { blend gl_dst_alpha, gl_one map textures/p/x }
    { maskcolor map makealpha(textures/p/y) }
}
textures/p/flare { translucent deform sprite { blend add map textures/p/flare } }
textures/p/mirror { translucent mirror { blend filter map textures/p/tint } }
textures/p/tinted { translucent { blend gl_zero, gl_one_minus_src_color map textures/p/splat red 0.5 } }
textures/skies/s { forceOpaque { blend add cameraCubeMap env/cloudy // desert
    texgen skybox red .75 green .75 blue .75 } }
textures/p/chrome { diffusemap textures/p/chrome_d { blend add cubeMap env/x texgen reflect } }
)";

    std::map<std::string, MaterialInfo> materials;
    MaterialTables tables;
    std::string error;
    bool passed = ScanMaterials(kText, materials, error, &tables);
    const MaterialInfo & glow = materials["textures/p/glow"];

    // The world's parms: parm0-3 one, parm7 zero.
    const MaterialPassPlan world = PlanMaterialPasses(glow, WorldShaderParms(), tables);
    passed = passed && world.alphaRef == 32 && world.diffuseColor[0] == 0.5f && world.diffuseColor[2] == 0.5f && world.passes.size() == 3 &&
             world.skipped.size() == 1;
    passed = passed && world.passes.size() == 3 && world.passes[0].image == "textures/p/pulse_add" && !world.passes[0].nodes.empty() &&
             world.passes[0].pass.colorNodes[3] == kD3txNoNode && world.passes[1].image == "textures/p/on_add" && world.passes[1].nodes.empty() &&
             world.passes[1].pass.color == std::array<std::uint8_t, 4>{ 0x80, 0xFF, 0x40, 0x80 } && world.passes[1].pass.condition == kD3txNoNode &&
             world.passes[2].image == "textures/p/late" && world.passes[2].pass.condition != kD3txNoNode;

    // An entity's: parm7 set swaps the stages, parm0 folds into a constant.
    ShaderParms entity = WorldShaderParms();
    entity[0] = 0.25f;
    entity[7] = 1.0f;
    const MaterialPassPlan lit = PlanMaterialPasses(glow, entity, tables);
    passed = passed && lit.passes.size() == 3 && lit.passes[1].image == "textures/p/off_add" && lit.passes[2].pass.colorNodes[2] == kD3txNoNode &&
             lit.passes[2].pass.color[2] == 0x20;

    // A translucent material's stages draw over the frame: an add; its diffuse lit by the vertex
    // colours (its colour folded there); a multiply by the image's luminance (its colour's in the
    // vertex alpha), or one minus it; an alpha blend with the colour's alpha. Cube maps, blends the
    // GS cannot draw and replacing stages draw nothing, nor does a sprite, a mirror, or an inverse
    // multiply with a colour.
    const MaterialPassPlan glass = PlanMaterialPasses(materials["textures/p/glass"], WorldShaderParms(), tables);
    passed = passed && glass.passes.size() == 1 && glass.passes[0].pass.blend == kD3txBlendAdd && glass.passes[0].pass.flags == 0 &&
             glass.passes[0].source == PassImage::Opaque;
    const MaterialPassPlan decal = PlanMaterialPasses(materials["textures/p/decal"], WorldShaderParms(), tables);
    passed = passed && decal.diffuseColor[0] == 0.5f && decal.passes.size() == 4 && decal.skipped.size() == 3;
    passed = passed && decal.passes.size() == 4 && decal.passes[0].source == PassImage::Diffuse && decal.passes[0].pass.flags == kD3txPassVertexColor &&
             decal.passes[0].pass.blend == kD3txBlendAdd && decal.passes[0].pass.color == std::array<std::uint8_t, 4>{ 0x80, 0x80, 0x80, 0x80 } &&
             decal.passes[1].source == PassImage::Luminance && decal.passes[1].pass.blend == kD3txBlendMultiply && decal.passes[1].pass.color[3] == 0x40 &&
             decal.passes[2].source == PassImage::InverseLuminance && decal.passes[2].pass.blend == kD3txBlendMultiply &&
             decal.passes[3].source == PassImage::Alpha && decal.passes[3].pass.blend == kD3txBlendAlpha && decal.passes[3].pass.color[3] == 0x40;
    const MaterialPassPlan flare = PlanMaterialPasses(materials["textures/p/flare"], WorldShaderParms(), tables);
    const MaterialPassPlan mirror = PlanMaterialPasses(materials["textures/p/mirror"], WorldShaderParms(), tables);
    const MaterialPassPlan tinted = PlanMaterialPasses(materials["textures/p/tinted"], WorldShaderParms(), tables);
    passed = passed && flare.passes.empty() && flare.skipped.size() == 1 && mirror.passes.empty() && mirror.skipped.size() == 1 &&
             tinted.passes.empty() && tinted.skipped.size() == 1;

    // A sky stage: a camera cube map read along the view ray; a reflection is not one.
    const MaterialInfo & skyMaterial = materials["textures/skies/s"];
    const MaterialInfo & chrome = materials["textures/p/chrome"];
    passed = passed && skyMaterial.skyStage == 0 && skyMaterial.diffuse.empty() && skyMaterial.stages[0].cubeMap == "env/cloudy" &&
             skyMaterial.stages[0].cameraCube && skyMaterial.stages[0].texgen == "skybox" && chrome.skyStage < 0 &&
             chrome.stages[1].cubeMap == "env/x" && !chrome.stages[1].cameraCube && chrome.stages[1].texgen == "reflect" &&
             GeneratedImageName("env/cloudy", kTextureUsageDefault, kCubeFilesCamera) == "generated/images/env/cloudy#__0202.bimage" &&
             GeneratedImageName("env/x", kTextureUsageDefault, kCubeFilesNative) == "generated/images/env/x#__0201.bimage";

    // Packed, the target evaluates every pass as the cook's planner and id's own expressions do.
    TexturePolicy policy;
    CookedTexture texture;
    std::vector<std::uint8_t> textureFile;
    passed = passed && CookTexture(Noise(16, 16, true), policy, GstxUsage::Diffuse, texture, error) && WriteGstxV2(texture.image, textureFile, error);
    TexturePack pack;
    pack.materialsHash = MaterialNamesHash({ "textures/p/glow", "textures/p/glow" });
    pack.materialTextures = { 0, 0 };
    pack.materialUvDensity = { 0.0f, 0.0f };
    pack.materialAlphaRef = { world.alphaRef, lit.alphaRef };
    pack.textureFlags = { 0 };
    pack.textures = { textureFile };
    std::map<std::string, std::uint16_t> offsets;

    for (const MaterialPassPlan * plan : { &world, &lit })
    {
        pack.materialFirstPass.push_back(static_cast<std::uint16_t>(pack.passes.size()));
        pack.materialPassCount.push_back(static_cast<std::uint8_t>(plan->passes.size()));

        for (const PlannedPass & planned : plan->passes)
        {
            AddPlannedPass(pack, planned, 0, tables, offsets);
        }
    }

    std::vector<std::uint8_t> file;
    ps2::renderer::tex::Pack target{};
    ps2::renderer::tex::Image images[1]{};
    passed = passed && WriteD3tx(pack, file, error);
    std::memcpy(s_aligned, file.data(), file.size());
    passed = passed && ps2::renderer::tex::ValidatePack(s_aligned, static_cast<std::uint32_t>(file.size()), target, images, 1) == nullptr &&
             target.passCount == 6 && pack.tableValues.size() == 9;

    for (std::size_t plan = 0; passed && plan < 2; ++plan)
    {
        const MaterialPassPlan & source = plan == 0 ? world : lit;
        ExpressionInputs inputs;
        const ShaderParms parms = plan == 0 ? WorldShaderParms() : entity;
        std::copy(parms.begin(), parms.end(), inputs.parms.begin());
        inputs.tables = &tables;

        for (std::size_t index = 0; index < source.passes.size(); ++index)
        {
            const PlannedPass & planned = source.passes[index];
            const ps2::renderer::tex::PackPass & record = target.passes[target.materials[plan].firstPass + index];
            const MaterialStage & stage = glow.stages[static_cast<std::size_t>(planned.stage)];

            for (const float time : { 0.0f, 0.3f, 1.7f, 2.5f, 5.0f, -3.2f, 123.4f })
            {
                float registers[ps2::renderer::tex::kPackMaxNodesPerPass] = {};
                ps2::renderer::tex::EvaluatePassNodes(target, record, time, registers);
                const std::vector<float> planner = EvaluatePlannedNodes(planned, tables, time);
                inputs.time = time;

                for (std::size_t node = 0; node < planner.size(); ++node)
                {
                    passed = passed && planner[node] == registers[node];
                }

                for (std::size_t channel = 0; channel < 4; ++channel)
                {
                    if (record.colorNodes[channel] != ps2::renderer::tex::kPackNoNode)
                    {
                        passed = passed && registers[record.colorNodes[channel] - record.firstNode] == EvaluateExpression(stage.color[channel], inputs);
                    }
                }

                if (record.condition != ps2::renderer::tex::kPackNoNode)
                {
                    passed = passed && registers[record.condition - record.firstNode] == EvaluateExpression(stage.condition, inputs);
                }
            }
        }
    }

    return Check(passed, "material passes fold parms, keep time, and evaluate on the target as id's registers do");
}

static bool Cavity()
{
    RgbaImage diffuse;
    diffuse.Allocate(8, 8);
    RgbaImage normal;
    normal.Allocate(4, 4);

    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            std::uint8_t * texel = diffuse.At(x, y);
            texel[0] = texel[1] = texel[2] = 100;
            texel[3] = 255;
        }
    }

    // The left half faces the key light (+x), the right half away from it.
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            std::uint8_t * texel = normal.At(x, y);
            const bool toward = x < 2;
            texel[0] = toward ? 218 : 37;
            texel[1] = 128;
            texel[2] = 228;
            texel[3] = 255;
        }
    }

    RgbaImage untouched = diffuse;
    CavityPolicy off;
    BakeCavity(untouched, normal, off);
    bool passed = untouched.pixels == diffuse.pixels;

    CavityPolicy policy;
    policy.amplitude = 1.0f;
    policy.light = { 1.0f, 0.0f, 1.0f };
    RgbaImage baked = diffuse;
    BakeCavity(baked, normal, policy);
    passed = passed && baked.At(1, 3)[0] > 100 && baked.At(6, 3)[0] < 100 && baked.At(1, 3)[3] == 255 && baked.At(1, 3)[0] <= 150 &&
             baked.At(6, 3)[0] >= 50;

    // Half the amplitude moves half as far.
    policy.amplitude = 0.5f;
    RgbaImage half = diffuse;
    BakeCavity(half, normal, policy);
    passed = passed && std::abs((half.At(1, 3)[0] - 100) * 2 - (baked.At(1, 3)[0] - 100)) <= 2;
    return Check(passed, "the cavity bake brightens texels facing the key light and darkens the rest, by its amplitude");
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
    passed = MaterialPasses() && passed;
    passed = Cavity() && passed;

    if (!passed)
    {
        return 1;
    }

    std::puts("Texture format tests passed.");
    return 0;
}
