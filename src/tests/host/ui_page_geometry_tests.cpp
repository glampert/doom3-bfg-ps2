// ================================================================================================
// File: ui_page_geometry_tests.cpp
// Brief: Verify page seams, interpolation, winding and hostile input admission without target DMA.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/ui_page_geometry.h"

#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <limits>
#include <set>

#ifdef NDEBUG
    #error "Page geometry checks require active assertions."
#endif

namespace
{

using ps2::ui::FixedTriangle;
using ps2::ui::FixedVertex;
using ps2::ui::kMaxPageTriangles;
using ps2::ui::MapPageTriangle;
using ps2::ui::Result;
using ps2::ui::Vertex;
using ps2::ui::Viewport;

static constexpr Viewport kIdentity{ 640, 480 };
static constexpr Viewport kNtsc{ 512, 448 };
static constexpr Viewport kPal{ 512, 512 };
static constexpr FixedVertex kSentinel{ -11, -22, -33, -44, 9, 8, 7, 6 };
static constexpr Vertex kTriangle[3] = {
    { 0.0f, 0.0f, 0.0f, 0.0f, 0, 20, 17, 0 },
    { 640.0f, 0.0f, 1.0f, 0.0f, 200, 20, 17, 200 },
    { 0.0f, 480.0f, 0.0f, 1.0f, 0, 220, 17, 0 }
};

static bool Equal(const FixedVertex & first, const FixedVertex & second)
{
    return (first.x == second.x) && (first.y == second.y) && (first.u == second.u) && (first.v == second.v) &&
           (first.r == second.r) && (first.g == second.g) && (first.b == second.b) && (first.a == second.a);
}

static void Fill(FixedTriangle (&output)[kMaxPageTriangles])
{
    for (FixedTriangle & triangle : output)
    {
        for (FixedVertex & vertex : triangle.vertices)
        {
            vertex = kSentinel;
        }
    }
}

static void AssertUntouched(const FixedTriangle (&output)[kMaxPageTriangles], int first = 0)
{
    for (int i = first; i < kMaxPageTriangles; ++i)
    {
        for (const FixedVertex & vertex : output[i].vertices)
        {
            assert(Equal(vertex, kSentinel));
        }
    }
}

static bool Contains(const FixedTriangle (&output)[kMaxPageTriangles], int count, const FixedVertex & value)
{
    for (int i = 0; i < count; ++i)
    {
        for (const FixedVertex & vertex : output[i].vertices)
        {
            if (Equal(vertex, value))
            {
                return true;
            }
        }
    }

    return false;
}

static std::int64_t Area(const FixedTriangle & triangle)
{
    const FixedVertex(&vertices)[3] = triangle.vertices;
    const std::int64_t dx1 = vertices[1].x - vertices[0].x;
    const std::int64_t dy1 = vertices[1].y - vertices[0].y;
    const std::int64_t dx2 = vertices[2].x - vertices[0].x;
    const std::int64_t dy2 = vertices[2].y - vertices[0].y;
    return dx1 * dy2 - dy1 * dx2;
}

static void AssertResult(const Vertex (&input)[3], Viewport viewport, int width, int height, int column, int row, Result result)
{
    FixedTriangle output[kMaxPageTriangles];
    Fill(output);
    int count = -99;
    assert(MapPageTriangle(input, viewport, width, height, column, row, output, count) == result);
    assert(count == (result == Result::Invalid ? -99 : 0));
    AssertUntouched(output);
}

static void BasicMapping()
{
    FixedTriangle output[kMaxPageTriangles];
    Fill(output);
    int count = -1;
    assert(MapPageTriangle(kTriangle, kNtsc, 254, 254, 0, 0, output, count) == Result::Draw);
    assert(count == 1);
    assert(Equal(output[0].vertices[0], { 0, 0, 16, 16, 0, 20, 17, 0 }));
    assert(Equal(output[0].vertices[1], { 8192, 0, 4080, 16, 200, 20, 17, 200 }));
    assert(Equal(output[0].vertices[2], { 0, 7168, 16, 4080, 0, 220, 17, 0 }));
    AssertUntouched(output, count);
    assert(MapPageTriangle(kTriangle, kPal, 1, 1, 0, 0, output, count) == Result::Draw);
    assert(count == 1);
    assert(output[0].vertices[1].u == 32);
    assert(output[0].vertices[2].v == 32);
    assert(output[0].vertices[2].y == 8192);

    // The last NPOT page has one interior texel; gutter/padding is not logical artwork.
    const Vertex edge[3] = {
        { 100, 100, 1, 0, 3, 4, 5, 6 }, { 200, 100, 1, 1, 7, 8, 9, 10 }, { 100, 200, 1, 0, 11, 12, 13, 14 }
    };
    assert(MapPageTriangle(edge, kIdentity, 255, 100, 1, 0, output, count) == Result::Draw);
    assert(count == 1);
    assert(output[0].vertices[0].u == 32);
    assert(output[0].vertices[1].u == 32);
    assert(output[0].vertices[1].v == 1616);
}

static void PageSeams()
{
    const Vertex quad[4] = {
        { 0, 0, 0, 0, 0, 20, 17, 0 }, { 640, 0, 1, 0, 200, 20, 17, 200 }, { 640, 480, 1, 1, 200, 220, 17, 200 }, { 0, 480, 0, 1, 0, 220, 17, 0 }
    };
    const Vertex input[2][3] = { { quad[0], quad[1], quad[2] }, { quad[2], quad[3], quad[0] } };
    for (int row = 0; row < 2; ++row)
    {
        for (int column = 0; column < 2; ++column)
        {
            std::int64_t area = 0;
            bool center = false;
            for (const Vertex(&triangle)[3] : input)
            {
                FixedTriangle output[kMaxPageTriangles];
                Fill(output);
                int count = -1;
                const Result result = MapPageTriangle(triangle, kIdentity, 508, 508, column, row, output, count);
                assert(result != Result::Invalid);
                assert(count >= 0 && count <= kMaxPageTriangles);
                AssertUntouched(output, count);
                for (int i = 0; i < count; ++i)
                {
                    area += Area(output[i]);
                    for (const FixedVertex & vertex : output[i].vertices)
                    {
                        assert(vertex.x >= column * 5120 && vertex.x <= (column + 1) * 5120);
                        assert(vertex.y >= row * 3840 && vertex.y <= (row + 1) * 3840);
                        assert(vertex.u >= 16 && vertex.u <= 4080);
                        assert(vertex.v >= 16 && vertex.v <= 4080);
                        if (vertex.x == 5120 && vertex.y == 3840)
                        {
                            assert(vertex.r == 100 && vertex.g == 120 && vertex.b == 17 && vertex.a == 100);
                            assert(vertex.u == (column == 0 ? 4080 : 16));
                            assert(vertex.v == (row == 0 ? 4080 : 16));
                            center = true;
                        }
                    }
                }
            }

            assert(center);
            assert(area == 39321600);
        }
    }
}

static void BoundaryOwnership()
{
    Vertex input[3] = { kTriangle[0], kTriangle[1], kTriangle[2] };
    for (Vertex & vertex : input)
    {
        vertex.s = 0.5f;
    }

    AssertResult(input, kIdentity, 508, 254, 0, 0, Result::Clipped);
    FixedTriangle output[kMaxPageTriangles]{};
    int count = 0;
    assert(MapPageTriangle(input, kIdentity, 508, 254, 1, 0, output, count) == Result::Draw);
    assert(count == 1);
    for (const FixedVertex & vertex : output[0].vertices)
    {
        assert(vertex.u == 16);
    }

    for (Vertex & vertex : input)
    {
        vertex.s = 0.0f;
        vertex.t = 0.5f;
    }

    AssertResult(input, kIdentity, 254, 508, 0, 0, Result::Clipped);
    assert(MapPageTriangle(input, kIdentity, 254, 508, 0, 1, output, count) == Result::Draw);
    assert(count == 1);
    for (const FixedVertex & vertex : output[0].vertices)
    {
        assert(vertex.v == 16);
    }

    for (Vertex & vertex : input)
    {
        vertex.s = 0.5f;
    }

    AssertResult(input, kIdentity, 508, 508, 0, 0, Result::Clipped);
    AssertResult(input, kIdentity, 508, 508, 0, 1, Result::Clipped);
    AssertResult(input, kIdentity, 508, 508, 1, 0, Result::Clipped);
    assert(MapPageTriangle(input, kIdentity, 508, 508, 1, 1, output, count) == Result::Draw);
    assert(count == 1);
    for (Vertex & vertex : input)
    {
        vertex.s = 1.0f;
        vertex.t = 1.0f;
    }

    assert(MapPageTriangle(input, kIdentity, 508, 508, 1, 1, output, count) == Result::Draw);
    assert(count == 1);
    assert(output[0].vertices[0].u == 4080 && output[0].vertices[0].v == 4080);
}

static void ClippingAndWinding()
{
    const Vertex input[3] = {
        { -100, 100, 0, 0, 0, 20, 100, 0 },
        { 100, 100, 1, 0, 200, 40, 200, 200 },
        { 100, 300, 1, 1, 100, 60, 0, 100 }
    };
    FixedTriangle output[kMaxPageTriangles]{};
    int count = 0;
    assert(MapPageTriangle(input, kIdentity, 100, 100, 0, 0, output, count) == Result::Draw);
    assert(count == 2);
    assert(Contains(output, count, { 0, 1600, 816, 16, 100, 30, 150, 100 }));
    assert(Contains(output, count, { 0, 3200, 816, 816, 50, 40, 50, 50 }));

    const Vertex crossing[3] = {
        { -100, 100, 0.125f, 0.0625f, 0, 13, 255, 70 },
        { 740, 100, 0.9375f, 0.125f, 255, 90, 0, 200 },
        { 320, 600, 0.25f, 1, 127, 255, 100, 255 }
    };
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            const Result result = MapPageTriangle(crossing, kNtsc, 800, 600, column, row, output, count);
            assert(result != Result::Invalid);
            for (int start = 0; start < 3; ++start)
            {
                const Vertex reverse[3] = { crossing[start], crossing[(start + 2) % 3], crossing[(start + 1) % 3] };
                FixedTriangle backward[kMaxPageTriangles]{};
                int backCount = 0;
                assert(MapPageTriangle(reverse, kNtsc, 800, 600, column, row, backward, backCount) == result);
                assert(backCount == count);
                std::int64_t area = 0;
                for (int i = 0; i < count; ++i)
                {
                    area += Area(output[i]) + Area(backward[i]);
                    for (const FixedVertex & vertex : output[i].vertices)
                    {
                        assert(Contains(backward, backCount, vertex));
                    }
                }

                assert(area == 0);
            }
        }
    }
}

