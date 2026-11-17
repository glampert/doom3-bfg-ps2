// ================================================================================================
// File: heap_large.cpp
// Brief: The large tier: a two-level segregated-fit heap (TLSF) of boundary-tagged blocks. Its free
//        top block always merges back into the gap, so the small tier can use that memory too.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/heap_allocator.h"

#include <cstring>

namespace ps2::system::heap
{
namespace
{

static constexpr std::uint32_t kFree = kBlockFree;
static constexpr std::uint32_t kPrevFree = kBlockPrevFree;
static constexpr std::uint32_t kFlagMask = kGranule - 1;

// A free block's two links sit 8 bytes in: the payload's start in release builds, the header's
// unused tracking fields otherwise. Either way 16 bytes hold a free block, so any remainder of a
// split (a multiple of 16) can be one, and a block's size is always what its request rounds to.
static constexpr std::uint32_t kLinksOffset = 8;
static constexpr std::uint32_t kMinBlockBytes = kGranule;

// What tracking builds poison in a free block: everything past its links.
static constexpr std::uint32_t kPoisonOffset = kLinksOffset + 8;

static_assert(kBlockHeaderBytes <= kPoisonOffset && kPoisonOffset == kMinBlockBytes);

// Blocks start this far into the arena so that every payload is 16-aligned.
static constexpr std::uint32_t kFirstBlock = (kGranule - kBlockHeaderBytes % kGranule) % kGranule;

struct FreeLinks
{
    std::uint32_t next;
    std::uint32_t prev;
};

static std::uint32_t BlockBytesFor(size_t size)
{
    const auto bytes = static_cast<std::uint32_t>((size + kBlockHeaderBytes + kGranule - 1) & ~static_cast<size_t>(kGranule - 1));
    return bytes < kMinBlockBytes ? kMinBlockBytes : bytes;
}

// Exact 16-byte bins below 256; above, sixteen bins per power of two.
static void Mapping(std::uint32_t bytes, std::uint32_t & firstLevel, std::uint32_t & secondLevel)
{
    if (bytes < 256)
    {
        firstLevel = 0;
        secondLevel = bytes >> 4;
        return;
    }

    const auto top = static_cast<std::uint32_t>(31 - CountLeadingZeros(bytes));
    firstLevel = top - 7;
    secondLevel = (bytes >> (top - 4)) ^ 16u;
}

// Rounds up to the next bin's start, so any block found in a bin at or above it is big enough.
static void MappingSearch(std::uint32_t bytes, std::uint32_t & firstLevel, std::uint32_t & secondLevel)
{
    if (bytes >= 256)
    {
        const auto top = static_cast<std::uint32_t>(31 - CountLeadingZeros(bytes));
        bytes += (1u << (top - 4)) - 1;
    }

    Mapping(bytes, firstLevel, secondLevel);
}

#if PS2_D3BFG_ASSERTS
static void Fill(void * memory, size_t bytes, std::uint32_t pattern)
{
    auto * words = static_cast<std::uint32_t *>(memory);

    for (size_t word = 0; word < bytes / 4; ++word)
    {
        words[word] = pattern;
    }
}

// A binned block is poisoned past its links. Checks the first and last 256 bytes, which keeps debug
// map loads quick and still catches a write through a stale pointer to a block's start or end.
static void CheckPoisonWord(const BlockHeader * block, std::uint32_t bytes, const std::uint32_t * words, std::uint32_t word)
{
    if (words[word] != kFreedPattern) [[unlikely]]
    {
        FatalError("heap: %u-byte block %p was written after it was freed (payload word %u is 0x%08x)",
                   static_cast<unsigned>(bytes), static_cast<const void *>(block + 1), static_cast<unsigned>(word + (kPoisonOffset - kBlockHeaderBytes) / 4),
                   static_cast<unsigned>(words[word]));
    }
}

static void CheckPoison(const BlockHeader * block, std::uint32_t bytes)
{
    const auto * words = static_cast<const std::uint32_t *>(static_cast<const void *>(reinterpret_cast<const std::uint8_t *>(block) + kPoisonOffset));
    const std::uint32_t count = (bytes - kPoisonOffset) / 4;
    const std::uint32_t head = count < 64 ? count : 64;
    const std::uint32_t tail = count > head + 64 ? count - 64 : head;

    for (std::uint32_t word = 0; word < head; ++word)
    {
        CheckPoisonWord(block, bytes, words, word);
    }

    for (std::uint32_t word = tail; word < count; ++word)
    {
        CheckPoisonWord(block, bytes, words, word);
    }
}
#endif // PS2_D3BFG_ASSERTS

} // namespace

PS2_COLD_FUNC void Allocator::InitLarge()
{
    m_large = LargeState{};

    for (auto & level : m_large.heads)
    {
        for (std::uint32_t & head : level)
        {
            head = kNoBlock;
        }
    }

    m_large.begin = kFirstBlock;
    m_largeTop = kFirstBlock;
}

BlockHeader * Allocator::BlockAt(std::uint32_t offset) const
{
    return reinterpret_cast<BlockHeader *>(m_base + offset);
}

std::uint32_t Allocator::BlockSize(std::uint32_t offset) const
{
    return BlockAt(offset)->sizeFlags & ~kFlagMask;
}

static FreeLinks * LinksOf(BlockHeader * block)
{
    return static_cast<FreeLinks *>(static_cast<void *>(reinterpret_cast<std::uint8_t *>(block) + kLinksOffset));
}

void Allocator::InsertFree(std::uint32_t offset)
{
    BlockHeader * block = BlockAt(offset);
    std::uint32_t firstLevel;
    std::uint32_t secondLevel;
    Mapping(block->sizeFlags & ~kFlagMask, firstLevel, secondLevel);

    FreeLinks * links = LinksOf(block);
    const std::uint32_t head = m_large.heads[firstLevel][secondLevel];
    links->next = head;
    links->prev = kNoBlock;

    if (head != kNoBlock)
    {
        LinksOf(BlockAt(head))->prev = offset;
    }

    m_large.heads[firstLevel][secondLevel] = offset;
    m_large.firstLevelMap |= 1u << firstLevel;
    m_large.secondLevelMap[firstLevel] |= 1u << secondLevel;
    m_large.freeBytes += block->sizeFlags & ~kFlagMask;
}

void Allocator::RemoveFree(std::uint32_t offset)
{
    BlockHeader * block = BlockAt(offset);
    std::uint32_t firstLevel;
    std::uint32_t secondLevel;
    Mapping(block->sizeFlags & ~kFlagMask, firstLevel, secondLevel);

    const FreeLinks * links = LinksOf(block);

    if (links->next != kNoBlock)
    {
        LinksOf(BlockAt(links->next))->prev = links->prev;
    }

    if (links->prev != kNoBlock)
    {
        LinksOf(BlockAt(links->prev))->next = links->next;
    }
    else
    {
        m_large.heads[firstLevel][secondLevel] = links->next;

        if (links->next == kNoBlock)
        {
            m_large.secondLevelMap[firstLevel] &= ~(1u << secondLevel);

            if (m_large.secondLevelMap[firstLevel] == 0)
            {
                m_large.firstLevelMap &= ~(1u << firstLevel);
            }
        }
    }

    m_large.freeBytes -= block->sizeFlags & ~kFlagMask;
}

std::uint32_t Allocator::FindFree(std::uint32_t blockBytes)
{
    std::uint32_t firstLevel;
    std::uint32_t secondLevel;
    MappingSearch(blockBytes, firstLevel, secondLevel);

    if (firstLevel >= kFirstLevelCount)
    {
        return kNoBlock;
    }

    std::uint32_t secondMap = m_large.secondLevelMap[firstLevel] & (~0u << secondLevel);

    if (secondMap == 0)
    {
        const std::uint32_t firstMap = firstLevel + 1 < 32 ? m_large.firstLevelMap & (~0u << (firstLevel + 1)) : 0;

        if (firstMap == 0)
        {
            return kNoBlock;
        }

        firstLevel = static_cast<std::uint32_t>(CountTrailingZeros(firstMap));
        secondMap = m_large.secondLevelMap[firstLevel];
    }

    secondLevel = static_cast<std::uint32_t>(CountTrailingZeros(secondMap));
    const std::uint32_t offset = m_large.heads[firstLevel][secondLevel];
    RemoveFree(offset);
    return offset;
}

bool Allocator::GrowLarge(std::uint32_t bytes)
{
    if (m_smallLow - m_largeTop < bytes)
    {
        return false;
    }

    m_largeTop += bytes;
    UpdatePeak();
    return true;
}

// A new used block on top of the large tier, taken from the gap. The top block is always used: a
// free one merges into the gap.
std::uint32_t Allocator::Extend(std::uint32_t blockBytes)
{
    const std::uint32_t offset = m_largeTop;

    if (!GrowLarge(blockBytes))
    {
        return kNoBlock;
    }

    BlockHeader * block = BlockAt(offset);
    block->prevPhys = m_large.last;
    block->sizeFlags = blockBytes;
    m_large.last = offset;
    return offset;
}

// Clears the free flag of a block just taken from a bin, and the next block's record of it.
void Allocator::MarkUsed(std::uint32_t offset)
{
    BlockHeader * block = BlockAt(offset);
    block->sizeFlags &= ~kFree;
    const std::uint32_t next = offset + (block->sizeFlags & ~kFlagMask);

    if (next < m_largeTop)
    {
        BlockAt(next)->sizeFlags &= ~kPrevFree;
    }
}

// Frees a block: merges it with free neighbours, then returns it to the gap if it is on top or
// files it in its bin.
void Allocator::ReleaseBlock(std::uint32_t offset)
{
    BlockHeader * block = BlockAt(offset);
    std::uint32_t bytes = block->sizeFlags & ~kFlagMask;
    std::uint32_t next = offset + bytes;

    if (next < m_largeTop && (BlockAt(next)->sizeFlags & kFree) != 0)
    {
        RemoveFree(next);
        bytes += BlockSize(next);

        if (m_large.last == next)
        {
            m_large.last = offset;
        }

        next = offset + bytes;
    }

    if ((block->sizeFlags & kPrevFree) != 0)
    {
        const std::uint32_t prev = block->prevPhys;
        RemoveFree(prev);
        bytes += BlockSize(prev);

        if (m_large.last == offset)
        {
            m_large.last = prev;
        }

        offset = prev;
        block = BlockAt(prev);
    }

#if PS2_D3BFG_ASSERTS
    // Merged headers sit inside the block now; poison everything past the links.
    Fill(reinterpret_cast<std::uint8_t *>(block) + kPoisonOffset, bytes - kPoisonOffset, kFreedPattern);
    block->magic = 0;
#endif // PS2_D3BFG_ASSERTS

    if (next >= m_largeTop)
    {
        // The top block goes back to the gap; the block below it is used.
        m_largeTop = offset;
        m_large.last = block->prevPhys;
        return;
    }

    block->sizeFlags = bytes | kFree | (block->sizeFlags & kPrevFree);
    BlockHeader * after = BlockAt(next);
    after->prevPhys = offset;
    after->sizeFlags |= kPrevFree;
    InsertFree(offset);
}

// Trims a used block to `blockBytes`, freeing the tail when it can hold a block.
void Allocator::Split(std::uint32_t offset, std::uint32_t blockBytes)
{
    BlockHeader * block = BlockAt(offset);
    const std::uint32_t bytes = block->sizeFlags & ~kFlagMask;

    if (bytes - blockBytes < kMinBlockBytes)
    {
        return;
    }

    const std::uint32_t rest = offset + blockBytes;
    const std::uint32_t restBytes = bytes - blockBytes;
    block->sizeFlags = blockBytes | (block->sizeFlags & kFlagMask);

    BlockHeader * tail = BlockAt(rest);
    tail->prevPhys = offset;
    tail->sizeFlags = restBytes;

    if (m_large.last == offset)
    {
        m_large.last = rest;
    }
    else
    {
        BlockAt(rest + restBytes)->prevPhys = rest;
    }

    ReleaseBlock(rest);
}

void * Allocator::AllocLarge(size_t size, size_t alignment)
{
    if (size > kMaxArenaBytes || alignment > kMaxArenaBytes) [[unlikely]]
    {
        return nullptr;
    }

    const std::uint32_t blockBytes = BlockBytesFor(size);
    const auto align = static_cast<std::uint32_t>(alignment);
    const std::uint32_t search = align <= kGranule ? blockBytes : blockBytes + align + kMinBlockBytes;

    std::uint32_t offset = FindFree(search);

    if (offset == kNoBlock)
    {
        offset = Extend(search);

        if (offset == kNoBlock) [[unlikely]]
        {
            return nullptr;
        }
    }
    else
    {
#if PS2_D3BFG_ASSERTS
        CheckPoison(BlockAt(offset), BlockSize(offset));
#endif // PS2_D3BFG_ASSERTS
        MarkUsed(offset);
    }

    if (align > kGranule)
    {
        // Move the payload up to the alignment and free the blocks in front of it.
        const std::uint32_t payload = offset + kBlockHeaderBytes;
        const auto absolute = static_cast<std::uint32_t>((m_base + payload) & (align - 1));
        // A multiple of 16, so the front is always big enough to be a free block.
        const std::uint32_t front = absolute == 0 ? 0 : align - absolute;

        if (front != 0)
        {
            BlockHeader * block = BlockAt(offset);
            const std::uint32_t bytes = block->sizeFlags & ~kFlagMask;
            const std::uint32_t moved = offset + front;

            BlockHeader * aligned = BlockAt(moved);
            aligned->prevPhys = offset;
            aligned->sizeFlags = bytes - front;
            block->sizeFlags = front | (block->sizeFlags & kPrevFree);

            if (m_large.last == offset)
            {
                m_large.last = moved;
            }
            else
            {
                BlockAt(moved + bytes - front)->prevPhys = moved;
            }

            // The front block's neighbour below is used, so it only joins a bin.
#if PS2_D3BFG_ASSERTS
            Fill(reinterpret_cast<std::uint8_t *>(block) + kPoisonOffset, front - kPoisonOffset, kFreedPattern);
            block->magic = 0;
#endif // PS2_D3BFG_ASSERTS
            block->sizeFlags |= kFree;
            aligned->sizeFlags |= kPrevFree;
            InsertFree(offset);
            offset = moved;
        }
    }

    Split(offset, blockBytes);

    BlockHeader * block = BlockAt(offset);
    void * payload = reinterpret_cast<std::uint8_t *>(block) + kBlockHeaderBytes;

#if PS2_D3BFG_ASSERTS
    block->magic = kBlockMagic;
    block->requested = static_cast<std::uint32_t>(size);
    block->tag = 0;
    Fill(payload, (block->sizeFlags & ~kFlagMask) - kBlockHeaderBytes, kFreshPattern);
#endif // PS2_D3BFG_ASSERTS

    return payload;
}

size_t Allocator::FreeLarge(void * pointer)
{
    auto * block = static_cast<BlockHeader *>(static_cast<void *>(static_cast<std::uint8_t *>(pointer) - kBlockHeaderBytes));
    const auto offset = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(block) - m_base);

#if PS2_D3BFG_ASSERTS
    if (block->magic != kBlockMagic || (block->sizeFlags & kFree) != 0 || offset + (block->sizeFlags & ~kFlagMask) > m_largeTop) [[unlikely]]
    {
        FatalError("heap: free of %p, which is not a live large block (double free or corrupt header)", pointer);
    }
#endif // PS2_D3BFG_ASSERTS

