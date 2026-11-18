// ================================================================================================
// File: stack_trace.cpp
// Brief: EE call stack unwinding by MIPS prologue scanning (stack_trace.h), from quake2-ps2.
//        Assertion builds only.
//
//  Unwinding a frame takes two numbers: how many bytes the function took off $sp and where in that
//  frame it spilled $ra. Nothing records them at run time (the ELF that runs is stripped, there is
//  no frame pointer, and -fno-exceptions leaves no unwind tables), so both are read back out of the
//  instruction stream. From an address inside a function, the scan goes BACKWARD to the function's
//  prologue, then reads forward through it to total the frame and find the $ra spill.
//
//  ps2sdk's own unwinder (libdebug's ps2GetStackTrace, ee/debug/src/callstack.c) scans FORWARD for
//  the epilogue instead, which breaks on exactly the paths worth tracing: GCC sinks cold blocks past
//  the function's return, so from a fatal branch the scan runs off the end of the function and reads
//  the next one's prologue. In quake2-ps2 it read `addiu $sp, $sp, -2096` unsigned, a +63,440 byte
//  frame. Backward has no such failure: from anywhere in a function, cold blocks included, the first
//  NEGATIVE `addiu $sp, $sp, imm` that owns a $ra spill is its prologue; a positive one on the way is
//  its epilogue. quake2-ps2 checked this against the debug build's DWARF frame information at 11,129
//  call sites: it agreed at 11,099 and gave up cleanly at the other 30, frames opened from a
//  register (IsRegisterSpAdjust). A wrong address in a crash dump costs more than a missing one.
//
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/debug/stack_trace.h"

#if PS2_D3BFG_ASSERTS

#include "ps2/system/log.h"

#include <cstdio>

// The bounds of .text, from the linker script (config/linkfile.ld); _etext is PROVIDE'd, so it
// exists because this refers to it.
extern "C" u8 _ftext[];
extern "C" u8 _etext[];

