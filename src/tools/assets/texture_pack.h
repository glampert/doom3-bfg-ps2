// ================================================================================================
// File: texture_pack.h
// Brief: Cook a level's world textures into GSTX v2 images (power-of-two levels, box-filtered mips,
// PSMT4 where its palette is good enough, PSMT8 otherwise) and pack them with the map's Light8
// lightmap pages into D3TX v2, the map's texture package, indexed by the D3WD's material order and
// its batches' lightmap pages.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "assets/gstx_v2.h"
#include "assets/image.h"
#include "assets/palette.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace assets
{

// Little-endian. A 48-byte header, the material table, the texture table, the lightmap page table,
// then the GSTX v2 files, each 64-byte aligned with zero padding (the DMA arena's alignment).
// Canonical like D3WD.
//
// Header:    u32 magic 'D3TX', u16 version 2, u16 header bytes 48, u32 material count, u32 texture
//            count, u32 FNV-1a of the world's material names (each with its NUL, the D3WD Strings
//            section), u32 file bytes, u32 FNV-1a of header and tables with this field read as zero,
//            u32 converter version, u32 lightmap page count, 12 zero bytes.
// Materials: 8 bytes each: u32 the texture index, 0xFFFFFFFF (drawn untextured) or 0xFFFFFFFE (not
//            drawn textured: translucent without a diffuse stage, which only its own stages could
//            draw), f32 log2 of the
//            material's UV units per world unit over the map's surfaces (0 without area), from
//            which the target sets the mip LOD (TEX1.K).
// Textures:  16 bytes each: u32 offset, u32 bytes, u32 flags (bit 0 alpha-tested, bit 1 a lightmap
//            page: GSTX usage Lightmap, which no material names), u32 0.
// Pages:     4 bytes each: u32 the lightmap texture of D3WD batch page N.
static constexpr std::uint32_t kD3txMagic = 0x58543344;
static constexpr std::uint16_t kD3txVersion = 2;
static constexpr std::uint32_t kD3txConverterVersion = 2;
static constexpr std::size_t kD3txHeaderBytes = 48;
static constexpr std::size_t kD3txMaterialRecordBytes = 8;
static constexpr std::size_t kD3txTextureRecordBytes = 16;
static constexpr std::size_t kD3txPageRecordBytes = 4;
static constexpr std::size_t kD3txAlignment = 64;
static constexpr std::uint32_t kD3txNoTexture = 0xFFFFFFFFU;
static constexpr std::uint32_t kD3txHidden = 0xFFFFFFFEU;
static constexpr std::uint32_t kD3txFlagAlphaTest = 1U;
static constexpr std::uint32_t kD3txFlagLightmap = 2U;
static constexpr std::uint32_t kD3txMaxPages = 256;
static constexpr std::uint32_t kD3txMaxMaterials = 4096;
static constexpr std::uint32_t kD3txMaxTextures = 4096;

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

struct TexturePack
{
    std::uint32_t materialsHash = 0;
    std::vector<std::uint32_t> materialTextures;
    std::vector<float> materialUvDensity;
    std::vector<std::uint32_t> textureFlags;
    std::vector<std::vector<std::uint8_t> > textures;
    std::vector<std::uint32_t> lightmapPages;
};

// A Light8 page: 'texels' (size x size, 128 = 1.0) as PSMT8 through an identity ramp CLUT whose
// alpha is the index (and RGB a grey of it), clamped, one level.
bool CookLightmapPage(int size, const std::vector<std::uint8_t> & texels, GstxV2Image & output, std::string & error);

// Validates every texture (ReadGstxV2) and index, then encodes.
bool WriteD3tx(const TexturePack & pack, std::vector<std::uint8_t> & output, std::string & error);

// Validates the header, tables, alignment, padding and every texture.
bool ReadD3tx(std::span<const std::uint8_t> bytes, TexturePack & output, std::string & error);

} // namespace assets
