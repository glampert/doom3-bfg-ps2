// ================================================================================================
// File: world.cpp
// Brief: The world cook: batches of at most one VU1 input half, greedy strips over them, s16
// quantization, the VIF packets, the CPU mirror and the D3WD reader and writer.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/world.h"
#include "assets/bytes.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>

namespace assets
{
namespace
{

// VIFcodes (EE manual, VIF chapter): CMD in bits 24-31, NUM in 16-23, IMMEDIATE in 0-15.
static constexpr std::uint32_t kVifNop = 0x00;
static constexpr std::uint32_t kVifStcycl = 0x01;
static constexpr std::uint32_t kVifItop = 0x04;
static constexpr std::uint32_t kVifUnpack = 0x60;
static constexpr std::uint32_t kUnpackV4_32 = 0xC;
static constexpr std::uint32_t kUnpackV4_16 = 0xD;
static constexpr std::uint32_t kUnpackV4_8 = 0xE;

static constexpr std::size_t kMaterialRecordBytes = 8;
static constexpr std::size_t kAreaRecordBytes = 64;
static constexpr std::size_t kSurfaceRecordBytes = 64;
static constexpr std::size_t kPortalRecordBytes = 16;
static constexpr std::size_t kPointRecordBytes = 12;
static constexpr std::size_t kNodeRecordBytes = 24;
static constexpr std::size_t kBatchRecordBytes = 64;
static constexpr std::size_t kMirrorPositionBytes = 6;
static constexpr std::size_t kMirrorIndexBytes = 2;

static constexpr std::size_t kRecordBytes[kD3wdSectionCount] = {
    0, kMaterialRecordBytes, kAreaRecordBytes, kSurfaceRecordBytes, kPortalRecordBytes, kPointRecordBytes,
    kNodeRecordBytes, kBatchRecordBytes, kMirrorPositionBytes, kMirrorIndexBytes, 0
};

constexpr std::uint32_t VifCode(std::uint32_t command, std::uint32_t num, std::uint32_t immediate)
{
    return (command << 24) | ((num & 0xFFU) << 16) | (immediate & 0xFFFFU);
}

constexpr std::uint32_t VifStcycl(std::uint32_t cycle, std::uint32_t write)
{
    return VifCode(kVifStcycl, 0, cycle | (write << 8));
}

// Every unpack of a batch is relative to TOPS: VIF1 alternates the two input halves.
constexpr std::uint32_t VifUnpack(std::uint32_t format, std::uint32_t count, std::uint32_t address, bool unsignedExtend)
{
    return VifCode(kVifUnpack | format, count, address | (unsignedExtend ? 1U << 14 : 0U) | (1U << 15));
}

std::uint32_t Pack16(std::int16_t low, std::int16_t high)
{
    return static_cast<std::uint32_t>(static_cast<std::uint16_t>(low)) | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(high)) << 16);
}

std::uint32_t FloatBits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

float BitsFloat(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

void Extend(WorldBounds & bounds, const std::array<float, 3> & point, bool & empty)
{
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        bounds.min[axis] = empty ? point[axis] : std::min(bounds.min[axis], point[axis]);
        bounds.max[axis] = empty ? point[axis] : std::max(bounds.max[axis], point[axis]);
    }

    empty = false;
}

void Extend(WorldBounds & bounds, const WorldBounds & other, bool & empty)
{
    Extend(bounds, other.min, empty);
    Extend(bounds, other.max, empty);
}

// The smallest power-of-two step that keeps every coordinate within s16 once the offset is snapped
// to that step. Power-of-two steps keep integer map coordinates exact.
bool QuantizeStep(const WorldBounds & bounds, std::array<float, 3> & offset, float & step)
{
    float half = 0.0f;

    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        half = std::max(half, (bounds.max[axis] - bounds.min[axis]) * 0.5f);
    }

    for (int exponent = -24; exponent <= 24; ++exponent)
    {
        step = std::ldexp(1.0f, exponent);

        if (half / step + 2.0f <= 32767.0f)
        {
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                offset[axis] = std::round((bounds.min[axis] + bounds.max[axis]) * 0.5f / step) * step;
            }

            return true;
        }
    }

    return false;
}

std::int16_t Quantize(float value, float offset, float step)
{
    const float scaled = std::round((value - offset) / step);
    return static_cast<std::int16_t>(std::clamp(scaled, -32767.0f, 32767.0f));
}

struct Writer
{
    std::vector<std::uint8_t> bytes;

    void U8(std::uint8_t value)
    {
        bytes.push_back(value);
    }

    void U16(std::uint16_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    }

    void U32(std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
        }
    }

    void F32(float value)
    {
        U32(FloatBits(value));
    }

    void Zero(std::size_t count)
    {
        bytes.insert(bytes.end(), count, 0);
    }

    void Bounds(const WorldBounds & bounds, std::uint32_t afterMin, std::uint32_t afterMax)
    {
        F32(bounds.min[0]);
        F32(bounds.min[1]);
        F32(bounds.min[2]);
        U32(afterMin);
        F32(bounds.max[0]);
        F32(bounds.max[1]);
        F32(bounds.max[2]);
        U32(afterMax);
    }
};

class Reader final
{
public:
    explicit Reader(std::span<const std::uint8_t> bytes)
        : m_bytes(bytes)
    {
    }

    std::uint8_t U8()
    {
        return m_bytes[m_offset++];
    }

    std::uint16_t U16()
    {
        const std::uint16_t value = ReadLe16(m_bytes.data() + m_offset);
        m_offset += 2;
        return value;
    }

    std::uint32_t U32()
    {
        const std::uint32_t value = ReadLe32(m_bytes.data() + m_offset);
        m_offset += 4;
        return value;
    }

    float F32()
    {
        return BitsFloat(U32());
    }

    bool Zero(std::size_t count)
    {
        bool zero = true;

        for (std::size_t index = 0; index < count; ++index)
        {
            zero = U8() == 0 && zero;
        }

        return zero;
    }

    void Bounds(WorldBounds & bounds, std::uint32_t & afterMin, std::uint32_t & afterMax)
    {
        bounds.min = { F32(), F32(), F32() };
        afterMin = U32();
        bounds.max = { F32(), F32(), F32() };
        afterMax = U32();
    }

private:
    std::span<const std::uint8_t> m_bytes;
    std::size_t m_offset = 0;
};

// ------------------------------------------------------------------------------------------------
// Strips
// ------------------------------------------------------------------------------------------------

using Triangle = std::array<std::uint8_t, 3>;

