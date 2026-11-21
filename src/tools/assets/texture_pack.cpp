// ================================================================================================
// File: texture_pack.cpp
// Brief: Cook a level's world textures into GSTX v2 images (power-of-two levels, box-filtered mips,
// PSMT4 where its palette is good enough, PSMT8 otherwise), bake bump maps into them as cavity
// shading, and pack them with the materials' passes into D3TX v3, indexed by the D3WD's material
// order.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/texture_pack.h"

#include "assets/bytes.h"
#include "assets/material.h"

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
    bool fitsT4 = quality.rmse <= policy.t4MaxRmse && quality.maxError <= policy.t4MaxError;

    if (policy.formatSource != nullptr)
    {
        if (policy.formatSource->width != source.width || policy.formatSource->height != source.height)
        {
            return Fail(error, "the format source is not the source's size");
        }

        std::vector<RgbaImage> reference;
        ColorHistogram referenceHistogram;

        for (int level = 0; level < levelCount; ++level)
        {
            reference.push_back(ResampleArea(*policy.formatSource, std::max(1, width >> level), std::max(1, height >> level)));
            referenceHistogram.Add(reference.back());
        }

        std::vector<std::vector<std::uint8_t> > referenceIndices;
        const QuantizeStats referenceQuality = MapLevels(reference, BuildPalette(referenceHistogram, 16), referenceIndices);
        fitsT4 = referenceQuality.rmse <= policy.t4MaxRmse && referenceQuality.maxError <= policy.t4MaxError;
    }

    if (!fitsT4)
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

