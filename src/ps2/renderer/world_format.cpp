// ================================================================================================
// File: world_format.cpp
// Brief: Validate a cooked D3WD world file in place: header, directory, checksums, record ranges,
// cross references and every batch packet's VIFcodes.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/world_format.h"
#include "ps2/renderer/vif.h"

#include <bit> // not ps2/math: the host world tests compile this file too

namespace ps2::renderer::world
{
namespace
{

static constexpr std::uint32_t kRecordBytes[kSectionCount] = { 0, 8, 64, 64, 16, 12, 24, 64, 6, 2, 0 };

static std::uint32_t Word(const std::uint8_t * bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

static std::uint32_t Half(const std::uint8_t * bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8);
}

static std::uint32_t Fnv(const std::uint8_t * bytes, std::uint32_t length, std::uint32_t hash)
{
    for (std::uint32_t index = 0; index < length; ++index)
    {
        hash = (hash ^ bytes[index]) * 16777619U;
    }

    return hash;
}

static bool Zero(const std::uint8_t * bytes, std::uint32_t count)
{
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (bytes[index] != 0)
        {
            return false;
        }
    }

    return true;
}

static std::uint32_t Align(std::uint32_t value, std::uint32_t alignment)
{
    return (value + alignment - 1U) / alignment * alignment;
}

static bool Finite(float value)
{
    // No Inf/NaN on the EE FPU, but a file can still carry their bit patterns.
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    return (bits & 0x7F800000U) != 0x7F800000U;
}

// Every unpack of a batch is relative to TOPS: VIF1 alternates the two input halves.
static constexpr std::uint32_t Unpack(dma::UnpackFormat format, std::uint32_t count, std::uint32_t address, bool unsignedExtend)
{
    return dma::VifUnpack(format, count, address, true, unsignedExtend);
}

template<typename T>
static const T * Records(const std::uint8_t * bytes)
{
    return static_cast<const T *>(static_cast<const void *>(bytes));
}

// The packet the cook writes for this header, VIFcode for VIFcode (src/tools/assets/world.h).
static const char * CheckPacket(const std::uint8_t * packet, std::uint32_t qwords, const BatchRecord & batch)
{
    const std::uint32_t words = qwords * 4U;
    const bool indexed = (batch.flags & kFlagIndexedStrips) != 0;
    const std::uint32_t count = batch.vertexCount;
    const std::uint32_t indexCount = batch.indexCount;
    const std::uint32_t indexQwords = (indexCount + 3U) / 4U;
    const std::uint32_t base = kHeaderQwords + indexQwords;

    if (indexed != (indexCount != 0) || count == 0 || (indexed && count > kMaxIndexedVertices) || base + kVertexQwords * count > kInputHalfQwords) [[unlikely]]
    {
        return "batch exceeds the VU1 input half";
    }

    std::uint32_t at = 0;
    const auto take = [packet, words, &at](std::uint32_t expected)
    {
        return at < words && Word(packet + 4U * at++) == expected;
    };

    if (!take(dma::VifStcycl(1U, 1U)) || !take(Unpack(dma::UnpackFormat::V4_32, 2, 0, false)) || at + 8U > words) [[unlikely]]
    {
        return "bad batch header unpack";
    }

    const std::uint8_t * header = packet + 4U * at;

    if (Word(header) != std::bit_cast<std::uint32_t>(batch.positionOffset[0]) ||
        Word(header + 4) != std::bit_cast<std::uint32_t>(batch.positionOffset[1]) ||
        Word(header + 8) != std::bit_cast<std::uint32_t>(batch.positionOffset[2]) || Word(header + 12) != indexQwords ||
        Word(header + 16) != std::bit_cast<std::uint32_t>(batch.positionScale)) [[unlikely]]
    {
        return "VU header disagrees with the batch header";
    }

    at += 8U;

    if (indexed)
    {
        if (!take(Unpack(dma::UnpackFormat::V4_16, indexQwords, 2, true)) || at + 2U * indexQwords > words) [[unlikely]]
        {
            return "bad index unpack";
        }

        std::uint32_t run = 0;

        for (std::uint32_t index = 0; index < indexQwords * 4U; ++index)
        {
            const std::uint32_t entry = Half(packet + 4U * at + 2U * index);
            const std::uint32_t offset = entry & ~kEntryAdc;

            if (index >= indexCount)
            {
                if (entry != (kEntryAdc | base)) [[unlikely]]
                {
                    return "bad index padding";
                }

                continue;
            }

            if (offset < base || (offset - base) % kVertexQwords != 0 || (offset - base) / kVertexQwords >= count) [[unlikely]]
            {
                return "strip index out of range";
            }

            // Two ADC entries open a strip, which then closes at least one triangle.
            const bool adc = (entry & kEntryAdc) != 0;

            if ((adc && run == 2) || (!adc && run < 2)) [[unlikely]]
            {
                return "malformed strip";
            }

            run = adc ? (run == 1 ? 2U : 1U) : run + 1U;
        }

        if (run < 3) [[unlikely]]
        {
            return "unfinished strip";
        }

        at += 2U * indexQwords;
    }

    if (!take(dma::VifStcycl(3U, 1U)) || !take(Unpack(dma::UnpackFormat::V4_16, count, base, false)) || at + 2U * count > words) [[unlikely]]
    {
        return "bad position unpack";
    }

    for (std::uint32_t vertex = 0; vertex < count; ++vertex)
    {
        const std::uint32_t w = Word(packet + 4U * (at + 2U * vertex + 1U)) >> 16;

        if (indexed ? w != 0 : (w != 0 && w != kEntryAdc)) [[unlikely]]
        {
            return "bad position flag";
        }
    }

    at += 2U * count;

    if (!take(Unpack(dma::UnpackFormat::V4_16, count, base + 1U, false)) || at + 2U * count > words) [[unlikely]]
    {
        return "bad UV unpack";
    }

    at += 2U * count;

    if (!take(Unpack(dma::UnpackFormat::V4_8, count, base + 2U, true)) || at + count > words) [[unlikely]]
    {
        return "bad colour unpack";
    }

    at += count;

    if (!take(dma::VifItop(count))) [[unlikely]]
    {
        return "bad packet tail";
    }

    while (at < words)
    {
        if (!take(dma::kVifNop)) [[unlikely]]
        {
            return "bad packet padding";
        }
    }

    return nullptr;
}

static const char * CheckRecords(Sections & s)
{
    std::uint32_t nextSurface = 0;
    std::uint32_t nextBatch = 0;

    for (std::uint32_t index = 0; index < s.areaCount; ++index)
    {
        const AreaRecord & area = s.areas[index];

        if (area.reserved[0] != 0 || area.reserved[1] != 0 || area.firstSurface != nextSurface || area.surfaceCount > s.surfaceCount - nextSurface ||
            area.firstBatch != nextBatch || area.batchCount > s.batchCount - nextBatch || !(area.mirrorScale > 0.0f) || !Finite(area.mirrorScale)) [[unlikely]]
        {
            return "bad area record";
        }

        nextSurface += area.surfaceCount;
        nextBatch += area.batchCount;
    }

    if (nextSurface != s.surfaceCount || nextBatch != s.batchCount) [[unlikely]]
    {
        return "areas do not cover the surfaces and batches";
    }

    std::uint32_t nextVertex = 0;
    std::uint32_t nextIndex = 0;
    nextBatch = 0;

    for (std::uint32_t index = 0; index < s.surfaceCount; ++index)
    {
        const SurfaceRecord & surface = s.surfaces[index];
        const bool inArea = surface.area < s.areaCount && index >= s.areas[surface.area].firstSurface &&
                            index - s.areas[surface.area].firstSurface < s.areas[surface.area].surfaceCount;

        if (surface.reserved[0] != 0 || surface.reserved[1] != 0 || !inArea || surface.material >= s.materialCount || surface.firstBatch != nextBatch ||
            surface.batchCount > s.batchCount - nextBatch || surface.firstVertex != nextVertex || surface.vertexCount > s.mirrorVertexCount - nextVertex ||
            surface.vertexCount > 65536U || surface.firstIndex != nextIndex || surface.indexCount > s.mirrorIndexCount - nextIndex ||
            surface.indexCount % 3U != 0) [[unlikely]]
        {
            return "bad surface record";
        }

        for (std::uint32_t corner = 0; corner < surface.indexCount; ++corner)
        {
            if (s.mirrorIndices[surface.firstIndex + corner] >= surface.vertexCount) [[unlikely]]
            {
                return "mirror index out of range";
            }
        }

        for (std::uint32_t batch = surface.firstBatch; batch < surface.firstBatch + surface.batchCount; ++batch)
        {
            if (s.batches[batch].material != surface.material) [[unlikely]]
            {
                return "batch material differs from its surface";
            }
        }

        nextBatch += surface.batchCount;
        nextVertex += surface.vertexCount;
        nextIndex += surface.indexCount;
    }

    if (nextBatch != s.batchCount || nextVertex != s.mirrorVertexCount || nextIndex != s.mirrorIndexCount) [[unlikely]]
    {
        return "surfaces do not cover the batches and mirror";
    }

    std::uint32_t nextPoint = 0;

    for (std::uint32_t index = 0; index < s.portalCount; ++index)
    {
        const PortalRecord & portal = s.portals[index];

        if (portal.firstPoint != nextPoint || portal.pointCount < 3 || portal.pointCount > s.portalPointCount - nextPoint ||
            portal.areas[0] >= s.areaCount || portal.areas[1] >= s.areaCount || portal.areas[0] == portal.areas[1]) [[unlikely]]
        {
            return "bad portal record";
        }

        nextPoint += portal.pointCount;
    }

    if (nextPoint != s.portalPointCount) [[unlikely]]
    {
        return "unused portal points";
    }

    if (s.nodeCount == 0) [[unlikely]]
    {
        return "no area nodes";
    }

    // Children follow their node, as dmap numbers them, so id's recursive walks end.
    for (std::uint32_t index = 0; index < s.nodeCount; ++index)
    {
        for (const std::int32_t child : s.nodes[index].children)
        {
            if ((child > 0 && (static_cast<std::uint32_t>(child) >= s.nodeCount || static_cast<std::uint32_t>(child) <= index)) ||
                (child < 0 && static_cast<std::uint32_t>(-1 - child) >= s.areaCount)) [[unlikely]]
            {
                return "bad node child";
            }
        }
    }

    std::uint32_t nextPacket = 0;
    s.indexedStrips = s.batchCount != 0 && (s.batches[0].flags & kFlagIndexedStrips) != 0;

    for (std::uint32_t index = 0; index < s.batchCount; ++index)
    {
        const BatchRecord & batch = s.batches[index];

        // One program draws the whole pass.
        if (((batch.flags & kFlagIndexedStrips) != 0) != s.indexedStrips) [[unlikely]]
        {
            return "batches mix topologies";
        }

        if (batch.reserved != 0 || (batch.flags & ~(kLayoutMask | kFlagIndexedStrips)) != 0 || (batch.flags & kLayoutMask) != kLayoutWorldLightmapped ||
            batch.material >= s.materialCount || batch.lightmapPage != kNoLightmap || batch.packetOffset != nextPacket || batch.packetQwords == 0 ||
            batch.packetQwords > (s.packetBytes - nextPacket) / 16U || !(batch.positionScale > 0.0f) || !Finite(batch.positionScale)) [[unlikely]]
        {
            return "bad batch record";
        }

        if (const char * error = CheckPacket(s.packets + batch.packetOffset, batch.packetQwords, batch)) [[unlikely]]
        {
            return error;
        }

        nextPacket += batch.packetQwords * 16U;
    }

    return nextPacket == s.packetBytes ? nullptr : "unused packet bytes";
}

} // namespace

