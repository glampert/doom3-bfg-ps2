// ================================================================================================
// File: heap.cpp
// Brief: Store allocation metadata outside Doom objects; preserve accounting across all delete forms.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <cstdlib>
#include <limits>
#include <new>

#if defined(_EE)
    #define USE_DL_PREFIX 1
    #include <dlmalloc/malloc.h>
#endif

namespace ps2::system::heap
{
namespace
{

static constexpr std::uint32_t kAllocationMagic = 0xD3BF6A11;

struct Header
{
    void * base;
    size_t requested;
    size_t backing;
    std::uint32_t magic;
    std::uint16_t tag;
};

// Zero initialization is complete before any engine registration constructors run.
static Stats s_tags[kTagCount] = {};
static Stats s_total = {};

Header * GetHeader(const void * pointer)
{
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    auto * header = static_cast<Header *>(reinterpret_cast<void *>(address - sizeof(Header)));
    if (header->magic != kAllocationMagic || header->tag >= kTagCount)
    {
        Fail("invalid allocation header");
    }
    return header;
}

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

// The failed request and every live tag, so an exhausted budget names its owners. Formats
// only integers: newlib's floating-point conversion allocates.
[[noreturn]] PS2_COLD_FUNC void FailAllocation(size_t size, std::uint16_t tag, size_t alignment)
{
    for (std::uint16_t index = 0; index < kTagCount; ++index)
    {
        const Stats & stats = s_tags[index];

        if (stats.allocationCount != 0)
        {
            ps2::Log(ps2::LogLevel::Error, "heap tag=%u requested=%zu backing=%zu allocations=%zu peak_requested=%zu\n",
                     static_cast<unsigned>(index), stats.requestedBytes, stats.backingBytes, stats.allocationCount,
                     stats.peakRequestedBytes);
        }
    }

    const ArenaStats arena = GetArenaStats();
    ps2::FatalError("heap: cannot allocate %zu bytes (tag %u, alignment %zu); live %zu requested / %zu backing bytes in %zu "
                    "allocations; arena %zu committed, %zu free",
                    size, static_cast<unsigned>(tag), alignment, s_total.requestedBytes, s_total.backingBytes,
                    s_total.allocationCount, arena.committedBytes, arena.freeBytes);
}

} // namespace

[[noreturn]] PS2_COLD_FUNC void Fail(const char * reason)
{
    // Uses no C++ allocation, including when invoked before main or during OOM.
    ps2::FatalError("heap: %s", reason);
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
    *header = { base, size, backing, kAllocationMagic, tag };
    Add(s_tags[tag], size, backing);
    Add(s_total, size, backing);
    return pointer;
}

void * Alloc(size_t size, std::uint16_t tag, size_t alignment)
{
    void * pointer = TryAlloc(size, tag, alignment);
    if (pointer == nullptr)
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
    Header * header = GetHeader(pointer);
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

size_t GetRequestedSize(const void * pointer)
{
    return pointer == nullptr ? 0 : GetHeader(pointer)->requested;
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
    return { true, static_cast<size_t>(info.arena), static_cast<size_t>(info.uordblks),
             static_cast<size_t>(info.fordblks), static_cast<size_t>(info.usmblks) };
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

} // namespace ps2::system::heap

// Standard, sized, nothrow and over-aligned allocations share the same metadata.
void * operator new(size_t size)
{
    return ps2::system::heap::Alloc(size, ps2::system::heap::kNewTag);
}

void * operator new[](size_t size)
{
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
    return ps2::system::heap::TryAlloc(size, ps2::system::heap::kNewTag);
}

void * operator new[](size_t size, const std::nothrow_t &) noexcept
{
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
    return ps2::system::heap::Alloc(size, ps2::system::heap::kNewTag, static_cast<size_t>(alignment));
}

void * operator new[](size_t size, std::align_val_t alignment)
{
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
    return ps2::system::heap::TryAlloc(size, ps2::system::heap::kNewTag, static_cast<size_t>(alignment));
}

void * operator new[](size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept
{
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
