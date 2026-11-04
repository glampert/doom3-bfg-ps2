// ================================================================================================
// File: vif.h
// Brief: VIF1 code encoders, shared by the EE display list, the VU program manager and the D3WD
//        packet validator, which the host world tests also compile; hence <cstdint>, no ps2sdk.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstdint>

namespace ps2::renderer::dma
{

// VIFcode packing (VIF1: IMMEDIATE 15:0, NUM 23:16, CMD 30:24). A chain runs with TTE, so the
// upper 64 bits of every DMA tag reach VIF1 as two of these.
constexpr std::uint32_t VifCode(std::uint32_t cmd, std::uint32_t num, std::uint32_t immediate)
{
    return (immediate & 0xFFFFU) | ((num & 0xFFU) << 16) | ((cmd & 0x7FU) << 24);
}

static constexpr std::uint32_t kVifNop    = VifCode(0x00U, 0U, 0U);
static constexpr std::uint32_t kVifFlushE = VifCode(0x10U, 0U, 0U); // wait for the microprogram
static constexpr std::uint32_t kVifFlush  = VifCode(0x11U, 0U, 0U); // ... and for PATH1/PATH2 to the GS
static constexpr std::uint32_t kVifFlushA = VifCode(0x13U, 0U, 0U); // ... and for PATH3
static constexpr std::uint32_t kVifMscnt  = VifCode(0x17U, 0U, 0U);

constexpr std::uint32_t VifDirect(std::uint32_t qwords)
{
    return VifCode(0x50U, 0U, qwords); // 1..65535 qwords of GIF data to PATH2
}

constexpr std::uint32_t VifMscal(std::uint32_t instructionAddress)
{
    return VifCode(0x14U, 0U, instructionAddress);
}

// STCYCL: CL and WL cycle lengths (1, 1 for a plain unpack). BASE and OFFSET: the VU data memory
// double buffer TOPS alternates between (qword addresses).
constexpr std::uint32_t VifStcycl(std::uint32_t cl, std::uint32_t wl)
{
    return VifCode(0x01U, 0U, (cl & 0xFFU) | ((wl & 0xFFU) << 8));
}

constexpr std::uint32_t VifBase(std::uint32_t qwordAddress)
{
    return VifCode(0x03U, 0U, qwordAddress);
}

constexpr std::uint32_t VifOffset(std::uint32_t qwords)
{
    return VifCode(0x02U, 0U, qwords);
}

// ITOP: the value a program reads with XITOP (a batch's vertex count).
constexpr std::uint32_t VifItop(std::uint32_t value)
{
    return VifCode(0x04U, 0U, value);
}

// MPG: 'count' 64-bit instructions (256 encodes as 0) to micro memory at 'instructionAddress'.
// VIF1 waits for the running microprogram to end first.
constexpr std::uint32_t VifMpg(std::uint32_t count, std::uint32_t instructionAddress)
{
    return VifCode(0x4AU, count, instructionAddress);
}

// UNPACK formats: the low nibble of the VIFcode command (V4-32 is 0xC; 0x60 is the UNPACK group).
enum class UnpackFormat : std::uint32_t
{
    S32   = 0x0U,
    S16   = 0x1U,
    S8    = 0x2U,
    V2_32 = 0x4U,
    V2_16 = 0x5U,
    V2_8  = 0x6U,
    V3_32 = 0x8U,
    V3_16 = 0x9U,
    V3_8  = 0xAU,
    V4_32 = 0xCU,
    V4_16 = 0xDU,
    V4_8  = 0xEU,
    V4_5  = 0xFU
};

// UNPACK 'count' vectors to 'qwordAddress'; 'relativeToTops' adds the double buffer's TOPS;
// 'unsignedExtend' zero-extends the 16- and 8-bit formats instead of sign-extending them.
constexpr std::uint32_t VifUnpack(UnpackFormat format, std::uint32_t count, std::uint32_t qwordAddress,
                                  bool relativeToTops, bool unsignedExtend)
{
    const std::uint32_t immediate = (qwordAddress & 0x3FFU) | (unsignedExtend ? 1U << 14 : 0U) | (relativeToTops ? 1U << 15 : 0U);
    return VifCode(0x60U | static_cast<std::uint32_t>(format), count, immediate);
}

} // namespace ps2::renderer::dma
