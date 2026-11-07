// ================================================================================================
// File: gs_memory.cpp
// Brief: Emulate GS local memory addressing (pages, blocks, columns) for the four storage formats
// the cook writes, so a texture can be laid out exactly as the GS will hold it and stored as the
// 32-bit words that recreate it, with no reordering left for the EE.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/gs_memory.h"

namespace assets
{

// ------------------------------------------------------------------------------------------------
// Address tables (GS manual 8.3): blocks within a page, then the pixel's unit within its block
// (a word for PSMCT32, a halfword for PSMCT16, a byte for PSMT8, a nibble for PSMT4). PSMT8 shares
// PSMCT32's block arrangement and PSMT4 shares PSMCT16's, which is what lets a 32- or 16-bit
// transfer write an 8- or 4-bit texture.
// ------------------------------------------------------------------------------------------------

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

// The first column of each block; the second keeps it plus 32 words (PSMCT32) or 64 halfwords
// (PSMCT16) and so on, as the per-format functions below derive.
static constexpr std::uint8_t kColumn32[2][8] = {
    { 0, 1, 4, 5, 8, 9, 12, 13 },
    { 2, 3, 6, 7, 10, 11, 14, 15 }
};

static constexpr std::uint8_t kColumn16[2][16] = {
    { 0, 2, 8, 10, 16, 18, 24, 26, 1, 3, 9, 11, 17, 19, 25, 27 },
    { 4, 6, 12, 14, 20, 22, 28, 30, 5, 7, 13, 15, 21, 23, 29, 31 }
};

// Rows 0-3 of a PSMT8 block (one 64-byte column); rows 4-7 are these XOR 96, rows 8-15 add 128.
static constexpr std::uint8_t kColumn8[4][16] = {
    { 0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54 },
    { 8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62 },
    { 33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23 },
    { 41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31 }
};

// Rows 0-3 of a PSMT4 block, in nibbles; rows 4-7 are these XOR 192, rows 8-15 add 256.
static constexpr std::uint16_t kColumn4[4][32] = {
    { 0, 8, 32, 40, 64, 72, 96, 104, 2, 10, 34, 42, 66, 74, 98, 106,
      4, 12, 36, 44, 68, 76, 100, 108, 6, 14, 38, 46, 70, 78, 102, 110 },
    { 16, 24, 48, 56, 80, 88, 112, 120, 18, 26, 50, 58, 82, 90, 114, 122,
      20, 28, 52, 60, 84, 92, 116, 124, 22, 30, 54, 62, 86, 94, 118, 126 },
    { 65, 73, 97, 105, 1, 9, 33, 41, 67, 75, 99, 107, 3, 11, 35, 43,
      69, 77, 101, 109, 5, 13, 37, 45, 71, 79, 103, 111, 7, 15, 39, 47 },
    { 81, 89, 113, 121, 17, 25, 49, 57, 83, 91, 115, 123, 19, 27, 51, 59,
      85, 93, 117, 125, 21, 29, 53, 61, 87, 95, 119, 127, 23, 31, 55, 63 }
};

static std::uint32_t UnitInBlock(GsPsm psm, int x, int y)
{
    switch (psm)
    {
    case GsPsm::Ct32:
        return static_cast<std::uint32_t>(kColumn32[y & 1][x & 7] + 16 * ((y >> 1) & 3));

    case GsPsm::Ct16:
        return static_cast<std::uint32_t>(kColumn16[y & 1][x & 15] + 32 * ((y >> 1) & 3));

    case GsPsm::T8:
    {
        std::uint32_t unit = kColumn8[y & 3][x & 15];

        if ((y & 4) != 0)
        {
            unit ^= 96U;
        }

        return unit + 128U * static_cast<std::uint32_t>((y >> 3) & 1);
    }

    case GsPsm::T4:
    {
        std::uint32_t unit = kColumn4[y & 3][x & 31];

        if ((y & 4) != 0)
        {
            unit ^= 192U;
        }

        return unit + 256U * static_cast<std::uint32_t>((y >> 3) & 1);
    }
    }

    return 0;
}

// ------------------------------------------------------------------------------------------------
// Formats
// ------------------------------------------------------------------------------------------------

int GsBitsPerPixel(GsPsm psm)
{
    switch (psm)
    {
    case GsPsm::Ct32:
        return 32;

    case GsPsm::Ct16:
        return 16;

    case GsPsm::T8:
        return 8;

    case GsPsm::T4:
        return 4;
    }

    return 0;
}

int GsPageWidth(GsPsm psm)
{
    return psm == GsPsm::Ct32 || psm == GsPsm::Ct16 ? 64 : 128;
}

int GsPageHeight(GsPsm psm)
{
    switch (psm)
    {
    case GsPsm::Ct32:
        return 32;

    case GsPsm::Ct16:
    case GsPsm::T8:
        return 64;

    case GsPsm::T4:
        return 128;
    }

    return 0;
}

int GsBlockWidth(GsPsm psm)
{
    switch (psm)
    {
    case GsPsm::Ct32:
        return 8;

    case GsPsm::Ct16:
    case GsPsm::T8:
        return 16;

    case GsPsm::T4:
        return 32;
    }

    return 0;
}

int GsBlockHeight(GsPsm psm)
{
    return psm == GsPsm::Ct32 || psm == GsPsm::Ct16 ? 8 : 16;
}

std::uint32_t GsBlockNumber(GsPsm psm, int x, int y, std::uint32_t bp, std::uint32_t bw)
{
    const int pageWidth = GsPageWidth(psm);
    const int pageHeight = GsPageHeight(psm);
    const std::uint32_t pagesPerRow = bw * 64U / static_cast<std::uint32_t>(pageWidth);
    const std::uint32_t page = static_cast<std::uint32_t>(y / pageHeight) * pagesPerRow + static_cast<std::uint32_t>(x / pageWidth);
    const int blockX = (x % pageWidth) / GsBlockWidth(psm);
    const int blockY = (y % pageHeight) / GsBlockHeight(psm);
    const bool wide = psm == GsPsm::Ct32 || psm == GsPsm::T8;
    const std::uint32_t block = wide ? kBlocks32[blockY][blockX] : kBlocks16[blockY][blockX];

    return bp + page * kGsPageBlocks + block;
}

// ------------------------------------------------------------------------------------------------
// GsMemory
// ------------------------------------------------------------------------------------------------

GsMemory::GsMemory(std::uint32_t blocks)
    : m_bytes(static_cast<std::size_t>(blocks) * kGsBlockBytes, 0)
{
}

std::uint32_t GsMemory::Blocks() const
{
    return static_cast<std::uint32_t>(m_bytes.size() / kGsBlockBytes);
}

std::span<const std::uint8_t> GsMemory::Bytes() const
{
    return m_bytes;
}

bool GsMemory::Locate(GsPsm psm, std::uint32_t bp, std::uint32_t bw, int x, int y, std::size_t & unit) const
{
    if (x < 0 || y < 0 || bw == 0)
    {
        return false;
    }

    const std::uint32_t block = GsBlockNumber(psm, x, y, bp, bw);

    if (block >= Blocks())
    {
        return false;
    }

    const std::size_t unitsPerBlock = kGsBlockBytes * 8U / static_cast<std::size_t>(GsBitsPerPixel(psm));
    unit = block * unitsPerBlock + UnitInBlock(psm, x, y);
    return true;
}

bool GsMemory::Write(GsPsm psm, std::uint32_t bp, std::uint32_t bw, int x, int y, std::uint32_t value)
{
    std::size_t unit = 0;

    if (!Locate(psm, bp, bw, x, y, unit))
    {
        return false;
    }

    switch (psm)
    {
    case GsPsm::Ct32:
        for (std::size_t index = 0; index < 4; ++index)
        {
            m_bytes[unit * 4 + index] = static_cast<std::uint8_t>((value >> (8 * index)) & 0xFFU);
        }

        break;

    case GsPsm::Ct16:
        m_bytes[unit * 2] = static_cast<std::uint8_t>(value & 0xFFU);
        m_bytes[unit * 2 + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFU);
        break;

    case GsPsm::T8:
        m_bytes[unit] = static_cast<std::uint8_t>(value & 0xFFU);
        break;

    case GsPsm::T4:
    {
        std::uint8_t & byte = m_bytes[unit / 2];
        const std::uint8_t nibble = static_cast<std::uint8_t>(value & 0x0FU);
        byte = (unit & 1U) != 0 ? static_cast<std::uint8_t>((byte & 0x0FU) | (static_cast<std::uint32_t>(nibble) << 4)) : static_cast<std::uint8_t>((byte & 0xF0U) | static_cast<std::uint32_t>(nibble));
        break;
    }
    }

    return true;
}

bool GsMemory::Read(GsPsm psm, std::uint32_t bp, std::uint32_t bw, int x, int y, std::uint32_t & value) const
{
    std::size_t unit = 0;

    if (!Locate(psm, bp, bw, x, y, unit))
    {
        return false;
    }

    switch (psm)
    {
    case GsPsm::Ct32:
        value = static_cast<std::uint32_t>(m_bytes[unit * 4]) | (static_cast<std::uint32_t>(m_bytes[unit * 4 + 1]) << 8) |
                (static_cast<std::uint32_t>(m_bytes[unit * 4 + 2]) << 16) | (static_cast<std::uint32_t>(m_bytes[unit * 4 + 3]) << 24);
        break;

    case GsPsm::Ct16:
        value = static_cast<std::uint32_t>(m_bytes[unit * 2]) | (static_cast<std::uint32_t>(m_bytes[unit * 2 + 1]) << 8);
        break;

    case GsPsm::T8:
        value = m_bytes[unit];
        break;

    case GsPsm::T4:
        value = (unit & 1U) != 0 ? static_cast<std::uint32_t>(m_bytes[unit / 2] >> 4) : static_cast<std::uint32_t>(m_bytes[unit / 2] & 0x0FU);
        break;
    }

    return true;
}

} // namespace assets
