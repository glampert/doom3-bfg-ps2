// ================================================================================================
// File: ui_triangles_tests.cpp
// Brief: Verify clipped UI triangle interpolation, winding, fixed seams and hostile float handling.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/masks.h"
#include "ps2/ui/triangles.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{

using ps2::ui::ClipMaskLine;
using ps2::ui::ClipMaskTriangle;
using ps2::ui::ConvexMask;
using ps2::ui::FixedLine;
using ps2::ui::FixedTriangle;
using ps2::ui::FixedVertex;
using ps2::ui::kMaxClippedTriangles;
using ps2::ui::kMaxMaskClipVertices;
using ps2::ui::kMaxMaskTriangles;
using ps2::ui::MapLine;
using ps2::ui::MapTriangle;
using ps2::ui::MaskBuilder;
using ps2::ui::MaskPoint;
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

static bool Equal(const Vertex & first, const Vertex & second)
{
    return first.x == second.x && first.y == second.y && first.s == second.s && first.t == second.t &&
           first.r == second.r && first.g == second.g && first.b == second.b && first.a == second.a;
}

static constexpr Vertex kMaskSentinel{ -99.0f, -98.0f, -97.0f, -96.0f, 11, 22, 33, 44 };

static void FillMaskSentinels(Vertex (&output)[kMaxMaskClipVertices])
{
    for (Vertex & vertex : output)
    {
        vertex = kMaskSentinel;
    }
}

static bool MaskUntouched(const Vertex (&output)[kMaxMaskClipVertices], int begin = 0)
{
    for (int i = begin; i < kMaxMaskClipVertices; ++i)
    {
        if (!Equal(output[i], kMaskSentinel))
        {
            return false;
        }
    }

    return true;
}

static bool MaskContains(const Vertex (&output)[kMaxMaskClipVertices], int count, const Vertex & expected)
{
    for (int i = 0; i < count; ++i)
    {
        if (Equal(output[i], expected))
        {
            return true;
        }
    }

    return false;
}

static ConvexMask RectangleMask(float left, float top, float right, float bottom)
{
    ConvexMask result{};
    result.points[0] = { left, top };
    result.points[1] = { right, top };
    result.points[2] = { right, bottom };
    result.points[3] = { left, bottom };
    result.count = 4;
    return result;
}

static bool AddQuad(MaskBuilder & builder, MaskPoint a, MaskPoint b, MaskPoint c, MaskPoint d)
{
    const MaskPoint first[3] = { a, b, c };
    const MaskPoint second[3] = { a, c, d };
    return builder.AddTriangle(first) && builder.AddTriangle(second);
}

static bool MaskConstruction()
{
    MaskBuilder builder;
    ConvexMask output{};
    output.count = -99;
    if (!Check(builder.Finish(output) && output.count == 0, "empty mask construction"))
    {
        return false;
    }

    const MaskPoint perimeter[] = {
        { 0.0f, 0.0f }, { 8.0f, 0.0f }, { 16.0f, 0.0f }, { 16.0f, 8.0f }, { 16.0f, 16.0f }, { 8.0f, 16.0f }, { 0.0f, 16.0f }, { 0.0f, 8.0f }
    };
    for (int reflected = 0; reflected < 2; ++reflected)
    {
        builder.Reset();
        for (int i = 0; i < 8; ++i)
        {
            MaskPoint triangle[3] = { { 8.0f, 8.0f }, perimeter[i], perimeter[(i + 1) % 8] };
            if ((i & 1) != 0)
            {
                const MaskPoint temporary = triangle[1];
                triangle[1] = triangle[2];
                triangle[2] = temporary;
            }

            if (reflected != 0)
            {
                for (MaskPoint & point : triangle)
                {
                    point.x = -point.x;
                }
            }

            if (!Check(builder.AddTriangle(triangle), "accept mixed triangle winding and reflected positions"))
            {
                return false;
            }
        }

        if (!Check(builder.Finish(output) && output.count == 4,
                   "cancel interior fan edges and simplify collinear boundary vertices"))
        {
            return false;
        }
    }

    for (int points = 16; points <= 17; ++points)
    {
        builder.Reset();
        for (int i = 1; i < points - 1; ++i)
        {
            const MaskPoint triangle[3] = {
                { 0.0f, 0.0f },
                { static_cast<float>(i), static_cast<float>(i * i) },
                { static_cast<float>(i + 1), static_cast<float>((i + 1) * (i + 1)) }
            };
            if (!Check(builder.AddTriangle(triangle), "bounded convex polygon fan"))
            {
                return false;
            }
        }

        output.count = -99;
        if (!Check((points == 16 ? builder.Finish(output) && output.count == 16 : !builder.Finish(output) && output.count == -99),
                   "exact convex boundary vertex limit"))
        {
            return false;
        }
    }

    builder.Reset();
    const MaskPoint degenerate[3] = { { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 2.0f, 2.0f } };
    for (int i = 0; i < kMaxMaskTriangles; ++i)
    {
        if (!Check(builder.AddTriangle(degenerate), "degenerate triangles have bounded ingestion work"))
        {
            return false;
        }
    }

    if (!Check(builder.Finish(output) && output.count == 0 && !builder.AddTriangle(degenerate),
               "triangle ingestion limit includes empty triangles"))
    {
        return false;
    }

    output.count = -99;
    if (!Check(!builder.Finish(output) && output.count == -99, "builder rejection is latched and transactional"))
    {
        return false;
    }

    builder.Reset();
    return Check(AddQuad(builder, { 0.0f, 0.0f }, { 16.0f, 0.0f }, { 16.0f, 16.0f }, { 0.0f, 16.0f }) &&
                 builder.Finish(output) && output.count == 4,
                 "reset recovers rejected builder");
}

