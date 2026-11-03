// ================================================================================================
// File: dma_list.cpp
// Brief: The scratchpad display list builder. The EE writes tags and payloads into uncached
//        scratchpad; channel 8 (fromSPR) appends each full half to the chain ring in main RAM while
//        the EE fills the other half, and the ring reaches VIF1 without a single cache writeback.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/dma_list.h"
#include "ps2/renderer/pipeline_wait.h"
#include "ps2/qwords.h"

#include <dma_registers.h>
#include <dma_tags.h>
#include <ee_regs.h>
#include <kernel.h>

namespace ps2::renderer::dma
{
namespace
{

static constexpr u32 kDmaStart = 1U << 8;

// DMA addresses are physical: main RAM aliases (cached, uncached, UCAB) share the low 28 bits.
inline u32 PhysicalAddress(const void * pointer)
{
    return static_cast<u32>(reinterpret_cast<uiptr>(pointer) & 0x0FFFFFFFU);
}

inline bool FromSprIdle()
{
    return (*R_EE_D8_CHCR & kDmaStart) == 0;
}

} // namespace

qword_t * DisplayList::HalfBase(int half) const
{
    return reinterpret_cast<qword_t *>(kScratchpadBase + static_cast<u32>(half) * (kScratchpadBytes / 2U));
}

void DisplayList::Begin(ChainRing & ring, u32 offsetBytes)
{
    PS2_AssertMsg(ring.base != nullptr && (reinterpret_cast<uiptr>(ring.base) & 63U) == 0 && (ring.bytes & 15U) == 0 &&
                      offsetBytes <= ring.bytes && (offsetBytes & 15U) == 0,
                  "invalid chain ring");
    PS2_AssertMsg(m_ring == nullptr || m_cursor == HalfBase(m_half), "display list begun with scratchpad content pending");

    m_ring = &ring;
    m_ringOffset = offsetBytes;
    m_cursor = HalfBase(m_half);
}

bool DisplayList::Fits(u32 qwords) const
{
    PS2_Assert(m_ring != nullptr);
    return RingUsedBytes() + qwords * 16U <= m_ring->bytes;
}

qword_t * DisplayList::Reserve(u32 qwords)
{
    PS2_AssertMsg(m_ring != nullptr && qwords >= 1U && qwords <= kHalfQwords, "invalid display list reservation");

    if (m_cursor + qwords > HalfBase(m_half) + kHalfQwords)
    {
        Drain();
    }

    PS2_AssertMsg(Fits(qwords), "chain ring overflow");

    qword_t * const reserved = m_cursor;
    m_cursor += qwords;
    return reserved;
}

void DisplayList::Release(qword_t * cursor)
{
    PS2_AssertMsg(cursor >= HalfBase(m_half) && cursor <= m_cursor, "display list release outside the last reservation");
    m_cursor = cursor;
}

qword_t * DisplayList::Cnt(u32 payloadQwords, u32 vif0, u32 vif1)
{
    PS2_AssertMsg(payloadQwords < kHalfQwords, "CNT payload exceeds a scratchpad half");

    qword_t * const tag = Reserve(payloadQwords + 1U);
    StoreQword(tag, DMATAG(payloadQwords, 0, DMA_TAG_CNT, 0, 0, 0), static_cast<u64>(vif0) | (static_cast<u64>(vif1) << 32));
    return tag + 1;
}

void DisplayList::Ref(const void * data, u32 qwords, u32 vif0, u32 vif1)
{
    PS2_AssertMsg(data != nullptr && (reinterpret_cast<uiptr>(data) & 15U) == 0 && qwords >= 1U && qwords <= 0xFFFFU, "invalid REF data");

    qword_t * const tag = Reserve(1U);
    StoreQword(tag, DMATAG(qwords, 0, DMA_TAG_REF, 0, PhysicalAddress(data), 0), static_cast<u64>(vif0) | (static_cast<u64>(vif1) << 32));
}

void DisplayList::Call(const void * chain, u32 vif0, u32 vif1)
{
    PS2_AssertMsg(chain != nullptr && (reinterpret_cast<uiptr>(chain) & 15U) == 0, "invalid CALL chain");

    qword_t * const tag = Reserve(1U);
    StoreQword(tag, DMATAG(0, 0, DMA_TAG_CALL, 0, PhysicalAddress(chain), 0), static_cast<u64>(vif0) | (static_cast<u64>(vif1) << 32));
}

void DisplayList::End(u32 vif0, u32 vif1)
{
    qword_t * const tag = Reserve(1U);
    StoreQword(tag, DMATAG(0, 0, DMA_TAG_END, 0, 0, 0), static_cast<u64>(vif0) | (static_cast<u64>(vif1) << 32));
}

void DisplayList::Drain()
{
    const u32 used = static_cast<u32>(m_cursor - HalfBase(m_half));
    if (used == 0U)
    {
        return;
    }

    // The EE's scratchpad stores complete before the DMAC reads them, and the previous drain (of
    // the other half, which becomes the write target) has finished before this one starts.
    EE_SYNCL();
    gs::WaitUntil(FromSprIdle, "fromSPR DMA");

    *R_EE_D_STAT = 1U << 8; // channel 8 interrupt status: write one to clear
    *R_EE_D8_SADR = DMA_SET_SADR(static_cast<u32>(m_half) * (kScratchpadBytes / 2U));
    *R_EE_D8_MADR = DMA_SET_MADR(PhysicalAddress(m_ring->base + m_ringOffset), 0);
    *R_EE_D8_QWC = DMA_SET_QWC(used);
    *R_EE_D8_CHCR = DMA_SET_CHCR(0, 0, 0, 0, 0, 1, 0);

    m_ringOffset += used * 16U;
    m_half ^= 1;
    m_cursor = HalfBase(m_half);
    ++m_sprDrains;
}

void DisplayList::Flush()
{
    PS2_Assert(m_ring != nullptr);

    Drain();
    gs::WaitUntil(FromSprIdle, "fromSPR DMA drain");
}

u32 DisplayList::RingUsedBytes() const
{
    return m_ringOffset + static_cast<u32>(m_cursor - HalfBase(m_half)) * 16U;
}

} // namespace ps2::renderer::dma
