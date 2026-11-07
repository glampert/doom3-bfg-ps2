// ================================================================================================
// File: texture_format.h
// Brief: Validate a cooked GSTX v2 texture in place (its GS memory image, upload stream and register
// templates) and relocate the registers to an allocation, so a file the GS would misread or hang
// on is refused at load and an upload needs no work per texel.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include <cstddef>
#include <cstdint>

namespace ps2::renderer::tex
{

// The layout is defined by src/tools/assets/gstx_v2.h, which writes it.
static constexpr std::uint32_t kImageMagic = 0x58545347;
static constexpr std::uint32_t kImageVersion = 2;
static constexpr std::uint32_t kImageHeaderBytes = 96;
static constexpr std::uint32_t kImageMaxExtent = 512;
static constexpr std::uint32_t kImageMaxLevels = 7;
static constexpr std::uint32_t kImageMaxBlocks = 2048;
static constexpr std::uint32_t kImageMaxRectangles = 7;
static constexpr std::uint32_t kImageTagQwordsPerRectangle = 5;
static constexpr std::uint32_t kImageMaxFileBytes = kImageHeaderBytes + 16U * (kImageMaxRectangles * kImageTagQwordsPerRectangle + kImageMaxBlocks * 16U);

static constexpr std::uint8_t kPsmCt32 = 0x00;
static constexpr std::uint8_t kPsmCt16 = 0x02;
static constexpr std::uint8_t kPsmT8 = 0x13;
static constexpr std::uint8_t kPsmT4 = 0x14;

enum class ImageUsage : std::uint8_t
{
    Diffuse,
    Default,
    Light,
    Sphere,
    Lightmap,
    Count
};

// A validated file. The stream points into it: the BITBLTBUF the uploader writes, then a REF of
// 'streamQwords' from 'stream', recreate the image at the allocation.
struct Image
{
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t levels;
    std::uint32_t blocks;
    std::uint32_t streamQwords;
    std::uint8_t psm;
    ImageUsage usage;
    std::uint64_t tex0;
    std::uint64_t tex1;
    std::uint64_t miptbp1;
    std::uint64_t miptbp2;
    std::uint64_t clamp;
    const std::uint8_t * stream;
};

// Null when the file is valid; it must stay alive and 16-byte aligned. The stream's checksum is
// checked only in assertion builds (corrupt texels cannot hang the GIF; every tag is checked).
PS2_COLD_FUNC const char * ValidateImage(const std::uint8_t * file, std::uint32_t bytes, Image & output);

// The registers of an image uploaded at block 'base': every block address moves by 'base'
// (TEX0's TBP0 and CBP, MIPTBP's TBP1..TBP6). No field overflows below block 16,384.
inline std::uint64_t RelocateTex0(std::uint64_t tex0, std::uint32_t base)
{
    return tex0 + static_cast<std::uint64_t>(base) * (1ULL | (1ULL << 37));
}

inline std::uint64_t RelocateMiptbp(std::uint64_t miptbp, std::uint32_t base)
{
    return miptbp + static_cast<std::uint64_t>(base) * (1ULL | (1ULL << 20) | (1ULL << 40));
}

} // namespace ps2::renderer::tex
