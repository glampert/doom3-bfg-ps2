// ================================================================================================
// File: crash_report.cpp
// Brief: Fatal error and EE CPU exception reports (crash_report.h); the exception handler is
//        quake2-ps2's. Assertion builds only.
//
//  ps2sdk's libeedebug owns the hard part of the handler: it replaces the EE's level 1 exception
//  vectors with a stub that spills every register into an EE_RegFrame and calls a C handler per
//  cause. That handler runs at exception level (EXL set, on libeedebug's stack), where nothing that
//  talks to the IOP works: printf and every SIF RPC need interrupts. So it only copies what the
//  report needs out of the frame, points the frame's EPC and $sp at ReportCrash and returns;
//  libeedebug reloads the frame and erets, and the faulting thread resumes in ReportCrash, on a
//  stack of its own, as ordinary thread code that can log and draw.
//
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/debug/crash_report.h"

#if PS2_D3BFG_ASSERTS

#include "ps2/debug/scr_print.h"
#include "ps2/debug/stack_trace.h"
#include "ps2/system/log.h"

#include <cstdio>

#include <ee_debug.h>
#include <kernel.h>

// The bounds of our .text (config/linkfile.ld), to tell a fault in the program from one the kernel
// took for it: the two unwind from different places.
extern "C" u8 _ftext[];
extern "C" u8 _etext[];