static bool MaskTopologyRejections()
{
    static constexpr MaskPoint kInvalidMeshes[][6] = {
        // Two disconnected positive components.
        { { 0.0f, 0.0f }, { 4.0f, 0.0f }, { 0.0f, 4.0f }, { 10.0f, 10.0f }, { 14.0f, 10.0f }, { 10.0f, 14.0f } },
        // Crossing triangles overlap without a conforming shared edge.
        { { 0.0f, 0.0f }, { 8.0f, 0.0f }, { 0.0f, 8.0f }, { 1.0f, 1.0f }, { 9.0f, 1.0f }, { 1.0f, 9.0f } },
        // A simple but concave boundary has an inward turn at (2, 2).
        { { 0.0f, 0.0f }, { 8.0f, 0.0f }, { 2.0f, 2.0f }, { 0.0f, 0.0f }, { 2.0f, 2.0f }, { 0.0f, 8.0f } }
    };
    for (const MaskPoint(&mesh)[6] : kInvalidMeshes)
    {
        MaskBuilder builder;
        const MaskPoint first[3] = { mesh[0], mesh[1], mesh[2] };
        const MaskPoint second[3] = { mesh[3], mesh[4], mesh[5] };
        ConvexMask output{};
        output.count = -99;
        if (!Check(builder.AddTriangle(first) && builder.AddTriangle(second) &&
                   !builder.Finish(output) && output.count == -99,
                   "reject disconnected, overlapping and concave triangle coverage"))
        {
            return false;
        }
    }

    MaskBuilder ring;
    if (!Check(AddQuad(ring, { 0.0f, 0.0f }, { 12.0f, 0.0f }, { 8.0f, 4.0f }, { 4.0f, 4.0f }) &&
               AddQuad(ring, { 12.0f, 0.0f }, { 12.0f, 12.0f }, { 8.0f, 8.0f }, { 8.0f, 4.0f }) &&
               AddQuad(ring, { 12.0f, 12.0f }, { 0.0f, 12.0f }, { 4.0f, 8.0f }, { 8.0f, 8.0f }) &&
               AddQuad(ring, { 0.0f, 12.0f }, { 0.0f, 0.0f }, { 4.0f, 4.0f }, { 4.0f, 8.0f }),
               "ingest conforming ring triangles"))
    {
        return false;
    }

    ConvexMask output{};
    output.count = -99;
    if (!Check(!ring.Finish(output) && output.count == -99, "reject hole instead of filling its bounding polygon"))
    {
        return false;
    }

    MaskBuilder duplicate;
    const MaskPoint triangle[3] = { { 0.0f, 0.0f }, { 8.0f, 0.0f }, { 0.0f, 8.0f } };
    const MaskPoint reversed[3] = { triangle[0], triangle[2], triangle[1] };
    if (!Check(duplicate.AddTriangle(triangle) && !duplicate.AddTriangle(reversed) && !duplicate.Finish(output),
               "normalize winding before rejecting duplicate positive coverage"))
    {
        return false;
    }

    MaskBuilder thirdIncidence;
    const MaskPoint otherSide[3] = { { 8.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, -8.0f } };
    if (!Check(thirdIncidence.AddTriangle(triangle) && thirdIncidence.AddTriangle(otherSide) &&
               !thirdIncidence.AddTriangle(otherSide),
               "retain cancelled edges to reject third incidence"))
    {
        return false;
    }

    MaskBuilder tJunction;
    const MaskPoint fullEdge[3] = { { 0.0f, 0.0f }, { 16.0f, 0.0f }, { 0.0f, 16.0f } };
    const MaskPoint halfEdgeA[3] = { { 16.0f, 0.0f }, { 16.0f, 16.0f }, { 8.0f, 8.0f } };
    const MaskPoint halfEdgeB[3] = { { 8.0f, 8.0f }, { 16.0f, 16.0f }, { 0.0f, 16.0f } };
    return Check(tJunction.AddTriangle(fullEdge) && tJunction.AddTriangle(halfEdgeA) &&
                 tJunction.AddTriangle(halfEdgeB) && !tJunction.Finish(output),
                 "reject nonconforming shared-edge T junction instead of guessing coverage");
}

