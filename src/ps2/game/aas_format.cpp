// ================================================================================================
// File: aas_format.cpp
// Brief: The D3AS checks, run at load in every build before the EE builds anything from a
// table, and by the host cook on what it writes: bounds, strings, the references the game
// follows, and the checksum for a file held whole.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/game/aas_format.h"
#include "ps2/fnv.h"
#include <cstring>

namespace ps2::game::aas
{

namespace
{

static constexpr std::int32_t kMinInt = -2147483647 - 1;

// The EE FPU has no Inf or NaN, and treats their bit patterns as huge numbers: refuse them.
bool Finite(float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7F800000U) != 0x7F800000U;
}

// 'records' viewed as their floats; a table may be empty and null.
bool AllFinite(const void * records, std::size_t count)
{
    const auto * const values = static_cast<const float *>(records);

    for (std::size_t index = 0; index < count; ++index)
    {
        if (!Finite(values[index])) [[unlikely]]
        {
            return false;
        }
    }

    return true;
}

// A signed table index (edge and face indexes, reachability edges): its magnitude below 'count'.
bool SignedBelow(std::int32_t value, std::uint32_t count)
{
    if (value == kMinInt) [[unlikely]]
    {
        return false;
    }

    const auto magnitude = static_cast<std::uint32_t>(value < 0 ? -value : value);
    return magnitude < count;
}

// [first, first + count) inside a table of 'size' entries.
bool RangeIn(std::int32_t first, std::int32_t count, std::uint32_t size)
{
    return first >= 0 && count >= 0 && static_cast<std::uint64_t>(first) + static_cast<std::uint64_t>(count) <= size;
}

bool Fail(const char * reason, const char *& error)
{
    error = reason;
    return false;
}

} // namespace

std::size_t SectionBytes(const Header & header)
{
    const std::uint64_t bytes =
        sizeof(Settings) + static_cast<std::uint64_t>(header.stringBytes) + static_cast<std::uint64_t>(header.pairCount) * sizeof(Pair) +
        static_cast<std::uint64_t>(header.planeCount) * sizeof(Plane) + static_cast<std::uint64_t>(header.vertexCount) * sizeof(Vertex) +
        static_cast<std::uint64_t>(header.edgeCount) * sizeof(Edge) + static_cast<std::uint64_t>(header.edgeIndexCount) * 4U +
        static_cast<std::uint64_t>(header.faceCount) * sizeof(Face) + static_cast<std::uint64_t>(header.faceIndexCount) * 4U +
        static_cast<std::uint64_t>(header.areaCount) * sizeof(Area) + static_cast<std::uint64_t>(header.nodeCount) * sizeof(Node) +
        static_cast<std::uint64_t>(header.portalCount) * sizeof(Portal) + static_cast<std::uint64_t>(header.portalIndexCount) * 4U +
        static_cast<std::uint64_t>(header.clusterCount) * sizeof(Cluster) + static_cast<std::uint64_t>(header.reachCount) * sizeof(Reach);
    return bytes > kMaxFileBytes ? 0 : static_cast<std::size_t>(bytes);
}

bool CheckHeader(const Header & header, std::size_t fileBytes, const char *& error)
{
    if (header.magic != kMagic || header.version != kVersion) [[unlikely]]
    {
        return Fail("not a D3AS version 1 file", error);
    }

    const std::uint32_t counts[] = { header.planeCount, header.vertexCount,      header.edgeCount,    header.edgeIndexCount,
                                     header.faceCount,  header.faceIndexCount,   header.areaCount,    header.nodeCount,
                                     header.portalCount, header.portalIndexCount, header.clusterCount, header.reachCount,
                                     header.pairCount };
    for (const std::uint32_t count : counts)
    {
        if (count > kMaxCount) [[unlikely]]
        {
            return Fail("a table count outside the format's bounds", error);
        }
    }

    // Area 0 and node 0 are dummies; the EE's point and trace queries start at node 1.
    if (header.areaCount < 1 || header.nodeCount < 2) [[unlikely]]
    {
        return Fail("no areas or no tree", error);
    }

    // The settings name their file extension, so the block holds a string at least.
    if (header.stringBytes < 4 || (header.stringBytes & 3U) != 0) [[unlikely]]
    {
        return Fail("string block size outside the format's bounds", error);
    }

    const std::size_t sections = SectionBytes(header);
    if (sections == 0 || fileBytes != sizeof(Header) + sections) [[unlikely]]
    {
        return Fail("section sizes do not add up to the file", error);
    }

    return true;
}

