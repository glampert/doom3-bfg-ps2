// ================================================================================================
// File: vram.h
// Brief: Track a bounded page-aligned texture range without assuming GS work has completed.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstdint>

namespace ps2::vram
{

static constexpr std::uint32_t kPageBytes = 8192;
static constexpr std::uint32_t kTotalBytes = 4U * 1024U * 1024U;
static constexpr int kMaxAllocations = 64;

struct Allocation
{
    std::uint32_t byteAddress;
    std::uint32_t bytes;
    std::uint32_t generation;
    std::uint16_t slot;
};

struct Stats
{
    std::uint32_t capacityBytes;
    std::uint32_t usedBytes;
    std::uint32_t peakBytes;
    std::uint32_t allocationCount;
};

// Range and allocation sizes must be nonzero whole pages entirely within GS VRAM.
// Invalid requests fail without modifying the allocator or the output.
bool Init(std::uint32_t beginBytes, std::uint32_t budgetBytes);
bool IsInitialized();
bool Allocate(std::uint32_t bytes, Allocation & output);
bool Release(const Allocation & allocation);
Stats GetStats();

// The owner must drain and fence every queued draw/upload before releasing or resetting its range.
// Reset preserves the range and peak accounting; Shutdown forgets it. Both reject stale handles.
void Reset();
void Shutdown();

} // namespace ps2::vram
