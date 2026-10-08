// ================================================================================================
// File: heap.h
// Brief: One tagged allocation boundary with exact requested-size accounting on unsized frees.
// SPDX-License-Identifier: GPL-3.0-or-later
// ================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2::heap
{

static constexpr std::uint16_t kTagCount = 256;
static constexpr std::uint16_t kNewTag = 3; // Doom's TAG_NEW; verified by the engine adapter.

struct Stats
{
    size_t requestedBytes;
    size_t backingBytes;
    size_t allocationCount;
    size_t peakRequestedBytes;
    size_t peakBackingBytes;
};

struct ArenaStats
{
    bool available;
    size_t committedBytes;
    size_t usedBytes;
    size_t freeBytes;
    size_t peakCommittedBytes;
};

// Invalid sizes, alignments or tags fail without changing the ledger.
void * TryAlloc(size_t size, std::uint16_t tag, size_t alignment = 16);
void * Alloc(size_t size, std::uint16_t tag, size_t alignment = 16);
void Free(void * pointer);
Stats GetStats(std::uint16_t tag);
Stats GetTotalStats();
// Includes untagged C/newlib allocations and allocator overhead on the EE.
ArenaStats GetArenaStats();
void ResetPeaks();
[[noreturn]] void Fail(const char * reason);

} // namespace ps2::heap
