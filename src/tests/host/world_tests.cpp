// ================================================================================================
// File: world_tests.cpp
// Brief: Exercise the world cook: .proc parsing, greedy strips, D3WD round trips and corruption,
// and every graybox batch packet unpacked by an independent VIF model into a VU1 input half,
// rebuilt into triangles and compared with the source geometry.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/bytes.h"
#include "assets/proc.h"
#include "assets/world.h"
#include "ps2/renderer/world_format.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace
{

using namespace assets;

static const char * const kGrayboxProc = "src/tests/data/graybox_map/maps/graybox.proc";

// The target validator wants the file 16-byte aligned, as the loader's DMA allocation is.
alignas(64) static std::uint8_t s_aligned[1U << 20];

static const char * TargetValidate(const std::vector<std::uint8_t> & file, ps2::renderer::world::Sections & sections)
{
    if (file.size() > sizeof(s_aligned))
    {
        return "test buffer too small";
    }

    std::memcpy(s_aligned, file.data(), file.size());
    return ps2::renderer::world::Validate(s_aligned, static_cast<std::uint32_t>(file.size()), sections);
}

static bool Check(bool passed, const char * name)
{
    if (!passed)
    {
        std::fprintf(stderr, "World cook failed: %s\n", name);
    }

    return passed;
}

using Triangle = std::array<std::uint8_t, 3>;

// Rotate so the smallest corner leads; winding is preserved.
static Triangle Canonical(Triangle triangle)
{
    while (triangle[0] > triangle[1] || triangle[0] > triangle[2])
    {
        triangle = { triangle[1], triangle[2], triangle[0] };
    }

    return triangle;
}

static bool SameTriangles(std::vector<Triangle> a, std::vector<Triangle> b)
{
    for (Triangle & triangle : a)
    {
        triangle = Canonical(triangle);
    }

    for (Triangle & triangle : b)
    {
        triangle = Canonical(triangle);
    }

    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b;
}

static bool StreamShape(const std::vector<std::uint8_t> & stream, std::size_t strips)
{
    // Each strip: two ADC entries, then at least one closing entry.
    std::size_t opened = 0;
    std::size_t index = 0;

    while (index < stream.size())
    {
        if (index + 2 >= stream.size() || (stream[index] & kStripIndexAdc) == 0 || (stream[index + 1] & kStripIndexAdc) == 0 ||
            (stream[index + 2] & kStripIndexAdc) != 0)
        {
            return false;
        }

        index += 3;

        while (index < stream.size() && (stream[index] & kStripIndexAdc) == 0)
        {
            ++index;
        }

        ++opened;
    }

    return opened == strips;
}

static bool Strips()
{
    std::mt19937 random(11);
    bool passed = true;

    for (int round = 0; round < 300 && passed; ++round)
    {
        // A grid of quads with some removed, wound consistently, plus a few loose triangles.
        const int width = 1 + static_cast<int>(random() % 8);
        const int height = 1 + static_cast<int>(random() % 8);
        std::vector<Triangle> triangles;

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const auto at = [width](int column, int row)
                {
                    return static_cast<std::uint8_t>(row * (width + 1) + column);
                };

                if (random() % 5 != 0)
                {
                    triangles.push_back({ at(x, y), at(x + 1, y + 1), at(x + 1, y) });
                }

                if (random() % 5 != 0)
                {
                    triangles.push_back({ at(x, y), at(x, y + 1), at(x + 1, y + 1) });
                }
            }
        }

        for (std::uint32_t loose = random() % 3; loose > 0; --loose)
        {
            triangles.push_back({ 120, 121, static_cast<std::uint8_t>(122 + loose) });
        }

        std::size_t strips = 0;
        const std::vector<std::uint8_t> stream = BuildStrips(triangles, strips);
        passed = SameTriangles(WalkStrips(stream), triangles) && StreamShape(stream, strips) && stream.size() == triangles.size() + 2 * strips;
    }

    passed = Check(passed, "strips draw exactly the source triangles in id's winding");

    // A 2x1 quad strip must come out as one strip of four entries.
    const std::vector<Triangle> quad = { { 0, 2, 1 }, { 0, 3, 2 } };
    std::size_t strips = 0;
    const std::vector<std::uint8_t> stream = BuildStrips(quad, strips);
    return Check(passed && strips == 1 && stream.size() == 4, "a quad is one strip");
}