namespace ps2::debug
{
namespace
{

// I-type instructions, matched on the opcode and both register fields; the low half is the
// immediate.
static constexpr u32 kITypeMask = 0xFFFF0000u;
static constexpr u32 kAddiuSpSp = 0x27BD0000u; // addiu $sp, $sp, imm
static constexpr u32 kSdRaSp = 0xFFBF0000u;    // sd    $ra, imm($sp)
static constexpr u32 kSwRaSp = 0xAFBF0000u;    // sw    $ra, imm($sp)

// R-type <op|rs|rd|shamt>, $rt and the function code free, against "<op> $sp, $sp, <reg>".
static constexpr u32 kSpRegOpMask = 0xFFE0FFC0u;
static constexpr u32 kSpRegOp = 0x03A0E800u;

// How far back the prologue may be. The bound makes a shape that cannot be decoded end the walk
// instead of scanning to the bottom of .text.
static constexpr int kMaxPrologueScan = 8192;

// How far past its first instruction a prologue may run: GCC spills $ra within the first handful.
static constexpr int kMaxPrologueBodyScan = 64;

int ImmediateOf(u32 instruction)
{
    return static_cast<s16>(static_cast<u16>(instruction & 0xFFFFu));
}

const u32 * TextBegin()
{
    return static_cast<const u32 *>(static_cast<const void *>(_ftext));
}

const u32 * TextEnd()
{
    return static_cast<const u32 *>(static_cast<const void *>(_etext));
}

// Anything that can end the straight-line run read as a prologue: past one, the code is no longer
// known to be on the path that ran.
bool IsBranchOrJump(u32 instruction)
{
    switch (instruction >> 26)
    {
        case 0x00u: // SPECIAL: jr and jalr only
        {
            const u32 funct = instruction & 0x3Fu;
            return funct == 0x08u || funct == 0x09u;
        }

        case 0x01u: // REGIMM: bltz, bgez, bltzal, bgezal
        case 0x02u: // j
        case 0x03u: // jal
        case 0x04u: // beq
        case 0x05u: // bne
        case 0x06u: // blez
        case 0x07u: // bgtz
        case 0x14u: // beql
        case 0x15u: // bnel
        case 0x16u: // blezl
        case 0x17u: // bgtzl
            return true;

        default:
            return false;
    }
}

// addu/subu/daddu/dsubu $sp, $sp, <reg>: how GCC opens a frame too big for addiu steps, the size
// loaded into a register first. Recovering it would mean tracking the register back through its
// lui/ori; the unwinder stops there and says so.
bool IsRegisterSpAdjust(u32 instruction)
{
    if ((instruction & kSpRegOpMask) != kSpRegOp)
    {
        return false;
    }

    const u32 funct = instruction & 0x3Fu;
    return funct == 0x21u || funct == 0x23u || funct == 0x2Du || funct == 0x2Fu;
}

struct FrameInfo
{
    int frameSize; // bytes the prologue took off $sp
    int raOffset;  // where in the frame $ra went, from $sp
};

enum class PrologueScan
{
    NotAPrologue, // some other adjustment: keep looking
    Undecodable,  // a real prologue that cannot be measured: stop
    Decoded
};

// Reads the straight-line prologue starting at `prologue` and works out its frame. Doubles as the
// test of whether it starts one at all: a function stores $ra within a few instructions of opening
// its frame, at an offset inside it.
PrologueScan ScanPrologue(const u32 * prologue, const u32 * textEnd, FrameInfo & outFrame)
{
    int frameSize = -ImmediateOf(*prologue);

    if (frameSize <= 0)
    {
        return PrologueScan::NotAPrologue;
    }

    int raOffset = -1;

    for (int i = 1; i < kMaxPrologueBodyScan; ++i)
    {
        const u32 * const at = prologue + i;

        if (at >= textEnd)
        {
            break;
        }

        // A branch ends the prologue, but its delay slot runs either way, and GCC fills it with
        // prologue work, the $ra spill included: read the slot, then stop.
        const bool endsPrologue = IsBranchOrJump(*at);
        const u32 * const slot = endsPrologue ? at + 1 : at;

        if (slot >= textEnd)
        {
            break;
        }

        const u32 instruction = *slot;

        if (IsRegisterSpAdjust(instruction))
        {
            return PrologueScan::Undecodable;
        }

        const u32 masked = instruction & kITypeMask;

        if (masked == kAddiuSpSp)
        {
            // addiu tops out at 32767, so a bigger frame opens in steps; each moves $sp further
            // down, so what was spilled already sits that much higher above it.
            const int more = -ImmediateOf(instruction);

            if (more <= 0)
            {
                break; // an epilogue: the prologue ended before it
            }

            frameSize += more;

            if (raOffset >= 0)
            {
                raOffset += more;
            }
        }
        else if ((masked == kSdRaSp || masked == kSwRaSp) && raOffset < 0)
        {
            raOffset = ImmediateOf(instruction);
        }

        if (endsPrologue)
        {
            break;
        }
    }

    if (raOffset < 0 || raOffset >= frameSize)
    {
        return PrologueScan::NotAPrologue;
    }

    outFrame.frameSize = frameSize;
    outFrame.raOffset = raOffset;
    return PrologueScan::Decoded;
}

// The frame layout of the function holding pc; false for "cannot tell", which stops the walk.
bool DecodeFrame(const u32 * pc, FrameInfo & outFrame)
{
    const u32 * const textBegin = TextBegin();
    const u32 * const textEnd = TextEnd();

    if (pc < textBegin || pc >= textEnd)
    {
        return false;
    }

    for (int i = 0; i < kMaxPrologueScan; ++i)
    {
        const u32 * const at = pc - i;

        if (at < textBegin)
        {
            break;
        }

        if ((*at & kITypeMask) != kAddiuSpSp)
        {
            continue;
        }

        // Not every negative adjustment opens a frame (a multi-step one's later steps look the
        // same), and a positive one is an epilogue crossed on the way back, as from a cold block
        // sunk past the return. ScanPrologue tells them apart by the $ra spill, so a rejected
        // candidate keeps the scan going.
        switch (ScanPrologue(at, textEnd, outFrame))
        {
            case PrologueScan::Decoded:
                return true;

            case PrologueScan::Undecodable:
                return false;

            case PrologueScan::NotAPrologue:
                break;
        }
    }

    // Nothing decodable in reach. A frame that never spills $ra makes no calls, so nothing above
    // it is worth reporting anyway.
    return false;
}

} // namespace

namespace detail
{

int WalkStack(u32 pc, u32 sp, u32 * outFrames, int maxFrames)
{
    if (outFrames == nullptr || maxFrames <= 0)
    {
        return 0;
    }

    const auto * framePc = reinterpret_cast<const u32 *>(pc);
    int count = 0;

    while (count < maxFrames)
    {
        FrameInfo frame = {};

        if (!DecodeFrame(framePc, frame))
        {
            break;
        }

        // $sp is 16-byte aligned and the spill offset a multiple of 4: the load is aligned.
        const auto * const raSlot = reinterpret_cast<const u32 *>(sp + static_cast<u32>(frame.raOffset));
        const u32 returnAddress = *raSlot;

        // The top of the chain: a return into the kernel (which entered crt0), or a spilled 0.
        if (returnAddress < reinterpret_cast<u32>(_ftext) + 8u || returnAddress > reinterpret_cast<u32>(_etext))
        {
            break;
        }

        // A jal leaves $ra past its delay slot: 8 bytes back is the call itself. The caller's frame
        // is decoded from the call, not the return address: a [[noreturn]] function ends on its
        // call, so the address after it is the next function's first instruction, whose frame the
        // scan would read instead (FatalErrorV returning into FatalError, 2026-11-18).
        const u32 callAddress = returnAddress - 8u;
        sp += static_cast<u32>(frame.frameSize);
        framePc = reinterpret_cast<const u32 *>(callAddress);
        outFrames[count] = callAddress;
        ++count;
    }

    return count;
}

} // namespace detail

PS2_COLD_FUNC void LogStackFrames(const u32 * frames, int count)
{
    Log(LogLevel::Info, "------------------------- STACK TRACE -------------------------\n");

    for (int i = 0; i < count; ++i)
    {
        Log(LogLevel::Info, "#%-2d 0x%08x\n", i, frames[i]);
    }

    if (count == 0)
    {
        Log(LogLevel::Info, "<unavailable: the call stack could not be unwound>\n");
    }
    else if (count == kStackTraceMaxFrames)
    {
        Log(LogLevel::Info, "... (truncated)\n");
    }

    // The ELF that runs is stripped; the symbols are in the unstripped one built beside it.
    Log(LogLevel::Info, "Resolve with: python3 src/tools/scripts/symbolize.py --elf "
                        "build/<config>/d3bfg_unstripped.elf (or the test image's) < this log\n");
    Log(LogLevel::Info, "------------------------- STACK TRACE -------------------------\n");
}

PS2_COLD_FUNC size_t FormatStackFrames(char * output, size_t capacity, const u32 * frames, int count)
{
    size_t used = 0;

    const auto append = [&](const char * text)
    {
        const int written = std::snprintf(output + used, capacity - used, "%s", text);

        if (written > 0)
        {
            const auto length = static_cast<size_t>(written);
            used += length < capacity - used ? length : capacity - used - 1;
        }
    };

    if (capacity == 0)
    {
        return 0;
    }

    output[0] = '\0';

    for (int i = 0; i < count; ++i)
    {
        char frame[16];
        const bool lineEnds = i % 5 == 4 || i == count - 1;
        std::snprintf(frame, sizeof(frame), "0x%08x%s", frames[i], lineEnds ? "\n" : " ");
        append(frame);
    }

    if (count == 0)
    {
        append("<could not unwind>\n");
    }
    else if (count == kStackTraceMaxFrames)
    {
        append("... (truncated)\n");
    }

    return used;
}

PS2_COLD_FUNC void PrintStackTrace()
{
    u32 frames[kStackTraceMaxFrames];
    const int count = CaptureStackTrace(frames, kStackTraceMaxFrames);
    LogStackFrames(frames, count);
}

} // namespace ps2::debug

#endif // PS2_D3BFG_ASSERTS
