// ================================================================================================
// File: lightmap.cpp
// Brief: Bake a map's static lights (class A) into Light8 pages for the PS2's lightmapped world
// (RENDERING_ARCHITECTURE.md 8.2, 8.3): charts of the lit area surfaces, each texel's light from
// id's projection and falloff images with N.L and ray-cast shadows, split into a per-vertex chroma
// (times a per-chart exposure) and a texel intensity the GS multiplies in with Cd x As.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/lightmap.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <unordered_map>

namespace assets
{

namespace
{

// 255 / 128: the most either factor of Cd x As carries.
static constexpr float kMaxScale = 255.0f / 128.0f;
static constexpr int kBvhLeafTriangles = 4;

static Vec3 Add(const Vec3 & a, const Vec3 & b)
{
    return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };
}

static Vec3 Subtract(const Vec3 & a, const Vec3 & b)
{
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}

static Vec3 Scale(const Vec3 & a, float scale)
{
    return { a[0] * scale, a[1] * scale, a[2] * scale };
}

static float Dot(const Vec3 & a, const Vec3 & b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static Vec3 Cross(const Vec3 & a, const Vec3 & b)
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

static float Length(const Vec3 & a)
{
    return std::sqrt(Dot(a, a));
}

static Vec3 Normalize(const Vec3 & a, const Vec3 & fallback)
{
    const float length = Length(a);
    return length > 1e-12f ? Scale(a, 1.0f / length) : fallback;
}

static float MaxChannel(const Vec3 & a)
{
    return std::max({ a[0], a[1], a[2] });
}

// ------------------------------------------------------------------------------------------------
// Shadow rays: a BVH over the occluders, any hit along a segment.
// ------------------------------------------------------------------------------------------------

class Bvh final
{
public:
    explicit Bvh(std::vector<std::array<Vec3, 3> > triangles)
        : m_triangles(std::move(triangles))
    {
        if (m_triangles.empty())
        {
            return;
        }

        m_order.resize(m_triangles.size());
        std::iota(m_order.begin(), m_order.end(), std::size_t(0));
        m_centroids.resize(m_triangles.size());

        for (std::size_t index = 0; index < m_triangles.size(); ++index)
        {
            const auto & triangle = m_triangles[index];
            m_centroids[index] = Scale(Add(Add(triangle[0], triangle[1]), triangle[2]), 1.0f / 3.0f);
        }

        Build(0, m_order.size());

        // Leaves read triangles in node order.
        std::vector<std::array<Vec3, 3> > ordered;
        ordered.reserve(m_triangles.size());

        for (const std::size_t index : m_order)
        {
            ordered.push_back(m_triangles[index]);
        }

        m_triangles = std::move(ordered);
    }

    // Whether anything lies strictly between 'from' and 'to'.
    bool Occluded(const Vec3 & from, const Vec3 & to) const
    {
        if (m_nodes.empty())
        {
            return false;
        }

        const Vec3 direction = Subtract(to, from);
        Vec3 inverse = {};

        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            inverse[axis] = std::fabs(direction[axis]) > 1e-20f ? 1.0f / direction[axis] : (direction[axis] < 0.0f ? -1e20f : 1e20f);
        }

        std::size_t stack[64];
        std::size_t depth = 0;
        stack[depth++] = 0;

        while (depth > 0)
        {
            const Node & node = m_nodes[stack[--depth]];

            if (!HitsBox(node, from, inverse))
            {
                continue;
            }

            if (node.count > 0)
            {
                for (std::size_t index = node.first; index < node.first + node.count; ++index)
                {
                    if (HitsTriangle(m_triangles[index], from, direction))
                    {
                        return true;
                    }
                }

                continue;
            }

            if (depth + 2 > std::size(stack))
            {
                // Deeper than any balanced tree of this size; treat as unoccluded.
                continue;
            }

            stack[depth++] = node.left;
            stack[depth++] = node.right;
        }

        return false;
    }

    std::size_t Size() const
    {
        return m_triangles.size();
    }

private:
    struct Node
    {
        Vec3 min = {};
        Vec3 max = {};
        // Leaves: the first triangle and the count; inner nodes: the children.
        std::size_t first = 0;
        std::size_t count = 0;
        std::size_t left = 0;
        std::size_t right = 0;
    };

    std::size_t Build(std::size_t begin, std::size_t end)
    {
        const std::size_t index = m_nodes.size();
        m_nodes.emplace_back();
        Node node;
        Vec3 centroidMin = m_centroids[m_order[begin]];
        Vec3 centroidMax = centroidMin;
        node.min = m_triangles[m_order[begin]][0];
        node.max = node.min;

        for (std::size_t at = begin; at < end; ++at)
        {
            const std::size_t triangle = m_order[at];

            for (const Vec3 & corner : m_triangles[triangle])
            {
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    node.min[axis] = std::min(node.min[axis], corner[axis]);
                    node.max[axis] = std::max(node.max[axis], corner[axis]);
                }
            }

            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                centroidMin[axis] = std::min(centroidMin[axis], m_centroids[triangle][axis]);
                centroidMax[axis] = std::max(centroidMax[axis], m_centroids[triangle][axis]);
            }
        }

        if (end - begin <= static_cast<std::size_t>(kBvhLeafTriangles))
        {
            node.first = begin;
            node.count = end - begin;
            m_nodes[index] = node;
            return index;
        }

        std::size_t axis = 0;

        for (std::size_t candidate = 1; candidate < 3; ++candidate)
        {
            if (centroidMax[candidate] - centroidMin[candidate] > centroidMax[axis] - centroidMin[axis])
            {
                axis = candidate;
            }
        }

        const std::size_t middle = begin + (end - begin) / 2;
        std::nth_element(m_order.begin() + static_cast<std::ptrdiff_t>(begin), m_order.begin() + static_cast<std::ptrdiff_t>(middle),
                         m_order.begin() + static_cast<std::ptrdiff_t>(end), [this, axis](std::size_t a, std::size_t b)
                         {
                             return m_centroids[a][axis] < m_centroids[b][axis] || (m_centroids[a][axis] == m_centroids[b][axis] && a < b);
                         });

