// ================================================================================================
// File: heap_core_tests.cpp
// Brief: Drive the heap's Allocator over a private arena against a reference model: class choice,
//        alignment, content integrity, tier exhaustion and recovery, and (tracking builds) the
//        poison and double-free checks, each of which a mode seeds as a defect.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/heap_allocator.h"
#include "ps2/system/log.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using ps2::system::heap::Allocator;
using ps2::system::heap::kSmallClasses;
using ps2::system::heap::kSmallClassCount;
using ps2::system::heap::kSmallMaxBytes;

namespace
{

static int s_failures = 0;

void Check(bool condition, const char * name)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", name);
        ++s_failures;
    }
}

struct Xorshift
{
    std::uint32_t state = 0x9E3779B9u;

    std::uint32_t Next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    std::uint32_t Below(std::uint32_t limit)
    {
        return Next() % limit;
    }
};

// An arena from the host heap, released at scope exit.
struct Arena
{
    explicit Arena(size_t bytes)
        : memory(static_cast<std::uint8_t *>(std::malloc(bytes)))
        , size(bytes)
    {
    }

    ~Arena()
    {
        std::free(memory);
    }

    Arena(const Arena &) = delete;
    Arena & operator=(const Arena &) = delete;

    std::uint8_t * memory;
    size_t size;
};

struct Live
{
    std::uint8_t * pointer;
    size_t size;
    size_t alignment;
    std::uint8_t fill;
};

void FillLive(const Live & live)
{
    std::memset(live.pointer, live.fill, live.size);
}

bool Intact(const Live & live)
{
    for (size_t index = 0; index < live.size; ++index)
    {
        if (live.pointer[index] != live.fill)
        {
            return false;
        }
    }

    return true;
}

// With nothing live, the large tier holds only free blocks and (tracking builds) the shadows of the
// empty pages each class keeps, and no small page is borrowed.
bool Drained(const ps2::system::heap::TierStats & stats)
{
    return stats.largeBytes == stats.largeFreeBytes + stats.metadataBytes && stats.smallBorrowedPages == 0 &&
           stats.smallPages <= static_cast<size_t>(kSmallClassCount);
}

// Backing depends on the request alone, never on which free block served it: the gates compare
// warm reload ledgers exactly.
size_t ExpectedBacking(size_t size, size_t alignment)
{
    if (size <= kSmallMaxBytes && alignment <= 16)
    {
        const std::uint32_t level = alignment == 16 ? 2u : alignment == 8 ? 1u : 0u;
        return kSmallClasses.bytes[kSmallClasses.forSize[level][(size + 3) / 4]];
    }

    const size_t bytes = (size + ps2::system::heap::kBlockHeaderBytes + 15) & ~size_t{ 15 };
    return bytes < 16 ? 16 : bytes;
}

void CheckClassTable()
{
    bool valid = true;

    for (std::uint32_t level = 0; level < 3; ++level)
    {
        const std::uint32_t alignment = 4u << level;

        for (std::uint32_t size = 0; size <= kSmallMaxBytes; ++size)
        {
            const int sizeClass = kSmallClasses.forSize[level][(size + 3) / 4];
            valid = valid && sizeClass < kSmallClassCount && kSmallClasses.bytes[sizeClass] >= size &&
                    kSmallClasses.alignment[sizeClass] >= alignment;

            // No smaller class would do.
            for (int smaller = 0; smaller < sizeClass; ++smaller)
            {
                valid = valid && !(kSmallClasses.bytes[smaller] >= size && kSmallClasses.alignment[smaller] >= alignment);
            }
        }
    }

    for (int sizeClass = 0; sizeClass < kSmallClassCount; ++sizeClass)
    {
        const std::uint32_t bytes = kSmallClasses.bytes[sizeClass];
        valid = valid && (16u + kSmallClasses.capacity[sizeClass] * bytes) <= 4096u;
        valid = valid && (16u + (kSmallClasses.capacity[sizeClass] + 1u) * bytes) > 4096u;
    }

    Check(valid, "class-table");
}

