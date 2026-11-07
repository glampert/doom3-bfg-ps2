// ================================================================================================
// File: gs_memory.h
// Brief: Emulate GS local memory addressing (pages, blocks, columns) for the four storage formats
// the cook writes, so a texture can be laid out exactly as the GS will hold it and stored as the
// 32-bit words that recreate it, with no reordering left for the EE.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace assets
{

// GS PSM codes, as TEX0 and BITBLTBUF write them.
enum class GsPsm : std::uint8_t
{
    Ct32 = 0x00,
    Ct16 = 0x02,
    T8 = 0x13,
    T4 = 0x14
};

static constexpr std::uint32_t kGsBlockBytes = 256;
static constexpr std::uint32_t kGsPageBlocks = 32;

int GsBitsPerPixel(GsPsm psm);
// Pixels of one page and of one block of the format.
int GsPageWidth(GsPsm psm);
int GsPageHeight(GsPsm psm);
int GsBlockWidth(GsPsm psm);
int GsBlockHeight(GsPsm psm);

// The block holding pixel (x, y) of a buffer at block 'bp' that is 'bw' x 64 pixels wide (GS manual
// 8.2-8.3). Block numbers grow with x for a fixed y and with y for a fixed x.
std::uint32_t GsBlockNumber(GsPsm psm, int x, int y, std::uint32_t bp, std::uint32_t bw);

// A run of blocks starting at block 0, read and written as the GS addresses them.
class GsMemory final
{
public:
    explicit GsMemory(std::uint32_t blocks);

    std::uint32_t Blocks() const;
    std::span<const std::uint8_t> Bytes() const;

    // False when the pixel's address lies outside the run.
    bool Write(GsPsm psm, std::uint32_t bp, std::uint32_t bw, int x, int y, std::uint32_t value);
    bool Read(GsPsm psm, std::uint32_t bp, std::uint32_t bw, int x, int y, std::uint32_t & value) const;

private:
    bool Locate(GsPsm psm, std::uint32_t bp, std::uint32_t bw, int x, int y, std::size_t & unit) const;

    std::vector<std::uint8_t> m_bytes;
};

} // namespace assets