class StripBuilder final
{
public:
    explicit StripBuilder(std::span<const Triangle> triangles)
        : m_triangles(triangles)
        , m_done(triangles.size(), false)
    {
        for (std::size_t index = 0; index < triangles.size(); ++index)
        {
            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                m_byEdge[Key(triangles[index][corner], triangles[index][(corner + 1) % 3])].push_back(index);
            }
        }
    }

    std::vector<std::uint8_t> Build(std::size_t & strips)
    {
        std::vector<std::uint8_t> stream;
        strips = 0;

        for (std::size_t remaining = m_triangles.size(); remaining > 0;)
        {
            const std::size_t start = PickStart();
            std::vector<std::size_t> bestUsed;
            std::vector<std::uint8_t> best;

            // Every rotation of the start triangle leads in a different direction; keep the longest.
            for (std::size_t rotation = 0; rotation < 3; ++rotation)
            {
                std::vector<std::size_t> used = { start };
                std::vector<std::uint8_t> strip = { m_triangles[start][rotation], m_triangles[start][(rotation + 1) % 3],
                                                    m_triangles[start][(rotation + 2) % 3] };
                Extend(strip, used);

                if (strip.size() > best.size())
                {
                    best = std::move(strip);
                    bestUsed = std::move(used);
                }
            }

            for (const std::size_t index : bestUsed)
            {
                m_done[index] = true;
            }

            remaining -= bestUsed.size();
            stream.push_back(static_cast<std::uint8_t>(best[0] | kStripIndexAdc));
            stream.push_back(static_cast<std::uint8_t>(best[1] | kStripIndexAdc));
            stream.insert(stream.end(), best.begin() + 2, best.end());
            ++strips;
        }

        return stream;
    }

private:
    static std::uint16_t Key(std::uint8_t from, std::uint8_t to)
    {
        return static_cast<std::uint16_t>((from << 8) | to);
    }

    std::size_t OpenNeighbours(std::size_t index) const
    {
        std::size_t count = 0;

        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            // A consistently wound neighbour holds this edge reversed.
            const auto found = m_byEdge.find(Key(m_triangles[index][(corner + 1) % 3], m_triangles[index][corner]));

            if (found != m_byEdge.end())
            {
                count += static_cast<std::size_t>(std::count_if(found->second.begin(), found->second.end(), [this](std::size_t other) { return !m_done[other]; }));
            }
        }

        return count;
    }

    // Start where the fewest neighbours remain, so strips do not strand single triangles.
    std::size_t PickStart() const
    {
        std::size_t best = m_triangles.size();
        std::size_t bestCount = std::numeric_limits<std::size_t>::max();

        for (std::size_t index = 0; index < m_triangles.size(); ++index)
        {
            if (m_done[index])
            {
                continue;
            }

            const std::size_t count = OpenNeighbours(index);

            if (count < bestCount)
            {
                best = index;
                bestCount = count;
            }
        }

        return best;
    }

    void Extend(std::vector<std::uint8_t> & strip, std::vector<std::size_t> & used) const
    {
        for (;;)
        {
            const std::size_t next = strip.size() - 2;
            const std::uint8_t previous = strip[strip.size() - 2];
            const std::uint8_t last = strip[strip.size() - 1];
            // Triangle k is (k, k+1, k+2) for even k and (k+1, k, k+2) for odd k in id's winding.
            const bool even = (next & 1U) == 0;
            const std::uint8_t from = even ? previous : last;
            const std::uint8_t to = even ? last : previous;
            const auto found = m_byEdge.find(Key(from, to));

            if (found == m_byEdge.end())
            {
                return;
            }

            bool extended = false;

            for (const std::size_t candidate : found->second)
            {
                if (m_done[candidate] || std::find(used.begin(), used.end(), candidate) != used.end())
                {
                    continue;
                }

                const Triangle & triangle = m_triangles[candidate];
                const std::size_t corner = triangle[0] == to ? 0 : triangle[1] == to ? 1 : 2;
                strip.push_back(triangle[(corner + 1) % 3]);
                used.push_back(candidate);
                extended = true;
                break;
            }

            if (!extended)
            {
                return;
            }
        }
    }

    std::span<const Triangle> m_triangles;
    std::vector<bool> m_done;
    std::map<std::uint16_t, std::vector<std::size_t>> m_byEdge;
};

// ------------------------------------------------------------------------------------------------
// Batches
// ------------------------------------------------------------------------------------------------

struct SourceTriangle
{
    std::array<std::uint32_t, 3> corners = {};
    std::array<float, 3> centroid = {};
};

struct Fit
{
    std::vector<std::uint32_t> vertices;
    std::vector<Triangle> triangles;
    std::vector<std::uint8_t> stream;
    std::size_t strips = 0;
};

int InputQwords(WorldTopology topology, std::size_t vertices, std::size_t indices)
{
    if (topology == WorldTopology::Strips)
    {
        return kWorldHeaderQwords + kWorldVertexQwords * static_cast<int>(indices);
    }

    return kWorldHeaderQwords + static_cast<int>((indices + 3) / 4) + kWorldVertexQwords * static_cast<int>(vertices);
}

bool TryFit(const std::vector<SourceTriangle> & triangles, std::span<const std::size_t> run, WorldTopology topology, Fit & fit)
{
    fit = {};
    std::map<std::uint32_t, std::uint8_t> local;

    for (const std::size_t index : run)
    {
        Triangle triangle = {};

        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const std::uint32_t source = triangles[index].corners[corner];
            const auto found = local.find(source);

            if (found != local.end())
            {
                triangle[corner] = found->second;
                continue;
            }

            if (fit.vertices.size() >= 127)
            {
                return false;
            }

            triangle[corner] = static_cast<std::uint8_t>(fit.vertices.size());
            local.emplace(source, triangle[corner]);
            fit.vertices.push_back(source);
        }

        fit.triangles.push_back(triangle);
    }

    if (topology == WorldTopology::IndexedStrips && fit.vertices.size() > static_cast<std::size_t>(kWorldMaxIndexedVertices))
    {
        return false;
    }

    fit.stream = BuildStrips(fit.triangles, fit.strips);
    return fit.stream.size() <= static_cast<std::size_t>(kWorldMaxStripIndices) &&
           InputQwords(topology, fit.vertices.size(), fit.stream.size()) <= kWorldInputHalfQwords;
}

// A breadth-first walk over shared edges, falling back to the nearest remaining triangle, so each
// batch is a compact piece of the surface.
std::vector<std::size_t> LocalityOrder(const std::vector<SourceTriangle> & triangles)
{
    std::map<std::uint64_t, std::vector<std::size_t>> byEdge;

    for (std::size_t index = 0; index < triangles.size(); ++index)
    {
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const std::uint64_t a = triangles[index].corners[corner];
            const std::uint64_t b = triangles[index].corners[(corner + 1) % 3];
            byEdge[(std::min(a, b) << 32) | std::max(a, b)].push_back(index);
        }
    }

    std::vector<std::size_t> order;
    std::vector<bool> queued(triangles.size(), false);
    std::size_t head = 0;

    while (order.size() < triangles.size())
    {
        if (head == order.size())
        {
            std::size_t pick = triangles.size();
            float bestDistance = std::numeric_limits<float>::max();
            const std::array<float, 3> from = order.empty() ? triangles[0].centroid : triangles[order.back()].centroid;

            for (std::size_t index = 0; index < triangles.size(); ++index)
            {
                if (queued[index])
                {
                    continue;
                }

                float distance = 0.0f;

                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    const float delta = triangles[index].centroid[axis] - from[axis];
                    distance += delta * delta;
                }

                if (distance < bestDistance)
                {
                    pick = index;
                    bestDistance = distance;
                }
            }

            queued[pick] = true;
            order.push_back(pick);
        }

        const std::size_t current = order[head++];

        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const std::uint64_t a = triangles[current].corners[corner];
            const std::uint64_t b = triangles[current].corners[(corner + 1) % 3];

            for (const std::size_t neighbour : byEdge[(std::min(a, b) << 32) | std::max(a, b)])
            {
                if (!queued[neighbour])
                {
                    queued[neighbour] = true;
                    order.push_back(neighbour);
                }
            }
        }
    }

    return order;
}

