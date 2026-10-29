// ================================================================================================
// File: swf_runtime.h
// Brief: Scope admitted retail timeline execution; assertion builds also bound and trace its work.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include <cstddef>
#include <cstdint>

namespace ps2::ui::swf
{

// The original shell frame-one hierarchy has 1,855 sprites, 485 texts and nine levels.
static constexpr unsigned kMaxSprites = 2048;
static constexpr unsigned kMaxTexts = 512;
static constexpr unsigned kMaxHierarchyDepth = 16;

// Every build counts scopes, instructions, calls and commands; the rest is assertion-build data.
struct Stats
{
    std::uint32_t scopes = 0;
    std::uint32_t instructions = 0;
    std::uint32_t calls = 0;
    std::uint32_t commands = 0;
    std::uint32_t allocations = 0;
    std::uint32_t traces = 0;
    std::uint32_t peakInstructions = 0;
    std::uint32_t peakCalls = 0;
    std::uint32_t peakCommands = 0;
    std::uint32_t peakDepth = 0;
    std::uint32_t peakVisitDepth = 0;
    std::uint32_t peakStack = 0;
    std::uint32_t peakObjects = 0;
    std::uint32_t peakFunctions = 0;
    std::uint32_t peakProperties = 0;
    std::uint32_t peakStrings = 0;
    std::uint32_t peakStringBytes = 0;
};

// The executing movie and the counters, exported so release builds charge work inline.
extern const char * g_executingMovie;
extern Stats g_stats;

class ExecutionScope final
{
public:
    explicit ExecutionScope(const char * movie);
    ~ExecutionScope();
    ExecutionScope(const ExecutionScope &) = delete;
    ExecutionScope & operator=(const ExecutionScope &) = delete;
};

inline bool Active()
{
    return g_executingMovie != nullptr;
}

PS2_COLD_FUNC void ResetStats();
const Stats & GetStats();

using ParentQuery = const void * (*)(const void *);

#if PS2_D3BFG_ASSERTS
class CallScope final
{
public:
    CallScope();
    ~CallScope();

private:
    bool m_active;
};

// Protect both a script's source sprite and its receiver across nested calls with different owners.
// Strict ancestor snapshots keep seek checks independent of later property lookup changes.
class OriginScope final
{
public:
    OriginScope(const void * source, const void * receiver, ParentQuery parent);
    ~OriginScope();
    OriginScope(const OriginScope &) = delete;
    OriginScope & operator=(const OriginScope &) = delete;

private:
    bool m_active;
    std::uint32_t m_frame = 0;
};

class VisitScope final
{
public:
    VisitScope();
    ~VisitScope();

private:
    bool m_active;
};

void Require(bool condition, const char * reason);
void CheckCount(const char * kind, int count, int maximum);
void CheckStack(int count);
void CheckString(std::size_t length);
void CheckSeek(const void * target, const char * reason);
void ChargeInstruction();
void ChargeCommand();
void TrackObject(bool created);
void TrackFunction(bool created);
void TrackProperties(int change);
void TrackString(bool created, std::size_t bytes);
void Trace(const char * kind, const char * name);
#else // PS2_D3BFG_ASSERTS
// Release builds keep the counters and drop every budget, ledger, origin and trace. Retail movies
// passed the load-time preflight; assertion builds enforce the rest while they execute.
class CallScope final
{
public:
    CallScope()
    {
        g_stats.calls += Active() ? 1U : 0U;
    }
};

class OriginScope final
{
public:
    OriginScope(const void *, const void *, ParentQuery)
    {
    }
};

class VisitScope final
{
public:
    VisitScope()
    {
    }
};

inline void Require(bool, const char *)
{
}

inline void CheckCount(const char *, int, int)
{
}

inline void CheckStack(int)
{
}

inline void CheckString(std::size_t)
{
}

inline void CheckSeek(const void *, const char *)
{
}

inline void ChargeInstruction()
{
    g_stats.instructions += Active() ? 1U : 0U;
}

inline void ChargeCommand()
{
    g_stats.commands += Active() ? 1U : 0U;
}

inline void TrackObject(bool)
{
}

inline void TrackFunction(bool)
{
}

inline void TrackProperties(int)
{
}

inline void TrackString(bool, std::size_t)
{
}

inline void Trace(const char *, const char *)
{
}
#endif // PS2_D3BFG_ASSERTS

} // namespace ps2::ui::swf
