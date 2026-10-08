// ================================================================================================
// File: heap_tests.cpp
// Brief: Check alignment, overflow and exact ledger recovery across the standard allocation APIs.
// SPDX-License-Identifier: GPL-3.0-or-later
// ================================================================================================

#include "tests/smoketests/heap_tests.h"
#include "ps2/system/heap.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <initializer_list>
#include <limits>
#include <new>

namespace ps2::smoketests
{
namespace
{

// Models engine registration allocation before main; the heap ledger must already exist.
constexpr std::uint16_t kEarlyTag = 201;
void * s_earlyAllocation = ps2::heap::TryAlloc(23, kEarlyTag, 64);

bool Check(bool condition, const char * name)
{
    std::printf("[D3BFG] CHECK %s %s\n", name, condition ? "PASS" : "FAIL");
    return condition;
}

} // namespace

bool RunHeapTests()
{
    using namespace ps2::heap;
    bool passed = true;
    passed &= Check(s_earlyAllocation != nullptr && GetStats(kEarlyTag).requestedBytes == 23,
        "allocation-before-main");
    Free(s_earlyAllocation);
    s_earlyAllocation = nullptr;
    const Stats baseline = GetTotalStats();
    constexpr std::uint16_t kTestTag = 200;
    for (const size_t alignment : {size_t{16}, size_t{64}, size_t{256}, size_t{4096}})
    {
        void * allocation = TryAlloc(37, kTestTag, alignment);
        passed &= Check(allocation != nullptr &&
            reinterpret_cast<std::uintptr_t>(allocation) % alignment == 0, "alignment");
        const Stats stats = GetStats(kTestTag);
        passed &= Check(stats.requestedBytes == 37 && stats.backingBytes >= 37 &&
            stats.allocationCount == 1, "tag-accounting");
        Free(allocation);
        passed &= Check(GetStats(kTestTag).requestedBytes == 0 &&
            GetStats(kTestTag).backingBytes == 0 && GetStats(kTestTag).allocationCount == 0, "unsized-free");
    }
    passed &= Check(TryAlloc(std::numeric_limits<size_t>::max(), kTestTag) == nullptr &&
        TryAlloc(7, kTestTag, 3) == nullptr && TryAlloc(7, kTagCount) == nullptr, "invalid-requests");
#if defined(_EE)
    // Exercise the program-wide C allocator, including its newlib entry points' common implementation.
    volatile size_t overflowCount = std::numeric_limits<size_t>::max() / 2 + 1;
    // GCC can remove a direct calloc/free pair and its errno observation as a builtin.
    void * (*volatile allocate)(size_t, size_t) = std::calloc;
    errno = 0;
    void * overflow = allocate(overflowCount, 2);
    passed &= Check(overflow == nullptr && errno == ENOMEM, "calloc-overflow");
    std::free(overflow);
#endif
    void * zero = TryAlloc(0, kTestTag);
    passed &= Check(zero != nullptr && GetStats(kTestTag).requestedBytes == 0 &&
        GetStats(kTestTag).allocationCount == 1, "zero-size");
    Free(zero);

    const Stats newBaseline = GetStats(kNewTag);
    void * ordinary = ::operator new(13);
    void * array = ::operator new[](47);
    void * aligned = ::operator new(81, std::align_val_t{128});
    void * noThrow = ::operator new[](9, std::align_val_t{64}, std::nothrow);
    passed &= Check(GetStats(kNewTag).requestedBytes == newBaseline.requestedBytes + 150 &&
        reinterpret_cast<std::uintptr_t>(aligned) % 128 == 0 && noThrow != nullptr,
        "global-new-forms");
    ::operator delete(ordinary);
    ::operator delete[](array, size_t{47});
    ::operator delete(aligned, size_t{81}, std::align_val_t{128});
    ::operator delete[](noThrow, std::align_val_t{64}, std::nothrow);
    passed &= Check(GetStats(kNewTag).requestedBytes == newBaseline.requestedBytes &&
        GetStats(kNewTag).allocationCount == newBaseline.allocationCount, "global-delete-forms");
    ResetPeaks();
    const Stats finalStats = GetTotalStats();
    passed &= Check(finalStats.requestedBytes == baseline.requestedBytes &&
        finalStats.backingBytes == baseline.backingBytes && finalStats.allocationCount == baseline.allocationCount &&
        finalStats.peakRequestedBytes == baseline.requestedBytes, "ledger-restored");
    return passed;
}

} // namespace ps2::smoketests