std::array<float, 3> MaterialColor(const WorldPolicy & policy, const std::string & material)
{
    for (const WorldMaterialColor & entry : policy.colors)
    {
        if (entry.material == material)
        {
            return entry.color;
        }
    }

    return policy.defaultColor;
}

std::uint8_t ColorByte(float value)
{
    // 0x80 is 1.0 for the GS.
    return static_cast<std::uint8_t>(std::clamp(std::round(value * 128.0f), 0.0f, 255.0f));
}

class Cook final
{
public:
    Cook(const WorldPolicy & policy, World & world, WorldCookStats & stats, std::string & error)
        : m_policy(policy)
        , m_world(world)
        , m_stats(stats)
        , m_error(error)
    {
        const float length = std::sqrt(policy.light[0] * policy.light[0] + policy.light[1] * policy.light[1] + policy.light[2] * policy.light[2]);

        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            m_light[axis] = length > 0.0f ? policy.light[axis] / length : 0.0f;
        }
    }

    bool Fail(const std::string & message)
    {
        m_error = message;
        return false;
    }

    bool Run(const ProcFile & proc)
    {
        for (const ProcModel & model : proc.models)
        {
            for (const ProcSurface & surface : model.surfaces)
            {
                m_materialIndex.emplace(surface.material, std::uint16_t(0));
            }
        }

        if (m_materialIndex.size() > 0xFFFFU)
        {
            return Fail("too many materials");
        }

        for (auto & [name, index] : m_materialIndex)
        {
            index = static_cast<std::uint16_t>(m_world.materials.size());
            m_world.materials.push_back(name);
        }

        std::size_t areaModels = 0;

        for (int area = 0; area < proc.areaCount; ++area)
        {
            const ProcModel * model = FindAreaModel(proc, area);

            if (model == nullptr || !Area(*model, static_cast<std::uint32_t>(area)))
            {
                return m_error.empty() ? Fail("missing area model") : false;
            }

            ++areaModels;
        }

        m_stats.skippedModels = proc.models.size() - areaModels;

        for (const ProcPortal & portal : proc.portals)
        {
            WorldPortal record;
            record.firstPoint = static_cast<std::uint32_t>(m_world.portalPoints.size());
            record.pointCount = static_cast<std::uint32_t>(portal.points.size());
            record.areas[0] = static_cast<std::uint32_t>(portal.areas[0]);
            record.areas[1] = static_cast<std::uint32_t>(portal.areas[1]);
            m_world.portalPoints.insert(m_world.portalPoints.end(), portal.points.begin(), portal.points.end());
            m_world.portals.push_back(record);
        }

        for (const ProcNode & node : proc.nodes)
        {
            WorldNode record;
            record.plane = node.plane;
            record.children[0] = node.children[0];
            record.children[1] = node.children[1];
            m_world.nodes.push_back(record);
        }

        m_stats.batches = m_world.batches.size();
        m_stats.packetBytes = m_world.packets.size();
        return true;
    }

