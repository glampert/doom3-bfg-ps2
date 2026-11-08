// ================================================================================================
// File: texture_pack.cpp
// Brief: Cook a level's world textures into GSTX v2 images (power-of-two levels, box-filtered mips,
// PSMT4 where its palette is good enough, PSMT8 otherwise) and pack them into D3TX v1, the map's
// texture package, indexed by the D3WD's material order.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/texture_pack.h"

#include "assets/bytes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace assets
{
namespace
{

static int FloorPowerOfTwo(int value)
{
    int power = 1;

    while (power * 2 <= value)
    {
        power *= 2;
    }

    return power;
}

static std::size_t Align(std::size_t value, std::size_t alignment)
{
    return (value + alignment - 1U) / alignment * alignment;
}

// The GS stores alpha 0..0x80; the palette keeps straight 8-bit values that land on those levels.
static std::uint32_t PaletteWord(const PaletteColor & color)
{
    const std::uint32_t alpha = (static_cast<std::uint32_t>(color.a) * 128U + 127U) / 255U;
    return static_cast<std::uint32_t>(color.r) | (static_cast<std::uint32_t>(color.g) << 8) | (static_cast<std::uint32_t>(color.b) << 16) |
           (alpha << 24);
}

// Maps every level and folds the per-level statistics into one.
static QuantizeStats MapLevels(const std::vector<RgbaImage> & levels, const std::vector<PaletteColor> & palette,
                               std::vector<std::vector<std::uint8_t> > & indices)
{
    QuantizeStats total;
    double squared = 0.0;
    double over8 = 0.0;
    indices.assign(levels.size(), {});

    for (std::size_t level = 0; level < levels.size(); ++level)
    {
        const QuantizeStats stats = MapToPalette(levels[level], palette, indices[level]);
        squared += stats.rmse * stats.rmse * static_cast<double>(stats.texels);
        over8 += stats.over8 * static_cast<double>(stats.texels);
        total.texels += stats.texels;
        total.maxError = std::max(total.maxError, stats.maxError);
    }

    total.rmse = total.texels == 0 ? 0.0 : std::sqrt(squared / static_cast<double>(total.texels));
    total.over8 = total.texels == 0 ? 0.0 : over8 / static_cast<double>(total.texels);
    return total;
}

static bool Fail(std::string & error, const std::string & message)
{
    error = message;
    return false;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Cooking
// ------------------------------------------------------------------------------------------------

bool CookTexture(const RgbaImage & source, const TexturePolicy & policy, GstxUsage usage, CookedTexture & output, std::string & error)
{
    if (source.width < 1 || source.height < 1 || policy.scale <= 0.0 || policy.scale > 1.0 || policy.minExtent < 1 ||
        policy.maxExtent < policy.minExtent || policy.maxExtent > kGstxV2MaxExtent || policy.minMipExtent < 1)
    {
        return Fail(error, "invalid texture or policy");
    }

    const auto extent = [&policy](int side)
    {
        const int scaled = FloorPowerOfTwo(std::max(1, static_cast<int>(side * policy.scale)));
        const int floor = std::min(FloorPowerOfTwo(side), policy.minExtent);
        return std::min(policy.maxExtent, std::max(scaled, floor));
    };
    const int width = extent(source.width);
    const int height = extent(source.height);
    int levelCount = 1;

    while (levelCount < kGstxV2MaxLevels && (std::min(width, height) >> levelCount) >= policy.minMipExtent)
    {
        ++levelCount;
    }

    // Every level straight from the source, so no level inherits another's rounding.
    std::vector<RgbaImage> levels;
    ColorHistogram histogram;

    for (int level = 0; level < levelCount; ++level)
    {
        levels.push_back(ResampleArea(source, std::max(1, width >> level), std::max(1, height >> level)));
        histogram.Add(levels.back());
    }

    std::vector<std::vector<std::uint8_t> > indices;
    std::vector<PaletteColor> palette = BuildPalette(histogram, 16);
    QuantizeStats quality = MapLevels(levels, palette, indices);
    GsPsm psm = GsPsm::T4;

    if (quality.rmse > policy.t4MaxRmse || quality.maxError > policy.t4MaxError)
    {
        palette = BuildPalette(histogram, 256);
        quality = MapLevels(levels, palette, indices);
        psm = GsPsm::T8;
    }

    CookedTexture cooked;
    cooked.sourceWidth = source.width;
    cooked.sourceHeight = source.height;
    cooked.quality = quality;
    cooked.image.width = width;
    cooked.image.height = height;
    cooked.image.psm = psm;
    cooked.image.usage = usage;

    for (const std::vector<std::uint8_t> & level : indices)
    {
        cooked.image.levels.emplace_back(level.begin(), level.end());
    }

    cooked.image.palette.assign(psm == GsPsm::T8 ? 256U : 16U, 0U);

    for (std::size_t entry = 0; entry < palette.size(); ++entry)
    {
        cooked.image.palette[entry] = PaletteWord(palette[entry]);
    }

    output = std::move(cooked);
    return true;
}

std::uint32_t MaterialNamesHash(const std::vector<std::string> & materials)
{
    std::uint32_t hash = 2166136261U;

    for (const std::string & name : materials)
    {
        hash = Fnv1a32({ reinterpret_cast<const std::uint8_t *>(name.data()), name.size() }, hash);
        const std::uint8_t terminator = 0;
        hash = Fnv1a32({ &terminator, 1 }, hash);
    }

    return hash;
}

// ------------------------------------------------------------------------------------------------
// D3TX
// ------------------------------------------------------------------------------------------------

static std::size_t TablesEnd(std::size_t materials, std::size_t textures)
{
    return kD3txHeaderBytes + 4U * materials + kD3txTextureRecordBytes * textures;
}

bool WriteD3tx(const TexturePack & pack, std::vector<std::uint8_t> & output, std::string & error)
{
    if (pack.materialTextures.size() > kD3txMaxMaterials || pack.textures.size() > kD3txMaxTextures ||
        pack.textureFlags.size() != pack.textures.size())
    {
        return Fail(error, "D3TX table sizes out of range");
    }

    for (const std::uint32_t texture : pack.materialTextures)
    {
        if (texture != kD3txNoTexture && texture >= pack.textures.size())
        {
            return Fail(error, "D3TX material names a missing texture");
        }
    }

    for (std::size_t index = 0; index < pack.textures.size(); ++index)
    {
        GstxV2Texture texture;

        if ((pack.textureFlags[index] & ~kD3txFlagAlphaTest) != 0 || !ReadGstxV2(pack.textures[index], texture, error))
        {
            return Fail(error, "D3TX texture " + std::to_string(index) + ": " + (error.empty() ? "unknown flags" : error));
        }
    }

    std::size_t offset = Align(TablesEnd(pack.materialTextures.size(), pack.textures.size()), kD3txAlignment);
    std::vector<std::size_t> offsets;

    for (const auto & texture : pack.textures)
    {
        offsets.push_back(offset);
        offset = Align(offset + texture.size(), kD3txAlignment);
    }

    if (offset > 0xFFFFFFFFU)
    {
        return Fail(error, "D3TX exceeds 4 GiB");
    }

    output.assign(offset, 0);
    std::uint8_t * const bytes = output.data();
    WriteLe32(bytes, kD3txMagic);
    WriteLe16(bytes + 4, kD3txVersion);
    WriteLe16(bytes + 6, static_cast<std::uint16_t>(kD3txHeaderBytes));
    WriteLe32(bytes + 8, static_cast<std::uint32_t>(pack.materialTextures.size()));
    WriteLe32(bytes + 12, static_cast<std::uint32_t>(pack.textures.size()));
    WriteLe32(bytes + 16, pack.materialsHash);
    WriteLe32(bytes + 20, static_cast<std::uint32_t>(output.size()));
    WriteLe32(bytes + 28, kD3txConverterVersion);
    std::size_t at = kD3txHeaderBytes;

    for (const std::uint32_t texture : pack.materialTextures)
    {
        WriteLe32(bytes + at, texture);
        at += 4;
    }

    for (std::size_t index = 0; index < pack.textures.size(); ++index)
    {
        WriteLe32(bytes + at, static_cast<std::uint32_t>(offsets[index]));
        WriteLe32(bytes + at + 4, static_cast<std::uint32_t>(pack.textures[index].size()));
        WriteLe32(bytes + at + 8, pack.textureFlags[index]);
        at += kD3txTextureRecordBytes;
        std::memcpy(bytes + offsets[index], pack.textures[index].data(), pack.textures[index].size());
    }

    WriteLe32(bytes + 24, Fnv1a32({ bytes, at }));
    return true;
}

bool ReadD3tx(std::span<const std::uint8_t> bytes, TexturePack & output, std::string & error)
{
    if (bytes.size() < kD3txHeaderBytes || ReadLe32(bytes.data()) != kD3txMagic || ReadLe16(bytes.data() + 4) != kD3txVersion ||
        ReadLe16(bytes.data() + 6) != kD3txHeaderBytes || ReadLe32(bytes.data() + 28) != kD3txConverterVersion)
    {
        return Fail(error, "not a D3TX v1 file");
    }

    const std::uint32_t materials = ReadLe32(bytes.data() + 8);
    const std::uint32_t textures = ReadLe32(bytes.data() + 12);

    if (materials > kD3txMaxMaterials || textures > kD3txMaxTextures || ReadLe32(bytes.data() + 20) != bytes.size() ||
        TablesEnd(materials, textures) > bytes.size())
    {
        return Fail(error, "D3TX sizes out of range");
    }

    const std::size_t tablesEnd = TablesEnd(materials, textures);
    std::vector<std::uint8_t> header(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(tablesEnd));
    WriteLe32(header.data() + 24, 0);

    if (Fnv1a32(header) != ReadLe32(bytes.data() + 24))
    {
        return Fail(error, "D3TX table checksum mismatch");
    }

    TexturePack pack;
    pack.materialsHash = ReadLe32(bytes.data() + 16);

    for (std::uint32_t index = 0; index < materials; ++index)
    {
        pack.materialTextures.push_back(ReadLe32(bytes.data() + kD3txHeaderBytes + 4U * index));
    }

    for (std::uint32_t index = 0; index < textures; ++index)
    {
        const std::uint8_t * record = bytes.data() + kD3txHeaderBytes + 4U * materials + kD3txTextureRecordBytes * index;
        const std::uint32_t offset = ReadLe32(record);
        const std::uint32_t size = ReadLe32(record + 4);

        if (offset % kD3txAlignment != 0 || offset < tablesEnd || size > bytes.size() || offset > bytes.size() - size || ReadLe32(record + 12) != 0)
        {
            return Fail(error, "D3TX texture " + std::to_string(index) + " lies outside the file");
        }

        pack.textures.emplace_back(bytes.begin() + offset, bytes.begin() + offset + size);
        pack.textureFlags.push_back(ReadLe32(record + 8));
    }

    // Canonical: the writer rebuilds exactly these bytes, padding and order included.
    std::vector<std::uint8_t> encoded;

    if (!WriteD3tx(pack, encoded, error))
    {
        return false;
    }

    if (encoded.size() != bytes.size() || std::memcmp(encoded.data(), bytes.data(), bytes.size()) != 0)
    {
        return Fail(error, "D3TX file is not canonical");
    }

    output = std::move(pack);
    return true;
}

} // namespace assets
