// ================================================================================================
// File: heap_tests.cpp
// Brief: Check alignment, the tier boundary, overflow and exact ledger recovery across the standard
//        allocation APIs; tracking builds check per-tag ledgers and requested sizes too.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/log.h"

#include "ps2/system/heap.h"
#include "tests/smoketests/heap_tests.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <new>

namespace ps2::tests
{
namespace
{

bool Check(bool condition, const char * name)
{
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] CHECK %s %s\n", name, condition ? "PASS" : "FAIL");
    return condition;
}

// The tagged ledger a check reads: its own tag in tracking builds, the total otherwise (release builds
// keep no per-tag ledger and count backing bytes as requested).
system::heap::Stats Ledger(std::uint16_t tag)
{
#if PS2_D3BFG_ASSERTS
    return system::heap::GetStats(tag);
#else // PS2_D3BFG_ASSERTS
    static_cast<void>(tag);
    return system::heap::GetTotalStats();
#endif // PS2_D3BFG_ASSERTS
}

} // namespace

bool RunHeapTests()
{
    using namespace ps2::system::heap;
    bool passed = true;
    const Stats baseline = GetTotalStats();
    constexpr std::uint16_t kTestTag = 200;

    for (const size_t alignment : { size_t{ 16 }, size_t{ 64 }, size_t{ 256 }, size_t{ 4096 } })
    {
        const Stats before = Ledger(kTestTag);
        void * allocation = TryAlloc(37, kTestTag, alignment);
        passed &= Check(allocation != nullptr && reinterpret_cast<std::uintptr_t>(allocation) % alignment == 0, "alignment");

        const Stats stats = Ledger(kTestTag);
        const size_t backing = stats.backingBytes - before.backingBytes;
        const size_t requested = stats.requestedBytes - before.requestedBytes;
        passed &= Check(backing >= 37 && stats.allocationCount == before.allocationCount + 1 &&
                            (PS2_D3BFG_ASSERTS ? requested == 37 : requested == backing),
                        "tag-accounting");

        Free(allocation);
        const Stats after = Ledger(kTestTag);
        passed &= Check(after.requestedBytes == before.requestedBytes && after.backingBytes == before.backingBytes &&
                            after.allocationCount == before.allocationCount,
                        "unsized-free");
    }

    // Natural alignment: the size's lowest set bit after rounding up to 4, capped at 16.
    bool natural = true;

    for (const size_t size : { size_t{ 1 }, size_t{ 4 }, size_t{ 8 }, size_t{ 12 }, size_t{ 24 }, size_t{ 40 }, size_t{ 48 },
                               size_t{ 92 }, size_t{ 200 }, size_t{ 256 }, size_t{ 1000 }, size_t{ 4096 } })
    {
        const size_t rounded = (size + 3) & ~size_t{ 3 };
        const size_t lowBit = rounded & (size_t{ 0 } - rounded);
        const size_t expected = lowBit > 16 ? 16 : lowBit;
        void * allocation = TryAlloc(size, kTestTag);
        natural = natural && allocation != nullptr && reinterpret_cast<std::uintptr_t>(allocation) % expected == 0;
        Free(allocation);
    }

    passed &= Check(natural, "natural-alignment");

    // The largest small class and the smallest large block, both 16-aligned when asked.
    {
        const Stats before = GetTotalStats();
        void * small = TryAlloc(256, kTestTag, 16);
        const size_t smallBacking = GetTotalStats().backingBytes - before.backingBytes;
        void * large = TryAlloc(257, kTestTag, 16);
        const size_t largeBacking = GetTotalStats().backingBytes - before.backingBytes - smallBacking;
        passed &= Check(small != nullptr && large != nullptr && smallBacking == 256 && largeBacking >= 257 + 8 &&
                            reinterpret_cast<std::uintptr_t>(small) % 16 == 0 && reinterpret_cast<std::uintptr_t>(large) % 16 == 0,
                        "tier-boundary");
        Free(large);
        Free(small);
    }

    // A size beyond the arena fails without touching the ledger; a bad tag or alignment is an assertion.
    passed &= Check(TryAlloc(std::numeric_limits<size_t>::max(), kTestTag) == nullptr &&
                        TryAlloc(std::numeric_limits<size_t>::max() - 64, kTestTag, 4096) == nullptr,
                    "invalid-requests");

#if defined(_EE)
    // Exercise the program-wide C allocator, including its newlib entry points' common implementation.
    volatile size_t overflowCount = std::numeric_limits<size_t>::max() / 2 + 1;
    // GCC can remove a direct calloc/free pair and its errno observation as a builtin.
    void * (*volatile allocate)(size_t, size_t) = std::calloc;
    errno = 0;
    void * overflow = allocate(overflowCount, 2);
    passed &= Check(overflow == nullptr && errno == ENOMEM, "calloc-overflow");
    std::free(overflow);

    // The C library's allocations stay out of the tagged ledger.
    const Stats libcBefore = GetLibcStats();
    const Stats taggedBefore = GetTotalStats();
    void * (*volatile allocateBytes)(size_t) = std::malloc;
    void * bytes = allocateBytes(100);
    const bool counted = bytes != nullptr && GetLibcStats().allocationCount == libcBefore.allocationCount + 1 &&
                         GetTotalStats().allocationCount == taggedBefore.allocationCount &&
                         reinterpret_cast<std::uintptr_t>(bytes) % 8 == 0;
    std::free(bytes);
    passed &= Check(counted && GetLibcStats().allocationCount == libcBefore.allocationCount &&
                        GetLibcStats().backingBytes == libcBefore.backingBytes,
                    "libc-ledger");
#endif // _EE

    const Stats zeroBefore = Ledger(kTestTag);
    void * zero = TryAlloc(0, kTestTag);
    passed &= Check(zero != nullptr && Ledger(kTestTag).allocationCount == zeroBefore.allocationCount + 1 &&
                        (!PS2_D3BFG_ASSERTS || Ledger(kTestTag).requestedBytes == zeroBefore.requestedBytes),
                    "zero-size");
    Free(zero);

    const Stats newBaseline = Ledger(kNewTag);
    void * ordinary = ::operator new(13);
    void * array = ::operator new[](47);
    void * aligned = ::operator new(81, std::align_val_t{ 128 });
    void * noThrow = ::operator new[](9, std::align_val_t{ 64 }, std::nothrow);
    const Stats newStats = Ledger(kNewTag);
    passed &= Check(newStats.allocationCount == newBaseline.allocationCount + 4 &&
                        (!PS2_D3BFG_ASSERTS || newStats.requestedBytes == newBaseline.requestedBytes + 150) &&
                        reinterpret_cast<std::uintptr_t>(aligned) % 128 == 0 && noThrow != nullptr,
                    "global-new-forms");
    ::operator delete(ordinary);
    ::operator delete[](array, size_t{ 47 });
    ::operator delete(aligned, size_t{ 81 }, std::align_val_t{ 128 });
    ::operator delete[](noThrow, std::align_val_t{ 64 }, std::nothrow);
    passed &= Check(Ledger(kNewTag).requestedBytes == newBaseline.requestedBytes &&
                        Ledger(kNewTag).allocationCount == newBaseline.allocationCount,
                    "global-delete-forms");

    ResetPeaks();
    const Stats finalStats = GetTotalStats();
    passed &= Check(finalStats.requestedBytes == baseline.requestedBytes && finalStats.backingBytes == baseline.backingBytes &&
                        finalStats.allocationCount == baseline.allocationCount &&
                        finalStats.peakRequestedBytes == baseline.requestedBytes,
                    "ledger-restored");
    return passed;
}

} // namespace ps2::tests
