// ================================================================================================
// File: gstx_v2.h
// Brief: Write, validate and sample GSTX v2, a texture stored as its GS memory image (every mip
// level and the CLUT in one run of blocks) with the GIF stream that uploads it as PSMCT32
// rectangles, so the target uploads a world texture without touching a texel.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "assets/gs_memory.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace assets
{

// Little-endian. A 96-byte header, then the GIF stream (RENDERING_ARCHITECTURE.md 10.4):
//
//   u32 magic 'GSTX', u16 version 2, u16 header bytes 96, u16 width/height (level 0),
//   u8 PSM, u8 levels, u8 usage, u8 rectangles, u32 blocks, u32 stream qwords,
//   u32 FNV-1a of the stream, u32 FNV-1a of the header with this field read as zero,
//   u64 TEX0, TEX1, MIPTBP1, MIPTBP2, CLAMP (block addresses relative to 0), 24 zero bytes.
//
// The stream holds, per rectangle of a 64-wide PSMCT32 buffer, an A+D tag with TRXPOS, TRXREG
// and TRXDIR, an IMAGE tag and the rectangle's words; together the rectangles cover blocks
// [0, blocks) in order, whole pages first (at most 32 per rectangle), then one rectangle per set
// bit of blocks mod 32. The last IMAGE tag sets EOP. The target writes BITBLTBUF (DBP = its
// allocation, DBW 1, PSMCT32) before the stream and adds the allocation to the registers' block
// addresses. Every texture has exactly one valid encoding.
static constexpr std::uint32_t kGstxV2Version = 2;
static constexpr std::size_t kGstxV2HeaderBytes = 96;
static constexpr int kGstxV2MaxExtent = 512;
static constexpr int kGstxV2MaxLevels = 7;
// 2,048 rows of a 64-wide PSMCT32 buffer: TRXPOS addresses 11 bits of y.
static constexpr std::uint32_t kGstxV2MaxBlocks = 2048;
static constexpr std::uint32_t kGstxV2MaxRectangles = 7;
static constexpr std::uint32_t kGstxV2TagQwordsPerRectangle = 5;

enum class GstxUsage : std::uint8_t
{
    Diffuse,
    Default,
    Light,
    Sphere,
    Lightmap,
    Count
};

enum class GstxWrap : std::uint8_t
{
    Repeat,
    Clamp
};

// What the writer encodes. Level n is max(1, width >> n) x max(1, height >> n) values, row-major:
// CLUT indices for PSMT8/PSMT4, RGBA words (R | G << 8 | B << 16 | A << 24, GS alpha 0..0x80) for
// PSMCT32, RGBA5551 halfwords for PSMCT16. The palette (256 or 16 RGBA words, GS alpha, palette
// order) is empty for direct colour.
struct GstxV2Image
{
    int width = 0;
    int height = 0;
    GsPsm psm = GsPsm::T8;
    GstxUsage usage = GstxUsage::Diffuse;
    GstxWrap wrapS = GstxWrap::Repeat;
    GstxWrap wrapT = GstxWrap::Repeat;
    std::vector<std::vector<std::uint32_t> > levels;
    std::vector<std::uint32_t> palette;
};

struct GstxV2Registers
{
    std::uint64_t tex0 = 0;
    std::uint64_t tex1 = 0;
    std::uint64_t miptbp1 = 0;
    std::uint64_t miptbp2 = 0;
    std::uint64_t clamp = 0;
};

// A validated file: its header fields, registers and the memory image the stream writes.
struct GstxV2Texture
{
    int width = 0;
    int height = 0;
    GsPsm psm = GsPsm::T8;
    int levels = 0;
    GstxUsage usage = GstxUsage::Diffuse;
    std::uint32_t blocks = 0;
    std::uint32_t rectangles = 0;
    GstxV2Registers registers;
    GsMemory memory{ 0 };
};

struct GstxV2Rectangle
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// The rectangles of a 64-wide PSMCT32 buffer that cover blocks [0, blocks), in block order.
std::vector<GstxV2Rectangle> GstxV2Rectangles(std::uint32_t blocks);

// The TBW the cook gives a level of this width (the smallest covering it, even for PSMT8/PSMT4).
std::uint32_t GstxV2BufferWidth(GsPsm psm, int width);

// Validates the image (sizes, values within the format, palette size), lays it out and encodes it.
bool WriteGstxV2(const GstxV2Image & image, std::vector<std::uint8_t> & output, std::string & error);

// Validates every field, both checksums, the stream's tags and the canonical layout.
bool ReadGstxV2(std::span<const std::uint8_t> bytes, GstxV2Texture & output, std::string & error);

// The texel of a level as the GS samples it through the file's registers: the CLUT entry for
// indexed formats, the stored value otherwise. False outside the level.
bool SampleGstxV2(const GstxV2Texture & texture, int level, int x, int y, std::uint32_t & value);

} // namespace assets
