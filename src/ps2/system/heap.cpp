// ================================================================================================
// File: heap.cpp
// Brief: The heap's public API, every operator new/delete form and the C library's allocation entry
//        points, over one Allocator that takes all of sbrk's memory before libc or any constructor
//        runs. A Ledger counts exact backing bytes; tracking builds add tags and requested sizes.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/heap.h"
#include "ps2/system/heap_allocator.h"
#include "ps2/system/log.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <new>

#if defined(_EE)
    #include <kernel.h>
    #include <unistd.h>
#endif // _EE

namespace ps2::system::heap
{
namespace
{

// ------------------------------------------------------------------------------------------------
// Ledger
// ------------------------------------------------------------------------------------------------

// Exact backing bytes and counts for tagged and C library allocations, kept apart: newlib holds
// stdio buffers across shutdown. Tracking builds add requested sizes and a ledger per tag.
class Ledger
{
public:
    void AddTagged(std::uint16_t tag, size_t requested, size_t backing)
    {
        Add(m_total, requested, backing);

#if PS2_D3BFG_ASSERTS
        Add(m_tags[tag], requested, backing);
#else // PS2_D3BFG_ASSERTS
        static_cast<void>(tag);
#endif // PS2_D3BFG_ASSERTS
    }

    void RemoveTagged(std::uint16_t tag, size_t requested, size_t backing)
    {
        Remove(m_total, requested, backing);

#if PS2_D3BFG_ASSERTS
        Remove(m_tags[tag], requested, backing);
#else // PS2_D3BFG_ASSERTS
        static_cast<void>(tag);
#endif // PS2_D3BFG_ASSERTS
    }

    void AddLibc(size_t requested, size_t backing)
    {
        Add(m_libc, requested, backing);
    }

    void RemoveLibc(size_t requested, size_t backing)
    {
        Remove(m_libc, requested, backing);
    }

    const Stats & Total() const
    {
        return m_total;
    }

    const Stats & Libc() const
    {
        return m_libc;
    }

#if PS2_D3BFG_ASSERTS
    const Stats & Tag(std::uint16_t tag) const
    {
        return m_tags[tag];
    }
#endif // PS2_D3BFG_ASSERTS

    PS2_COLD_FUNC void ResetPeaks()
    {
        ResetPeak(m_total);
        ResetPeak(m_libc);

#if PS2_D3BFG_ASSERTS
        for (Stats & stats : m_tags)
        {
            ResetPeak(stats);
        }
#endif // PS2_D3BFG_ASSERTS
    }

private:
    static void Add(Stats & stats, size_t requested, size_t backing)
    {
        stats.requestedBytes += requested;
        stats.backingBytes += backing;
        ++stats.allocationCount;

        if (stats.requestedBytes > stats.peakRequestedBytes)
        {
            stats.peakRequestedBytes = stats.requestedBytes;
        }

        if (stats.backingBytes > stats.peakBackingBytes)
        {
            stats.peakBackingBytes = stats.backingBytes;
        }
    }

    static void Remove(Stats & stats, size_t requested, size_t backing)
    {
        PS2_AssertMsg(stats.requestedBytes >= requested && stats.backingBytes >= backing && stats.allocationCount != 0,
                      "heap ledger underflow");
        stats.requestedBytes -= requested;
        stats.backingBytes -= backing;
        --stats.allocationCount;
    }

    static void ResetPeak(Stats & stats)
    {
        stats.peakRequestedBytes = stats.requestedBytes;
        stats.peakBackingBytes = stats.backingBytes;
    }

    Stats m_total = {};
    Stats m_libc = {};

#if PS2_D3BFG_ASSERTS
    Stats m_tags[kTagCount] = {};
#endif // PS2_D3BFG_ASSERTS
};

// ------------------------------------------------------------------------------------------------
// Singletons
// ------------------------------------------------------------------------------------------------

// Constant-initialized: both are ready before any constructor, and neither has a destructor to run.
constinit static Allocator s_allocator;
constinit static Ledger s_ledger;

#if PS2_D3BFG_ASSERTS
static bool s_mainEntered = false;

