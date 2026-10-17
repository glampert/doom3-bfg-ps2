// ================================================================================================
// File: ui_page_geometry.cpp
// Brief: Bound UV and screen clipping before a paged atlas triangle reaches the GS packet builder.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/ui_page_geometry.h"

#include <bit>
#include <cstdint>

namespace ps2::ui
{
namespace
{

static constexpr int kAtlasExtentLimit = 16384;
static constexpr std::int64_t kAtlasPixelLimit = (64 * 1024 * 1024) / 4;
static constexpr int kPageCountLimit = 1024;
static constexpr int kInteriorExtent = 254;
static constexpr int kViewportLimit = 1024;
static constexpr int kClipCapacity = kMaxPageTriangles + 3;
static constexpr float kCoordinateLimit = 32768.0f;
static constexpr float kVirtualWidth = 640.0f;
static constexpr float kVirtualHeight = 480.0f;
static constexpr int kOriginalEdges = 3;
static constexpr int kClipPlanes = 8;
static constexpr float kWeightTolerance = 1.0e-6f;
static constexpr int kOriginalEndpoints[kOriginalEdges][2] = { { 0, 1 }, { 1, 2 }, { 0, 2 } };

struct ClipVertex
{
    float x;
    float y;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
    std::uint32_t constraints;
};

enum class Axis
{
    X,
    Y,
    U,
    V
};

struct Plane
{
    Axis axis;
    float boundary;
    bool lower;
    std::uint32_t equivalent = 0;
};

static bool PortableFloat(float value)
{
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t exponent = bits & 0x7F800000U;
    return (exponent != 0x7F800000U) && ((exponent != 0U) || ((bits & 0x007FFFFFU) == 0U));
}

static bool ValidPosition(float value)
{
    return PortableFloat(value) && (value >= -kCoordinateLimit) && (value <= kCoordinateLimit);
}

static bool ValidUv(float value)
{
    return PortableFloat(value) && (value >= 0.0f) && (value <= 1.0f);
}

static float Clamp(float value, float lower, float upper)
{
    if (value < lower)
    {
        return lower;
    }

    return value > upper ? upper : value;
}

static float Coordinate(const ClipVertex & vertex, Axis axis)
{
    switch (axis)
    {
    case Axis::X:
        return vertex.x;
    case Axis::Y:
        return vertex.y;
    case Axis::U:
        return vertex.u;
    case Axis::V:
        return vertex.v;
    }

    return 0.0f;
}

static void SetCoordinate(ClipVertex & vertex, Axis axis, float value)
{
    switch (axis)
    {
    case Axis::X:
        vertex.x = value;
        break;
    case Axis::Y:
        vertex.y = value;
        break;
    case Axis::U:
        vertex.u = value;
        break;
    case Axis::V:
        vertex.v = value;
        break;
    }
}

static bool Inside(const ClipVertex & vertex, const Plane & plane)
{
    const float value = Coordinate(vertex, plane.axis);
    return plane.lower ? value >= plane.boundary : value <= plane.boundary;
}

static bool Before(const ClipVertex & first, const ClipVertex & second)
{
    if (first.x != second.x)
    {
        return first.x < second.x;
    }

    if (first.y != second.y)
    {
        return first.y < second.y;
    }

    if (first.u != second.u)
    {
        return first.u < second.u;
    }

    return first.v < second.v;
}

static bool BoundedWeight(float value)
{
    return PortableFloat(value) && (value >= -kWeightTolerance) && (value <= 1.0f + kWeightTolerance);
}

static float ClampWeight(float value)
{
    if (value < 0.0f)
    {
        return 0.0f;
    }

    return value > 1.0f ? 1.0f : value;
}

static bool WeightedValue(float first, float second, float third, const float (&weights)[3], float & output)
{
    // Constant UV/position/color components must retain their exact input bits;
    // summing three weighted copies can otherwise drift across a clipping plane.
    if (first == second && second == third)
    {
        output = first;
        return true;
    }

    const float value = first * weights[0] + second * weights[1] + third * weights[2];
    if (!PortableFloat(value) || value < -static_cast<float>(kCoordinateLimit) || value > static_cast<float>(kCoordinateLimit))
    {
        return false;
    }

    output = value;
    return true;
}

static bool WeightedVertex(const ClipVertex (&original)[3], float (&weights)[3], ClipVertex & output)
{
    for (float & weight : weights)
    {
        if (!BoundedWeight(weight))
        {
            return false;
        }

        weight = ClampWeight(weight);
    }

    const float sum = weights[0] + weights[1] + weights[2];
    if (!PortableFloat(sum) || sum < 0.5f || sum > 1.5f)
    {
        return false;
    }

    for (float & weight : weights)
    {
        weight /= sum;
        if (!PortableFloat(weight))
        {
            return false;
        }
    }

    ClipVertex result{};
    if (!WeightedValue(original[0].x, original[1].x, original[2].x, weights, result.x) ||
        !WeightedValue(original[0].y, original[1].y, original[2].y, weights, result.y) ||
        !WeightedValue(original[0].u, original[1].u, original[2].u, weights, result.u) ||
        !WeightedValue(original[0].v, original[1].v, original[2].v, weights, result.v) ||
        !WeightedValue(original[0].r, original[1].r, original[2].r, weights, result.r) ||
        !WeightedValue(original[0].g, original[1].g, original[2].g, weights, result.g) ||
        !WeightedValue(original[0].b, original[1].b, original[2].b, weights, result.b) ||
        !WeightedValue(original[0].a, original[1].a, original[2].a, weights, result.a))
    {
        return false;
    }

    for (int i = 0; i < 3; ++i)
    {
        if (weights[i] == 1.0f)
        {
            result.constraints |= original[i].constraints;
        }
    }

    output = result;
    return true;
}

static bool ConstraintBefore(int first, int second, const Plane (&planes)[kClipPlanes])
{
    if (first < kOriginalEdges || second < kOriginalEdges)
    {
        return first < second;
    }

    const Plane & a = planes[first - kOriginalEdges];
    const Plane & b = planes[second - kOriginalEdges];
    return a.axis == b.axis ? a.boundary < b.boundary : a.axis < b.axis;
}

static bool Reconstruct(const ClipVertex (&original)[3], const Plane (&planes)[kClipPlanes],
                        int firstConstraint, int secondConstraint, ClipVertex & output)
{
    if (ConstraintBefore(secondConstraint, firstConstraint, planes))
    {
        const int temporary = firstConstraint;
        firstConstraint = secondConstraint;
        secondConstraint = temporary;
    }

    const Plane & second = planes[secondConstraint - kOriginalEdges];
    float weights[3]{};
    if (firstConstraint < kOriginalEdges)
    {
        const int from = kOriginalEndpoints[firstConstraint][0];
        const int to = kOriginalEndpoints[firstConstraint][1];
        const float origin = static_cast<float>(Coordinate(original[from], second.axis));
        const float extent = static_cast<float>(Coordinate(original[to], second.axis)) - origin;
        if (!PortableFloat(extent) || extent == 0.0f)
        {
            return false;
        }

        const float fraction = (static_cast<float>(second.boundary) - origin) / extent;
        if (!BoundedWeight(fraction))
        {
            return false;
        }

        weights[from] = 1.0f - fraction;
        weights[to] = fraction;
    }
    else
    {
        const Plane & first = planes[firstConstraint - kOriginalEdges];
        const float a0 = static_cast<float>(Coordinate(original[0], first.axis));
        const float a1 = static_cast<float>(Coordinate(original[1], first.axis)) - a0;
        const float a2 = static_cast<float>(Coordinate(original[2], first.axis)) - a0;
        const float b0 = static_cast<float>(Coordinate(original[0], second.axis));
        const float b1 = static_cast<float>(Coordinate(original[1], second.axis)) - b0;
        const float b2 = static_cast<float>(Coordinate(original[2], second.axis)) - b0;
        const float determinant = a1 * b2 - a2 * b1;
        // Solve from the original triangle, never an already clipped endpoint.
        // Both pages use the same float operations and reject singular or
        // nonportable determinants before division on the EE FPU.
        if (!PortableFloat(determinant) || determinant == 0.0f)
        {
            return false;
        }

        const float a = static_cast<float>(first.boundary) - a0;
        const float b = static_cast<float>(second.boundary) - b0;
        weights[1] = (a * b2 - a2 * b) / determinant;
        weights[2] = (a1 * b - a * b1) / determinant;
        weights[0] = 1.0f - weights[1] - weights[2];
    }

    ClipVertex result{};
    if (!WeightedVertex(original, weights, result))
    {
        return false;
    }

    if (firstConstraint >= kOriginalEdges)
    {
        const Plane & first = planes[firstConstraint - kOriginalEdges];
        SetCoordinate(result, first.axis, first.boundary);
    }

    SetCoordinate(result, second.axis, second.boundary);
    output = result;
    return true;
}

struct ExactProduct
{
    std::uint64_t significand;
    int exponent;
    bool negative;
};

static ExactProduct Product(float first, float second)
{
    const std::uint32_t a = std::bit_cast<std::uint32_t>(first);
    const std::uint32_t b = std::bit_cast<std::uint32_t>(second);
    if ((a & 0x7FFFFFFFU) == 0U || (b & 0x7FFFFFFFU) == 0U)
    {
        return { 0U, 0, false };
    }

    const std::uint64_t ma = (a & 0x007FFFFFU) | 0x00800000U;
    const std::uint64_t mb = (b & 0x007FFFFFU) | 0x00800000U;
    std::uint64_t significand = ma * mb;
    const int zeros = std::countr_zero(significand);
    significand >>= static_cast<unsigned int>(zeros);
    const int exponent = static_cast<int>((a >> 23U) & 255U) + static_cast<int>((b >> 23U) & 255U) - 300 + zeros;
    return { significand, exponent, ((a ^ b) & 0x80000000U) != 0U };
}

static bool ProductsEqual(float a, float b, float c, float d)
{
    const ExactProduct first = Product(a, b);
    const ExactProduct second = Product(c, d);
    return first.significand == second.significand && first.exponent == second.exponent && first.negative == second.negative;
}

static bool Equivalent(const ClipVertex (&original)[3], const Plane & first, const Plane & second)
{
    const float a0 = Coordinate(original[0], first.axis);
    const float a1 = Coordinate(original[1], first.axis) - a0;
    const float a2 = Coordinate(original[2], first.axis) - a0;
    const float b0 = Coordinate(original[0], second.axis);
    const float b1 = Coordinate(original[1], second.axis) - b0;
    const float b2 = Coordinate(original[2], second.axis) - b0;
    const float a = first.boundary - a0;
    const float b = second.boundary - b0;
    if (!PortableFloat(a1) || !PortableFloat(a2) || !PortableFloat(b1) || !PortableFloat(b2) ||
        !PortableFloat(a) || !PortableFloat(b) || (a1 == 0.0f && a2 == 0.0f) || (b1 == 0.0f && b2 == 0.0f))
    {
        return false;
    }

    // The inexpensive float comparison rejects ordinary independent planes.
    // Exact 48-bit significand products distinguish genuine rank-one equations
    // from a merely small determinant; no epsilon turns a thin strip into a line.
    if (a1 * b2 != a2 * b1 || a * b1 != b * a1 || a * b2 != b * a2)
    {
        return false;
    }

    return ProductsEqual(a1, b2, a2, b1) && ProductsEqual(a, b1, b, a1) && ProductsEqual(a, b2, b, a2);
}

static void InitializeSupports(const ClipVertex (&original)[3], Plane (&planes)[kClipPlanes])
{
    for (int i = 0; i < kClipPlanes; ++i)
    {
        Plane & plane = planes[i];
        for (int edge = 0; edge < kOriginalEdges; ++edge)
        {
            if (Coordinate(original[kOriginalEndpoints[edge][0]], plane.axis) == plane.boundary &&
                Coordinate(original[kOriginalEndpoints[edge][1]], plane.axis) == plane.boundary)
            {
                plane.equivalent |= 1U << static_cast<unsigned int>(edge);
            }
        }

        for (int previous = 0; previous < i; ++previous)
        {
            if (Equivalent(original, planes[previous], plane))
            {
                plane.equivalent |= 1U << static_cast<unsigned int>(previous + kOriginalEdges);
            }
        }
    }
}

static void MarkPlane(ClipVertex & vertex, const Plane (&planes)[kClipPlanes], int planeIndex)
{
    const Plane & plane = planes[planeIndex];
    if ((vertex.constraints & plane.equivalent) != 0U || Coordinate(vertex, plane.axis) == plane.boundary)
    {
        SetCoordinate(vertex, plane.axis, plane.boundary);
        vertex.constraints |= 1U << static_cast<unsigned int>(planeIndex + kOriginalEdges);
    }
}

static bool Intersection(const ClipVertex & first, const ClipVertex & second, const ClipVertex (&original)[3],
                         const Plane (&planes)[kClipPlanes], int planeIndex, ClipVertex & output)
{
    const std::uint32_t shared = first.constraints & second.constraints;
    int ordered[kOriginalEdges + kClipPlanes]{};
    int count = 0;
    for (int constraint = 0; constraint < kOriginalEdges + planeIndex; ++constraint)
    {
        if ((shared & (1U << static_cast<unsigned int>(constraint))) != 0U)
        {
            int index = count;
            while (index > 0 && ConstraintBefore(constraint, ordered[index - 1], planes))
            {
                ordered[index] = ordered[index - 1];
                --index;
            }

            ordered[index] = constraint;
            ++count;
        }
    }

    for (int i = 0; i < count; ++i)
    {
        ClipVertex result{};
        if (Reconstruct(original, planes, ordered[i], planeIndex + kOriginalEdges, result))
        {
            result.constraints |= shared | (1U << static_cast<unsigned int>(planeIndex + kOriginalEdges));
            output = result;
            return true;
        }
    }

    return false;
}

static bool SamePosition(const ClipVertex & first, const ClipVertex & second)
{
    return (first.x == second.x) && (first.y == second.y) && (first.u == second.u) && (first.v == second.v);
}

static bool Append(ClipVertex (&output)[kClipCapacity], int & count, const ClipVertex & vertex)
{
    if ((count > 0) && SamePosition(output[count - 1], vertex))
    {
        output[count - 1].constraints |= vertex.constraints;
        return true;
    }

    if (count == kClipCapacity)
    {
        return false;
    }

    output[count++] = vertex;
    return true;
}

static bool Clip(const ClipVertex (&input)[kClipCapacity], int count, const ClipVertex (&original)[3],
                 const Plane (&planes)[kClipPlanes], int planeIndex, ClipVertex (&output)[kClipCapacity], int & outputCount)
{
    const Plane & plane = planes[planeIndex];
    outputCount = 0;
    ClipVertex previous = input[count - 1];
    MarkPlane(previous, planes, planeIndex);
    bool previousInside = Inside(previous, plane);
    for (int i = 0; i < count; ++i)
    {
        ClipVertex current = input[i];
        MarkPlane(current, planes, planeIndex);
        const bool currentInside = Inside(current, plane);
        if (previousInside != currentInside)
        {
            ClipVertex intersection{};
            if (!Intersection(previous, current, original, planes, planeIndex, intersection) || !Append(output, outputCount, intersection))
            {
                return false;
            }
        }

        if (currentInside && !Append(output, outputCount, current))
        {
            return false;
        }

        previous = current;
        previousInside = currentInside;
    }

    if ((outputCount > 1) && SamePosition(output[0], output[outputCount - 1]))
    {
        output[0].constraints |= output[outputCount - 1].constraints;
        --outputCount;
    }

    return true;
}

static bool Degenerate(const Vertex (&vertices)[3])
{
    const float dx1 = vertices[1].x - vertices[0].x;
    const float dy1 = vertices[1].y - vertices[0].y;
    const float dx2 = vertices[2].x - vertices[0].x;
    const float dy2 = vertices[2].y - vertices[0].y;
    return ((dx1 * dy2) - (dy1 * dx2)) == 0.0f;
}

static bool Degenerate(const FixedTriangle & triangle)
{
    const FixedVertex(&vertices)[3] = triangle.vertices;
    const std::int64_t dx1 = vertices[1].x - vertices[0].x;
    const std::int64_t dy1 = vertices[1].y - vertices[0].y;
    const std::int64_t dx2 = vertices[2].x - vertices[0].x;
    const std::int64_t dy2 = vertices[2].y - vertices[0].y;
    return ((dx1 * dy2) - (dy1 * dx2)) == 0;
}

static int RoundFixed(float value)
{
    return static_cast<int>((value * 16.0f) + 0.5f);
}

static std::uint8_t RoundColor(float value)
{
    return static_cast<std::uint8_t>(Clamp(value, 0.0f, 255.0f) + 0.5f);
}

static FixedVertex MapVertex(const ClipVertex & vertex, Viewport viewport, float originU, float originV,
                             float endU, float endV)
{
    const float scaleX = static_cast<float>(viewport.width) / kVirtualWidth;
    const float scaleY = static_cast<float>(viewport.height) / kVirtualHeight;
    return {
        RoundFixed(Clamp(vertex.x, 0.0f, kVirtualWidth) * scaleX),
        RoundFixed(Clamp(vertex.y, 0.0f, kVirtualHeight) * scaleY),
        RoundFixed(Clamp(vertex.u, originU, endU) - originU + 1.0f),
        RoundFixed(Clamp(vertex.v, originV, endV) - originV + 1.0f),
        RoundColor(vertex.r), RoundColor(vertex.g), RoundColor(vertex.b), RoundColor(vertex.a)
    };
}

} // namespace

Result MapPageTriangle(const Vertex (&input)[3], Viewport viewport, int atlasWidth, int atlasHeight,
                       int column, int row, FixedTriangle (&output)[kMaxPageTriangles], int & triangleCount)
{
    if ((viewport.width < 1) || (viewport.height < 1) || (viewport.width > kViewportLimit) || (viewport.height > kViewportLimit) ||
        (atlasWidth < 1) || (atlasHeight < 1) || (atlasWidth > kAtlasExtentLimit) || (atlasHeight > kAtlasExtentLimit) ||
        (static_cast<std::int64_t>(atlasWidth) * atlasHeight > kAtlasPixelLimit))
    {
        return Result::Invalid;
    }

    const int columns = (atlasWidth + kInteriorExtent - 1) / kInteriorExtent;
    const int rows = (atlasHeight + kInteriorExtent - 1) / kInteriorExtent;
    if ((columns * rows > kPageCountLimit) || (column < 0) || (row < 0) || (column >= columns) || (row >= rows))
    {
        return Result::Invalid;
    }

    // Complete input admission precedes arithmetic even when an earlier vertex
    // would make the triangle empty. EE cannot inspect IEEE special values safely.
    for (const Vertex & vertex : input)
    {
        if (!ValidPosition(vertex.x) || !ValidPosition(vertex.y) || !ValidUv(vertex.s) || !ValidUv(vertex.t))
        {
            return Result::Invalid;
        }
    }

    const int originX = column * kInteriorExtent;
    const int originY = row * kInteriorExtent;
    const float originU = static_cast<float>(originX);
    const float originV = static_cast<float>(originY);
    const float endU = static_cast<float>((atlasWidth - originX < kInteriorExtent) ? atlasWidth : originX + kInteriorExtent);
    const float endV = static_cast<float>((atlasHeight - originY < kInteriorExtent) ? atlasHeight : originY + kInteriorExtent);
    ClipVertex scratch[2][kClipCapacity]{};
    for (int i = 0; i < 3; ++i)
    {
        const Vertex & vertex = input[i];
        scratch[0][i] = {
            vertex.x, vertex.y, vertex.s * static_cast<float>(atlasWidth), vertex.t * static_cast<float>(atlasHeight),
            static_cast<float>(vertex.r), static_cast<float>(vertex.g), static_cast<float>(vertex.b), static_cast<float>(vertex.a), 0U
        };
    }

    // A constant UV exactly on a shared page edge has nonzero screen area.
    // Give that boundary to just one page to avoid float alpha blending.
    const bool upperU = (column + 1 < columns) && (scratch[0][0].u == endU) && (scratch[0][1].u == endU) && (scratch[0][2].u == endU);
    const bool upperV = (row + 1 < rows) && (scratch[0][0].v == endV) && (scratch[0][1].v == endV) && (scratch[0][2].v == endV);
    if (upperU || upperV || Degenerate(input))
    {
        triangleCount = 0;
        return Result::Clipped;
    }

    ClipVertex original[3] = { scratch[0][0], scratch[0][1], scratch[0][2] };
    for (int i = 1; i < 3; ++i)
    {
        const ClipVertex value = original[i];
        int position = i;
        while (position > 0 && Before(value, original[position - 1]))
        {
            original[position] = original[position - 1];
            --position;
        }

        original[position] = value;
    }

    static constexpr std::uint32_t kVertexConstraints[] = { 5U, 3U, 6U };
    for (int i = 0; i < 3; ++i)
    {
        original[i].constraints = kVertexConstraints[i];
        for (ClipVertex & vertex : scratch[0])
        {
            if (SamePosition(vertex, original[i]))
            {
                vertex.constraints = original[i].constraints;
            }
        }
    }

    Plane planes[] = {
        { Axis::U, originU, true }, { Axis::U, endU, false }, { Axis::V, originV, true }, { Axis::V, endV, false }, { Axis::X, 0.0f, true }, { Axis::X, kVirtualWidth, false }, { Axis::Y, 0.0f, true }, { Axis::Y, kVirtualHeight, false }
    };
    InitializeSupports(original, planes);
    int count = 3;
    int active = 0;
    for (int planeIndex = 0; planeIndex < kClipPlanes; ++planeIndex)
    {
        const int next = 1 - active;
        int nextCount = 0;
        if (!Clip(scratch[active], count, original, planes, planeIndex, scratch[next], nextCount))
        {
            return Result::Invalid;
        }

        if (nextCount < 3)
        {
            triangleCount = 0;
            return Result::Clipped;
        }

        active = next;
        count = nextCount;
    }

    if (count > kMaxPageTriangles + 2)
    {
        return Result::Invalid;
    }

    FixedVertex mapped[kClipCapacity]{};
    for (int i = 0; i < count; ++i)
    {
        mapped[i] = MapVertex(scratch[active][i], viewport, originU, originV, endU, endV);
    }

    FixedTriangle triangles[kMaxPageTriangles]{};
    int mappedCount = 0;
    for (int i = 1; i < count - 1; ++i)
    {
        const FixedTriangle triangle{ { mapped[0], mapped[i], mapped[i + 1] } };
        if (!Degenerate(triangle))
        {
            triangles[mappedCount++] = triangle;
        }
    }

    for (int i = 0; i < mappedCount; ++i)
    {
        output[i] = triangles[i];
    }

    triangleCount = mappedCount;
    return mappedCount > 0 ? Result::Draw : Result::Clipped;
}

} // namespace ps2::ui
