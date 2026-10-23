// ================================================================================================
// File: masks.cpp
// Brief: Prove convex triangle coverage and preserve indexed seams through bounded CPU mask clips.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/masks.h"

#include <bit>
#include <cstdint>

namespace ps2::ui
{
namespace
{

static constexpr float kCoordinateLimit = 32768.0f;
static constexpr float kUvLimit = 1023.0f;
static constexpr float kWeightTolerance = 1.0e-5f;
static constexpr int kOriginalEdges = 3;
static constexpr int kMaxPlanes = kMaxActiveMasks * kMaxMaskPoints;
static constexpr int kConstraintWords = (kOriginalEdges + kMaxPlanes + 31) / 32;
static constexpr int kOriginalEndpoints[kOriginalEdges][2] = { { 0, 1 }, { 1, 2 }, { 2, 0 } };

struct Constraints
{
    std::uint32_t words[kConstraintWords]{};
};

struct ClipVertex
{
    float x;
    float y;
    float s;
    float t;
    float r;
    float g;
    float b;
    float a;
    Constraints constraints;
};

struct Plane
{
    MaskPoint from;
    MaskPoint to;
};

static bool PortableFloat(float value)
{
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t exponent = bits & 0x7F800000U;
    return (exponent != 0x7F800000U) && ((exponent != 0U) || ((bits & 0x007FFFFFU) == 0U));
}

static bool Bounded(float value, float limit)
{
    return PortableFloat(value) && (value >= -limit) && (value <= limit);
}

static bool ValidPoint(MaskPoint point)
{
    return Bounded(point.x, kCoordinateLimit) && Bounded(point.y, kCoordinateLimit);
}

static bool Same(MaskPoint first, MaskPoint second)
{
    return (first.x == second.x) && (first.y == second.y);
}

static bool Before(MaskPoint first, MaskPoint second)
{
    return (first.x < second.x) || ((first.x == second.x) && (first.y < second.y));
}

static MaskPoint Position(const ClipVertex & vertex)
{
    return { vertex.x, vertex.y };
}

static float Cross(MaskPoint first, MaskPoint second, MaskPoint third)
{
    return ((second.x - first.x) * (third.y - first.y)) -
           ((second.y - first.y) * (third.x - first.x));
}

static bool Between(MaskPoint first, MaskPoint middle, MaskPoint last)
{
    return ((middle.x >= first.x && middle.x <= last.x) || (middle.x <= first.x && middle.x >= last.x)) &&
           ((middle.y >= first.y && middle.y <= last.y) || (middle.y <= first.y && middle.y >= last.y));
}

static bool ValidMask(const ConvexMask & mask)
{
    if (mask.count == 0)
    {
        return true;
    }

    if (mask.count < 3 || mask.count > kMaxMaskPoints)
    {
        return false;
    }

    for (int i = 0; i < mask.count; ++i)
    {
        if (!ValidPoint(mask.points[i]))
        {
            return false;
        }

        for (int j = 0; j < i; ++j)
        {
            if (Same(mask.points[i], mask.points[j]))
            {
                return false;
            }
        }
    }

    for (int i = 0; i < mask.count; ++i)
    {
        bool hasInterior = false;
        for (int j = 0; j < mask.count; ++j)
        {
            const float distance = Cross(mask.points[i], mask.points[(i + 1) % mask.count], mask.points[j]);
            if (!PortableFloat(distance) || distance < 0.0f)
            {
                return false;
            }

            hasInterior = hasInterior || (distance > 0.0f);
        }

        if (!hasInterior)
        {
            return false;
        }
    }

    return true;
}

static void AddConstraint(Constraints & constraints, int index)
{
    constraints.words[index / 32] |= std::uint32_t{ 1 } << static_cast<unsigned int>(index % 32);
}

static bool HasConstraint(const Constraints & constraints, int index)
{
    return (constraints.words[index / 32] & (std::uint32_t{ 1 } << static_cast<unsigned int>(index % 32))) != 0U;
}

static Constraints Common(const Constraints & first, const Constraints & second)
{
    Constraints result{};
    for (int i = 0; i < kConstraintWords; ++i)
    {
        result.words[i] = first.words[i] & second.words[i];
    }

    return result;
}

static void Merge(Constraints & first, const Constraints & second)
{
    for (int i = 0; i < kConstraintWords; ++i)
    {
        first.words[i] |= second.words[i];
    }
}

static float ClampUnit(float value)
{
    if (value < 0.0f)
    {
        return 0.0f;
    }

    return value > 1.0f ? 1.0f : value;
}

static bool ValidWeight(float value)
{
    return PortableFloat(value) && value >= -kWeightTolerance && value <= 1.0f + kWeightTolerance;
}

static float Interpolate(float first, float second, float fraction)
{
    return first + ((second - first) * fraction);
}

static bool ValidVertex(const ClipVertex & vertex)
{
    return ValidPoint(Position(vertex)) && Bounded(vertex.s, kUvLimit) && Bounded(vertex.t, kUvLimit) &&
           Bounded(vertex.r, 256.0f) && Bounded(vertex.g, 256.0f) &&
           Bounded(vertex.b, 256.0f) && Bounded(vertex.a, 256.0f);
}

static float Weighted(float first, float second, float third, const float (&weights)[3])
{
    // Preserve exact constant components instead of allowing the sum of weights to perturb them.
    return (first == second && second == third) ? first : first * weights[0] + second * weights[1] + third * weights[2];
}

static bool OriginalEdgeIntersection(const ClipVertex (&original)[3], int edge,
                                     const Plane & plane, ClipVertex & output)
{
    int fromIndex = kOriginalEndpoints[edge][0];
    int toIndex = kOriginalEndpoints[edge][1];
    if (Before(Position(original[toIndex]), Position(original[fromIndex])))
    {
        const int temporary = fromIndex;
        fromIndex = toIndex;
        toIndex = temporary;
    }

    const ClipVertex & from = original[fromIndex];
    const ClipVertex & to = original[toIndex];
    const float firstDistance = Cross(plane.from, plane.to, Position(from));
    const float secondDistance = Cross(plane.from, plane.to, Position(to));
    const float denominator = firstDistance - secondDistance;
    if (!PortableFloat(firstDistance) || !PortableFloat(secondDistance) ||
        !PortableFloat(denominator) || denominator == 0.0f)
    {
        return false;
    }

    const float fraction = firstDistance / denominator;
    if (!ValidWeight(fraction))
    {
        return false;
    }

    const float weight = ClampUnit(fraction);
    output = {
        Interpolate(from.x, to.x, weight), Interpolate(from.y, to.y, weight), Interpolate(from.s, to.s, weight), Interpolate(from.t, to.t, weight), Interpolate(from.r, to.r, weight), Interpolate(from.g, to.g, weight), Interpolate(from.b, to.b, weight), Interpolate(from.a, to.a, weight), {}
    };
    if (weight == 0.0f)
    {
        output.constraints = from.constraints;
    }
    else if (weight == 1.0f)
    {
        output.constraints = to.constraints;
    }

    return ValidVertex(output);
}

static bool PlaneBefore(const Plane & first, const Plane & second)
{
    return Same(first.from, second.from) ? Before(first.to, second.to) : Before(first.from, second.from);
}

static bool PlaneIntersection(const ClipVertex (&original)[3], const Plane & a, const Plane & b,
                              ClipVertex & output)
{
    const Plane & first = PlaneBefore(a, b) ? a : b;
    const Plane & second = PlaneBefore(a, b) ? b : a;
    const float dx = first.to.x - first.from.x;
    const float dy = first.to.y - first.from.y;
    const float secondDx = second.to.x - second.from.x;
    const float secondDy = second.to.y - second.from.y;
    const float denominator = (dx * secondDy) - (dy * secondDx);
    if (!PortableFloat(denominator) || denominator == 0.0f)
    {
        return false;
    }

    const float numerator = ((second.from.x - first.from.x) * secondDy) -
                            ((second.from.y - first.from.y) * secondDx);
    const float fraction = numerator / denominator;
    // Valid crossings lie within the admitted coordinate domain. Bound the intermediate ratio
    // before multiplication too, since the EE saturates overflow rather than producing IEEE Inf.
    if (!Bounded(fraction, 1.0e10f))
    {
        return false;
    }

    const MaskPoint point{ first.from.x + fraction * dx, first.from.y + fraction * dy };
    if (!ValidPoint(point))
    {
        return false;
    }

    // Canonical vertex order keeps a reversed input triangle's barycentric arithmetic identical.
    int order[3] = { 0, 1, 2 };
    for (int i = 1; i < 3; ++i)
    {
        for (int j = i; j > 0 && Before(Position(original[order[j]]), Position(original[order[j - 1]])); --j)
        {
            const int temporary = order[j];
            order[j] = order[j - 1];
            order[j - 1] = temporary;
        }
    }

    const ClipVertex & v0 = original[order[0]];
    const ClipVertex & v1 = original[order[1]];
    const ClipVertex & v2 = original[order[2]];
    const float area = Cross(Position(v0), Position(v1), Position(v2));
    if (!PortableFloat(area) || area == 0.0f)
    {
        return false;
    }

    float weights[3] = {
        Cross(point, Position(v1), Position(v2)) / area,
        Cross(Position(v0), point, Position(v2)) / area,
        Cross(Position(v0), Position(v1), point) / area
    };
    for (float & weight : weights)
    {
        if (!ValidWeight(weight))
        {
            return false;
        }

        weight = ClampUnit(weight);
    }

    const float sum = weights[0] + weights[1] + weights[2];
    if (!PortableFloat(sum) || sum < 0.5f || sum > 1.5f)
    {
        return false;
    }

    for (float & weight : weights)
    {
        weight /= sum;
    }

    output = {
        point.x, point.y, Weighted(v0.s, v1.s, v2.s, weights), Weighted(v0.t, v1.t, v2.t, weights), Weighted(v0.r, v1.r, v2.r, weights), Weighted(v0.g, v1.g, v2.g, weights), Weighted(v0.b, v1.b, v2.b, weights), Weighted(v0.a, v1.a, v2.a, weights), {}
    };

    // A mask corner can also lie on an original edge or vertex. Retain that identity so a later
    // plane still reconstructs adjacent indexed triangles from their common original endpoints.
    for (int i = 0; i < kOriginalEdges; ++i)
    {
        if (Cross(Position(original[kOriginalEndpoints[i][0]]), Position(original[kOriginalEndpoints[i][1]]), point) == 0.0f)
        {
            AddConstraint(output.constraints, i);
        }
    }

    return ValidVertex(output);
}

static bool Intersection(const ClipVertex (&original)[3], const Plane (&planes)[kMaxPlanes],
                         int planeIndex, const ClipVertex & first, const ClipVertex & second,
                         ClipVertex & output)
{
    const Constraints common = Common(first.constraints, second.constraints);
    for (int i = 0; i < kOriginalEdges + planeIndex; ++i)
    {
        if (HasConstraint(common, i))
        {
            ClipVertex result{};
            const bool reconstructed = i < kOriginalEdges ? OriginalEdgeIntersection(original, i, planes[planeIndex], result) : PlaneIntersection(original, planes[i - kOriginalEdges], planes[planeIndex], result);
            if (reconstructed)
            {
                Merge(result.constraints, common);
                AddConstraint(result.constraints, kOriginalEdges + planeIndex);
                output = result;
                return true;
            }
        }
    }

    return false;
}

static bool Append(ClipVertex (&output)[kMaxMaskClipVertices], int & count, const ClipVertex & vertex)
{
    if (count > 0 && Same(Position(output[count - 1]), Position(vertex)))
    {
        Merge(output[count - 1].constraints, vertex.constraints);
        return true;
    }

    if (count == kMaxMaskClipVertices)
    {
        return false;
    }

    output[count++] = vertex;
    return true;
}

static bool Clip(const ClipVertex (&original)[3], const Plane (&planes)[kMaxPlanes], int planeIndex,
                 const ClipVertex (&input)[kMaxMaskClipVertices], int inputCount,
                 ClipVertex (&output)[kMaxMaskClipVertices], int & outputCount)
{
    outputCount = 0;
    ClipVertex previous = input[inputCount - 1];
    float previousDistance = Cross(planes[planeIndex].from, planes[planeIndex].to, Position(previous));
    if (!PortableFloat(previousDistance))
    {
        return false;
    }

    for (int i = 0; i < inputCount; ++i)
    {
        ClipVertex current = input[i];
        const float distance = Cross(planes[planeIndex].from, planes[planeIndex].to, Position(current));
        if (!PortableFloat(distance))
        {
            return false;
        }

        if ((previousDistance >= 0.0f) != (distance >= 0.0f))
        {
            ClipVertex intersection{};
            if (!Intersection(original, planes, planeIndex, previous, current, intersection) ||
                !Append(output, outputCount, intersection))
            {
                return false;
            }
        }

        if (distance >= 0.0f)
        {
            if (distance == 0.0f)
            {
                AddConstraint(current.constraints, kOriginalEdges + planeIndex);
            }

            if (!Append(output, outputCount, current))
            {
                return false;
            }
        }

        previous = current;
        previousDistance = distance;
    }

    if (outputCount > 1 && Same(Position(output[0]), Position(output[outputCount - 1])))
    {
        Merge(output[0].constraints, output[outputCount - 1].constraints);
        --outputCount;
    }

    return true;
}

static std::uint8_t RoundColor(float value)
{
    if (value < 0.0f)
    {
        value = 0.0f;
    }
    else if (value > 255.0f)
    {
        value = 255.0f;
    }

    return static_cast<std::uint8_t>(value + 0.5f);
}

} // namespace

void MaskBuilder::Reset()
{
    m_edgeCount = 0;
    m_triangleCount = 0;
    m_valid = true;
}

bool MaskBuilder::AddTriangle(const MaskPoint (&triangle)[3])
{
    if (!m_valid || m_triangleCount == kMaxMaskTriangles)
    {
        m_valid = false;
        return false;
    }

    ++m_triangleCount;
    for (const MaskPoint & point : triangle)
    {
        if (!ValidPoint(point))
        {
            m_valid = false;
            return false;
        }
    }

    const float area = Cross(triangle[0], triangle[1], triangle[2]);
    if (!PortableFloat(area))
    {
        m_valid = false;
        return false;
    }

    if (area == 0.0f)
    {
        return true;
    }

    const MaskPoint points[3] = { triangle[0], triangle[area > 0.0f ? 1 : 2], triangle[area > 0.0f ? 2 : 1] };
    for (int i = 0; i < 3; ++i)
    {
        const MaskPoint from = points[i];
        const MaskPoint to = points[(i + 1) % 3];
        bool matched = false;
        for (int j = 0; j < m_edgeCount; ++j)
        {
            Edge & edge = m_edges[j];
            if (Same(edge.from, from) && Same(edge.to, to))
            {
                m_valid = false;
                return false;
            }

            if (Same(edge.from, to) && Same(edge.to, from))
            {
                if (edge.paired)
                {
                    m_valid = false;
                    return false;
                }

                edge.paired = true;
                matched = true;
                break;
            }
        }

        if (!matched)
        {
            if (m_edgeCount == kMaxEdges)
            {
                m_valid = false;
                return false;
            }

            m_edges[m_edgeCount++] = { from, to, false };
        }
    }

    return true;
}

bool MaskBuilder::Finish(ConvexMask & output) const
{
    if (!m_valid)
    {
        return false;
    }

    int firstEdge = -1;
    int boundaryCount = 0;
    for (int i = 0; i < m_edgeCount; ++i)
    {
        if (m_edges[i].paired)
        {
            continue;
        }

        firstEdge = firstEdge < 0 ? i : firstEdge;
        ++boundaryCount;
        int outgoing = 0;
        int incoming = 0;
        for (int j = 0; j < m_edgeCount; ++j)
        {
            if (!m_edges[j].paired)
            {
                outgoing += Same(m_edges[i].from, m_edges[j].from) ? 1 : 0;
                incoming += Same(m_edges[i].from, m_edges[j].to) ? 1 : 0;
            }
        }

        if (outgoing != 1 || incoming != 1)
        {
            return false;
        }
    }

    if (boundaryCount == 0)
    {
        if (m_edgeCount != 0)
        {
            return false;
        }

        output.count = 0;
        return true;
    }

    if (boundaryCount < 3)
    {
        return false;
    }

    MaskPoint boundary[kMaxEdges]{};
    int current = firstEdge;
    int count = 0;
    do
    {
        if (count == boundaryCount)
        {
            return false;
        }

        boundary[count++] = m_edges[current].from;
        int next = -1;
        for (int i = 0; i < m_edgeCount; ++i)
        {
            if (!m_edges[i].paired && Same(m_edges[current].to, m_edges[i].from))
            {
                next = i;
                break;
            }
        }

        if (next < 0)
        {
            return false;
        }

        current = next;
    } while (current != firstEdge);

    if (count != boundaryCount)
    {
        return false;
    }

    bool changed = true;
    while (changed && count > 3)
    {
        changed = false;
        for (int i = 0; i < count; ++i)
        {
            const MaskPoint previous = boundary[(i + count - 1) % count];
            const MaskPoint next = boundary[(i + 1) % count];
            if (Cross(previous, boundary[i], next) == 0.0f && Between(previous, boundary[i], next))
            {
                for (int j = i; j < count - 1; ++j)
                {
                    boundary[j] = boundary[j + 1];
                }

                --count;
                changed = true;
                break;
            }
        }
    }

    if (count > kMaxMaskPoints)
    {
        return false;
    }

    ConvexMask result{};
    result.count = count;
    for (int i = 0; i < count; ++i)
    {
        result.points[i] = boundary[i];
    }

    if (!ValidMask(result))
    {
        return false;
    }

    // Every CCW triangle edge has either one boundary incidence or one cancelling reverse incidence.
    // A single convex boundary therefore has winding one inside and zero outside: no hidden overlap.
    output = result;
    return true;
}

bool ClipMaskTriangle(const Vertex (&input)[3], const ConvexMask * masks, int maskCount,
                      Vertex (&output)[kMaxMaskClipVertices], int & count)
{
    if (maskCount < 0 || maskCount > kMaxActiveMasks || (maskCount > 0 && masks == nullptr))
    {
        return false;
    }

    ClipVertex original[3]{};
    for (int i = 0; i < 3; ++i)
    {
        const Vertex & vertex = input[i];
        if (!ValidPoint({ vertex.x, vertex.y }) || !Bounded(vertex.s, kUvLimit) || !Bounded(vertex.t, kUvLimit))
        {
            return false;
        }

        original[i] = {
            vertex.x, vertex.y, vertex.s, vertex.t, static_cast<float>(vertex.r), static_cast<float>(vertex.g), static_cast<float>(vertex.b), static_cast<float>(vertex.a), {}
        };
        AddConstraint(original[i].constraints, i);
        AddConstraint(original[i].constraints, (i + 2) % 3);
    }

    Plane planes[kMaxPlanes]{};
    int planeCount = 0;
    bool empty = false;
    for (int i = 0; i < maskCount; ++i)
    {
        const ConvexMask & mask = masks[i];
        if (!ValidMask(mask))
        {
            return false;
        }

        empty = empty || mask.count == 0;
        for (int j = 0; j < mask.count; ++j)
        {
            planes[planeCount++] = { mask.points[j], mask.points[(j + 1) % mask.count] };
        }
    }

    if (empty)
    {
        count = 0;
        return true;
    }

    if (maskCount == 0)
    {
        for (int i = 0; i < 3; ++i)
        {
            output[i] = input[i];
        }

        count = 3;
        return true;
    }

    const float area = Cross(Position(original[0]), Position(original[1]), Position(original[2]));
    if (!PortableFloat(area))
    {
        return false;
    }

    if (area == 0.0f)
    {
        count = 0;
        return true;
    }

    ClipVertex scratch[2][kMaxMaskClipVertices]{};
    for (int i = 0; i < 3; ++i)
    {
        scratch[0][i] = original[i];
    }

    int active = 0;
    int clippedCount = 3;
    for (int i = 0; i < planeCount; ++i)
    {
        const int next = 1 - active;
        int nextCount = 0;
        if (!Clip(original, planes, i, scratch[active], clippedCount, scratch[next], nextCount))
        {
            return false;
        }

        if (nextCount < 3)
        {
            count = 0;
            return true;
        }

        active = next;
        clippedCount = nextCount;
    }

    bool hasArea = false;
    for (int i = 1; i < clippedCount - 1; ++i)
    {
        const float clippedArea = Cross(Position(scratch[active][0]), Position(scratch[active][i]),
                                        Position(scratch[active][i + 1]));
        if (!PortableFloat(clippedArea))
        {
            return false;
        }

        hasArea = hasArea || clippedArea != 0.0f;
    }

    if (!hasArea)
    {
        count = 0;
        return true;
    }

    for (int i = 0; i < clippedCount; ++i)
    {
        const ClipVertex & vertex = scratch[active][i];
        output[i] = {
            vertex.x, vertex.y, vertex.s, vertex.t,
            RoundColor(vertex.r), RoundColor(vertex.g), RoundColor(vertex.b), RoundColor(vertex.a)
        };
    }

    count = clippedCount;
    return true;
}

bool ClipMaskLine(const Vertex (&input)[2], const ConvexMask * masks, int maskCount, Vertex (&output)[2], bool & visible)
{
    if (maskCount < 0 || maskCount > kMaxActiveMasks || (maskCount > 0 && masks == nullptr))
    {
        return false;
    }

    for (const Vertex & vertex : input)
    {
        if (!ValidPoint({ vertex.x, vertex.y }))
        {
            return false;
        }
    }

    float enter = 0.0f;
    float leave = 1.0f;
    const MaskPoint first{ input[0].x, input[0].y };
    const MaskPoint last{ input[1].x, input[1].y };

    for (int i = 0; i < maskCount; ++i)
    {
        const ConvexMask & mask = masks[i];

        if (!ValidMask(mask))
        {
            return false;
        }

        if (mask.count == 0)
        {
            enter = 1.0f;
            leave = 0.0f;
        }

        // Inside every edge of a CCW polygon: the cross product from the edge is not negative.
        for (int j = 0; j < mask.count; ++j)
        {
            const MaskPoint from = mask.points[j];
            const MaskPoint to = mask.points[(j + 1) % mask.count];
            const float start = Cross(from, to, first);
            const float end = Cross(from, to, last);

            if (!PortableFloat(start) || !PortableFloat(end))
            {
                return false;
            }

            if (start < 0.0f && end < 0.0f)
            {
                enter = 1.0f;
                leave = 0.0f;
            }
            else if (start < 0.0f || end < 0.0f)
            {
                const float fraction = ClampUnit(start / (start - end));

                if (start < 0.0f)
                {
                    enter = fraction > enter ? fraction : enter;
                }
                else
                {
                    leave = fraction < leave ? fraction : leave;
                }
            }
        }
    }

    visible = enter < leave;

    if (!visible)
    {
        return true;
    }

    const auto At = [&input](float fraction)
    {
        const Vertex & from = input[0];
        const Vertex & to = input[1];
        const auto Channel = [fraction](std::uint8_t start, std::uint8_t end)
        {
            return RoundColor(Interpolate(static_cast<float>(start), static_cast<float>(end), fraction));
        };
        return Vertex{ Interpolate(from.x, to.x, fraction), Interpolate(from.y, to.y, fraction), 0.0f, 0.0f,
                       Channel(from.r, to.r), Channel(from.g, to.g), Channel(from.b, to.b), Channel(from.a, to.a) };
    };
    output[0] = enter > 0.0f ? At(enter) : input[0];
    output[1] = leave < 1.0f ? At(leave) : input[1];
    return true;
}

} // namespace ps2::ui
