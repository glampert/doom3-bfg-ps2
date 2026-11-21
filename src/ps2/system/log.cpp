// ================================================================================================
// File: log.cpp
// Brief: Keep stdout and termination details behind the backend logging boundary.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_EE)
    #include <kernel.h>
#endif // _EE

#if defined(_EE) && PS2_D3BFG_ASSERTS
    #include "ps2/debug/crash_report.h"
    #include "ps2/debug/log_mirror.h"
#endif // _EE && PS2_D3BFG_ASSERTS

namespace ps2
{

namespace
{

const char * Prefix(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Info:
        break;
    case LogLevel::Warning:
        return "[D3BFG] WARNING ";
    case LogLevel::Error:
        return "[D3BFG] ERROR ";
    case LogLevel::Fatal:
        return "[D3BFG] FATAL ";
    }

    return "";
}

#if defined(_EE) && PS2_D3BFG_ASSERTS
// A console run's readers of the log, the boot screen and the package's log file, take each message
// whole; a longer one is cut, still ending its line.
void Mirror(LogLevel level, const char * prefix, const char * format, va_list arguments)
{
    static char s_line[1024];
    const size_t prefixLength = std::strlen(prefix);
    std::memcpy(s_line, prefix, prefixLength + 1);
    const int written = std::vsnprintf(s_line + prefixLength, sizeof(s_line) - prefixLength, format, arguments);
    size_t length = written > 0 ? prefixLength + static_cast<size_t>(written) : prefixLength;

    if (length >= sizeof(s_line) - 1)
    {
        length = sizeof(s_line) - 2;
        s_line[length] = '\n';
        s_line[length + 1] = '\0';
    }
    else if (level != LogLevel::Info)
    {
        s_line[length] = '\n';
        s_line[length + 1] = '\0';
    }

    debug::MirrorLog(level, s_line);
}
#endif // _EE && PS2_D3BFG_ASSERTS

} // namespace

void LogV(LogLevel level, const char * format, va_list arguments)
{
#if defined(_EE) && PS2_D3BFG_ASSERTS
    va_list mirror;
    va_copy(mirror, arguments);
#endif // _EE && PS2_D3BFG_ASSERTS

    // This is the only stdout sink. Keep it independent of common, Sys_* and the tagged heap.
    const char * prefix = Prefix(level);
    std::fputs(prefix, stdout);
    std::vprintf(format, arguments);
    if (level != LogLevel::Info)
    {
        std::printf("\n");
    }
    std::fflush(stdout);

#if defined(_EE) && PS2_D3BFG_ASSERTS
    Mirror(level, prefix, format, mirror);
    va_end(mirror);
#endif // _EE && PS2_D3BFG_ASSERTS
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