static const char * const kSmallProc = R"(// comment
mapProcFile003
/* block
   comment */
model { "_area0" 1 { "textures/a" 3 3
( 0 0 0 0 0 0 0 1 ) ( 0 64 0 0 1 0 0 1 ) ( 64 0 0 1 0 0 0 1 )
0 1 2 } }
model { "_area1" 0 }
shadowModel { "_prelight_x" 2 0 0 3 0 ( 0 0 0 ) ( 1 0 0 ) 0 1 0 }
interAreaPortals { 2 1 4 0 1 ( 0 0 0 ) ( 0 0 1 ) ( 0 1 1 ) ( 0 1 0 ) }
nodes { 1 ( 1 0 0 0 ) -2 -1 }
)";

static bool Procs()
{
    ProcFile proc;
    std::string error;
    bool passed = Check(ParseProc(kSmallProc, proc, error), "small proc parses") &&
                  Check(proc.models.size() == 2 && proc.areaCount == 2 && proc.shadowModelCount == 1 && proc.portals.size() == 1 && proc.nodes.size() == 1,
                        "small proc counts") &&
                  Check(proc.models[0].surfaces[0].vertices[1].position[1] == 64.0f && proc.models[0].surfaces[0].vertices[2].uv[0] == 1.0f,
                        "small proc values") &&
                  Check(FindAreaModel(proc, 1) == &proc.models[1] && FindAreaModel(proc, 2) == nullptr, "area models are found by name");

    const std::string base(kSmallProc);
    const struct
    {
        const char * from;
        const char * to;
        const char * name;
    } corruptions[] = {
        { "mapProcFile003", "mapProcFile002", "wrong version" },
        { "0 1 2 } }", "0 1 3 } }", "index out of range" },
        { "\"textures/a\" 3 3", "\"textures/a\" 3 2", "index count not a multiple of 3" },
        { "-2 -1", "-3 -1", "node child names a missing area" },
        { "-2 -1", "1 -1", "node child names a missing node" },
        { "model { \"_area1\" 0 }", "model { \"_area7\" 0 }", "missing area model" },
        { "4 0 1", "4 0 0", "portal joins an area to itself" },
        { "( 64 0 0 1 0 0 0 1 )", "( 64 0 0 1 0 0 0 )", "short vertex" },
        { "( 64 0 0 1 0 0 0 1 )", "( 64 0 0 1 0 0 0 1e99 )", "out of range number" },
        { "nodes { 1", "nodes { 1 1", "garbage in nodes" },
        { "\"_area0\"", "\"_area0", "unterminated string" },
    };

    for (const auto & corruption : corruptions)
    {
        std::string text = base;
        text.replace(text.find(corruption.from), std::string(corruption.from).size(), corruption.to);
        passed = Check(!ParseProc(text, proc, error) && !error.empty(), corruption.name) && passed;
    }

    return passed;
}

// ------------------------------------------------------------------------------------------------
// An independent VIF model of one batch packet
// ------------------------------------------------------------------------------------------------

// VIFcode fields (EE User's Manual, VIF chapter): CMD in bits 24-31, NUM in 16-23, IMMEDIATE in
// 0-15. Only the commands a batch packet may hold are modelled; anything else fails the packet.
static constexpr std::uint32_t kVifCommandShift = 24;
static constexpr std::uint32_t kVifNumShift = 16;
static constexpr std::uint32_t kVifNumMask = 0xFFU;
static constexpr std::uint32_t kVifImmediateMask = 0xFFFFU;

static constexpr std::uint32_t kVifNop = 0x00;
static constexpr std::uint32_t kVifStcycl = 0x01;
static constexpr std::uint32_t kVifItop = 0x04;

// STCYCL: IMMEDIATE holds CL (cycle length) in bits 0-7 and WL (write length) in bits 8-15.
static constexpr std::uint32_t kStcyclCycleMask = 0xFFU;
static constexpr std::uint32_t kStcyclWriteShift = 8;

