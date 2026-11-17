// ================================================================================================
// File: class_alloc.cpp
// Brief: Allocate game classes at their alignment; tracking builds also count their bytes and objects.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/game/class_alloc.h"
#include "ps2/system/heap.h"

#include <climits>

namespace ps2::game
{

void * TryAllocClass(size_t size, size_t alignment, std::uint16_t tag, int & bytes, int & objects)
{
#if PS2_D3BFG_ASSERTS
    if (bytes < 0 || objects < 0 || objects == INT_MAX || size > static_cast<size_t>(INT_MAX - bytes)) [[unlikely]]
    {
        return nullptr;
    }
#endif // PS2_D3BFG_ASSERTS

    void * pointer = system::heap::TryAlloc(size, tag, alignment);

#if PS2_D3BFG_ASSERTS
    if (pointer != nullptr) [[likely]]
    {
        bytes += static_cast<int>(size);
        ++objects;
    }
#else // PS2_D3BFG_ASSERTS
    static_cast<void>(bytes);
    static_cast<void>(objects);
#endif // PS2_D3BFG_ASSERTS

    return pointer;
}

void * AllocClass(size_t size, size_t alignment, std::uint16_t tag, int & bytes, int & objects)
{
    void * pointer = TryAllocClass(size, alignment, tag, bytes, objects);
    if (pointer == nullptr) [[unlikely]]
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

#if PS2_D3BFG_ASSERTS
    const size_t size = system::heap::GetRequestedSize(pointer);

    if (bytes < 0 || objects <= 0 || size > static_cast<size_t>(bytes)) [[unlikely]]
    {
        FatalError("idClass allocation counters underflow");
    }

    bytes -= static_cast<int>(size);
    --objects;
#else // PS2_D3BFG_ASSERTS
    static_cast<void>(bytes);
    static_cast<void>(objects);
#endif // PS2_D3BFG_ASSERTS

    system::heap::Free(pointer);
}

} // namespace ps2::game