    #if defined(_EE)
static s32 s_ownerThread = -1;

// The stack's lowest address, which is the arena's end, and the paint EnterMain writes below the
// live frames. The guard is the stack's bottom 1 KiB: an overflow past it reaches the arena.
static std::uintptr_t s_stackBottom = 0;
static constexpr std::uint32_t kStackPattern = 0x57AC57ACu;
static constexpr size_t kStackGuardBytes = 1024;
    #endif // _EE
#endif     // PS2_D3BFG_ASSERTS

#if !defined(_EE)
// Host tests run the real allocator over a static arena, set up by the first allocation.
alignas(4096) static std::uint8_t s_hostArena[kMaxArenaBytes];
#endif // !_EE

PS2_ALWAYS_INLINE void EnsureArena()
{
#if !defined(_EE)
    if (!s_allocator.IsInitialized()) [[unlikely]]
    {
        s_allocator.Init(s_hostArena, s_hostArena + sizeof(s_hostArena));
    }
#endif // !_EE
}

PS2_ALWAYS_INLINE size_t NaturalAlignment(size_t size)
{
    const size_t rounded = (size + 3) & ~size_t{ 3 };
    const size_t lowBit = rounded & (size_t{ 0 } - rounded);

    if (lowBit == 0)
    {
        return 4;
    }

    return lowBit > kGranule ? size_t{ kGranule } : lowBit;
}

PS2_ALWAYS_INLINE size_t EffectiveAlignment(size_t size, size_t alignment)
{
    PS2_AssertMsg((alignment & (alignment - 1)) == 0, "heap alignment must be zero or a power of two");

    if (alignment == kNaturalAlignment)
    {
        return NaturalAlignment(size);
    }

    return alignment < 4 ? 4 : alignment;
}

// The failed request and every live tag, so an exhausted budget names its owners. Formats only
// integers: newlib's floating-point conversion allocates.
[[noreturn]] PS2_COLD_FUNC void FailAllocation(size_t size, std::uint16_t tag, size_t alignment)
{
#if PS2_D3BFG_ASSERTS
    for (std::uint16_t index = 0; index < kTagCount; ++index)
    {
        const Stats & stats = s_ledger.Tag(index);

        if (stats.allocationCount != 0)
        {
            ps2::Log(ps2::LogLevel::Error, "heap tag=%u requested=%zu backing=%zu allocations=%zu peak_requested=%zu\n",
                     static_cast<unsigned>(index), stats.requestedBytes, stats.backingBytes, stats.allocationCount,
                     stats.peakRequestedBytes);
        }
    }
#endif // PS2_D3BFG_ASSERTS

    const Stats & total = s_ledger.Total();
    const ArenaStats arena = GetArenaStats();
    ps2::FatalError("heap: cannot allocate %zu bytes (tag %u, alignment %zu); live %zu backing bytes in %zu allocations, "
                    "%zu C library bytes; arena %zu committed, %zu gap, %zu largest free block, %zu small free pages",
                    size, static_cast<unsigned>(tag), alignment, total.backingBytes, total.allocationCount,
                    s_ledger.Libc().backingBytes, arena.committedBytes, arena.gapBytes, arena.largestFreeBlock,
                    arena.smallFreePages);
}

#if defined(_EE)

void * LibcAlloc(size_t size, size_t alignment)
{
    // C callers allocate header-plus-payload structs, so 8 is the floor.
    const size_t natural = NaturalAlignment(size);
    size_t effective = alignment > natural ? alignment : natural;
    effective = effective < 8 ? 8 : effective;

    size_t backing = 0;
    void * pointer = s_allocator.Alloc(size, effective, static_cast<std::uint8_t>(kLibcTag), backing);

    if (pointer == nullptr) [[unlikely]]
    {
        errno = ENOMEM;
        return nullptr;
    }

    s_ledger.AddLibc(PS2_D3BFG_ASSERTS ? size : backing, backing);
    return pointer;
}

void LibcFree(void * pointer)
{
    if (pointer == nullptr)
    {
        return;
    }

    const Released released = s_allocator.Free(pointer);

    #if PS2_D3BFG_ASSERTS
    if (released.tag != kLibcTag) [[unlikely]]
    {
        FatalError("heap: free() of %p, a tag %u heap allocation", pointer, static_cast<unsigned>(released.tag));
    }

    s_ledger.RemoveLibc(released.requested, released.backing);
    #else // PS2_D3BFG_ASSERTS
    s_ledger.RemoveLibc(released.backing, released.backing);
    #endif // PS2_D3BFG_ASSERTS
}

void * LibcRealloc(void * pointer, size_t size)
{
    if (pointer == nullptr)
    {
        return LibcAlloc(size, 8);
    }

    if (size == 0)
    {
        LibcFree(pointer);
        return nullptr;
    }

    #if PS2_D3BFG_ASSERTS
    std::uint8_t tag = 0;
    size_t oldRequested = 0;
    s_allocator.GetTracking(pointer, tag, oldRequested);

    if (tag != kLibcTag) [[unlikely]]
    {
        FatalError("heap: realloc() of %p, a tag %u heap allocation", pointer, static_cast<unsigned>(tag));
    }
    #endif // PS2_D3BFG_ASSERTS

    const size_t natural = NaturalAlignment(size);
    size_t oldBacking = 0;
    size_t newBacking = 0;
    void * moved = s_allocator.Realloc(pointer, size, natural < 8 ? 8 : natural, static_cast<std::uint8_t>(kLibcTag),
                                       oldBacking, newBacking);

    if (moved == nullptr) [[unlikely]]
    {
        errno = ENOMEM;
        return nullptr;
    }

    #if PS2_D3BFG_ASSERTS
    s_ledger.RemoveLibc(oldRequested, oldBacking);
    s_ledger.AddLibc(size, newBacking);
    #else // PS2_D3BFG_ASSERTS
    s_ledger.RemoveLibc(oldBacking, oldBacking);
    s_ledger.AddLibc(newBacking, newBacking);
    #endif // PS2_D3BFG_ASSERTS

    return moved;
}

void * LibcCalloc(size_t count, size_t size)
{
    if (size != 0 && count > static_cast<size_t>(-1) / size) [[unlikely]]
    {
        errno = ENOMEM;
        return nullptr;
    }

    void * pointer = LibcAlloc(count * size, 8);

    if (pointer != nullptr)
    {
        std::memset(pointer, 0, count * size);
    }

    return pointer;
}

#endif // _EE

} // namespace

// ------------------------------------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------------------------------------

[[noreturn]] PS2_COLD_FUNC void Fail(const char * reason)
{
    // Uses no C++ allocation, including when invoked before main or during OOM.
    ps2::FatalError("heap: %s", reason);
}

PS2_COLD_FUNC void EnterMain()
{
#if PS2_D3BFG_ASSERTS
    s_mainEntered = true;

    #if defined(_EE)
    s_ownerThread = GetThreadId();

    // Paint up to 2 KiB below this frame; main's caller and crt0 sit above it.
    const auto live = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0)) - 2048;
    auto * words = reinterpret_cast<volatile std::uint32_t *>(s_stackBottom);

    for (std::uintptr_t address = s_stackBottom; address < live; address += 4)
    {
        *words++ = kStackPattern;
    }
    #endif // _EE