// ITOP: a 10-bit count the microprogram reads back with XITOP.
static constexpr std::uint32_t kItopMask = 0x3FFU;

// UNPACK: CMD is 011m vnvl. The m bit (masking) is unused here; vn is the component count less
// one and vl the component width (0: 32, 1: 16, 2: 8, 3: 5 bits, V4-5 only). IMMEDIATE holds the
// qword address (bits 0-9), USN (bit 14, zero instead of sign extension) and FLG (bit 15, the
// address is relative to TOPS). NUM 0 means 256 elements.
static constexpr std::uint32_t kUnpackCommandMask = 0xE0U;
static constexpr std::uint32_t kUnpackCommand = 0x60U;
static constexpr std::uint32_t kUnpackMaskBit = 0x10U;
static constexpr std::uint32_t kUnpackVnShift = 2;
static constexpr std::uint32_t kUnpackFieldMask = 3U;
static constexpr std::uint32_t kUnpackAddressMask = 0x3FFU;
static constexpr std::uint32_t kUnpackUnsignedBit = 1U << 14;
static constexpr std::uint32_t kUnpackTopsBit = 1U << 15;
static constexpr std::uint32_t kUnpackMaxElements = 256;
static constexpr std::uint32_t kUnpackV4_5Bits = 5;

static constexpr std::uint32_t kQwordLanes = 4;
static constexpr std::uint32_t kWordBits = 32;
static constexpr std::size_t kWordBytes = 4;

struct Unpacked
{
    std::vector<std::array<std::uint32_t, 4>> memory = std::vector<std::array<std::uint32_t, 4>>(kWorldInputHalfQwords);
    int highest = -1;
    std::uint32_t itop = 0;
    bool ended = false;
};

static bool RunVif(std::span<const std::uint8_t> packet, Unpacked & output)
{
    std::uint32_t cycle = 1;
    std::uint32_t write = 1;
    std::size_t at = 0;
    const std::size_t words = packet.size() / kWordBytes;

    while (at < words)
    {
        const std::uint32_t code = ReadLe32(packet.data() + kWordBytes * at++);
        const std::uint32_t command = code >> kVifCommandShift;
        const std::uint32_t num = (code >> kVifNumShift) & kVifNumMask;
        const std::uint32_t immediate = code & kVifImmediateMask;

        // ITOP closes the packet (the display list sends MSCAL after it); only padding may follow.
        if (output.ended && command != kVifNop)
        {
            return false;
        }

        if (command == kVifNop)
        {
            continue;
        }

        if (command == kVifStcycl)
        {
            cycle = immediate & kStcyclCycleMask;
            write = immediate >> kStcyclWriteShift;
            continue;
        }

        if (command == kVifItop)
        {
            output.itop = immediate & kItopMask;
            output.ended = true;
            continue;
        }

        // Every batch unpack is unmasked and relative to TOPS, in skipping-write mode (CL >= WL).
        if ((command & kUnpackCommandMask) != kUnpackCommand || (command & kUnpackMaskBit) != 0 || (immediate & kUnpackTopsBit) == 0 || write == 0 ||
            cycle < write)
        {
            return false;
        }

        const std::uint32_t components = ((command >> kUnpackVnShift) & kUnpackFieldMask) + 1U;
        const std::uint32_t bits = kWordBits >> (command & kUnpackFieldMask);
        const bool unsignedExtend = (immediate & kUnpackUnsignedBit) != 0;
        const std::uint32_t address = immediate & kUnpackAddressMask;
        const std::uint32_t count = num == 0 ? kUnpackMaxElements : num;
        const std::size_t dataWords = (static_cast<std::size_t>(count) * components * bits + kWordBits - 1U) / kWordBits;

        if (components != kQwordLanes || bits == kUnpackV4_5Bits || at + dataWords > words)
        {
            return false;
        }

        for (std::uint32_t element = 0; element < count; ++element)
        {
            // Skipping write: WL qwords written, then CL - WL skipped.
            const std::uint32_t target = address + (element / write) * cycle + element % write;

            if (target >= static_cast<std::uint32_t>(kWorldInputHalfQwords))
            {
                return false;
            }

            for (std::uint32_t lane = 0; lane < kQwordLanes; ++lane)
            {
                const std::size_t bit = (static_cast<std::size_t>(element) * kQwordLanes + lane) * bits;
                const std::uint32_t word = ReadLe32(packet.data() + kWordBytes * (at + bit / kWordBits));
                const std::uint32_t fieldMask = bits == kWordBits ? ~0U : (1U << bits) - 1U;
                std::uint32_t value = bits == kWordBits ? word : (word >> (bit % kWordBits)) & fieldMask;

                if (!unsignedExtend && bits < kWordBits && (value & (1U << (bits - 1U))) != 0)
                {
                    value |= ~fieldMask;
                }

                output.memory[target][lane] = value;
            }

            output.highest = std::max(output.highest, static_cast<int>(target));
        }

        at += dataWords;
    }

    return output.ended;
}

