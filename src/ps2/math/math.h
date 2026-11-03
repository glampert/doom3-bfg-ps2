// ================================================================================================
// File: math.h
// Brief: Scalar float helpers for hot paths: the EE FPU instructions GCC does not emit by itself
//        (abs.s, sqrt.s, min.s, max.s) and float-precision polynomial transcendentals in place of
//        newlib's calls. The EE has no NaN and returns sqrt(|x|) for a negative input, so none of
//        this needs the checks libm carries. The header includes nothing, so backend code that must
//        not include idlib can use it; under ID_PS2, idMath forwards its trig, inverse trig, floor
//        and ceil here, which is how id's code reaches these.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::math
{

constexpr float kPI     = 3.1415926535897932384626433832795f;
constexpr float kTwoPI  = 6.283185307179586476925286766559f;
constexpr float kHalfPI = 1.5707963267948966192313216916398f;

inline float Fabsf(float x)
{
    float r;
    __asm__("abs.s %0, %1" : "=f"(r) : "f"(x));
    return r;
}

inline float Sqrtf(float x)
{
    float r;
    __asm__("sqrt.s %0, %1" : "=f"(r) : "f"(x));
    return r;
}

inline float Minf(float a, float b)
{
    float r;
    __asm__("min.s %0, %1, %2" : "=f"(r) : "f"(a), "f"(b));
    return r;
}

inline float Maxf(float a, float b)
{
    float r;
    __asm__("max.s %0, %1, %2" : "=f"(r) : "f"(a), "f"(b));
    return r;
}

// 1 / sqrt(x). The EE has no rsqrt.s; sqrt.s of zero is zero and the divide saturates to FLT_MAX.
inline float RSqrtf(float x)
{
    return 1.0f / Sqrtf(x);
}

// Sine and cosine on the Cephes sinf/cosf scheme: |x| is reduced to r in [0, pi/4] by octant with a
// three-constant split of pi/4 (exact for the first 8192 radians), then a degree-7 odd sine or a
// degree-8 even cosine polynomial, picked by octant. Zero reduces to r = 0 exactly, so Sinf(0) is 0
// and Cosf(0) is 1, which id's code relies on when it compares axes built from zero angles; at the
// float values of pi/2 and pi the results carry libm's sign and magnitude (the rounding residue of
// those constants). Error is about one ulp within a few turns of zero.
inline void ReduceOctant(float ax, float & r, int & octant)
{
    int   j = static_cast<int>(ax * 1.27323954473516f); // 4 / pi, truncated
    float y = static_cast<float>(j);

    if ((j & 1) != 0)
    {
        ++j;
        y += 1.0f;
    }

    octant = j & 7;
    r      = ((ax - y * 0.78515625f) - y * 2.4187564849853515625e-4f) - y * 3.77489497744594108e-8f;
}

inline float SinPolynomial(float r)
{
    const float z = r * r;
    return ((-1.9515295891e-4f * z + 8.3321608736e-3f) * z - 1.6666654611e-1f) * z * r + r;
}

inline float CosPolynomial(float r)
{
    const float z = r * r;
    return ((2.443315711809948e-5f * z - 1.388731625493765e-3f) * z + 4.166664568298827e-2f) * z * z - 0.5f * z + 1.0f;
}

// The reduction leaves an even octant: 0 and 4 take the sine polynomial, 2 and 6 the cosine one,
// and the upper half-turn (4, 6) negates. Cosine swaps the polynomials and also negates quadrant 2.
inline float Sinf(float x)
{
    float r;
    int   octant;
    ReduceOctant(Fabsf(x), r, octant);
    float y = ((octant & 3) == 2) ? CosPolynomial(r) : SinPolynomial(r);

    if (octant > 3)
    {
        y = -y;
    }

    return (x < 0.0f) ? -y : y;
}

inline float Cosf(float x)
{
    float r;
    int   octant;
    ReduceOctant(Fabsf(x), r, octant);
    const bool  negate = (octant > 3) != ((octant & 3) == 2);
    const float y      = ((octant & 3) == 2) ? SinPolynomial(r) : CosPolynomial(r);
    return negate ? -y : y;
}

// One reduction for both: the sine and cosine polynomials of r are the pair, swapped in the odd
// quadrants.
inline void SinCosf(float x, float & s, float & c)
{
    float r;
    int   octant;
    ReduceOctant(Fabsf(x), r, octant);
    const float sp = SinPolynomial(r);
    const float cp = CosPolynomial(r);
    const bool  swap = (octant & 3) == 2;
    const float sy   = swap ? cp : sp;
    const float cy   = swap ? sp : cp;
    const bool  flip = octant > 3;

    s = ((x < 0.0f) != flip) ? -sy : sy;
    c = (flip != swap) ? -cy : cy;
}

// sin / cos: exact where both are, and the pole is where libm's tanf also loses meaning.
inline float Tanf(float x)
{
    return Sinf(x) / Cosf(x);
}

// Arc tangent on the Cephes atanf scheme: fold |x| into [0, tan(pi/8)] with one of two identities,
// then an odd degree-9 minimax polynomial. Error is about 2e-7 relative; the divides saturate on
// the EE instead of producing infinities, which the folds tolerate (1 / FLT_MAX is zero).
inline float ATanf(float x)
{
    const float ax = Fabsf(x);
    float y;
    float z;

    if (ax > 2.414213562373095f)
    {
        y = kHalfPI;
        z = -1.0f / ax;
    }
    else if (ax > 0.4142135623730950f)
    {
        y = 0.25f * kPI;
        z = (ax - 1.0f) / (ax + 1.0f);
    }
    else
    {
        y = 0.0f;
        z = ax;
    }

    const float z2 = z * z;
    y += z + z * z2 * (((8.05374449538e-2f * z2 - 1.38776856032e-1f) * z2 + 1.99777106478e-1f) * z2 - 3.33329491539e-1f);
    return (x < 0.0f) ? -y : y;
}

// Four-quadrant arc tangent with libm's conventions for the axes: atan2(0, x < 0) is pi,
// atan2(y != 0, 0) is +-pi/2 and atan2(0, 0) is 0.
inline float ATan2f(float y, float x)
{
    if (x == 0.0f)
    {
        if (y > 0.0f)
        {
            return kHalfPI;
        }

        if (y < 0.0f)
        {
            return -kHalfPI;
        }

        return 0.0f;
    }

    const float a = ATanf(y / x);

    if (x < 0.0f)
    {
        return (y >= 0.0f) ? a + kPI : a - kPI;
    }

    return a;
}

// Arc sine on the Cephes asinf scheme: |x| above 1/2 goes through asin(x) = pi/2 - 2 asin(sqrt((1 - x) / 2)),
// then a degree-11 polynomial on the reduced argument. About 2.5e-7 relative; inputs beyond [-1, 1]
// clamp to +-pi/2 (libm would return NaN, which this FPU cannot represent).
inline float ASinf(float x)
{
    const float ax = Fabsf(x);

    if (ax >= 1.0f)
    {
        return (x < 0.0f) ? -kHalfPI : kHalfPI;
    }

    const bool  folded = ax > 0.5f;
    const float z      = folded ? 0.5f * (1.0f - ax) : ax * ax;
    const float w      = folded ? Sqrtf(z) : ax;
    float       r      = w + w * z * ((((4.2163199048e-2f * z + 2.4181311049e-2f) * z + 4.5470025998e-2f) * z + 7.4953002686e-2f) * z + 1.6666752422e-1f);

    if (folded)
    {
        r = kHalfPI - 2.0f * r;
    }

    return (x < 0.0f) ? -r : r;
}

// Arc cosine through the arc sine, choosing the identity that keeps the polynomial argument small.
inline float ACosf(float x)
{
    if (x <= -1.0f)
    {
        return kPI;
    }

    if (x >= 1.0f)
    {
        return 0.0f;
    }

    if (x < -0.5f)
    {
        return kPI - 2.0f * ASinf(Sqrtf(0.5f * (1.0f + x)));
    }

    if (x > 0.5f)
    {
        return 2.0f * ASinf(Sqrtf(0.5f * (1.0f - x)));
    }

    return kHalfPI - ASinf(x);
}

// The EE has no floor.w.s / ceil.w.s, only the truncating cvt.w.s, which saturates past 2^31; any
// float of magnitude 2^23 or more is already integral, so those return unchanged.
inline float Floorf(float x)
{
    if (Fabsf(x) >= 8388608.0f)
    {
        return x;
    }

    const float t = static_cast<float>(static_cast<int>(x));
    return (t > x) ? t - 1.0f : t;
}

inline float Ceilf(float x)
{
    if (Fabsf(x) >= 8388608.0f)
    {
        return x;
    }

    const float t = static_cast<float>(static_cast<int>(x));
    return (t < x) ? t + 1.0f : t;
}

constexpr float DegToRad(float degrees)
{
    return degrees * (kPI / 180.0f);
}

constexpr float RadToDeg(float radians)
{
    return radians * (180.0f / kPI);
}

} // namespace ps2::math
