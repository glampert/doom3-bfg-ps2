// ================================================================================================
// File: class_alloc.h
// Brief: Preserve Doom class alignment and signed memory counters without a second allocation prefix.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2::game
{

// bytes counts object bytes; allocator metadata/backing is tracked separately by the shared heap.
// Failed requests leave both counters and the heap ledger untouched.
void * TryAllocClass(size_t size, size_t alignment, std::uint16_t tag, int & bytes, int & objects);
void * AllocClass(size_t size, size_t alignment, std::uint16_t tag, int & bytes, int & objects);
void FreeClass(void * pointer, int & bytes, int & objects);

template<typename T>
T * CreateClass()
{
    // Required allocations terminate on failure. Preserve the post-constructor diagnostic hook.
    T * instance = new T;
    instance->FindUninitializedMemory();
    return instance;
}

} // namespace ps2::game
