// ================================================================================================
// File: triangles.cpp
// Brief: Bound CPU triangle clipping and interpolation independently of target GS packet emission.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/triangles.h"

#include <bit>
#include <cstdint>

namespace ps2::ui
{
namespace
{

static constexpr float kVirtualWidth = 640.0f;
static constexpr float kVirtualHeight = 480.0f;
static constexpr float kCoordinateLimit = 32768.0f;
static constexpr float kTexelLimit = 1023.0f;
static constexpr int kViewportLimit = 1024;
static constexpr int kTextureLimit = 512;
static constexpr int kClipCapacity = 8;

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
};

enum class Edge
{
    Left,
    Right,
    Top,
    Bottom
};

static bool PortableFloat(float value)
{
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t exponent = bits & 0x7F800000U;
    // EE has neither IEEE Inf/NaN nor denormal support: reject their encodings before arithmetic.
    return (exponent != 0x7F800000U) &&
           ((exponent != 0U) || ((bits & 0x007FFFFFU) == 0U));
}

static bool ValidCoordinate(float value)
{
    return PortableFloat(value) && (value >= -kCoordinateLimit) && (value <= kCoordinateLimit);
}

static bool ValidUv(float value, int extent)
{
    return PortableFloat(value) && (value >= 0.0f) &&
           (value <= kTexelLimit / static_cast<float>(extent));
}

static float Clamp(float value, float lower, float upper)
{
    if (value < lower)
    {
        return lower;
    }
    return (value > upper) ? upper : value;
}

static bool Inside(const ClipVertex & vertex, Edge edge)
{
    switch (edge)
    {
    case Edge::Left:
        return vertex.x >= 0.0f;
    case Edge::Right:
        return vertex.x <= kVirtualWidth;
    case Edge::Top:
        return vertex.y >= 0.0f;
    case Edge::Bottom:
        return vertex.y <= kVirtualHeight;
    }
    return false;
}

static float Interpolate(float first, float second, float fraction)
{
    return first + ((second - first) * fraction);
}

static ClipVertex Intersection(const ClipVertex & first, const ClipVertex & second, Edge edge)
{
    // Shared indexed edges must round identically when adjacent triangles traverse them in reverse.
    const bool reverse = (first.x > second.x) || ((first.x == second.x) && (first.y > second.y));
    const ClipVertex & from = reverse ? second : first;
    const ClipVertex & to = reverse ? first : second;
    const bool horizontal = (edge == Edge::Top) || (edge == Edge::Bottom);
    const float boundary = (edge == Edge::Right) ? kVirtualWidth : ((edge == Edge::Bottom) ? kVirtualHeight : 0.0f);
    const float origin = horizontal ? from.y : from.x;
    const float end = horizontal ? to.y : to.x;
    const float fraction = Clamp((boundary - origin) / (end - origin), 0.0f, 1.0f);
    ClipVertex clipped{
        Interpolate(from.x, to.x, fraction), Interpolate(from.y, to.y, fraction),
        Interpolate(from.s, to.s, fraction), Interpolate(from.t, to.t, fraction),
        Interpolate(from.r, to.r, fraction), Interpolate(from.g, to.g, fraction),
        Interpolate(from.b, to.b, fraction), Interpolate(from.a, to.a, fraction)
    };
    if (horizontal)
    {
        clipped.y = boundary;
    }
    else
    {
        clipped.x = boundary;
    }
    return clipped;
}

static bool SamePosition(const ClipVertex & first, const ClipVertex & second)
{
    return (first.x == second.x) && (first.y == second.y);
}

static bool Append(ClipVertex (&output)[kClipCapacity], int & count, const ClipVertex & vertex)
{
    if ((count > 0) && SamePosition(output[count - 1], vertex))
    {
        return true;
    }
    if (count == kClipCapacity)
    {
        return false;
    }
    output[count++] = vertex;
    return true;
}

static bool Clip(const ClipVertex (&input)[kClipCapacity], int inputCount, Edge edge,
                 ClipVertex (&output)[kClipCapacity], int & outputCount)
{
    outputCount = 0;
    ClipVertex previous = input[inputCount - 1];
    bool previousInside = Inside(previous, edge);
    for (int i = 0; i < inputCount; ++i)
    {
        const ClipVertex & current = input[i];
        const bool currentInside = Inside(current, edge);
        if ((previousInside != currentInside) &&
            !Append(output, outputCount, Intersection(previous, current, edge)))
        {
            return false;
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
        --outputCount;
    }
    return true;
}

static int RoundFixed(float value)
{
    return static_cast<int>((value * 16.0f) + 0.5f);
}

static std::uint8_t RoundColor(float value)
{
    return static_cast<std::uint8_t>(Clamp(value, 0.0f, 255.0f) + 0.5f);
}

static FixedVertex MapVertex(const ClipVertex & vertex, Viewport viewport,
                             int textureWidth, int textureHeight)
{
    const float scaleX = static_cast<float>(viewport.width) / kVirtualWidth;
    const float scaleY = static_cast<float>(viewport.height) / kVirtualHeight;
    return {
        RoundFixed(Clamp(vertex.x, 0.0f, kVirtualWidth) * scaleX),
        RoundFixed(Clamp(vertex.y, 0.0f, kVirtualHeight) * scaleY),
        RoundFixed(Clamp(vertex.s * static_cast<float>(textureWidth), 0.0f, kTexelLimit)),
        RoundFixed(Clamp(vertex.t * static_cast<float>(textureHeight), 0.0f, kTexelLimit)),
        RoundColor(vertex.r), RoundColor(vertex.g), RoundColor(vertex.b), RoundColor(vertex.a)
    };
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
    const FixedVertex(&v)[3] = triangle.vertices;
    const std::int64_t dx1 = v[1].x - v[0].x;
    const std::int64_t dy1 = v[1].y - v[0].y;
    const std::int64_t dx2 = v[2].x - v[0].x;
    const std::int64_t dy2 = v[2].y - v[0].y;
    return ((dx1 * dy2) - (dy1 * dx2)) == 0;
}

} // namespace

Result MapTriangle(const Vertex (&input)[3], Viewport viewport, int textureWidth, int textureHeight,
                   FixedTriangle (&output)[kMaxClippedTriangles], int & triangleCount)
{
    if ((viewport.width < 1) || (viewport.height < 1) ||
        (viewport.width > kViewportLimit) || (viewport.height > kViewportLimit) ||
        (textureWidth < 1) || (textureHeight < 1) ||
        (textureWidth > kTextureLimit) || (textureHeight > kTextureLimit))
    {
        return Result::Invalid;
    }
    ClipVertex scratch[2][kClipCapacity]{};
    for (int i = 0; i < 3; ++i)
    {
        const Vertex & v = input[i];
        if (!ValidCoordinate(v.x) || !ValidCoordinate(v.y) ||
            !ValidUv(v.s, textureWidth) || !ValidUv(v.t, textureHeight))
        {
            return Result::Invalid;
        }
        scratch[0][i] = {
            v.x, v.y, v.s, v.t, static_cast<float>(v.r), static_cast<float>(v.g),
            static_cast<float>(v.b), static_cast<float>(v.a)
        };
    }
    if (Degenerate(input))
    {
        triangleCount = 0;
        return Result::Clipped;
    }

    static constexpr Edge kEdges[] = { Edge::Left, Edge::Right, Edge::Top, Edge::Bottom };
    int count = 3;
    int active = 0;
    for (Edge edge : kEdges)
    {
        const int next = 1 - active;
        int clippedCount = 0;
        if (!Clip(scratch[active], count, edge, scratch[next], clippedCount))
        {
            return Result::Invalid;
        }
        if (clippedCount < 3)
        {
            triangleCount = 0;
            return Result::Clipped;
        }
        active = next;
        count = clippedCount;
    }
    if (count > kMaxClippedTriangles + 2)
    {
        return Result::Invalid;
    }

    FixedVertex mapped[kClipCapacity]{};
    for (int i = 0; i < count; ++i)
    {
        mapped[i] = MapVertex(scratch[active][i], viewport, textureWidth, textureHeight);
    }
    FixedTriangle triangles[kMaxClippedTriangles]{};
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
    return (mappedCount > 0) ? Result::Draw : Result::Clipped;
}

Result MapLine(const Vertex (&input)[2], Viewport viewport, FixedLine & output)
{
    if ((viewport.width < 1) || (viewport.height < 1) ||
        (viewport.width > kViewportLimit) || (viewport.height > kViewportLimit))
    {
        return Result::Invalid;
    }

    ClipVertex ends[2]{};

    for (int i = 0; i < 2; ++i)
    {
        const Vertex & v = input[i];

        if (!ValidCoordinate(v.x) || !ValidCoordinate(v.y))
        {
            return Result::Invalid;
        }

        ends[i] = { v.x, v.y, 0.0f, 0.0f, static_cast<float>(v.r), static_cast<float>(v.g),
                    static_cast<float>(v.b), static_cast<float>(v.a) };
    }

    // Liang-Barsky: keep the parameter interval of the segment inside every viewport edge.
    const float dx = ends[1].x - ends[0].x;
    const float dy = ends[1].y - ends[0].y;
    const float deltas[] = { -dx, dx, -dy, dy };
    const float distances[] = { ends[0].x, kVirtualWidth - ends[0].x, ends[0].y, kVirtualHeight - ends[0].y };
    float enter = 0.0f;
    float leave = 1.0f;

    for (int edge = 0; edge < 4; ++edge)
    {
        if (deltas[edge] == 0.0f)
        {
            if (distances[edge] < 0.0f)
            {
                return Result::Clipped;
            }

            continue;
        }

        const float fraction = distances[edge] / deltas[edge];

        if (deltas[edge] < 0.0f)
        {
            enter = fraction > enter ? fraction : enter;
        }
        else
        {
            leave = fraction < leave ? fraction : leave;
        }
    }

    if (enter > leave)
    {
        return Result::Clipped;
    }

    const auto At = [&ends](float fraction)
    {
        const ClipVertex & from = ends[0];
        const ClipVertex & to = ends[1];
        return ClipVertex{ Interpolate(from.x, to.x, fraction), Interpolate(from.y, to.y, fraction), 0.0f, 0.0f,
                           Interpolate(from.r, to.r, fraction), Interpolate(from.g, to.g, fraction),
                           Interpolate(from.b, to.b, fraction), Interpolate(from.a, to.a, fraction) };
    };
    // Unclipped endpoints keep their exact input values.
    const ClipVertex first = enter > 0.0f ? At(enter) : ends[0];
    const ClipVertex last = leave < 1.0f ? At(leave) : ends[1];
    const FixedLine line{ { MapVertex(first, viewport, 1, 1), MapVertex(last, viewport, 1, 1) } };

    if ((line.vertices[0].x == line.vertices[1].x) && (line.vertices[0].y == line.vertices[1].y))
    {
        return Result::Clipped;
    }

    output = line;
    return Result::Draw;
}

} // namespace ps2::ui
