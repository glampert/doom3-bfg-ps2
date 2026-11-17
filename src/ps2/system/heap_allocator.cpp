// ================================================================================================
// File: heap_allocator.cpp
// Brief: Arena setup, the gap between the tiers, realloc and statistics; the tiers' own paths are in
//        heap_small.cpp and heap_large.cpp.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/heap_allocator.h"

#include <cstring>

namespace ps2::system::heap
{

static const SmallPage * PageOf(const void * pointer)
{
    return reinterpret_cast<const SmallPage *>(reinterpret_cast<std::uintptr_t>(pointer) & ~static_cast<std::uintptr_t>(kPageBytes - 1));
}

PS2_COLD_FUNC bool Allocator::Init(void * begin, void * end)
{
    const std::uintptr_t first = (reinterpret_cast<std::uintptr_t>(begin) + kPageBytes - 1) & ~static_cast<std::uintptr_t>(kPageBytes - 1);
    const std::uintptr_t last = reinterpret_cast<std::uintptr_t>(end) & ~static_cast<std::uintptr_t>(kPageBytes - 1);

    if (m_base != 0 || last <= first || last - first < 4u * kPageBytes) [[unlikely]]
    {
        return false;
    }

    const std::uintptr_t bytes = last - first;
    m_base = first;
    m_bytes = bytes < kMaxArenaBytes ? static_cast<std::uint32_t>(bytes) : kMaxArenaBytes;
    m_smallLow = m_bytes;
    m_peakCommitted = 0;
    InitSmall();
    InitLarge();
    return true;
}

void Allocator::UpdatePeak()
{
    const std::uint32_t committed = m_bytes - (m_smallLow - m_largeTop) - m_large.begin;

    if (committed > m_peakCommitted)
    {
        m_peakCommitted = committed;
    }
}

size_t Allocator::UsableSize(const void * pointer) const
{
    if (IsSmall(pointer))
    {
        return kSmallClasses.bytes[PageOf(pointer)->sizeClass];
    }

    const auto offset = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer) - kBlockHeaderBytes - m_base);
    return BlockSize(offset) - kBlockHeaderBytes;
}

void * Allocator::Realloc(void * pointer, size_t size, size_t alignment, std::uint8_t tag, size_t & oldBacking,
                          size_t & newBacking)
{
    PS2_AssertMsg(alignment >= 4 && (alignment & (alignment - 1)) == 0, "heap alignment must be a power of two from 4");
    const size_t usable = UsableSize(pointer);

    if (IsSmall(pointer))
    {
        const int sizeClass = PageOf(pointer)->sizeClass;
        oldBacking = kSmallClasses.bytes[sizeClass];

        // Stays when the request maps to the same class; a smaller class takes less memory.
        if (size <= kSmallMaxBytes && alignment <= kGranule && kSmallClasses.forSize[alignment >> 3][(size + 3) >> 2] == sizeClass)
        {
            newBacking = oldBacking;

#if PS2_D3BFG_ASSERTS
            std::uint8_t * shadow = Shadow(PageOf(pointer), pointer);
            shadow[0] = tag;
            shadow[1] = static_cast<std::uint8_t>(oldBacking - size);
#endif // PS2_D3BFG_ASSERTS

            return pointer;
        }
    }
    else
    {
        oldBacking = usable + kBlockHeaderBytes;
        const bool aligned = (reinterpret_cast<std::uintptr_t>(pointer) & (alignment - 1)) == 0;

        if (size > kSmallMaxBytes && aligned && ResizeLargeInPlace(pointer, size))
        {
            newBacking = UsableSize(pointer) + kBlockHeaderBytes;

#if PS2_D3BFG_ASSERTS
            auto * block = static_cast<BlockHeader *>(static_cast<void *>(static_cast<std::uint8_t *>(pointer) - kBlockHeaderBytes));
            block->requested = static_cast<std::uint32_t>(size);
            block->tag = tag;
#endif // PS2_D3BFG_ASSERTS

            return pointer;
        }
    }

    void * moved = Alloc(size, alignment, tag, newBacking);

    if (moved == nullptr) [[unlikely]]
    {
        return nullptr;
    }

    std::memcpy(moved, pointer, usable < size ? usable : size);
    Free(pointer);
    return moved;
}

TierStats Allocator::GetTierStats() const
{
    TierStats stats{};
    stats.arenaBytes = m_bytes;
    stats.gapBytes = m_smallLow - m_largeTop;
    stats.committedBytes = m_bytes - stats.gapBytes - m_large.begin;
    stats.peakCommittedBytes = m_peakCommitted;
    stats.smallPages = m_small.pages;
    stats.smallFreePages = m_small.freePageCount;
    stats.smallBorrowedPages = m_small.borrowedPages;
    stats.smallSlotBytes = m_small.slotBytes;
    stats.largeBytes = m_largeTop - m_large.begin;
    stats.largeFreeBytes = m_large.freeBytes;
    stats.largestFreeBlock = LargestFreeBlock();
    stats.metadataBytes = m_small.metadataBytes;
    return stats;
}

#if PS2_D3BFG_ASSERTS

void Allocator::GetTracking(const void * pointer, std::uint8_t & tag, size_t & requested) const
{
    if (!Owns(pointer)) [[unlikely]]
    {
        FatalError("heap: %p is not in the arena", pointer);
    }

    if (IsSmall(pointer))
    {
        const std::uint8_t * shadow = Shadow(PageOf(pointer), pointer);

        if (shadow[1] == kFreeSlack) [[unlikely]]
        {
            FatalError("heap: %p is a free slot", pointer);
        }

        tag = shadow[0];
        requested = kSmallClasses.bytes[PageOf(pointer)->sizeClass] - shadow[1];
        return;
    }

    const auto * block = static_cast<const BlockHeader *>(static_cast<const void *>(static_cast<const std::uint8_t *>(pointer) - kBlockHeaderBytes));

    if (block->magic != kBlockMagic || (block->sizeFlags & kBlockFree) != 0) [[unlikely]]
    {
        FatalError("heap: %p is not a live allocation", pointer);
    }

    tag = static_cast<std::uint8_t>(block->tag);
    requested = block->requested;
}

#endif // PS2_D3BFG_ASSERTS

} // namespace ps2::system::heap