static bool MaskClipping()
{
    const Vertex input[3] = {
        { 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 255, 0 },
        { 16.0f, 0.0f, 1.0f, 0.0f, 255, 127, 0, 255 },
        { 0.0f, 16.0f, 0.0f, 1.0f, 127, 255, 255, 128 }
    };
    const ConvexMask masks[] = { RectangleMask(2.0f, 4.0f, 8.0f, 12.0f), RectangleMask(4.0f, 2.0f, 12.0f, 8.0f) };
    Vertex output[kMaxMaskClipVertices];
    FillMaskSentinels(output);
    int count = -99;
    if (!Check(ClipMaskTriangle(input, masks, 2, output, count) && count == 4 &&
               MaskContains(output, count, { 4.0f, 4.0f, 0.25f, 0.25f, 96, 96, 191, 96 }) &&
               MaskContains(output, count, { 8.0f, 4.0f, 0.5f, 0.25f, 159, 127, 128, 160 }) &&
               MaskContains(output, count, { 4.0f, 8.0f, 0.25f, 0.5f, 127, 159, 191, 128 }) &&
               MaskContains(output, count, { 8.0f, 8.0f, 0.5f, 0.5f, 191, 191, 128, 192 }) &&
               MaskUntouched(output, count),
               "nested masks interpolate UV and byte colors only once"))
    {
        return false;
    }

    const Vertex reversed[3] = { input[2], input[1], input[0] };
    Vertex backward[kMaxMaskClipVertices];
    FillMaskSentinels(backward);
    int backwardCount = -99;
    if (!Check(ClipMaskTriangle(reversed, masks, 2, backward, backwardCount) && backwardCount == count,
               "mask clipping preserves either input winding"))
    {
        return false;
    }

    for (int i = 0; i < count; ++i)
    {
        if (!Check(MaskContains(backward, backwardCount, output[i]), "reversed mask clip has exact UV/color intersections"))
        {
            return false;
        }
    }

    Vertex signedUv[3] = { input[0], input[1], input[2] };
    for (Vertex & vertex : signedUv)
    {
        vertex.s = -0.001953125f;
    }

    if (!Check(ClipMaskTriangle(signedUv, masks, 2, output, count) && count == 4,
               "retail font half-texel negative UV admitted"))
    {
        return false;
    }

    for (int i = 0; i < count; ++i)
    {
        if (!Check(output[i].s == -0.001953125f, "constant negative UV bits preserved through clipping"))
        {
            return false;
        }
    }

    FillMaskSentinels(output);
    if (!Check(ClipMaskTriangle(input, nullptr, 0, output, count) && count == 3 &&
               Equal(output[0], input[0]) && Equal(output[1], input[1]) && Equal(output[2], input[2]) &&
               MaskUntouched(output, count),
               "zero masks preserve original vertices"))
    {
        return false;
    }

    const ConvexMask empty{};
    const ConvexMask outside = RectangleMask(100.0f, 100.0f, 110.0f, 110.0f);
    const ConvexMask touching = RectangleMask(16.0f, 0.0f, 20.0f, 4.0f);
    const ConvexMask emptyCases[] = { empty, outside, touching };
    for (const ConvexMask & mask : emptyCases)
    {
        FillMaskSentinels(output);
        count = -99;
        if (!Check(ClipMaskTriangle(input, &mask, 1, output, count) && count == 0 && MaskUntouched(output),
                   "empty, disjoint and point-touching mask output is transactional"))
        {
            return false;
        }
    }

    return true;
}