void CheckAlignmentAndSizes()
{
    Arena arena(8u * 1024u * 1024u);
    Allocator allocator;
    Check(allocator.Init(arena.memory, arena.memory + arena.size), "init");
    std::vector<void *> pointers;
    bool valid = true;

    for (size_t alignment = 4; alignment <= 8192; alignment <<= 1)
    {
        for (size_t size = 0; size <= 1200; size += size < 300 ? 1 : 37)
        {
            size_t backing = 0;
            void * pointer = allocator.Alloc(size, alignment, 7, backing);
            valid = valid && pointer != nullptr && reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0;
            valid = valid && allocator.Owns(pointer) && allocator.UsableSize(pointer) >= size && backing >= size;

            if (pointer != nullptr)
            {
                std::memset(pointer, 0x5A, size);
                pointers.push_back(pointer);
            }
        }
    }

    for (void * pointer : pointers)
    {
        allocator.Free(pointer);
    }

    const auto stats = allocator.GetTierStats();
    Check(valid, "alignment-and-sizes");
    Check(Drained(stats), "large-tier-drains");
    Check(stats.smallPages <= static_cast<size_t>(kSmallClassCount), "small-pages-cached");
}

// The boot data's hold: the arena's first bytes past the header's 16 survive Init and every
// allocation, a hold the arena cannot place is refused, and the release serves large requests.
void CheckHeldBlock()
{
    static constexpr size_t kPage = 4096;
    static constexpr size_t kArenaBytes = 2u * 1024u * 1024u;
    Arena arena(kArenaBytes + kPage);
    auto * base = reinterpret_cast<std::uint8_t *>((reinterpret_cast<std::uintptr_t>(arena.memory) + kPage - 1) & ~std::uintptr_t{ kPage - 1 });

    {
        Allocator misplaced;
        Check(!misplaced.Init(base + 16, base + kArenaBytes, 64 * 1024), "held-unaligned-refused");
        Allocator oversized;
        Check(!oversized.Init(base, base + kArenaBytes, kArenaBytes), "held-oversized-refused");
        Allocator headerOnly;
        Check(!headerOnly.Init(base, base + kArenaBytes, 16), "held-header-only-refused");
    }

    // An odd size: the block rounds up past it.
    static constexpr size_t kHeldBytes = 190u * 1024u + 37u;

    for (size_t index = 16; index < kHeldBytes; ++index)
    {
        base[index] = static_cast<std::uint8_t>(index * 7u + 3u);
    }

    Allocator allocator;
    Check(allocator.Init(base, base + kArenaBytes, kHeldBytes), "held-init");
    const auto held = allocator.GetTierStats();
    Check(held.largeBytes >= kHeldBytes - 16 && held.largeFreeBytes == 0, "held-is-one-used-block");

    std::vector<void *> pointers;
    bool outside = true;

    for (size_t size : { size_t{ 24 }, size_t{ 300 }, size_t{ 5000 }, size_t{ 70000 }, size_t{ 200 } })
    {
        size_t backing = 0;
        auto * pointer = static_cast<std::uint8_t *>(allocator.Alloc(size, 16, 9, backing));
        outside = outside && pointer != nullptr && (pointer + size <= base || pointer >= base + kHeldBytes);

        if (pointer != nullptr)
        {
            std::memset(pointer, 0xA5, size);
            pointers.push_back(pointer);
        }
    }

    bool intact = true;

    for (size_t index = 16; index < kHeldBytes; ++index)
    {
        intact = intact && base[index] == static_cast<std::uint8_t>(index * 7u + 3u);
    }

    Check(outside, "held-allocations-outside");
    Check(intact, "held-bytes-intact");

    const size_t released = allocator.ReleaseHeld();
    Check(released >= kHeldBytes - 16 && released < kHeldBytes + 16, "held-release-bytes");
    Check(allocator.ReleaseHeld() == 0, "held-release-once");

    // The released block sits below live large blocks, so a request of nearly its size fits it.
    size_t backing = 0;
    auto * reused = static_cast<std::uint8_t *>(allocator.Alloc(180u * 1024u, 16, 9, backing));
    Check(reused == base + 16, "held-space-reused");
    allocator.Free(reused);

    for (void * pointer : pointers)
    {
        allocator.Free(pointer);
    }

    Check(Drained(allocator.GetTierStats()), "held-drains");
}