static float AsFloat(std::uint32_t bits)
{
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(bits));
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

static std::int32_t AsInt(std::uint32_t bits)
{
    return static_cast<std::int32_t>(bits);
}

using Point = std::array<float, 3>;
using Corners = std::array<Point, 3>;

static bool Near(const Point & a, const Point & b, float tolerance)
{
    return std::fabs(a[0] - b[0]) <= tolerance && std::fabs(a[1] - b[1]) <= tolerance && std::fabs(a[2] - b[2]) <= tolerance;
}

// Every cooked triangle matches a distinct source triangle, corner for corner up to rotation.
static bool MatchTriangles(const std::vector<Corners> & cooked, const std::vector<Corners> & source, float tolerance)
{
    if (cooked.size() != source.size())
    {
        return false;
    }

    std::vector<bool> used(source.size(), false);

    for (const Corners & triangle : cooked)
    {
        bool found = false;

        for (std::size_t index = 0; index < source.size() && !found; ++index)
        {
            for (std::size_t rotation = 0; rotation < 3 && !used[index]; ++rotation)
            {
                if (Near(triangle[0], source[index][rotation], tolerance) && Near(triangle[1], source[index][(rotation + 1) % 3], tolerance) &&
                    Near(triangle[2], source[index][(rotation + 2) % 3], tolerance))
                {
                    used[index] = true;
                    found = true;
                }
            }
        }

        if (!found)
        {
            return false;
        }
    }

    return true;
}

static bool Inside(const WorldBounds & outer, const WorldBounds & inner)
{
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        if (inner.min[axis] < outer.min[axis] || inner.max[axis] > outer.max[axis])
        {
            return false;
        }
    }

    return true;
}

static bool ReadText(const char * path, std::string & output)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream text;
    text << stream.rdbuf();
    output = text.str();
    return static_cast<bool>(stream);
}

static WorldPolicy GrayboxPolicy(WorldTopology topology)
{
    WorldPolicy policy;
    policy.topology = topology;
    policy.light = { 0.35f, 0.25f, 0.9f };
    policy.ambient = 0.45f;
    policy.colors.push_back({ "textures/graybox/floor_light", { 0.62f, 0.62f, 0.60f } });
    return policy;
}