PS2_COLD_FUNC const char * Validate(const std::uint8_t * file, std::uint32_t bytes, Sections & output)
{
    output = {};

    if (bytes < kDirectoryBytes || bytes > kMaxFileBytes || (reinterpret_cast<std::uintptr_t>(file) & 15U) != 0) [[unlikely]]
    {
        return "truncated or misaligned world file";
    }

    if (Word(file) != kMagic || Half(file + 4) != kVersion || Half(file + 6) != kHeaderBytes || Half(file + 8) != kSectionCount ||
        Half(file + 10) != 0 || Word(file + 20) != kConverterVersion || !Zero(file + 24, 8)) [[unlikely]]
    {
        return "unsupported world header";
    }

    if (Word(file + 12) != bytes || bytes % 16U != 0) [[unlikely]]
    {
        return "world file size mismatch";
    }

    // FNV-1a over the header and directory with the checksum field (bytes 16..19) read as zero.
    static constexpr std::uint8_t kZero[4] = {};
    const std::uint32_t checksum = Fnv(file + 20, kDirectoryBytes - 20U, Fnv(kZero, 4, Fnv(file, 16, 2166136261U)));

    if (checksum != Word(file + 16)) [[unlikely]]
    {
        return "world header checksum mismatch";
    }

    const std::uint8_t * sections[kSectionCount] = {};
    std::uint32_t sizes[kSectionCount] = {};
    std::uint32_t counts[kSectionCount] = {};
    std::uint32_t expected = kDirectoryBytes;

    for (std::uint32_t section = 0; section < kSectionCount; ++section)
    {
        const std::uint8_t * record = file + kHeaderBytes + section * 16U;
        const std::uint32_t offset = Word(record);
        const std::uint32_t size = Word(record + 4);
        const std::uint32_t previousEnd = expected;
        expected = Align(expected, section == static_cast<std::uint32_t>(Section::Packets) ? kPacketAlignment : 16U);

        if (offset != expected || offset > bytes || size > bytes - offset || !Zero(file + previousEnd, offset - previousEnd)) [[unlikely]]
        {
            return "world section out of place";
        }

        if (Fnv(file + offset, size, 2166136261U) != Word(record + 12)) [[unlikely]]
        {
            return "world section checksum mismatch";
        }

        counts[section] = Word(record + 8);

        if (kRecordBytes[section] != 0 && counts[section] * kRecordBytes[section] != size) [[unlikely]]
        {
            return "world section size mismatch";
        }

        sections[section] = file + offset;
        sizes[section] = size;
        expected = offset + size;
    }

    if (Align(expected, 16U) != bytes || !Zero(file + expected, bytes - expected)) [[unlikely]]
    {
        return "trailing world bytes";
    }

    const auto at = [](Section section)
    {
        return static_cast<std::uint32_t>(section);
    };

    output.strings = static_cast<const char *>(static_cast<const void *>(sections[at(Section::Strings)]));
    output.materials = Records<MaterialRecord>(sections[at(Section::Materials)]);
    output.areas = Records<AreaRecord>(sections[at(Section::Areas)]);
    output.surfaces = Records<SurfaceRecord>(sections[at(Section::Surfaces)]);
    output.portals = Records<PortalRecord>(sections[at(Section::Portals)]);
    output.portalPoints = Records<float>(sections[at(Section::PortalPoints)]);
    output.nodes = Records<NodeRecord>(sections[at(Section::Nodes)]);
    output.batches = Records<BatchRecord>(sections[at(Section::Batches)]);
    output.mirrorPositions = Records<std::int16_t>(sections[at(Section::MirrorPositions)]);
    output.mirrorIndices = Records<std::uint16_t>(sections[at(Section::MirrorIndices)]);
    output.packets = sections[at(Section::Packets)];
    output.materialCount = counts[at(Section::Materials)];
    output.areaCount = counts[at(Section::Areas)];
    output.surfaceCount = counts[at(Section::Surfaces)];
    output.portalCount = counts[at(Section::Portals)];
    output.portalPointCount = counts[at(Section::PortalPoints)];
    output.nodeCount = counts[at(Section::Nodes)];
    output.batchCount = counts[at(Section::Batches)];
    output.mirrorVertexCount = counts[at(Section::MirrorPositions)];
    output.mirrorIndexCount = counts[at(Section::MirrorIndices)];
    output.packetBytes = sizes[at(Section::Packets)];

    if (output.packetBytes != counts[at(Section::Packets)] * 16U || counts[at(Section::Strings)] != output.materialCount) [[unlikely]]
    {
        output = {};
        return "bad string or packet count";
    }

    // Canonical names: packed in order, each NUL-terminated, nothing left over.
    std::uint32_t stringEnd = 0;
    const std::uint32_t stringBytes = sizes[at(Section::Strings)];

    for (std::uint32_t index = 0; index < output.materialCount; ++index)
    {
        const MaterialRecord & material = output.materials[index];

        if (material.nameOffset != stringEnd || material.nameBytes == 0 || material.nameBytes >= stringBytes - stringEnd ||
            output.strings[stringEnd + material.nameBytes] != '\0') [[unlikely]]
        {
            output = {};
            return "bad material name";
        }

        for (std::uint32_t character = 0; character < material.nameBytes; ++character)
        {
            if (output.strings[stringEnd + character] == '\0') [[unlikely]]
            {
                output = {};
                return "bad material name";
            }
        }

        stringEnd += material.nameBytes + 1U;
    }

    if (stringEnd != stringBytes) [[unlikely]]
    {
        output = {};
        return "unused string bytes";
    }

    if (const char * error = CheckRecords(output)) [[unlikely]]
    {
        output = {};
        return error;
    }

    return nullptr;
}

} // namespace ps2::renderer::world