bool CheckStrings(const Tables & tables, const char *& error)
{
    const Header & header = *tables.header;
    const Settings & settings = *tables.settings;

    if (settings.boundingBoxCount < 1 || settings.boundingBoxCount > kMaxBoundingBoxes || (settings.flags & ~kKnownSettingsFlags) != 0)
        [[unlikely]]
    {
        return Fail("settings outside the format's bounds", error);
    }

    if (!AllFinite(settings.boundingBoxes, kMaxBoundingBoxes * 6U) || !AllFinite(settings.gravity, 3) ||
        !Finite(settings.maxStepHeight) || !Finite(settings.maxBarrierHeight) || !Finite(settings.maxWaterJumpHeight) ||
        !Finite(settings.maxFallHeight) || !Finite(settings.minFloorCos)) [[unlikely]]
    {
        return Fail("settings hold a value that is not finite", error);
    }

    // Every string ends inside the block, so a block ending in NUL bounds them all.
    if (tables.strings[header.stringBytes - 1] != '\0' || settings.fileExtension >= header.stringBytes) [[unlikely]]
    {
        return Fail("string block does not end in NUL, or the extension is outside it", error);
    }

    for (std::uint32_t index = 0; index < header.pairCount; ++index)
    {
        const Pair & pair = tables.pairs[index];
        if (pair.key >= header.stringBytes || pair.value >= header.stringBytes || tables.strings[pair.key] == '\0') [[unlikely]]
        {
            return Fail("pair string outside the block, or an empty key", error);
        }
    }

    return true;
}

bool CheckTables(const Tables & tables, const char *& error)
{
    const Header & header = *tables.header;

    if (!AllFinite(tables.planes, static_cast<std::size_t>(header.planeCount) * 4U) ||
        !AllFinite(tables.vertices, static_cast<std::size_t>(header.vertexCount) * 3U)) [[unlikely]]
    {
        return Fail("a plane or vertex that is not finite", error);
    }

    for (std::uint32_t index = 1; index < header.edgeCount; ++index)
    {
        const Edge & edge = tables.edges[index];
        if (edge.vertexNum[0] < 0 || static_cast<std::uint32_t>(edge.vertexNum[0]) >= header.vertexCount || edge.vertexNum[1] < 0 ||
            static_cast<std::uint32_t>(edge.vertexNum[1]) >= header.vertexCount) [[unlikely]]
        {
            return Fail("an edge's vertex outside the vertices", error);
        }
    }

    // Index entries are never 0: edge 0 and face 0 are dummies left unset.
    for (std::uint32_t index = 0; index < header.edgeIndexCount; ++index)
    {
        if (tables.edgeIndex[index] == 0 || !SignedBelow(tables.edgeIndex[index], header.edgeCount)) [[unlikely]]
        {
            return Fail("an edge index entry outside the edges", error);
        }
    }

    for (std::uint32_t index = 0; index < header.faceIndexCount; ++index)
    {
        if (tables.faceIndex[index] == 0 || !SignedBelow(tables.faceIndex[index], header.faceCount)) [[unlikely]]
        {
            return Fail("a face index entry outside the faces", error);
        }
    }

    for (std::uint32_t index = 1; index < header.faceCount; ++index)
    {
        const Face & face = tables.faces[index];
        if (face.planeNum >= header.planeCount || !RangeIn(face.firstEdge, face.numEdges, header.edgeIndexCount) || face.areas[0] < 0 ||
            static_cast<std::uint32_t>(face.areas[0]) >= header.areaCount || face.areas[1] < 0 ||
            static_cast<std::uint32_t>(face.areas[1]) >= header.areaCount) [[unlikely]]
        {
            return Fail("a face's plane, edges or areas outside their tables", error);
        }
    }

    std::uint64_t reaches = 0;
    for (std::uint32_t index = 0; index < header.areaCount; ++index)
    {
        const Area & area = tables.areas[index];
        if (!RangeIn(area.firstFace, area.numFaces, header.faceIndexCount)) [[unlikely]]
        {
            return Fail("an area's faces outside the face index", error);
        }

        reaches += area.reachCount;
    }

    if (reaches != header.reachCount) [[unlikely]]
    {
        return Fail("area reachability counts do not cover the reachabilities", error);
    }

    // A child is a node (positive), solid (0) or an area (negative).
    for (std::uint32_t index = 1; index < header.nodeCount; ++index)
    {
        const Node & node = tables.nodes[index];
        if (node.planeNum >= header.planeCount) [[unlikely]]
        {
            return Fail("a node's plane outside the planes", error);
        }

        for (const std::int32_t child : node.children)
        {
            const bool valid = child >= 0 ? static_cast<std::uint32_t>(child) < header.nodeCount : SignedBelow(child, header.areaCount);
            if (!valid) [[unlikely]]
            {
                return Fail("a node's child outside the nodes and areas", error);
            }
        }
    }

    for (std::uint32_t index = 1; index < header.portalCount; ++index)
    {
        const Portal & portal = tables.portals[index];
        if (portal.areaNum < 0 || static_cast<std::uint32_t>(portal.areaNum) >= header.areaCount || portal.clusters[0] < 0 ||
            static_cast<std::uint32_t>(portal.clusters[0]) >= header.clusterCount || portal.clusters[1] < 0 ||
            static_cast<std::uint32_t>(portal.clusters[1]) >= header.clusterCount) [[unlikely]]
        {
            return Fail("a portal's area or clusters outside their tables", error);
        }
    }

    for (std::uint32_t index = 0; index < header.portalIndexCount; ++index)
    {
        if (tables.portalIndex[index] < 0 || static_cast<std::uint32_t>(tables.portalIndex[index]) >= header.portalCount) [[unlikely]]
        {
            return Fail("a portal index entry outside the portals", error);
        }
    }

    for (std::uint32_t index = 1; index < header.clusterCount; ++index)
    {
        const Cluster & cluster = tables.clusters[index];
        if (!RangeIn(cluster.firstPortal, cluster.numPortals, header.portalIndexCount)) [[unlikely]]
        {
            return Fail("a cluster's portals outside the portal index", error);
        }
    }

    return true;
}