private:
    bool Area(const ProcModel & model, std::uint32_t areaIndex)
    {
        WorldArea area;
        area.firstSurface = static_cast<std::uint32_t>(m_world.surfaces.size());
        area.firstBatch = static_cast<std::uint32_t>(m_world.batches.size());

        WorldBounds areaBounds;
        bool empty = true;

        for (const ProcSurface & surface : model.surfaces)
        {
            for (const ProcVertex & vertex : surface.vertices)
            {
                Extend(areaBounds, vertex.position, empty);
            }
        }

        if (!QuantizeStep(areaBounds, area.mirrorOffset, area.mirrorScale))
        {
            return Fail("area " + std::to_string(areaIndex) + " is too large for the s16 mirror");
        }

        bool areaEmpty = true;

        for (const ProcSurface & surface : model.surfaces)
        {
            if (!Surface(surface, areaIndex, area))
            {
                return false;
            }

            if (!surface.vertices.empty())
            {
                Extend(area.bounds, m_world.surfaces.back().bounds, areaEmpty);
            }
        }

        area.surfaceCount = static_cast<std::uint32_t>(m_world.surfaces.size()) - area.firstSurface;
        area.batchCount = static_cast<std::uint32_t>(m_world.batches.size()) - area.firstBatch;
        m_world.areas.push_back(area);
        return true;
    }

    bool Surface(const ProcSurface & surface, std::uint32_t areaIndex, const WorldArea & area)
    {
        WorldSurface record;
        record.material = m_materialIndex.at(surface.material);
        record.area = areaIndex;
        record.firstBatch = static_cast<std::uint32_t>(m_world.batches.size());
        record.firstVertex = static_cast<std::uint32_t>(m_world.mirrorPositions.size());
        record.firstIndex = static_cast<std::uint32_t>(m_world.mirrorIndices.size());

        bool empty = true;

        for (const ProcVertex & vertex : surface.vertices)
        {
            Extend(record.bounds, vertex.position, empty);
        }

        // The mirror: positions merged once quantized, every source triangle kept.
        std::map<std::array<std::int16_t, 3>, std::uint16_t> mirrorLookup;
        std::vector<std::uint16_t> mirrorOf(surface.vertices.size());

        for (std::size_t index = 0; index < surface.vertices.size(); ++index)
        {
            const std::array<float, 3> & position = surface.vertices[index].position;
            const std::array<std::int16_t, 3> quantized = { Quantize(position[0], area.mirrorOffset[0], area.mirrorScale),
                                                            Quantize(position[1], area.mirrorOffset[1], area.mirrorScale),
                                                            Quantize(position[2], area.mirrorOffset[2], area.mirrorScale) };
            const auto [found, inserted] = mirrorLookup.emplace(quantized, static_cast<std::uint16_t>(mirrorLookup.size()));

            if (inserted)
            {
                m_world.mirrorPositions.push_back(quantized);
            }

            mirrorOf[index] = found->second;
        }

        std::vector<SourceTriangle> triangles;

        for (std::size_t index = 0; index + 2 < surface.indices.size(); index += 3)
        {
            SourceTriangle triangle;
            triangle.corners = { surface.indices[index], surface.indices[index + 1], surface.indices[index + 2] };

            // Repeated indices draw nothing; id's loader keeps them, the GS packet does not.
            if (triangle.corners[0] == triangle.corners[1] || triangle.corners[1] == triangle.corners[2] || triangle.corners[0] == triangle.corners[2])
            {
                continue;
            }

            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                triangle.centroid[axis] = (surface.vertices[triangle.corners[0]].position[axis] + surface.vertices[triangle.corners[1]].position[axis] +
                                           surface.vertices[triangle.corners[2]].position[axis]) / 3.0f;
            }

            triangles.push_back(triangle);

            for (const std::uint32_t corner : triangle.corners)
            {
                m_world.mirrorIndices.push_back(mirrorOf[corner]);
            }
        }

        record.vertexCount = static_cast<std::uint32_t>(m_world.mirrorPositions.size()) - record.firstVertex;
        record.indexCount = static_cast<std::uint32_t>(m_world.mirrorIndices.size()) - record.firstIndex;
        m_stats.triangles += triangles.size();

        const std::vector<std::size_t> order = triangles.empty() ? std::vector<std::size_t>() : LocalityOrder(triangles);

        for (std::size_t start = 0; start < order.size();)
        {
            const std::span<const std::size_t> rest(order.data() + start, order.size() - start);
            Fit fit;
            std::size_t low = 1;
            std::size_t high = rest.size();

            // Batches are runs of the locality order; take the longest run that fits a half.
            while (low < high)
            {
                const std::size_t middle = (low + high + 1) / 2;

                if (TryFit(triangles, rest.first(middle), m_policy.topology, fit))
                {
                    low = middle;
                }
                else
                {
                    high = middle - 1;
                }
            }

            if (!TryFit(triangles, rest.first(low), m_policy.topology, fit))
            {
                return Fail("a triangle does not fit a VU1 input half");
            }

            if (!Batch(surface, record, fit))
            {
                return false;
            }

            start += low;
        }

        record.batchCount = static_cast<std::uint32_t>(m_world.batches.size()) - record.firstBatch;
        m_world.surfaces.push_back(record);
        return true;
    }

    bool Batch(const ProcSurface & surface, WorldSurface & record, const Fit & fit)
    {
        WorldBatch batch;
        batch.material = static_cast<std::uint16_t>(record.material);
        batch.flags = kBatchLayoutWorldLightmapped | (m_policy.topology == WorldTopology::IndexedStrips ? kBatchFlagIndexedStrips : 0U);

        WorldBounds source;
        bool empty = true;
        std::array<float, 2> uvMin = {};
        std::array<float, 2> uvMax = {};

        for (std::size_t index = 0; index < fit.vertices.size(); ++index)
        {
            const ProcVertex & vertex = surface.vertices[fit.vertices[index]];

            for (std::size_t axis = 0; axis < 2; ++axis)
            {
                uvMin[axis] = index == 0 ? vertex.uv[axis] : std::min(uvMin[axis], vertex.uv[axis]);
                uvMax[axis] = index == 0 ? vertex.uv[axis] : std::max(uvMax[axis], vertex.uv[axis]);
            }

            Extend(source, vertex.position, empty);
        }

        if (!QuantizeStep(source, batch.positionOffset, batch.positionScale))
        {
            return Fail("batch too large for s16 positions");
        }

        // UVs keep 4.12 (the plan's format) unless the batch's range needs a coarser step; the
        // offset is whole so texture wrapping is unchanged.
        std::array<float, 2> uvOffset = {};
        float uvRange = 0.0f;

        for (std::size_t axis = 0; axis < 2; ++axis)
        {
            uvOffset[axis] = std::round((uvMin[axis] + uvMax[axis]) * 0.5f);
            uvRange = std::max({ uvRange, std::fabs(uvMax[axis] - uvOffset[axis]), std::fabs(uvMin[axis] - uvOffset[axis]) });
        }

        int uvBits = 12;

        while (uvBits >= 0 && uvRange * std::ldexp(1.0f, uvBits) > 32767.0f)
        {
            --uvBits;
        }

        if (uvBits < 0)
        {
            return Fail("UV range too large in " + surface.material);
        }

        const float uvScale = std::ldexp(1.0f, -uvBits);
        const std::array<float, 3> base = MaterialColor(m_policy, surface.material);

        struct Encoded
        {
            std::array<std::int16_t, 4> position;
            std::array<std::int16_t, 4> uv;
            std::array<std::uint8_t, 4> color;
        };

        std::vector<Encoded> encoded(fit.vertices.size());
        bool boundsEmpty = true;

        for (std::size_t index = 0; index < fit.vertices.size(); ++index)
        {
            const ProcVertex & vertex = surface.vertices[fit.vertices[index]];
            Encoded & output = encoded[index];
            std::array<float, 3> decoded = {};

            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                output.position[axis] = Quantize(vertex.position[axis], batch.positionOffset[axis], batch.positionScale);
                decoded[axis] = static_cast<float>(output.position[axis]) * batch.positionScale + batch.positionOffset[axis];
                m_stats.maxPositionError = std::max(m_stats.maxPositionError, std::fabs(decoded[axis] - vertex.position[axis]));
            }

            Extend(batch.bounds, decoded, boundsEmpty);
            output.position[3] = 0;

            for (std::size_t axis = 0; axis < 2; ++axis)
            {
                output.uv[axis] = static_cast<std::int16_t>(std::round((vertex.uv[axis] - uvOffset[axis]) / uvScale));
                const float uv = static_cast<float>(output.uv[axis]) * uvScale + uvOffset[axis];
                m_stats.maxUvError = std::max(m_stats.maxUvError, std::fabs(uv - vertex.uv[axis]));
            }

            output.uv[2] = 0;
            output.uv[3] = 0;

            const float facing = vertex.normal[0] * m_light[0] + vertex.normal[1] * m_light[1] + vertex.normal[2] * m_light[2];
            const float shade = m_policy.ambient + (1.0f - m_policy.ambient) * std::max(0.0f, facing);
            output.color = { ColorByte(base[0] * shade), ColorByte(base[1] * shade), ColorByte(base[2] * shade), 0x80 };
        }

        // The surface's bounds already hold its source vertices; add what the batch draws.
        bool recordEmpty = false;
        Extend(record.bounds, batch.bounds, recordEmpty);

        // The vertex stream: unique vertices for indexed strips, strip order for plain strips.
        std::vector<Encoded> stream;
        std::vector<std::uint8_t> indices;

        if (m_policy.topology == WorldTopology::IndexedStrips)
        {
            stream = encoded;
            indices = fit.stream;
        }
        else
        {
            for (const std::uint8_t entry : fit.stream)
            {
                Encoded vertex = encoded[entry & 0x7FU];
                vertex.position[3] = (entry & kStripIndexAdc) != 0 ? kStripVertexAdc : std::int16_t(0);
                stream.push_back(vertex);
            }
        }

        const std::uint32_t count = static_cast<std::uint32_t>(stream.size());
        const std::uint32_t indexQwords = static_cast<std::uint32_t>((indices.size() + 3) / 4);
        std::vector<std::uint32_t> words;

        words.push_back(VifStcycl(1, 1));
        words.push_back(VifUnpack(kUnpackV4_32, 2, 0, false));
        words.push_back(FloatBits(batch.positionOffset[0]));
        words.push_back(FloatBits(batch.positionOffset[1]));
        words.push_back(FloatBits(batch.positionOffset[2]));
        words.push_back(indexQwords);
        words.push_back(FloatBits(batch.positionScale));
        words.push_back(FloatBits(uvScale));
        words.push_back(FloatBits(uvOffset[0]));
        words.push_back(FloatBits(uvOffset[1]));

        const std::uint32_t vertexBase = 2U + indexQwords;

        if (!indices.empty())
        {
            words.push_back(VifUnpack(kUnpackV4_16, indexQwords, 2, true));

            // Each entry is the vertex's qword offset in the input half with the ADC flag in bit 15:
            // VU1 has no integer multiply or shift, so the program adds it to XTOP as it stands.
            // Padding repeats the first vertex with ADC, which draws nothing.
            for (std::size_t index = 0; index < indexQwords * 4; index += 2)
            {
                std::uint16_t pair[2] = {};

                for (std::size_t lane = 0; lane < 2; ++lane)
                {
                    const bool real = index + lane < indices.size();
                    const std::uint32_t entry = real ? indices[index + lane] : kStripIndexAdc;
                    const std::uint32_t offset = vertexBase + static_cast<std::uint32_t>(kWorldVertexQwords) * (entry & 0x7FU);
                    pair[lane] = static_cast<std::uint16_t>(offset | ((entry & kStripIndexAdc) != 0 ? kStripEntryAdc : 0U));
                }

                words.push_back(static_cast<std::uint32_t>(pair[0]) | (static_cast<std::uint32_t>(pair[1]) << 16));
            }
        }

        words.push_back(VifStcycl(static_cast<std::uint32_t>(kWorldVertexQwords), 1));
        words.push_back(VifUnpack(kUnpackV4_16, count, vertexBase, false));

        for (const Encoded & vertex : stream)
        {
            words.push_back(Pack16(vertex.position[0], vertex.position[1]));
            words.push_back(Pack16(vertex.position[2], vertex.position[3]));
        }

        words.push_back(VifUnpack(kUnpackV4_16, count, vertexBase + 1, false));

        for (const Encoded & vertex : stream)
        {
            words.push_back(Pack16(vertex.uv[0], vertex.uv[1]));
            words.push_back(Pack16(vertex.uv[2], vertex.uv[3]));
        }

        words.push_back(VifUnpack(kUnpackV4_8, count, vertexBase + 2, true));

        for (const Encoded & vertex : stream)
        {
            words.push_back(static_cast<std::uint32_t>(vertex.color[0]) | (static_cast<std::uint32_t>(vertex.color[1]) << 8) |
                            (static_cast<std::uint32_t>(vertex.color[2]) << 16) | (static_cast<std::uint32_t>(vertex.color[3]) << 24));
        }

        words.push_back(VifCode(kVifItop, 0, count));

        while (words.size() % 4 != 0)
        {
            words.push_back(VifCode(kVifNop, 0, 0));
        }

        batch.vertexCount = count;
        batch.indexCount = static_cast<std::uint16_t>(indices.size());
        batch.packetOffset = static_cast<std::uint32_t>(m_world.packets.size());
        batch.packetQwords = static_cast<std::uint32_t>(words.size() / 4);

        for (const std::uint32_t word : words)
        {
            for (int shift = 0; shift < 32; shift += 8)
            {
                m_world.packets.push_back(static_cast<std::uint8_t>((word >> shift) & 0xFFU));
            }
        }

        m_world.batches.push_back(batch);
        m_stats.transformedVertices += fit.vertices.size();
        m_stats.emittedVertices += fit.stream.size();
        m_stats.strips += fit.strips;
        return true;
    }

    const WorldPolicy & m_policy;
    World & m_world;
    WorldCookStats & m_stats;
    std::string & m_error;
    std::array<float, 3> m_light = {};
    std::map<std::string, std::uint16_t> m_materialIndex;
};