        node.count = 0;
        node.left = Build(begin, middle);
        node.right = Build(middle, end);
        m_nodes[index] = node;
        return index;
    }

    static bool HitsBox(const Node & node, const Vec3 & from, const Vec3 & inverse)
    {
        float near = 0.0f;
        float far = 1.0f;

        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            float t0 = (node.min[axis] - from[axis]) * inverse[axis];
            float t1 = (node.max[axis] - from[axis]) * inverse[axis];

            if (t0 > t1)
            {
                std::swap(t0, t1);
            }

            near = std::max(near, t0);
            far = std::min(far, t1);

            if (near > far)
            {
                return false;
            }
        }

        return true;
    }

    // Moller-Trumbore, both faces, hits strictly inside the segment.
    static bool HitsTriangle(const std::array<Vec3, 3> & triangle, const Vec3 & from, const Vec3 & direction)
    {
        static constexpr float kEpsilon = 1e-4f;
        const Vec3 edge1 = Subtract(triangle[1], triangle[0]);
        const Vec3 edge2 = Subtract(triangle[2], triangle[0]);
        const Vec3 p = Cross(direction, edge2);
        const float determinant = Dot(edge1, p);

        if (std::fabs(determinant) < 1e-12f)
        {
            return false;
        }

        const float inverse = 1.0f / determinant;
        const Vec3 s = Subtract(from, triangle[0]);
        const float u = Dot(s, p) * inverse;

        if (u < 0.0f || u > 1.0f)
        {
            return false;
        }

        const Vec3 q = Cross(s, edge1);
        const float v = Dot(direction, q) * inverse;

        if (v < 0.0f || u + v > 1.0f)
        {
            return false;
        }

        const float t = Dot(edge2, q) * inverse;
        return t > kEpsilon && t < 1.0f - kEpsilon;
    }

    std::vector<std::array<Vec3, 3> > m_triangles;
    std::vector<Vec3> m_centroids;
    std::vector<std::size_t> m_order;
    std::vector<Node> m_nodes;
};

// ------------------------------------------------------------------------------------------------
// Areas: which lights reach which area (id floods a light through the portals its volume touches).
// ------------------------------------------------------------------------------------------------

static int PointInArea(const ProcFile & proc, const Vec3 & point)
{
    if (proc.nodes.empty())
    {
        return proc.areaCount == 1 ? 0 : -1;
    }

    int node = 0;

    for (std::size_t steps = 0; steps <= proc.nodes.size(); ++steps)
    {
        const ProcNode & entry = proc.nodes[static_cast<std::size_t>(node)];
        const float distance = entry.plane[0] * point[0] + entry.plane[1] * point[1] + entry.plane[2] * point[2] + entry.plane[3];
        const int child = distance > 0.0f ? entry.children[0] : entry.children[1];

        if (child == 0)
        {
            return -1;
        }

        if (child < 0)
        {
            return -1 - child;
        }

        node = child;
    }

    return -1;
}

static bool BoundsOverlap(const Vec3 & aMin, const Vec3 & aMax, const Vec3 & bMin, const Vec3 & bMax)
{
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        if (aMax[axis] < bMin[axis] || bMax[axis] < aMin[axis])
        {
            return false;
        }
    }

    return true;
}

struct AreaBounds
{
    Vec3 min = {};
    Vec3 max = {};
    bool empty = true;
};

static std::vector<std::vector<std::size_t> > AreaLights(const ProcFile & proc, const std::vector<LightSource> & lights,
                                                         const std::vector<AreaBounds> & areas)
{
    std::vector<std::vector<std::size_t> > reach(static_cast<std::size_t>(proc.areaCount));
    std::vector<std::vector<std::size_t> > portalsOf(static_cast<std::size_t>(proc.areaCount));
    std::vector<AreaBounds> portalBounds(proc.portals.size());

    for (std::size_t index = 0; index < proc.portals.size(); ++index)
    {
        const ProcPortal & portal = proc.portals[index];

        for (const auto & point : portal.points)
        {
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                portalBounds[index].min[axis] = portalBounds[index].empty ? point[axis] : std::min(portalBounds[index].min[axis], point[axis]);
                portalBounds[index].max[axis] = portalBounds[index].empty ? point[axis] : std::max(portalBounds[index].max[axis], point[axis]);
            }

            portalBounds[index].empty = false;
        }

        for (const int area : portal.areas)
        {
            if (area >= 0 && area < proc.areaCount)
            {
                portalsOf[static_cast<std::size_t>(area)].push_back(index);
            }
        }
    }

    for (std::size_t light = 0; light < lights.size(); ++light)
    {
        const LightVolume & volume = lights[light].volume;
        std::vector<bool> seen(static_cast<std::size_t>(proc.areaCount), false);
        std::vector<int> queue;
        const int start = PointInArea(proc, volume.origin);

        if (start >= 0)
        {
            queue.push_back(start);
            seen[static_cast<std::size_t>(start)] = true;
        }
        else
        {
            // In solid: every area the volume touches, as id falls back to.
            for (int area = 0; area < proc.areaCount; ++area)
            {
                const AreaBounds & bounds = areas[static_cast<std::size_t>(area)];

                if (!bounds.empty && BoundsOverlap(bounds.min, bounds.max, volume.boundsMin, volume.boundsMax))
                {
                    queue.push_back(area);
                    seen[static_cast<std::size_t>(area)] = true;
                }
            }
        }

        for (std::size_t head = 0; head < queue.size(); ++head)
        {
            const int area = queue[head];

            for (const std::size_t portal : portalsOf[static_cast<std::size_t>(area)])
            {
                if (portalBounds[portal].empty || !BoundsOverlap(portalBounds[portal].min, portalBounds[portal].max, volume.boundsMin, volume.boundsMax))
                {
                    continue;
                }

                for (const int other : proc.portals[portal].areas)
                {
                    if (other >= 0 && other < proc.areaCount && !seen[static_cast<std::size_t>(other)])
                    {
                        seen[static_cast<std::size_t>(other)] = true;
                        queue.push_back(other);
                    }
                }
            }
        }

        for (const int area : queue)
        {
            reach[static_cast<std::size_t>(area)].push_back(light);
        }
    }

    return reach;
}