namespace ps2::debug
{
namespace
{

// ------------------------------------------------------------------------------------------------
// The report
// ------------------------------------------------------------------------------------------------

// Built whole, then logged and drawn. Laid out for the debug screen: 64 columns, and few enough
// lines to fit its 22 rows with a full stack.
static char s_report[2048];
static size_t s_reportLength = 0;

static constexpr u32 kRed = 0xFF0000FF;

PS2_COLD_FUNC PS2_PRINTF_FUNC(1, 2) void Append(const char * format, ...)
{
    const size_t room = sizeof(s_report) - s_reportLength;

    if (room <= 1)
    {
        return;
    }

    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(&s_report[s_reportLength], room, format, args);
    va_end(args);

    if (written > 0)
    {
        const auto length = static_cast<size_t>(written);
        s_reportLength += length < room ? length : room - 1;
    }
}

PS2_COLD_FUNC void AppendFrames(const u32 * frames, int count)
{
    s_reportLength += FormatStackFrames(&s_report[s_reportLength], sizeof(s_report) - s_reportLength, frames, count);
}

// Logs the stack in symbolize.py's form, then shows the report in red and halts the thread.
[[noreturn]] PS2_COLD_FUNC void ShowAndHalt(const u32 * frames, int count)
{
    LogStackFrames(frames, count);

    ScrInit();
    ScrSetTextColor(kRed);
    ScrPrintf("***************************************************************\n");
    ScrPrintf("%s", s_report);
    ScrPrintf("Resolve the stack with symbolize.py (d3bfg_unstripped.elf).\n");
    ScrPrintf("***************************************************************\n");

    for (;;)
    {
        SleepThread();
    }
}

// ------------------------------------------------------------------------------------------------
// CPU exceptions
// ------------------------------------------------------------------------------------------------

// MIPS ExcCode values libeedebug routes to level 1 handlers; syscall and breakpoint stay the
// kernel's.
PS2_COLD_FUNC const char * CauseName(int cause)
{
    switch (cause)
    {
        case 1:
            return "TLB modified";

        case 2:
            return "TLB refill (load/fetch)";

        case 3:
            return "TLB refill (store)";

        case 4:
            return "Address error (load/fetch)";

        case 5:
            return "Address error (store)";

        case 6:
            return "Bus error (instruction fetch)";

        case 7:
            return "Bus error (data)";

        case 10:
            return "Reserved instruction";

        case 11:
            return "Coprocessor unusable";

        case 12:
            return "Arithmetic overflow";

        case 13:
            return "Trap";

        default:
            return "Unknown";
    }
}

// What the report needs, copied out of the frame: all the handler does at exception level.
struct Fault
{
    int cause;
    u32 epc;
    u32 badVAddr;
    u32 status;
    u32 ra;
    u32 sp;
    u32 fp;
    u32 gp;
    u32 a[4];
    u32 v[2];
};

// How far the report got. A fault inside ReportCrash re-enters the handler, which goes by this.
enum class Stage : u8
{
    Idle,      // no fault yet
    Reporting, // ReportCrash's first run
    Retrying,  // that faulted, most likely walking a stack the fault left unreadable: once more
               // without the unwind
    Stopped    // the retry faulted too
};

static Stage s_stage = Stage::Idle;
static Fault s_fault = {};

// Recorded at install: a fault on another thread must stop this one, or its next frame would draw
// over the report.
static int s_mainThreadId = -1;

// ReportCrash runs on this, not on the faulting thread's stack, which may be what broke.
static constexpr u32 kReportStackBytes = 16u * 1024u;
alignas(64) static u8 s_reportStack[kReportStackBytes];

// The low 32 bits of a saved 128-bit EE register, and a 32-bit write to one.
u32 Reg(const u32 (&r)[4])
{
    return r[0];
}

void SetReg(u32 (&r)[4], u32 value)
{
    r[0] = value;
    r[1] = 0;
    r[2] = 0;
    r[3] = 0;
}

bool InOurText(u32 address)
{
    return address >= reinterpret_cast<u32>(_ftext) && address < reinterpret_cast<u32>(_etext);
}

[[noreturn]] PS2_COLD_FUNC void ReportCrash()
{
    // The fault may have struck with interrupts masked; everything below needs them.
    EIntr();

    const Fault & fault = s_fault;
    const int threadId = GetThreadId();

    s_reportLength = 0;
    Append("EE CPU EXCEPTION: %s (cause %d), thread %d%s\n", CauseName(fault.cause), fault.cause, threadId,
           threadId == s_mainThreadId ? " (main)" : "");
    Append("EPC 0x%08x  BadVAddr 0x%08x  Status 0x%08x\n", fault.epc, fault.badVAddr, fault.status);
    Append("ra  0x%08x  sp 0x%08x  fp 0x%08x  gp 0x%08x\n", fault.ra, fault.sp, fault.fp, fault.gp);

    // A null pointer handed to a callee is the common shape of this fault; $a0-$a3 usually hold it.
    Append("a0-a3 0x%08x 0x%08x 0x%08x 0x%08x\n", fault.a[0], fault.a[1], fault.a[2], fault.a[3]);
    Append("v0-v1 0x%08x 0x%08x\n", fault.v[0], fault.v[1]);

    // Where to unwind from depends on where the fault landed. In our text, EPC names the faulting
    // instruction and the scan walks back from it. Outside it (a kernel routine handed a bad
    // pointer) the scan cannot read the prologues, and $ra points back into the function of ours
    // that made the call. The first frame is the address the walk starts from.
    u32 frames[kStackTraceMaxFrames];
    int count = 0;

    if (s_stage == Stage::Retrying)
    {
        Append("Walking the stack faulted as well, so it is left out.\n");
    }
    else if (InOurText(fault.epc))
    {
        frames[0] = fault.epc;
        count = 1 + detail::WalkStack(fault.epc, fault.sp, frames + 1, kStackTraceMaxFrames - 1);
        Append("Stack from EPC, innermost first:\n");
    }
    else if (InOurText(fault.ra))
    {
        frames[0] = fault.ra - 8u;
        count = 1 + detail::WalkStack(fault.ra - 8u, fault.sp, frames + 1, kStackTraceMaxFrames - 1);
        Append("EPC is outside our text: a kernel or library call faulted.\n");
        Append("Stack from $ra, innermost first:\n");
    }
    else
    {
        Append("EPC and $ra are both outside our text: a smashed stack,\n"
               "or a jump through a corrupt pointer.\n");
    }

    if (count > 0)
    {
        AppendFrames(frames, count);
    }

    Log(LogLevel::Fatal, "%s", s_report);

    // A fault on another thread leaves the main one running, and its next frame would draw over the
    // report. Stopped only after the log, since it may hold a lock the log needs.
    if (s_mainThreadId >= 0 && threadId != s_mainThreadId)
    {
        SuspendThread(s_mainThreadId);
    }

    ShowAndHalt(frames, count);
}

int OnException(EE_RegFrame * frame)
{
    switch (s_stage)
    {
        case Stage::Idle:
            s_fault.cause = static_cast<int>((frame->cause >> 2) & 0x1F);
            s_fault.epc = frame->epc;
            s_fault.badVAddr = frame->badvaddr;
            s_fault.status = frame->status;
            s_fault.ra = Reg(frame->ra);
            s_fault.sp = Reg(frame->sp);
            s_fault.fp = Reg(frame->fp);
            s_fault.gp = Reg(frame->gp);
            s_fault.a[0] = Reg(frame->a0);
            s_fault.a[1] = Reg(frame->a1);
            s_fault.a[2] = Reg(frame->a2);
            s_fault.a[3] = Reg(frame->a3);
            s_fault.v[0] = Reg(frame->v0);
            s_fault.v[1] = Reg(frame->v1);
            s_stage = Stage::Reporting;
            break;

        case Stage::Reporting:
            s_stage = Stage::Retrying; // s_fault keeps the first fault
            break;

        case Stage::Retrying:
        case Stage::Stopped:
            // Nothing is safe to try any more, and returning would resume the fault.
            s_stage = Stage::Stopped;

            for (;;)
            {
            }
    }

    // libeedebug reloads every register from the frame and erets to its EPC: the faulting thread
    // resumes in ReportCrash, on the report's stack.
    frame->epc = reinterpret_cast<u32>(&ReportCrash);
    SetReg(frame->sp, reinterpret_cast<u32>(s_reportStack + kReportStackBytes));
    SetReg(frame->ra, 0);
    return 0;
}

// Set by the first fatal report: a fatal error while drawing one only halts.
static bool s_fatalReported = false;

} // namespace

PS2_COLD_FUNC void InstallExceptionHandlers()
{
    s_mainThreadId = GetThreadId();

    // Level 1 only: level 2 is the debug/counter vector of hardware breakpoints, which a real
    // debugger wants left alone.
    if (ee_dbg_install(1) < 0)
    {
        Log(LogLevel::Warning, "[D3BFG] DEBUG EE exception handlers could not be installed");
        return;
    }

    // TLB refill (1-3), address and bus errors (4-7) and the instruction faults (10-13); syscall (8)
    // and breakpoint (9) belong to the kernel and a debugger.
    for (int cause = 1; cause <= 13; ++cause)
    {
        if (cause != 8 && cause != 9)
        {
            ee_dbg_set_level1_handler(cause, OnException);
        }
    }

    Log(LogLevel::Info, "[D3BFG] DEBUG EE exception handlers installed\n");
}

[[noreturn]] PS2_COLD_FUNC void ReportFatalError(const char * format, va_list arguments)
{
    if (s_fatalReported)
    {
        for (;;)
        {
            SleepThread();
        }
    }

    s_fatalReported = true;

    // From here: the first frame is the call into FatalErrorV.
    u32 frames[kStackTraceMaxFrames];
    const int count = CaptureStackTrace(frames, kStackTraceMaxFrames);

    s_reportLength = 0;
    Append("FATAL ERROR:\n");
    const size_t room = sizeof(s_report) - s_reportLength;
    const int written = std::vsnprintf(&s_report[s_reportLength], room, format, arguments);

    if (written > 0)
    {
        const auto length = static_cast<size_t>(written);
        s_reportLength += length < room ? length : room - 1;
    }

    Append("\nStack, innermost first:\n");
    AppendFrames(frames, count);
    ShowAndHalt(frames, count);
}

} // namespace ps2::debug

#endif // PS2_D3BFG_ASSERTS
