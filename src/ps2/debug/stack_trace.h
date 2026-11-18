// ================================================================================================
// File: stack_trace.h
// Brief: EE call stack unwinding for the crash reports, by MIPS prologue scanning. Assertion
//        builds only.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include <cstddef>
#include <tamtypes.h>

#if PS2_D3BFG_ASSERTS

namespace ps2::debug
{

// Deep enough for the engine's recursive paths (portal flow, the menu stack, script calls)
// without making the caller's array matter on its stack.
static constexpr int kStackTraceMaxFrames = 32;

namespace detail
{

// Walks up from a (pc, $sp) pair of one live frame. See CaptureStackTrace.
int WalkStack(u32 pc, u32 sp, u32 * outFrames, int maxFrames);

} // namespace detail

// Writes one address per frame into outFrames, innermost first, starting with this call's caller.
// Each names the call instruction, not its return address, so addr2line names the calling line.
// The walk stops, rather than guess, at a frame it cannot decode, so fewer than maxFrames can come.
//
// always_inline is load-bearing: it puts the (pc, $sp) pair in the caller's frame, which the
// unwinder can always decode, since the caller is about to call WalkStack and so opened a frame and
// spilled $ra. With a body of its own GCC turned this into a frameless leaf whose $sp belonged to a
// frame further up (quake2-ps2, -O2), which unwinds to nonsense.
inline __attribute__((always_inline)) int CaptureStackTrace(u32 * outFrames, int maxFrames)
{
    // $sp does not move for the rest of the caller (-Wvla rules out dynamic stack arrays), and the
    // label gives a pc no clone, split or rename can move away from the code it sits in.
    u32 sp = 0;
    u32 pc = 0;
    asm volatile("move %0, $sp \n\t"
                 "la   %1, 1f  \n\t"
                 "1:"
                 : "=r"(sp), "=r"(pc));

    return detail::WalkStack(pc, sp, outFrames, maxFrames);
}

// Logs the frames as "#N 0x00000000" lines between banners, the form symbolize.py reads, with how
// to resolve them.
PS2_COLD_FUNC void LogStackFrames(const u32 * frames, int count);

// Writes the frames five to a line, for the debug screen's 64 columns, into output; returns the
// characters written. Notes an empty or truncated walk.
PS2_COLD_FUNC size_t FormatStackFrames(char * output, size_t capacity, const u32 * frames, int count);

// Captures this call's stack and logs it (LogStackFrames).
PS2_COLD_FUNC void PrintStackTrace();

} // namespace ps2::debug

#endif // PS2_D3BFG_ASSERTS
