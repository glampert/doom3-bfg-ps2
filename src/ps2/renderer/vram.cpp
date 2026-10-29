// ================================================================================================
// File: vram.cpp
// Brief: First-fit whole GS pages with fixed metadata and generation-checked releases.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/vram.h"
#include <limits>

namespace ps2::renderer::gs
{
namespace
{

static constexpr std::uint32_t kMaxVramPages = kVramBytes / kVramPageBytes;
static std::uint8_t s_pages[kMaxVramPages]{};
static VramAllocation s_allocations[kMaxVramAllocations]{};
static std::uint32_t s_beginBytes = 0;
static std::uint32_t s_pageCount = 0;
static std::uint32_t s_nextGeneration = 1;
static VramStats s_stats{};
static bool s_initialized = false;

} // namespace

PS2_COLD_FUNC bool InitVram(std::uint32_t beginBytes, std::uint32_t budgetBytes)
{
    if (s_initialized || (beginBytes >= kVramBytes) || (budgetBytes == 0) ||
        ((beginBytes % kVramPageBytes) != 0) || ((budgetBytes % kVramPageBytes) != 0) ||
        (budgetBytes > (kVramBytes - beginBytes)))
    {
        return false;
    }

    s_beginBytes = beginBytes;
    s_pageCount = budgetBytes / kVramPageBytes;
    s_stats = { budgetBytes, 0, 0, 0 };
    s_initialized = true;
    ResetVram();
    return true;
}

PS2_COLD_FUNC void ShutdownVram()
{
    ResetVram();
    s_beginBytes = 0;
    s_pageCount = 0;
    s_stats = {};
    s_initialized = false;
}

bool IsVramInitialized()
{
    return s_initialized;
}

bool AllocateVram(std::uint32_t bytes, VramAllocation & output)
{
    PS2_AssertMsg(s_initialized && bytes != 0 && (bytes % kVramPageBytes) == 0, "invalid VRAM allocation request");
    PS2_AssertMsg(s_nextGeneration != std::numeric_limits<std::uint32_t>::max(), "VRAM allocation generations exhausted");

    if (bytes > (s_stats.capacityBytes - s_stats.usedBytes))
    {
        return false;
    }

    int slot = -1;
    for (int i = 0; i < kMaxVramAllocations; ++i)
    {
        if (s_allocations[i].bytes == 0)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        return false;
    }

    const std::uint32_t pages = bytes / kVramPageBytes;
    std::uint32_t run = 0;
    for (std::uint32_t i = 0; i < s_pageCount; ++i)
    {
        run = (s_pages[i] == 0) ? (run + 1) : 0;
        if (run < pages)
        {
            continue;
        }

        const std::uint32_t firstPage = (i + 1) - pages;
        for (std::uint32_t page = firstPage; page <= i; ++page)
        {
            s_pages[page] = 1;
        }

        const VramAllocation allocation{
            s_beginBytes + (firstPage * kVramPageBytes), bytes,
            s_nextGeneration++, static_cast<std::uint16_t>(slot)
        };

        s_allocations[slot] = allocation;
        s_stats.usedBytes += bytes;
        if (s_stats.usedBytes > s_stats.peakBytes)
        {
            s_stats.peakBytes = s_stats.usedBytes;
        }

        ++s_stats.allocationCount;
        output = allocation;
        return true;
    }

    return false;
}

void ReleaseVram(const VramAllocation & allocation)
{
    PS2_AssertMsg(s_initialized && allocation.slot < kMaxVramAllocations && allocation.bytes != 0, "invalid VRAM release");

    const VramAllocation & current = s_allocations[allocation.slot];
    PS2_AssertMsg(allocation.byteAddress == current.byteAddress && allocation.bytes == current.bytes &&
                      allocation.generation == current.generation,
                  "texture VRAM ownership mismatch");

    const std::uint32_t firstPage = (current.byteAddress - s_beginBytes) / kVramPageBytes;
    const std::uint32_t lastPage = firstPage + (current.bytes / kVramPageBytes);

    for (std::uint32_t page = firstPage; page < lastPage; ++page)
    {
        s_pages[page] = 0;
    }

    s_stats.usedBytes -= current.bytes;
    --s_stats.allocationCount;
    s_allocations[allocation.slot] = {};
}

void ResetVram()
{
    for (auto & page : s_pages)
    {
        page = 0;
    }
    for (auto & allocation : s_allocations)
    {
        allocation = {};
    }
    s_stats.usedBytes = 0;
    s_stats.allocationCount = 0;
}

const VramStats & GetVramStats()
{
    return s_stats;
}

} // namespace ps2::renderer::gs
