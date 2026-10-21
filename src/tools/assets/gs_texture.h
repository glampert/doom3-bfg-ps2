// ================================================================================================
// File: gs_texture.h
// Brief: Lay out paletted texture data exactly as the GS consumes it: PSMT8/PSMT4 index rows and
// CSM1 PSMCT32 CLUT texels, so the target uploads converted files without reordering them.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "assets/palette.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace assets
{

// Values are the GS PSM codes the target writes into TEX0.
enum class IndexFormat : std::uint8_t
{
    T8 = 0x13,
    T4 = 0x14
};

int IndexBits(IndexFormat format);
int PaletteSize(IndexFormat format);
// GS memory pages: PSMT8 covers 128x64 texels, PSMT4 128x128 (8 KiB either way). TBW counts
// 64-texel units and must be even for both, so page widths are whole multiples of 128.
int GsPageWidth(IndexFormat format);
int GsPageHeight(IndexFormat format);
std::size_t IndexBytes(IndexFormat format, int width, int height);

// One index per input byte, row-major. PSMT4 stores the even texel in the low nibble.
std::vector<std::uint8_t> PackIndices(std::span<const std::uint8_t> indices, int width, int height, IndexFormat format);
std::vector<std::uint8_t> UnpackIndices(std::span<const std::uint8_t> packed, int width, int height, IndexFormat format);

// CSM1 swaps the two middle 8-entry blocks of every 32 entries of a 256-entry CLUT; the mapping
// is its own inverse.
std::uint32_t ClutSwizzle(std::uint32_t index);

// The CLUT image the GS reads: 256 entries as a 16x16 tile in swizzled order, 16 entries as an 8x2
// tile. Missing palette entries are transparent black.
std::vector<PaletteColor> ClutTexels(const std::vector<PaletteColor> & palette, IndexFormat format);

// The palette entry behind each CLUT texel, undoing ClutTexels.
std::vector<PaletteColor> PaletteFromClut(const std::vector<PaletteColor> & texels, IndexFormat format);

} // namespace assets
