// ================================================================================================
// File: vram.h
// Brief: Track a bounded page-aligned texture range without assuming GS work has completed.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include <cstdint>

namespace ps2::renderer::gs
{

static constexpr std::uint32_t kVramPageBytes = 8192;
static constexpr std::uint32_t kVramBytes = 4U * 1024U * 1024U;
static constexpr int kMaxVramAllocations = 64;

// Range sizes must be nonzero whole pages entirely within GS VRAM; invalid ranges fail InitVram.
// Allocations are nonzero whole pages (asserted); one that does not fit fails without side effects.
PS2_COLD_FUNC bool InitVram(std::uint32_t beginBytes, std::uint32_t budgetBytes);
PS2_COLD_FUNC void ShutdownVram();
bool IsVramInitialized();

struct VramAllocation
{
    std::uint32_t byteAddress;
    std::uint32_t bytes;
    std::uint32_t generation;
    std::uint16_t slot;
};

bool AllocateVram(std::uint32_t bytes, VramAllocation & output);

// The allocation must be live and unchanged (asserted).
void ReleaseVram(const VramAllocation & allocation);

// The owner must drain and fence every queued draw/upload before releasing or resetting its range.
// ResetVram preserves the range and peak accounting; ShutdownVram forgets it. Both invalidate every handle.
void ResetVram();

struct VramStats
{
    std::uint32_t capacityBytes;
    std::uint32_t usedBytes;
    std::uint32_t peakBytes;
    std::uint32_t allocationCount;
};

const VramStats & GetVramStats();

} // namespace ps2::renderer::gs