namespace
{

struct D3txCounts
{
    std::size_t materials = 0;
    std::size_t passes = 0;
    std::size_t textures = 0;
    std::size_t pages = 0;
    std::size_t nodes = 0;
    std::size_t values = 0;
};

static std::size_t PassesAt(const D3txCounts & counts)
{
    return kD3txHeaderBytes + kD3txMaterialRecordBytes * counts.materials;
}

static std::size_t TexturesAt(const D3txCounts & counts)
{
    return PassesAt(counts) + kD3txPassRecordBytes * counts.passes;
}

static std::size_t PagesAt(const D3txCounts & counts)
{
    return TexturesAt(counts) + kD3txTextureRecordBytes * counts.textures;
}

static std::size_t NodesAt(const D3txCounts & counts)
{
    return PagesAt(counts) + kD3txPageRecordBytes * counts.pages;
}

static std::size_t ValuesAt(const D3txCounts & counts)
{
    return NodesAt(counts) + kD3txNodeRecordBytes * counts.nodes;
}

static std::size_t TablesEnd(const D3txCounts & counts)
{
    return ValuesAt(counts) + kD3txValueRecordBytes * counts.values;
}

static std::uint32_t FloatBits(float value)
{
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(float));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float BitsFloat(std::uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// The register ops a cooked node may hold: every input but time is folded in by the cook.
static bool CookedOp(std::uint8_t op)
{
    const auto value = static_cast<ExpressionOp>(op);
    return op <= static_cast<std::uint8_t>(ExpressionOp::Or) && value != ExpressionOp::Parm && value != ExpressionOp::Global &&
           value != ExpressionOp::Sound;
}

static bool ValidPass(const D3txPass & pass, const TexturePack & pack, std::string & reason)
{
    if (pass.texture >= pack.textures.size() || (pack.textureFlags[pass.texture] & kD3txFlagLightmap) != 0)
    {
        reason = "names a missing texture or a lightmap";
        return false;
    }

    if (pass.blend != kD3txBlendAdd || pass.nodeCount > kD3txMaxNodesPerPass ||
        static_cast<std::size_t>(pass.firstNode) + pass.nodeCount > pack.nodes.size())
    {
        reason = "has a bad blend or node range";
        return false;
    }

    const auto inRange = [&pass](std::uint16_t node)
    {
        return node == kD3txNoNode || (node >= pass.firstNode && node < pass.firstNode + pass.nodeCount);
    };

    bool valid = inRange(pass.condition);

    for (const std::uint16_t node : pass.colorNodes)
    {
        valid = valid && inRange(node);
    }

    for (std::size_t index = pass.firstNode; valid && index < static_cast<std::size_t>(pass.firstNode) + pass.nodeCount; ++index)
    {
        const D3txNode & node = pack.nodes[index];
        const auto op = static_cast<ExpressionOp>(node.op);
        const bool leaf = op == ExpressionOp::Constant || op == ExpressionOp::Time;
        valid = CookedOp(node.op) && std::isfinite(node.value);

        if (op == ExpressionOp::Table)
        {
            valid = valid && node.a >= pass.firstNode && node.a < index && (node.flags & ~(kD3txTableSnap | kD3txTableClamp)) == 0 &&
                    node.count >= 2 && static_cast<std::size_t>(node.b) + node.count <= pack.tableValues.size();
        }
        else if (leaf)
        {
            valid = valid && node.a == 0 && node.b == 0 && node.count == 0 && node.flags == 0;
        }
        else
        {
            valid = valid && node.a >= pass.firstNode && node.a < index && node.b >= pass.firstNode && node.b < index && node.count == 0 &&
                    node.flags == 0;
        }

        valid = valid && (op == ExpressionOp::Constant || node.value == 0.0f);
    }

    if (!valid)
    {
        reason = "holds a bad expression";
    }

    return valid;
}

} // namespace

void BakeCavity(RgbaImage & diffuse, const RgbaImage & normal, const CavityPolicy & policy)
{
    if (policy.amplitude == 0.0f || normal.width <= 0 || normal.height <= 0)
    {
        return;
    }

    const float length = std::sqrt(policy.light[0] * policy.light[0] + policy.light[1] * policy.light[1] + policy.light[2] * policy.light[2]);
    const std::array<float, 3> light = { policy.light[0] / length, policy.light[1] / length, policy.light[2] / length };

    for (int y = 0; y < diffuse.height; ++y)
    {
        for (int x = 0; x < diffuse.width; ++x)
        {
            // The normal map's texel centres at the diffuse texel's centre, bilinear and wrapping.
            const float u = (static_cast<float>(x) + 0.5f) * static_cast<float>(normal.width) / static_cast<float>(diffuse.width) - 0.5f;
            const float v = (static_cast<float>(y) + 0.5f) * static_cast<float>(normal.height) / static_cast<float>(diffuse.height) - 0.5f;
            const int x0 = static_cast<int>(std::floor(u));
            const int y0 = static_cast<int>(std::floor(v));
            const float fu = u - static_cast<float>(x0);
            const float fv = v - static_cast<float>(y0);
            std::array<float, 3> n = {};

            for (int corner = 0; corner < 4; ++corner)
            {
                const int cx = ((x0 + (corner & 1)) % normal.width + normal.width) % normal.width;
                const int cy = ((y0 + (corner >> 1)) % normal.height + normal.height) % normal.height;
                const float weight = ((corner & 1) != 0 ? fu : 1.0f - fu) * ((corner >> 1) != 0 ? fv : 1.0f - fv);
                const std::uint8_t * texel = normal.At(cx, cy);

                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    n[axis] += weight * (static_cast<float>(texel[axis]) / 127.5f - 1.0f);
                }
            }

            const float shade = std::clamp((n[0] * light[0] + n[1] * light[1] + n[2] * light[2]) / light[2], 0.5f, 1.5f);
            const float cavity = 1.0f + policy.amplitude * (shade - 1.0f);
            std::uint8_t * texel = diffuse.At(x, y);

            for (int channel = 0; channel < 3; ++channel)
            {
                texel[channel] = static_cast<std::uint8_t>(std::clamp(std::round(static_cast<float>(texel[channel]) * cavity), 0.0f, 255.0f));
            }
        }
    }
}

