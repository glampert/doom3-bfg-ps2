// ================================================================================================
// File: heap_allocator.h
// Brief: The heap's two tiers over one arena. Requests up to 256 bytes take headerless slots in 4 KiB
//        pages of one size class, carved down from the arena's top; larger ones take a TLSF block with
//        an 8-byte header, carved up from its bottom. The gap between them serves whichever needs it.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

#include <cstddef>
#include <cstdint>

namespace ps2::system::heap
{

// ------------------------------------------------------------------------------------------------
// Layout constants
// ------------------------------------------------------------------------------------------------

static constexpr std::uint32_t kPageShift = 12;
static constexpr std::uint32_t kPageBytes = 1u << kPageShift;

// Page indices are 16 bits and the small-page bitmap is a fixed array.
static constexpr std::uint32_t kMaxArenaBytes = 32u * 1024u * 1024u;
static constexpr std::uint32_t kMaxPages = kMaxArenaBytes >> kPageShift;

// Every large payload, and every small class whose size is a multiple of 16, is 16-aligned.
static constexpr std::uint32_t kGranule = 16;

static constexpr std::uint32_t kSmallMaxBytes = 256;
static constexpr int kSmallClassCount = 48;
static constexpr std::uint32_t kSmallPageHeaderBytes = 16;

// Tracking builds keep each allocation's tag and requested size; release builds keep neither.
#if PS2_D3BFG_ASSERTS
static constexpr std::uint32_t kBlockHeaderBytes = 16;
#else // PS2_D3BFG_ASSERTS
static constexpr std::uint32_t kBlockHeaderBytes = 8;
#endif // PS2_D3BFG_ASSERTS

// Debug fill patterns: freed memory, and memory handed out but not yet written. Both are odd, so a
// stale pointer read from either faults on its first word access.
static constexpr std::uint32_t kFreedPattern = 0xDEADBEEFu;
static constexpr std::uint32_t kFreshPattern = 0xCDCDCDCDu;

static constexpr std::uint16_t kNoPage = 0xFFFF;
static constexpr std::uint32_t kNoBlock = 0xFFFFFFFFu;

// Flags in a block's size word, and the tracking builds' header magic.
static constexpr std::uint32_t kBlockFree = 1;
static constexpr std::uint32_t kBlockPrevFree = 2;
static constexpr std::uint16_t kBlockMagic = 0xB10C;

// A slot's shadow is {tag, slack}; slack is the class size less the requested size, under 16 for
// every request, so this value marks a free slot.
static constexpr std::uint8_t kFreeSlack = 0xFF;

// ------------------------------------------------------------------------------------------------
// Small size classes
// ------------------------------------------------------------------------------------------------

// 4-byte steps to 128 and 8-byte steps to 256 (48 classes), measured on the retail boot's live set
// (docs/reference/memory-budget.md, 2026-11-17). A class's slots sit at page + 16 + i x size, so a
// class is aligned to its size's lowest set bit, capped at 16.
struct SmallClassTable
{
    std::uint16_t bytes[kSmallClassCount];
    std::uint16_t capacity[kSmallClassCount];
    std::uint8_t alignment[kSmallClassCount];
    std::uint8_t forSize[3][kSmallMaxBytes / 4 + 1]; // alignment 4, 8, 16; index (size + 3) / 4
};

constexpr SmallClassTable MakeSmallClassTable()
{
    SmallClassTable table{};
    int count = 0;

    for (std::uint32_t bytes = 4; bytes <= kSmallMaxBytes; bytes += bytes < 128 ? 4u : 8u)
    {
        const std::uint32_t lowBit = bytes & (0u - bytes);
        table.bytes[count] = static_cast<std::uint16_t>(bytes);
        table.capacity[count] = static_cast<std::uint16_t>((kPageBytes - kSmallPageHeaderBytes) / bytes);
        table.alignment[count] = static_cast<std::uint8_t>(lowBit < kGranule ? lowBit : kGranule);
        ++count;
    }

    for (int level = 0; level < 3; ++level)
    {
        const std::uint32_t alignment = 4u << level;

        for (std::uint32_t index = 0; index <= kSmallMaxBytes / 4; ++index)
        {
            const std::uint32_t size = index * 4;
            int sizeClass = 0;

            while (sizeClass < kSmallClassCount &&
                   (table.bytes[sizeClass] < size || table.alignment[sizeClass] < alignment))
            {
                ++sizeClass;
            }

            table.forSize[level][index] = static_cast<std::uint8_t>(sizeClass);
        }
    }

    return table;
}

inline constexpr SmallClassTable kSmallClasses = MakeSmallClassTable();

static_assert(kSmallClasses.bytes[kSmallClassCount - 1] == kSmallMaxBytes);
static_assert(kSmallClasses.forSize[2][kSmallMaxBytes / 4] == kSmallClassCount - 1);

// ------------------------------------------------------------------------------------------------
// In-band records
// ------------------------------------------------------------------------------------------------

// The first 16 bytes of each small page; slots follow. Links are page indices, so the layout is the
// same on the host and the EE.
struct SmallPage
{
    std::uint16_t freeHead; // page offset of the first free slot, 0 for none (the header is at 0)
    std::uint16_t used;     // live slots
    std::uint8_t sizeClass;
    std::uint8_t flags;
    std::uint16_t prev; // the class's partial list
    std::uint16_t next;
    std::uint16_t reserved;
    std::uint32_t shadow; // tracking builds: arena offset of one {tag, slack} pair per slot
};

static_assert(sizeof(SmallPage) == kSmallPageHeaderBytes);

// Sits right before every large payload. Sizes include the header and are multiples of kGranule;
// the low bits hold the flags. A free block's payload holds its two free-list links.
struct BlockHeader
{
    std::uint32_t prevPhys; // arena offset of the block below, kNoBlock for the first
    std::uint32_t sizeFlags;
#if PS2_D3BFG_ASSERTS
    std::uint32_t requested;
    std::uint16_t tag;
    std::uint16_t magic;
#endif // PS2_D3BFG_ASSERTS
};

static_assert(sizeof(BlockHeader) == kBlockHeaderBytes);

// ------------------------------------------------------------------------------------------------
// Statistics
// ------------------------------------------------------------------------------------------------

struct TierStats
{
    size_t arenaBytes;
    size_t committedBytes; // the arena less the gap
    size_t peakCommittedBytes;
    size_t gapBytes;
    size_t smallPages;         // pages holding slots, borrowed ones included
    size_t smallFreePages;     // released pages above the frontier, ready for any class
    size_t smallBorrowedPages; // taken from the large tier when the gap ran out
    size_t smallSlotBytes;     // capacity of every slot in small pages
    size_t largeBytes;         // the large tier's span, free blocks included
    size_t largeFreeBytes;
    size_t largestFreeBlock;
    size_t metadataBytes; // tracking builds: the small pages' shadow arrays
};

// What a free gives back to the ledger.
struct Released
{
    size_t backing;
#if PS2_D3BFG_ASSERTS
    size_t requested;
    std::uint8_t tag;
#endif // PS2_D3BFG_ASSERTS
};

// ------------------------------------------------------------------------------------------------
// Allocator
// ------------------------------------------------------------------------------------------------

// Not thread-safe: the EE runs the engine on one thread. Alignments are powers of two from 4. A
// caller's mistake is caught by assertions only, as on any per-frame path.
class Allocator
{
public:
    constexpr Allocator() = default;

