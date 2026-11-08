// ================================================================================================
// File: gstx_v2.cpp
// Brief: Write, validate and sample GSTX v2, a texture stored as its GS memory image (every mip
// level and the CLUT in one run of blocks) with the GIF stream that uploads it as PSMCT32
// rectangles, so the target uploads a world texture without touching a texel.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/gstx_v2.h"

#include "assets/bytes.h"
#include "assets/gs_texture.h"
#include "assets/gstx.h"

#include <algorithm>
#include <cstring>

namespace assets
{

// ------------------------------------------------------------------------------------------------
// GS register fields
// ------------------------------------------------------------------------------------------------

static constexpr std::uint64_t kRegisterTrxpos = 0x51;
static constexpr std::uint64_t kRegisterTrxreg = 0x52;
static constexpr std::uint64_t kRegisterTrxdir = 0x53;
static constexpr std::uint64_t kGifFlagPacked = 0;
static constexpr std::uint64_t kGifFlagImage = 2;
static constexpr std::uint64_t kGifRegisterAd = 0xE;
// TEX1.MMIN: LINEAR_MIPMAP_NEAREST samples one level bilinearly; trilinear costs a second read.
static constexpr std::uint64_t kMinifyLinear = 1;
static constexpr std::uint64_t kMinifyLinearMipNearest = 4;

static std::uint64_t Field(std::uint64_t value, int shift)
{
    return value << shift;
}

static std::uint32_t Extract(std::uint64_t value, int shift, int bits)
{
    return static_cast<std::uint32_t>((value >> shift) & ((1ULL << bits) - 1ULL));
}

static bool PowerOfTwo(int value)
{
    return value > 0 && (value & (value - 1)) == 0;
}

static std::uint32_t Log2(int value)
{
    std::uint32_t log = 0;

    while ((1 << log) < value)
    {
        ++log;
    }

    return log;
}

static bool Indexed(GsPsm psm)
{
    return psm == GsPsm::T8 || psm == GsPsm::T4;
}

static bool KnownPsm(std::uint32_t value)
{
    return value == static_cast<std::uint32_t>(GsPsm::Ct32) || value == static_cast<std::uint32_t>(GsPsm::Ct16) ||
           value == static_cast<std::uint32_t>(GsPsm::T8) || value == static_cast<std::uint32_t>(GsPsm::T4);
}

static int LevelExtent(int extent, int level)
{
    return std::max(1, extent >> level);
}

static std::uint64_t GifTag(std::uint64_t loops, bool endOfPacket, std::uint64_t flag, std::uint64_t registers)
{
    return loops | Field(endOfPacket ? 1U : 0U, 15) | Field(flag, 58) | Field(registers, 60);
}

static void WriteLe64(std::uint8_t * bytes, std::uint64_t value)
{
    WriteLe32(bytes, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
    WriteLe32(bytes + 4, static_cast<std::uint32_t>(value >> 32));
}

static std::uint64_t ReadLe64(const std::uint8_t * bytes)
{
    return static_cast<std::uint64_t>(ReadLe32(bytes)) | (static_cast<std::uint64_t>(ReadLe32(bytes + 4)) << 32);
}

// The CLUT tile position of palette entry 'index' in CSM1 (16x16 for PSMT8, 8x2 for PSMT4).
static void ClutPosition(GsPsm psm, std::uint32_t index, int & x, int & y)
{
    if (psm == GsPsm::T8)
    {
        const std::uint32_t swizzled = ClutSwizzle(index);
        x = static_cast<int>(swizzled & 15U);
        y = static_cast<int>(swizzled >> 4);
    }
    else
    {
        x = static_cast<int>(index & 7U);
        y = static_cast<int>(index >> 3);
    }
}

static std::uint32_t ClutBlocks(GsPsm psm)
{
    return psm == GsPsm::T8 ? 4U : psm == GsPsm::T4 ? 1U : 0U;
}

// The blocks a w x h region of the format occupies at block 0 with buffer width 'bw', sorted.
static std::vector<std::uint32_t> Footprint(GsPsm psm, int width, int height, std::uint32_t bw)
{
    std::vector<std::uint32_t> blocks;
    const int stepX = std::min(width, GsBlockWidth(psm));
    const int stepY = std::min(height, GsBlockHeight(psm));

    for (int y = 0; y < height; y += stepY)
    {
        for (int x = 0; x < width; x += stepX)
        {
            blocks.push_back(GsBlockNumber(psm, x, y, 0, bw));
        }
    }

    std::sort(blocks.begin(), blocks.end());
    blocks.erase(std::unique(blocks.begin(), blocks.end()), blocks.end());
    return blocks;
}

// The lowest block address where every block of 'footprint' is free; marks them used.
static std::uint32_t Place(const std::vector<std::uint32_t> & footprint, std::vector<bool> & used)
{
    for (std::uint32_t base = 0;; ++base)
    {
        bool free = true;

        for (const std::uint32_t block : footprint)
        {
            if (base + block < used.size() && used[base + block])
            {
                free = false;
                break;
            }
        }

        if (!free)
        {
            continue;
        }

        for (const std::uint32_t block : footprint)
        {
            if (base + block >= used.size())
            {
                used.resize(base + block + 1, false);
            }

            used[base + block] = true;
        }

        return base;
    }
}

// ------------------------------------------------------------------------------------------------
// Layout
// ------------------------------------------------------------------------------------------------

std::vector<GstxV2Rectangle> GstxV2Rectangles(std::uint32_t blocks)
{
    std::vector<GstxV2Rectangle> rectangles;
    std::uint32_t position = 0;

    while (blocks - position >= kGsPageBlocks)
    {
        const std::uint32_t pages = std::min<std::uint32_t>((blocks - position) / kGsPageBlocks, 32U);
        rectangles.push_back({ 0, static_cast<int>(position / kGsPageBlocks * 32U), 64, static_cast<int>(pages * 32U) });
        position += pages * kGsPageBlocks;
    }

    // Inside a page of a 64-wide PSMCT32 buffer the blocks run in Morton order: x0 y0 x1 y1 x2.
    static constexpr int kSizes[][3] = { { 16, 32, 32 }, { 8, 32, 16 }, { 4, 16, 16 }, { 2, 16, 8 }, { 1, 8, 8 } };

    for (const auto & size : kSizes)
    {
        const std::uint32_t run = static_cast<std::uint32_t>(size[0]);

        if (((blocks - position) & run) == 0)
        {
            continue;
        }

        const std::uint32_t block = position % kGsPageBlocks;
        const int x = static_cast<int>(((block & 1U) | (((block >> 2) & 1U) << 1) | (((block >> 4) & 1U) << 2)) * 8U);
        const int y = static_cast<int>(position / kGsPageBlocks * 32U + ((((block >> 1) & 1U) | (((block >> 3) & 1U) << 1)) * 8U));
        rectangles.push_back({ x, y, size[1], size[2] });
        position += run;
    }

    return rectangles;
}

std::uint32_t GstxV2BufferWidth(GsPsm psm, int width)
{
    const std::uint32_t units = static_cast<std::uint32_t>((width + 63) / 64);

    if (Indexed(psm))
    {
        return std::max<std::uint32_t>(2U, (units + 1U) & ~1U);
    }

    return std::max<std::uint32_t>(1U, units);
}

namespace
{

struct Layout
{
    std::uint32_t tbp[kGstxV2MaxLevels] = {};
    std::uint32_t tbw[kGstxV2MaxLevels] = {};
    std::uint32_t cbp = 0;
    std::uint32_t blocks = 0;
};

} // namespace

static Layout MakeLayout(GsPsm psm, int width, int height, int levels)
{
    Layout layout;
    std::vector<bool> used;

    for (int level = 0; level < levels; ++level)
    {
        const int levelWidth = LevelExtent(width, level);
        const int levelHeight = LevelExtent(height, level);
        layout.tbw[level] = GstxV2BufferWidth(psm, levelWidth);
        layout.tbp[level] = Place(Footprint(psm, levelWidth, levelHeight, layout.tbw[level]), used);
    }

    if (Indexed(psm))
    {
        std::vector<std::uint32_t> clut;

        for (std::uint32_t block = 0; block < ClutBlocks(psm); ++block)
        {
            clut.push_back(block);
        }

        layout.cbp = Place(clut, used);
    }

    layout.blocks = static_cast<std::uint32_t>(used.size());
    return layout;
}

static GstxV2Registers MakeRegisters(const GstxV2Image & image, int levels, const Layout & layout)
{
    const bool indexed = Indexed(image.psm);
    GstxV2Registers registers;

    registers.tex0 = Field(layout.tbw[0], 14) | Field(static_cast<std::uint64_t>(image.psm), 20) |
                     Field(Log2(image.width), 26) | Field(Log2(image.height), 30) | Field(1, 34) |
                     Field(indexed ? layout.cbp : 0U, 37) | Field(indexed ? 1U : 0U, 61);

    registers.tex1 = Field(static_cast<std::uint64_t>(levels - 1), 2) | Field(1, 5) |
                     Field(levels > 1 ? kMinifyLinearMipNearest : kMinifyLinear, 6);

    for (int level = 1; level < levels; ++level)
    {
        std::uint64_t & target = level <= 3 ? registers.miptbp1 : registers.miptbp2;
        const int shift = 20 * ((level - 1) % 3);
        target |= Field(layout.tbp[level], shift) | Field(layout.tbw[level], shift + 14);
    }

    registers.clamp = Field(static_cast<std::uint64_t>(image.wrapS), 0) | Field(static_cast<std::uint64_t>(image.wrapT), 2);
    return registers;
}

static void WriteTags(std::uint8_t * at, const GstxV2Rectangle & rectangle, bool last)
{
    WriteLe64(at, GifTag(3, false, kGifFlagPacked, 1));
    WriteLe64(at + 8, kGifRegisterAd);
    WriteLe64(at + 16, Field(static_cast<std::uint64_t>(rectangle.x), 32) | Field(static_cast<std::uint64_t>(rectangle.y), 48));
    WriteLe64(at + 24, kRegisterTrxpos);
    WriteLe64(at + 32, static_cast<std::uint64_t>(rectangle.width) | Field(static_cast<std::uint64_t>(rectangle.height), 32));
    WriteLe64(at + 40, kRegisterTrxreg);
    WriteLe64(at + 48, 0);
    WriteLe64(at + 56, kRegisterTrxdir);
    WriteLe64(at + 64, GifTag(static_cast<std::uint64_t>(rectangle.width * rectangle.height / 4), last, kGifFlagImage, 0));
    WriteLe64(at + 72, 0);
}

// ------------------------------------------------------------------------------------------------
// Writer
// ------------------------------------------------------------------------------------------------

static bool ValidateImage(const GstxV2Image & image, std::string & error)
{
    if (!PowerOfTwo(image.width) || !PowerOfTwo(image.height) || image.width > kGstxV2MaxExtent || image.height > kGstxV2MaxExtent)
    {
        error = "GSTX v2 sizes must be powers of two up to 512";
        return false;
    }

    const int levels = static_cast<int>(image.levels.size());

    if (levels < 1 || levels > kGstxV2MaxLevels || (std::min(image.width, image.height) >> (levels - 1)) < 1)
    {
        error = "GSTX v2 takes 1 to 7 levels, none smaller than one texel";
        return false;
    }

    if (static_cast<std::uint8_t>(image.usage) >= static_cast<std::uint8_t>(GstxUsage::Count))
    {
        error = "unknown GSTX v2 usage";
        return false;
    }

    const std::uint32_t limit = image.psm == GsPsm::T8 ? 0xFFU : image.psm == GsPsm::T4 ? 0xFU : image.psm == GsPsm::Ct16 ? 0xFFFFU : 0xFFFFFFFFU;

    for (int level = 0; level < levels; ++level)
    {
        const std::size_t texels = static_cast<std::size_t>(LevelExtent(image.width, level)) * static_cast<std::size_t>(LevelExtent(image.height, level));

        if (image.levels[static_cast<std::size_t>(level)].size() != texels)
        {
            error = "GSTX v2 level " + std::to_string(level) + " has the wrong texel count";
            return false;
        }

        for (const std::uint32_t value : image.levels[static_cast<std::size_t>(level)])
        {
            if (value > limit)
            {
                error = "GSTX v2 texel exceeds its format";
                return false;
            }
        }
    }

    const std::size_t paletteSize = image.psm == GsPsm::T8 ? 256U : image.psm == GsPsm::T4 ? 16U : 0U;

    if (image.palette.size() != paletteSize)
    {
        error = "GSTX v2 palette size does not match the format";
        return false;
    }

    // GS alpha 0x80 is 1.0; a lightmap's palette is the light intensity ramp, which reaches 2x
    // through Cd x As.
    for (const std::uint32_t color : image.palette)
    {
        if ((color >> 24) > 0x80U && image.usage != GstxUsage::Lightmap)
        {
            error = "GSTX v2 palette alpha exceeds 0x80";
            return false;
        }
    }

    if (image.psm == GsPsm::Ct32)
    {
        for (const auto & level : image.levels)
        {
            for (const std::uint32_t color : level)
            {
                if ((color >> 24) > 0x80U)
                {
                    error = "GSTX v2 texel alpha exceeds 0x80";
                    return false;
                }
            }
        }
    }

    return true;
}

bool WriteGstxV2(const GstxV2Image & image, std::vector<std::uint8_t> & output, std::string & error)
{
    if (!ValidateImage(image, error))
    {
        return false;
    }

    const int levels = static_cast<int>(image.levels.size());
    const Layout layout = MakeLayout(image.psm, image.width, image.height, levels);

    if (layout.blocks > kGstxV2MaxBlocks)
    {
        error = "GSTX v2 footprint exceeds 2,048 blocks";
        return false;
    }

    GsMemory memory(layout.blocks);

    for (int level = 0; level < levels; ++level)
    {
        const int levelWidth = LevelExtent(image.width, level);
        const int levelHeight = LevelExtent(image.height, level);
        const auto & texels = image.levels[static_cast<std::size_t>(level)];

        for (int y = 0; y < levelHeight; ++y)
        {
            for (int x = 0; x < levelWidth; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(levelWidth) + static_cast<std::size_t>(x);
                (void)memory.Write(image.psm, layout.tbp[level], layout.tbw[level], x, y, texels[index]);
            }
        }
    }

    for (std::uint32_t entry = 0; entry < image.palette.size(); ++entry)
    {
        int x = 0;
        int y = 0;
        ClutPosition(image.psm, entry, x, y);
        (void)memory.Write(GsPsm::Ct32, layout.cbp, 1, x, y, image.palette[entry]);
    }

    const std::vector<GstxV2Rectangle> rectangles = GstxV2Rectangles(layout.blocks);
    const std::uint32_t streamQwords = static_cast<std::uint32_t>(rectangles.size()) * kGstxV2TagQwordsPerRectangle + layout.blocks * 16U;
    const GstxV2Registers registers = MakeRegisters(image, levels, layout);

    output.assign(kGstxV2HeaderBytes + static_cast<std::size_t>(streamQwords) * 16U, 0);
    std::uint8_t * stream = output.data() + kGstxV2HeaderBytes;
    std::uint8_t * at = stream;

    for (std::size_t index = 0; index < rectangles.size(); ++index)
    {
        const GstxV2Rectangle & rectangle = rectangles[index];
        WriteTags(at, rectangle, index + 1 == rectangles.size());
        at += kGstxV2TagQwordsPerRectangle * 16U;

        for (int y = rectangle.y; y < rectangle.y + rectangle.height; ++y)
        {
            for (int x = rectangle.x; x < rectangle.x + rectangle.width; ++x)
            {
                std::uint32_t word = 0;
                (void)memory.Read(GsPsm::Ct32, 0, 1, x, y, word);
                WriteLe32(at, word);
                at += 4;
            }
        }
    }

    std::uint8_t * header = output.data();
    WriteLe32(header, kGstxMagic);
    WriteLe16(header + 4, static_cast<std::uint16_t>(kGstxV2Version));
    WriteLe16(header + 6, static_cast<std::uint16_t>(kGstxV2HeaderBytes));
    WriteLe16(header + 8, static_cast<std::uint16_t>(image.width));
    WriteLe16(header + 10, static_cast<std::uint16_t>(image.height));
    header[12] = static_cast<std::uint8_t>(image.psm);
    header[13] = static_cast<std::uint8_t>(levels);
    header[14] = static_cast<std::uint8_t>(image.usage);
    header[15] = static_cast<std::uint8_t>(rectangles.size());
    WriteLe32(header + 16, layout.blocks);
    WriteLe32(header + 20, streamQwords);
    WriteLe32(header + 24, Fnv1a32({ stream, static_cast<std::size_t>(streamQwords) * 16U }));
    WriteLe64(header + 32, registers.tex0);
    WriteLe64(header + 40, registers.tex1);
    WriteLe64(header + 48, registers.miptbp1);
    WriteLe64(header + 56, registers.miptbp2);
    WriteLe64(header + 64, registers.clamp);
    WriteLe32(header + 28, Fnv1a32({ header, kGstxV2HeaderBytes }));
    return true;
}

// ------------------------------------------------------------------------------------------------
// Reader and sampler
// ------------------------------------------------------------------------------------------------

static void LevelBuffer(const GstxV2Texture & texture, int level, std::uint32_t & tbp, std::uint32_t & tbw)
{
    if (level == 0)
    {
        tbp = Extract(texture.registers.tex0, 0, 14);
        tbw = Extract(texture.registers.tex0, 14, 6);
        return;
    }

    const std::uint64_t value = level <= 3 ? texture.registers.miptbp1 : texture.registers.miptbp2;
    const int shift = 20 * ((level - 1) % 3);
    tbp = Extract(value, shift, 14);
    tbw = Extract(value, shift + 14, 6);
}

bool SampleGstxV2(const GstxV2Texture & texture, int level, int x, int y, std::uint32_t & value)
{
    if (level < 0 || level >= texture.levels || x < 0 || y < 0 || x >= LevelExtent(texture.width, level) ||
        y >= LevelExtent(texture.height, level))
    {
        return false;
    }

    std::uint32_t tbp = 0;
    std::uint32_t tbw = 0;
    LevelBuffer(texture, level, tbp, tbw);

    std::uint32_t texel = 0;

    if (!texture.memory.Read(texture.psm, tbp, tbw, x, y, texel))
    {
        return false;
    }

    if (!Indexed(texture.psm))
    {
        value = texel;
        return true;
    }

    int clutX = 0;
    int clutY = 0;
    ClutPosition(texture.psm, texel, clutX, clutY);
    return texture.memory.Read(GsPsm::Ct32, Extract(texture.registers.tex0, 37, 14), 1, clutX, clutY, value);
}

static bool Fail(std::string & error, const char * message)
{
    error = message;
    return false;
}

bool ReadGstxV2(std::span<const std::uint8_t> bytes, GstxV2Texture & output, std::string & error)
{
    if (bytes.size() < kGstxV2HeaderBytes)
    {
        return Fail(error, "truncated GSTX v2 header");
    }

    const std::uint8_t * header = bytes.data();

    if (ReadLe32(header) != kGstxMagic || ReadLe16(header + 4) != kGstxV2Version || ReadLe16(header + 6) != kGstxV2HeaderBytes)
    {
        return Fail(error, "not a GSTX v2 file");
    }

    std::uint8_t copy[kGstxV2HeaderBytes];
    std::memcpy(copy, header, kGstxV2HeaderBytes);
    WriteLe32(copy + 28, 0);

    if (Fnv1a32(copy) != ReadLe32(header + 28))
    {
        return Fail(error, "GSTX v2 header checksum mismatch");
    }

    for (std::size_t index = 72; index < kGstxV2HeaderBytes; ++index)
    {
        if (header[index] != 0)
        {
            return Fail(error, "GSTX v2 reserved bytes are not zero");
        }
    }

    GstxV2Texture texture;
    texture.width = ReadLe16(header + 8);
    texture.height = ReadLe16(header + 10);
    texture.levels = header[13];
    texture.rectangles = header[15];
    texture.blocks = ReadLe32(header + 16);
    const std::uint32_t streamQwords = ReadLe32(header + 20);

    if (!KnownPsm(header[12]) || header[14] >= static_cast<std::uint8_t>(GstxUsage::Count))
    {
        return Fail(error, "unknown GSTX v2 format or usage");
    }

    texture.psm = static_cast<GsPsm>(header[12]);
    texture.usage = static_cast<GstxUsage>(header[14]);

    if (texture.blocks == 0 || texture.blocks > kGstxV2MaxBlocks)
    {
        return Fail(error, "GSTX v2 footprint out of range");
    }

    const std::vector<GstxV2Rectangle> rectangles = GstxV2Rectangles(texture.blocks);

    if (texture.rectangles != rectangles.size() ||
        streamQwords != texture.rectangles * kGstxV2TagQwordsPerRectangle + texture.blocks * 16U ||
        bytes.size() != kGstxV2HeaderBytes + static_cast<std::size_t>(streamQwords) * 16U)
    {
        return Fail(error, "GSTX v2 stream size does not match its footprint");
    }

    const std::span<const std::uint8_t> stream = bytes.subspan(kGstxV2HeaderBytes);

    if (Fnv1a32(stream) != ReadLe32(header + 24))
    {
        return Fail(error, "GSTX v2 stream checksum mismatch");
    }

    texture.registers.tex0 = ReadLe64(header + 32);
    texture.registers.tex1 = ReadLe64(header + 40);
    texture.registers.miptbp1 = ReadLe64(header + 48);
    texture.registers.miptbp2 = ReadLe64(header + 56);
    texture.registers.clamp = ReadLe64(header + 64);

    // The stream's tags, then its words into the memory image.
    texture.memory = GsMemory(texture.blocks);
    std::size_t offset = 0;

    for (std::size_t index = 0; index < rectangles.size(); ++index)
    {
        const GstxV2Rectangle & rectangle = rectangles[index];
        std::uint8_t expected[kGstxV2TagQwordsPerRectangle * 16U];
        WriteTags(expected, rectangle, index + 1 == rectangles.size());

        if (std::memcmp(expected, stream.data() + offset, sizeof(expected)) != 0)
        {
            return Fail(error, "GSTX v2 stream tags are not canonical");
        }

        offset += sizeof(expected);

        for (int y = rectangle.y; y < rectangle.y + rectangle.height; ++y)
        {
            for (int x = rectangle.x; x < rectangle.x + rectangle.width; ++x)
            {
                (void)texture.memory.Write(GsPsm::Ct32, 0, 1, x, y, ReadLe32(stream.data() + offset));
                offset += 4;
            }
        }
    }

    // Canonical: sample every level and the palette back out and encode them again.
    if (!PowerOfTwo(texture.width) || !PowerOfTwo(texture.height) || texture.width > kGstxV2MaxExtent ||
        texture.height > kGstxV2MaxExtent || texture.levels < 1 || texture.levels > kGstxV2MaxLevels ||
        (std::min(texture.width, texture.height) >> (texture.levels - 1)) < 1)
    {
        return Fail(error, "GSTX v2 size or level count out of range");
    }

    GstxV2Image image;
    image.width = texture.width;
    image.height = texture.height;
    image.psm = texture.psm;
    image.usage = texture.usage;
    image.wrapS = static_cast<GstxWrap>(Extract(texture.registers.clamp, 0, 2) & 1U);
    image.wrapT = static_cast<GstxWrap>(Extract(texture.registers.clamp, 2, 2) & 1U);

    const Layout layout = MakeLayout(texture.psm, texture.width, texture.height, texture.levels);

    if (layout.blocks != texture.blocks)
    {
        return Fail(error, "GSTX v2 footprint is not canonical");
    }

    // Sample through the canonical layout, so a register pointing elsewhere fails the comparison.
    GstxV2Texture canonical;
    canonical.width = texture.width;
    canonical.height = texture.height;
    canonical.psm = texture.psm;
    canonical.levels = texture.levels;
    canonical.registers.tex0 = Field(layout.tbw[0], 14) | Field(Indexed(texture.psm) ? layout.cbp : 0U, 37);

    for (int level = 1; level < texture.levels; ++level)
    {
        std::uint64_t & target = level <= 3 ? canonical.registers.miptbp1 : canonical.registers.miptbp2;
        const int shift = 20 * ((level - 1) % 3);
        target |= Field(layout.tbp[level], shift) | Field(layout.tbw[level], shift + 14);
    }

    const bool indexed = Indexed(texture.psm);

    for (int level = 0; level < texture.levels; ++level)
    {
        const int levelWidth = LevelExtent(texture.width, level);
        const int levelHeight = LevelExtent(texture.height, level);
        std::vector<std::uint32_t> texels;
        texels.reserve(static_cast<std::size_t>(levelWidth) * static_cast<std::size_t>(levelHeight));

        for (int y = 0; y < levelHeight; ++y)
        {
            for (int x = 0; x < levelWidth; ++x)
            {
                std::uint32_t tbp = 0;
                std::uint32_t tbw = 0;
                LevelBuffer(canonical, level, tbp, tbw);
                std::uint32_t value = 0;
                (void)texture.memory.Read(texture.psm, tbp, tbw, x, y, value);
                texels.push_back(value);
            }
        }

        image.levels.push_back(std::move(texels));
    }

    if (indexed)
    {
        const std::uint32_t entries = texture.psm == GsPsm::T8 ? 256U : 16U;

        for (std::uint32_t entry = 0; entry < entries; ++entry)
        {
            int x = 0;
            int y = 0;
            ClutPosition(texture.psm, entry, x, y);
            std::uint32_t color = 0;
            (void)texture.memory.Read(GsPsm::Ct32, layout.cbp, 1, x, y, color);
            image.palette.push_back(color);
        }
    }

    std::vector<std::uint8_t> encoded;
    std::string encodeError;

    if (!WriteGstxV2(image, encoded, encodeError) || encoded.size() != bytes.size() ||
        std::memcmp(encoded.data(), bytes.data(), bytes.size()) != 0)
    {
        return Fail(error, "GSTX v2 file is not canonical");
    }

    output = std::move(texture);
    return true;
}

} // namespace assets