static void SharedIndexedEdges()
{
    const Vertex first[3] = {
        { -17.03125f, 77.3125f, 0.03125f, 0.0625f, 3, 252, 17, 7 },
        { 500.0f, 10.0f, 0.75f, 0.125f, 200, 27, 91, 180 },
        { 630.90625f, 499.53125f, 0.96875f, 0.9375f, 251, 7, 129, 249 }
    };
    const Vertex second[3] = {
        first[2], first[0], { 10.0f, 450.0f, 0.125f, 0.75f, 33, 89, 211, 100 }
    };
    int sharedPages = 0;
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 5; ++column)
        {
            FixedTriangle forward[kMaxPageTriangles]{};
            FixedTriangle backward[kMaxPageTriangles]{};
            int forwardCount = 0;
            int backwardCount = 0;
            assert(MapPageTriangle(first, kNtsc, 1024, 768, column, row, forward, forwardCount) != Result::Invalid);
            assert(MapPageTriangle(second, kNtsc, 1024, 768, column, row, backward, backwardCount) != Result::Invalid);
            FixedVertex shared[2]{};
            int sharedCount = 0;
            for (int i = 0; i < forwardCount; ++i)
            {
                for (const FixedVertex & vertex : forward[i].vertices)
                {
                    if (Contains(backward, backwardCount, vertex) &&
                        ((sharedCount == 0) || !Equal(shared[0], vertex)) &&
                        ((sharedCount < 2) || !Equal(shared[1], vertex)))
                    {
                        assert(sharedCount < 2);
                        shared[sharedCount++] = vertex;
                    }
                }
            }

            if (sharedCount == 2)
            {
                ++sharedPages;
            }
        }
    }

    assert(sharedPages >= 3);
}

