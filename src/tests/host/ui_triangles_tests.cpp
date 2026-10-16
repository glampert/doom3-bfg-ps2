// ================================================================================================
// File: ui_triangles_tests.cpp
// Brief: Verify clipped UI triangle interpolation, winding, fixed seams and hostile float handling.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/ui_triangles.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{

using ps2::ui::FixedTriangle;
using ps2::ui::FixedVertex;
using ps2::ui::kMaxClippedTriangles;
using ps2::ui::MapTriangle;
using ps2::ui::Result;
using ps2::ui::Vertex;
using ps2::ui::Viewport;

static constexpr Viewport kNtsc{ 512, 448 };
static constexpr Viewport kPal{ 512, 512 };
static constexpr Viewport kIdentity{ 640, 480 };
static constexpr Vertex kFull[3] = {
    { 0.0f, 0.0f, 1.0f, 0.0f, 255, 0, 17, 255 },
    { 640.0f, 0.0f, 0.0f, 1.0f, 0, 255, 31, 128 },
    { 0.0f, 480.0f, 0.5f, 0.25f, 0, 0, 255, 0 }
};
static constexpr FixedVertex kSentinel{ -1, -2, -3, -4, 11, 22, 33, 44 };

static bool Check(bool passed, const char * name)
{
    if (!passed)
    {
        std::fprintf(stderr, "UI triangles failed: %s\n", name);
    }
    return passed;
}

static bool Equal(const FixedVertex & first, const FixedVertex & second)
{
    return (first.x == second.x) && (first.y == second.y) &&
           (first.u == second.u) && (first.v == second.v) &&
           (first.r == second.r) && (first.g == second.g) &&
           (first.b == second.b) && (first.a == second.a);
}

static void FillSentinels(FixedTriangle (&triangles)[kMaxClippedTriangles])
{
    for (FixedTriangle & triangle : triangles)
    {
        for (FixedVertex & vertex : triangle.vertices)
        {
            vertex = kSentinel;
        }
    }
}

static bool Untouched(const FixedTriangle (&triangles)[kMaxClippedTriangles], int begin = 0)
{
    for (int i = begin; i < kMaxClippedTriangles; ++i)
    {
        for (const FixedVertex & vertex : triangles[i].vertices)
        {
            if (!Equal(vertex, kSentinel))
            {
                return false;
            }
        }
    }
    return true;
}

static bool Unchanged(const Vertex (&input)[3], Viewport viewport, int width, int height,
                      Result expected)
{
    FixedTriangle output[kMaxClippedTriangles];
    FillSentinels(output);
    int count = -99;
    const int expectedCount = (expected == Result::Invalid) ? -99 : 0;
    return (MapTriangle(input, viewport, width, height, output, count) == expected) &&
           (count == expectedCount) && Untouched(output);
}

static std::int64_t Area(const FixedTriangle & triangle)
{
    const FixedVertex(&v)[3] = triangle.vertices;
    const std::int64_t dx1 = v[1].x - v[0].x;
    const std::int64_t dy1 = v[1].y - v[0].y;
    const std::int64_t dx2 = v[2].x - v[0].x;
    const std::int64_t dy2 = v[2].y - v[0].y;
    return (dx1 * dy2) - (dy1 * dx2);
}

static bool Contains(const FixedTriangle (&triangles)[kMaxClippedTriangles], int count,
                     const FixedVertex & expected)
{
    for (int i = 0; i < count; ++i)
    {
        for (const FixedVertex & vertex : triangles[i].vertices)
        {
            if (Equal(vertex, expected))
            {
                return true;
            }
        }
    }
    return false;
}

