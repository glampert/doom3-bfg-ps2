// ================================================================================================
// File: log.cpp
// Brief: Keep stdout and termination details behind the backend logging boundary.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/log.h"

#include <cstdio>
#include <cstdlib>

#if defined(_EE)
    #include <kernel.h>
#endif // _EE

#if defined(_EE) && PS2_D3BFG_ASSERTS
    #include "ps2/debug/crash_report.h"
#endif // _EE && PS2_D3BFG_ASSERTS

namespace ps2
{

void LogV(LogLevel level, const char * format, va_list arguments)
{
    // This is the only stdout sink. Keep it independent of common, Sys_* and the tagged heap.
    switch (level)
    {
    case LogLevel::Info:
        break;
    case LogLevel::Warning:
        std::printf("[D3BFG] WARNING ");
        break;
    case LogLevel::Error:
        std::printf("[D3BFG] ERROR ");
        break;
    case LogLevel::Fatal:
        std::printf("[D3BFG] FATAL ");
        break;
    }
    std::vprintf(format, arguments);
    if (level != LogLevel::Info)
    {
        std::printf("\n");
    }
    std::fflush(stdout);
}

void Log(LogLevel level, const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    LogV(level, format, arguments);
    va_end(arguments);
}

[[noreturn]] PS2_COLD_FUNC void FatalErrorV(const char * format, va_list arguments)
{
#if defined(_EE) && PS2_D3BFG_ASSERTS
    va_list report;
    va_copy(report, arguments);
#endif // _EE && PS2_D3BFG_ASSERTS

    LogV(LogLevel::Fatal, format, arguments);

#if defined(_EE) && PS2_D3BFG_ASSERTS
    // The call stack, then the message and the stack on the debug screen, which stays up: a console
    // has no other record. The runners stop the emulator at the FATAL line.
    debug::ReportFatalError(format, report);
#elif defined(_EE)
    // TODO: release builds have no on-screen report.
    Exit(1);
#else
    std::abort();
#endif
}

[[noreturn]] PS2_COLD_FUNC void FatalError(const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    FatalErrorV(format, arguments);
}

} // namespace ps2