static void Rejections()
{
    const Viewport viewports[] = { { 0, 1 }, { 1, 0 }, { -1, 1 }, { 1025, 1 }, { 1, 1025 } };
    for (Viewport viewport : viewports)
    {
        AssertResult(kTriangle, viewport, 254, 254, 0, 0, Result::Invalid);
    }

    const int dimensions[][2] = { { 0, 1 }, { 1, -1 }, { 16385, 1 }, { 1, 16385 }, { 16384, 1025 }, { std::numeric_limits<int>::max(), std::numeric_limits<int>::max() } };
    for (const auto & size : dimensions)
    {
        AssertResult(kTriangle, kIdentity, size[0], size[1], 0, 0, Result::Invalid);
    }

    AssertResult(kTriangle, kIdentity, 508, 508, -1, 0, Result::Invalid);
    AssertResult(kTriangle, kIdentity, 508, 508, 0, -1, Result::Invalid);
    AssertResult(kTriangle, kIdentity, 508, 508, 2, 0, Result::Invalid);
    AssertResult(kTriangle, kIdentity, 508, 508, 0, 2, Result::Invalid);

    static constexpr float Vertex::* kFields[] = { &Vertex::x, &Vertex::y, &Vertex::s, &Vertex::t };
    static constexpr std::uint32_t kBadBits[] = { 0x7F800000U, 0xFF800000U, 0x7FC00000U, 0x7F800001U, 1U, 0x80000001U };
    for (int index = 0; index < 3; ++index)
    {
        for (float Vertex::* field : kFields)
        {
            for (std::uint32_t bits : kBadBits)
            {
                Vertex input[3] = { kTriangle[0], kTriangle[1], kTriangle[2] };
                input[index].*field = std::bit_cast<float>(bits);
                AssertResult(input, kIdentity, 254, 254, 0, 0, Result::Invalid);
            }
        }
    }

    Vertex input[3] = { kTriangle[0], kTriangle[1], kTriangle[2] };
    input[0].x = -32769;
    AssertResult(input, kIdentity, 254, 254, 0, 0, Result::Invalid);
    input[0].x = -32768;
    input[2].s = -0.01f;
    AssertResult(input, kIdentity, 254, 254, 0, 0, Result::Invalid);
    input[2].s = 1.01f;
    AssertResult(input, kIdentity, 254, 254, 0, 0, Result::Invalid);
    input[0] = kTriangle[0];
    input[2] = kTriangle[1];
    AssertResult(input, kIdentity, 254, 254, 0, 0, Result::Clipped);
    input[1].x = 0.001f;
    input[2] = kTriangle[2];
    input[2].y = 0.001f;
    AssertResult(input, kIdentity, 254, 254, 0, 0, Result::Clipped);
    input[0].x = -0.0f;
    input[0].s = -0.0f;
    input[0].y = std::numeric_limits<float>::min();
    FixedTriangle output[kMaxPageTriangles]{};
    int count = 0;
    assert(MapPageTriangle(input, kIdentity, 254, 254, 0, 0, output, count) != Result::Invalid);
}

