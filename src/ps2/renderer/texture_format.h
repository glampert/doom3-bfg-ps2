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

// A map's texture package (D3TX v3, src/tools/assets/texture_pack.h), validated in place: the
// material, pass, texture, lightmap page, expression node and table value tables, then each
// texture validated as an Image. 64-byte aligned like the D3WD.
static constexpr std::uint32_t kPackMagic = 0x58543344;
static constexpr std::uint32_t kPackVersion = 3;
static constexpr std::uint32_t kPackConverterVersion = 3;
static constexpr std::uint32_t kPackHeaderBytes = 48;
static constexpr std::uint32_t kPackMaterialBytes = 16;
static constexpr std::uint32_t kPackPassBytes = 24;
static constexpr std::uint32_t kPackTextureBytes = 16;
static constexpr std::uint32_t kPackPageBytes = 4;
static constexpr std::uint32_t kPackNodeBytes = 12;
static constexpr std::uint32_t kPackValueBytes = 4;
static constexpr std::uint32_t kPackAlignment = 64;
static constexpr std::uint32_t kPackMaxMaterials = 4096;
static constexpr std::uint32_t kPackMaxTextures = 4096;
static constexpr std::uint32_t kPackMaxPasses = 4096;
static constexpr std::uint32_t kPackMaxPassesPerMaterial = 8;
static constexpr std::uint32_t kPackMaxNodes = 16384;
static constexpr std::uint32_t kPackMaxNodesPerPass = 64;
static constexpr std::uint32_t kPackMaxValues = 16384;
static constexpr std::uint32_t kPackMaxFileBytes = 16U * 1024U * 1024U;
static constexpr std::uint32_t kPackNoTexture = 0xFFFFFFFFU;
// Translucent without a diffuse stage: the textured pass leaves it to the material's own stages.
static constexpr std::uint32_t kPackHidden = 0xFFFFFFFEU;
static constexpr std::uint32_t kPackFlagAlphaTest = 1U;
// A Light8 page: usage Lightmap, named by the page table, never by a material.
static constexpr std::uint32_t kPackFlagLightmap = 2U;
static constexpr std::uint32_t kPackMaxPages = 256;
static constexpr std::uint8_t kPackMaxAlphaRef = 0x80;
static constexpr std::uint8_t kPackBlendAdd = 1;
static constexpr std::uint16_t kPackNoNode = 0xFFFF;
static constexpr std::uint8_t kPackTableSnap = 1;
static constexpr std::uint8_t kPackTableClamp = 2;

// The cooked register ops (the cook's ExpressionOp, src/tools/assets/material.h; parms, globals and
// sound are folded in by the cook and refused here).
enum class PackOp : std::uint8_t
{
    Constant,
    Time,
    Parm,
    Global,
    Sound,
    Table,
    Add,
    Subtract,
    Multiply,
    Divide,
    Mod,
    Greater,
    GreaterEqual,
    Less,
    LessEqual,
    Equal,
    NotEqual,
    And,
    Or
};

struct PackMaterial
{
    std::uint32_t texture;  // kPackNoTexture: drawn untextured; kPackHidden: not drawn textured
    float uvDensityLog2;    // log2 of UV units per world unit
    std::uint16_t firstPass;
    std::uint8_t passCount;
    std::uint8_t alphaRef;  // the alpha test's reference, 0x80 = 1.0; 0 without a test
    std::uint32_t reserved;
};

// An additive pass over the base pass, drawn while its condition holds with its colour times its
// texture.
struct PackPass
{
    std::uint32_t texture;
    std::uint8_t color[4];  // 0x80 = 1.0, for channels no node drives
    std::uint8_t blend;     // kPackBlendAdd
    std::uint8_t reserved;
    std::uint16_t firstNode;
    std::uint16_t nodeCount;
    std::uint16_t condition; // kPackNoNode: always
    std::uint16_t colorNodes[4];
};

struct PackTexture
{
    std::uint32_t offset;
    std::uint32_t bytes;
    std::uint32_t flags;
    std::uint32_t reserved;
};

struct PackNode
{
    PackOp op;
    std::uint8_t flags;  // Table: kPackTableSnap, kPackTableClamp
    std::uint16_t a;     // operands, earlier nodes of the pass; Table: the index
    std::uint16_t b;     // Table: its first value
    std::uint16_t count; // Table: its values, the first repeated at the end
    float value;         // Constant
};

static_assert(sizeof(PackMaterial) == kPackMaterialBytes && sizeof(PackPass) == kPackPassBytes && sizeof(PackTexture) == kPackTextureBytes &&
              sizeof(PackNode) == kPackNodeBytes);

struct Pack
{
    std::uint32_t materialCount;
    std::uint32_t textureCount;
    std::uint32_t materialsHash; // FNV-1a of the world's material names, each with its NUL
    std::uint32_t pageCount;
    std::uint32_t passCount;
    std::uint32_t nodeCount;
    std::uint32_t valueCount;
    const PackMaterial * materials;
    const PackPass * passes;
    const PackTexture * textures;
    const std::uint32_t * pages; // the texture of each D3WD lightmap page
    const PackNode * nodes;
    const float * values;
};

// Evaluates a validated pass's nodes at 'time' (seconds) into 'registers' (its nodeCount of them),
// as idMaterial::EvaluateRegisters does the cooked ops: a divide by zero gives zero, a modulo works
// on integers, and a table is idDeclTable::TableLookup.
void EvaluatePassNodes(const Pack & pack, const PackPass & pass, float time, float * registers);

// Null when the package is valid; 'images' receives every texture's Image (textureCount of them).
// The file must stay alive and 64-byte aligned.
PS2_COLD_FUNC const char * ValidatePack(const std::uint8_t * file, std::uint32_t bytes, Pack & output, Image * images,
                                        std::uint32_t imageCapacity);

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