#endif     // PS2_D3BFG_ASSERTS
}

#if PS2_D3BFG_ASSERTS

void CheckStack()
{
    #if defined(_EE)
    const auto * words = reinterpret_cast<const volatile std::uint32_t *>(s_stackBottom);

    for (size_t word = 0; word < kStackGuardBytes / 4; ++word)
    {
        if (words[word] != kStackPattern) [[unlikely]]
        {
            FatalError("heap: the main stack (%zu bytes) overflowed into its bottom guard at %p; the arena below it is corrupt",
                       kMainStackBytes, static_cast<const void *>(const_cast<const std::uint32_t *>(words + word)));
        }
    }
    #endif // _EE
}

size_t GetStackHeadroom()
{
    #if defined(_EE)
    const auto * words = reinterpret_cast<const volatile std::uint32_t *>(s_stackBottom);
    size_t untouched = 0;

    while (untouched < kMainStackBytes / 4 && words[untouched] == kStackPattern)
    {
        ++untouched;
    }

    return untouched * 4;
    #else // _EE
    return 0;
    #endif // _EE
}

#endif // PS2_D3BFG_ASSERTS

void * TryAlloc(size_t size, std::uint16_t tag, size_t alignment)
{
    EnsureArena();
    PS2_AssertMsg(tag < kTagCount && tag != kLibcTag, "heap allocation with an invalid tag");

#if PS2_D3BFG_ASSERTS && defined(_EE)
    if (!s_mainEntered) [[unlikely]]
    {
        FatalError("heap: %zu-byte tag %u allocation before main, called from %p; give its global ps2::DeferredObject storage",
                   size, static_cast<unsigned>(tag), __builtin_return_address(0));
    }

    PS2_AssertMsg(GetThreadId() == s_ownerThread, "heap used from a thread other than main's");
#endif // PS2_D3BFG_ASSERTS && _EE

    size_t backing = 0;
    void * pointer = s_allocator.Alloc(size, EffectiveAlignment(size, alignment), static_cast<std::uint8_t>(tag), backing);

    if (pointer != nullptr) [[likely]]
    {
        s_ledger.AddTagged(tag, PS2_D3BFG_ASSERTS ? size : backing, backing);
    }

    return pointer;
}

