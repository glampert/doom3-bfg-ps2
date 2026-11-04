// ================================================================================================
// File: world.h
// Brief: Cook a level's render geometry into D3WD v1, the PS2 world file: areas, portals and nodes
// copied for id's portal code, VU1 batch packets REF'd straight from the display list, and the
// compact CPU mirror traces and decals read.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "assets/proc.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace assets
{

// Little-endian. A 32-byte header, a directory of one 16-byte record per section, then the
// sections in directory order, each starting 16-byte aligned (Packets 64-byte aligned, the DMA
// arena's alignment) with zero padding between them. The layout is canonical: one world has one
// valid encoding.
//
// Header:    u32 magic 'D3WD', u16 version 1, u16 header bytes 32, u16 section count 11, u16 flags 0,
//            u32 file bytes, u32 FNV-1a of header and directory with this field read as zero,
//            u32 converter version, 8 reserved zero bytes.
// Directory: per section u32 offset, u32 bytes, u32 record count, u32 FNV-1a of its bytes.
//
// Sections and records:
//   Strings         NUL-terminated material names; the count is the number of names.
//   Materials   8   u32 name offset (into Strings), u32 name bytes (without the NUL).
//   Areas      64   f32 bounds min[3], u32 first surface, f32 bounds max[3], u32 surface count,
//                   f32 mirror offset[3], f32 mirror scale, u32 first batch, u32 batch count, 8 zero.
//   Surfaces   64   f32 bounds min[3], u32 material, f32 bounds max[3], u32 area, u32 first batch,
//                   u32 batch count, u32 first mirror vertex, u32 mirror vertex count, u32 first
//                   mirror index, u32 mirror index count, 8 zero.
//   Portals    16   u32 first point, u32 point count, u32 area seen from, u32 area seen into.
//   PortalPoints 12 f32 xyz. A portal's winding faces its first area (.proc order).
//   Nodes      24   f32 plane[4] (n.p + d), s32 children[2] (.proc semantics).
//   Batches    64   BatchHeader, below.
//   MirrorPositions 6  s16 xyz; position = q * area mirror scale + area mirror offset.
//   MirrorIndices   2  u16, per surface relative to its first mirror vertex, id's winding.
//   Packets         VIF packets; the count is the qword total.
//
// An area's surfaces and batches, and a surface's batches, are contiguous; surfaces are ordered by
// area, batches by surface.
static constexpr std::uint32_t kD3wdMagic = 0x44573344;
static constexpr std::uint16_t kD3wdVersion = 1;
static constexpr std::uint32_t kD3wdConverterVersion = 1;
static constexpr std::size_t kD3wdHeaderBytes = 32;
static constexpr std::size_t kD3wdDirectoryRecordBytes = 16;
static constexpr std::size_t kD3wdPacketAlignment = 64;

enum class D3wdSection : std::uint8_t
{
    Strings,
    Materials,
    Areas,
    Surfaces,
    Portals,
    PortalPoints,
    Nodes,
    Batches,
    MirrorPositions,
    MirrorIndices,
    Packets,
    Count
};

static constexpr int kD3wdSectionCount = static_cast<int>(D3wdSection::Count);

// Batch header (64 bytes, plan section 6.2):
//   f32 bounds min[3], u32 flags, f32 bounds max[3], u32 vertex count, f32 position offset[3],
//   f32 position scale, u32 packet offset (bytes into Packets, qword aligned), u32 packet qwords,
//   u16 material, u16 lightmap page (0xFFFF none), u16 index count, u16 0.
//
// Flags: bits 0-3 vertex layout (0: WA), bit 4 indexed strips. Every packet is, in order:
//   STCYCL 1,1
//   UNPACK V4-32 2 qwords at +0 (TOPS):  qw0 position offset xyz (f32), w index qwords Q (u32);
//                                        qw1 position scale, UV scale, UV offset s, t (f32)
//   UNPACK V4-16 unsigned Q at +2        strip index stream, four u16 entries per qword (indexed only)
//   STCYCL 3,1
//   UNPACK V4-16 N at +2+Q    position s16 xyz, w the ADC flag 0x8000 (plain strips) or 0
//   UNPACK V4-16 N at +3+Q    UV s16 s, t and lightmap s, t (UV = q * UV scale + offset)
//   UNPACK V4-8 unsigned N at +4+Q  RGBA, 0x80 = 1.0
//   ITOP N; NOPs to the qword
// with Q = ceil(I/4). The packet starts no program: the display list sends MSCAL with the next tag,
// so the packet stays valid wherever vu::Init places the program. A strip index entry is the
// vertex's qword offset in the input half (2 + Q + 3 * vertex) in bits 0-14 and the GS ADC flag in
// bit 15, set on the first two entries of each strip, which close no triangle: VU1 has no integer
// multiply or shift, so the program adds the entry to XTOP and writes the flag into XYZ2 as they
// stand. Entries past I repeat the first vertex with ADC set, so a program may walk whole qwords.
// Strip triangle k is (k, k+1, k+2) in id's winding for even k and (k+1, k, k+2) for odd k. Plain
// strips have no index stream: the N vertices are emitted in strip order with the same ADC rule
// in position w.
static constexpr std::uint32_t kBatchLayoutWorldLightmapped = 0;
static constexpr std::uint32_t kBatchLayoutMask = 0xF;
static constexpr std::uint32_t kBatchFlagIndexedStrips = 1U << 4;
static constexpr std::uint16_t kBatchNoLightmap = 0xFFFF;
// BuildStrips and WalkStrips number vertices 0..127 with the ADC flag in bit 7; packets use bit 15.
static constexpr std::uint8_t kStripIndexAdc = 0x80;
static constexpr std::uint32_t kStripEntryAdc = 0x8000;
static constexpr std::int16_t kStripVertexAdc = -32768;

// VU1 data memory (ps2/renderer/vu1progs/vu_memory.h, plan section 7.2): each input half is 306
// qwords, the last write of a batch included; WA vertices take 3 qwords and the indexed layout
// stops at 88 unique vertices.
static constexpr int kWorldInputHalfQwords = 306;
static constexpr int kWorldHeaderQwords = 2;
static constexpr int kWorldVertexQwords = 3;
static constexpr int kWorldMaxIndexedVertices = 88;
static constexpr int kWorldMaxStripIndices = 255;

struct WorldBounds
{
    std::array<float, 3> min = {};
    std::array<float, 3> max = {};
};

struct WorldArea
{
    WorldBounds bounds;
    std::uint32_t firstSurface = 0;
    std::uint32_t surfaceCount = 0;
    std::array<float, 3> mirrorOffset = {};
    float mirrorScale = 1.0f;
    std::uint32_t firstBatch = 0;
    std::uint32_t batchCount = 0;
};

struct WorldSurface
{
    WorldBounds bounds;
    std::uint32_t material = 0;
    std::uint32_t area = 0;
    std::uint32_t firstBatch = 0;
    std::uint32_t batchCount = 0;
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
};

struct WorldPortal
{
    std::uint32_t firstPoint = 0;
    std::uint32_t pointCount = 0;
    std::uint32_t areas[2] = {};
};

struct WorldNode
{
    std::array<float, 4> plane = {};
    std::int32_t children[2] = {};
};

struct WorldBatch
{
    WorldBounds bounds;
    std::uint32_t flags = 0;
    std::uint32_t vertexCount = 0;
    std::array<float, 3> positionOffset = {};
    float positionScale = 1.0f;
    std::uint32_t packetOffset = 0;
    std::uint32_t packetQwords = 0;
    std::uint16_t material = 0;
    std::uint16_t lightmapPage = kBatchNoLightmap;
    std::uint16_t indexCount = 0;
};

struct World
{
    std::vector<std::string> materials;
    std::vector<WorldArea> areas;
    std::vector<WorldSurface> surfaces;
    std::vector<WorldPortal> portals;
    std::vector<std::array<float, 3>> portalPoints;
    std::vector<WorldNode> nodes;
    std::vector<WorldBatch> batches;
    std::vector<std::array<std::int16_t, 3>> mirrorPositions;
    std::vector<std::uint16_t> mirrorIndices;
    std::vector<std::uint8_t> packets;
};

enum class WorldTopology : std::uint8_t
{
    // Unique vertices plus a strip index stream (the plan's default).
    IndexedStrips,
    // Vertices repeated in strip order, for the VU1 cost comparison of gate R1.
    Strips
};

struct WorldMaterialColor
{
    std::string material;
    std::array<float, 3> color = {};
};

// Until lightmaps exist (gate R2) the vertex colour is the material's base colour shaded by one
// fixed direction: base * (ambient + (1 - ambient) * max(0, N.L)).
struct WorldPolicy
{
    WorldTopology topology = WorldTopology::IndexedStrips;
    std::array<float, 3> light = { 0.0f, 0.0f, 1.0f };
    float ambient = 0.5f;
    std::array<float, 3> defaultColor = { 0.5f, 0.5f, 0.5f };
    std::vector<WorldMaterialColor> colors;
};

struct WorldCookStats
{
    std::size_t triangles = 0;
    std::size_t batches = 0;
    // Vertices the VU transforms (unique per batch) and emits (strip order).
    std::size_t transformedVertices = 0;
    std::size_t emittedVertices = 0;
    std::size_t strips = 0;
    std::size_t packetBytes = 0;
    std::size_t skippedModels = 0;
    float maxPositionError = 0.0f;
    float maxUvError = 0.0f;
};

// Cooks every area model of 'proc'. Other models (entity brush models) are counted and skipped.
bool CookWorld(const ProcFile & proc, const WorldPolicy & policy, World & output, WorldCookStats & stats, std::string & error);

bool WriteD3wd(const World & world, std::vector<std::uint8_t> & file, std::string & error);

// Validates the header, directory, checksums, record ranges and every cross reference, including
// each batch packet's VIFcodes against its header.
bool ReadD3wd(std::span<const std::uint8_t> file, World & world, std::string & error);

// Greedy strips over triangles of local vertex indices (< 128), wound as id's. Returns the index
// stream (ADC on each strip's first two entries) and the strip count.
std::vector<std::uint8_t> BuildStrips(std::span<const std::array<std::uint8_t, 3>> triangles, std::size_t & strips);

// The triangles an index stream draws, in id's winding.
std::vector<std::array<std::uint8_t, 3>> WalkStrips(std::span<const std::uint8_t> stream);

} // namespace assets
