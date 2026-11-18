// ================================================================================================
// File: common.h
// Brief: Shared backend assertions and array helpers, independent of engine initialization.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
    // Rarely called functions: Init/shutdown/error handlers - optimize for size aggressively (Oz), never inline.
    // Clang (the host test compiler) has no optimize attribute.
    #if defined(__clang__)
        #define PS2_COLD_FUNC __attribute__((cold, noinline))
    #else // __clang__
        #define PS2_COLD_FUNC __attribute__((cold, noinline, optimize("Oz")))
    #endif // __clang__
    #define PS2_PRINTF_FUNC(fmtIndex, varIndex) __attribute__((format(printf, fmtIndex, varIndex)))
    // Hot helpers that must vanish into their caller even at -Os: the qword copies, whose asm
    // only pays off when the loads and stores schedule into the caller's loop.
    #define PS2_ALWAYS_INLINE inline __attribute__((always_inline))
#else // __GNUC__ || __clang__
    #define PS2_COLD_FUNC
    #define PS2_PRINTF_FUNC(fmtIndex, varIndex)
    #define PS2_ALWAYS_INLINE inline
#endif // __GNUC__ || __clang__

namespace ps2
{

// Assertions and heap failure can report errors before engine initialization.
[[noreturn]] PS2_COLD_FUNC void FatalError(const char * format, ...) PS2_PRINTF_FUNC(1, 2);

template<typename T, size_t N>
constexpr int ArrayLength(const T (&)[N])
{
    static_assert(N <= INT_MAX);
    return static_cast<int>(N);
}

// Leading zero bits of a nonzero value. __builtin_clz is a libgcc call on the EE. plzcw counts the
// bits after the first one that equal it, so a value below 2^31 has plzcw + 1 leading zeros; it
// writes the upper word's count above bit 31 too, which sll clears back to a sign-extended word.
inline int CountLeadingZeros(std::uint32_t value)
{
#if defined(_EE)
    if (static_cast<std::int32_t>(value) < 0)
    {
        return 0;
    }

    std::uint32_t count;
    asm("plzcw %0, %1\n\tsll %0, %0, 0" : "=r"(count) : "r"(value));
    return static_cast<int>(count) + 1;
#else // _EE
    return std::countl_zero(value);
#endif // _EE
}

// The index of the lowest set bit of a nonzero value.
inline int CountTrailingZeros(std::uint32_t value)
{
    return 31 - CountLeadingZeros(value & (0u - value));
}

} // namespace ps2

// Conditions run exactly once when enabled; neither condition nor message runs when disabled.
#if PS2_D3BFG_ASSERTS
    #define PS2_Assert(cond)                                                             \
        do                                                                               \
        {                                                                                \
            if (!(cond)) [[unlikely]]                                                    \
            {                                                                            \
                ps2::FatalError("Assert Failed: %s (%s:%d)", #cond, __FILE__, __LINE__); \
            }                                                                            \
        } while (0)
    #define PS2_AssertMsg(cond, message)                                                                \
        do                                                                                              \
        {                                                                                               \
            if (!(cond)) [[unlikely]]                                                                   \
            {                                                                                           \
                ps2::FatalError("Assert Failed: %s: %s (%s:%d)", #cond, (message), __FILE__, __LINE__); \
            }                                                                                           \
        } while (0)
#else // PS2_D3BFG_ASSERTS
    #define PS2_Assert(cond)             (void)sizeof(cond)
    #define PS2_AssertMsg(cond, message) (void)sizeof(cond)
#endif // PS2_D3BFG_ASSERTS
