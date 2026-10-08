// ================================================================================================
// File: class_alloc.cpp
// Brief: Bound game class allocation/accounting while retaining the heap's requested alignment.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/game/class_alloc.h"
#include "ps2/system/heap.h"

#include <climits>

namespace ps2::game
{

void * TryAllocClass(size_t size, size_t alignment, std::uint16_t tag, int & bytes, int & objects)
{
    if (bytes < 0 || objects < 0 || objects == INT_MAX || size > static_cast<size_t>(INT_MAX - bytes))
    {
        return nullptr;
    }
    void * pointer = heap::TryAlloc(size, tag, alignment);
    if (pointer != nullptr)
    {
        bytes += static_cast<int>(size);
        ++objects;
    }
    return pointer;
}

void * AllocClass(size_t size, size_t alignment, std::uint16_t tag, int & bytes, int & objects)
{
    void * pointer = TryAllocClass(size, alignment, tag, bytes, objects);
    if (pointer == nullptr)
    {
        FatalError("idClass allocation failed: size=%zu alignment=%zu tag=%u bytes=%d objects=%d",
            size, alignment, static_cast<unsigned int>(tag), bytes, objects);
    }
    return pointer;
}

void FreeClass(void * pointer, int & bytes, int & objects)
{
    if (pointer == nullptr)
    {
        return;
    }
    const size_t size = heap::GetRequestedSize(pointer);
    if (bytes < 0 || objects <= 0 || size > static_cast<size_t>(bytes))
    {
        FatalError("idClass allocation counters underflow");
    }
    bytes -= static_cast<int>(size);
    --objects;
    heap::Free(pointer);
}

} // namespace ps2::game
