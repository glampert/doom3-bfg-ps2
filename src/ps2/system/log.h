// ================================================================================================
// File: log.h
// Brief: One synchronous diagnostic sink, usable before engine initialization and during failure.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

#include <cstdarg>

namespace ps2
{

enum class LogLevel
{
    Info,
    Warning,
    Error,
    Fatal
};

// Info preserves console fragments verbatim. Other levels add a prefix and terminating newline.
// All levels currently write to stdout and flush; no engine service or C++ allocation is required.
void Log(LogLevel level, const char * format, ...) PS2_PRINTF_FUNC(2, 3);
void LogV(LogLevel level, const char * format, va_list arguments) PS2_PRINTF_FUNC(2, 0);

// Logging at Fatal level alone does not halt. These are the shared terminating entry points.
// FatalError is declared in common.h for the assertion helpers.
[[noreturn]] PS2_COLD_FUNC void FatalErrorV(const char * format, va_list arguments) PS2_PRINTF_FUNC(1, 0);

} // namespace ps2