// ------------------------------------------------------------------------------------------------
// Charts
// ------------------------------------------------------------------------------------------------

struct Chart
{
    std::size_t area = 0;
    std::size_t surface = 0;
    std::vector<std::size_t> triangles;
    Vec3 normal = {};
    Vec3 u = {};
    Vec3 v = {};
    float luxel = 16.0f;
    // The texel rect: texel 0 starts at (x0, y0) in luxel units of (u, v).
    int x0 = 0;
    int y0 = 0;
    int width = 0;
    int height = 0;
    bool point = false;
    // Baked: intensity over exposure per texel, the exposure, per chart vertex the chroma.
    std::vector<std::uint8_t> texels;
    float exposure = 1.0f;
    std::map<std::uint32_t, Vec3> chroma;
    // Placement.
    int page = -1;
    int pageX = 0;
    int pageY = 0;
};

static Vec3 FaceNormal(const ProcSurface & surface, const std::array<std::uint32_t, 3> & corners, float & area)
{
    const Vec3 & a = surface.vertices[corners[0]].position;
    const Vec3 & b = surface.vertices[corners[1]].position;
    const Vec3 & c = surface.vertices[corners[2]].position;
    Vec3 normal = Cross(Subtract(b, a), Subtract(c, a));
    area = 0.5f * Length(normal);

    // The proc's winding is id's; face the vertex normals whichever way that makes it.
    const Vec3 smooth = Add(Add(surface.vertices[corners[0]].normal, surface.vertices[corners[1]].normal), surface.vertices[corners[2]].normal);

    if (Dot(normal, smooth) < 0.0f)
    {
        normal = Scale(normal, -1.0f);
    }

    return Normalize(normal, Normalize(smooth, Vec3{ 0.0f, 0.0f, 1.0f }));
}

static void ChartBasis(Chart & chart)
{
    // The world axis least along the normal gives u, so axial planes keep axial texels.
    std::size_t axis = 0;

    for (std::size_t candidate = 1; candidate < 3; ++candidate)
    {
        if (std::fabs(chart.normal[candidate]) < std::fabs(chart.normal[axis]))
        {
            axis = candidate;
        }
    }

    Vec3 reference = {};
    reference[axis] = 1.0f;
    chart.u = Normalize(Subtract(reference, Scale(chart.normal, Dot(reference, chart.normal))), Vec3{ 1.0f, 0.0f, 0.0f });
    chart.v = Cross(chart.normal, chart.u);
}

static std::array<float, 2> Project(const Chart & chart, const Vec3 & point)
{
    return { Dot(point, chart.u) / chart.luxel, Dot(point, chart.v) / chart.luxel };
}

static std::uint64_t PositionKey(const Vec3 & position)
{
    std::uint64_t key = 1469598103934665603ULL;

    for (const float value : position)
    {
        const auto quantized = static_cast<std::int64_t>(std::llround(static_cast<double>(value) * 8.0));
        key = (key ^ static_cast<std::uint64_t>(quantized)) * 1099511628211ULL;
    }

    return key;
}

static std::vector<Chart> BuildCharts(const ProcSurface & surface, std::size_t area, std::size_t surfaceIndex, const LightmapPolicy & policy)
{
    const std::size_t count = surface.indices.size() / 3;
    std::vector<Vec3> normals(count);
    std::vector<float> areas(count, 0.0f);
    std::vector<bool> valid(count, false);
    std::unordered_map<std::uint64_t, std::vector<std::size_t> > byEdge;

    for (std::size_t triangle = 0; triangle < count; ++triangle)
    {
        const std::array<std::uint32_t, 3> corners = { surface.indices[triangle * 3], surface.indices[triangle * 3 + 1], surface.indices[triangle * 3 + 2] };
        normals[triangle] = FaceNormal(surface, corners, areas[triangle]);
        valid[triangle] = corners[0] != corners[1] && corners[1] != corners[2] && corners[0] != corners[2];

        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            std::uint64_t a = PositionKey(surface.vertices[corners[corner]].position);
            std::uint64_t b = PositionKey(surface.vertices[corners[(corner + 1) % 3]].position);

            if (a > b)
            {
                std::swap(a, b);
            }

            byEdge[a * 31 + b].push_back(triangle);
        }
    }

    std::vector<std::size_t> seeds(count);
    std::iota(seeds.begin(), seeds.end(), std::size_t(0));
    std::stable_sort(seeds.begin(), seeds.end(), [&areas](std::size_t a, std::size_t b) { return areas[a] > areas[b]; });

    const float minimumDot = std::cos(policy.chartAngle * 3.14159265f / 180.0f);
    std::vector<int> owner(count, -1);
    std::vector<Chart> charts;

    for (const std::size_t seed : seeds)
    {
        if (owner[seed] >= 0 || !valid[seed])
        {
            continue;
        }

        Chart chart;
        chart.area = area;
        chart.surface = surfaceIndex;
        chart.normal = normals[seed];
        chart.luxel = policy.luxelSize;
        const int id = static_cast<int>(charts.size());
        owner[seed] = id;
        chart.triangles.push_back(seed);

        for (std::size_t head = 0; head < chart.triangles.size(); ++head)
        {
            const std::size_t triangle = chart.triangles[head];

            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                std::uint64_t a = PositionKey(surface.vertices[surface.indices[triangle * 3 + corner]].position);
                std::uint64_t b = PositionKey(surface.vertices[surface.indices[triangle * 3 + (corner + 1) % 3]].position);

                if (a > b)
                {
                    std::swap(a, b);
                }

                for (const std::size_t neighbour : byEdge[a * 31 + b])
                {
                    if (owner[neighbour] < 0 && valid[neighbour] && Dot(normals[neighbour], chart.normal) >= minimumDot)
                    {
                        owner[neighbour] = id;
                        chart.triangles.push_back(neighbour);
                    }
                }
            }
        }

        std::sort(chart.triangles.begin(), chart.triangles.end());
        ChartBasis(chart);
        charts.push_back(std::move(chart));
    }

    return charts;
}

