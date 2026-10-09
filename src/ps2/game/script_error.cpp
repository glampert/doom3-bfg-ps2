// ================================================================================================
// File: script_error.cpp
// Brief: Script failures retain their source location and clean owned resources without unwinding.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/game/script_error.h"

#include <cstdarg>
#include <cstdio>

namespace ps2::script
{
namespace
{
static ErrorScope * s_scope = nullptr;
static char s_file[512] = {};
static int s_line = 0;
static int s_depth = 0;
static constexpr int kMaxDepth = 64;
}

ErrorScope::ErrorScope(Cleanup cleanup, void * data)
    : m_previous(s_scope), m_cleanup(cleanup), m_data(data)
{
    PS2_Assert(cleanup != nullptr);
    if (s_scope == nullptr)
    {
        s_file[0] = '\0';
        s_line = 0;
        s_depth = 0;
    }
    s_scope = this;
}

ErrorScope::~ErrorScope()
{
    PS2_Assert(s_scope == this);
    s_scope = m_previous;
}

void ErrorScope::SetLocation(const char * file, int line)
{
    std::snprintf(s_file, sizeof(s_file), "%s", file != nullptr ? file : "<script>");
    s_line = line;
}

bool ErrorScope::IsActive() { return s_scope != nullptr; }

void ErrorScope::CleanupAll()
{
    while (s_scope != nullptr)
    {
        ErrorScope * current = s_scope;
        s_scope = current->m_previous;
        current->m_cleanup(current->m_data);
    }
}

NestingScope::NestingScope()
{
    if (s_depth >= kMaxDepth)
    {
        Fail("compiler nesting exceeds %d calls", kMaxDepth);
    }
    ++s_depth;
}

NestingScope::~NestingScope() { --s_depth; }

void Fail(const char * format, ...)
{
    char message[1024];
    char file[sizeof(s_file)];
    const int line = s_line;
    std::snprintf(file, sizeof(file), "%s", s_file[0] != '\0' ? s_file : "<script>");
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    ErrorScope::CleanupAll();
    FatalError("script %s:%d: %s", file, line, message);
}

} // namespace ps2::script