void * Alloc(size_t size, std::uint16_t tag, size_t alignment)
{
    void * pointer = TryAlloc(size, tag, alignment);

    if (pointer == nullptr) [[unlikely]]
    {
        FailAllocation(size, tag, alignment);
    }

    return pointer;
}

void Free(void * pointer)
{
    if (pointer == nullptr)
    {
        return;
    }

#if PS2_D3BFG_ASSERTS && defined(_EE)
    PS2_AssertMsg(GetThreadId() == s_ownerThread, "heap used from a thread other than main's");
#endif // PS2_D3BFG_ASSERTS && _EE

    const Released released = s_allocator.Free(pointer);

#if PS2_D3BFG_ASSERTS
    if (released.tag == kLibcTag) [[unlikely]]
    {
        FatalError("heap: heap::Free of %p, a C library allocation", pointer);
    }

    s_ledger.RemoveTagged(released.tag, released.requested, released.backing);
#else // PS2_D3BFG_ASSERTS
    s_ledger.RemoveTagged(0, released.backing, released.backing);
#endif // PS2_D3BFG_ASSERTS
}

Stats GetTotalStats()
{
    return s_ledger.Total();
}

Stats GetLibcStats()
{
    return s_ledger.Libc();
}

#if PS2_D3BFG_ASSERTS

Stats GetStats(std::uint16_t tag)
{
    return tag < kTagCount ? s_ledger.Tag(tag) : Stats{};
}

size_t GetRequestedSize(const void * pointer)
{
    if (pointer == nullptr)
    {
        return 0;
    }

    std::uint8_t tag = 0;
    size_t requested = 0;
    s_allocator.GetTracking(pointer, tag, requested);
    return requested;
}

#endif // PS2_D3BFG_ASSERTS

ArenaStats GetArenaStats()
{
    EnsureArena();
    const TierStats tiers = s_allocator.GetTierStats();
    const size_t used = s_ledger.Total().backingBytes + s_ledger.Libc().backingBytes;

    ArenaStats stats{};
    stats.available = s_allocator.IsInitialized();
    stats.committedBytes = tiers.committedBytes;
    stats.usedBytes = used;
    stats.freeBytes = tiers.committedBytes > used ? tiers.committedBytes - used : 0;
    stats.peakCommittedBytes = tiers.peakCommittedBytes;
    stats.gapBytes = tiers.gapBytes;
    stats.smallPages = tiers.smallPages;
    stats.smallFreePages = tiers.smallFreePages;
    stats.smallBorrowedPages = tiers.smallBorrowedPages;
    stats.largeFreeBytes = tiers.largeFreeBytes;
    stats.largestFreeBlock = tiers.largestFreeBlock;
    stats.metadataBytes = tiers.metadataBytes;
    return stats;
}

