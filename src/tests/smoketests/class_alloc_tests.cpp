// ================================================================================================
// File: class_alloc_tests.cpp
// Brief: Check alignment, constructor ordering, signed counter bounds and exact allocation recovery.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/class_alloc_tests.h"
#include "ps2/game/class_alloc.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <climits>
#include <initializer_list>
#include <limits>
#include <new>

namespace ps2::smoketests
{
namespace
{

static constexpr std::uint16_t kClassTestTag = 202;
static int s_bytes = 0;
static int s_objects = 0;
static int s_diagnostics = 0;
static int s_destructors = 0;

bool Check(bool condition, const char * name)
{
    Log(LogLevel::Info, "[D3BFG] CHECK class/%s %s\n", name, condition ? "PASS" : "FAIL");
    return condition;
}

bool SameStats(const heap::Stats & first, const heap::Stats & second)
{
    return first.requestedBytes == second.requestedBytes && first.backingBytes == second.backingBytes &&
        first.allocationCount == second.allocationCount;
}

// Exercise the same inherited new/delete forms used by idClass without linking a fake game world.
class Probe
{
public:
    static void * operator new(size_t size)
    {
        return game::AllocClass(size, 16, kClassTestTag, s_bytes, s_objects);
    }
    static void * operator new(size_t size, std::align_val_t alignment)
    {
        return game::AllocClass(size, static_cast<size_t>(alignment), kClassTestTag, s_bytes, s_objects);
    }
    static void operator delete(void * pointer) { game::FreeClass(pointer, s_bytes, s_objects); }
    static void operator delete(void * pointer, std::align_val_t) { game::FreeClass(pointer, s_bytes, s_objects); }
    virtual ~Probe() { ++s_destructors; }
    void FindUninitializedMemory()
    {
        PS2_Assert(m_constructed == 42);
        ++s_diagnostics;
    }

private:
    int m_constructed = 42;
};

class alignas(64) AlignedProbe final : public Probe
{
public:
    int payload[16] = {73};
};

bool Rejected(size_t size, size_t alignment, std::uint16_t tag, int bytes, int objects)
{
    const int oldBytes = bytes;
    const int oldObjects = objects;
    const heap::Stats before = heap::GetTotalStats();
    void * pointer = game::TryAllocClass(size, alignment, tag, bytes, objects);
    return pointer == nullptr && bytes == oldBytes && objects == oldObjects &&
        SameStats(before, heap::GetTotalStats());
}

} // namespace

bool RunClassAllocTests()
{
    const heap::Stats baseline = heap::GetTotalStats();
    bool passed = true;
    for (const size_t alignment : {size_t{16}, size_t{64}, size_t{256}})
    {
        int bytes = 0;
        int objects = 0;
        void * first = game::AllocClass(37, alignment, kClassTestTag, bytes, objects);
        void * second = game::AllocClass(19, alignment, kClassTestTag, bytes, objects);
        passed &= Check(reinterpret_cast<std::uintptr_t>(first) % alignment == 0 &&
            reinterpret_cast<std::uintptr_t>(second) % alignment == 0 &&
            heap::GetRequestedSize(first) == 37 && heap::GetRequestedSize(second) == 19 &&
            bytes == 56 && objects == 2, "aligned-accounting");
        game::FreeClass(first, bytes, objects);
        passed &= Check(bytes == 19 && objects == 1, "out-of-order-free");
        game::FreeClass(second, bytes, objects);
        game::FreeClass(nullptr, bytes, objects);
        passed &= Check(bytes == 0 && objects == 0 && SameStats(baseline, heap::GetTotalStats()), "ledger-recovery");
    }
    passed &= Check(Rejected(std::numeric_limits<size_t>::max(), 16, kClassTestTag, 0, 0) &&
        Rejected(4, 16, kClassTestTag, INT_MAX - 3, 0) &&
        Rejected(1, 16, kClassTestTag, 0, INT_MAX) && Rejected(1, 16, kClassTestTag, -1, 0) &&
        Rejected(1, 16, kClassTestTag, 0, -1) && Rejected(1, 3, kClassTestTag, 0, 0) &&
        Rejected(1, 16, heap::kTagCount, 0, 0), "reject-without-mutation");

    int bytes = INT_MAX - 8;
    int objects = INT_MAX - 1;
    void * last = game::AllocClass(8, 16, kClassTestTag, bytes, objects);
    passed &= Check(bytes == INT_MAX && objects == INT_MAX &&
        game::TryAllocClass(1, 16, kClassTestTag, bytes, objects) == nullptr, "counter-limit");
    game::FreeClass(last, bytes, objects);
    passed &= Check(bytes == INT_MAX - 8 && objects == INT_MAX - 1, "counter-limit-free");
    bytes = objects = 0;
    void * zero = game::AllocClass(0, 16, kClassTestTag, bytes, objects);
    passed &= Check(zero != nullptr && heap::GetRequestedSize(zero) == 0 &&
        heap::GetRequestedSize(nullptr) == 0 && bytes == 0 && objects == 1, "zero-size");
    game::FreeClass(zero, bytes, objects);

    s_diagnostics = s_destructors = 0;
    Probe * ordinary = game::CreateClass<Probe>();
    AlignedProbe * aligned = game::CreateClass<AlignedProbe>();
    passed &= Check(s_objects == 2 && s_diagnostics == 2 &&
        s_bytes == static_cast<int>(sizeof(Probe) + sizeof(AlignedProbe)) && aligned->payload[0] == 73 &&
        reinterpret_cast<std::uintptr_t>(aligned) % 64 == 0, "factory-construction");
    Probe * base = aligned;
    delete ordinary;
    delete base;
    passed &= Check(s_destructors == 2 && s_bytes == 0 && s_objects == 0 &&
        SameStats(baseline, heap::GetTotalStats()), "factory-virtual-delete");
    return passed;
}

} // namespace ps2::smoketests