// Unpacks every batch and rebuilds the drawn triangles, positions, UVs and colours.
static bool CheckCookedWorld(const ProcFile & proc, const World & world, const WorldPolicy & policy, const char * label)
{
    bool packets = true;
    bool geometry = true;
    bool colours = true;
    bool bounds = true;
    bool mirror = true;
    const float lightLength = std::sqrt(0.35f * 0.35f + 0.25f * 0.25f + 0.9f * 0.9f);

    for (std::size_t areaIndex = 0; areaIndex < world.areas.size(); ++areaIndex)
    {
        const WorldArea & area = world.areas[areaIndex];
        const ProcModel & model = *FindAreaModel(proc, static_cast<int>(areaIndex));
        packets = packets && area.surfaceCount == model.surfaces.size();

        for (std::uint32_t local = 0; local < area.surfaceCount && packets; ++local)
        {
            const WorldSurface & surface = world.surfaces[area.firstSurface + local];
            const ProcSurface & source = model.surfaces[local];
            std::vector<Corners> sourceTriangles;

            for (std::size_t index = 0; index + 2 < source.indices.size(); index += 3)
            {
                sourceTriangles.push_back({ source.vertices[source.indices[index]].position, source.vertices[source.indices[index + 1]].position,
                                            source.vertices[source.indices[index + 2]].position });
            }

            packets = packets && world.materials[surface.material] == source.material;
            bounds = bounds && Inside(area.bounds, surface.bounds);

            std::vector<Corners> drawn;
            float tolerance = 0.0f;

            for (std::uint32_t batchIndex = surface.firstBatch; batchIndex < surface.firstBatch + surface.batchCount && packets; ++batchIndex)
            {
                const WorldBatch & batch = world.batches[batchIndex];
                Unpacked unpacked;
                packets = RunVif(std::span<const std::uint8_t>(world.packets).subspan(batch.packetOffset, batch.packetQwords * 16U), unpacked) &&
                          unpacked.itop == batch.vertexCount && batch.material == surface.material;

                if (!packets)
                {
                    break;
                }

                const std::array<std::uint32_t, 4> & header0 = unpacked.memory[0];
                const std::array<std::uint32_t, 4> & header1 = unpacked.memory[1];
                const std::uint32_t indexQwords = header0[3];
                const std::uint32_t indexCount = batch.indexCount;
                const std::uint32_t base = 2U + indexQwords;
                const bool indexed = (batch.flags & kBatchFlagIndexedStrips) != 0;
                const float scale = AsFloat(header1[0]);
                const float uvScale = AsFloat(header1[1]);
                const Point offset = { AsFloat(header0[0]), AsFloat(header0[1]), AsFloat(header0[2]) };

                // The program's last write is the last vertex's colour; nothing may land beyond it.
                packets = indexQwords == (indexCount + 3U) / 4U && indexed == (indexCount != 0) && scale == batch.positionScale &&
                          unpacked.highest == static_cast<int>(base + 3U * batch.vertexCount) - 1 &&
                          (!indexed || batch.vertexCount <= static_cast<std::uint32_t>(kWorldMaxIndexedVertices));
                tolerance = std::max(tolerance, scale * 0.5f + 1e-3f);

                std::vector<Point> positions;
                std::vector<bool> adc;

                for (std::uint32_t vertex = 0; vertex < batch.vertexCount; ++vertex)
                {
                    const std::array<std::uint32_t, 4> & position = unpacked.memory[base + 3U * vertex];
                    const std::array<std::uint32_t, 4> & color = unpacked.memory[base + 3U * vertex + 2U];
                    const Point point = { static_cast<float>(AsInt(position[0])) * scale + offset[0], static_cast<float>(AsInt(position[1])) * scale + offset[1],
                                          static_cast<float>(AsInt(position[2])) * scale + offset[2] };
                    positions.push_back(point);
                    adc.push_back(AsInt(position[3]) == -32768);
                    bounds = bounds && Inside(batch.bounds, { point, point }) && Inside(surface.bounds, { point, point });
                    colours = colours && color[3] == 0x80 && color[0] <= 255 && unpacked.memory[base + 3U * vertex + 1U][2] == 0;

                    // The floor faces +z, the light's z share shades it.
                    if (world.materials[surface.material] == "textures/graybox/floor_light")
                    {
                        const float shade = policy.ambient + (1.0f - policy.ambient) * (0.9f / lightLength);
                        colours = colours && color[0] == static_cast<std::uint32_t>(std::round(0.62f * shade * 128.0f)) &&
                                  color[2] == static_cast<std::uint32_t>(std::round(0.60f * shade * 128.0f));
                    }

                    // UVs: some source vertex at this position carries this UV within half a step.
                    const std::array<std::uint32_t, 4> & uv = unpacked.memory[base + 3U * vertex + 1U];
                    const float s = static_cast<float>(AsInt(uv[0])) * uvScale + AsFloat(header1[2]);
                    const float t = static_cast<float>(AsInt(uv[1])) * uvScale + AsFloat(header1[3]);
                    bool uvFound = false;

                    for (const ProcVertex & candidate : source.vertices)
                    {
                        uvFound = uvFound || (Near(candidate.position, point, scale * 0.5f + 1e-3f) && std::fabs(candidate.uv[0] - s) <= uvScale * 0.5f + 1e-6f &&
                                              std::fabs(candidate.uv[1] - t) <= uvScale * 0.5f + 1e-6f);
                    }

                    geometry = geometry && uvFound;
                }

                // The GS draws a triangle on each vertex without ADC, from the last three.
                std::vector<std::uint32_t> order;

                if (indexed)
                {
                    // Entries are vertex qword offsets with ADC in bit 15; padding repeats the first
                    // vertex with ADC.
                    for (std::uint32_t entry = 0; entry < indexQwords * 4U; ++entry)
                    {
                        const std::uint32_t lane = unpacked.memory[2U + entry / 4U][entry % 4U];
                        const std::uint32_t entryOffset = lane & 0x7FFFU;
                        packets = packets && lane <= 0xFFFFU && entryOffset >= base && (entryOffset - base) % 3U == 0;

                        if (entry >= indexCount)
                        {
                            packets = packets && lane == (0x8000U | base);
                            continue;
                        }

                        order.push_back((entryOffset - base) / 3U | ((lane & 0x8000U) != 0 ? 0x80U : 0U));
                    }
                }
                else
                {
                    for (std::uint32_t vertex = 0; vertex < batch.vertexCount; ++vertex)
                    {
                        order.push_back(vertex | (adc[vertex] ? 0x80U : 0U));
                    }
                }

                std::size_t run = 0;
                std::uint32_t window[2] = {};

                for (const std::uint32_t entry : order)
                {
                    const std::uint32_t vertex = entry & 0x7FU;
                    packets = packets && vertex < batch.vertexCount;

                    if (!packets)
                    {
                        break;
                    }

                    if ((entry & 0x80U) != 0)
                    {
                        run = run == 1 ? 2 : 1;
                    }
                    else
                    {
                        const bool even = (run & 1U) == 0;
                        drawn.push_back(even ? Corners{ positions[window[0]], positions[window[1]], positions[vertex] }
                                             : Corners{ positions[window[1]], positions[window[0]], positions[vertex] });
                        ++run;
                    }

                    window[0] = window[1];
                    window[1] = vertex;
                }
            }

            geometry = geometry && MatchTriangles(drawn, sourceTriangles, tolerance);

            // The mirror holds every source triangle within half the area's step.
            std::vector<Corners> mirrored;

            for (std::uint32_t index = 0; index < surface.indexCount; index += 3)
            {
                Corners corners = {};

                for (std::size_t corner = 0; corner < 3; ++corner)
                {
                    const std::array<std::int16_t, 3> & q = world.mirrorPositions[surface.firstVertex + world.mirrorIndices[surface.firstIndex + index + corner]];

                    for (std::size_t axis = 0; axis < 3; ++axis)
                    {
                        corners[corner][axis] = static_cast<float>(q[axis]) * area.mirrorScale + area.mirrorOffset[axis];
                    }
                }

                mirrored.push_back(corners);
            }

            mirror = mirror && MatchTriangles(mirrored, sourceTriangles, area.mirrorScale * 0.5f + 1e-3f);
        }
    }

    const std::string prefix(label);
    return Check(packets, (prefix + ": every packet unpacks inside one VU1 input half").c_str()) &&
           Check(geometry, (prefix + ": batches draw exactly the source triangles and UVs").c_str()) &&
           Check(colours, (prefix + ": vertex colours follow the shading policy").c_str()) &&
           Check(bounds, (prefix + ": batch, surface and area bounds contain their vertices").c_str()) &&
           Check(mirror, (prefix + ": the CPU mirror holds the source triangles").c_str());
}