static bool Mapping()
{
    FixedTriangle output[kMaxClippedTriangles];
    FillSentinels(output);
    int count = 0;
    if (!Check((MapTriangle(kFull, kNtsc, 64, 32, output, count) == Result::Draw) &&
               (count == 1) &&
               Equal(output[0].vertices[0], { 0, 0, 1024, 0, 255, 0, 17, 255 }) &&
               Equal(output[0].vertices[1], { 8192, 0, 0, 512, 0, 255, 31, 128 }) &&
               Equal(output[0].vertices[2], { 0, 7168, 512, 128, 0, 0, 255, 0 }) &&
               Untouched(output, 1),
               "NTSC reversed UV and byte colors") ||
        !Check((MapTriangle(kFull, kPal, 3, 5, output, count) == Result::Draw) &&
               (count == 1) && (output[0].vertices[1].u == 0) &&
               (output[0].vertices[1].v == 80) &&
               (output[0].vertices[2].y == 8192) &&
               (output[0].vertices[2].u == 24) &&
               (output[0].vertices[2].v == 20),
               "PAL and logical NPOT texture"))
    {
        return false;
    }

    Vertex reversed[3] = { kFull[0], kFull[2], kFull[1] };
    if (!Check((MapTriangle(reversed, kNtsc, 64, 32, output, count) == Result::Draw) &&
               (count == 1) && (Area(output[0]) < 0) &&
               Equal(output[0].vertices[1], { 0, 7168, 512, 128, 0, 0, 255, 0 }),
               "clockwise input winding"))
    {
        return false;
    }

    const Vertex subpixels[3] = {
        { 0.5f, 0.5f, 0.25f, 0.125f, 255, 255, 255, 255 },
        { 1.75f, 0.5f, 0.75f, 0.125f, 255, 255, 255, 255 },
        { 0.5f, 1.75f, 0.25f, 0.875f, 255, 255, 255, 255 }
    };
    if (!Check((MapTriangle(subpixels, kNtsc, 64, 32, output, count) == Result::Draw) &&
               (count == 1) &&
               Equal(output[0].vertices[0], { 6, 7, 256, 64, 255, 255, 255, 255 }) &&
               Equal(output[0].vertices[1], { 22, 7, 768, 64, 255, 255, 255, 255 }) &&
               Equal(output[0].vertices[2], { 6, 26, 256, 448, 255, 255, 255, 255 }),
               "fractional fixed positions"))
    {
        return false;
    }
    Vertex repeat[3] = { kFull[0], kFull[1], kFull[2] };
    repeat[0].s = 1023.0f;
    repeat[1].t = 1023.0f;
    return Check((MapTriangle(repeat, kIdentity, 1, 1, output, count) == Result::Draw) &&
                 (output[0].vertices[0].u == 16368) &&
                 (output[0].vertices[1].v == 16368),
                 "largest safe UV field") &&
           Check(Unchanged(repeat, kIdentity, 2, 1, Result::Invalid), "UV fields never wrap");
}

