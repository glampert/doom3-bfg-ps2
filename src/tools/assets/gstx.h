// ================================================================================================
// File: gstx.h
// Brief: Write and validate GSTX v1, the converted GS texture: paletted index pages, CLUTs and the
// placements that map an engine image's logical texels onto them.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "assets/gs_texture.h"
#include "assets/image.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace assets
{

// Little-endian. A 48-byte header, then page, CLUT and placement tables, then 16-byte aligned
// data blocks in table order (CLUTs, then pages). The layout is canonical: one texture has exactly
// one valid encoding, so a catalog can admit a file by its header and tables alone.
//
// Header:     u32 magic 'GSTX', u16 version 1, u16 header bytes 48, u16 logical width/height,
//             u16 page/CLUT/placement counts, u16 flags 0, u32 table bytes, u32 file bytes,
//             u32 FNV-1a of the header and tables with this field read as zero, 16 reserved zero bytes.
// Page:       u16 width/height, u8 PSM, u8 encoding (0 raw, 1 RLE), u16 0, u32 data offset/bytes,
//             u32 decoded bytes, u32 FNV-1a of the decoded bytes, 8 reserved zero bytes.
// CLUT:       u16 entries (16 or 256), u16 0, u32 data offset/bytes, u32 FNV-1a of the entries.
//             Entries are RGBA8 texels in GS order (ClutTexels), alpha straight 0..255.
// Placement:  u16 match x/y/w/h, u16 source x/y/w/h (logical texels), u16 page x/y/w/h,
//             u16 page index, u16 CLUT index, 4 reserved zero bytes.
//
// A triangle belongs to the placement whose match rectangle holds its UV centroid; the placement
// maps the source rectangle onto the page rectangle, and sampling clamps to the page rectangle.
static constexpr std::uint32_t kGstxMagic = 0x58545347;
static constexpr std::uint16_t kGstxVersion = 1;
static constexpr std::size_t kGstxHeaderBytes = 48;
static constexpr std::size_t kGstxPageRecordBytes = 32;
static constexpr std::size_t kGstxClutRecordBytes = 16;
static constexpr std::size_t kGstxPlacementRecordBytes = 32;
static constexpr int kGstxMaxPages = 16;
static constexpr int kGstxMaxCluts = 128;
static constexpr int kGstxMaxPlacements = 1024;
static constexpr int kGstxMaxPageExtent = 512;
static constexpr int kGstxMaxLogicalExtent = 16384;

struct GstxPage
{
    int width = 0;
    int height = 0;
    IndexFormat format = IndexFormat::T8;
    // GS upload layout (PackIndices).
    std::vector<std::uint8_t> data;
};

struct GstxClut
{
    IndexFormat format = IndexFormat::T8;
    // GS order (ClutTexels): 256 or 16 texels.
    std::vector<PaletteColor> texels;
};

struct GstxPlacement
{
    Rect match;
    Rect source;
    Rect dest;
    int page = 0;
    int clut = 0;
};

struct GstxTexture
{
    int logicalWidth = 0;
    int logicalHeight = 0;
    std::vector<GstxPage> pages;
    std::vector<GstxClut> cluts;
    std::vector<GstxPlacement> placements;
};

struct GstxSizes
{
    std::size_t fileBytes = 0;
    std::size_t tableBytes = 0;
    // Decoded page and CLUT bytes the EE keeps for uploads.
    std::size_t decodedBytes = 0;
    // GS memory in whole 8 KiB pages, as the target allocates it.
    std::size_t vramBytes = 0;
};

// Validates the texture as ReadGstx would, then encodes it; pages use RLE when it is smaller.
bool WriteGstx(const GstxTexture & texture, std::vector<std::uint8_t> & output, GstxSizes & sizes, std::string & error);

// Validates every field, checksum and the canonical layout, and decodes pages and CLUTs.
bool ReadGstx(std::span<const std::uint8_t> bytes, GstxTexture & output, std::string & error);

// Bilinear sample of the converted texture at a logical texel position (texel centers at i + 0.5),
// through the placement whose match rectangle holds it. Returns false outside every placement.
bool SampleGstx(const GstxTexture & texture, double x, double y, PaletteColor & output);

} // namespace assets