static std::uint32_t Random(std::uint32_t & state)
{
    state = state * 1664525U + 1013904223U;
    return state;
}

using SeamVertex = std::array<int, 7>;
using SeamVertices = std::set<SeamVertex>;

static SeamVertices Seam(const FixedTriangle (&triangles)[kMaxPageTriangles], int count, bool vertical, int boundary)
{
    SeamVertices result;
    for (int i = 0; i < count; ++i)
    {
        for (const FixedVertex & vertex : triangles[i].vertices)
        {
            if ((vertical ? vertex.u : vertex.v) == boundary)
            {
                result.insert({ vertex.x, vertex.y, vertical ? vertex.v : vertex.u, vertex.r, vertex.g, vertex.b, vertex.a });
            }
        }
    }

    return result;
}

static void AdjacentPageRegression()
{
    // The old chained interpolation differed by one GS subpixel at the lower
    // shared endpoint: column 1 clipped an edge already changed by U=254 while
    // column 2 constructed U=508 directly from the original edge.
    const Vertex input[3] = {
        { 700.70874f, -26.107893f, 0.0752850696f, 0.564191818f, 80, 110, 140, 170 },
        { 448.169769f, 522.80481f, 0.919546902f, 0.2003555f, 80, 110, 140, 170 },
        { 720.190979f, 217.450958f, 0.403793961f, 0.993158221f, 80, 110, 140, 170 }
    };
    FixedTriangle left[kMaxPageTriangles]{};
    FixedTriangle right[kMaxPageTriangles]{};
    int leftCount = 0;
    int rightCount = 0;
    assert(MapPageTriangle(input, kNtsc, 800, 600, 1, 0, left, leftCount) == Result::Draw);
    assert(MapPageTriangle(input, kNtsc, 800, 600, 2, 0, right, rightCount) == Result::Draw);
    const SeamVertices first = Seam(left, leftCount, true, 4080);
    const SeamVertices second = Seam(right, rightCount, true, 16);
    assert(first.size() == 2);
    assert(first == second);

    // An interior point U=507.984528 also rounds to fixed page U=4080. It is
    // correctly present only in the left polygon, in addition to both actual
    // shared endpoints. Rounded UV equality alone does not identify provenance.
    const Vertex quantizedAlias[3] = {
        { -84.6530914f, -67.6198807f, 0.53853178f, 0.558655024f, 80, 110, 140, 170 },
        { 145.777145f, 242.29892f, 0.80107075f, 0.860236883f, 80, 110, 140, 170 },
        { 727.416931f, 176.435333f, 0.796920061f, 0.32201013f, 80, 110, 140, 170 }
    };
    assert(MapPageTriangle(quantizedAlias, kNtsc, 800, 600, 1, 1, left, leftCount) == Result::Draw);
    assert(MapPageTriangle(quantizedAlias, kNtsc, 800, 600, 2, 1, right, rightCount) == Result::Draw);
    const SeamVertices aliases = Seam(left, leftCount, true, 4080);
    const SeamVertices actual = Seam(right, rightCount, true, 16);
    assert(aliases.size() == 3 && actual.size() == 2);
    for (const SeamVertex & vertex : actual)
    {
        assert(aliases.contains(vertex));
    }

    const Vertex constantV[3] = {
        { 421.548462f, 322.840912f, 0.124545313f, 0.63499999f, 132, 133, 131, 23 },
        { 685.320923f, 293.464844f, 0.524764955f, 0.63499999f, 102, 176, 175, 192 },
        { 109.091942f, 561.408997f, 0.486068368f, 0.63499999f, 249, 152, 5, 27 }
    };
    AssertResult(constantV, kNtsc, 600, 800, 0, 1, Result::Clipped);
    assert(MapPageTriangle(constantV, kNtsc, 600, 800, 0, 2, left, leftCount) == Result::Draw);
    for (int i = 0; i < leftCount; ++i)
    {
        for (const FixedVertex & vertex : left[i].vertices)
        {
            assert(vertex.v == 16);
        }
    }

    // A rank-one UV mapping crosses the shared U=1016/V=762 corner.
    // These clip equations are redundant even though their axes differ.
    const Vertex rankOne[3] = {
        { 145.132172f, 115.932747f, 0.4111328125f, 0.4111328125f, 114, 182, 27, 124 },
        { 175.799942f, 492.735565f, 0.8564453125f, 0.8564453125f, 75, 199, 219, 59 },
        { 217.390915f, 267.89917f, 0.23828125f, 0.23828125f, 174, 78, 70, 46 }
    };
    AssertResult(rankOne, kNtsc, 1600, 1200, 3, 3, Result::Clipped);
    assert(MapPageTriangle(rankOne, kNtsc, 1600, 1200, 4, 3, left, leftCount) == Result::Draw);

    const Vertex corners[3] = {
        { 0, 480, 0.635f, 254.0f / 600.0f, 1, 255, 128, 63 },
        { 740, -20, 1, 0, 250, 19, 0, 192 },
        { -30, 100, 0, 1, 71, 32, 247, 10 }
    };
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            assert(MapPageTriangle(corners, kNtsc, 800, 600, column, row, left, leftCount) != Result::Invalid);
        }
    }
}

