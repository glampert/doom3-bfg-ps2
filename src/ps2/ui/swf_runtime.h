// ================================================================================================
// File: swf_runtime.h
// Brief: Bound explicitly admitted retail timeline execution independently of movie presentation.
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

class ExecutionScope final
{
public:
    explicit ExecutionScope(const char * movie);
    ~ExecutionScope();
    ExecutionScope(const ExecutionScope &) = delete;
    ExecutionScope & operator=(const ExecutionScope &) = delete;
};

class CallScope final
{
public:
    CallScope();
    ~CallScope();

private:
    bool m_active;
};

using ParentQuery = const void * (*)(const void *);

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

bool Active();
PS2_COLD_FUNC void ResetStats();
const Stats & GetStats();
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

} // namespace ps2::ui::swf