// The texel rect a chart needs at its luxel size: every texel bilinear filtering reads, plus the border.
static void ChartRect(const ProcSurface & surface, Chart & chart, const LightmapPolicy & policy)
{
    float minimum[2] = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    float maximum[2] = { -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };

    for (const std::size_t triangle : chart.triangles)
    {
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const std::array<float, 2> at = Project(chart, surface.vertices[surface.indices[triangle * 3 + corner]].position);

            for (std::size_t axis = 0; axis < 2; ++axis)
            {
                minimum[axis] = std::min(minimum[axis], at[axis]);
                maximum[axis] = std::max(maximum[axis], at[axis]);
            }
        }
    }

    chart.point = maximum[0] - minimum[0] < 1.0f && maximum[1] - minimum[1] < 1.0f;
    chart.x0 = static_cast<int>(std::floor(minimum[0] - 0.5f)) - policy.border;
    chart.y0 = static_cast<int>(std::floor(minimum[1] - 0.5f)) - policy.border;
    chart.width = static_cast<int>(std::floor(maximum[0] - 0.5f)) + 1 + policy.border - chart.x0 + 1;
    chart.height = static_cast<int>(std::floor(maximum[1] - 0.5f)) + 1 + policy.border - chart.y0 + 1;
}

// ------------------------------------------------------------------------------------------------
// Baking
// ------------------------------------------------------------------------------------------------

struct TexelSample
{
    int triangle = -1;
    float distance = std::numeric_limits<float>::max();
    std::array<float, 3> weights = {};
};

// The barycentric weights of 'p' in (a, b, c), and of the closest point of the triangle to it.
static void ClosestWeights(const std::array<float, 2> & p, const std::array<float, 2> & a, const std::array<float, 2> & b,
                           const std::array<float, 2> & c, std::array<float, 3> & weights, float & distance)
{
    const float v0x = b[0] - a[0];
    const float v0y = b[1] - a[1];
    const float v1x = c[0] - a[0];
    const float v1y = c[1] - a[1];
    const float v2x = p[0] - a[0];
    const float v2y = p[1] - a[1];
    const float d00 = v0x * v0x + v0y * v0y;
    const float d01 = v0x * v1x + v0y * v1y;
    const float d11 = v1x * v1x + v1y * v1y;
    const float d20 = v2x * v0x + v2y * v0y;
    const float d21 = v2x * v1x + v2y * v1y;
    const float denominator = d00 * d11 - d01 * d01;

    if (std::fabs(denominator) > 1e-12f)
    {
        const float wb = (d11 * d20 - d01 * d21) / denominator;
        const float wc = (d00 * d21 - d01 * d20) / denominator;
        const float wa = 1.0f - wb - wc;

        if (wa >= 0.0f && wb >= 0.0f && wc >= 0.0f)
        {
            weights = { wa, wb, wc };
            distance = 0.0f;
            return;
        }
    }

    // Outside (or degenerate in the plane): the nearest point of the three edges.
    const std::array<float, 2> corners[3] = { a, b, c };
    distance = std::numeric_limits<float>::max();

    for (std::size_t edge = 0; edge < 3; ++edge)
    {
        const std::array<float, 2> & from = corners[edge];
        const std::array<float, 2> & to = corners[(edge + 1) % 3];
        const float dx = to[0] - from[0];
        const float dy = to[1] - from[1];
        const float lengthSquared = dx * dx + dy * dy;
        const float t = lengthSquared > 1e-12f ? std::clamp(((p[0] - from[0]) * dx + (p[1] - from[1]) * dy) / lengthSquared, 0.0f, 1.0f) : 0.0f;
        const float ex = from[0] + dx * t - p[0];
        const float ey = from[1] + dy * t - p[1];
        const float edgeDistance = std::sqrt(ex * ex + ey * ey);

        if (edgeDistance < distance)
        {
            distance = edgeDistance;
            weights = { 0.0f, 0.0f, 0.0f };
            weights[edge] = 1.0f - t;
            weights[(edge + 1) % 3] = t;
        }
    }
}

class Baker final
{
public:
    Baker(const LightmapScene & scene, const LightmapPolicy & policy, const Bvh & bvh, LightmapStats & stats)
        : m_scene(scene)
        , m_policy(policy)
        , m_bvh(bvh)
        , m_stats(stats)
    {
    }

    // The light at a vertex of a surface lit per vertex: one shadow ray per light, the colour
    // clipped to what the vertex colour carries.
    Vec3 VertexLight(const Vec3 & point, const Vec3 & normal, const std::vector<std::size_t> & lights)
    {
        Chart chart;
        chart.normal = normal;
        ChartBasis(chart);
        Vec3 light = Light(point, normal, chart, lights, 1);

        for (float & channel : light)
        {
            m_stats.clippedTexels += channel > kMaxScale ? 1U : 0U;
            channel = std::min(channel, kMaxScale);
        }

        return light;
    }

    // Lights the chart's texels and its vertices; texels end as intensity over exposure.
    void Bake(const ProcSurface & surface, const std::vector<std::size_t> & lights, Chart & chart)
    {
        const std::size_t texelCount = static_cast<std::size_t>(chart.width) * static_cast<std::size_t>(chart.height);
        std::vector<TexelSample> samples(texelCount);
        std::vector<Vec3> light(texelCount, Vec3{});

        if (chart.point)
        {
            // One sample: the centroid of the largest triangle.
            std::size_t largest = chart.triangles[0];
            float largestArea = -1.0f;

            for (const std::size_t triangle : chart.triangles)
            {
                float area = 0.0f;
                (void)FaceNormal(surface, { surface.indices[triangle * 3], surface.indices[triangle * 3 + 1], surface.indices[triangle * 3 + 2] }, area);

                if (area > largestArea)
                {
                    largest = triangle;
                    largestArea = area;
                }
            }

            TexelSample centre;
            centre.triangle = static_cast<int>(largest);
            centre.distance = 0.0f;
            centre.weights = { 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f };
            std::fill(samples.begin(), samples.end(), centre);
        }
        else
        {
            Rasterize(surface, chart, samples);
        }

        std::map<std::uint32_t, Vec3> vertexLight;
        std::map<std::uint32_t, float> vertexWeight;
        Vec3 pointLight = {};

        for (std::size_t texel = 0; texel < texelCount; ++texel)
        {
            const TexelSample & sample = samples[texel];

            if (sample.triangle < 0 || (chart.point && texel > 0))
            {
                continue;
            }

            const std::size_t triangle = static_cast<std::size_t>(sample.triangle);
            Vec3 point = {};
            Vec3 normal = {};

            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                const ProcVertex & vertex = surface.vertices[surface.indices[triangle * 3 + corner]];
                point = Add(point, Scale(vertex.position, sample.weights[corner]));
                normal = Add(normal, Scale(vertex.normal, sample.weights[corner]));
            }

            normal = Normalize(normal, chart.normal);
            light[texel] = Light(point, normal, chart, lights, m_policy.shadowSamples);

            if (chart.point)
            {
                pointLight = light[texel];
                continue;
            }

            if (sample.distance == 0.0f)
            {
                for (std::size_t corner = 0; corner < 3; ++corner)
                {
                    const std::uint32_t vertex = surface.indices[triangle * 3 + corner];
                    vertexLight[vertex] = Add(vertexLight[vertex], Scale(light[texel], sample.weights[corner]));
                    vertexWeight[vertex] += sample.weights[corner];
                }
            }
        }

