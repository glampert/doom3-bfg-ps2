// ================================================================================================
// File: script_storage.h
// Brief: idStaticList's interface over tagged heap storage, so the script program's fixed arrays
// are counted under their memory tag from compilation start instead of in the ELF's BSS.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include "ps2/system/heap.h"

#include <cstddef>
#include <cstdint>
#include <new>

namespace ps2::game::script
{

// Holds N value-initialized elements between Allocate and Release (nothing before or after, like
// a zeroed static array once allocated). Element addresses stay stable until Release.
template<typename T, int N>
class HeapStaticList final
{
    static_assert(N > 0);

public:
    HeapStaticList() = default;

    ~HeapStaticList()
    {
        Release();
    }

    HeapStaticList(const HeapStaticList &) = delete;
    HeapStaticList & operator=(const HeapStaticList &) = delete;

    PS2_COLD_FUNC void Allocate(std::uint16_t tag)
    {
        if (m_list != nullptr) [[unlikely]]
        {
            FatalError("script storage allocated twice");
        }

        const size_t alignment = alignof(T) > 16 ? alignof(T) : 16;
        void * const memory = system::heap::Alloc(sizeof(T) * static_cast<size_t>(N), tag, alignment);
        m_list = static_cast<T *>(memory);

        for (int index = 0; index < N; ++index)
        {
            new (&m_list[index]) T();
        }

        m_num = 0;
    }

    PS2_COLD_FUNC void Release()
    {
        if (m_list == nullptr)
        {
            return;
        }

        for (int index = 0; index < N; ++index)
        {
            m_list[index].~T();
        }

        system::heap::Free(m_list);
        m_list = nullptr;
        m_num = 0;
    }

    int Num() const
    {
        return m_num;
    }

    int Max() const
    {
        return N;
    }

    T * Ptr()
    {
        return m_list;
    }

    size_t MemoryUsed() const
    {
        return static_cast<size_t>(m_num) * sizeof(T);
    }

    // Like idStaticList::Alloc, returns null when full; callers check Num against Max first.
    T * Alloc()
    {
        PS2_Assert(m_list != nullptr);

        if (m_num >= N)
        {
            return nullptr;
        }

        return &m_list[m_num++];
    }

    void Clear()
    {
        m_num = 0;
    }

    void SetNum(int num)
    {
        PS2_Assert(num >= 0 && num <= N && (m_list != nullptr || num == 0));
        m_num = num;
    }

    T & operator[](int index)
    {
        PS2_Assert(index >= 0 && index < m_num);
        return m_list[index];
    }

    const T & operator[](int index) const
    {
        PS2_Assert(index >= 0 && index < m_num);
        return m_list[index];
    }

private:
    T * m_list = nullptr;
    int m_num = 0;
};

} // namespace ps2::game::script