static bool Clipping()
{
    const Vertex left[3] = {
        { -100.0f, 100.0f, 0.0f, 0.0f, 0, 20, 100, 0 },
        { 100.0f, 100.0f, 1.0f, 0.0f, 200, 40, 200, 200 },
        { 100.0f, 300.0f, 1.0f, 1.0f, 100, 60, 0, 100 }
    };
    FixedTriangle output[kMaxClippedTriangles];
    FillSentinels(output);
    int count = 0;
    if (!Check((MapTriangle(left, kIdentity, 100, 100, output, count) == Result::Draw) &&
               (count == 2) &&
               Equal(output[0].vertices[0], { 0, 3200, 800, 800, 50, 40, 50, 50 }) &&
               Equal(output[0].vertices[1], { 0, 1600, 800, 0, 100, 30, 150, 100 }) &&
               Equal(output[0].vertices[2], { 1600, 1600, 1600, 0, 200, 40, 200, 200 }) &&
               Equal(output[1].vertices[2], { 1600, 4800, 1600, 1600, 100, 60, 0, 100 }) &&
               (Area(output[0]) + Area(output[1]) == 7680000) && Untouched(output, 2),
               "left clip interpolates UV, RGBA and covered area"))
    {
        return false;
    }

    const Vertex enclosing[3] = {
        { -1000.0f, -1000.0f, 0.0f, 0.0f, 255, 255, 255, 255 },
        { 5000.0f, -1000.0f, 1.0f, 0.0f, 255, 255, 255, 255 },
        { -1000.0f, 5000.0f, 0.0f, 1.0f, 255, 255, 255, 255 }
    };
    if (!Check((MapTriangle(enclosing, kIdentity, 1, 1, output, count) == Result::Draw) &&
               (count == 2) &&
               (Area(output[0]) + Area(output[1]) == 157286400),
               "four-plane clipping preserves full viewport area"))
    {
        return false;
    }

    const Vertex crossing[3] = {
        { -100.0f, 100.0f, 0.0f, 0.0f, 0, 13, 255, 70 },
        { 740.0f, 100.0f, 1.0f, 0.0f, 255, 90, 0, 200 },
        { 320.0f, 600.0f, 0.5f, 1.0f, 127, 255, 100, 255 }
    };
    if (!Check((MapTriangle(crossing, kIdentity, 64, 32, output, count) == Result::Draw) &&
               (count >= 3),
               "multiple clipped fan triangles"))
    {
        return false;
    }

    for (int start = 0; start < 3; ++start)
    {
        const Vertex permuted[3] = {
            crossing[start], crossing[(start + 2) % 3], crossing[(start + 1) % 3]
        };
        FixedTriangle backward[kMaxClippedTriangles]{};
        int backwardCount = 0;
        if (!Check((MapTriangle(permuted, kIdentity, 64, 32, backward, backwardCount) == Result::Draw) &&
                   (backwardCount == count),
                   "index ordering preserves clipped vertex count"))
        {
            return false;
        }
        std::int64_t forwardArea = 0;
        std::int64_t backwardArea = 0;
        for (int i = 0; i < count; ++i)
        {
            forwardArea += Area(output[i]);
            backwardArea += Area(backward[i]);
            for (const FixedVertex & vertex : output[i].vertices)
            {
                if (!Check(Contains(backward, backwardCount, vertex),
                           "reversed indexed edges share exact fixed position, UV and colors"))
                {
                    return false;
                }
            }
        }
        if (!Check(forwardArea == -backwardArea, "clipped winding reverses covered area"))
        {
            return false;
        }
    }

    const Vertex roundedColor[3] = {
        { -1.0f, 100.0f, 0.0f, 0.0f, 0, 0, 255, 0 },
        { 1.0f, 100.0f, 1.0f, 0.0f, 1, 255, 0, 255 },
        { 1.0f, 102.0f, 1.0f, 1.0f, 1, 255, 0, 255 }
    };
    return Check((MapTriangle(roundedColor, kIdentity, 1, 1, output, count) == Result::Draw) &&
                 Contains(output, count, { 0, 1600, 8, 0, 1, 128, 128, 128 }),
                 "clip color interpolation rounds nearest instead of truncating");
}

