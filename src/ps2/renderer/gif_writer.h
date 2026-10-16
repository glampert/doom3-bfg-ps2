// ================================================================================================
// File: gif_writer.h
// Brief: Write GIF data into a reserved DIRECT payload, checking bounds before SDK helpers write.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once
#include "ps2/common.h"
#include <draw.h>
#include <gif_tags.h>
#include <tamtypes.h>

namespace ps2
{
class GifWriter final
{
public:
    GifWriter(qword_t * base, int capacity) : m_base(base), m_ptr(base), m_capacity(capacity)
    {
        if (base == nullptr || capacity < 0 || (reinterpret_cast<uiptr>(base) & 15U) != 0)
        {
            FatalError("invalid GIF payload storage");
        }
    }

    qword_t * Base() const
    {
        return m_base;
    }

    qword_t * Cursor() const
    {
        return m_ptr;
    }

    int QwordCount() const
    {
        return static_cast<int>(m_ptr - m_base);
    }

    void EnsureSpace(int count) const
    {
        if (count < 0 || count > m_capacity - QwordCount())
        {
            FatalError("GIF payload overflow: used=%d request=%d capacity=%d", QwordCount(), count, m_capacity);
        }
    }

    void SetRegister(u64 reg, u64 data)
    {
        EnsureSpace(2);
        PACK_GIFTAG(m_ptr, GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
        ++m_ptr;
        PACK_GIFTAG(m_ptr, data, reg);
        ++m_ptr;
    }

    void SetupEnvironment(int context, framebuffer_t & frame, zbuffer_t & depth)
    {
        // Audited ps2sdk draw.c: one tag and fifteen A+D writes. Re-audit on SDK changes.
        constexpr int kEnvironmentQwords = 16;
        if (context < 0 || context > 1)
        {
            FatalError("invalid GS drawing context");
        }
        EnsureSpace(kEnvironmentQwords);
        qword_t * const end = draw_setup_environment(m_ptr, context, &frame, &depth);
        if (end != m_ptr + kEnvironmentQwords)
        {
            FatalError("unexpected SDK environment packet size");
        }
        m_ptr = end;
    }

private:
    qword_t * m_base;
    qword_t * m_ptr;
    int m_capacity;
};
} // namespace ps2
