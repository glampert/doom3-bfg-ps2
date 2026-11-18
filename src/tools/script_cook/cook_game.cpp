// ================================================================================================
// File: cook_game.cpp
// Brief: The game objects cook_game.h declares for id's script compiler (the event table from
// events.txt, the game's output and error calls), and the tagged heap idlib allocates from,
// backed by the host's allocator.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

// The standard headers come first: idlib's precompiled.h redefines printf-family names.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "idlib/precompiled.h"

#include "tools/script_cook/cook_events.h"
#include "tools/script_cook/cook_game.h"

#include "ps2/system/heap.h"

namespace
{

[[noreturn]] void FailV(const char * format, va_list arguments)
{
    char text[4096];
    idStr::vsnPrintf(text, sizeof(text), format, arguments);
    std::fprintf(stderr, "script_cook: %s\n", text);
    std::exit(1);
}

void PrintV(FILE * stream, const char * prefix, const char * format, va_list arguments)
{
    char text[4096];
    idStr::vsnPrintf(text, sizeof(text), format, arguments);
    std::fprintf(stream, "%s%s", prefix, text);
}

static std::vector<std::unique_ptr<idEventDef> > s_events;
static std::map<std::string, const idEventDef *> s_eventsByName;

} // namespace

// ------------------------------------------------------------------------------------------------
// Events
// ------------------------------------------------------------------------------------------------

idEventDef::idEventDef(const char * name, const char * format, char returnType, bool thread)
    : m_name(name), m_format(format), m_returnType(returnType), m_thread(thread)
{
}

const idEventDef * idEventDef::FindEvent(const char * name)
{
    const auto found = s_eventsByName.find(name);
    return found != s_eventsByName.end() ? found->second : nullptr;
}

namespace cook
{

bool LoadEvents(const std::string & text, std::string & error)
{
    s_events.clear();
    s_eventsByName.clear();

    size_t start = 0;
    int line = 0;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
        {
            end = text.size();
        }

        std::string row = text.substr(start, end - start);
        start = end + 1;
        ++line;

        if (!row.empty() && row.back() == '\r')
        {
            row.pop_back();
        }

        if (row.empty() || row[0] == '#')
        {
            continue;
        }

        // name format return thread, the format and return '-' when empty
        std::vector<std::string> fields;
        size_t cursor = 0;
        while (cursor < row.size())
        {
            const size_t space = row.find(' ', cursor);
            const size_t stop = space == std::string::npos ? row.size() : space;
            fields.push_back(row.substr(cursor, stop - cursor));
            cursor = stop + 1;
        }

        if (fields.size() != 4 || fields[0].empty() || fields[1].empty() || fields[2].size() != 1 ||
            (fields[3] != "0" && fields[3] != "1"))
        {
            error = "events.txt line " + std::to_string(line) + " is not 'name format return thread'";
            return false;
        }

        const std::string format = fields[1] == "-" ? std::string() : fields[1];
        if (format.size() > D_EVENT_MAXARGS)
        {
            error = "event " + fields[0] + " takes more than " + std::to_string(D_EVENT_MAXARGS) + " arguments";
            return false;
        }

        const char returnType = fields[2] == "-" ? D_EVENT_VOID : fields[2][0];
        s_events.push_back(std::make_unique<idEventDef>(fields[0].c_str(), format.c_str(), returnType, fields[3] == "1"));
        if (!s_eventsByName.emplace(fields[0], s_events.back().get()).second)
        {
            error = "events.txt lists " + fields[0] + " twice";
            return false;
        }
    }

    if (s_events.empty())
    {
        error = "events.txt lists no events";
        return false;
    }

    return true;
}

} // namespace cook

// ------------------------------------------------------------------------------------------------
// Game objects
// ------------------------------------------------------------------------------------------------

idThread::CookType idThread::Type;
idCookDisasmCVar g_disasm;
idCookGameLocal gameLocal;

void idCookGameLocal::Printf(const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    PrintV(stdout, "", format, arguments);
    va_end(arguments);
}

void idCookGameLocal::DPrintf(const char *, ...)
{
}

void idCookGameLocal::Warning(const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    PrintV(stderr, "script_cook: warning: ", format, arguments);
    va_end(arguments);
    std::fprintf(stderr, "\n");
}

void idCookGameLocal::Error(const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    FailV(format, arguments);
}

void gameError(const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    FailV(format, arguments);
}