    Allocator(const Allocator &) = delete;
    Allocator & operator=(const Allocator &) = delete;

    // Takes [begin, end), at most kMaxArenaBytes once its start is rounded up to a page.
    PS2_COLD_FUNC bool Init(void * begin, void * end);

    bool IsInitialized() const
    {
        return m_base != 0;
    }

    bool Owns(const void * pointer) const
    {
        return reinterpret_cast<std::uintptr_t>(pointer) - m_base < m_bytes;
    }

    // Null when the arena has no room. `backing` receives the bytes the allocation occupies.
    void * Alloc(size_t size, size_t alignment, std::uint8_t tag, size_t & backing);
    Released Free(void * pointer);

    // Keeps the block when the new size fits it; otherwise moves the data. Null leaves `pointer` live.
    void * Realloc(void * pointer, size_t size, size_t alignment, std::uint8_t tag, size_t & oldBacking,
                   size_t & newBacking);

    size_t UsableSize(const void * pointer) const;
    TierStats GetTierStats() const;

#if PS2_D3BFG_ASSERTS
    // The tag and requested size of a live allocation; fatal for any other pointer.
    void GetTracking(const void * pointer, std::uint8_t & tag, size_t & requested) const;
#endif // PS2_D3BFG_ASSERTS

private:
    static constexpr std::uint8_t kPageBorrowed = 1;

