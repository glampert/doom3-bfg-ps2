// ================================================================================================
// File: math_tests.cpp
// Brief: Prove the EE qword copies and the VU0 macro-mode math against scalar references on target.
//        The asm in ps2/qwords.h and ps2/math/vec_mat.h has no host fallback, so this is the only
//        place a GCC upgrade or an edit to a constraint list is caught before a renderer draws
//        with it. Scalar references run on the EE FPU; VU0 results are compared within a tolerance
//        where the FMAC's rounding may differ, and exactly where the arithmetic is exact. The
//        polynomial transcendentals of ps2/math/math.h, which idMath forwards to, are compared
//        with newlib on the same FPU, so their error bounds hold under round-toward-zero.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/math_tests.h"
#include "ps2/math/math.h"
#include "ps2/math/vec_mat.h"
#include "ps2/qwords.h"
#include "ps2/system/log.h"

#include <cmath>
#include <cstring>

namespace ps2::tests
{
namespace
{

using math::Mat4;
using math::Vec3;
using math::Vec4;

bool Check(const char * name, bool passed)
{
    Log(LogLevel::Info, "[D3BFG] CHECK math/%s %s\n", name, passed ? "PASS" : "FAIL");
    return passed;
}

// Deterministic inputs: xorshift32, mapped into [-8, 8).
struct Random
{
    u32 state = 0x9E3779B9u;

    u32 Next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    float Float()
    {
        return (static_cast<float>(Next() >> 8) * (16.0f / 16777216.0f)) - 8.0f;
    }

    Vec4 Vector()
    {
        return { Float(), Float(), Float(), Float() };
    }

    Vec3 Point()
    {
        return { Float(), Float(), Float() };
    }

    Mat4 Matrix()
    {
        Mat4 m;
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                m.m[row][col] = Float();
            }
        }

        return m;
    }
};

bool Near(float actual, float expected)
{
    const float scale = math::Maxf(1.0f, math::Fabsf(expected));
    return math::Fabsf(actual - expected) <= 1.0e-4f * scale;
}

bool Near(const Vec4 & actual, const Vec4 & expected)
{
    return Near(actual.x, expected.x) && Near(actual.y, expected.y) && Near(actual.z, expected.z) && Near(actual.w, expected.w);
}

bool Same(const Vec4 & a, const Vec4 & b)
{
    return std::memcmp(&a, &b, sizeof(Vec4)) == 0;
}

Vec4 ScalarTransform(const Vec4 & v, const Mat4 & m)
{
    Vec4 r;
    for (int col = 0; col < 4; ++col)
    {
        r[col] = (v.x * m.m[0][col]) + (v.y * m.m[1][col]) + (v.z * m.m[2][col]) + (v.w * m.m[3][col]);
    }

    return r;
}

Mat4 ScalarMultiply(const Mat4 & a, const Mat4 & b)
{
    Mat4 r;
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            r.m[row][col] = (a.m[row][0] * b.m[0][col]) + (a.m[row][1] * b.m[1][col]) + (a.m[row][2] * b.m[2][col]) + (a.m[row][3] * b.m[3][col]);
        }
    }

    return r;
}

// Guarded qword buffers: a copy must write exactly its qwords and nothing either side.
struct alignas(16) GuardedQwords
{
    u64 before[2];
    u64 data[2 * 8];
    u64 after[2];
};