// Saves are the game's; the cook compiles once and never saves or restores.
void idSaveGame::WriteInt(int)
{
    std::abort();
}

void idSaveGame::WriteByte(byte)
{
    std::abort();
}

void idSaveGame::WriteString(const char *)
{
    std::abort();
}

void idSaveGame::Write(const void *, int)
{
    std::abort();
}

void idSaveGame::Error(const char *, ...)
{
    std::abort();
}

void idRestoreGame::ReadInt(int &)
{
    std::abort();
}

void idRestoreGame::ReadByte(byte &)
{
    std::abort();
}

void idRestoreGame::ReadString(idStr &)
{
    std::abort();
}

void idRestoreGame::Read(void *, int)
{
    std::abort();
}

void idRestoreGame::Error(const char *, ...)
{
    std::abort();
}

// ------------------------------------------------------------------------------------------------
// The tagged heap idlib's Mem_Alloc calls, over the host allocator. The cook keeps no ledger.
// ------------------------------------------------------------------------------------------------

namespace ps2::system::heap
{

void * TryAlloc(size_t size, std::uint16_t, size_t alignment)
{
    // Zero asks for the size's natural alignment, which 16 bytes covers.
    if (alignment < 16)
    {
        alignment = 16;
    }

    // Room for the alignment and the block's own address just below the aligned pointer.
    unsigned char * const block = static_cast<unsigned char *>(std::malloc(size + alignment + sizeof(void *)));
    if (block == nullptr)
    {
        return nullptr;
    }

    const std::uintptr_t first = reinterpret_cast<std::uintptr_t>(block) + sizeof(void *);
    const std::uintptr_t aligned = (first + alignment - 1) & ~static_cast<std::uintptr_t>(alignment - 1);
    void ** const slot = reinterpret_cast<void **>(aligned - sizeof(void *));
    *slot = block;
    return reinterpret_cast<void *>(aligned);
}

void * Alloc(size_t size, std::uint16_t tag, size_t alignment)
{
    void * const memory = TryAlloc(size, tag, alignment);
    if (memory == nullptr)
    {
        Fail("out of host memory");
    }

    return memory;
}

void Free(void * pointer)
{
    if (pointer == nullptr)
    {
        return;
    }

    void * const * const slot = static_cast<void * const *>(static_cast<void *>(static_cast<unsigned char *>(pointer) - sizeof(void *)));
    std::free(*slot);
}

void Fail(const char * reason)
{
    std::fprintf(stderr, "script_cook: heap: %s\n", reason);
    std::exit(1);
}

} // namespace ps2::system::heap

// Every standard new and delete form uses the same heap: idlib's tagged new allocates from it, and
// its strings and lists free with the plain forms (Heap.h leaves them to the backend under
// ID_HOST_TEST, as the PS2's heap.cpp supplies them on the target).
void * operator new(size_t size)
{
    return ps2::system::heap::Alloc(size, ps2::system::heap::kNewTag, 0);
}

void * operator new[](size_t size)
{
    return ps2::system::heap::Alloc(size, ps2::system::heap::kNewTag, 0);
}

void * operator new(size_t size, std::align_val_t alignment)
{
    return ps2::system::heap::Alloc(size, ps2::system::heap::kNewTag, static_cast<size_t>(alignment));
}

void * operator new[](size_t size, std::align_val_t alignment)
{
    return ps2::system::heap::Alloc(size, ps2::system::heap::kNewTag, static_cast<size_t>(alignment));
}

void operator delete(void * pointer) noexcept
{
    ps2::system::heap::Free(pointer);
}

void operator delete[](void * pointer) noexcept
{
    ps2::system::heap::Free(pointer);
}

void operator delete(void * pointer, size_t) noexcept
{
    ps2::system::heap::Free(pointer);
}

void operator delete[](void * pointer, size_t) noexcept
{
    ps2::system::heap::Free(pointer);
}

void operator delete(void * pointer, std::align_val_t) noexcept
{
    ps2::system::heap::Free(pointer);
}

void operator delete[](void * pointer, std::align_val_t) noexcept
{
    ps2::system::heap::Free(pointer);
}

void operator delete(void * pointer, size_t, std::align_val_t) noexcept
{
    ps2::system::heap::Free(pointer);
}

void operator delete[](void * pointer, size_t, std::align_val_t) noexcept
{
    ps2::system::heap::Free(pointer);
}