static bool Graybox()
{
    std::string text;
    ProcFile proc;
    std::string error;

    if (!Check(ReadText(kGrayboxProc, text) && ParseProc(text, proc, error), "graybox proc parses"))
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return false;
    }

    bool passed = Check(proc.areaCount == 2 && proc.portals.size() == 1 && proc.nodes.size() == 11, "graybox has two areas, a portal and 11 nodes");

    for (const WorldTopology topology : { WorldTopology::IndexedStrips, WorldTopology::Strips })
    {
        const char * label = topology == WorldTopology::IndexedStrips ? "indexed strips" : "plain strips";
        const WorldPolicy policy = GrayboxPolicy(topology);
        World world;
        WorldCookStats stats;
        std::vector<std::uint8_t> file;
        std::vector<std::uint8_t> again;
        World read;

        if (!Check(CookWorld(proc, policy, world, stats, error) && WriteD3wd(world, file, error), "graybox cooks"))
        {
            std::fprintf(stderr, "%s\n", error.c_str());
            return false;
        }

        passed = Check(ReadD3wd(file, read, error), "graybox D3WD reads back") && passed;
        ps2::renderer::world::Sections sections = {};
        const char * targetError = TargetValidate(file, sections);
        passed = Check(targetError == nullptr && sections.areaCount == world.areas.size() && sections.batchCount == world.batches.size() &&
                           sections.surfaceCount == world.surfaces.size() && sections.nodeCount == world.nodes.size() &&
                           sections.packetBytes == world.packets.size() && sections.mirrorIndexCount == world.mirrorIndices.size(),
                       "the target validator admits the graybox") &&
                 passed;

        if (targetError != nullptr)
        {
            std::fprintf(stderr, "%s\n", targetError);
        }
        passed = Check(WriteD3wd(read, again, error) && again == file, "graybox D3WD rewrites byte for byte") && passed;

        World second;
        WorldCookStats secondStats;
        std::vector<std::uint8_t> secondFile;
        passed = Check(CookWorld(proc, policy, second, secondStats, error) && WriteD3wd(second, secondFile, error) && secondFile == file,
                       "graybox cook is deterministic") &&
                 passed;
        passed = Check(stats.triangles == 1516 && stats.skippedModels == 0 && world.portals.size() == 1 && world.portalPoints.size() == 4 &&
                           world.nodes.size() == 11 && world.nodes[0].children[0] == 1 && world.nodes[5].children[1] == -2,
                       "graybox counts, portals and nodes are copied") &&
                 passed;
        passed = CheckCookedWorld(proc, read, policy, label) && passed;
    }

    return passed;
}