static bool Rejections()
{
    const Vertex clipped[][3] = {
        { { -10.0f, 0.0f, 0, 0, 0, 0, 0, 0 },
          { 0.0f, 10.0f, 0, 0, 0, 0, 0, 0 },
          { 0.0f, 20.0f, 0, 0, 0, 0, 0, 0 } },
        { { 640.0f, 100.0f, 0, 0, 0, 0, 0, 0 },
          { 740.0f, 100.0f, 0, 0, 0, 0, 0, 0 },
          { 640.0f, 200.0f, 0, 0, 0, 0, 0, 0 } },
        { { 0.0f, -10.0f, 0, 0, 0, 0, 0, 0 },
          { 10.0f, 0.0f, 0, 0, 0, 0, 0, 0 },
          { 20.0f, 0.0f, 0, 0, 0, 0, 0, 0 } },
        { { 100.0f, 480.0f, 0, 0, 0, 0, 0, 0 },
          { 100.0f, 580.0f, 0, 0, 0, 0, 0, 0 },
          { 200.0f, 480.0f, 0, 0, 0, 0, 0, 0 } },
        { { 0.0f, 0.0f, 0, 0, 0, 0, 0, 0 },
          { 0.0f, 0.0f, 0, 0, 0, 0, 0, 0 },
          { 20.0f, 20.0f, 0, 0, 0, 0, 0, 0 } },
        { { 0.0f, 0.0f, 0, 0, 0, 0, 0, 0 },
          { 0.001f, 0.0f, 0, 0, 0, 0, 0, 0 },
          { 0.0f, 0.001f, 0, 0, 0, 0, 0, 0 } },
        { { 0.03125f, 0.015625f, 0, 0, 0, 0, 0, 0 },
          { 0.09375f, 0.046875f, 0, 0, 0, 0, 0, 0 },
          { 0.15625f, 0.078125f, 0, 0, 0, 0, 0, 0 } }
    };
    for (const Vertex(&triangle)[3] : clipped)
    {
        if (!Check(Unchanged(triangle, kIdentity, 64, 32, Result::Clipped),
                   "empty/float-degenerate/fixed-degenerate output remains untouched"))
        {
            return false;
        }
    }
    const Viewport invalidViewports[] = { { 0, 448 }, { 512, -1 }, { 1025, 512 }, { 512, 1025 } };
    for (Viewport viewport : invalidViewports)
    {
        if (!Check(Unchanged(kFull, viewport, 64, 32, Result::Invalid), "invalid viewport"))
        {
            return false;
        }
    }
    return Check(Unchanged(kFull, kNtsc, 0, 32, Result::Invalid) &&
                 Unchanged(kFull, kNtsc, 64, -1, Result::Invalid) &&
                 Unchanged(kFull, kNtsc, 513, 32, Result::Invalid) &&
                 Unchanged(kFull, kNtsc, 64, 513, Result::Invalid),
                 "invalid texture dimensions");
}

static bool SharedEdges()
{
    // Two separately indexed triangles traverse the same diagonal in opposite directions. Both
    // endpoints are outside different clipping planes, so the shared visible edge is generated.
    const Vertex first[3] = {
        { -17.03125f, 77.3125f, 0.03125f, 0.0625f, 3, 252, 17, 7 },
        { 500.0f, 10.0f, 0.75f, 0.125f, 200, 27, 91, 180 },
        { 630.90625f, 499.53125f, 0.96875f, 0.9375f, 251, 7, 129, 249 }
    };
    const Vertex second[3] = {
        first[2], first[0], { 10.0f, 450.0f, 0.125f, 0.75f, 33, 89, 211, 100 }
    };
    FixedTriangle forward[kMaxClippedTriangles]{};
    FixedTriangle backward[kMaxClippedTriangles]{};
    int forwardCount = 0;
    int backwardCount = 0;
    if (!Check((MapTriangle(first, kNtsc, 123, 59, forward, forwardCount) == Result::Draw) &&
               (MapTriangle(second, kNtsc, 123, 59, backward, backwardCount) == Result::Draw),
               "adjacent indexed triangles clip independently"))
    {
        return false;
    }
    FixedVertex common[2]{};
    int sharedCount = 0;
    for (int i = 0; i < forwardCount; ++i)
    {
        for (const FixedVertex & vertex : forward[i].vertices)
        {
            if (Contains(backward, backwardCount, vertex) &&
                ((sharedCount == 0) || !Equal(common[0], vertex)) &&
                ((sharedCount < 2) || !Equal(common[1], vertex)))
            {
                if (!Check(sharedCount < 2, "only diagonal intersections are shared"))
                {
                    return false;
                }
                common[sharedCount++] = vertex;
            }
        }
    }
    return Check((sharedCount == 2) &&
                 ((common[0].x == 0) || (common[1].x == 0)) &&
                 ((common[0].y == 7168) || (common[1].y == 7168)),
                 "opposite shared-edge traversal has identical clipped coordinates, UV and RGBA");
}