        if (chart.point)
        {
            std::fill(light.begin(), light.end(), pointLight);
        }

        // Chroma per chart vertex; a vertex no texel centre fell inside is sampled where it is.
        for (const std::size_t triangle : chart.triangles)
        {
            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                const std::uint32_t vertex = surface.indices[triangle * 3 + corner];

                if (chart.chroma.count(vertex) != 0)
                {
                    continue;
                }

                Vec3 sum = chart.point ? pointLight : vertexLight[vertex];

                if (!chart.point && vertexWeight[vertex] <= 1e-6f)
                {
                    const ProcVertex & source = surface.vertices[vertex];
                    sum = Light(source.position, Normalize(source.normal, chart.normal), chart, lights, m_policy.shadowSamples);
                }

                const float peak = MaxChannel(sum);
                chart.chroma[vertex] = peak > 1e-5f ? Scale(sum, 1.0f / peak) : Vec3{ 1.0f, 1.0f, 1.0f };
            }
        }

        // Each texel's intensity against the chroma the GS interpolates there.
        std::vector<float> intensity(texelCount, 0.0f);
        float peak = 0.0f;

        for (std::size_t texel = 0; texel < texelCount; ++texel)
        {
            const TexelSample & sample = samples[texel];

            if (sample.triangle < 0)
            {
                continue;
            }

            const std::size_t triangle = static_cast<std::size_t>(sample.triangle);
            Vec3 chroma = {};

            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                chroma = Add(chroma, Scale(chart.chroma[surface.indices[triangle * 3 + corner]], sample.weights[corner]));
            }

            const float norm = Dot(chroma, chroma);
            intensity[texel] = norm > 1e-12f ? std::max(0.0f, Dot(light[texel], chroma) / norm) : 0.0f;
            peak = std::max(peak, intensity[texel]);
        }

        m_stats.maxIntensity = std::max(m_stats.maxIntensity, peak);
        chart.exposure = std::clamp(peak / kMaxScale, m_policy.minExposure, kMaxScale);
        chart.texels.assign(texelCount, 0);
        std::vector<bool> filled(texelCount, false);

        for (std::size_t texel = 0; texel < texelCount; ++texel)
        {
            if (samples[texel].triangle < 0)
            {
                continue;
            }

            const float value = std::round(intensity[texel] / chart.exposure * 128.0f);
            m_stats.clippedTexels += value > 255.0f ? 1U : 0U;
            chart.texels[texel] = static_cast<std::uint8_t>(std::clamp(value, 0.0f, 255.0f));
            filled[texel] = true;
        }

        Dilate(chart, filled);
    }

