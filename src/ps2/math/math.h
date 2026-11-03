// ================================================================================================
// File: math.h
// Brief: Scalar float helpers for the backend's hot paths, on the EE FPU instructions GCC does not
//        emit by itself. GCC keeps fabsf as integer masking under the legacy NaN encoding and wraps
//        sqrt.s in an errno check for negative inputs; neither applies to the EE, which has no NaN
//        and returns sqrt(|x|) for a negative input. id's idMath stays the engine's; this header is
//        for backend code that must not include idlib.
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

// Cosine as sin(2 pi r) with r folded into [-1/4, 1/4], where a degree 9 minimax polynomial is
// exact to float precision. The reduction measures the angle in turns: cos is even, so
// u = |x| / 2 pi, and its fraction f in [0, 1) gives cos(2 pi f) = sin(2 pi (|f - 1/2| - 1/4)).
// Error, with the EE's round-toward-zero: 6e-7 within one turn either side of zero, growing with
// |x| as the division loses the fraction (5e-6 at |x| = 50). Valid for |x| below 2^31 turns.
inline float Cosf(float x)
{
    const float u  = Fabsf(x) * 0.159154943f;
    const float r  = Fabsf((u - static_cast<float>(static_cast<int>(u))) - 0.5f) - 0.25f;
    const float r2 = r * r;
    return r * (6.28318548f + r2 * (-41.3416901f + r2 * (81.6032639f + r2 * (-76.5982056f + r2 * 39.87323f))));
}

inline float Sinf(float x)
{
    return Cosf(x - 1.57079637f);
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