static void OuterFontGutters()
{
    // Native font bounds expand by half a source texel. The outside gutter
    // keeps the full screen geometry and maps the endpoint to page texel 0.5.
    const Vertex lower[3] = {
        { 100.0f, 100.0f, -0.5f / 512.0f, -0.5f / 128.0f, 80, 100, 120, 140 },
        { 116.0f, 100.0f, 15.5f / 512.0f, -0.5f / 128.0f, 80, 100, 120, 140 },
        { 100.0f, 115.0f, -0.5f / 512.0f, 14.5f / 128.0f, 80, 100, 120, 140 }
    };
    FixedTriangle output[kMaxPageTriangles];
    Fill(output);
    int count = -99;
    assert(MapPageTriangle(lower, kIdentity, 512, 128, 0, 0, output, count) == Result::Invalid);
    assert(count == -99);
    AssertUntouched(output);
    assert(MapPageTriangle(lower, kIdentity, 512, 128, 0, 0, output, count, true) == Result::Draw);
    assert(count == 1);
    assert(Contains(output, count, { 1600, 1600, 8, 8, 80, 100, 120, 140 }));
    assert(Contains(output, count, { 1856, 1600, 264, 8, 80, 100, 120, 140 }));
    assert(Contains(output, count, { 1600, 1840, 8, 248, 80, 100, 120, 140 }));

    const Vertex upper[3] = {
        { 100.0f, 100.0f, 511.5f / 512.0f, 127.5f / 128.0f, 80, 100, 120, 140 },
        { 116.0f, 100.0f, 512.5f / 512.0f, 127.5f / 128.0f, 80, 100, 120, 140 },
        { 100.0f, 115.0f, 511.5f / 512.0f, 128.5f / 128.0f, 80, 100, 120, 140 }
    };
    assert(MapPageTriangle(upper, kIdentity, 512, 128, 2, 0, output, count, true) == Result::Draw);
    assert(count == 1);
    assert(Contains(output, count, { 1600, 1600, 72, 2056, 80, 100, 120, 140 }));
    assert(Contains(output, count, { 1856, 1600, 88, 2056, 80, 100, 120, 140 }));
    assert(Contains(output, count, { 1600, 1840, 72, 2072, 80, 100, 120, 140 }));

    Vertex invalid[3] = { lower[0], lower[1], lower[2] };
    invalid[0].s = -0.51f / 512.0f;
    Fill(output);
    count = -99;
    assert(MapPageTriangle(invalid, kIdentity, 512, 128, 0, 0, output, count, true) == Result::Invalid);
    assert(count == -99);
    AssertUntouched(output);

    // Extending the outside atlas planes must not move any endpoint shared
    // by the ordinary interior page planes.
    const Vertex crossing[3] = {
        { 20.0f, 20.0f, -0.5f / 512.0f, -0.5f / 128.0f, 80, 100, 120, 140 },
        { 620.0f, 20.0f, 510.5f / 512.0f, -0.5f / 128.0f, 80, 100, 120, 140 },
        { 20.0f, 450.0f, -0.5f / 512.0f, 100.5f / 128.0f, 80, 100, 120, 140 }
    };
    FixedTriangle right[kMaxPageTriangles]{};
    int rightCount = 0;
    assert(MapPageTriangle(crossing, kNtsc, 512, 128, 0, 0, output, count, true) == Result::Draw);
    assert(MapPageTriangle(crossing, kNtsc, 512, 128, 1, 0, right, rightCount, true) == Result::Draw);
    const SeamVertices leftSeam = Seam(output, count, true, 4080);
    const SeamVertices rightSeam = Seam(right, rightCount, true, 16);
    assert(leftSeam.size() == 2 && leftSeam == rightSeam);
}