// Random allocations, frees and reallocs; every live block keeps a fill byte that must survive.
void CheckRandomModel(std::uint32_t seed, size_t arenaBytes, int operations)
{
    Arena arena(arenaBytes);
    Allocator allocator;
    Check(allocator.Init(arena.memory, arena.memory + arena.size), "model-init");
    Xorshift random;
    random.state = seed;
    std::vector<Live> live;
    bool intact = true;
    bool aligned = true;
    bool disjoint = true;
    bool deterministic = true;
    int failures = 0;

    for (int operation = 0; operation < operations; ++operation)
    {
        const std::uint32_t choice = random.Below(100);

        if (choice < 55 || live.empty())
        {
            const std::uint32_t kind = random.Below(100);
            size_t size = kind < 70 ? random.Below(257) : kind < 95 ? 257 + random.Below(4000) : 4096 + random.Below(200000);
            const size_t alignment = random.Below(10) == 0 ? (size_t{ 32 } << random.Below(5)) : (size_t{ 4 } << random.Below(3));
            size_t backing = 0;
            auto * pointer = static_cast<std::uint8_t *>(allocator.Alloc(size, alignment, static_cast<std::uint8_t>(kind), backing));

            if (pointer == nullptr)
            {
                ++failures;
                continue;
            }

            aligned = aligned && reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0;
            deterministic = deterministic && backing == ExpectedBacking(size, alignment);
            const Live entry{ pointer, size, alignment, static_cast<std::uint8_t>(random.Next()) };
            FillLive(entry);
            live.push_back(entry);
        }
        else if (choice < 90)
        {
            const size_t index = random.Below(static_cast<std::uint32_t>(live.size()));
            intact = intact && Intact(live[index]);

#if PS2_D3BFG_ASSERTS
            std::uint8_t tag = 0;
            size_t requested = 0;
            allocator.GetTracking(live[index].pointer, tag, requested);
            intact = intact && requested == live[index].size;
#endif // PS2_D3BFG_ASSERTS

            allocator.Free(live[index].pointer);
            live[index] = live.back();
            live.pop_back();
        }
        else
        {
            const size_t index = random.Below(static_cast<std::uint32_t>(live.size()));
            Live & entry = live[index];
            intact = intact && Intact(entry);
            const size_t size = random.Below(2) == 0 ? entry.size / 2 : entry.size * 2 + random.Below(300);
            size_t oldBacking = 0;
            size_t newBacking = 0;
            auto * pointer = static_cast<std::uint8_t *>(allocator.Realloc(entry.pointer, size, entry.alignment, 3, oldBacking, newBacking));

            if (pointer == nullptr)
            {
                ++failures;
                continue;
            }

            // The kept prefix survives the move.
            for (size_t byte = 0; byte < std::min(size, entry.size); ++byte)
            {
                intact = intact && pointer[byte] == entry.fill;
            }

            aligned = aligned && reinterpret_cast<std::uintptr_t>(pointer) % entry.alignment == 0;
            deterministic = deterministic && newBacking == ExpectedBacking(size, entry.alignment);
            entry.pointer = pointer;
            entry.size = size;
            FillLive(entry);
        }

        if (operation % 5000 == 0)
        {
            std::vector<Live> sorted = live;
            std::sort(sorted.begin(), sorted.end(), [](const Live & a, const Live & b) { return a.pointer < b.pointer; });

            for (size_t index = 1; index < sorted.size(); ++index)
            {
                disjoint = disjoint && sorted[index - 1].pointer + allocator.UsableSize(sorted[index - 1].pointer) <= sorted[index].pointer;
            }
        }
    }

    for (const Live & entry : live)
    {
        intact = intact && Intact(entry);
        allocator.Free(entry.pointer);
    }

    const auto stats = allocator.GetTierStats();
    Check(intact, "model-intact");
    Check(aligned, "model-aligned");
    Check(disjoint, "model-disjoint");
    Check(deterministic, "model-backing-deterministic");
    Check(Drained(stats), "model-drains");
    std::printf("model seed=%08x ops=%d failures=%d peak_committed=%zu\n", seed, operations, failures, stats.peakCommittedBytes);
}