// ------------------------------------------------------------------------------------------------
// Packet validation
// ------------------------------------------------------------------------------------------------

bool CheckPacket(std::span<const std::uint8_t> packet, const WorldBatch & batch, std::string & error)
{
    const auto fail = [&error](const char * message)
    {
        error = std::string("batch packet: ") + message;
        return false;
    };

    std::vector<std::uint32_t> words(packet.size() / 4);

    for (std::size_t index = 0; index < words.size(); ++index)
    {
        words[index] = ReadLe32(packet.data() + index * 4);
    }

    const bool indexed = (batch.flags & kBatchFlagIndexedStrips) != 0;
    const std::uint32_t count = batch.vertexCount;
    const std::uint32_t indexCount = batch.indexCount;
    const std::uint32_t indexQwords = (indexCount + 3U) / 4U;

    if (indexed != (indexCount != 0) || count == 0 || (indexed && count > static_cast<std::uint32_t>(kWorldMaxIndexedVertices)) ||
        static_cast<int>(2U + indexQwords + 3U * count) > kWorldInputHalfQwords)
    {
        return fail("counts exceed the VU1 input half");
    }

    std::size_t at = 0;
    const auto take = [&words, &at](std::uint32_t expected)
    {
        return at < words.size() && words[at++] == expected;
    };

    if (!take(VifStcycl(1, 1)) || !take(VifUnpack(kUnpackV4_32, 2, 0, false)) || at + 8 > words.size())
    {
        return fail("bad header unpack");
    }

    if (words[at] != FloatBits(batch.positionOffset[0]) || words[at + 1] != FloatBits(batch.positionOffset[1]) ||
        words[at + 2] != FloatBits(batch.positionOffset[2]) || words[at + 3] != indexQwords || words[at + 4] != FloatBits(batch.positionScale))
    {
        return fail("VU header disagrees with the batch header");
    }

    const float uvScale = BitsFloat(words[at + 5]);

    if (!(uvScale > 0.0f) || !std::isfinite(BitsFloat(words[at + 6])) || !std::isfinite(BitsFloat(words[at + 7])))
    {
        return fail("bad UV transform");
    }

    at += 8;

    const std::uint32_t base = 2U + indexQwords;

    if (indexed)
    {
        if (!take(VifUnpack(kUnpackV4_16, indexQwords, 2, true)) || at + 2U * indexQwords > words.size())
        {
            return fail("bad index unpack");
        }

        std::uint32_t run = 0;

        for (std::uint32_t index = 0; index < indexQwords * 4U; ++index)
        {
            const std::uint32_t entry = (words[at + index / 2U] >> (16U * (index % 2U))) & 0xFFFFU;
            const std::uint32_t offset = entry & ~kStripEntryAdc;

            if (index >= indexCount)
            {
                if (entry != (kStripEntryAdc | base))
                {
                    return fail("bad index padding");
                }

                continue;
            }

            if (offset < base || (offset - base) % 3U != 0 || (offset - base) / 3U >= count)
            {
                return fail("strip index out of range");
            }

            // Two ADC entries open a strip, which then closes at least one triangle.
            const bool adc = (entry & kStripEntryAdc) != 0;

            if ((adc && run == 2) || (!adc && run < 2))
            {
                return fail("malformed strip");
            }

            run = adc ? (run == 1 ? 2U : 1U) : run + 1U;
        }

        if (run < 3 && indexCount > 0)
        {
            return fail("unfinished strip");
        }

        at += 2U * indexQwords;
    }

    if (!take(VifStcycl(3, 1)) || !take(VifUnpack(kUnpackV4_16, count, base, false)) || at + 2U * count > words.size())
    {
        return fail("bad position unpack");
    }

    for (std::uint32_t vertex = 0; vertex < count; ++vertex)
    {
        const std::uint32_t w = words[at + 2U * vertex + 1U] >> 16;

        if (indexed ? w != 0 : (w != 0 && w != 0x8000U))
        {
            return fail("bad position flag");
        }
    }

    at += 2U * count;

    if (!take(VifUnpack(kUnpackV4_16, count, base + 1U, false)) || at + 2U * count > words.size())
    {
        return fail("bad UV unpack");
    }

    at += 2U * count;

    if (!take(VifUnpack(kUnpackV4_8, count, base + 2U, true)) || at + count > words.size())
    {
        return fail("bad colour unpack");
    }

    at += count;

    if (!take(VifCode(kVifItop, 0, count)))
    {
        return fail("bad packet tail");
    }

    while (at < words.size())
    {
        if (!take(VifCode(kVifNop, 0, 0)))
        {
            return fail("bad packet padding");
        }
    }

    return at % 4 == 0 || fail("packet not qword sized");
}