private:
    // Each texel's nearest triangle of the chart, within reach of the rect.
    void Rasterize(const ProcSurface & surface, const Chart & chart, std::vector<TexelSample> & samples) const
    {
        static constexpr float kReach = 2.5f;

        for (const std::size_t triangle : chart.triangles)
        {
            std::array<std::array<float, 2>, 3> corners = {};
            float minimum[2] = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
            float maximum[2] = { -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };

            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                const std::array<float, 2> at = Project(chart, surface.vertices[surface.indices[triangle * 3 + corner]].position);
                corners[corner] = { at[0] - static_cast<float>(chart.x0), at[1] - static_cast<float>(chart.y0) };

                for (std::size_t axis = 0; axis < 2; ++axis)
                {
                    minimum[axis] = std::min(minimum[axis], corners[corner][axis]);
                    maximum[axis] = std::max(maximum[axis], corners[corner][axis]);
                }
            }

            const int xBegin = std::max(0, static_cast<int>(std::floor(minimum[0] - kReach)));
            const int yBegin = std::max(0, static_cast<int>(std::floor(minimum[1] - kReach)));
            const int xEnd = std::min(chart.width, static_cast<int>(std::ceil(maximum[0] + kReach)));
            const int yEnd = std::min(chart.height, static_cast<int>(std::ceil(maximum[1] + kReach)));

            for (int y = yBegin; y < yEnd; ++y)
            {
                for (int x = xBegin; x < xEnd; ++x)
                {
                    TexelSample & sample = samples[static_cast<std::size_t>(y) * static_cast<std::size_t>(chart.width) + static_cast<std::size_t>(x)];
                    std::array<float, 3> weights = {};
                    float distance = 0.0f;
                    ClosestWeights({ static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f }, corners[0], corners[1], corners[2], weights, distance);

                    if (distance <= kReach && distance < sample.distance)
                    {
                        sample.triangle = static_cast<int>(triangle);
                        sample.distance = distance;
                        sample.weights = weights;
                    }
                }
            }
        }
    }

    // 'shadowSamples' rays per shadowing light: 1, or 4 on a 2x2 grid inside the chart's texel.
    Vec3 Light(const Vec3 & point, const Vec3 & normal, const Chart & chart, const std::vector<std::size_t> & lights, int shadowSamples)
    {
        Vec3 sum = {};

        for (const std::size_t index : lights)
        {
            const LightSource & source = m_scene.lights[index];
            const LightVolume & volume = source.volume;
            bool inside = true;

            for (std::size_t axis = 0; axis < 3 && inside; ++axis)
            {
                inside = point[axis] >= volume.boundsMin[axis] - 1.0f && point[axis] <= volume.boundsMax[axis] + 1.0f;
            }

            if (!inside)
            {
                continue;
            }

            const Vec3 color = EvaluateLight(source, point, normal);

            if (MaxChannel(color) <= 1e-5f)
            {
                continue;
            }

            float visible = 1.0f;

            if (source.castsShadows)
            {
                const int samples = shadowSamples >= 4 ? 4 : 1;
                int lit = 0;

                for (int sample = 0; sample < samples; ++sample)
                {
                    // A 2x2 grid a quarter texel from the centre, in the chart's plane.
                    const float du = samples == 1 ? 0.0f : ((sample & 1) != 0 ? 0.25f : -0.25f);
                    const float dv = samples == 1 ? 0.0f : ((sample & 2) != 0 ? 0.25f : -0.25f);
                    const Vec3 offset = Add(Scale(chart.u, du * chart.luxel), Scale(chart.v, dv * chart.luxel));
                    const Vec3 from = Add(Add(point, offset), Scale(normal, m_policy.rayOffset));
                    ++m_stats.rays;
                    lit += m_bvh.Occluded(from, volume.origin) ? 0 : 1;
                }

                visible = static_cast<float>(lit) / static_cast<float>(samples);
            }

            sum = Add(sum, Scale(color, visible));
        }

        return sum;
    }

    // Texels no triangle reached take the brightest filled neighbour.
    static void Dilate(Chart & chart, std::vector<bool> & filled)
    {
        for (int pass = 0; pass < chart.width + chart.height; ++pass)
        {
            bool changed = false;
            std::vector<bool> next = filled;

            for (int y = 0; y < chart.height; ++y)
            {
                for (int x = 0; x < chart.width; ++x)
                {
                    const std::size_t texel = static_cast<std::size_t>(y) * static_cast<std::size_t>(chart.width) + static_cast<std::size_t>(x);

                    if (filled[texel])
                    {
                        continue;
                    }

                    int best = -1;

                    for (int dy = -1; dy <= 1; ++dy)
                    {
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            const int nx = x + dx;
                            const int ny = y + dy;

                            if (nx < 0 || ny < 0 || nx >= chart.width || ny >= chart.height)
                            {
                                continue;
                            }

                            const std::size_t neighbour = static_cast<std::size_t>(ny) * static_cast<std::size_t>(chart.width) + static_cast<std::size_t>(nx);

                            if (filled[neighbour])
                            {
                                best = std::max(best, static_cast<int>(chart.texels[neighbour]));
                            }
                        }
                    }

                    if (best >= 0)
                    {
                        chart.texels[texel] = static_cast<std::uint8_t>(best);
                        next[texel] = true;
                        changed = true;
                    }
                }
            }

            filled = next;

            if (!changed)
            {
                break;
            }
        }
    }

    const LightmapScene & m_scene;
    const LightmapPolicy & m_policy;
    const Bvh & m_bvh;
    LightmapStats & m_stats;
};

// A chart whose texels vary by at most 'steps' becomes one texel (3x3 with its border).
static void CollapseFlat(Chart & chart, const LightmapPolicy & policy)
{
    if (chart.texels.empty())
    {
        return;
    }

    const auto [low, high] = std::minmax_element(chart.texels.begin(), chart.texels.end());

    if (!chart.point && static_cast<int>(*high) - static_cast<int>(*low) > policy.flatSteps)
    {
        return;
    }

    const std::uint8_t value = static_cast<std::uint8_t>((static_cast<int>(*high) + static_cast<int>(*low) + 1) / 2);
    chart.point = true;
    chart.width = 1 + 2 * policy.border;
    chart.height = chart.width;
    chart.texels.assign(static_cast<std::size_t>(chart.width) * static_cast<std::size_t>(chart.height), value);
}

// ------------------------------------------------------------------------------------------------
// Packing: a skyline per page, bottom-left placement.
// ------------------------------------------------------------------------------------------------

class Skyline final
{
public:
    explicit Skyline(int size)
        : m_size(size)
    {
        m_segments.push_back({ 0, 0, size });
    }

    bool Place(int width, int height, int & outX, int & outY)
    {
        int bestY = std::numeric_limits<int>::max();
        int bestX = 0;
        std::size_t bestIndex = m_segments.size();

        for (std::size_t index = 0; index < m_segments.size(); ++index)
        {
            const int x = m_segments[index].x;

            if (x + width > m_size)
            {
                break;
            }

            int y = 0;
            int covered = 0;

            for (std::size_t next = index; next < m_segments.size() && covered < width; ++next)
            {
                y = std::max(y, m_segments[next].y);
                covered += m_segments[next].width;
            }

            if (y + height <= m_size && y < bestY)
            {
                bestY = y;
                bestX = x;
                bestIndex = index;
            }
        }

        if (bestIndex == m_segments.size())
        {
            return false;
        }

        // Replace the covered span with the new top.
        std::vector<Segment> segments(m_segments.begin(), m_segments.begin() + static_cast<std::ptrdiff_t>(bestIndex));
        segments.push_back({ bestX, bestY + height, width });

        for (std::size_t index = bestIndex; index < m_segments.size(); ++index)
        {
            const Segment & segment = m_segments[index];
            const int end = segment.x + segment.width;

            if (end <= bestX + width)
            {
                continue;
            }

            const int start = std::max(segment.x, bestX + width);
            segments.push_back({ start, segment.y, end - start });
        }

        // Merge equal heights.
        m_segments.clear();

        for (const Segment & segment : segments)
        {
            if (!m_segments.empty() && m_segments.back().y == segment.y)
            {
                m_segments.back().width += segment.width;
            }
            else
            {
                m_segments.push_back(segment);
            }
        }

        outX = bestX;
        outY = bestY;
        return true;
    }

private:
    struct Segment
    {
        int x = 0;
        int y = 0;
        int width = 0;
    };

    int m_size = 0;
    std::vector<Segment> m_segments;
};

} // namespace