static void RandomAdjacentPages()
{
    std::uint32_t state = 0x5345414DU;
    int comparedU = 0;
    int comparedV = 0;
    for (int iteration = 0; iteration < 20000; ++iteration)
    {
        Vertex input[3]{};
        for (Vertex & vertex : input)
        {
            vertex.x = static_cast<float>(static_cast<int>(Random(state) % 13441U) - 1600) / 16.0f;
            vertex.y = static_cast<float>(static_cast<int>(Random(state) % 10881U) - 1600) / 16.0f;
            vertex.s = static_cast<float>(Random(state) % 65537U) / 65536.0f;
            vertex.t = static_cast<float>(Random(state) % 65537U) / 65536.0f;
            vertex.r = static_cast<std::uint8_t>(Random(state) >> 24U);
            vertex.g = static_cast<std::uint8_t>(Random(state) >> 24U);
            vertex.b = static_cast<std::uint8_t>(Random(state) >> 24U);
            vertex.a = static_cast<std::uint8_t>(Random(state) >> 24U);
        }

        for (int axis = 0; axis < 2; ++axis)
        {
            const bool vertical = axis == 0;
            const int column = vertical ? 1 : static_cast<int>(Random(state) % 4U);
            const int row = vertical ? static_cast<int>(Random(state) % 3U) : 0;
            FixedTriangle first[kMaxPageTriangles]{};
            FixedTriangle second[kMaxPageTriangles]{};
            int firstCount = 0;
            int secondCount = 0;
            const Result firstResult = MapPageTriangle(input, kNtsc, 800, 600, column, row, first, firstCount);
            const Result secondResult = MapPageTriangle(input, kNtsc, 800, 600, column + (vertical ? 1 : 0), row + (vertical ? 0 : 1), second, secondCount);
            assert(firstResult != Result::Invalid && secondResult != Result::Invalid);
            if (firstResult != Result::Draw || secondResult != Result::Draw)
            {
                continue;
            }

            const SeamVertices a = Seam(first, firstCount, vertical, 4080);
            const SeamVertices b = Seam(second, secondCount, vertical, 16);
            // Compare well-separated two-endpoint boundaries. Additional rounded
            // UV aliases are covered by the explicit regression above; they do
            // not identify shared points without the internal support constraints.
            if (a.size() == 2 && b.size() == 2)
            {
                assert(a == b);
                if (vertical)
                {
                    ++comparedU;
                }
                else
                {
                    ++comparedV;
                }
            }
        }
    }

    assert(comparedU > 3000 && comparedV > 2000);
}

