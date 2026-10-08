// ================================================================================================
// File: heap.cpp
// Brief: Store allocation metadata outside Doom objects; preserve accounting across all delete forms.
// SPDX-License-Identifier: GPL-3.0-or-later
// ================================================================================================

#include "ps2/system/heap.h"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>

#if defined(_EE)
#define USE_DL_PREFIX 1
#include <dlmalloc/malloc.h>
#endif

namespace ps2::heap
{
namespace
{

constexpr std::uint32_t kAllocationMagic = 0xD3BF6A11;

struct Header
{
    void * base;
    size_t requested;
    size_t backing;
    std::uint32_t magic;
    std::uint16_t tag;
};

// Zero initialization is complete before any engine registration constructors run.
Stats s_tags[kTagCount] = {};
Stats s_total = {};

void Add(Stats & stats, size_t requested, size_t backing)
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

void Remove(Stats & stats, const Header & header)
{
    if (stats.requestedBytes < header.requested || stats.backingBytes < header.backing ||
        stats.allocationCount == 0)
    {
        Fail("heap ledger underflow");
    }
    stats.requestedBytes -= header.requested;
    stats.backingBytes -= header.backing;
    --stats.allocationCount;
}

} // namespace

[[noreturn]] void Fail(const char * reason)
{
    // Uses no C++ allocation, including when invoked before main or during OOM.
    std::fprintf(stderr, "[D3BFG] FATAL heap: %s\n", reason);
    std::fflush(stderr);
    std::abort();
}

void * TryAlloc(size_t size, std::uint16_t tag, size_t alignment)
{
    if (tag >= kTagCount || alignment == 0 || (alignment & (alignment - 1)) != 0)
    {
        return nullptr;
    }
    if (alignment < 16)
    {
        alignment = 16;
    }
    constexpr size_t kMax = static_cast<size_t>(std::numeric_limits<ptrdiff_t>::max());
    if (alignment > kMax - sizeof(Header) || size > kMax - sizeof(Header) - alignment)
    {
        return nullptr;
    }
    const size_t reserved = (size == 0 ? 1 : size) + sizeof(Header) + alignment - 1;
#if defined(_EE)
    void * base = dlmalloc(reserved);
#else
    void * base = std::malloc(reserved);
#endif
    if (base == nullptr)
    {
        return nullptr;
    }
#if defined(_EE)
    const size_t backing = dlmalloc_usable_size(base);
#else
    const size_t backing = reserved;
#endif
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(base) + sizeof(Header);
    const std::uintptr_t address = (start + alignment - 1) & ~(alignment - 1);
    void * pointer = reinterpret_cast<void *>(address);
    auto * header = static_cast<Header *>(reinterpret_cast<void *>(address - sizeof(Header)));
    *header = {base, size, backing, kAllocationMagic, tag};
    Add(s_tags[tag], size, backing);
    Add(s_total, size, backing);
    return pointer;
}

void * Alloc(size_t size, std::uint16_t tag, size_t alignment)
{
    void * pointer = TryAlloc(size, tag, alignment);
    if (pointer == nullptr)
    {
        Fail("invalid request or out of memory");
    }
    return pointer;
}

void Free(void * pointer)
{
    if (pointer == nullptr)
    {
        return;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    auto * header = static_cast<Header *>(reinterpret_cast<void *>(address - sizeof(Header)));
    if (header->magic != kAllocationMagic || header->tag >= kTagCount)
    {
        Fail("invalid allocation header");
    }
    Remove(s_tags[header->tag], *header);
    Remove(s_total, *header);
    void * base = header->base;
    header->magic = 0;
#if defined(_EE)
    dlfree(base);
#else
    std::free(base);
#endif
}

Stats GetStats(std::uint16_t tag)
{
    return tag < kTagCount ? s_tags[tag] : Stats{};
}

Stats GetTotalStats()
{
    return s_total;
}

ArenaStats GetArenaStats()
{
#if defined(_EE)
    const struct mallinfo info = dlmallinfo();
    // This ABI uses signed int fields, safe within the EE's 32 MiB addressable RAM.
    return {true, static_cast<size_t>(info.arena), static_cast<size_t>(info.uordblks),
        static_cast<size_t>(info.fordblks), static_cast<size_t>(info.usmblks)};
#else
    return {};
#endif
}

void ResetPeaks()
{
    for (auto & stats : s_tags)
    {
        stats.peakRequestedBytes = stats.requestedBytes;
        stats.peakBackingBytes = stats.backingBytes;
    }
    s_total.peakRequestedBytes = s_total.requestedBytes;
    s_total.peakBackingBytes = s_total.backingBytes;
}

} // namespace ps2::heap

// Standard, sized, nothrow and over-aligned allocations share the same metadata.
void * operator new(size_t size) { return ps2::heap::Alloc(size, ps2::heap::kNewTag); }
void * operator new[](size_t size) { return ps2::heap::Alloc(size, ps2::heap::kNewTag); }
void operator delete(void * pointer) noexcept { ps2::heap::Free(pointer); }
void operator delete[](void * pointer) noexcept { ps2::heap::Free(pointer); }
void operator delete(void * pointer, size_t) noexcept { ps2::heap::Free(pointer); }
void operator delete[](void * pointer, size_t) noexcept { ps2::heap::Free(pointer); }
void * operator new(size_t size, const std::nothrow_t &) noexcept { return ps2::heap::TryAlloc(size, ps2::heap::kNewTag); }
void * operator new[](size_t size, const std::nothrow_t &) noexcept { return ps2::heap::TryAlloc(size, ps2::heap::kNewTag); }
void operator delete(void * pointer, const std::nothrow_t &) noexcept { ps2::heap::Free(pointer); }
void operator delete[](void * pointer, const std::nothrow_t &) noexcept { ps2::heap::Free(pointer); }
void * operator new(size_t size, std::align_val_t alignment) { return ps2::heap::Alloc(size, ps2::heap::kNewTag, static_cast<size_t>(alignment)); }
void * operator new[](size_t size, std::align_val_t alignment) { return ps2::heap::Alloc(size, ps2::heap::kNewTag, static_cast<size_t>(alignment)); }
void operator delete(void * pointer, std::align_val_t) noexcept { ps2::heap::Free(pointer); }
void operator delete[](void * pointer, std::align_val_t) noexcept { ps2::heap::Free(pointer); }
void operator delete(void * pointer, size_t, std::align_val_t) noexcept { ps2::heap::Free(pointer); }
void operator delete[](void * pointer, size_t, std::align_val_t) noexcept { ps2::heap::Free(pointer); }
void * operator new(size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept { return ps2::heap::TryAlloc(size, ps2::heap::kNewTag, static_cast<size_t>(alignment)); }
void * operator new[](size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept { return ps2::heap::TryAlloc(size, ps2::heap::kNewTag, static_cast<size_t>(alignment)); }
void operator delete(void * pointer, std::align_val_t, const std::nothrow_t &) noexcept { ps2::heap::Free(pointer); }
void operator delete[](void * pointer, std::align_val_t, const std::nothrow_t &) noexcept { ps2::heap::Free(pointer); }
