// ================================================================================================
// File: common.h
// Brief: Shared backend assertions and array helpers, independent of engine initialization.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <climits>
#include <cstddef>

#if defined(__GNUC__) || defined(__clang__)
    #define PS2_COLD_FUNC __attribute__((cold))
    #define PS2_PRINTF_FUNC(fmtIndex, varIndex) __attribute__((format(printf, fmtIndex, varIndex)))
#else
    #define PS2_COLD_FUNC
    #define PS2_PRINTF_FUNC(fmtIndex, varIndex)
#endif

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

} // namespace ps2

// Conditions run exactly once when enabled; neither condition nor message runs when disabled.
#if PS2_D3BFG_ASSERTS
    #define PS2_Assert(cond) \
        do { \
            if (!(cond)) [[unlikely]] \
            { \
                ps2::FatalError("Assert Failed: %s (%s:%d)", #cond, __FILE__, __LINE__); \
            } \
        } while (0)
    #define PS2_AssertMsg(cond, message) \
        do { \
            if (!(cond)) [[unlikely]] \
            { \
                ps2::FatalError("Assert Failed: %s: %s (%s:%d)", #cond, (message), __FILE__, __LINE__); \
            } \
        } while (0)
#else
    #define PS2_Assert(cond) (void)sizeof(cond)
    #define PS2_AssertMsg(cond, message) (void)sizeof(cond)
#endif