static bool MaskClipRejections()
{
    const ConvexMask rectangle = RectangleMask(0.0f, 0.0f, 100.0f, 100.0f);
    Vertex output[kMaxMaskClipVertices];
    FillMaskSentinels(output);
    int count = -99;
    if (!Check(!ClipMaskTriangle(kFull, nullptr, 1, output, count) &&
               !ClipMaskTriangle(kFull, &rectangle, -1, output, count) &&
               !ClipMaskTriangle(kFull, &rectangle, 5, output, count) &&
               count == -99 && MaskUntouched(output),
               "mask pointer/count validation precedes dereference"))
    {
        return false;
    }

    static constexpr float Vertex::* kFields[] = { &Vertex::x, &Vertex::y, &Vertex::s, &Vertex::t };
    static constexpr std::uint32_t kInvalidBits[] = {
        0x7F800000U, 0xFF800000U, 0x7FC00000U, 0xFFC00000U, 0x00000001U, 0x80000001U, 0x7F7FFFFFU
    };
    const ConvexMask empty{};
    for (float Vertex::* field : kFields)
    {
        for (std::uint32_t bits : kInvalidBits)
        {
            Vertex input[3] = { kFull[0], kFull[1], kFull[2] };
            input[2].*field = std::bit_cast<float>(bits);
            if (!Check(!ClipMaskTriangle(input, &empty, 1, output, count) && count == -99 && MaskUntouched(output),
                       "invalid original float cannot hide behind empty mask"))
            {
                return false;
            }
        }
    }

    const ConvexMask invalidMasks[] = {
        RectangleMask(10.0f, 0.0f, 0.0f, 10.0f),
        RectangleMask(0.0f, 0.0f, 0.0f, 10.0f),
        RectangleMask(0.0f, 0.0f, 32769.0f, 10.0f)
    };
    for (const ConvexMask & invalid : invalidMasks)
    {
        const ConvexMask masks[2] = { empty, invalid };
        if (!Check(!ClipMaskTriangle(kFull, masks, 2, output, count) && count == -99 && MaskUntouched(output),
                   "validate every mask before processing empty coverage"))
        {
            return false;
        }
    }

    for (std::uint32_t bits : kInvalidBits)
    {
        MaskBuilder builder;
        const MaskPoint triangle[3] = { { 0.0f, 0.0f }, { 16.0f, 0.0f }, { 0.0f, std::bit_cast<float>(bits) } };
        ConvexMask result{};
        result.count = -99;
        if (!Check(!builder.AddTriangle(triangle) && !builder.Finish(result) && result.count == -99,
                   "mask construction rejects nonportable and unbounded point fields"))
        {
            return false;
        }

        ConvexMask invalid = rectangle;
        invalid.points[2].x = std::bit_cast<float>(bits);
        if (!Check(!ClipMaskTriangle(kFull, &invalid, 1, output, count) && count == -99 && MaskUntouched(output),
                   "clipper validates caller-supplied mask coordinates"))
        {
            return false;
        }
    }

    static constexpr int kInvalidCounts[] = { -1, 1, 2, 17 };
    for (int invalidCount : kInvalidCounts)
    {
        ConvexMask invalid = rectangle;
        invalid.count = invalidCount;
        if (!Check(!ClipMaskTriangle(kFull, &invalid, 1, output, count) && count == -99 && MaskUntouched(output),
                   "invalid mask polygon count checked before point access"))
        {
            return false;
        }
    }

    return true;
}

