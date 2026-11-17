// ================================================================================================
// File: heap_small.cpp
// Brief: The small tier's slow paths. Pages come from the free-page list, then the gap below the
//        lowest small page, then the large tier; a released page at the frontier gives the gap back.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/heap_allocator.h"

#include <cstring>

namespace ps2::system::heap
{

PS2_COLD_FUNC void Allocator::InitSmall()
{
    m_small = SmallState{};

    for (int sizeClass = 0; sizeClass < kSmallClassCount; ++sizeClass)
    {
        m_small.partial[sizeClass] = kNoPage;
        m_small.emptyPage[sizeClass] = kNoPage;
    }

    std::memset(m_smallBitmap, 0, sizeof(m_smallBitmap));
    std::memset(m_freeBitmap, 0, sizeof(m_freeBitmap));
}

void Allocator::SetBit(std::uint32_t * bitmap, std::uint32_t index, bool set)
{
    const std::uint32_t bit = 1u << (index & 31u);

    if (set)
    {
        bitmap[index >> 5] |= bit;
    }
    else
    {
        bitmap[index >> 5] &= ~bit;
    }
}

void Allocator::PushPartial(SmallPage * page)
{
    const std::uint16_t index = PageIndex(page);
    const std::uint16_t head = m_small.partial[page->sizeClass];

    page->prev = kNoPage;
    page->next = head;

    if (head != kNoPage)
    {
        PageAt(head)->prev = index;
    }

    m_small.partial[page->sizeClass] = index;
}

void Allocator::UnlinkPartial(SmallPage * page)
{
    if (page->prev != kNoPage)
    {
        PageAt(page->prev)->next = page->next;
    }
    else
    {
        m_small.partial[page->sizeClass] = page->next;
    }

    if (page->next != kNoPage)
    {
        PageAt(page->next)->prev = page->prev;
    }
}

std::uint16_t Allocator::NewPage(int sizeClass)
{
    const std::uint32_t bytes = kSmallClasses.bytes[sizeClass];
    const std::uint32_t capacity = kSmallClasses.capacity[sizeClass];

#if PS2_D3BFG_ASSERTS
    // Only tracking builds need the shadow, so they run out slightly sooner than release builds.
    auto * shadow = static_cast<std::uint8_t *>(AllocLarge(capacity * 2u, 4));

    if (shadow == nullptr) [[unlikely]]
    {
        return kNoPage;
    }
#endif // PS2_D3BFG_ASSERTS

    SmallPage * page = nullptr;
    std::uint8_t flags = 0;

    if (m_small.freePageCount != 0)
    {
        page = TakeFreePage();
    }
    else if (m_smallLow - m_largeTop >= kPageBytes)
    {
        m_smallLow -= kPageBytes;
        page = reinterpret_cast<SmallPage *>(m_base + m_smallLow);
        UpdatePeak();
    }
    else
    {
        // The gap is spent; a page-aligned block from the large tier serves instead.
        page = static_cast<SmallPage *>(AllocLarge(kPageBytes, kPageBytes));

        if (page == nullptr) [[unlikely]]
        {
#if PS2_D3BFG_ASSERTS
            FreeLarge(shadow);
#endif // PS2_D3BFG_ASSERTS
            return kNoPage;
        }

        flags = kPageBorrowed;
        ++m_small.borrowedPages;
    }

#if PS2_D3BFG_ASSERTS
    for (std::uint32_t slot = 0; slot < capacity; ++slot)
    {
        shadow[slot * 2u] = 0;
        shadow[slot * 2u + 1u] = kFreeSlack;
    }

    page->shadow = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(shadow) - m_base);
    m_small.metadataBytes += UsableSize(shadow) + kBlockHeaderBytes;

    // Every free slot is poisoned, so the first allocation of each is checked like any reuse.
    auto * words = reinterpret_cast<std::uint32_t *>(page);

    for (std::uint32_t word = kSmallPageHeaderBytes / 4; word < kPageBytes / 4; ++word)
    {
        words[word] = kFreedPattern;
    }
#else // PS2_D3BFG_ASSERTS
    page->shadow = 0;
#endif // PS2_D3BFG_ASSERTS

    // Thread every slot onto the free list in address order; the last link is 0.
    auto * base = reinterpret_cast<std::uint8_t *>(page);
    std::uint32_t offset = kSmallPageHeaderBytes;

    for (std::uint32_t slot = 0; slot < capacity; ++slot)
    {
        const std::uint32_t next = slot + 1 < capacity ? offset + bytes : 0;
        *static_cast<std::uint16_t *>(static_cast<void *>(base + offset)) = static_cast<std::uint16_t>(next);
        offset += bytes;
    }

    page->freeHead = static_cast<std::uint16_t>(kSmallPageHeaderBytes);
    page->used = 0;
    page->sizeClass = static_cast<std::uint8_t>(sizeClass);
    page->flags = flags;
    page->reserved = 0;
    PushPartial(page);
    SetBit(m_smallBitmap, PageIndex(page), true);

    ++m_small.pages;
    m_small.slotBytes += capacity * bytes;
    return PageIndex(page);
}

// The highest free page: low ones stay free longest, so the frontier can rise past them.
SmallPage * Allocator::TakeFreePage()
{
    std::uint32_t word = ((m_bytes >> kPageShift) - 1) >> 5;

    while (m_freeBitmap[word] == 0)
    {
        --word;
    }

    const std::uint32_t index = word * 32u + static_cast<std::uint32_t>(31 - CountLeadingZeros(m_freeBitmap[word]));
    SetBit(m_freeBitmap, index, false);
    --m_small.freePageCount;
    return PageAt(static_cast<std::uint16_t>(index));
}