bool CheckQwordCopies()
{
    Random random;
    bool passed = true;
    for (int trial = 0; trial < 16; ++trial)
    {
        GuardedQwords source;
        GuardedQwords destination;
        for (u64 & word : source.data)
        {
            word = (static_cast<u64>(random.Next()) << 32) | random.Next();
        }

        std::memset(static_cast<void *>(&destination), 0xA5, sizeof(destination));
        GuardedQwords expected = destination;

        const int count = 1 + (trial % 8);
        switch (count)
        {
            case 1: CopyQwords<1>(destination.data, source.data); break;
            case 2: CopyQwords<2>(destination.data, source.data); break;
            case 3: CopyQwords<3>(destination.data, source.data); break;
            case 4: CopyQwords<4>(destination.data, source.data); break;
            case 5: CopyQwords<5>(destination.data, source.data); break;
            case 6: CopyQwords<6>(destination.data, source.data); break;
            case 7: CopyQwords<7>(destination.data, source.data); break;
            default: CopyQwords<8>(destination.data, source.data); break;
        }

        std::memcpy(expected.data, source.data, static_cast<size_t>(count) * 16u);
        passed = passed && std::memcmp(&destination, &expected, sizeof(expected)) == 0;
    }

    GuardedQwords stored;
    std::memset(static_cast<void *>(&stored), 0x5A, sizeof(stored));
    GuardedQwords expected = stored;
    StoreQword(stored.data, 0x1122334455667788ull, 0x99AABBCCDDEEFF00ull);
    StoreQword(stored.data + 2, 0x00000001u, 0x00000002u, 0x00000003u, 0x00000004u);
    expected.data[0] = 0x1122334455667788ull;
    expected.data[1] = 0x99AABBCCDDEEFF00ull;
    expected.data[2] = 0x0000000200000001ull;
    expected.data[3] = 0x0000000400000003ull;
    passed = passed && std::memcmp(&stored, &expected, sizeof(expected)) == 0;
    return passed;
}

bool CheckTransform()
{
    Random random;
    bool passed = true;
    for (int trial = 0; trial < 64; ++trial)
    {
        const Mat4 m = random.Matrix();
        const Vec4 v = random.Vector();
        const Vec3 p = random.Point();
        passed = passed && Near(math::Transform(v, m), ScalarTransform(v, m));
        passed = passed && Near(math::Transform(p, m), ScalarTransform({ p.x, p.y, p.z, 1.0f }, m));

        // Products with 1 and 0 are exact on both units.
        passed = passed && Same(math::Transform(v, math::Identity()), v);
    }

    return passed;
}

bool CheckLerp()
{
    Random random;
    bool passed = true;
    for (int trial = 0; trial < 64; ++trial)
    {
        const Vec4 a = random.Vector();
        const Vec4 b = random.Vector();
        const float t = (random.Float() + 8.0f) / 16.0f;
        const Vec4 expected = { a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z), a.w + t * (b.w - a.w) };
        passed = passed && Near(math::Lerp(a, b, t), expected);

        Vec4 aliased = a;
        math::LerpTo(aliased, aliased, b, t);
        passed = passed && Near(aliased, expected);

        const Vec3 p = random.Point();
        const Vec3 q = random.Point();
        const Vec3 packed = math::Lerp(p, q, t);
        passed = passed && Near(packed.x, p.x + t * (q.x - p.x)) && Near(packed.y, p.y + t * (q.y - p.y)) && Near(packed.z, p.z + t * (q.z - p.z));

        // t = 0 gives a exactly: (b - a) * 0 + a * 1.
        passed = passed && Same(math::Lerp(a, b, 0.0f), a);
    }

    return passed;
}

bool CheckMatrixMultiply()
{
    Random random;
    bool passed = true;
    for (int trial = 0; trial < 32; ++trial)
    {
        const Mat4 a = random.Matrix();
        const Mat4 b = random.Matrix();
        const Mat4 product = a * b;
        const Mat4 expected = ScalarMultiply(a, b);
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                passed = passed && Near(product.m[row][col], expected.m[row][col]);
            }
        }

        const Mat4 same = a * math::Identity();
        passed = passed && std::memcmp(&same, &a, sizeof(Mat4)) == 0;
    }

    // The builders agree with the VU0 multiply on a translate-then-rotate chain, and the
    // projection sends the near plane to z/w = +1 and the far plane to -1.
    const Mat4 chain = math::Translation(1.0f, 2.0f, 3.0f) * math::RotationZ(math::kHalfPI);
    const Vec4 moved = math::Transform(Vec4{ 1.0f, 0.0f, 0.0f, 1.0f }, chain);
    passed = passed && Near(moved.x, -2.0f) && Near(moved.y, 2.0f) && Near(moved.z, 3.0f) && Near(moved.w, 1.0f);

    const Mat4 projection = math::PerspectiveProjection(math::DegToRad(90.0f), 640.0f / 448.0f, 640.0f, 448.0f, 4.0f, 4096.0f);
    const Vec4 nearPoint = math::Transform(Vec4{ 0.0f, 0.0f, -4.0f, 1.0f }, projection);
    const Vec4 farPoint = math::Transform(Vec4{ 0.0f, 0.0f, -4096.0f, 1.0f }, projection);
    passed = passed && Near(nearPoint.z / nearPoint.w, 1.0f) && Near(farPoint.z / farPoint.w, -1.0f);
    return passed;
}

