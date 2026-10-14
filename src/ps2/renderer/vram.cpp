// ================================================================================================
// File: vram.cpp
// Brief: First-fit whole GS pages with fixed metadata and generation-checked releases.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/vram.h"

#include <limits>

namespace ps2::vram
{
namespace
{

static constexpr std::uint32_t kMaxPages = kTotalBytes / kPageBytes;
static std::uint8_t s_pages[kMaxPages]{};
static Allocation s_allocations[kMaxAllocations]{};
static std::uint32_t s_beginBytes = 0;
static std::uint32_t s_pageCount = 0;
static std::uint32_t s_nextGeneration = 1;
static Stats s_stats{};
static bool s_initialized = false;

} // namespace

bool Init(std::uint32_t beginBytes, std::uint32_t budgetBytes)
{
    if (s_initialized || (beginBytes >= kTotalBytes) || (budgetBytes == 0) ||
        ((beginBytes % kPageBytes) != 0) || ((budgetBytes % kPageBytes) != 0) ||
        (budgetBytes > (kTotalBytes - beginBytes)))
    {
        return false;
    }
    s_beginBytes = beginBytes;
    s_pageCount = budgetBytes / kPageBytes;
    s_stats = { budgetBytes, 0, 0, 0 };
    s_initialized = true;
    Reset();
    return true;
}

bool IsInitialized()
{
    return s_initialized;
}

bool Allocate(std::uint32_t bytes, Allocation & output)
{
    if (!s_initialized || (bytes == 0) || ((bytes % kPageBytes) != 0) ||
        (bytes > (s_stats.capacityBytes - s_stats.usedBytes)) ||
        (s_nextGeneration == std::numeric_limits<std::uint32_t>::max()))
    {
        return false;
    }

    int slot = -1;
    for (int i = 0; i < kMaxAllocations; ++i)
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

    const std::uint32_t pages = bytes / kPageBytes;
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
        const Allocation allocation{
            s_beginBytes + (firstPage * kPageBytes), bytes,
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

bool Release(const Allocation & allocation)
{
    if (!s_initialized || (allocation.slot >= kMaxAllocations) || (allocation.bytes == 0))
    {
        return false;
    }
    const Allocation & current = s_allocations[allocation.slot];
    if ((allocation.byteAddress != current.byteAddress) || (allocation.bytes != current.bytes) ||
        (allocation.generation != current.generation))
    {
        return false;
    }

    const std::uint32_t firstPage = (current.byteAddress - s_beginBytes) / kPageBytes;
    const std::uint32_t lastPage = firstPage + (current.bytes / kPageBytes);
    for (std::uint32_t page = firstPage; page < lastPage; ++page)
    {
        s_pages[page] = 0;
    }
    s_stats.usedBytes -= current.bytes;
    --s_stats.allocationCount;
    s_allocations[allocation.slot] = {};
    return true;
}

Stats GetStats()
{
    return s_stats;
}

void Reset()
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

void Shutdown()
{
    Reset();
    s_beginBytes = 0;
    s_pageCount = 0;
    s_stats = {};
    s_initialized = false;
}

} // namespace ps2::vram
