// ================================================================================================
// File: script_error.h
// Brief: Bound script diagnostics and explicitly release compilation resources before fatal exit.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

namespace ps2::script
{

// Single EE thread only. Cleanup callbacks must not allocate or report another error.
class ErrorScope
{
public:
    using Cleanup = void (*)(void *);
    ErrorScope(Cleanup cleanup, void * data);
    ~ErrorScope();
    ErrorScope(const ErrorScope &) = delete;
    ErrorScope & operator=(const ErrorScope &) = delete;
    static void SetLocation(const char * file, int line);
    static bool IsActive();
    static void CleanupAll();

private:
    ErrorScope * m_previous;
    Cleanup m_cleanup;
    void * m_data;
};

// Recursive-descent calls share a bounded depth, independently of source length.
class NestingScope
{
public:
    NestingScope();
    ~NestingScope();
    NestingScope(const NestingScope &) = delete;
    NestingScope & operator=(const NestingScope &) = delete;
};

[[noreturn]] PS2_COLD_FUNC void Fail(const char * format, ...) PS2_PRINTF_FUNC(1, 2);

} // namespace ps2::script