    const std::uint32_t bytes = block->sizeFlags & ~kFlagMask;
    ReleaseBlock(offset);
    return bytes;
}

bool Allocator::ResizeLargeInPlace(void * pointer, size_t size)
{
    if (size > kMaxArenaBytes) [[unlikely]]
    {
        return false;
    }

    const auto offset = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer) - kBlockHeaderBytes - m_base);
    BlockHeader * block = BlockAt(offset);
    const std::uint32_t blockBytes = BlockBytesFor(size);
    const std::uint32_t bytes = block->sizeFlags & ~kFlagMask;

    if (blockBytes <= bytes)
    {
        Split(offset, blockBytes);
        return true;
    }

    const std::uint32_t next = offset + bytes;

    if (next >= m_largeTop)
    {
        if (!GrowLarge(blockBytes - bytes))
        {
            return false;
        }

        block->sizeFlags = blockBytes | (block->sizeFlags & kFlagMask);
        return true;
    }

    if ((BlockAt(next)->sizeFlags & kFree) == 0 || bytes + BlockSize(next) < blockBytes)
    {
        return false;
    }

    const std::uint32_t merged = bytes + BlockSize(next);
    RemoveFree(next);
    block->sizeFlags = merged | (block->sizeFlags & kFlagMask);

    // A free block is never on top, so a block follows the merged one.
    BlockHeader * after = BlockAt(offset + merged);
    after->prevPhys = offset;
    after->sizeFlags &= ~kPrevFree;
    Split(offset, blockBytes);
    return true;
}

size_t Allocator::LargestFreeBlock() const
{
    if (m_large.firstLevelMap == 0)
    {
        return 0;
    }

    const auto firstLevel = static_cast<std::uint32_t>(31 - CountLeadingZeros(m_large.firstLevelMap));
    const auto secondLevel = static_cast<std::uint32_t>(31 - CountLeadingZeros(m_large.secondLevelMap[firstLevel]));
    size_t largest = 0;

    for (std::uint32_t offset = m_large.heads[firstLevel][secondLevel]; offset != kNoBlock;
         offset = LinksOf(BlockAt(offset))->next)
    {
        const size_t bytes = BlockSize(offset);
        largest = bytes > largest ? bytes : largest;
    }

    return largest;
}

} // namespace ps2::system::heap