    struct SmallState
    {
        std::uint16_t partial[kSmallClassCount] = {}; // head page of each class's partial list
        std::uint16_t emptyPage[kSmallClassCount] = {}; // one empty page each class keeps listed
        std::uint32_t pages = 0;
        std::uint32_t freePageCount = 0;
        std::uint32_t borrowedPages = 0;
        std::uint32_t slotBytes = 0;
        std::uint32_t metadataBytes = 0;
    };

    // Two-level segregated fit: 16 second-level bins per power of two, exact 16-byte bins below 256.
    static constexpr std::uint32_t kSecondLevelShift = 4;
    static constexpr std::uint32_t kSecondLevelCount = 1u << kSecondLevelShift;
    static constexpr std::uint32_t kFirstLevelCount = 20;

    struct LargeState
    {
        std::uint32_t firstLevelMap = 0;
        std::uint32_t secondLevelMap[kFirstLevelCount] = {};
        std::uint32_t heads[kFirstLevelCount][kSecondLevelCount] = {};
        std::uint32_t begin = 0;       // offset of the first block
        std::uint32_t last = kNoBlock; // the block ending at the top
        std::uint32_t freeBytes = 0;
    };

    // Small tier (heap_small.cpp, fast paths below)
    void * AllocSmall(int sizeClass);
    size_t FreeSmall(void * pointer);
    PS2_COLD_FUNC void InitSmall();
    std::uint16_t NewPage(int sizeClass);
    SmallPage * TakeFreePage();
    void RetirePage(SmallPage * page);
    void ReleasePage(SmallPage * page);
    void PushPartial(SmallPage * page);
    void UnlinkPartial(SmallPage * page);
    bool IsSmall(const void * pointer) const;
    SmallPage * PageAt(std::uint16_t index) const;
    std::uint16_t PageIndex(const void * page) const;
    static void SetBit(std::uint32_t * bitmap, std::uint32_t index, bool set);
#if PS2_D3BFG_ASSERTS
    std::uint8_t * Shadow(const SmallPage * page, const void * slot) const;
    void TrackSmall(void * slot, size_t size, std::uint8_t tag);
    void UntrackSmall(void * slot, std::uint8_t & tag, size_t & requested);
#endif // PS2_D3BFG_ASSERTS

    // Large tier (heap_large.cpp)
    void * AllocLarge(size_t size, size_t alignment);
    size_t FreeLarge(void * pointer);
    bool ResizeLargeInPlace(void * pointer, size_t size);
    PS2_COLD_FUNC void InitLarge();
    BlockHeader * BlockAt(std::uint32_t offset) const;
    std::uint32_t BlockSize(std::uint32_t offset) const;
    void InsertFree(std::uint32_t offset);
    void RemoveFree(std::uint32_t offset);
    std::uint32_t FindFree(std::uint32_t blockBytes);
    std::uint32_t Extend(std::uint32_t blockBytes);
    void MarkUsed(std::uint32_t offset);
    void Split(std::uint32_t offset, std::uint32_t blockBytes);
    void ReleaseBlock(std::uint32_t offset);
    size_t LargestFreeBlock() const;

    // The gap between the tiers.
    bool GrowLarge(std::uint32_t bytes);
    void UpdatePeak();