bool CheckBackFaceCull()
{
    Random random;
    bool passed = true;
    int judged = 0;
    for (int trial = 0; trial < 256; ++trial)
    {
        const Vec4 eye = random.Vector();
        const Vec4 v0 = random.Vector();
        const Vec4 v1 = random.Vector();
        const Vec4 v2 = random.Vector();
        const Vec3 d = math::Cross(Vec3{ v1.x - v0.x, v1.y - v0.y, v1.z - v0.z }, Vec3{ v2.x - v0.x, v2.y - v0.y, v2.z - v0.z });
        const float dot = math::Dot(Vec3{ v0.x - eye.x, v0.y - eye.y, v0.z - eye.z }, d);
        if (math::Fabsf(dot) < 1.0e-2f)
        {
            continue; // Too close to the plane for the two units to agree on the sign.
        }

        ++judged;
        passed = passed && (math::CullBackFacingTriangle(eye, v0, v1, v2) == (dot <= 0.0f));
    }

    return passed && judged >= 200;
}

// Largest |actual - expected| over the inputs, with the bound it must stay under, logged so a
// polynomial change shows its real error even when it passes.
struct ErrorBound
{
    const char * name;
    float        bound;
    float        worst = 0.0f;

    void Observe(float actual, float expected)
    {
        worst = math::Maxf(worst, math::Fabsf(actual - expected));
    }

    bool Report() const
    {
        Log(LogLevel::Info, "[D3BFG] math/transcendental %s max error %.3g (bound %.3g)\n", name, static_cast<double>(worst), static_cast<double>(bound));
        return worst <= bound;
    }
};

