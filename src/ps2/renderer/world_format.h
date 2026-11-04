// ================================================================================================
// File: world_format.h
// Brief: Validate a cooked D3WD world file in place and expose its sections as record arrays, so a
// malformed or tampered file is refused at load and the batch packets the display list REFs are
// known to fit one VU1 input half.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include <cstddef>
#include <cstdint>

namespace ps2::renderer::world
{

// The layout is defined by src/tools/assets/world.h, which writes it.
static constexpr std::uint32_t kMagic = 0x44573344;
static constexpr std::uint32_t kVersion = 1;
static constexpr std::uint32_t kConverterVersion = 1;
static constexpr std::uint32_t kHeaderBytes = 32;
static constexpr std::uint32_t kSectionCount = 11;
static constexpr std::uint32_t kDirectoryBytes = kHeaderBytes + kSectionCount * 16U;
static constexpr std::uint32_t kPacketAlignment = 64;
static constexpr std::uint32_t kMaxFileBytes = 16U * 1024U * 1024U;

static constexpr std::uint32_t kLayoutMask = 0xFU;
static constexpr std::uint32_t kLayoutWorldLightmapped = 0;
static constexpr std::uint32_t kFlagIndexedStrips = 1U << 4;
static constexpr std::uint32_t kNoLightmap = 0xFFFFU;
static constexpr std::uint32_t kEntryAdc = 0x8000U;

// VU1 data memory (vu1progs/vu_memory.h): a batch, its last write included, fits one input half.
static constexpr std::uint32_t kInputHalfQwords = 306;
static constexpr std::uint32_t kHeaderQwords = 2;
static constexpr std::uint32_t kVertexQwords = 3;
static constexpr std::uint32_t kMaxIndexedVertices = 88;

enum class Section : std::uint32_t
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
    Packets
};

// Records exactly as the file holds them (little-endian, like the EE).
struct MaterialRecord
{
    std::uint32_t nameOffset;
    std::uint32_t nameBytes;
};

struct AreaRecord
{
    float boundsMin[3];
    std::uint32_t firstSurface;
    float boundsMax[3];
    std::uint32_t surfaceCount;
    float mirrorOffset[3];
    float mirrorScale;
    std::uint32_t firstBatch;
    std::uint32_t batchCount;
    std::uint32_t reserved[2];
};

struct SurfaceRecord
{
    float boundsMin[3];
    std::uint32_t material;
    float boundsMax[3];
    std::uint32_t area;
    std::uint32_t firstBatch;
    std::uint32_t batchCount;
    std::uint32_t firstVertex;
    std::uint32_t vertexCount;
    std::uint32_t firstIndex;
    std::uint32_t indexCount;
    std::uint32_t reserved[2];
};

struct PortalRecord
{
    std::uint32_t firstPoint;
    std::uint32_t pointCount;
    std::uint32_t areas[2];
};

struct NodeRecord
{
    float plane[4];
    std::int32_t children[2];
};

struct BatchRecord
{
    float boundsMin[3];
    std::uint32_t flags;
    float boundsMax[3];
    std::uint32_t vertexCount;
    float positionOffset[3];
    float positionScale;
    std::uint32_t packetOffset; // bytes into the packet section, qword aligned
    std::uint32_t packetQwords;
    std::uint16_t material;
    std::uint16_t lightmapPage;
    std::uint16_t indexCount;
    std::uint16_t reserved;
};

static_assert(sizeof(MaterialRecord) == 8  && sizeof(AreaRecord)   == 64 &&
              sizeof(SurfaceRecord)  == 64 && sizeof(PortalRecord) == 16 &&
              sizeof(NodeRecord)     == 24 && sizeof(BatchRecord)  == 64);

// Views into a validated file. Names are NUL-terminated inside 'strings'.
struct Sections
{
    const char * strings;
    const MaterialRecord * materials;
    const AreaRecord * areas;
    const SurfaceRecord * surfaces;
    const PortalRecord * portals;
    const float * portalPoints; // xyz per point
    const NodeRecord * nodes;
    const BatchRecord * batches;
    const std::int16_t * mirrorPositions; // xyz per vertex
    const std::uint16_t * mirrorIndices;
    const std::uint8_t * packets;
    std::uint32_t materialCount;
    std::uint32_t areaCount;
    std::uint32_t surfaceCount;
    std::uint32_t portalCount;
    std::uint32_t portalPointCount;
    std::uint32_t nodeCount;
    std::uint32_t batchCount;
    std::uint32_t mirrorVertexCount;
    std::uint32_t mirrorIndexCount;
    std::uint32_t packetBytes;
};

// Validates the header, directory, checksums, every record range and cross reference and each
// batch packet's VIFcodes against its header. 'file' must be 16-byte aligned (64 for the packets
// to be REF'd in place). Returns null when valid, else what was wrong.
PS2_COLD_FUNC const char * Validate(const std::uint8_t * file, std::uint32_t bytes, Sections & output);

} // namespace ps2::renderer::world