// Fills the arena with each tier in turn, frees everything, and expects the whole gap back.
void CheckExhaustion()
{
    const size_t arenaBytes = 2u * 1024u * 1024u;
    Arena arena(arenaBytes);
    Allocator allocator;
    allocator.Init(arena.memory, arena.memory + arena.size);
    std::vector<void *> pointers;
    size_t backing = 0;

    for (int round = 0; round < 3; ++round)
    {
        // Small slots of several classes until nothing is left.
        for (std::uint32_t size = 8;; size = size == 256 ? 8 : size + 8)
        {
            void * pointer = allocator.Alloc(size, 4, 1, backing);

            if (pointer == nullptr)
            {
                break;
            }

            pointers.push_back(pointer);
        }

        const auto full = allocator.GetTierStats();
        Check(full.gapBytes < 4096u, "small-fills-gap");

        for (void * pointer : pointers)
        {
            allocator.Free(pointer);
        }

        pointers.clear();

        // Large blocks until nothing is left; most of the arena must come back from the small tier.
        size_t largeTotal = 0;

        for (;;)
        {
            void * pointer = allocator.Alloc(3000, 16, 2, backing);

            if (pointer == nullptr)
            {
                break;
            }

            largeTotal += backing;
            pointers.push_back(pointer);
        }

        Check(largeTotal > arenaBytes - 52u * 4096u, "gap-returns-to-large");

        // Every other block freed: small requests now borrow pages from the large tier's holes.
        for (size_t index = 0; index < pointers.size(); index += 2)
        {
            allocator.Free(pointers[index]);
            pointers[index] = nullptr;
        }

        std::vector<void *> small;

        for (;;)
        {
            void * pointer = allocator.Alloc(64, 16, 3, backing);

            if (pointer == nullptr)
            {
                break;
            }

            small.push_back(pointer);
        }

        Check(allocator.GetTierStats().smallBorrowedPages == 0, "holes-too-small-to-borrow");

        for (void * pointer : small)
        {
            allocator.Free(pointer);
        }

        for (void * pointer : pointers)
        {
            if (pointer != nullptr)
            {
                allocator.Free(pointer);
            }
        }

        pointers.clear();
    }

    // Large blocks with holes between them that fit a page-aligned page: the small tier borrows them.
    for (;;)
    {
        void * pointer = allocator.Alloc(16384, 16, 4, backing);

        if (pointer == nullptr)
        {
            break;
        }

        pointers.push_back(pointer);
    }

    for (size_t index = 0; index < pointers.size(); index += 2)
    {
        allocator.Free(pointers[index]);
        pointers[index] = nullptr;
    }

    std::vector<void *> small;

    for (int count = 0; count < 4000; ++count)
    {
        void * pointer = allocator.Alloc(32, 4, 5, backing);

        if (pointer == nullptr)
        {
            break;
        }

        small.push_back(pointer);
    }

    Check(allocator.GetTierStats().smallBorrowedPages > 0, "small-borrows-large-holes");

    for (void * pointer : small)
    {
        allocator.Free(pointer);
    }

    for (void * pointer : pointers)
    {
        if (pointer != nullptr)
        {
            allocator.Free(pointer);
        }
    }

    const auto stats = allocator.GetTierStats();
    Check(Drained(stats), "exhaustion-drains");
}