// A page just lost its last live slot. Each class keeps one empty page listed so that a burst of
// frees and allocations does not cycle pages; it keeps the higher one, the lower one goes. A page
// borrowed from the large tier always goes back, since it pins that tier's memory.
void Allocator::RetirePage(SmallPage * page)
{
    const int sizeClass = page->sizeClass;
    const std::uint16_t index = PageIndex(page);
    const std::uint16_t kept = m_small.emptyPage[sizeClass];

    if ((page->flags & kPageBorrowed) != 0)
    {
        ReleasePage(page);
        return;
    }

    if (kept == kNoPage)
    {
        m_small.emptyPage[sizeClass] = index;
        return;
    }

    if (index < kept)
    {
        ReleasePage(page);
        return;
    }

    m_small.emptyPage[sizeClass] = index;
    ReleasePage(PageAt(kept));
}

void Allocator::ReleasePage(SmallPage * page)
{
    PS2_AssertMsg(page->used == 0, "heap released a small page with live slots");

    // Its free slots keep it on the class's partial list until now.
    const std::uint16_t index = PageIndex(page);
    UnlinkPartial(page);
    SetBit(m_smallBitmap, index, false);
    --m_small.pages;
    m_small.slotBytes -= kSmallClasses.capacity[page->sizeClass] * kSmallClasses.bytes[page->sizeClass];

#if PS2_D3BFG_ASSERTS
    void * shadow = reinterpret_cast<void *>(m_base + page->shadow);
    m_small.metadataBytes -= UsableSize(shadow) + kBlockHeaderBytes;
    FreeLarge(shadow);
#endif // PS2_D3BFG_ASSERTS

    if ((page->flags & kPageBorrowed) != 0)
    {
        --m_small.borrowedPages;
        FreeLarge(page);
        return;
    }

    if ((static_cast<std::uint32_t>(index) << kPageShift) != m_smallLow)
    {
        SetBit(m_freeBitmap, index, true);
        ++m_small.freePageCount;
        return;
    }

    // The lowest page: the frontier rises past it and past every free page right above it.
    m_smallLow += kPageBytes;

    while (m_smallLow < m_bytes)
    {
        const std::uint32_t above = m_smallLow >> kPageShift;

        if (((m_freeBitmap[above >> 5] >> (above & 31u)) & 1u) == 0)
        {
            break;
        }

        SetBit(m_freeBitmap, above, false);
        --m_small.freePageCount;
        m_smallLow += kPageBytes;
    }
}

#if PS2_D3BFG_ASSERTS

std::uint8_t * Allocator::Shadow(const SmallPage * page, const void * slot) const
{
    const auto offset = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(slot) - reinterpret_cast<std::uintptr_t>(page));
    const std::uint32_t bytes = kSmallClasses.bytes[page->sizeClass];

    if (offset < kSmallPageHeaderBytes || (offset - kSmallPageHeaderBytes) % bytes != 0 ||
        (offset - kSmallPageHeaderBytes) / bytes >= kSmallClasses.capacity[page->sizeClass]) [[unlikely]]
    {
        FatalError("heap: %p is not a slot of its %u-byte page", slot, static_cast<unsigned>(bytes));
    }

    return reinterpret_cast<std::uint8_t *>(m_base + page->shadow) + (offset - kSmallPageHeaderBytes) / bytes * 2u;
}

void Allocator::TrackSmall(void * slot, size_t size, std::uint8_t tag)
{
    const auto * page = reinterpret_cast<const SmallPage *>(reinterpret_cast<std::uintptr_t>(slot) & ~static_cast<std::uintptr_t>(kPageBytes - 1));
    const std::uint32_t bytes = kSmallClasses.bytes[page->sizeClass];
    std::uint8_t * shadow = Shadow(page, slot);

    if (shadow[1] != kFreeSlack) [[unlikely]]
    {
        FatalError("heap: the free list handed out live slot %p (tag %u)", slot, static_cast<unsigned>(shadow[0]));
    }

    // The first word held the free-list link; the rest must still be poisoned.
    auto * words = static_cast<std::uint32_t *>(slot);

    for (std::uint32_t word = 1; word < bytes / 4; ++word)
    {
        if (words[word] != kFreedPattern) [[unlikely]]
        {
            FatalError("heap: %u-byte slot %p was written after it was freed (word %u is 0x%08x, last tag %u)",
                       static_cast<unsigned>(bytes), slot, static_cast<unsigned>(word), static_cast<unsigned>(words[word]),
                       static_cast<unsigned>(shadow[0]));
        }
    }

    for (std::uint32_t word = 0; word < bytes / 4; ++word)
    {
        words[word] = kFreshPattern;
    }

    shadow[0] = tag;
    shadow[1] = static_cast<std::uint8_t>(bytes - size);
}

void Allocator::UntrackSmall(void * slot, std::uint8_t & tag, size_t & requested)
{
    const auto * page = reinterpret_cast<const SmallPage *>(reinterpret_cast<std::uintptr_t>(slot) & ~static_cast<std::uintptr_t>(kPageBytes - 1));
    const std::uint32_t bytes = kSmallClasses.bytes[page->sizeClass];
    std::uint8_t * shadow = Shadow(page, slot);

    if (shadow[1] == kFreeSlack) [[unlikely]]
    {
        FatalError("heap: double free of %u-byte slot %p (last tag %u)", static_cast<unsigned>(bytes), slot,
                   static_cast<unsigned>(shadow[0]));
    }

    tag = shadow[0];
    requested = bytes - shadow[1];
    shadow[1] = kFreeSlack;

    auto * words = static_cast<std::uint32_t *>(slot);

    for (std::uint32_t word = 0; word < bytes / 4; ++word)
    {
        words[word] = kFreedPattern;
    }
}

#endif // PS2_D3BFG_ASSERTS

} // namespace ps2::system::heap
