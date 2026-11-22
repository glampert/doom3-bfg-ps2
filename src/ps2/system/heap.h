// ================================================================================================
// File: heap.h
// Brief: The one allocation boundary: tagged engine allocations, every operator new form and the C
//        library share one arena. Tracking builds keep each allocation's tag and requested size.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include <cstddef>
#include <cstdint>

namespace ps2::system::heap
{

static constexpr std::uint16_t kTagCount = 256;
static constexpr std::uint16_t kNewTag = 3; // Doom's TAG_NEW; verified by lifecycle.cpp.

// Doom's TAG_SYSTEM, for backend code without the engine's headers; verified by lifecycle.cpp.
static constexpr std::uint16_t kSystemTag = 88;

// The C library's allocations; counted apart from the tagged ledger.
static constexpr std::uint16_t kLibcTag = 255;

// The lowest set bit of the size rounded up to 4, capped at 16: enough for any T with sizeof(T)
// equal to the size, since alignof(T) divides sizeof(T).
static constexpr size_t kNaturalAlignment = 0;

// Null when the arena has no room, a size beyond it included. Alignments are 0 (natural) or powers
// of two; a bad alignment or tag is an assertion.
void * TryAlloc(size_t size, std::uint16_t tag, size_t alignment = kNaturalAlignment);
void * Alloc(size_t size, std::uint16_t tag, size_t alignment = kNaturalAlignment);
void Free(void * pointer);

[[noreturn]] PS2_COLD_FUNC void Fail(const char * reason);

// Called first by every EE main. Tracking builds refuse a tagged allocation before it: a global
// whose constructor allocates needs ps2::DeferredObject storage (deferred_object.h).
PS2_COLD_FUNC void EnterMain();

// The linker's boot data (config/linkfile.ld: the IOP storage drivers) is the arena's first block
// from before main, so the ELF loader's copy survives until the IOP boot has started it. This
// hands it to the large tier and returns its bytes; later calls, and images without boot data,
// return 0. It never enters the ledger.
PS2_COLD_FUNC size_t ReleaseBootData();

// The main thread's stack sits right above the arena, whose top holds the small tier's first
// pages, so an overflow corrupts them. Tracking builds paint the stack in EnterMain; CheckStack is
// fatal once the paint at its bottom is gone, and GetStackHeadroom is the paint still untouched.
static constexpr size_t kMainStackBytes = 256u * 1024u;

#if PS2_D3BFG_ASSERTS
void CheckStack();
size_t GetStackHeadroom();
#endif // PS2_D3BFG_ASSERTS

// Release builds keep no requested sizes: there `requestedBytes` counts the backing bytes.
struct Stats
{
    size_t requestedBytes;
    size_t backingBytes;
    size_t allocationCount;
    size_t peakRequestedBytes;
    size_t peakBackingBytes;
};

Stats GetTotalStats();
Stats GetLibcStats();

#if PS2_D3BFG_ASSERTS
// Per-tag ledgers and requested sizes exist in tracking builds only.
Stats GetStats(std::uint16_t tag);

// Requires a live allocation from this heap, or nullptr, which reports zero.
size_t GetRequestedSize(const void * pointer);
#endif // PS2_D3BFG_ASSERTS

struct ArenaStats
{
    bool available;
    size_t committedBytes; // the arena less the gap between the tiers
    size_t usedBytes;      // live backing, tagged and C library
    size_t freeBytes;      // committed but not live: free slots, pages and blocks, headers, metadata
    size_t peakCommittedBytes;
    size_t gapBytes;
    size_t smallPages;
    size_t smallFreePages;
    size_t smallBorrowedPages;
    size_t largeFreeBytes;
    size_t largestFreeBlock;
    size_t metadataBytes;
};

ArenaStats GetArenaStats();
PS2_COLD_FUNC void ResetPeaks();

} // namespace ps2::system::heap