// ------------------------------------------------------------------------------------------------
// Large, distant and corrupt worlds
// ------------------------------------------------------------------------------------------------

static ProcFile GridProc(int cells, float origin, float spacing, float uvPerCell)
{
    ProcFile proc;
    proc.areaCount = 1;
    proc.models.push_back({ "_area0", { { "textures/grid", {}, {} } } });
    ProcSurface & surface = proc.models[0].surfaces[0];

    for (int y = 0; y <= cells; ++y)
    {
        for (int x = 0; x <= cells; ++x)
        {
            ProcVertex vertex;
            vertex.position = { origin + static_cast<float>(x) * spacing, origin + static_cast<float>(y) * spacing, 0.0f };
            vertex.uv = { static_cast<float>(x) * uvPerCell, static_cast<float>(y) * uvPerCell };
            vertex.normal = { 0.0f, 0.0f, 1.0f };
            surface.vertices.push_back(vertex);
        }
    }

    for (int y = 0; y < cells; ++y)
    {
        for (int x = 0; x < cells; ++x)
        {
            const std::uint32_t a = static_cast<std::uint32_t>(y * (cells + 1) + x);
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + static_cast<std::uint32_t>(cells) + 1;
            const std::uint32_t d = c + 1;
            surface.indices.insert(surface.indices.end(), { a, d, b, a, c, d });
        }
    }

    ProcNode node;
    node.plane = { 0.0f, 0.0f, 1.0f, 0.0f };
    node.children[0] = -1;
    node.children[1] = 0;
    proc.nodes.push_back(node);
    return proc;
}