std::size_t Align(std::size_t value, std::size_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Public interface
// ------------------------------------------------------------------------------------------------

std::vector<std::uint8_t> BuildStrips(std::span<const std::array<std::uint8_t, 3>> triangles, std::size_t & strips)
{
    StripBuilder builder(triangles);
    return builder.Build(strips);
}

std::vector<std::array<std::uint8_t, 3>> WalkStrips(std::span<const std::uint8_t> stream)
{
    std::vector<std::array<std::uint8_t, 3>> triangles;
    std::uint8_t window[2] = {};
    std::size_t run = 0;

    for (const std::uint8_t entry : stream)
    {
        const std::uint8_t vertex = static_cast<std::uint8_t>(entry & 0x7FU);

        if ((entry & kStripIndexAdc) != 0)
        {
            run = run == 1 ? 2 : 1;
        }
        else if (run >= 2)
        {
            const bool even = (run & 1U) == 0;
            triangles.push_back(even ? std::array<std::uint8_t, 3>{ window[0], window[1], vertex } : std::array<std::uint8_t, 3>{ window[1], window[0], vertex });
            ++run;
        }

        window[0] = window[1];
        window[1] = vertex;
    }

    return triangles;
}

bool CookWorld(const ProcFile & proc, const WorldPolicy & policy, World & output, WorldCookStats & stats, std::string & error)
{
    output = {};
    stats = {};
    error.clear();
    Cook cook(policy, output, stats, error);
    return cook.Run(proc);
}

bool WriteD3wd(const World & world, std::vector<std::uint8_t> & file, std::string & error)
{
    Writer sections[kD3wdSectionCount];
    std::uint32_t counts[kD3wdSectionCount] = {};

    for (const std::string & name : world.materials)
    {
        Writer & material = sections[static_cast<int>(D3wdSection::Materials)];
        Writer & strings = sections[static_cast<int>(D3wdSection::Strings)];
        material.U32(static_cast<std::uint32_t>(strings.bytes.size()));
        material.U32(static_cast<std::uint32_t>(name.size()));
        strings.bytes.insert(strings.bytes.end(), name.begin(), name.end());
        strings.U8(0);
    }

    counts[static_cast<int>(D3wdSection::Strings)] = static_cast<std::uint32_t>(world.materials.size());

    for (const WorldArea & area : world.areas)
    {
        Writer & out = sections[static_cast<int>(D3wdSection::Areas)];
        out.Bounds(area.bounds, area.firstSurface, area.surfaceCount);
        out.F32(area.mirrorOffset[0]);
        out.F32(area.mirrorOffset[1]);
        out.F32(area.mirrorOffset[2]);
        out.F32(area.mirrorScale);
        out.U32(area.firstBatch);
        out.U32(area.batchCount);
        out.Zero(8);
    }

    for (const WorldSurface & surface : world.surfaces)
    {
        Writer & out = sections[static_cast<int>(D3wdSection::Surfaces)];
        out.Bounds(surface.bounds, surface.material, surface.area);
        out.U32(surface.firstBatch);
        out.U32(surface.batchCount);
        out.U32(surface.firstVertex);
        out.U32(surface.vertexCount);
        out.U32(surface.firstIndex);
        out.U32(surface.indexCount);
        out.Zero(8);
    }

    for (const WorldPortal & portal : world.portals)
    {
        Writer & out = sections[static_cast<int>(D3wdSection::Portals)];
        out.U32(portal.firstPoint);
        out.U32(portal.pointCount);
        out.U32(portal.areas[0]);
        out.U32(portal.areas[1]);
    }

    for (const std::array<float, 3> & point : world.portalPoints)
    {
        Writer & out = sections[static_cast<int>(D3wdSection::PortalPoints)];
        out.F32(point[0]);
        out.F32(point[1]);
        out.F32(point[2]);
    }

    for (const WorldNode & node : world.nodes)
    {
        Writer & out = sections[static_cast<int>(D3wdSection::Nodes)];

        for (const float value : node.plane)
        {
            out.F32(value);
        }

        out.U32(static_cast<std::uint32_t>(node.children[0]));
        out.U32(static_cast<std::uint32_t>(node.children[1]));
    }

    for (const WorldBatch & batch : world.batches)
    {
        Writer & out = sections[static_cast<int>(D3wdSection::Batches)];
        out.Bounds(batch.bounds, batch.flags, batch.vertexCount);
        out.F32(batch.positionOffset[0]);
        out.F32(batch.positionOffset[1]);
        out.F32(batch.positionOffset[2]);
        out.F32(batch.positionScale);
        out.U32(batch.packetOffset);
        out.U32(batch.packetQwords);
        out.U16(batch.material);
        out.U16(batch.lightmapPage);
        out.U16(batch.indexCount);
        out.U16(0);
    }

    for (const std::array<std::int16_t, 3> & position : world.mirrorPositions)
    {
        Writer & out = sections[static_cast<int>(D3wdSection::MirrorPositions)];

        for (const std::int16_t value : position)
        {
            out.U16(static_cast<std::uint16_t>(value));
        }
    }

    for (const std::uint16_t index : world.mirrorIndices)
    {
        sections[static_cast<int>(D3wdSection::MirrorIndices)].U16(index);
    }

    sections[static_cast<int>(D3wdSection::Packets)].bytes = world.packets;

    if (world.packets.size() % 16 != 0)
    {
        error = "packets are not qword sized";
        return false;
    }

    counts[static_cast<int>(D3wdSection::Packets)] = static_cast<std::uint32_t>(world.packets.size() / 16);

    for (int section = 1; section < kD3wdSectionCount - 1; ++section)
    {
        counts[section] = static_cast<std::uint32_t>(sections[section].bytes.size() / kRecordBytes[section]);
    }

    std::size_t offset = kD3wdHeaderBytes + kD3wdDirectoryRecordBytes * static_cast<std::size_t>(kD3wdSectionCount);
    std::size_t offsets[kD3wdSectionCount] = {};

    for (int section = 0; section < kD3wdSectionCount; ++section)
    {
        offset = Align(offset, section == static_cast<int>(D3wdSection::Packets) ? kD3wdPacketAlignment : 16);
        offsets[section] = offset;
        offset += sections[section].bytes.size();
    }

    const std::size_t fileBytes = Align(offset, 16);

    if (fileBytes > std::numeric_limits<std::uint32_t>::max())
    {
        error = "world file too large";
        return false;
    }

    Writer header;
    header.U32(kD3wdMagic);
    header.U16(kD3wdVersion);
    header.U16(static_cast<std::uint16_t>(kD3wdHeaderBytes));
    header.U16(static_cast<std::uint16_t>(kD3wdSectionCount));
    header.U16(0);
    header.U32(static_cast<std::uint32_t>(fileBytes));
    header.U32(0);
    header.U32(kD3wdConverterVersion);
    header.Zero(8);

    for (int section = 0; section < kD3wdSectionCount; ++section)
    {
        header.U32(static_cast<std::uint32_t>(offsets[section]));
        header.U32(static_cast<std::uint32_t>(sections[section].bytes.size()));
        header.U32(counts[section]);
        header.U32(Fnv1a32(sections[section].bytes));
    }

    WriteLe32(header.bytes.data() + 16, Fnv1a32(header.bytes));

    file.assign(fileBytes, 0);
    std::copy(header.bytes.begin(), header.bytes.end(), file.begin());

    for (int section = 0; section < kD3wdSectionCount; ++section)
    {
        std::copy(sections[section].bytes.begin(), sections[section].bytes.end(), file.begin() + static_cast<std::ptrdiff_t>(offsets[section]));
    }

    return true;
}

bool ReadD3wd(std::span<const std::uint8_t> file, World & world, std::string & error)
{
    world = {};
    const auto fail = [&error](const std::string & message)
    {
        error = "D3WD: " + message;
        return false;
    };

    const std::size_t directoryEnd = kD3wdHeaderBytes + kD3wdDirectoryRecordBytes * static_cast<std::size_t>(kD3wdSectionCount);

    if (file.size() < directoryEnd)
    {
        return fail("truncated header");
    }

    Reader header(file);
    const std::uint32_t magic = header.U32();
    const std::uint16_t version = header.U16();
    const std::uint16_t headerBytes = header.U16();
    const std::uint16_t sectionCount = header.U16();
    const std::uint16_t flags = header.U16();
    const std::uint32_t fileBytes = header.U32();
    const std::uint32_t checksum = header.U32();
    const std::uint32_t converter = header.U32();

    if (magic != kD3wdMagic || version != kD3wdVersion || headerBytes != kD3wdHeaderBytes || sectionCount != kD3wdSectionCount || flags != 0 ||
        converter != kD3wdConverterVersion || !header.Zero(8))
    {
        return fail("unsupported header");
    }

    if (fileBytes != file.size() || fileBytes % 16 != 0)
    {
        return fail("file size mismatch");
    }

    std::vector<std::uint8_t> headerCopy(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(directoryEnd));
    WriteLe32(headerCopy.data() + 16, 0);

    if (Fnv1a32(headerCopy) != checksum)
    {
        return fail("header checksum mismatch");
    }

    std::span<const std::uint8_t> sections[kD3wdSectionCount];
    std::uint32_t counts[kD3wdSectionCount] = {};
    std::size_t expected = directoryEnd;

    for (int section = 0; section < kD3wdSectionCount; ++section)
    {
        const std::uint32_t offset = header.U32();
        const std::uint32_t bytes = header.U32();
        counts[section] = header.U32();
        const std::uint32_t sectionChecksum = header.U32();
        const std::size_t previousEnd = expected;
        expected = Align(expected, section == static_cast<int>(D3wdSection::Packets) ? kD3wdPacketAlignment : 16);

        if (offset != expected || offset > file.size() || bytes > file.size() - offset)
        {
            return fail("section " + std::to_string(section) + " out of place");
        }

        for (std::size_t pad = previousEnd; pad < offset; ++pad)
        {
            if (file[pad] != 0)
            {
                return fail("nonzero padding");
            }
        }

        sections[section] = file.subspan(offset, bytes);
        expected = offset + bytes;

        if (Fnv1a32(sections[section]) != sectionChecksum)
        {
            return fail("section " + std::to_string(section) + " checksum mismatch");
        }

        if (kRecordBytes[section] != 0 && static_cast<std::size_t>(counts[section]) * kRecordBytes[section] != bytes)
        {
            return fail("section " + std::to_string(section) + " size mismatch");
        }
    }

    if (Align(expected, 16) != file.size())
    {
        return fail("trailing bytes");
    }

    for (std::size_t pad = expected; pad < file.size(); ++pad)
    {
        if (file[pad] != 0)
        {
            return fail("nonzero padding");
        }
    }

    const std::span<const std::uint8_t> strings = sections[static_cast<int>(D3wdSection::Strings)];
    const std::span<const std::uint8_t> packets = sections[static_cast<int>(D3wdSection::Packets)];

    if (packets.size() != static_cast<std::size_t>(counts[static_cast<int>(D3wdSection::Packets)]) * 16 ||
        counts[static_cast<int>(D3wdSection::Strings)] != counts[static_cast<int>(D3wdSection::Materials)])
    {
        return fail("bad string or packet count");
    }

    Reader materials(sections[static_cast<int>(D3wdSection::Materials)]);
    std::size_t stringEnd = 0;

    for (std::uint32_t index = 0; index < counts[static_cast<int>(D3wdSection::Materials)]; ++index)
    {
        const std::uint32_t offset = materials.U32();
        const std::uint32_t bytes = materials.U32();

        // Canonical: names packed in order, each NUL-terminated.
        if (offset != stringEnd || bytes == 0 || bytes >= strings.size() - offset || strings[offset + bytes] != 0)
        {
            return fail("bad material name");
        }

        world.materials.emplace_back(reinterpret_cast<const char *>(strings.data() + offset), bytes);

        if (world.materials.back().find('\0') != std::string::npos)
        {
            return fail("bad material name");
        }

        stringEnd = offset + bytes + 1;
    }

    if (stringEnd != strings.size())
    {
        return fail("unused string bytes");
    }

    const std::size_t surfaceCount = counts[static_cast<int>(D3wdSection::Surfaces)];
    const std::size_t batchCount = counts[static_cast<int>(D3wdSection::Batches)];
    const std::size_t pointCount = counts[static_cast<int>(D3wdSection::PortalPoints)];
    const std::size_t mirrorVertexCount = counts[static_cast<int>(D3wdSection::MirrorPositions)];
    const std::size_t mirrorIndexCount = counts[static_cast<int>(D3wdSection::MirrorIndices)];
    const std::size_t areaCount = counts[static_cast<int>(D3wdSection::Areas)];

    Reader areas(sections[static_cast<int>(D3wdSection::Areas)]);
    std::uint32_t nextSurface = 0;
    std::uint32_t nextBatch = 0;

    for (std::size_t index = 0; index < areaCount; ++index)
    {
        WorldArea area;
        areas.Bounds(area.bounds, area.firstSurface, area.surfaceCount);
        area.mirrorOffset = { areas.F32(), areas.F32(), areas.F32() };
        area.mirrorScale = areas.F32();
        area.firstBatch = areas.U32();
        area.batchCount = areas.U32();

        if (!areas.Zero(8) || area.firstSurface != nextSurface || area.surfaceCount > surfaceCount - nextSurface || area.firstBatch != nextBatch ||
            area.batchCount > batchCount - nextBatch || !(area.mirrorScale > 0.0f) || !std::isfinite(area.mirrorScale))
        {
            return fail("bad area record");
        }

        nextSurface += area.surfaceCount;
        nextBatch += area.batchCount;
        world.areas.push_back(area);
    }

    if (nextSurface != surfaceCount || nextBatch != batchCount)
    {
        return fail("areas do not cover the surfaces and batches");
    }

    Reader surfaces(sections[static_cast<int>(D3wdSection::Surfaces)]);
    std::uint32_t nextVertex = 0;
    std::uint32_t nextIndex = 0;
    nextBatch = 0;

    for (std::size_t index = 0; index < surfaceCount; ++index)
    {
        WorldSurface surface;
        surfaces.Bounds(surface.bounds, surface.material, surface.area);
        surface.firstBatch = surfaces.U32();
        surface.batchCount = surfaces.U32();
        surface.firstVertex = surfaces.U32();
        surface.vertexCount = surfaces.U32();
        surface.firstIndex = surfaces.U32();
        surface.indexCount = surfaces.U32();

        const bool inArea = surface.area < areaCount && index >= world.areas[surface.area].firstSurface &&
                            index - world.areas[surface.area].firstSurface < world.areas[surface.area].surfaceCount;

        if (!surfaces.Zero(8) || !inArea || surface.material >= world.materials.size() || surface.firstBatch != nextBatch ||
            surface.batchCount > batchCount - nextBatch || surface.firstVertex != nextVertex || surface.vertexCount > mirrorVertexCount - nextVertex ||
            surface.vertexCount > 65536U || surface.firstIndex != nextIndex || surface.indexCount > mirrorIndexCount - nextIndex || surface.indexCount % 3 != 0)
        {
            return fail("bad surface record");
        }

        nextBatch += surface.batchCount;
        nextVertex += surface.vertexCount;
        nextIndex += surface.indexCount;
        world.surfaces.push_back(surface);
    }

    if (nextBatch != batchCount || nextVertex != mirrorVertexCount || nextIndex != mirrorIndexCount)
    {
        return fail("surfaces do not cover the batches and mirror");
    }

    Reader portals(sections[static_cast<int>(D3wdSection::Portals)]);
    std::uint32_t nextPoint = 0;

    for (std::uint32_t index = 0; index < counts[static_cast<int>(D3wdSection::Portals)]; ++index)
    {
        WorldPortal portal;
        portal.firstPoint = portals.U32();
        portal.pointCount = portals.U32();
        portal.areas[0] = portals.U32();
        portal.areas[1] = portals.U32();

        if (portal.firstPoint != nextPoint || portal.pointCount < 3 || portal.pointCount > pointCount - nextPoint || portal.areas[0] >= areaCount ||
            portal.areas[1] >= areaCount || portal.areas[0] == portal.areas[1])
        {
            return fail("bad portal record");
        }

        nextPoint += portal.pointCount;
        world.portals.push_back(portal);
    }

    if (nextPoint != pointCount)
    {
        return fail("unused portal points");
    }

    Reader points(sections[static_cast<int>(D3wdSection::PortalPoints)]);

    for (std::size_t index = 0; index < pointCount; ++index)
    {
        world.portalPoints.push_back({ points.F32(), points.F32(), points.F32() });
    }

    Reader nodes(sections[static_cast<int>(D3wdSection::Nodes)]);
    const std::size_t nodeCount = counts[static_cast<int>(D3wdSection::Nodes)];

    for (std::size_t index = 0; index < nodeCount; ++index)
    {
        WorldNode node;
        node.plane = { nodes.F32(), nodes.F32(), nodes.F32(), nodes.F32() };
        node.children[0] = static_cast<std::int32_t>(nodes.U32());
        node.children[1] = static_cast<std::int32_t>(nodes.U32());

        // Children follow their node, as dmap numbers them, so the engine's recursive walks end.
        for (const std::int32_t child : node.children)
        {
            if ((child > 0 && (static_cast<std::size_t>(child) >= nodeCount || static_cast<std::size_t>(child) <= index)) ||
                (child < 0 && static_cast<std::size_t>(-1 - static_cast<std::int64_t>(child)) >= areaCount))
            {
                return fail("bad node child");
            }
        }

        world.nodes.push_back(node);
    }

    if (nodeCount == 0)
    {
        return fail("no nodes");
    }

    Reader batches(sections[static_cast<int>(D3wdSection::Batches)]);
    std::size_t nextPacket = 0;

    for (std::size_t index = 0; index < batchCount; ++index)
    {
        WorldBatch batch;
        batches.Bounds(batch.bounds, batch.flags, batch.vertexCount);
        batch.positionOffset = { batches.F32(), batches.F32(), batches.F32() };
        batch.positionScale = batches.F32();
        batch.packetOffset = batches.U32();
        batch.packetQwords = batches.U32();
        batch.material = batches.U16();
        batch.lightmapPage = batches.U16();
        batch.indexCount = batches.U16();

        if (batches.U16() != 0 || (batch.flags & ~(kBatchLayoutMask | kBatchFlagIndexedStrips)) != 0 ||
            (batch.flags & kBatchLayoutMask) != kBatchLayoutWorldLightmapped || batch.material >= world.materials.size() ||
            batch.lightmapPage != kBatchNoLightmap || batch.packetOffset != nextPacket || batch.packetQwords == 0 ||
            batch.packetQwords > (packets.size() - nextPacket) / 16 || !(batch.positionScale > 0.0f) || !std::isfinite(batch.positionScale))
        {
            return fail("bad batch record");
        }

        if (!CheckPacket(packets.subspan(batch.packetOffset, static_cast<std::size_t>(batch.packetQwords) * 16), batch, error))
        {
            return fail(error + " in batch " + std::to_string(index));
        }

        nextPacket += static_cast<std::size_t>(batch.packetQwords) * 16;
        world.batches.push_back(batch);
    }

    if (nextPacket != packets.size())
    {
        return fail("unused packet bytes");
    }

    for (const WorldSurface & surface : world.surfaces)
    {
        for (std::uint32_t batch = surface.firstBatch; batch < surface.firstBatch + surface.batchCount; ++batch)
        {
            if (world.batches[batch].material != surface.material)
            {
                return fail("batch material differs from its surface");
            }
        }
    }

    Reader positions(sections[static_cast<int>(D3wdSection::MirrorPositions)]);

    for (std::size_t index = 0; index < mirrorVertexCount; ++index)
    {
        world.mirrorPositions.push_back({ static_cast<std::int16_t>(positions.U16()), static_cast<std::int16_t>(positions.U16()),
                                          static_cast<std::int16_t>(positions.U16()) });
    }

    Reader mirrorIndices(sections[static_cast<int>(D3wdSection::MirrorIndices)]);

    for (const WorldSurface & surface : world.surfaces)
    {
        for (std::uint32_t index = 0; index < surface.indexCount; ++index)
        {
            const std::uint16_t value = mirrorIndices.U16();

            if (value >= surface.vertexCount)
            {
                return fail("mirror index out of range");
            }

            world.mirrorIndices.push_back(value);
        }
    }

    world.packets.assign(packets.begin(), packets.end());
    return true;
}

} // namespace assets