static bool MaskSharedEdges()
{
    // The mask is a translated, sheared quadrilateral; both shared endpoints lie outside it.
    // Two further masks require reconstruction after earlier clipping has replaced edge endpoints.
    ConvexMask masks[3] = {
        { { { 63.125f, 37.5f }, { 570.375f, 74.75f }, { 534.625f, 439.875f }, { 27.375f, 402.625f } }, 4 },
        RectangleMask(73.3125f, 41.1875f, 550.875f, 414.90625f),
        RectangleMask(51.0625f, 80.375f, 590.03125f, 423.625f)
    };
    const Vertex first[3] = {
        { -17.03125f, 77.3125f, 0.03125f, 0.0625f, 3, 252, 17, 7 },
        { 500.0f, 10.0f, 0.75f, 0.125f, 200, 27, 91, 180 },
        { 630.90625f, 499.53125f, 0.96875f, 0.9375f, 251, 7, 129, 249 }
    };
    const Vertex second[3] = {
        first[2], first[0], { 10.0f, 450.0f, 0.125f, 0.75f, 33, 89, 211, 100 }
    };
    Vertex forward[kMaxMaskClipVertices]{};
    Vertex backward[kMaxMaskClipVertices]{};
    int forwardCount = 0;
    int backwardCount = 0;
    if (!Check(ClipMaskTriangle(first, masks, 3, forward, forwardCount) && forwardCount >= 3 &&
               ClipMaskTriangle(second, masks, 3, backward, backwardCount) && backwardCount >= 3,
               "fractional transformed nested masks clip adjacent triangles"))
    {
        return false;
    }

    int shared = 0;
    for (int i = 0; i < forwardCount; ++i)
    {
        if (MaskContains(backward, backwardCount, forward[i]))
        {
            ++shared;
        }
    }

    return Check(shared == 2, "original shared edge retains identical float positions, UV and RGBA after nested masks");
}

static bool BoundedMaskClips()
{
    std::uint32_t state = 0x4D41534BU;
    for (int iteration = 0; iteration < 3000; ++iteration)
    {
        ConvexMask masks[4]{};
        for (ConvexMask & mask : masks)
        {
            const float x = static_cast<float>(Random(state) % 3201U) / 16.0f + 120.0f;
            const float y = static_cast<float>(Random(state) % 1921U) / 16.0f + 80.0f;
            const float shearX = static_cast<float>(static_cast<int>(Random(state) % 1025U) - 512) / 16.0f;
            const float shearY = static_cast<float>(static_cast<int>(Random(state) % 1025U) - 512) / 16.0f;
            mask.points[0] = { x - 160.0f - shearX, y - 120.0f - shearY };
            mask.points[1] = { x + 160.0f - shearX, y - 120.0f + shearY };
            mask.points[2] = { x + 160.0f + shearX, y + 120.0f + shearY };
            mask.points[3] = { x - 160.0f + shearX, y + 120.0f - shearY };
            mask.count = 4;
        }

        Vertex input[3] = { kFull[0], kFull[1], kFull[2] };
        input[1].x += static_cast<float>(Random(state) % 257U) / 16.0f;
        input[2].y += static_cast<float>(Random(state) % 257U) / 16.0f;
        Vertex output[kMaxMaskClipVertices];
        FillMaskSentinels(output);
        int count = -99;
        if (!Check(ClipMaskTriangle(input, masks, 4, output, count) && count >= 0 && count <= kMaxMaskClipVertices &&
                   MaskUntouched(output, count),
                   "fractional four-mask intersections remain bounded and transactional"))
        {
            std::fprintf(stderr, "Mask iteration: %d\n", iteration);
            return false;
        }

        const Vertex reversed[3] = { input[2], input[1], input[0] };
        Vertex backward[kMaxMaskClipVertices];
        FillMaskSentinels(backward);
        int backwardCount = -99;
        if (!Check(ClipMaskTriangle(reversed, masks, 4, backward, backwardCount) && backwardCount == count &&
                   MaskUntouched(backward, backwardCount),
                   "four-mask clipping is independent of input winding"))
        {
            return false;
        }

        for (int i = 0; i < count; ++i)
        {
            if (!Check(MaskContains(backward, backwardCount, output[i]),
                       "canonical original-edge and plane intersections preserve float UV/color bits under reversal"))
            {
                std::fprintf(stderr, "Mask winding iteration: %d\n", iteration);
                return false;
            }
        }

        const ConvexMask reversedMasks[4] = { masks[3], masks[2], masks[1], masks[0] };
        int reorderedCount = -99;
        if (!Check(ClipMaskTriangle(input, reversedMasks, 4, backward, reorderedCount) && reorderedCount == count,
                   "intersection count is independent of active mask insertion order"))
        {
            return false;
        }

        for (int i = 0; i < count; ++i)
        {
            if (!Check(MaskContains(backward, reorderedCount, output[i]),
                       "canonical plane pairs preserve exact output under mask reordering"))
            {
                return false;
            }
        }
    }

    static constexpr MaskPoint kCircle[] = {
        { -100.0f, 0.0f }, { -92.0f, -38.0f }, { -71.0f, -71.0f }, { -38.0f, -92.0f }, { 0.0f, -100.0f }, { 38.0f, -92.0f }, { 71.0f, -71.0f }, { 92.0f, -38.0f }, { 100.0f, 0.0f }, { 92.0f, 38.0f }, { 71.0f, 71.0f }, { 38.0f, 92.0f }, { 0.0f, 100.0f }, { -38.0f, 92.0f }, { -71.0f, 71.0f }, { -92.0f, 38.0f }
    };
    ConvexMask masks[4]{};
    for (int i = 0; i < 4; ++i)
    {
        masks[i].count = 16;
        const float scale = 1.0f + static_cast<float>(3 - i) * 0.125f;
        for (int j = 0; j < 16; ++j)
        {
            masks[i].points[j] = { 200.0f + kCircle[j].x * scale, 160.0f + kCircle[j].y * scale };
        }
    }

    const Vertex enclosing[3] = {
        { -1000.0f, -1000.0f, 0.0f, 0.0f, 0, 0, 0, 0 },
        { 5000.0f, -1000.0f, 1.0f, 0.0f, 255, 0, 0, 255 },
        { -1000.0f, 5000.0f, 0.0f, 1.0f, 0, 255, 0, 255 }
    };
    Vertex output[kMaxMaskClipVertices];
    FillMaskSentinels(output);
    int count = -99;
    return Check(ClipMaskTriangle(enclosing, masks, 4, output, count) && count == 16 && MaskUntouched(output, count),
                 "all sixty-four clipping planes exercise constraint words beyond bit sixty-three");
}