std::uint32_t MaterialNamesHash(const std::vector<std::string> & materials)
{
    std::uint32_t hash = kFnv1a32Basis;

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

bool CookLightmapPage(int size, const std::vector<std::uint8_t> & texels, GstxV2Image & output, std::string & error)
{
    if (size < 8 || size > kGstxV2MaxExtent || (size & (size - 1)) != 0 || texels.size() != static_cast<std::size_t>(size) * static_cast<std::size_t>(size))
    {
        error = "bad lightmap page size";
        return false;
    }

    GstxV2Image image;
    image.width = size;
    image.height = size;
    image.psm = GsPsm::T8;
    image.usage = GstxUsage::Lightmap;
    image.wrapS = GstxWrap::Clamp;
    image.wrapT = GstxWrap::Clamp;
    image.levels.emplace_back(texels.begin(), texels.end());

    for (std::uint32_t entry = 0; entry < 256; ++entry)
    {
        const std::uint32_t grey = std::min(255U, entry);
        image.palette.push_back(grey | (grey << 8) | (grey << 16) | (entry << 24));
    }

    output = std::move(image);
    return true;
}

bool WriteD3tx(const TexturePack & pack, std::vector<std::uint8_t> & output, std::string & error)
{
    const std::size_t materials = pack.materialTextures.size();

    if (materials > kD3txMaxMaterials || pack.textures.size() > kD3txMaxTextures || pack.textureFlags.size() != pack.textures.size() ||
        pack.materialUvDensity.size() != materials || pack.materialAlphaRef.size() != materials || pack.materialFirstPass.size() != materials ||
        pack.materialPassCount.size() != materials || pack.lightmapPages.size() > kD3txMaxPages || pack.passes.size() > kD3txMaxPasses ||
        pack.nodes.size() > kD3txMaxNodes || pack.tableValues.size() > kD3txMaxValues)
    {
        return Fail(error, "D3TX table sizes out of range");
    }

    for (std::size_t index = 0; index < materials; ++index)
    {
        const float density = pack.materialUvDensity[index];
        const std::uint32_t texture = pack.materialTextures[index];
        const std::size_t passEnd = static_cast<std::size_t>(pack.materialFirstPass[index]) + pack.materialPassCount[index];

        if (!std::isfinite(density) || density < -64.0f || density > 64.0f)
        {
            return Fail(error, "D3TX material density out of range");
        }

        if (texture != kD3txNoTexture && texture != kD3txHidden &&
            (texture >= pack.textures.size() || (pack.textureFlags[texture] & kD3txFlagLightmap) != 0))
        {
            return Fail(error, "D3TX material names a missing texture or a lightmap");
        }

        // Passes draw over a textured base pass, which a material without a texture lacks.
        if (pack.materialAlphaRef[index] > kD3txMaxAlphaRef || pack.materialPassCount[index] > kD3txMaxPassesPerMaterial ||
            passEnd > pack.passes.size() || (pack.materialPassCount[index] != 0 && texture >= kD3txHidden))
        {
            return Fail(error, "D3TX material " + std::to_string(index) + " has a bad alpha reference or pass range");
        }
    }

    for (const std::uint32_t texture : pack.lightmapPages)
    {
        if (texture >= pack.textures.size() || (pack.textureFlags[texture] & kD3txFlagLightmap) == 0)
        {
            return Fail(error, "D3TX page names a missing texture or one not a lightmap");
        }
    }

    for (std::size_t index = 0; index < pack.passes.size(); ++index)
    {
        std::string reason;

        if (!ValidPass(pack.passes[index], pack, reason))
        {
            return Fail(error, "D3TX pass " + std::to_string(index) + " " + reason);
        }
    }

    for (const float value : pack.tableValues)
    {
        if (!std::isfinite(value))
        {
            return Fail(error, "D3TX table value is not finite");
        }
    }

    for (std::size_t index = 0; index < pack.textures.size(); ++index)
    {
        GstxV2Texture texture;
        const std::uint32_t flags = pack.textureFlags[index];
        error.clear();

        if ((flags & ~(kD3txFlagAlphaTest | kD3txFlagLightmap)) != 0 || !ReadGstxV2(pack.textures[index], texture, error) ||
            ((flags & kD3txFlagLightmap) != 0) != (texture.usage == GstxUsage::Lightmap) || flags == (kD3txFlagAlphaTest | kD3txFlagLightmap))
        {
            return Fail(error, "D3TX texture " + std::to_string(index) + ": " + (error.empty() ? "flags disagree with the texture" : error));
        }
    }

    D3txCounts counts;
    counts.materials = materials;
    counts.passes = pack.passes.size();
    counts.textures = pack.textures.size();
    counts.pages = pack.lightmapPages.size();
    counts.nodes = pack.nodes.size();
    counts.values = pack.tableValues.size();
    std::size_t offset = Align(TablesEnd(counts), kD3txAlignment);
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
    WriteLe32(bytes + 8, static_cast<std::uint32_t>(counts.materials));
    WriteLe32(bytes + 12, static_cast<std::uint32_t>(counts.textures));
    WriteLe32(bytes + 16, pack.materialsHash);
    WriteLe32(bytes + 20, static_cast<std::uint32_t>(output.size()));
    WriteLe32(bytes + 28, kD3txConverterVersion);
    WriteLe32(bytes + 32, static_cast<std::uint32_t>(counts.pages));
    WriteLe32(bytes + 36, static_cast<std::uint32_t>(counts.passes));
    WriteLe32(bytes + 40, static_cast<std::uint32_t>(counts.nodes));
    WriteLe32(bytes + 44, static_cast<std::uint32_t>(counts.values));

    for (std::size_t index = 0; index < materials; ++index)
    {
        std::uint8_t * record = bytes + kD3txHeaderBytes + kD3txMaterialRecordBytes * index;
        WriteLe32(record, pack.materialTextures[index]);
        WriteLe32(record + 4, FloatBits(pack.materialUvDensity[index]));
        WriteLe16(record + 8, pack.materialFirstPass[index]);
        record[10] = pack.materialPassCount[index];
        record[11] = pack.materialAlphaRef[index];
    }

    for (std::size_t index = 0; index < pack.passes.size(); ++index)
    {
        const D3txPass & pass = pack.passes[index];
        std::uint8_t * record = bytes + PassesAt(counts) + kD3txPassRecordBytes * index;
        WriteLe32(record, pass.texture);
        std::memcpy(record + 4, pass.color.data(), 4);
        record[8] = pass.blend;
        WriteLe16(record + 10, pass.firstNode);
        WriteLe16(record + 12, pass.nodeCount);
        WriteLe16(record + 14, pass.condition);

        for (std::size_t channel = 0; channel < 4; ++channel)
        {
            WriteLe16(record + 16 + 2 * channel, pass.colorNodes[channel]);
        }
    }

    for (std::size_t index = 0; index < pack.textures.size(); ++index)
    {
        std::uint8_t * record = bytes + TexturesAt(counts) + kD3txTextureRecordBytes * index;
        WriteLe32(record, static_cast<std::uint32_t>(offsets[index]));
        WriteLe32(record + 4, static_cast<std::uint32_t>(pack.textures[index].size()));
        WriteLe32(record + 8, pack.textureFlags[index]);
        std::memcpy(bytes + offsets[index], pack.textures[index].data(), pack.textures[index].size());
    }

    for (std::size_t index = 0; index < pack.lightmapPages.size(); ++index)
    {
        WriteLe32(bytes + PagesAt(counts) + kD3txPageRecordBytes * index, pack.lightmapPages[index]);
    }

    for (std::size_t index = 0; index < pack.nodes.size(); ++index)
    {
        const D3txNode & node = pack.nodes[index];
        std::uint8_t * record = bytes + NodesAt(counts) + kD3txNodeRecordBytes * index;
        record[0] = node.op;
        record[1] = node.flags;
        WriteLe16(record + 2, node.a);
        WriteLe16(record + 4, node.b);
        WriteLe16(record + 6, node.count);
        WriteLe32(record + 8, FloatBits(node.value));
    }

    for (std::size_t index = 0; index < pack.tableValues.size(); ++index)
    {
        WriteLe32(bytes + ValuesAt(counts) + kD3txValueRecordBytes * index, FloatBits(pack.tableValues[index]));
    }

    WriteLe32(bytes + 24, Fnv1a32({ bytes, TablesEnd(counts) }));
    return true;
}

bool ReadD3tx(std::span<const std::uint8_t> bytes, TexturePack & output, std::string & error)
{
    if (bytes.size() < kD3txHeaderBytes || ReadLe32(bytes.data()) != kD3txMagic || ReadLe16(bytes.data() + 4) != kD3txVersion ||
        ReadLe16(bytes.data() + 6) != kD3txHeaderBytes || ReadLe32(bytes.data() + 28) != kD3txConverterVersion)
    {
        return Fail(error, "not a D3TX v3 file");
    }

    D3txCounts counts;
    counts.materials = ReadLe32(bytes.data() + 8);
    counts.textures = ReadLe32(bytes.data() + 12);
    counts.pages = ReadLe32(bytes.data() + 32);
    counts.passes = ReadLe32(bytes.data() + 36);
    counts.nodes = ReadLe32(bytes.data() + 40);
    counts.values = ReadLe32(bytes.data() + 44);

    if (counts.materials > kD3txMaxMaterials || counts.textures > kD3txMaxTextures || counts.pages > kD3txMaxPages ||
        counts.passes > kD3txMaxPasses || counts.nodes > kD3txMaxNodes || counts.values > kD3txMaxValues ||
        ReadLe32(bytes.data() + 20) != bytes.size() || TablesEnd(counts) > bytes.size())
    {
        return Fail(error, "D3TX sizes out of range");
    }

    const std::size_t tablesEnd = TablesEnd(counts);
    std::vector<std::uint8_t> header(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(tablesEnd));
    WriteLe32(header.data() + 24, 0);

    if (Fnv1a32(header) != ReadLe32(bytes.data() + 24))
    {
        return Fail(error, "D3TX table checksum mismatch");
    }

    TexturePack pack;
    pack.materialsHash = ReadLe32(bytes.data() + 16);

    for (std::size_t index = 0; index < counts.materials; ++index)
    {
        const std::uint8_t * record = bytes.data() + kD3txHeaderBytes + kD3txMaterialRecordBytes * index;
        const float density = BitsFloat(ReadLe32(record + 4));

        if (!std::isfinite(density) || density < -64.0f || density > 64.0f)
        {
            return Fail(error, "D3TX material density out of range");
        }

        pack.materialTextures.push_back(ReadLe32(record));
        pack.materialUvDensity.push_back(density);
        pack.materialFirstPass.push_back(ReadLe16(record + 8));
        pack.materialPassCount.push_back(record[10]);
        pack.materialAlphaRef.push_back(record[11]);
    }

    for (std::size_t index = 0; index < counts.passes; ++index)
    {
        const std::uint8_t * record = bytes.data() + PassesAt(counts) + kD3txPassRecordBytes * index;
        D3txPass pass;
        pass.texture = ReadLe32(record);
        std::memcpy(pass.color.data(), record + 4, 4);
        pass.blend = record[8];
        pass.firstNode = ReadLe16(record + 10);
        pass.nodeCount = ReadLe16(record + 12);
        pass.condition = ReadLe16(record + 14);

        for (std::size_t channel = 0; channel < 4; ++channel)
        {
            pass.colorNodes[channel] = ReadLe16(record + 16 + 2 * channel);
        }

        pack.passes.push_back(pass);
    }

    for (std::size_t index = 0; index < counts.textures; ++index)
    {
        const std::uint8_t * record = bytes.data() + TexturesAt(counts) + kD3txTextureRecordBytes * index;
        const std::uint32_t offset = ReadLe32(record);
        const std::uint32_t size = ReadLe32(record + 4);

        if (offset % kD3txAlignment != 0 || offset < tablesEnd || size > bytes.size() || offset > bytes.size() - size)
        {
            return Fail(error, "D3TX texture " + std::to_string(index) + " lies outside the file");
        }

        pack.textures.emplace_back(bytes.begin() + offset, bytes.begin() + offset + size);
        pack.textureFlags.push_back(ReadLe32(record + 8));
    }

    for (std::size_t index = 0; index < counts.pages; ++index)
    {
        pack.lightmapPages.push_back(ReadLe32(bytes.data() + PagesAt(counts) + kD3txPageRecordBytes * index));
    }

    for (std::size_t index = 0; index < counts.nodes; ++index)
    {
        const std::uint8_t * record = bytes.data() + NodesAt(counts) + kD3txNodeRecordBytes * index;
        D3txNode node;
        node.op = record[0];
        node.flags = record[1];
        node.a = ReadLe16(record + 2);
        node.b = ReadLe16(record + 4);
        node.count = ReadLe16(record + 6);
        node.value = BitsFloat(ReadLe32(record + 8));
        pack.nodes.push_back(node);
    }

    for (std::size_t index = 0; index < counts.values; ++index)
    {
        pack.tableValues.push_back(BitsFloat(ReadLe32(bytes.data() + ValuesAt(counts) + kD3txValueRecordBytes * index)));
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
