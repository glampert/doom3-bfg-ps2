// ================================================================================================
// File: gs_texture.cpp
// Brief: Lay out paletted texture data exactly as the GS consumes it: PSMT8/PSMT4 index rows and
// CSM1 PSMCT32 CLUT texels, so the target uploads converted files without reordering them.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/gs_texture.h"

namespace assets
{

int IndexBits(IndexFormat format)
{
    return format == IndexFormat::T8 ? 8 : 4;
}

int PaletteSize(IndexFormat format)
{
    return format == IndexFormat::T8 ? 256 : 16;
}

int GsPageWidth(IndexFormat)
{
    return 128;
}

int GsPageHeight(IndexFormat format)
{
    return format == IndexFormat::T8 ? 64 : 128;
}

std::size_t IndexBytes(IndexFormat format, int width, int height)
{
    const std::size_t texels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    return format == IndexFormat::T8 ? texels : (texels + 1) / 2;
}

std::vector<std::uint8_t> PackIndices(std::span<const std::uint8_t> indices, int width, int height, IndexFormat format)
{
    if (format == IndexFormat::T8)
    {
        return { indices.begin(), indices.end() };
    }

    std::vector<std::uint8_t> packed(IndexBytes(format, width, height), 0);

    for (std::size_t texel = 0; texel < indices.size(); ++texel)
    {
        const std::uint8_t value = static_cast<std::uint8_t>(indices[texel] & 0x0FU);
        packed[texel / 2] = static_cast<std::uint8_t>(packed[texel / 2] | ((texel & 1U) != 0 ? value << 4 : value));
    }

    return packed;
}

std::vector<std::uint8_t> UnpackIndices(std::span<const std::uint8_t> packed, int width, int height, IndexFormat format)
{
    const std::size_t texels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    if (format == IndexFormat::T8)
    {
        return { packed.begin(), packed.begin() + static_cast<std::ptrdiff_t>(texels) };
    }

    std::vector<std::uint8_t> indices(texels);

    for (std::size_t texel = 0; texel < texels; ++texel)
    {
        const std::uint8_t byte = packed[texel / 2];
        indices[texel] = static_cast<std::uint8_t>((texel & 1U) != 0 ? byte >> 4 : byte & 0x0FU);
    }

    return indices;
}

std::uint32_t ClutSwizzle(std::uint32_t index)
{
    return (index & ~0x18U) | ((index & 0x08U) << 1) | ((index & 0x10U) >> 1);
}

std::vector<PaletteColor> ClutTexels(const std::vector<PaletteColor> & palette, IndexFormat format)
{
    const std::size_t entries = static_cast<std::size_t>(PaletteSize(format));
    std::vector<PaletteColor> texels;
    texels.reserve(entries);

    // The CSM1 swizzle is its own inverse, so each texel reads the entry that lands on it.
    for (std::size_t position = 0; position < entries; ++position)
    {
        const std::size_t index = format == IndexFormat::T8 ? ClutSwizzle(static_cast<std::uint32_t>(position)) : position;
        texels.push_back(index < palette.size() ? palette[index] : PaletteColor{});
    }

    return texels;
}

std::vector<PaletteColor> PaletteFromClut(const std::vector<PaletteColor> & texels, IndexFormat format)
{
    std::vector<PaletteColor> palette(texels.size());

    for (std::size_t index = 0; index < texels.size(); ++index)
    {
        const std::size_t position = format == IndexFormat::T8 ? ClutSwizzle(static_cast<std::uint32_t>(index)) : index;
        palette[index] = texels[position];
    }

    return palette;
}

} // namespace assets