    std::uintptr_t m_base = 0; // page-aligned
    std::uint32_t m_bytes = 0;
    std::uint32_t m_largeTop = 0; // offsets from m_base
    std::uint32_t m_smallLow = 0;
    std::uint32_t m_peakCommitted = 0;
    std::uint32_t m_smallBitmap[kMaxPages / 32] = {}; // pages holding slots
    std::uint32_t m_freeBitmap[kMaxPages / 32] = {};  // released pages above the frontier
    SmallState m_small;
    LargeState m_large;
};

// ------------------------------------------------------------------------------------------------
// Inline fast paths
// ------------------------------------------------------------------------------------------------

inline bool Allocator::IsSmall(const void * pointer) const
{
    const auto index = static_cast<std::uint32_t>((reinterpret_cast<std::uintptr_t>(pointer) - m_base) >> kPageShift);
    return ((m_smallBitmap[index >> 5] >> (index & 31u)) & 1u) != 0;
}

inline SmallPage * Allocator::PageAt(std::uint16_t index) const
{
    return reinterpret_cast<SmallPage *>(m_base + (static_cast<std::uintptr_t>(index) << kPageShift));
}

inline std::uint16_t Allocator::PageIndex(const void * page) const
{
    return static_cast<std::uint16_t>((reinterpret_cast<std::uintptr_t>(page) - m_base) >> kPageShift);
}

inline void * Allocator::AllocSmall(int sizeClass)
{
    std::uint16_t index = m_small.partial[sizeClass];

    if (index == kNoPage) [[unlikely]]
    {
        index = NewPage(sizeClass);

        if (index == kNoPage) [[unlikely]]
        {
            return nullptr;
        }
    }

    SmallPage * page = PageAt(index);
    void * slot = reinterpret_cast<std::uint8_t *>(page) + page->freeHead;
    page->freeHead = *static_cast<const std::uint16_t *>(slot);

    if (page->used++ == 0)
    {
        // The only empty page on a class's list is the one it keeps.
        m_small.emptyPage[sizeClass] = kNoPage;
    }

    if (page->freeHead == 0) [[unlikely]]
    {
        // Full: it is the list's head, so it leaves the list from the front.
        m_small.partial[sizeClass] = page->next;

        if (page->next != kNoPage)
        {
            PageAt(page->next)->prev = kNoPage;
        }
    }

    return slot;
}

inline size_t Allocator::FreeSmall(void * pointer)
{
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(pointer);
    auto * page = reinterpret_cast<SmallPage *>(address & ~static_cast<std::uintptr_t>(kPageBytes - 1));
    const int sizeClass = page->sizeClass;

    *static_cast<std::uint16_t *>(pointer) = page->freeHead;
    page->freeHead = static_cast<std::uint16_t>(address & (kPageBytes - 1));

    if (page->used == kSmallClasses.capacity[sizeClass]) [[unlikely]]
    {
        PushPartial(page);
    }

    if (--page->used == 0) [[unlikely]]
    {
        RetirePage(page);
    }

    return kSmallClasses.bytes[sizeClass];
}

inline void * Allocator::Alloc(size_t size, size_t alignment, std::uint8_t tag, size_t & backing)
{
    PS2_AssertMsg(m_base != 0, "heap used before initialization");
    PS2_AssertMsg(alignment >= 4 && (alignment & (alignment - 1)) == 0, "heap alignment must be a power of two from 4");

    if (size <= kSmallMaxBytes && alignment <= kGranule) [[likely]]
    {
        const int sizeClass = kSmallClasses.forSize[alignment >> 3][(size + 3) >> 2];
        void * slot = AllocSmall(sizeClass);
        backing = kSmallClasses.bytes[sizeClass];

#if PS2_D3BFG_ASSERTS
        if (slot != nullptr)
        {
            TrackSmall(slot, size, tag);
        }
#else // PS2_D3BFG_ASSERTS
        static_cast<void>(tag);
#endif // PS2_D3BFG_ASSERTS

        return slot;
    }

    void * payload = AllocLarge(size, alignment);

    if (payload == nullptr) [[unlikely]]
    {
        backing = 0;
        return nullptr;
    }

    auto * block = static_cast<BlockHeader *>(static_cast<void *>(static_cast<std::uint8_t *>(payload) - kBlockHeaderBytes));
    backing = block->sizeFlags & ~(kGranule - 1);

#if PS2_D3BFG_ASSERTS
    block->requested = static_cast<std::uint32_t>(size);
    block->tag = tag;
#endif // PS2_D3BFG_ASSERTS

    return payload;
}

inline Released Allocator::Free(void * pointer)
{
    PS2_AssertMsg(Owns(pointer), "heap free of a pointer outside the arena");
    Released released{};

#if PS2_D3BFG_ASSERTS
    if (IsSmall(pointer))
    {
        UntrackSmall(pointer, released.tag, released.requested);
        released.backing = FreeSmall(pointer);
    }
    else
    {
        GetTracking(pointer, released.tag, released.requested);
        released.backing = FreeLarge(pointer);
    }
#else // PS2_D3BFG_ASSERTS
    released.backing = IsSmall(pointer) ? FreeSmall(pointer) : FreeLarge(pointer);
#endif // PS2_D3BFG_ASSERTS

    return released;
}

} // namespace ps2::system::heap
