// ================================================================================================
// File: texture_format.cpp
// Brief: Validate a cooked GSTX v2 texture in place (its GS memory image, upload stream and register
// templates) and relocate the registers to an allocation, so a file the GS would misread or hang
// on is refused at load and an upload needs no work per texel.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/texture_format.h"

namespace ps2::renderer::tex
{
namespace
{

static constexpr std::uint32_t kPageBlocks = 32;

// GS manual 8.3: blocks within a page. PSMT8 shares PSMCT32's arrangement, PSMT4 PSMCT16's.
static constexpr std::uint8_t kBlocks32[4][8] = {
    { 0, 1, 4, 5, 16, 17, 20, 21 },
    { 2, 3, 6, 7, 18, 19, 22, 23 },
    { 8, 9, 12, 13, 24, 25, 28, 29 },
    { 10, 11, 14, 15, 26, 27, 30, 31 }
};

static constexpr std::uint8_t kBlocks16[8][4] = {
    { 0, 2, 8, 10 },
    { 1, 3, 9, 11 },
    { 4, 6, 12, 14 },
    { 5, 7, 13, 15 },
    { 16, 18, 24, 26 },
    { 17, 19, 25, 27 },
    { 20, 22, 28, 30 },
    { 21, 23, 29, 31 }
};

struct Rectangle
{
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t width;
    std::uint32_t height;
};

static std::uint32_t Word(const std::uint8_t * bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

static std::uint64_t Dword(const std::uint8_t * bytes)
{
    return static_cast<std::uint64_t>(Word(bytes)) | (static_cast<std::uint64_t>(Word(bytes + 4)) << 32);
}

static std::uint32_t Half(const std::uint8_t * bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8);
}

static std::uint32_t Fnv(const std::uint8_t * bytes, std::uint32_t length, std::uint32_t hash)
{
    for (std::uint32_t index = 0; index < length; ++index)
    {
        hash = (hash ^ bytes[index]) * 16777619U;
    }

    return hash;
}

static std::uint32_t Field(std::uint64_t value, int shift, int bits)
{
    return static_cast<std::uint32_t>((value >> shift) & ((1ULL << bits) - 1ULL));
}

static bool PowerOfTwo(std::uint32_t value)
{
    return value != 0 && (value & (value - 1U)) == 0;
}

static std::uint32_t Log2(std::uint32_t value)
{
    std::uint32_t log = 0;

    while ((1U << log) < value)
    {
        ++log;
    }

    return log;
}

static bool Indexed(std::uint8_t psm)
{
    return psm == kPsmT8 || psm == kPsmT4;
}

static std::uint32_t LevelExtent(std::uint32_t extent, std::uint32_t level)
{
    const std::uint32_t value = extent >> level;
    return value != 0 ? value : 1U;
}

// The cook's TBW for a level: the smallest covering it, even for PSMT8/PSMT4.
static std::uint32_t BufferWidth(std::uint8_t psm, std::uint32_t width)
{
    const std::uint32_t units = (width + 63U) / 64U;

    if (Indexed(psm))
    {
        const std::uint32_t even = (units + 1U) & ~1U;
        return even > 2U ? even : 2U;
    }

    return units > 1U ? units : 1U;
}

// The block of the level's last texel at block 0: the largest it occupies, since block numbers
// grow with x for a fixed y and with y for a fixed x.
static std::uint32_t LastBlock(std::uint8_t psm, std::uint32_t width, std::uint32_t height, std::uint32_t bw)
{
    const bool wide = psm == kPsmCt32 || psm == kPsmT8;
    const std::uint32_t pageWidth = psm == kPsmCt32 || psm == kPsmCt16 ? 64U : 128U;
    const std::uint32_t pageHeight = psm == kPsmCt32 ? 32U : psm == kPsmT4 ? 128U : 64U;
    const std::uint32_t blockWidth = psm == kPsmCt32 ? 8U : psm == kPsmT4 ? 32U : 16U;
    const std::uint32_t blockHeight = psm == kPsmCt32 || psm == kPsmCt16 ? 8U : 16U;
    const std::uint32_t x = width - 1U;
    const std::uint32_t y = height - 1U;
    const std::uint32_t page = (y / pageHeight) * (bw * 64U / pageWidth) + x / pageWidth;
    const std::uint32_t blockX = (x % pageWidth) / blockWidth;
    const std::uint32_t blockY = (y % pageHeight) / blockHeight;

    return page * kPageBlocks + (wide ? kBlocks32[blockY][blockX] : kBlocks16[blockY][blockX]);
}

// The rectangles of a 64-wide PSMCT32 buffer covering blocks [0, blocks), as the cook writes them.
static std::uint32_t Rectangles(std::uint32_t blocks, Rectangle (&output)[kImageMaxRectangles])
{
    static constexpr std::uint32_t kSizes[][3] = { { 16, 32, 32 }, { 8, 32, 16 }, { 4, 16, 16 }, { 2, 16, 8 }, { 1, 8, 8 } };
    std::uint32_t count = 0;
    std::uint32_t position = 0;

    while (blocks - position >= kPageBlocks)
    {
        std::uint32_t pages = (blocks - position) / kPageBlocks;
        pages = pages < 32U ? pages : 32U;
        output[count++] = { 0, position / kPageBlocks * 32U, 64, pages * 32U };
        position += pages * kPageBlocks;
    }

    for (const auto & size : kSizes)
    {
        if (((blocks - position) & size[0]) == 0)
        {
            continue;
        }

        const std::uint32_t block = position % kPageBlocks;
        const std::uint32_t x = ((block & 1U) | (((block >> 2) & 1U) << 1) | (((block >> 4) & 1U) << 2)) * 8U;
        const std::uint32_t y = position / kPageBlocks * 32U + (((block >> 1) & 1U) | (((block >> 3) & 1U) << 1)) * 8U;
        output[count++] = { x, y, size[1], size[2] };
        position += size[0];
    }

    return count;
}

// A+D TRXPOS/TRXREG/TRXDIR and the IMAGE tag, exactly as the cook writes them.
static bool TagsMatch(const std::uint8_t * at, const Rectangle & rectangle, bool last)
{
    const std::uint64_t expected[10] = {
        3ULL | (1ULL << 60), 0xEULL,
        (static_cast<std::uint64_t>(rectangle.x) << 32) | (static_cast<std::uint64_t>(rectangle.y) << 48), 0x51ULL,
        static_cast<std::uint64_t>(rectangle.width) | (static_cast<std::uint64_t>(rectangle.height) << 32), 0x52ULL,
        0ULL, 0x53ULL,
        static_cast<std::uint64_t>(rectangle.width * rectangle.height / 4U) | (last ? 1ULL << 15 : 0ULL) | (2ULL << 58), 0ULL
    };

    for (std::uint32_t index = 0; index < 10; ++index)
    {
        if (Dword(at + 8U * index) != expected[index])
        {
            return false;
        }
    }

    return true;
}

} // namespace

PS2_COLD_FUNC const char * ValidateImage(const std::uint8_t * file, std::uint32_t bytes, Image & output)
{
    if (file == nullptr || (reinterpret_cast<std::uintptr_t>(file) & 15U) != 0) [[unlikely]]
    {
        return "GSTX v2 file is not 16-byte aligned";
    }

    if (bytes < kImageHeaderBytes || bytes > kImageMaxFileBytes) [[unlikely]]
    {
        return "GSTX v2 file size out of range";
    }

    if (Word(file) != kImageMagic || Half(file + 4) != kImageVersion || Half(file + 6) != kImageHeaderBytes) [[unlikely]]
    {
        return "not a GSTX v2 file";
    }

    std::uint32_t headerHash = Fnv(file, 28, 2166136261U);
    headerHash = Fnv(reinterpret_cast<const std::uint8_t *>("\0\0\0\0"), 4, headerHash);
    headerHash = Fnv(file + 32, kImageHeaderBytes - 32U, headerHash);

    if (headerHash != Word(file + 28)) [[unlikely]]
    {
        return "GSTX v2 header checksum mismatch";
    }

    for (std::uint32_t index = 72; index < kImageHeaderBytes; ++index)
    {
        if (file[index] != 0) [[unlikely]]
        {
            return "GSTX v2 reserved bytes are not zero";
        }
    }

    Image image = {};
    image.width = Half(file + 8);
    image.height = Half(file + 10);
    image.psm = file[12];
    image.levels = file[13];
    const std::uint32_t usage = file[14];
    const std::uint32_t rectangleCount = file[15];
    image.blocks = Word(file + 16);
    image.streamQwords = Word(file + 20);
    image.tex0 = Dword(file + 32);
    image.tex1 = Dword(file + 40);
    image.miptbp1 = Dword(file + 48);
    image.miptbp2 = Dword(file + 56);
    image.clamp = Dword(file + 64);
    image.stream = file + kImageHeaderBytes;

    const std::uint32_t smaller = image.width < image.height ? image.width : image.height;

    if (!PowerOfTwo(image.width) || !PowerOfTwo(image.height) || image.width > kImageMaxExtent || image.height > kImageMaxExtent ||
        image.levels < 1 || image.levels > kImageMaxLevels || (smaller >> (image.levels - 1U)) == 0) [[unlikely]]
    {
        return "GSTX v2 size or level count out of range";
    }

    if ((image.psm != kPsmCt32 && image.psm != kPsmCt16 && !Indexed(image.psm)) || usage >= static_cast<std::uint32_t>(ImageUsage::Count)) [[unlikely]]
    {
        return "unknown GSTX v2 format or usage";
    }

    image.usage = static_cast<ImageUsage>(usage);

    if (image.blocks == 0 || image.blocks > kImageMaxBlocks) [[unlikely]]
    {
        return "GSTX v2 footprint out of range";
    }

    Rectangle rectangles[kImageMaxRectangles];
    const std::uint32_t expectedRectangles = Rectangles(image.blocks, rectangles);

    if (rectangleCount != expectedRectangles ||
        image.streamQwords != expectedRectangles * kImageTagQwordsPerRectangle + image.blocks * 16U ||
        bytes != kImageHeaderBytes + image.streamQwords * 16U) [[unlikely]]
    {
        return "GSTX v2 stream size does not match its footprint";
    }

    // TEX0: everything but CBP is fixed by the header; CBP leaves room for the CLUT.
    const bool indexed = Indexed(image.psm);
    const std::uint32_t clutBlocks = image.psm == kPsmT8 ? 4U : indexed ? 1U : 0U;
    const std::uint32_t cbp = Field(image.tex0, 37, 14);
    const std::uint64_t tex0 = (static_cast<std::uint64_t>(BufferWidth(image.psm, image.width)) << 14) |
                               (static_cast<std::uint64_t>(image.psm) << 20) | (static_cast<std::uint64_t>(Log2(image.width)) << 26) |
                               (static_cast<std::uint64_t>(Log2(image.height)) << 30) | (1ULL << 34) |
                               (static_cast<std::uint64_t>(cbp) << 37) | (indexed ? 1ULL << 61 : 0ULL);

    if (image.tex0 != tex0 || (indexed ? cbp + clutBlocks > image.blocks : cbp != 0)) [[unlikely]]
    {
        return "GSTX v2 TEX0 is not the cook's";
    }

    const std::uint64_t tex1 = (static_cast<std::uint64_t>(image.levels - 1U) << 2) | (1ULL << 5) | ((image.levels > 1U ? 4ULL : 1ULL) << 6);

    if (image.tex1 != tex1 || (image.clamp & ~0xFULL) != 0 || (image.clamp & 0xAULL) != 0) [[unlikely]]
    {
        return "GSTX v2 TEX1 or CLAMP is not the cook's";
    }

    // Every level lies inside the footprint; unused MIPTBP fields are zero.
    if (LastBlock(image.psm, image.width, image.height, BufferWidth(image.psm, image.width)) >= image.blocks) [[unlikely]]
    {
        return "GSTX v2 level 0 leaves its footprint";
    }

    for (std::uint32_t level = 1; level < kImageMaxLevels; ++level)
    {
        const std::uint64_t value = level <= 3U ? image.miptbp1 : image.miptbp2;
        const int shift = static_cast<int>(20U * ((level - 1U) % 3U));
        const std::uint32_t tbp = Field(value, shift, 14);
        const std::uint32_t tbw = Field(value, shift + 14, 6);

        if (level >= image.levels)
        {
            if (tbp != 0 || tbw != 0) [[unlikely]]
            {
                return "GSTX v2 MIPTBP names an unused level";
            }

            continue;
        }

        const std::uint32_t width = LevelExtent(image.width, level);
        const std::uint32_t height = LevelExtent(image.height, level);

        if (tbw != BufferWidth(image.psm, width) || tbp + LastBlock(image.psm, width, height, tbw) >= image.blocks) [[unlikely]]
        {
            return "GSTX v2 mip level leaves its footprint";
        }
    }

    if ((image.miptbp1 >> 60) != 0 || (image.miptbp2 >> 60) != 0) [[unlikely]]
    {
        return "GSTX v2 MIPTBP has stray bits";
    }

    // The stream: every tag exactly as the cook writes it, the words between them free.
    std::uint32_t offset = 0;

    for (std::uint32_t index = 0; index < expectedRectangles; ++index)
    {
        if (!TagsMatch(image.stream + offset, rectangles[index], index + 1U == expectedRectangles)) [[unlikely]]
        {
            return "GSTX v2 stream tags are not the cook's";
        }

        offset += (kImageTagQwordsPerRectangle + rectangles[index].width * rectangles[index].height / 4U) * 16U;
    }

#if PS2_D3BFG_ASSERTS
    if (Fnv(image.stream, image.streamQwords * 16U, 2166136261U) != Word(file + 24)) [[unlikely]]
    {
        return "GSTX v2 stream checksum mismatch";
    }
#endif // PS2_D3BFG_ASSERTS

    output = image;
    return nullptr;
}

PS2_COLD_FUNC const char * ValidatePack(const std::uint8_t * file, std::uint32_t bytes, Pack & output, Image * images,
                                        std::uint32_t imageCapacity)
{
    if (file == nullptr || (reinterpret_cast<std::uintptr_t>(file) & (kPackAlignment - 1U)) != 0) [[unlikely]]
    {
        return "D3TX file is not 64-byte aligned";
    }

    if (bytes < kPackHeaderBytes || bytes > kPackMaxFileBytes || Word(file) != kPackMagic || Half(file + 4) != kPackVersion ||
        Half(file + 6) != kPackHeaderBytes || Word(file + 20) != bytes || Word(file + 28) != kPackConverterVersion) [[unlikely]]
    {
        return "not a D3TX v2 file";
    }

    Pack pack = {};
    pack.materialCount = Word(file + 8);
    pack.textureCount = Word(file + 12);
    pack.materialsHash = Word(file + 16);
    pack.pageCount = Word(file + 32);

    if (pack.materialCount > kPackMaxMaterials || pack.textureCount > kPackMaxTextures || pack.pageCount > kPackMaxPages ||
        pack.textureCount > imageCapacity || Word(file + 36) != 0 || Word(file + 40) != 0 || Word(file + 44) != 0) [[unlikely]]
    {
        return "D3TX tables out of range";
    }

    const std::uint32_t pagesAt = kPackHeaderBytes + kPackMaterialBytes * pack.materialCount + kPackTextureBytes * pack.textureCount;
    const std::uint32_t tablesEnd = pagesAt + kPackPageBytes * pack.pageCount;

    if (tablesEnd > bytes) [[unlikely]]
    {
        return "D3TX tables out of range";
    }

    std::uint32_t hash = Fnv(file, 24, 2166136261U);
    hash = Fnv(reinterpret_cast<const std::uint8_t *>("\0\0\0\0"), 4, hash);
    hash = Fnv(file + 28, tablesEnd - 28U, hash);

    if (hash != Word(file + 24)) [[unlikely]]
    {
        return "D3TX table checksum mismatch";
    }

    pack.materials = static_cast<const PackMaterial *>(static_cast<const void *>(file + kPackHeaderBytes));
    pack.textures = static_cast<const PackTexture *>(static_cast<const void *>(file + kPackHeaderBytes + kPackMaterialBytes * pack.materialCount));
    pack.pages = static_cast<const std::uint32_t *>(static_cast<const void *>(file + pagesAt));

    for (std::uint32_t index = 0; index < pack.materialCount; ++index)
    {
        const PackMaterial & material = pack.materials[index];

        // Finite and bounded; NaN fails both comparisons.
        if ((material.texture != kPackNoTexture && material.texture != kPackHidden &&
             (material.texture >= pack.textureCount || (pack.textures[material.texture].flags & kPackFlagLightmap) != 0)) ||
            !(material.uvDensityLog2 >= -64.0f && material.uvDensityLog2 <= 64.0f)) [[unlikely]]
        {
            return "D3TX material record out of range";
        }
    }

    for (std::uint32_t index = 0; index < pack.pageCount; ++index)
    {
        if (pack.pages[index] >= pack.textureCount || (pack.textures[pack.pages[index]].flags & kPackFlagLightmap) == 0) [[unlikely]]
        {
            return "D3TX page record out of range";
        }
    }

    for (std::uint32_t index = 0; index < pack.textureCount; ++index)
    {
        const PackTexture & texture = pack.textures[index];

        if (texture.offset < tablesEnd || (texture.offset & (kPackAlignment - 1U)) != 0 || texture.bytes > bytes ||
            texture.offset > bytes - texture.bytes || (texture.flags & ~(kPackFlagAlphaTest | kPackFlagLightmap)) != 0 ||
            texture.flags == (kPackFlagAlphaTest | kPackFlagLightmap) || texture.reserved != 0) [[unlikely]]
        {
            return "D3TX texture record out of range";
        }

        const char * error = ValidateImage(file + texture.offset, texture.bytes, images[index]);

        if (error != nullptr) [[unlikely]]
        {
            return error;
        }

        if (((texture.flags & kPackFlagLightmap) != 0) != (images[index].usage == ImageUsage::Lightmap)) [[unlikely]]
        {
            return "D3TX texture flags disagree with its usage";
        }
    }

    output = pack;
    return nullptr;
}

} // namespace ps2::renderer::tex