bool CheckTranscendentals()
{
    Random     rng;
    ErrorBound sine{ "Sinf", 5.0e-7f };
    ErrorBound cosine{ "Cosf", 5.0e-7f };
    ErrorBound tangent{ "Tanf (|x| <= 1.2)", 1.0e-5f };
    ErrorBound arcTangent{ "ATanf", 1.0e-6f };
    ErrorBound arcTangent2{ "ATan2f", 1.5e-6f };
    ErrorBound arcSine{ "ASinf", 2.0e-6f };
    ErrorBound arcCosine{ "ACosf", 2.0e-6f };

    for (int i = 0; i < 4096; ++i)
    {
        const float x = rng.Float(); // [-8, 8): about 1.3 turns either side of zero
        const float y = rng.Float();
        float       s;
        float       c;
        math::SinCosf(x, s, c);
        sine.Observe(s, sinf(x));
        cosine.Observe(c, cosf(x));
        sine.Observe(math::Sinf(x), sinf(x));
        cosine.Observe(math::Cosf(x), cosf(x));
        arcTangent.Observe(math::ATanf(x), atanf(x));
        arcTangent2.Observe(math::ATan2f(y, x), atan2f(y, x));

        const float t = x * 0.15f;
        tangent.Observe(math::Tanf(t), tanf(t));

        const float u = x * 0.125f; // [-1, 1)
        arcSine.Observe(math::ASinf(u), asinf(u));
        arcCosine.Observe(math::ACosf(u), acosf(u));
    }

    // Axes and the clamp edges, where the folds and libm's conventions must agree.
    static constexpr float kAxes[] = { 0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 1.0e-3f, -1.0e-3f, 2.0e3f, -2.0e3f };

    for (const float y : kAxes)
    {
        for (const float x : kAxes)
        {
            arcTangent2.Observe(math::ATan2f(y, x), atan2f(y, x));
        }

        arcTangent.Observe(math::ATanf(y), atanf(y));
    }

    static constexpr float kUnit[] = { -1.0f, -0.999f, -0.5f, -0.4999f, 0.0f, 0.4999f, 0.5f, 0.999f, 1.0f };

    for (const float u : kUnit)
    {
        arcSine.Observe(math::ASinf(u), asinf(u));
        arcCosine.Observe(math::ACosf(u), acosf(u));
    }

    // id's code compares axes built from zero angles exactly, so zero must give exactly 0 and 1. At
    // the float values of pi/2, pi and 2 pi the true result is that constant's rounding residue
    // (4e-8 to 9e-8): sign and magnitude must match libm's, to well under the residue itself.
    float s0;
    float c0;
    math::SinCosf(0.0f, s0, c0);
    bool exactCardinals = math::Sinf(0.0f) == 0.0f && math::Cosf(0.0f) == 1.0f && s0 == 0.0f && c0 == 1.0f;
    static constexpr float kCardinal[] = { 1.5707963267948966f, -1.5707963267948966f, 3.1415926535897932f, -3.1415926535897932f, 6.2831853071795865f };

    for (const float a : kCardinal)
    {
        float s;
        float c;
        math::SinCosf(a, s, c);
        exactCardinals = exactCardinals && math::Fabsf(math::Sinf(a) - sinf(a)) <= 1.0e-9f && math::Fabsf(math::Cosf(a) - cosf(a)) <= 1.0e-9f &&
                         math::Fabsf(s - sinf(a)) <= 1.0e-9f && math::Fabsf(c - cosf(a)) <= 1.0e-9f;
    }

    if (!exactCardinals)
    {
        Log(LogLevel::Error, "[D3BFG] math/transcendental Sinf/Cosf differ from libm at a cardinal angle\n");
    }

    bool passed = sine.Report() && exactCardinals;
    passed = cosine.Report() && passed;
    passed = tangent.Report() && passed;
    passed = arcTangent.Report() && passed;
    passed = arcTangent2.Report() && passed;
    passed = arcSine.Report() && passed;
    passed = arcCosine.Report() && passed;

    // Floor and ceil must match libm exactly: integers, halves, negative fractions, the 2^23
    // integral threshold and values past the cvt.w.s range.
    static constexpr float kRounding[] = { 0.0f,        0.5f,          -0.5f,       1.0f,        -1.0f,        2.75f,
                                           -2.75f,      1.0e-7f,       -1.0e-7f,    8388607.5f,  -8388607.5f,  8388608.0f,
                                           -8388608.0f, 16777217.0f,   -1.0e7f,     1.0e7f,      3.0e9f,       -3.0e9f,
                                           1.0e30f,     -1.0e30f,      255.9999f,   -255.9999f,  65535.5f,     -65535.5f };
    bool exact = true;

    for (const float v : kRounding)
    {
        exact = exact && (math::Floorf(v) == floorf(v)) && (math::Ceilf(v) == ceilf(v));
    }

    for (int i = 0; i < 4096; ++i)
    {
        const float v = rng.Float() * 1000.0f;
        exact = exact && (math::Floorf(v) == floorf(v)) && (math::Ceilf(v) == ceilf(v));
    }

    if (!exact)
    {
        Log(LogLevel::Error, "[D3BFG] math/transcendental Floorf/Ceilf differ from libm\n");
    }

    return passed && exact;
}

} // namespace

bool RunMathTests()
{
    bool passed = Check("qword-copies", CheckQwordCopies());
    passed = Check("vu0-transform", CheckTransform()) && passed;
    passed = Check("vu0-lerp", CheckLerp()) && passed;
    passed = Check("vu0-matrix-multiply", CheckMatrixMultiply()) && passed;
    passed = Check("vu0-backface-cull", CheckBackFaceCull()) && passed;
    passed = Check("transcendentals", CheckTranscendentals()) && passed;
    return passed;
}

} // namespace ps2::tests