bool BakeLightmaps(const LightmapScene & scene, const LightmapPolicy & policy, WorldLighting & lighting, std::vector<LightmapPage> & pages,
                   LightmapStats & stats, std::string & error)
{
    lighting = {};
    pages.clear();
    stats = {};

    if (scene.proc == nullptr || policy.pageSize < 16 || policy.pageSize > 512 || policy.border < 0 || !(policy.luxelSize > 0.0f) ||
        !(policy.minExposure > 0.0f) || policy.minExposure > kMaxScale)
    {
        error = "bad lightmap policy";
        return false;
    }

    const ProcFile & proc = *scene.proc;
    std::vector<const ProcModel *> models(static_cast<std::size_t>(proc.areaCount), nullptr);
    std::vector<AreaBounds> areaBounds(static_cast<std::size_t>(proc.areaCount));
    std::vector<std::array<Vec3, 3> > occluders = scene.occluders;

    for (int area = 0; area < proc.areaCount; ++area)
    {
        const ProcModel * model = FindAreaModel(proc, area);

        if (model == nullptr)
        {
            error = "missing area model " + std::to_string(area);
            return false;
        }

        models[static_cast<std::size_t>(area)] = model;
        AreaBounds & bounds = areaBounds[static_cast<std::size_t>(area)];

        for (const ProcSurface & surface : model->surfaces)
        {
            const auto found = scene.materials.find(surface.material);
            const bool casts = found != scene.materials.end() && found->second.castsShadow;

            for (const ProcVertex & vertex : surface.vertices)
            {
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    bounds.min[axis] = bounds.empty ? vertex.position[axis] : std::min(bounds.min[axis], vertex.position[axis]);
                    bounds.max[axis] = bounds.empty ? vertex.position[axis] : std::max(bounds.max[axis], vertex.position[axis]);
                }

                bounds.empty = false;
            }

            for (std::size_t index = 0; casts && index + 2 < surface.indices.size(); index += 3)
            {
                occluders.push_back({ surface.vertices[surface.indices[index]].position, surface.vertices[surface.indices[index + 1]].position,
                                      surface.vertices[surface.indices[index + 2]].position });
            }
        }
    }

    const Bvh bvh(std::move(occluders));
    stats.occluders = bvh.Size();
    const std::vector<std::vector<std::size_t> > areaLights = AreaLights(proc, scene.lights, areaBounds);
    Baker baker(scene, policy, bvh, stats);
    std::vector<Chart> charts;
    std::map<std::pair<std::size_t, std::size_t>, std::vector<Vec3> > vertexLit;
    lighting.areas.resize(static_cast<std::size_t>(proc.areaCount));

    for (std::size_t area = 0; area < models.size(); ++area)
    {
        const ProcModel & model = *models[area];

        for (std::size_t surfaceIndex = 0; surfaceIndex < model.surfaces.size(); ++surfaceIndex)
        {
            const ProcSurface & surface = model.surfaces[surfaceIndex];
            const auto found = scene.materials.find(surface.material);

            if (found == scene.materials.end() || !found->second.lit || surface.indices.empty())
            {
                continue;
            }

            ++stats.litSurfaces;

            // The lights whose volume reaches the surface, from those its area receives.
            AreaBounds bounds;

            for (const ProcVertex & vertex : surface.vertices)
            {
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    bounds.min[axis] = bounds.empty ? vertex.position[axis] : std::min(bounds.min[axis], vertex.position[axis]);
                    bounds.max[axis] = bounds.empty ? vertex.position[axis] : std::max(bounds.max[axis], vertex.position[axis]);
                }

                bounds.empty = false;
            }

            std::vector<std::size_t> lights;

            for (const std::size_t light : areaLights[area])
            {
                const LightVolume & volume = scene.lights[light].volume;

                if (BoundsOverlap(bounds.min, bounds.max, volume.boundsMin, volume.boundsMax))
                {
                    lights.push_back(light);
                }
            }

            stats.surfaceLights += lights.size();
            stats.maxSurfaceLights = std::max(stats.maxSurfaceLights, lights.size());

            if (found->second.perforated)
            {
                std::vector<Vec3> & colors = vertexLit[{ area, surfaceIndex }];

                for (const ProcVertex & vertex : surface.vertices)
                {
                    colors.push_back(baker.VertexLight(vertex.position, Normalize(vertex.normal, Vec3{ 0.0f, 0.0f, 1.0f }), lights));
                }

                ++stats.vertexLitSurfaces;
                continue;
            }

            for (Chart & chart : BuildCharts(surface, area, surfaceIndex, policy))
            {
                ChartRect(surface, chart, policy);

                // Too large for a page: coarser texels until it fits.
                while (chart.width > policy.pageSize || chart.height > policy.pageSize)
                {
                    const float factor = static_cast<float>(std::max(chart.width, chart.height)) / static_cast<float>(policy.pageSize - 2 - 2 * policy.border);
                    chart.luxel *= std::max(factor, 1.01f);
                    ChartRect(surface, chart, policy);
                    ++stats.scaledCharts;
                }

                if (chart.point)
                {
                    chart.width = 1 + 2 * policy.border;
                    chart.height = chart.width;
                }

                baker.Bake(surface, lights, chart);
                CollapseFlat(chart, policy);
                charts.push_back(std::move(chart));
            }
        }
    }

    // Pack area by area, tallest first, into the last open page or a new one.
    std::vector<std::size_t> order(charts.size());
    std::iota(order.begin(), order.end(), std::size_t(0));
    std::stable_sort(order.begin(), order.end(), [&charts](std::size_t a, std::size_t b)
                     {
                         return charts[a].area != charts[b].area ? charts[a].area < charts[b].area : charts[a].height > charts[b].height;
                     });
    std::vector<Skyline> skylines;

    for (const std::size_t index : order)
    {
        Chart & chart = charts[index];
        bool placed = false;

        for (std::size_t back = 0; back < 2 && back < skylines.size() && !placed; ++back)
        {
            const std::size_t page = skylines.size() - 1 - back;

            if (skylines[page].Place(chart.width, chart.height, chart.pageX, chart.pageY))
            {
                chart.page = static_cast<int>(page);
                placed = true;
            }
        }

        if (!placed)
        {
            if (skylines.size() >= kD3wdMaxLightmapPages)
            {
                error = "the lightmaps need more than " + std::to_string(kD3wdMaxLightmapPages) + " pages";
                return false;
            }

            skylines.emplace_back(policy.pageSize);
            pages.push_back({ policy.pageSize, std::vector<std::uint8_t>(static_cast<std::size_t>(policy.pageSize) * static_cast<std::size_t>(policy.pageSize), 0) });

            if (!skylines.back().Place(chart.width, chart.height, chart.pageX, chart.pageY))
            {
                error = "a chart does not fit a lightmap page";
                return false;
            }

            chart.page = static_cast<int>(skylines.size()) - 1;
        }

        LightmapPage & page = pages[static_cast<std::size_t>(chart.page)];

        for (int y = 0; y < chart.height; ++y)
        {
            for (int x = 0; x < chart.width; ++x)
            {
                page.texels[static_cast<std::size_t>(chart.pageY + y) * static_cast<std::size_t>(page.size) + static_cast<std::size_t>(chart.pageX + x)] =
                    chart.texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(chart.width) + static_cast<std::size_t>(x)];
            }
        }

        ++stats.charts;
        stats.pointCharts += chart.point ? 1U : 0U;
        stats.texels += static_cast<std::size_t>(chart.width) * static_cast<std::size_t>(chart.height);
    }

    stats.pages = pages.size();
    lighting.pages = static_cast<std::uint32_t>(pages.size());

    // The lit surfaces: a vertex per (source vertex, chart), every triangle on its chart's page.
    std::map<std::pair<std::size_t, std::size_t>, std::vector<std::size_t> > chartsOf;

    for (std::size_t index = 0; index < charts.size(); ++index)
    {
        chartsOf[{ charts[index].area, charts[index].surface }].push_back(index);
    }

    for (std::size_t area = 0; area < models.size(); ++area)
    {
        const ProcModel & model = *models[area];
        std::vector<WorldLitSurface> & surfaces = lighting.areas[area];
        surfaces.resize(model.surfaces.size());

        for (std::size_t surfaceIndex = 0; surfaceIndex < model.surfaces.size(); ++surfaceIndex)
        {
            const ProcSurface & surface = model.surfaces[surfaceIndex];
            WorldLitSurface & output = surfaces[surfaceIndex];
            const auto found = chartsOf.find({ area, surfaceIndex });

            if (found == chartsOf.end())
            {
                // Unlit (full vertex colour) or lit per vertex: the source's vertices, no page.
                const auto perVertex = vertexLit.find({ area, surfaceIndex });

                for (std::size_t index = 0; index < surface.vertices.size(); ++index)
                {
                    const ProcVertex & vertex = surface.vertices[index];
                    WorldLitVertex copy;
                    copy.position = vertex.position;
                    copy.uv = vertex.uv;
                    copy.normal = vertex.normal;

                    if (perVertex != vertexLit.end())
                    {
                        copy.color = perVertex->second[index];
                    }

                    output.vertices.push_back(copy);
                }

                output.indices = surface.indices;
                output.pages.assign(surface.indices.size() / 3, kBatchNoLightmap);
                continue;
            }

            std::map<std::pair<std::uint32_t, std::size_t>, std::uint32_t> remap;

            for (const std::size_t chartIndex : found->second)
            {
                const Chart & chart = charts[chartIndex];
                const float scale = 1.0f / static_cast<float>(policy.pageSize);

                for (const std::size_t triangle : chart.triangles)
                {
                    for (std::size_t corner = 0; corner < 3; ++corner)
                    {
                        const std::uint32_t source = surface.indices[triangle * 3 + corner];
                        const auto [entry, inserted] = remap.emplace(std::make_pair(source, chartIndex), static_cast<std::uint32_t>(output.vertices.size()));

                        if (inserted)
                        {
                            const ProcVertex & vertex = surface.vertices[source];
                            WorldLitVertex lit;
                            lit.position = vertex.position;
                            lit.uv = vertex.uv;
                            lit.normal = vertex.normal;
                            float x = static_cast<float>(policy.border) + 0.5f;
                            float y = x;

                            if (!chart.point)
                            {
                                const std::array<float, 2> at = Project(chart, vertex.position);
                                x = at[0] - static_cast<float>(chart.x0);
                                y = at[1] - static_cast<float>(chart.y0);
                            }

                            lit.lightmapUv = { (static_cast<float>(chart.pageX) + x) * scale, (static_cast<float>(chart.pageY) + y) * scale };
                            const Vec3 chroma = chart.chroma.at(source);

                            for (std::size_t channel = 0; channel < 3; ++channel)
                            {
                                const float value = chroma[channel] * chart.exposure;
                                stats.clippedTexels += value > kMaxScale ? 1U : 0U;
                                lit.color[channel] = std::min(value, kMaxScale);
                            }

                            output.vertices.push_back(lit);
                        }

                        output.indices.push_back(entry->second);
                    }

                    output.pages.push_back(static_cast<std::uint16_t>(chart.page));
                }
            }

            // Degenerate triangles no chart took keep their place, unlit.
            std::set<std::size_t> charted;

            for (const std::size_t chartIndex : found->second)
            {
                charted.insert(charts[chartIndex].triangles.begin(), charts[chartIndex].triangles.end());
            }

            for (std::size_t triangle = 0; triangle * 3 + 2 < surface.indices.size(); ++triangle)
            {
                if (charted.count(triangle) != 0)
                {
                    continue;
                }

                for (std::size_t corner = 0; corner < 3; ++corner)
                {
                    const ProcVertex & vertex = surface.vertices[surface.indices[triangle * 3 + corner]];
                    WorldLitVertex copy;
                    copy.position = vertex.position;
                    copy.uv = vertex.uv;
                    copy.normal = vertex.normal;
                    output.indices.push_back(static_cast<std::uint32_t>(output.vertices.size()));
                    output.vertices.push_back(copy);
                }

                output.pages.push_back(output.pages.empty() ? kBatchNoLightmap : output.pages.back());
            }
        }
    }

    return true;
}

} // namespace assets
