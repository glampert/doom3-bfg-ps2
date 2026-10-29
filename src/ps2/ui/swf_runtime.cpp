// ================================================================================================
// File: swf_runtime.cpp
// Brief: Enforce retail script work, recursion, live allocation and diagnostic bounds in release too.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/swf_runtime.h"
#include "ps2/system/log.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace ps2::ui::swf
{
namespace
{

static const char * s_movie = nullptr;
static Stats s_stats;
static std::uint32_t s_instructions = 0;
static std::uint32_t s_calls = 0;
static std::uint32_t s_commands = 0;
static std::uint32_t s_allocations = 0;
static std::uint32_t s_depth = 0;
static std::uint32_t s_visitDepth = 0;
static std::uint32_t s_originDepth = 0;

struct OriginFrame
{
    const void * ancestors[2 * kMaxHierarchyDepth] = {};
    unsigned count = 0;
};

static OriginFrame s_origins[32];
static std::uint32_t s_objects = 0;
static std::uint32_t s_functions = 0;
static std::uint32_t s_properties = 0;
static std::uint32_t s_strings = 0;
static std::uint32_t s_stringBytes = 0;
static std::uint32_t s_traceHashes[512] = {};

void Increment(std::uint32_t & value)
{
    if (value == std::numeric_limits<std::uint32_t>::max())
    {
        FatalError("retail SWF runtime counter overflow");
    }

    ++value;
}

void Allocation()
{
    if (s_movie != nullptr)
    {
        Require(s_allocations < 32768, "allocation work budget");
        ++s_allocations;
        Increment(s_stats.allocations);
    }
}

void Live(std::uint32_t & count, bool created, std::uint32_t limit, std::uint32_t & peak)
{
    if (created)
    {
        Require(count < limit, "live allocation budget");
        Increment(count);
        Allocation();
    }
    else
    {
        if (count == 0)
        {
            FatalError("retail SWF runtime allocation ledger underflow");
        }

        --count;
    }

    if (s_movie != nullptr)
    {
        peak = std::max(peak, count);
    }
}

} // namespace

ExecutionScope::ExecutionScope(const char * movie)
{
    if (s_movie != nullptr || movie == nullptr || movie[0] == '\0')
    {
        FatalError("invalid retail SWF execution ownership");
    }

    s_movie = movie;
    s_instructions = s_calls = s_commands = s_allocations = 0;
    s_depth = s_visitDepth = s_originDepth = 0;
    Increment(s_stats.scopes);
}

ExecutionScope::~ExecutionScope()
{
    Require(s_depth == 0 && s_visitDepth == 0 && s_originDepth == 0, "unbalanced execution scope");
    s_stats.peakInstructions = std::max(s_stats.peakInstructions, s_instructions);
    s_stats.peakCalls = std::max(s_stats.peakCalls, s_calls);
    s_stats.peakCommands = std::max(s_stats.peakCommands, s_commands);
    s_movie = nullptr;
}

CallScope::CallScope() : m_active(Active())
{
    if (m_active)
    {
        Require(s_depth < 32 && s_calls < 4096, "script call budget");
        ++s_depth;
        ++s_calls;
        Increment(s_stats.calls);
        s_stats.peakDepth = std::max(s_stats.peakDepth, s_depth);
    }
}

CallScope::~CallScope()
{
    if (m_active)
    {
        Require(s_depth != 0, "script call ledger underflow");
        --s_depth;
    }
}

OriginScope::OriginScope(const void * source, const void * receiver, ParentQuery parent) : m_active(Active())
{
    if (!m_active)
    {
        return;
    }

    Require(parent != nullptr && (source != nullptr || receiver != nullptr), "script origin ownership");
    Require(s_originDepth < 32, "script origin depth");
    m_frame = s_originDepth++;
    auto & frame = s_origins[m_frame];
    frame.count = 0;

    for (const void * origin : { source, receiver })
    {
        unsigned depth = 1;

        for (const void * ancestor = origin != nullptr ? parent(origin) : nullptr;
             ancestor != nullptr; ancestor = parent(ancestor))
        {
            Require(++depth <= kMaxHierarchyDepth, "script origin hierarchy");
            frame.ancestors[frame.count++] = ancestor;
        }
    }
}

OriginScope::~OriginScope()
{
    if (m_active)
    {
        Require(s_originDepth == m_frame + 1, "script origin ledger ownership");
        s_origins[m_frame].count = 0;
        --s_originDepth;
    }
}

VisitScope::VisitScope() : m_active(Active())
{
    if (m_active)
    {
        Require(s_visitDepth < 64, "sprite traversal depth");
        ++s_visitDepth;
        s_stats.peakVisitDepth = std::max(s_stats.peakVisitDepth, s_visitDepth);
    }
}

VisitScope::~VisitScope()
{
    if (m_active)
    {
        Require(s_visitDepth != 0, "sprite traversal ledger underflow");
        --s_visitDepth;
    }
}

bool Active()
{
    return s_movie != nullptr;
}

void ResetStats()
{
    if (Active())
    {
        FatalError("cannot reset active retail SWF execution");
    }

    s_stats = {};
    std::memset(s_traceHashes, 0, sizeof(s_traceHashes));
}

Stats GetStats()
{
    return s_stats;
}

void Require(bool condition, const char * reason)
{
    if (Active() && !condition)
    {
        FatalError("retail SWF execution rejected (%s): %s", s_movie, reason);
    }
}

void CheckCount(const char * kind, int count, int maximum)
{
    Require(count >= 0 && count <= maximum, kind);
}

void CheckStack(int count)
{
    CheckCount("script operand stack", count, 4096);

    if (Active())
    {
        s_stats.peakStack = std::max(s_stats.peakStack, static_cast<std::uint32_t>(count));
    }
}

void CheckString(std::size_t length)
{
    Require(length <= 4096, "script string length");
}

void CheckSeek(const void * target, const char * reason)
{
    if (!Active())
    {
        return;
    }

    Require(target != nullptr, "timeline seek target");

    for (std::uint32_t index = 0; index < s_originDepth; ++index)
    {
        const auto & frame = s_origins[index];

        for (unsigned ancestor = 0; ancestor < frame.count; ++ancestor)
        {
            Require(frame.ancestors[ancestor] != target, reason);
        }
    }
}

void ChargeInstruction()
{
    if (Active())
    {
        Require(s_instructions < 65536, "instruction budget");
        ++s_instructions;
        Increment(s_stats.instructions);
    }
}

void ChargeCommand()
{
    if (Active())
    {
        Require(s_commands < 32768, "timeline command budget");
        ++s_commands;
        Increment(s_stats.commands);
    }
}

void TrackObject(bool created)
{
    Live(s_objects, created, 8192, s_stats.peakObjects);
}

void TrackFunction(bool created)
{
    Live(s_functions, created, 8192, s_stats.peakFunctions);
}

void TrackProperties(int change)
{
    if (change >= 0)
    {
        Require(static_cast<std::uint32_t>(change) <= 65536 && s_properties <= 65536 - static_cast<std::uint32_t>(change), "live property budget");
        s_properties += static_cast<std::uint32_t>(change);
    }
    else
    {
        const auto count = static_cast<std::uint32_t>(-change);

        if (count > s_properties)
        {
            FatalError("retail SWF property ledger underflow");
        }

        s_properties -= count;
    }

    if (Active())
    {
        s_stats.peakProperties = std::max(s_stats.peakProperties, s_properties);
    }
}

void TrackString(bool created, std::size_t bytes)
{
    if (created)
    {
        CheckString(bytes);
        Require(bytes <= 2U * 1024U * 1024U && s_stringBytes <= 2U * 1024U * 1024U - bytes, "live string byte budget");

        if (bytes > std::numeric_limits<std::uint32_t>::max() - s_stringBytes)
        {
            FatalError("retail SWF string ledger overflow");
        }

        s_stringBytes += static_cast<std::uint32_t>(bytes);
    }
    else
    {
        if (bytes > s_stringBytes)
        {
            FatalError("retail SWF string ledger underflow");
        }

        s_stringBytes -= static_cast<std::uint32_t>(bytes);
    }

    Live(s_strings, created, 16384, s_stats.peakStrings);

    if (Active())
    {
        s_stats.peakStringBytes = std::max(s_stats.peakStringBytes, s_stringBytes);
    }
}

void Trace(const char * kind, const char * name)
{
    if (!Active())
    {
        return;
    }

    Require(kind != nullptr && name != nullptr, "trace identity");
    CheckString(std::strlen(name));
    std::uint32_t hash = 2166136261U;

    for (const char * part : { kind, name })
    {
        for (const auto * c = reinterpret_cast<const unsigned char *>(part); *c != 0; ++c)
        {
            hash = (hash ^ *c) * 16777619U;
        }

        hash = (hash ^ 0xffU) * 16777619U;
    }

    hash = hash == 0 ? 1 : hash;

    for (std::uint32_t index = 0; index < s_stats.traces; ++index)
    {
        if (s_traceHashes[index] == hash)
        {
            return;
        }
    }

    Require(s_stats.traces < 512, "unique dependency trace budget");
    s_traceHashes[s_stats.traces++] = hash;
    char label[161] = {};
    std::size_t index = 0;

    for (; name[index] != '\0' && index + 1 < sizeof(label); ++index)
    {
        const auto c = static_cast<unsigned char>(name[index]);
        label[index] = c >= 32 && c <= 126 ? name[index] : '?';
    }

    Log(LogLevel::Info, "SWF dependency movie=%s kind=%s name=%s%s\n", s_movie, kind, label, name[index] != '\0' ? "..." : "");
}

} // namespace ps2::ui::swf