void CheckReallocInPlace()
{
    Arena arena(1u * 1024u * 1024u);
    Allocator allocator;
    allocator.Init(arena.memory, arena.memory + arena.size);
    size_t backing = 0;
    size_t oldBacking = 0;
    size_t newBacking = 0;

    // The top block grows into the gap without moving.
    void * top = allocator.Alloc(1000, 16, 1, backing);
    void * grown = allocator.Realloc(top, 50000, 16, 1, oldBacking, newBacking);
    Check(grown == top && newBacking >= 50000, "realloc-grows-top");

    // A block grows into its free neighbour.
    void * second = allocator.Alloc(1000, 16, 1, backing);
    void * third = allocator.Alloc(1000, 16, 1, backing);
    void * guard = allocator.Alloc(1000, 16, 1, backing);
    allocator.Free(third);
    void * merged = allocator.Realloc(second, 1800, 16, 1, oldBacking, newBacking);
    Check(merged == second, "realloc-merges-next");

    // A small slot stays in its class, and moves when the class changes.
    void * slot = allocator.Alloc(30, 4, 1, backing);
    Check(allocator.Realloc(slot, 31, 4, 1, oldBacking, newBacking) == slot, "realloc-same-class");
    void * moved = allocator.Realloc(slot, 200, 4, 1, oldBacking, newBacking);
    Check(moved != slot && newBacking == 200, "realloc-new-class");

    allocator.Free(moved);
    allocator.Free(merged);
    allocator.Free(guard);
    allocator.Free(grown);
    Check(Drained(allocator.GetTierStats()), "realloc-drains");
}

// Tracking builds: each mode seeds one defect, which must end the process in its fatal.
int RunDefect(const char * mode)
{
    Arena arena(1u * 1024u * 1024u);
    Allocator allocator;
    allocator.Init(arena.memory, arena.memory + arena.size);
    size_t backing = 0;
    void * small = allocator.Alloc(64, 16, 1, backing);
    void * large = allocator.Alloc(5000, 16, 1, backing);
    void * guard = allocator.Alloc(5000, 16, 1, backing);
    static_cast<void>(guard);

    if (std::strcmp(mode, "small-write-after-free") == 0)
    {
        allocator.Free(small);
        static_cast<std::uint8_t *>(small)[40] = 1;
        allocator.Alloc(64, 16, 1, backing);
    }
    else if (std::strcmp(mode, "large-write-after-free") == 0)
    {
        // TLSF rounds a search up to the next bin, so a smaller request is what reuses the block.
        allocator.Free(large);
        static_cast<std::uint8_t *>(large)[100] = 1;
        allocator.Alloc(4000, 16, 1, backing);
    }
    else if (std::strcmp(mode, "small-double-free") == 0)
    {
        allocator.Free(small);
        allocator.Free(small);
    }
    else if (std::strcmp(mode, "large-double-free") == 0)
    {
        allocator.Free(large);
        allocator.Free(large);
    }
    else
    {
        return 2;
    }

    std::printf("returned\n");
    return 0;
}

} // namespace

int main(int argc, char ** argv)
{
    if (argc == 2)
    {
        return RunDefect(argv[1]);
    }

    CheckClassTable();
    CheckAlignmentAndSizes();
    CheckReallocInPlace();
    CheckExhaustion();
    CheckHeldBlock();
    CheckRandomModel(0x12345678u, 32u * 1024u * 1024u, 300000);
    CheckRandomModel(0x0BADF00Du, 3u * 1024u * 1024u, 300000);
    CheckRandomModel(0xC0FFEE11u, 600u * 1024u, 200000);
    std::printf("heap core tests: %s\n", s_failures == 0 ? "PASS" : "FAIL");
    return s_failures == 0 ? 0 : 1;
}