static bool Lines()
{
    FixedLine output{};
    const Vertex inside[2] = { { 64.0f, 48.0f, 0.5f, 0.5f, 255, 0, 0, 255 }, { 320.0f, 240.0f, 0.0f, 0.0f, 0, 255, 0, 128 } };

    // NTSC maps 640x480 onto 512x448: x * 0.8, y * 448 / 480, then 1/16 fixed point. UVs are ignored.
    if (!Check(MapLine(inside, kNtsc, output) == Result::Draw && output.vertices[0].x == 819 && output.vertices[0].y == 717 &&
                   output.vertices[1].x == 4096 && output.vertices[1].y == 3584 && output.vertices[0].u == 0 &&
                   output.vertices[1].v == 0 && output.vertices[0].r == 255 && output.vertices[1].a == 128,
               "line maps endpoints and colors"))
    {
        return false;
    }

    // Each viewport edge clips; the clipped endpoint interpolates color and the inside one is exact.
    const Vertex crossing[2] = { { -320.0f, 240.0f, 0, 0, 0, 0, 0, 0 }, { 320.0f, 240.0f, 0, 0, 200, 100, 50, 250 } };

    if (!Check(MapLine(crossing, kIdentity, output) == Result::Draw && output.vertices[0].x == 0 && output.vertices[0].y == 3840 &&
                   output.vertices[0].r == 100 && output.vertices[0].a == 125 && output.vertices[1].x == 5120 && output.vertices[1].r == 200,
               "line clips at the left edge with interpolated color"))
    {
        return false;
    }

    const Vertex diagonal[2] = { { -100.0f, -100.0f, 0, 0, 0, 0, 0, 0 }, { 740.0f, 580.0f, 0, 0, 255, 255, 255, 255 } };

    // It enters through the top edge at x = 23.53 and leaves through the bottom one at x = 616.47.
    if (!Check(MapLine(diagonal, kIdentity, output) == Result::Draw && output.vertices[0].y == 0 && output.vertices[0].x >= 375 &&
                   output.vertices[0].x <= 377 && output.vertices[1].y == 480 * 16 && output.vertices[1].x >= 9863 &&
                   output.vertices[1].x <= 9865 && output.vertices[0].r == 38 && output.vertices[1].r == 218,
               "line clips at two edges"))
    {
        return false;
    }

    const FixedLine sentinel{ { kSentinel, kSentinel } };
    const Vertex outside[2] = { { -10.0f, 10.0f, 0, 0, 0, 0, 0, 0 }, { -1.0f, 400.0f, 0, 0, 0, 0, 0, 0 } };
    const Vertex point[2] = { { 100.0f, 100.0f, 0, 0, 0, 0, 0, 0 }, { 100.01f, 100.0f, 0, 0, 0, 0, 0, 0 } };
    const Vertex hostile[2] = { { std::numeric_limits<float>::infinity(), 0.0f, 0, 0, 0, 0, 0, 0 }, { 1.0f, 1.0f, 0, 0, 0, 0, 0, 0 } };
    const Vertex far[2] = { { 0.0f, 0.0f, 0, 0, 0, 0, 0, 0 }, { 40000.0f, 1.0f, 0, 0, 0, 0, 0, 0 } };
    const auto Untouched = [&sentinel](const FixedLine & line)
    {
        return Equal(line.vertices[0], sentinel.vertices[0]) && Equal(line.vertices[1], sentinel.vertices[1]);
    };

    output = sentinel;
    bool passed = Check(MapLine(outside, kIdentity, output) == Result::Clipped && Untouched(output), "line outside the viewport");
    output = sentinel;
    passed = Check(MapLine(point, kNtsc, output) == Result::Clipped && Untouched(output), "line without fixed-point length") && passed;
    output = sentinel;
    passed = Check(MapLine(hostile, kNtsc, output) == Result::Invalid && Untouched(output), "line rejects infinity") && passed;
    output = sentinel;
    passed = Check(MapLine(far, kNtsc, output) == Result::Invalid && Untouched(output), "line coordinate bound") && passed;
    output = sentinel;
    return Check(MapLine(inside, { 0, 448 }, output) == Result::Invalid && Untouched(output), "line viewport bound") && passed;
}