PS2_COLD_FUNC void ResetPeaks()
{
    s_ledger.ResetPeaks();
}

} // namespace ps2::system::heap

// ------------------------------------------------------------------------------------------------
// The land grab
// ------------------------------------------------------------------------------------------------

#if defined(_EE)

// The asm directive below needs the value as a literal.
#define PS2_MAIN_STACK_BYTES 262144
static_assert(PS2_MAIN_STACK_BYTES == ps2::system::heap::kMainStackBytes);

// crt0 calls this weak hook right after SetupHeap, before libcglue's startup and every global
// constructor. All of sbrk's range goes to the allocator, so any later sbrk caller fails.
// crt0 passes this symbol's value to SetupThread as the main stack's size; the SDK linkfile only
// PROVIDEs 128 KiB, so this definition wins.
#define PS2_STRINGIFY_VALUE(value) #value
#define PS2_STRINGIFY(value) PS2_STRINGIFY_VALUE(value)
asm(".globl _stack_size\n\t.set _stack_size, " PS2_STRINGIFY(PS2_MAIN_STACK_BYTES));

extern "C" void _ps2sdk_memory_init()
{
    void * begin = sbrk(0);
    const ptrdiff_t bytes = static_cast<std::uint8_t *>(EndOfHeap()) - static_cast<std::uint8_t *>(begin);

    #if PS2_D3BFG_ASSERTS
    ps2::system::heap::s_stackBottom = reinterpret_cast<std::uintptr_t>(EndOfHeap());
    #endif // PS2_D3BFG_ASSERTS

    if (bytes > 0 && sbrk(bytes) == begin) [[likely]]
    {
        ps2::system::heap::s_allocator.Init(begin, static_cast<std::uint8_t *>(begin) + bytes);
    }
}

#endif // _EE

// ------------------------------------------------------------------------------------------------
// operator new and delete
// ------------------------------------------------------------------------------------------------

// Standard, sized, nothrow and over-aligned forms share one ledger. A zero-size new is a bug.
void * operator new(size_t size)
{
    PS2_AssertMsg(size != 0, "operator new of zero bytes");
    return ps2::system::heap::Alloc(size, ps2::system::heap::kNewTag);
}

void * operator new[](size_t size)
{
    PS2_AssertMsg(size != 0, "operator new[] of zero bytes");
    return ps2::system::heap::Alloc(size, ps2::system::heap::kNewTag);
}

void operator delete(void * pointer) noexcept
{
    ps2::system::heap::Free(pointer);
}

void operator delete[](void * pointer) noexcept
{
    ps2::system::heap::Free(pointer);
}

void operator delete(void * pointer, size_t) noexcept
{
    ps2::system::heap::Free(pointer);
}

void operator delete[](void * pointer, size_t) noexcept
{
    ps2::system::heap::Free(pointer);
}

void * operator new(size_t size, const std::nothrow_t &) noexcept
{
    PS2_AssertMsg(size != 0, "operator new of zero bytes");
    return ps2::system::heap::TryAlloc(size, ps2::system::heap::kNewTag);
}

void * operator new[](size_t size, const std::nothrow_t &) noexcept
{
    PS2_AssertMsg(size != 0, "operator new[] of zero bytes");
    return ps2::system::heap::TryAlloc(size, ps2::system::heap::kNewTag);
}

void operator delete(void * pointer, const std::nothrow_t &) noexcept
{
    ps2::system::heap::Free(pointer);
}

void operator delete[](void * pointer, const std::nothrow_t &) noexcept
{
    ps2::system::heap::Free(pointer);
}

void * operator new(size_t size, std::align_val_t alignment)
{
    PS2_AssertMsg(size != 0, "operator new of zero bytes");
    return ps2::system::heap::Alloc(size, ps2::system::heap::kNewTag, static_cast<size_t>(alignment));
}

