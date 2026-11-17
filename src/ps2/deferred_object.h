// ================================================================================================
// File: deferred_object.h
// Brief: Static storage for a global whose owner constructs and destroys it explicitly. The heap
//        admits no allocation before main, and full shutdown must free what a constructor allocated.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <new>

namespace ps2
{

// A union never constructs or destroys its variant members itself, so `object` is raw storage until
// Construct runs. Bind a global reference to it (`T & name = s_storage.object;`) to keep its users.
template<typename T>
union DeferredObject
{
    constexpr DeferredObject()
        : unused()
    {
    }

    ~DeferredObject()
    {
    }

    DeferredObject(const DeferredObject &) = delete;
    DeferredObject & operator=(const DeferredObject &) = delete;

    T & Construct()
    {
        return *::new (static_cast<void *>(&object)) T();
    }

    void Destroy()
    {
        object.~T();
    }

    T object;
    char unused;
};

} // namespace ps2