static bool FloatEncodings()
{
    static constexpr float Vertex::* kFields[] = { &Vertex::x, &Vertex::y, &Vertex::s, &Vertex::t };
    static constexpr std::uint32_t kInvalidBits[] = {
        0x7F800000U, 0xFF800000U, 0x7FC00000U, 0xFFC00000U, 0x7F800001U, 0x00000001U, 0x80000001U
    };
    for (int i = 0; i < 3; ++i)
    {
        for (float Vertex::* field : kFields)
        {
            for (std::uint32_t bits : kInvalidBits)
            {
                Vertex input[3] = { kFull[0], kFull[1], kFull[2] };
                input[i].*field = std::bit_cast<float>(bits);
                if (!Check(Unchanged(input, kNtsc, 64, 32, Result::Invalid),
                           "every original vertex rejects nonportable float bits"))
                {
                    return false;
                }
            }
            Vertex input[3] = { kFull[0], kFull[1], kFull[2] };
            input[i].*field = std::numeric_limits<float>::max();
            if (!Check(Unchanged(input, kNtsc, 64, 32, Result::Invalid), "bounded float arithmetic"))
            {
                return false;
            }
        }
    }
    Vertex input[3] = { kFull[0], kFull[1], kFull[2] };
    input[0].x = -32769.0f;
    if (!Check(Unchanged(input, kNtsc, 64, 32, Result::Invalid), "coordinate lower bound"))
    {
        return false;
    }
    input[0].x = -32768.0f;
    input[2].s = -0.01f;
    if (!Check(Unchanged(input, kNtsc, 64, 32, Result::Invalid),
               "invalid UV cannot hide behind clipping an earlier vertex"))
    {
        return false;
    }
    input[0].x = -0.0f;
    input[0].y = std::numeric_limits<float>::min();
    input[2].s = -0.0f;
    FixedTriangle output[kMaxClippedTriangles]{};
    int count = 0;
    return Check((MapTriangle(input, kNtsc, 64, 32, output, count) == Result::Draw) && (count == 1),
                 "signed zero and minimum normal inputs accepted");
}

static std::uint32_t Random(std::uint32_t & state)
{
    state = (state * 1664525U) + 1013904223U;
    return state;
}

static bool BoundedPolygons()
{
    std::uint32_t state = 0x4D434C50U;
    int largestCount = 0;
    for (int iteration = 0; iteration < 20000; ++iteration)
    {
        Vertex input[3]{};
        for (Vertex & vertex : input)
        {
            vertex.x = static_cast<float>(static_cast<int>(Random(state) % 3001U) - 1500);
            vertex.y = static_cast<float>(static_cast<int>(Random(state) % 3001U) - 1500);
            vertex.s = static_cast<float>(Random(state) % 2049U) / 1024.0f;
            vertex.t = static_cast<float>(Random(state) % 2049U) / 1024.0f;
            vertex.r = static_cast<std::uint8_t>(Random(state) >> 24U);
            vertex.g = static_cast<std::uint8_t>(Random(state) >> 24U);
            vertex.b = static_cast<std::uint8_t>(Random(state) >> 24U);
            vertex.a = static_cast<std::uint8_t>(Random(state) >> 24U);
        }
        FixedTriangle output[kMaxClippedTriangles];
        FillSentinels(output);
        int count = -1;
        const Result result = MapTriangle(input, kNtsc, 64, 32, output, count);
        if (!Check((result != Result::Invalid) && (count >= 0) &&
                   (count <= kMaxClippedTriangles) && Untouched(output, count),
                   "convex clipping remains bounded without touching unused output"))
        {
            return false;
        }
        if (count > largestCount)
        {
            largestCount = count;
        }
        for (int i = 0; i < count; ++i)
        {
            if (!Check(Area(output[i]) != 0, "fixed-degenerate fan triangles omitted"))
            {
                return false;
            }
            for (const FixedVertex & vertex : output[i].vertices)
            {
                if (!Check((vertex.x >= 0) && (vertex.x <= 8192) &&
                           (vertex.y >= 0) && (vertex.y <= 7168) &&
                           (vertex.u >= 0) && (vertex.u <= 2048) &&
                           (vertex.v >= 0) && (vertex.v <= 1024),
                           "all output fields stay bounded"))
                {
                    return false;
                }
            }
        }
    }
    return Check(largestCount == kMaxClippedTriangles, "exercise maximum seven-vertex clipped polygon");
}

} // namespace

int main()
{
    return Mapping() && Clipping() && SharedEdges() && Rejections() && FloatEncodings() &&
           BoundedPolygons()
           ? 0
           : 1;
}