void * operator new[](size_t size, std::align_val_t alignment)
{
    PS2_AssertMsg(size != 0, "operator new[] of zero bytes");
    return ps2::system::heap::Alloc(size, ps2::system::heap::kNewTag, static_cast<size_t>(alignment));
}

void operator delete(void * pointer, std::align_val_t) noexcept
{
    ps2::system::heap::Free(pointer);
}

void operator delete[](void * pointer, std::align_val_t) noexcept
{
    ps2::system::heap::Free(pointer);
}

void operator delete(void * pointer, size_t, std::align_val_t) noexcept
{
    ps2::system::heap::Free(pointer);
}

void operator delete[](void * pointer, size_t, std::align_val_t) noexcept
{
    ps2::system::heap::Free(pointer);
}

void * operator new(size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept
{
    PS2_AssertMsg(size != 0, "operator new of zero bytes");
    return ps2::system::heap::TryAlloc(size, ps2::system::heap::kNewTag, static_cast<size_t>(alignment));
}

void * operator new[](size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept
{
    PS2_AssertMsg(size != 0, "operator new[] of zero bytes");
    return ps2::system::heap::TryAlloc(size, ps2::system::heap::kNewTag, static_cast<size_t>(alignment));
}

void operator delete(void * pointer, std::align_val_t, const std::nothrow_t &) noexcept
{
    ps2::system::heap::Free(pointer);
}

void operator delete[](void * pointer, std::align_val_t, const std::nothrow_t &) noexcept
{
    ps2::system::heap::Free(pointer);
}

// ------------------------------------------------------------------------------------------------
// The C library
// ------------------------------------------------------------------------------------------------

#if defined(_EE)

// Defining newlib's reentrant entry points too keeps its own malloc out of the link, so libc
// internals (stdio buffers and the rest) share this arena.
struct _reent;

extern "C"
{

void * malloc(size_t size)
{
    return ps2::system::heap::LibcAlloc(size, 8);
}

void free(void * pointer)
{
    ps2::system::heap::LibcFree(pointer);
}

void * calloc(size_t count, size_t size)
{
    return ps2::system::heap::LibcCalloc(count, size);
}

void * realloc(void * pointer, size_t size)
{
    return ps2::system::heap::LibcRealloc(pointer, size);
}

void * memalign(size_t alignment, size_t size)
{
    return ps2::system::heap::LibcAlloc(size, alignment);
}

void * aligned_alloc(size_t alignment, size_t size)
{
    return ps2::system::heap::LibcAlloc(size, alignment);
}

int posix_memalign(void ** result, size_t alignment, size_t size)
{
    void * pointer = ps2::system::heap::LibcAlloc(size, alignment);

    if (pointer == nullptr)
    {
        return ENOMEM;
    }

    *result = pointer;
    return 0;
}

void * valloc(size_t size)
{
    return ps2::system::heap::LibcAlloc(size, ps2::system::heap::kPageBytes);
}

void cfree(void * pointer)
{
    ps2::system::heap::LibcFree(pointer);
}

size_t malloc_usable_size(void * pointer)
{
    return pointer == nullptr ? 0 : ps2::system::heap::s_allocator.UsableSize(pointer);
}

void * _malloc_r(_reent *, size_t size)
{
    return ps2::system::heap::LibcAlloc(size, 8);
}

void _free_r(_reent *, void * pointer)
{
    ps2::system::heap::LibcFree(pointer);
}

void * _calloc_r(_reent *, size_t count, size_t size)
{
    return ps2::system::heap::LibcCalloc(count, size);
}

void * _realloc_r(_reent *, void * pointer, size_t size)
{
    return ps2::system::heap::LibcRealloc(pointer, size);
}

void * _memalign_r(_reent *, size_t alignment, size_t size)
{
    return ps2::system::heap::LibcAlloc(size, alignment);
}

void * _valloc_r(_reent *, size_t size)
{
    return ps2::system::heap::LibcAlloc(size, ps2::system::heap::kPageBytes);
}

} // extern "C"

#endif // _EE
