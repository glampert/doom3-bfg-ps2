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
#endif

namespace ps2
{

void LogV(LogLevel level, const char * format, va_list arguments)
{
    // This is the only stdout sink. Keep it independent of common, Sys_* and the tagged heap.
    switch (level)
    {
        case LogLevel::Info: break;
        case LogLevel::Warning: std::printf("[D3BFG] WARNING "); break;
        case LogLevel::Error: std::printf("[D3BFG] ERROR "); break;
        case LogLevel::Fatal: std::printf("[D3BFG] FATAL "); break;
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

[[noreturn]] void FatalErrorV(const char * format, va_list arguments)
{
    LogV(LogLevel::Fatal, format, arguments);
    // TODO: Add on-screen fatal/crash reporting when the emergency GS text renderer is available.
    // This path runs in ordinary thread context; an EE exception handler must defer reporting here.
#if defined(_EE)
    Exit(1);
#else
    std::abort();
#endif
}

[[noreturn]] void FatalError(const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    FatalErrorV(format, arguments);
}

} // namespace ps2