static void RankOneMappings()
{
    std::uint32_t state = 0x52414E4BU;
    for (int iteration = 0; iteration < 1200; ++iteration)
    {
        Vertex input[3]{};
        for (Vertex & vertex : input)
        {
            vertex.x = static_cast<float>(static_cast<int>(Random(state) % 13441U) - 1600) / 16.0f;
            vertex.y = static_cast<float>(static_cast<int>(Random(state) % 10881U) - 1600) / 16.0f;
            vertex.s = static_cast<float>(Random(state) % 1025U) / 1024.0f;
            vertex.t = vertex.s;
            if (iteration % 5 == 1)
            {
                vertex.t = 1.0f - vertex.s;
            }
            else if (iteration % 5 == 2)
            {
                vertex.t = 0.5f;
            }
            else if (iteration % 5 == 3)
            {
                vertex.s = 0.5f;
            }
            else if (iteration % 5 == 4)
            {
                vertex.s = 0.5f;
                vertex.t = 0.5f;
            }

            vertex.r = static_cast<std::uint8_t>(Random(state) >> 24U);
            vertex.g = static_cast<std::uint8_t>(Random(state) >> 24U);
            vertex.b = static_cast<std::uint8_t>(Random(state) >> 24U);
            vertex.a = static_cast<std::uint8_t>(Random(state) >> 24U);
        }

        // Positive/inverse collinearity and constant axes remain valid sampling
        // lines/points. Native-half UVs cross exact coincident U/V page corners.
        const int width = iteration % 2 == 0 ? 1600 : 1524;
        const int height = iteration % 2 == 0 ? 1200 : 1016;
        const int columns = (width + 253) / 254;
        const int rows = (height + 253) / 254;
        int drawnPages = 0;
        for (int row = 0; row < rows; ++row)
        {
            for (int column = 0; column < columns; ++column)
            {
                FixedTriangle output[kMaxPageTriangles];
                Fill(output);
                int count = -1;
                const Result result = MapPageTriangle(input, kNtsc, width, height, column, row, output, count);
                assert(result != Result::Invalid);
                assert(count >= 0 && count <= kMaxPageTriangles);
                AssertUntouched(output, count);
                if (result == Result::Draw)
                {
                    ++drawnPages;
                }
            }
        }

        if (iteration % 5 == 4)
        {
            assert(drawnPages <= 1);
        }
    }
}