static bool LargeWorlds()
{
    bool passed = true;
    const struct
    {
        int cells;
        float origin;
        float spacing;
        float uv;
        const char * name;
    } grids[] = {
        { 40, 0.0f, 32.0f, 1.0f, "a 3,200-triangle grid splits into many batches" },
        { 12, -70000.0f, 12000.0f, 0.5f, "a grid far from the origin keeps its geometry" },
        { 10, 0.0f, 16.0f, 7.0f, "a 70-repeat UV span coarsens its UV step" },
    };

    for (const auto & grid : grids)
    {
        const ProcFile proc = GridProc(grid.cells, grid.origin, grid.spacing, grid.uv);
        World world;
        WorldCookStats stats;
        std::vector<std::uint8_t> file;
        World read;
        std::string error;
        const WorldPolicy policy = GrayboxPolicy(WorldTopology::IndexedStrips);
        const bool cooked = CookWorld(proc, policy, world, stats, error) && WriteD3wd(world, file, error) && ReadD3wd(file, read, error);
        passed = Check(cooked && CheckCookedWorld(proc, read, policy, grid.name), grid.name) && passed;
    }

    return passed;
}

static bool Corruption()
{
    ProcFile proc = GridProc(6, 0.0f, 64.0f, 1.0f);
    World world;
    WorldCookStats stats;
    std::vector<std::uint8_t> file;
    World read;
    std::string error;
    const WorldPolicy policy = GrayboxPolicy(WorldTopology::IndexedStrips);

    if (!Check(CookWorld(proc, policy, world, stats, error) && WriteD3wd(world, file, error), "corruption base cooks"))
    {
        return false;
    }

    // Any flipped byte fails a checksum or a range check, in the host reader and the target's.
    bool rejected = true;
    bool targetRejected = true;
    ps2::renderer::world::Sections sections = {};

    for (std::size_t offset = 0; offset < file.size(); offset += 7)
    {
        std::vector<std::uint8_t> copy = file;
        copy[offset] ^= 0x10;
        rejected = rejected && !ReadD3wd(copy, read, error);
        targetRejected = targetRejected && TargetValidate(copy, sections) != nullptr;
    }

    bool passed = Check(rejected, "every flipped byte is refused") && Check(targetRejected, "the target refuses every flipped byte");
    passed = Check(!ReadD3wd(std::span<const std::uint8_t>(file).first(file.size() - 16), read, error), "a truncated file is refused") && passed;
    std::vector<std::uint8_t> truncated(file.begin(), file.end() - 16);
    passed = Check(TargetValidate(truncated, sections) != nullptr, "the target refuses a truncated file") && passed;

    // Records the writer serializes faithfully but the reader must refuse, checksums intact.
    const struct
    {
        void (*mutate)(World &);
        const char * name;
    } semantic[] = {
        { [](World & w) { w.batches[0].vertexCount += 1; }, "a batch header disagreeing with its packet" },
        { [](World & w) { w.batches[0].positionScale *= 2.0f; }, "a batch scale disagreeing with its packet" },
        { [](World & w) { w.mirrorIndices[0] = 60000; }, "a mirror index out of range" },
        { [](World & w) { w.nodes[0].children[0] = -5; }, "a node child naming a missing area" },
        { [](World & w) { w.nodes.push_back(w.nodes[0]); w.nodes[0].children[1] = 1; w.nodes[1].children[0] = 1; }, "a node naming itself as a child" },
        { [](World & w) { w.surfaces[0].material = 3; }, "a surface material out of range" },
        { [](World & w) { w.areas[0].surfaceCount = 0; }, "areas not covering their surfaces" },
        { [](World & w) { w.packets[44] = 0xFF; w.packets[45] = static_cast<std::uint8_t>(0x7F | (w.packets[45] & 0x80)); }, "a strip index out of range" },
        { [](World & w) { w.batches[0].lightmapPage = 0; }, "a lightmap page before lightmaps exist" },
    };

    for (const auto & entry : semantic)
    {
        World broken = world;
        entry.mutate(broken);
        std::vector<std::uint8_t> brokenFile;
        passed = Check(WriteD3wd(broken, brokenFile, error) && !ReadD3wd(brokenFile, read, error) &&
                           TargetValidate(brokenFile, sections) != nullptr,
                       entry.name) &&
                 passed;
    }

    return passed;
}

} // namespace

int main()
{
    const bool passed = Strips() && Procs() && Graybox() && LargeWorlds() && Corruption();

    if (passed)
    {
        std::printf("World cook: all checks passed.\n");
    }

    return passed ? 0 : 1;
}
