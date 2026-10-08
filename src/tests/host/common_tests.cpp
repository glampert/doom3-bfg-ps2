// ================================================================================================
// File: common_tests.cpp
// Brief: Exercise log forwarding and assertion/fatal behavior in isolated host processes.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/common.h"
#include "ps2/game/class_alloc.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <cstring>
#include <limits>

namespace
{

void Forward(const char * format, ...) PS2_PRINTF_FUNC(1, 2);
void Forward(const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    ps2::LogV(ps2::LogLevel::Info, format, arguments);
    va_end(arguments);
}

} // namespace

int main(int argc, char ** argv)
{
    constexpr int values[] = {1, 2, 3};
    static_assert(ps2::ArrayLength(values) == 3);
    if (argc != 2) { return 2; }
    const char * mode = argv[1];
    if (std::strcmp(mode, "log") == 0)
    {
        ps2::Log(ps2::LogLevel::Info, "fragment:");
        Forward("%s %d\n", "100%", 7);
        ps2::Log(ps2::LogLevel::Warning, "warning %s", "100%");
        ps2::Log(ps2::LogLevel::Error, "error %d", 9);
        char text[4097];
        std::memset(text, 'x', sizeof(text) - 1);
        text[sizeof(text) - 1] = '\0';
        ps2::Log(ps2::LogLevel::Info, "%s\n", text);
    }
    else if (std::strcmp(mode, "evaluation") == 0)
    {
        int conditions = 0;
        int messages = 0;
        PS2_Assert(++conditions == 1);
        PS2_AssertMsg(++conditions == 2, (++messages, "must not be evaluated"));
        ps2::Log(ps2::LogLevel::Info, "%d %d\n", conditions, messages);
    }
    else if (std::strcmp(mode, "assert") == 0)
    {
        PS2_Assert(false);
    }
    else if (std::strcmp(mode, "message") == 0)
    {
        PS2_AssertMsg(false, "message 100%");
    }
    else if (std::strcmp(mode, "fatal") == 0)
    {
        ps2::FatalError("fatal %s %d", "100%", 7);
    }
    else if (std::strcmp(mode, "heap-fail") == 0)
    {
        ps2::heap::Fail("allocation 100%");
    }
    else if (std::strcmp(mode, "class-fail") == 0)
    {
        int bytes = 0;
        int objects = 0;
        ps2::game::AllocClass(std::numeric_limits<size_t>::max(), 16, 202, bytes, objects);
    }
    else if (std::strcmp(mode, "class-underflow") == 0)
    {
        int bytes = 0;
        int objects = 0;
        void * pointer = ps2::heap::Alloc(8, 202);
        ps2::game::FreeClass(pointer, bytes, objects);
    }
    else { return 2; }
    ps2::Log(ps2::LogLevel::Info, "returned\n");
    return 0;
}
