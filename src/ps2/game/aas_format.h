// ================================================================================================
// File: aas_format.h
// Brief: The cooked D3AS AAS file: an .aas file's settings, tables and reachabilities in the
// layouts of idAASFileLocal's own lists, so the PS2 streams each table straight into its list
// with no lexer (the text parse took 2 to 3 s of EE time). The host cook validates what it
// writes with the same checks the EE runs at load.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2::game::aas
{

// Written by src/tools/assets/aas_cook.cpp. A file is the header, then every section in the
// order of the header's counts: settings, strings, pairs, planes, vertices, edges, edge index,
// faces, face index, areas, nodes, portals, portal index, clusters and reachabilities. All of it
// is little-endian; the checksum covers every byte after the header.
static constexpr std::uint32_t kMagic = 0x53413344; // "D3AS"
static constexpr std::uint32_t kVersion = 1;
static constexpr std::uint32_t kMaxFileBytes = 16U * 1024U * 1024U;
static constexpr std::uint32_t kMaxCount = 1U << 20;
static constexpr std::uint32_t kMaxBoundingBoxes = 4;

// TFL_SPECIAL: the one travel type whose reachability carries key/value pairs.
static constexpr std::int32_t kSpecialTravel = 1 << 12;

// Settings flags.
static constexpr std::uint32_t kUsePatches = 1U << 0;
static constexpr std::uint32_t kWriteBrushMap = 1U << 1;
static constexpr std::uint32_t kPlayerFlood = 1U << 2;
static constexpr std::uint32_t kAllowSwimReachabilities = 1U << 3;
static constexpr std::uint32_t kAllowFlyReachabilities = 1U << 4;
static constexpr std::uint32_t kKnownSettingsFlags = (1U << 5) - 1U;

struct Header
{
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t geometryCrc;
    std::uint32_t checksum;
    std::uint32_t planeCount;
    std::uint32_t vertexCount;
    std::uint32_t edgeCount;
    std::uint32_t edgeIndexCount;
    std::uint32_t faceCount;
    std::uint32_t faceIndexCount;
    std::uint32_t areaCount;
    std::uint32_t nodeCount;
    std::uint32_t portalCount;
    std::uint32_t portalIndexCount;
    std::uint32_t clusterCount;
    std::uint32_t reachCount;
    std::uint32_t pairCount;
    std::uint32_t stringBytes;
};

static_assert(sizeof(Header) == 72);

// idAASSettings as the text sets it, the ctor's defaults for what the file leaves out; the EE
// derives the gravity direction and value from 'gravity' as id's parser does.
struct Settings
{
    std::uint32_t boundingBoxCount;
    float boundingBoxes[kMaxBoundingBoxes][6]; // mins then maxs
    std::uint32_t flags;
    std::uint32_t fileExtension; // string offset
    float gravity[3];
    float maxStepHeight;
    float maxBarrierHeight;
    float maxWaterJumpHeight;
    float maxFallHeight;
    float minFloorCos;
    std::int32_t ttBarrierJump;
    std::int32_t ttStartCrouching;
    std::int32_t ttWaterJump;
    std::int32_t ttStartWalkOffLedge;
};

static_assert(sizeof(Settings) == 156);

// Offsets of two NUL-terminated strings in the string block.
struct Pair
{
    std::uint32_t key;
    std::uint32_t value;
};

// The tables below are the EE's own records (idPlane, aasVertex_t, aasEdge_t, aasFace_t,
// aasNode_t, aasPortal_t, aasCluster_t); AASFile.cpp asserts each layout.

// idPlane: the normal and d, the negated distance.
struct Plane
{
    float a;
    float b;
    float c;
    float d;
};

struct Vertex
{
    float x;
    float y;
    float z;
};

struct Edge
{
    std::int32_t vertexNum[2];
};

struct Face
{
    std::uint16_t planeNum;
    std::uint16_t flags;
    std::int32_t numEdges;
    std::int32_t firstEdge;
    std::int16_t areas[2];
};

static_assert(sizeof(Face) == 16);

// An area's text fields and how many reachabilities follow the previous area's for it.
struct Area
{
    std::int32_t numFaces;
    std::int32_t firstFace;
    std::uint16_t flags;
    std::uint16_t contents;
    std::int16_t cluster;
    std::int16_t clusterAreaNum;
    std::uint32_t reachCount;
};

static_assert(sizeof(Area) == 20);

struct Node
{
    std::uint16_t planeNum;
    std::uint16_t padding;
    std::int32_t children[2];
};

static_assert(sizeof(Node) == 12);

// maxAreaTravelTime is the routing's, never the file's: the cook writes 0.
struct Portal
{
    std::int16_t areaNum;
    std::int16_t clusters[2];
    std::int16_t clusterAreaNum[2];
    std::uint16_t maxAreaTravelTime;
};

static_assert(sizeof(Portal) == 12);

struct Cluster
{
    std::int32_t numAreas;
    std::int32_t numReachableAreas;
    std::int32_t numPortals;
    std::int32_t firstPortal;
};

static_assert(sizeof(Cluster) == 16);

// A reachability from its area, in file order (the EE prepends each to its area's list, as the
// text parse does); a special one's pairs follow the previous special's.
struct Reach
{
    std::int32_t travelType;
    std::int16_t toAreaNum;
    std::uint16_t travelTime;
    float start[3];
    float end[3];
    std::int32_t edgeNum;
    std::uint32_t pairCount;
};

static_assert(sizeof(Reach) == 40);

// Every table of a file, wherever its records live (the file itself, or the EE's lists).
struct Tables
{
    const Header * header = nullptr;
    const Settings * settings = nullptr;
    const char * strings = nullptr;
    const Pair * pairs = nullptr;
    const Plane * planes = nullptr;
    const Vertex * vertices = nullptr;
    const Edge * edges = nullptr;
    const std::int32_t * edgeIndex = nullptr;
    const Face * faces = nullptr;
    const std::int32_t * faceIndex = nullptr;
    const Area * areas = nullptr;
    const Node * nodes = nullptr;
    const Portal * portals = nullptr;
    const std::int32_t * portalIndex = nullptr;
    const Cluster * clusters = nullptr;
    const Reach * reaches = nullptr; // null when the reader streams them through CheckReach
};

// The bytes the file's sections take after the header, from its counts; 0 when they overflow.
std::size_t SectionBytes(const Header & header);

// The header: magic, version, counts within bounds and the sections adding up to 'fileBytes'.
bool CheckHeader(const Header & header, std::size_t fileBytes, const char *& error);

// The settings, the string block and the pairs (the strings' NUL ends and offsets).
bool CheckStrings(const Tables & tables, const char *& error);

// Every reference the EE follows from the tables: areas into the face index, faces into the edge
// index and the planes, edges into the vertices, nodes into the planes, nodes and areas, the
// portals and clusters into each other, and the areas' reachability counts against the header.
// Entry 0 of each table is a dummy the game never reads, which retail files leave unset.
bool CheckTables(const Tables & tables, const char *& error);

// One reachability before the EE builds it; 'pairsLeft' is how many pairs no earlier one took.
bool CheckReach(const Header & header, const Reach & reach, std::uint32_t pairsLeft, const char *& error);

// A whole file in memory (the host cook's output): every check above and the checksum.
// 'bytes' must stay alive and unchanged while 'tables' is used.
bool Validate(const void * bytes, std::size_t length, Tables & tables, const char *& error);

} // namespace ps2::game::aas
