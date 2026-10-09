// ================================================================================================
// File: type_query.h
// Brief: Checked pointer queries for single-inheritance engine interfaces without compiler RTTI.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <type_traits>

namespace ps2
{

// An inline template's local token has one address across translation units. Writable
// zero-initialized data prevents identical-constant folding and needs no static-init guard.
template<typename T>
const void * TypeKey()
{
    static unsigned char s_token = 0;
    return &s_token;
}

// Source must be a public, unambiguous ancestor (or descendant) of Target. The
// compiler rejects sibling/cross casts; these interfaces all use single inheritance.
template<typename Target, typename Source>
std::remove_cv_t<Target> CheckedCast(Source * source)
{
    static_assert(std::is_pointer_v<Target>);
#if defined(ID_PS2) || defined(PS2_D3BFG) || defined(ID_HOST_TEST)
    using Type = std::remove_cv_t<std::remove_pointer_t<Target>>;
    // Inheriting a parent's hook is enough for queries to that parent, but a newly
    // queried derived type must declare its own hook instead of always rejecting itself.
    static_assert(std::is_same_v<typename Type::PortableType, Type>,
        "Queried types must declare their own portable type hook");
    if (source == nullptr || !source->IsType(TypeKey<Type>()))
    {
        return nullptr;
    }
    return static_cast<std::remove_cv_t<Target>>(source);
#else
    return dynamic_cast<Target>(source);
#endif
}

} // namespace ps2

#if defined(ID_PS2) || defined(PS2_D3BFG) || defined(ID_HOST_TEST)
    #define PS2_TYPE_ROOT(Type) \
        using PortableType = Type; \
        static bool MatchesType(const void * key) { return key == ps2::TypeKey<Type>(); } \
        virtual bool IsType(const void * key) const { return MatchesType(key); }

    #define PS2_TYPE_DERIVED(Type, Base) \
        using PortableType = Type; \
        static bool MatchesType(const void * key) \
        { return key == ps2::TypeKey<Type>() || Base::MatchesType(key); } \
        bool IsType(const void * key) const override { return MatchesType(key); }
#else
    #define PS2_TYPE_ROOT(Type)
    #define PS2_TYPE_DERIVED(Type, Base)
#endif