static bool MaskLines()
{
    const ConvexMask mask = RectangleMask(100.0f, 100.0f, 200.0f, 200.0f);
    const Vertex across[2] = { { 50.0f, 150.0f, 0, 0, 0, 0, 0, 0 }, { 250.0f, 150.0f, 0, 0, 200, 100, 0, 200 } };
    Vertex output[2]{};
    bool visible = false;

    if (!Check(ClipMaskLine(across, &mask, 1, output, visible) && visible && output[0].x == 100.0f && output[1].x == 200.0f &&
                   output[0].r == 50 && output[1].r == 150 && output[0].a == 50 && output[1].a == 150,
               "mask clips both line ends with interpolated color"))
    {
        return false;
    }

    const Vertex inside[2] = { { 120.0f, 120.0f, 0, 0, 1, 2, 3, 4 }, { 180.0f, 190.0f, 0, 0, 5, 6, 7, 8 } };

    if (!Check(ClipMaskLine(inside, &mask, 1, output, visible) && visible && Equal(output[0], inside[0]) && Equal(output[1], inside[1]),
               "line inside a mask is unchanged"))
    {
        return false;
    }

    const Vertex outside[2] = { { 50.0f, 50.0f, 0, 0, 0, 0, 0, 0 }, { 250.0f, 90.0f, 0, 0, 0, 0, 0, 0 } };
    const ConvexMask empty{};
    bool passed = Check(ClipMaskLine(outside, &mask, 1, output, visible) && !visible, "line outside a mask");
    passed = Check(ClipMaskLine(inside, &empty, 1, output, visible) && !visible, "empty mask hides a line") && passed;
    passed = Check(ClipMaskLine(inside, nullptr, 0, output, visible) && visible && Equal(output[0], inside[0]), "no mask keeps a line") && passed;
    const Vertex hostile[2] = { { 1.0f, std::numeric_limits<float>::quiet_NaN(), 0, 0, 0, 0, 0, 0 }, { 1.0f, 1.0f, 0, 0, 0, 0, 0, 0 } };
    return Check(!ClipMaskLine(hostile, &mask, 1, output, visible), "mask line rejects NaN") &&
           Check(!ClipMaskLine(inside, &mask, 5, output, visible), "mask line count bound") && passed;
}

} // namespace

int main()
{
    return Mapping() && Clipping() && SharedEdges() && Rejections() && FloatEncodings() &&
           BoundedPolygons() && MaskConstruction() && MaskTopologyRejections() && MaskClipping() &&
           MaskClipRejections() && MaskSharedEdges() && BoundedMaskClips() && Lines() && MaskLines()
           ? 0
           : 1;
}
