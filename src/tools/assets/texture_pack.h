// ================================================================================================
// File: texture_pack.h
// Brief: Cook a level's world textures into GSTX v2 images (power-of-two levels, box-filtered mips,
// PSMT4 where its palette is good enough, PSMT8 otherwise, the bump map baked in as cavity shading)
// and pack them with the map's Light8 lightmap pages, each material's passes and its sky's six faces
// into D3TX v4, the map's texture package, indexed by the D3WD's material order and its batches'
// lightmap pages.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "assets/gstx_v2.h"
#include "assets/image.h"
#include "assets/palette.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace assets
{

// Little-endian. A 52-byte header, the material, pass, texture, lightmap page, expression node,
// table value and sky tables, then the GSTX v2 files, each 64-byte aligned with zero padding (the
// DMA arena's alignment), in the order a streaming target reads them (TexturePack::fileOrder), which
// need not be their index order. Canonical like D3WD.
//
// Header:    u32 magic 'D3TX', u16 version 5, u16 header bytes 52, u32 material count, u32 texture
//            count, u32 FNV-1a of the world's material names (each with its NUL, the D3WD Strings
//            section), u32 file bytes, u32 FNV-1a of header and tables with this field read as zero,
//            u32 converter version, u32 lightmap page count, u32 pass count, u32 expression node
//            count, u32 table value count, u32 sky count (0 or 1).
// Materials: 16 bytes each: u32 the base pass's texture, 0xFFFFFFFF (drawn untextured),
//            0xFFFFFFFE (no base pass: translucent, drawn by its passes alone) or 0xFFFFFFFD (a sky: drawn into depth alone, the sky box behind it
//            showing through; needs the sky); f32 log2 of the material's UV units per world unit over the map's
//            surfaces (0 without area), from which the target sets the mip LOD (TEX1.K); u16 the
//            first of its passes, u8 their count (drawn after the batches, in order), u8 the alpha
//            test's reference on the GS scale (0x80 = 1.0; 0 without a test); u32 0.
// Passes:    24 bytes each, a stage drawn with the batch's UVs and depth after the batches: an
//            opaque material's additive stage over its base pass, or a translucent material's stage
//            over the frame. u32 its texture, u8 x 4 its RGBA colour (0x80 = 1.0) when no expression
//            drives it, u8 the blend (1: its colour times its texture added to the frame; 2, a
//            translucent material's only: the frame times the texture's alpha times the colour's; 3,
//            likewise: the two mixed by that alpha), u8 flags (bit 0, an add of a translucent
//            material only: the batch's vertex colours instead of its colour),
//            u16 the first of its expression nodes, u16 their count, u16 the node whose value is
//            its condition (the pass draws while it is non-zero) or 0xFFFF for always, u16 x 4 the
//            nodes of its colour channels or 0xFFFF for the constant.
// Textures:  16 bytes each: u32 offset, u32 bytes, u32 flags (bit 0 alpha-tested, bit 1 a lightmap
//            page: GSTX usage Lightmap, which no material names), u32 0.
// Pages:     4 bytes each: u32 the lightmap texture of D3WD batch page N.
// Nodes:     12 bytes each, idMaterial's register ops with every input but time folded in by the cook
//            (the material's parms, zero globals and sound): u8 op (ExpressionOp: Constant, Time,
//            Table or an operator), u8 flags (Table: bit 0 snap, bit 1 clamp), u16 a, u16 b (operand
//            nodes, earlier in the same pass; Table: a the index, b its first value), u16 the table's
//            value count (its first value repeated at the end, as idDeclTable keeps it), f32 the
//            constant.
// Values:    4 bytes each: f32 table values.
// Sky:       32 bytes: u32 x 6 the textures of the sky box's faces in GL's cube order (+X, -X, +Y,
//            -Y, +Z, -Z in id's world axes, each face's S and T as GL's cube map lookup takes them),
//            u8 x 4 the RGBA colour they draw with (0x80 = 1.0), u32 0.
static constexpr std::uint32_t kD3txMagic = 0x58543344;
static constexpr std::uint16_t kD3txVersion = 5;
static constexpr std::uint32_t kD3txConverterVersion = 6;
static constexpr std::size_t kD3txHeaderBytes = 52;
static constexpr std::size_t kD3txMaterialRecordBytes = 16;
static constexpr std::size_t kD3txPassRecordBytes = 24;
static constexpr std::size_t kD3txTextureRecordBytes = 16;
static constexpr std::size_t kD3txPageRecordBytes = 4;
static constexpr std::size_t kD3txNodeRecordBytes = 12;
static constexpr std::size_t kD3txValueRecordBytes = 4;
static constexpr std::size_t kD3txSkyRecordBytes = 32;
static constexpr std::size_t kD3txAlignment = 64;
static constexpr std::uint32_t kD3txNoTexture = 0xFFFFFFFFU;
static constexpr std::uint32_t kD3txHidden = 0xFFFFFFFEU;
static constexpr std::uint32_t kD3txSky = 0xFFFFFFFDU;
static constexpr std::uint32_t kD3txMaxSkies = 1;
static constexpr std::size_t kD3txSkyFaces = 6;
static constexpr std::uint32_t kD3txFlagAlphaTest = 1U;
static constexpr std::uint32_t kD3txFlagLightmap = 2U;
static constexpr std::uint32_t kD3txMaxPages = 256;
static constexpr std::uint32_t kD3txMaxMaterials = 4096;
static constexpr std::uint32_t kD3txMaxTextures = 4096;
static constexpr std::uint32_t kD3txMaxPasses = 4096;
static constexpr std::uint32_t kD3txMaxPassesPerMaterial = 8;
static constexpr std::uint32_t kD3txMaxNodes = 16384;
static constexpr std::uint32_t kD3txMaxNodesPerPass = 64;
static constexpr std::uint32_t kD3txMaxValues = 16384;
static constexpr std::uint8_t kD3txMaxAlphaRef = 0x80;
static constexpr std::uint8_t kD3txBlendAdd = 1;
static constexpr std::uint8_t kD3txBlendMultiply = 2;
static constexpr std::uint8_t kD3txBlendAlpha = 3;
static constexpr std::uint8_t kD3txPassVertexColor = 1;
static constexpr std::uint16_t kD3txNoNode = 0xFFFF;
static constexpr std::uint8_t kD3txTableSnap = 1;
static constexpr std::uint8_t kD3txTableClamp = 2;

struct D3txNode
{
    std::uint8_t op = 0;
    std::uint8_t flags = 0;
    std::uint16_t a = 0;
    std::uint16_t b = 0;
    std::uint16_t count = 0;
    float value = 0.0f;

    bool operator==(const D3txNode &) const = default;
};

struct D3txPass
{
    std::uint32_t texture = 0;
    std::array<std::uint8_t, 4> color = { 0x80, 0x80, 0x80, 0x80 };
    std::uint8_t blend = kD3txBlendAdd;
    std::uint8_t flags = 0;
    std::uint16_t firstNode = 0;
    std::uint16_t nodeCount = 0;
    std::uint16_t condition = kD3txNoNode;
    std::array<std::uint16_t, 4> colorNodes = { kD3txNoNode, kD3txNoNode, kD3txNoNode, kD3txNoNode };

    bool operator==(const D3txPass &) const = default;
};

// The sky box: its faces' textures and the colour they draw with.
struct D3txSky
{
    std::array<std::uint32_t, kD3txSkyFaces> faces = {};
    std::array<std::uint8_t, 4> color = { 0x80, 0x80, 0x80, 0x80 };

    bool operator==(const D3txSky &) const = default;
};

struct TexturePolicy
{
    // Level 0's size over the source's, rounded down to a power of two, never enlarging; a side
    // keeps at least min(its source rounded down, minExtent), since halving small textures loses
    // their detail for almost no VRAM.
    double scale = 0.5;
    int minExtent = 64;
    int maxExtent = 256;
    // Mips stop before a level's smaller side drops under this.
    int minMipExtent = 8;
    // PSMT4 when a 16-colour palette stays within both limits over every level.
    double t4MaxRmse = 3.0;
    int t4MaxError = 48;
    // When set, the format is decided on this image (the source's size) instead: the cavity bake
    // decides on the unbaked diffuse, so its shading never costs VRAM.
    const RgbaImage * formatSource = nullptr;
};

struct CookedTexture
{
    GstxV2Image image;
    QuantizeStats quality;
    int sourceWidth = 0;
    int sourceHeight = 0;
};

// 'source' straight RGBA; opaque textures have alpha 255 everywhere. The cooked image repeats on both axes.
bool CookTexture(const RgbaImage & source, const TexturePolicy & policy, GstxUsage usage, CookedTexture & output, std::string & error);

// The FNV-1a of material names as the D3WD Strings section holds them.
std::uint32_t MaterialNamesHash(const std::vector<std::string> & materials);

// Per material: its base texture, UV density, alpha test reference and passes (alike in length).
struct TexturePack
{
    std::uint32_t materialsHash = 0;
    std::vector<std::uint32_t> materialTextures;
    std::vector<float> materialUvDensity;
    std::vector<std::uint8_t> materialAlphaRef;
    std::vector<std::uint16_t> materialFirstPass;
    std::vector<std::uint8_t> materialPassCount;
    std::vector<D3txPass> passes;
    std::vector<std::uint32_t> textureFlags;
    std::vector<std::vector<std::uint8_t> > textures;
    std::vector<std::uint32_t> lightmapPages;
    std::vector<D3txNode> nodes;
    std::vector<float> tableValues;
    // At most kD3txMaxSkies; the materials marked kD3txSky draw it.
    std::vector<D3txSky> skies;
    // The textures in the order their files follow the tables (a permutation of their indices), or
    // empty for index order. The target streams them one at a time, so the textures a view and its
    // neighbours read lie together.
    std::vector<std::uint32_t> fileOrder;
};

// Cavity shading from a normal map (tangent space, decoded RGB with 0..255 for -1..1), the plan's
// default for bump maps on the GS (RENDERING_ARCHITECTURE.md 10.2): each diffuse texel scales by
// 1 + amplitude x (clamp(N.L / L.z, 0.5, 1.5) - 1), with L a key light fixed in tangent space, so
// the surface keeps its relief under the light that bakes it. 'normal' is sampled at the diffuse's
// size, bilinearly, wrapping.
struct CavityPolicy
{
    float amplitude = 0.0f;
    std::array<float, 3> light = { 0.3f, 0.3f, 0.9f };
};

void BakeCavity(RgbaImage & diffuse, const RgbaImage & normal, const CavityPolicy & policy);

// A Light8 page: 'texels' (size x size, 128 = 1.0) as PSMT8 through an identity ramp CLUT whose
// alpha is the index (and RGB a grey of it), clamped, one level.
bool CookLightmapPage(int size, const std::vector<std::uint8_t> & texels, GstxV2Image & output, std::string & error);

// Validates every texture (ReadGstxV2) and index, then encodes.
bool WriteD3tx(const TexturePack & pack, std::vector<std::uint8_t> & output, std::string & error);

// Validates the header, tables, alignment, padding and every texture.
bool ReadD3tx(std::span<const std::uint8_t> bytes, TexturePack & output, std::string & error);

} // namespace assets
