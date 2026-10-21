// ================================================================================================
// File: vram.h
// Brief: Track a bounded page-aligned texture range without assuming GS work has completed.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstdint>

namespace ps2::renderer::gs
{

static constexpr std::uint32_t kVramPageBytes = 8192;
static constexpr std::uint32_t kVramBytes = 4U * 1024U * 1024U;
static constexpr int kMaxVramAllocations = 64;

struct VramAllocation
{
    std::uint32_t byteAddress;
    std::uint32_t bytes;
    std::uint32_t generation;
    std::uint16_t slot;
};

struct VramStats
{
    std::uint32_t capacityBytes;
    std::uint32_t usedBytes;
    std::uint32_t peakBytes;
    std::uint32_t allocationCount;
};

// Range and allocation sizes must be nonzero whole pages entirely within GS VRAM.
// Invalid requests fail without modifying the allocator or the output.
bool InitVram(std::uint32_t beginBytes, std::uint32_t budgetBytes);
bool IsVramInitialized();
bool AllocateVram(std::uint32_t bytes, VramAllocation & output);
bool ReleaseVram(const VramAllocation & allocation);
VramStats GetVramStats();

// The owner must drain and fence every queued draw/upload before releasing or resetting its range.
// ResetVram preserves the range and peak accounting; ShutdownVram forgets it. Both reject stale handles.
void ResetVram();
void ShutdownVram();

} // namespace ps2::renderer::gs