bool CheckReach(const Header & header, const Reach & reach, std::uint32_t pairsLeft, const char *& error)
{
    if (reach.toAreaNum < 0 || static_cast<std::uint32_t>(reach.toAreaNum) >= header.areaCount ||
        !SignedBelow(reach.edgeNum, header.edgeCount)) [[unlikely]]
    {
        return Fail("a reachability's area or edge outside their tables", error);
    }

    if (!AllFinite(reach.start, 3) || !AllFinite(reach.end, 3)) [[unlikely]]
    {
        return Fail("a reachability point that is not finite", error);
    }

    if (reach.pairCount > pairsLeft || (reach.pairCount != 0 && reach.travelType != kSpecialTravel)) [[unlikely]]
    {
        return Fail("reachability pairs past the pairs, or on a reachability that is not special", error);
    }

    return true;
}

bool Validate(const void * bytes, std::size_t length, Tables & tables, const char *& error)
{
    tables = Tables{};

    if (bytes == nullptr || length < sizeof(Header) || length > kMaxFileBytes) [[unlikely]]
    {
        return Fail("file size outside the format's bounds", error);
    }

    // Records are 4-byte aligned in the file; the buffer must be too.
    if ((reinterpret_cast<std::uintptr_t>(bytes) & 3U) != 0) [[unlikely]]
    {
        return Fail("file buffer is not 4-byte aligned", error);
    }

    const auto * const data = static_cast<const std::uint8_t *>(bytes);
    const auto * const header = static_cast<const Header *>(bytes);

    if (!CheckHeader(*header, length, error)) [[unlikely]]
    {
        return false;
    }

    if (Fnv1a32(data + sizeof(Header), length - sizeof(Header)) != header->checksum) [[unlikely]]
    {
        return Fail("checksum mismatch", error);
    }

    // Each section starts where the previous one ends, in the header's order.
    const std::uint8_t * at = data + sizeof(Header);
    const auto Take = [&at](std::size_t sectionBytes) -> const void *
    {
        const void * const section = at;
        at += sectionBytes;
        return section;
    };

    Tables view;
    view.header = header;
    view.settings = static_cast<const Settings *>(Take(sizeof(Settings)));
    view.strings = static_cast<const char *>(Take(header->stringBytes));
    view.pairs = static_cast<const Pair *>(Take(header->pairCount * sizeof(Pair)));
    view.planes = static_cast<const Plane *>(Take(header->planeCount * sizeof(Plane)));
    view.vertices = static_cast<const Vertex *>(Take(header->vertexCount * sizeof(Vertex)));
    view.edges = static_cast<const Edge *>(Take(header->edgeCount * sizeof(Edge)));
    view.edgeIndex = static_cast<const std::int32_t *>(Take(header->edgeIndexCount * 4U));
    view.faces = static_cast<const Face *>(Take(header->faceCount * sizeof(Face)));
    view.faceIndex = static_cast<const std::int32_t *>(Take(header->faceIndexCount * 4U));
    view.areas = static_cast<const Area *>(Take(header->areaCount * sizeof(Area)));
    view.nodes = static_cast<const Node *>(Take(header->nodeCount * sizeof(Node)));
    view.portals = static_cast<const Portal *>(Take(header->portalCount * sizeof(Portal)));
    view.portalIndex = static_cast<const std::int32_t *>(Take(header->portalIndexCount * 4U));
    view.clusters = static_cast<const Cluster *>(Take(header->clusterCount * sizeof(Cluster)));
    view.reaches = static_cast<const Reach *>(Take(header->reachCount * sizeof(Reach)));

    if (!CheckStrings(view, error) || !CheckTables(view, error)) [[unlikely]]
    {
        return false;
    }

    std::uint32_t pairsLeft = header->pairCount;
    for (std::uint32_t index = 0; index < header->reachCount; ++index)
    {
        if (!CheckReach(*header, view.reaches[index], pairsLeft, error)) [[unlikely]]
        {
            return false;
        }

        pairsLeft -= view.reaches[index].pairCount;
    }

    if (pairsLeft != 0) [[unlikely]]
    {
        return Fail("pairs no reachability takes", error);
    }

    tables = view;
    return true;
}

} // namespace ps2::game::aas