static void BoundedPolygons()
{
    std::uint32_t state = 0x50414745U;
    int largest = 0;
    for (int iteration = 0; iteration < 20000; ++iteration)
    {
        Vertex input[3]{};
        for (Vertex & vertex : input)
        {
            vertex.x = static_cast<float>(static_cast<int>(Random(state) % 2001U) - 700);
            vertex.y = static_cast<float>(static_cast<int>(Random(state) % 1601U) - 600);
            vertex.s = static_cast<float>(Random(state) % 4097U) / 4096.0f;
            vertex.t = static_cast<float>(Random(state) % 4097U) / 4096.0f;
            vertex.r = static_cast<std::uint8_t>(Random(state) >> 24U);
            vertex.g = static_cast<std::uint8_t>(Random(state) >> 24U);
            vertex.b = static_cast<std::uint8_t>(Random(state) >> 24U);
            vertex.a = static_cast<std::uint8_t>(Random(state) >> 24U);
        }

        const int column = static_cast<int>(Random(state) % 5U);
        const int row = static_cast<int>(Random(state) % 4U);
        FixedTriangle output[kMaxPageTriangles];
        Fill(output);
        int count = -1;
        assert(MapPageTriangle(input, kNtsc, 1024, 768, column, row, output, count) != Result::Invalid);
        assert(count >= 0 && count <= kMaxPageTriangles);
        AssertUntouched(output, count);
        if (count > largest)
        {
            largest = count;
        }

        for (int i = 0; i < count; ++i)
        {
            assert(Area(output[i]) != 0);
            for (const FixedVertex & vertex : output[i].vertices)
            {
                assert(vertex.x >= 0 && vertex.x <= 8192 && vertex.y >= 0 && vertex.y <= 7168);
                assert(vertex.u >= 16 && vertex.u <= 4080 && vertex.v >= 16 && vertex.v <= 4080);
            }
        }
    }

    assert(largest >= 5);
}

} // namespace

int main()
{
    BasicMapping();
    PageSeams();
    BoundaryOwnership();
    ClippingAndWinding();
    SharedIndexedEdges();
    Rejections();
    BoundedPolygons();
    AdjacentPageRegression();
    OuterFontGutters();
    RandomAdjacentPages();
    RankOneMappings();
    return 0;
}
