// ================================================================================================
// File: dma_list.cpp
// Brief: The scratchpad display list. The EE writes tags and payloads into uncached scratchpad;
//        channel 8 (fromSPR) appends each full half to the chain ring in main RAM while the EE
//        fills the other half, and the ring reaches VIF1 without a single cache writeback. The
//        commands on top write every tag and VIFcode, so draw code never builds one itself.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/dma_list.h"
#include "ps2/renderer/dma.h"
#include "ps2/renderer/pipeline_wait.h"
#include "ps2/qwords.h"

#include <dma_registers.h>
#include <ee_regs.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <kernel.h>

#include <algorithm>

namespace ps2::renderer::dma
{
namespace
{

static constexpr u32 kDmaStart = 1U << 8;

// Every chain ends with CNT { FLUSH, DIRECT 2 } + GIF tag + FINISH, then END: four qwords that
// every reservation leaves room for, with one more while a batch waits for its start.
static constexpr u32 kTerminatorQwords = 4U;

// A DIRECT block is its CNT tag, the payload and one closing EOP GIF tag.
static constexpr u32 kDirectOverheadQwords = 2U;

PS2_ALWAYS_INLINE bool FromSprIdle()
{
    return (*R_EE_D8_CHCR & kDmaStart) == 0;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Commands
// ------------------------------------------------------------------------------------------------

gs::GifWriter DisplayList::BeginDirect(int payloadQwords)
{
    PS2_AssertMsg(m_ring != nullptr && m_directTag == nullptr && payloadQwords >= 0, "invalid DIRECT reservation state");

    // The block must fit one scratchpad half and leave the ring its terminator.
    const u32 largest = std::min(kHalfQwords, m_ring->bytes / 16U - kTerminatorQwords) - kDirectOverheadQwords;
    PS2_AssertMsg(static_cast<u32>(payloadQwords) <= largest, "DIRECT request exceeds chain ring capacity");

    const u32 request = static_cast<u32>(payloadQwords) + kDirectOverheadQwords;
    Ensure(request);

    m_directTag = Reserve(request);
    m_directBase = m_directTag + 1;
    m_directCapacity = payloadQwords;

    return gs::GifWriter(m_directBase, payloadQwords);
}

void DisplayList::EndDirect(const gs::GifWriter & writer)
{
    PS2_AssertMsg(m_directTag != nullptr && writer.Base() == m_directBase && writer.QwordCount() <= m_directCapacity,
                  "closing a foreign or oversized DIRECT payload");

    // Close every GIF packet even if the payload was empty. No competing PATH3 writer.
    qword_t * const eop = writer.Cursor();
    PACK_GIFTAG(eop, GIF_SET_TAG(0, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);

    const u32 qwords = static_cast<u32>(writer.QwordCount()) + 1U;
    StoreCntTag(m_directTag, qwords, kVifNop, VifDirect(qwords));
    Release(eop + 1);

    m_directTag = nullptr;
    m_directBase = nullptr;
    m_directCapacity = 0;
}

void DisplayList::FlushPaths()
{
    Ensure(1U);
    Cnt(0U, kVifFlush, kVifNop);
}

void DisplayList::SetFrameConstants(const vu::FrameConstants & constants)
{
    PS2_AssertMsg(vu::IsInitialized() && m_pendingStart == kVifNop, "VU1 frame constants before initialization or over a pending batch");

    static_assert(sizeof(vu::FrameConstants) == 9U * 16U, "FrameConstants must match qwords 0-6 and 9-10 of vu_memory.h");
    static_assert(kVuWindowStateAddr == 7 && kVuNullGifTagAddr == 8 && kVuStripTagAddr == 9 && kVuListTagAddr == 10 && kVuFrameBlockQwords == 11,
                  "the frame block layout changed");

    Ensure(2U + kVuFrameBlockQwords);

    // The double buffer, then STCYCL 1,1 and one absolute V4-32 unpack of the whole block.
    Cnt(0U, VifBase(kVuInputBase), VifOffset(kVuInputHalfQwords));
    qword_t * const payload = Cnt(kVuFrameBlockQwords, VifStcycl(1U, 1U),
                                  VifUnpack(UnpackFormat::V4_32, kVuFrameBlockQwords, kVuFrameConstantsAddr, false, false));

    const qword_t * const source = reinterpret_cast<const qword_t *>(&constants);
    CopyQwords<7>(payload, source);

    // The windows restart at A each frame: nothing is in flight across a frame boundary.
    StoreQword(payload + kVuWindowStateAddr, static_cast<u64>(kVuWindowA) | (static_cast<u64>(kVuWindowQwords) << 32), 0);
    PACK_GIFTAG((payload + kVuNullGifTagAddr), GIF_SET_TAG(0, 1, 0, 0, GIF_FLG_PACKED, 0), 0);
    CopyQwords<2>(payload + kVuStripTagAddr, source + 7);
}

void DisplayList::SetProgram(vu::Program program)
{
    // A pending batch must start on the program it was drawn for, before an upload can evict it.
    StartPendingBatch();
    Ensure(1U);

    const vu::Binding binding = vu::Bind(program);
    if (binding.upload != nullptr)
    {
        Call(binding.upload, kVifNop, kVifNop);
    }

    m_programEntry = static_cast<u32>(binding.entry);
}

void DisplayList::DrawBatch(const void * packet, u32 qwords)
{
    PS2_AssertMsg(m_programEntry != kNoProgram, "batch drawn without a program");

    // The REF and the start it leaves pending.
    Ensure(2U);
    Ref(packet, qwords, kVifNop, m_pendingStart);
    m_pendingStart = VifMscal(m_programEntry);
}

qword_t * DisplayList::UnpackBatch(u32 qwords)
{
    PS2_AssertMsg(m_programEntry != kNoProgram && qwords >= 1U && qwords <= kVuInputHalfQwords && qwords <= 256U,
                  "invalid unpacked batch");

    StartPendingBatch();
    Ensure(qwords + 2U);

    qword_t * const payload = Cnt(qwords, VifStcycl(1U, 1U), VifUnpack(UnpackFormat::V4_32, qwords & 0xFFU, 0U, true, false));
    m_pendingStart = VifMscal(m_programEntry);
    return payload;
}

// ------------------------------------------------------------------------------------------------
// Chain ownership
// ------------------------------------------------------------------------------------------------

void DisplayList::Begin(ChainRing & ring)
{
    PS2_AssertMsg(ring.base != nullptr && (reinterpret_cast<uiptr>(ring.base) & 63U) == 0 && (ring.bytes & 15U) == 0 && ring.bytes >= kTerminatorQwords * 16U,
                  "invalid chain ring");
    PS2_AssertMsg(m_ring == nullptr || (m_cursor == HalfBase(m_half) && m_directTag == nullptr && m_pendingStart == kVifNop),
                  "display list begun with an open chain");

    m_ring = &ring;
    m_ringOffset = 0;
    m_chainStart = 0;
    m_cursor = HalfBase(m_half);
    m_programEntry = kNoProgram;
}

const void * DisplayList::CloseChain()
{
    PS2_AssertMsg(m_ring != nullptr && m_directTag == nullptr, "closing a chain inside a DIRECT block");

    StartPendingBatch();

    // FLUSH waits for the microprogram and PATH1/PATH2, then FINISH marks the chain's end in CSR.
    qword_t * const fence = Cnt(2U, kVifFlush, VifDirect(2U));
    PACK_GIFTAG(fence, GIF_SET_TAG(1, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
    PACK_GIFTAG((fence + 1), 1, GS_REG_FINISH);
    End(kVifNop, kVifNop);

    Drain();
    gs::WaitUntil(FromSprIdle, "fromSPR DMA drain");

    const u8 * const chain = m_ring->base + m_chainStart;
    m_chainStart = RingUsedBytes();
    return chain;
}

// ------------------------------------------------------------------------------------------------
// Tags and the scratchpad
// ------------------------------------------------------------------------------------------------

void DisplayList::Ensure(u32 qwords)
{
    const u32 reserve = qwords + kTerminatorQwords + (m_pendingStart != kVifNop ? 1U : 0U);
    PS2_AssertMsg(m_ring != nullptr && m_directTag == nullptr && reserve * 16U <= m_ring->bytes,
                  "display list reservation exceeds the chain ring");

    if (!Fits(reserve))
    {
        RestartRing();
    }
}

// The ring is full: the chain queued so far must run before the ring is written over. The kick
// starts any pending batch, so the next one carries no start of its own.
void DisplayList::RestartRing()
{
    Kick();
    WaitIdle();

    m_ringOffset = 0;
    m_chainStart = 0;
    ++m_ringRestarts;
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
    StoreCntTag(tag, payloadQwords, vif0, vif1);
    return tag + 1;
}

void DisplayList::Ref(const void * data, u32 qwords, u32 vif0, u32 vif1)
{
    PS2_AssertMsg(data != nullptr && (reinterpret_cast<uiptr>(data) & 15U) == 0 && qwords >= 1U && qwords <= 0xFFFFU, "invalid REF data");

    StoreRefTag(Reserve(1U), data, qwords, vif0, vif1);
}

void DisplayList::Call(const void * chain, u32 vif0, u32 vif1)
{
    PS2_AssertMsg(chain != nullptr && (reinterpret_cast<uiptr>(chain) & 15U) == 0, "invalid CALL chain");

    StoreCallTag(Reserve(1U), chain, vif0, vif1);
}

void DisplayList::End(u32 vif0, u32 vif1)
{
    StoreEndTag(Reserve(1U), vif0, vif1);
}

void DisplayList::StartPendingBatch()
{
    if (m_pendingStart != kVifNop)
    {
        Cnt(0U, kVifNop, m_pendingStart);
        m_pendingStart = kVifNop;
    }
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

    *R_EE_D_STAT  = 1U << 8; // channel 8 interrupt status: write one to clear
    *R_EE_D8_SADR = DMA_SET_SADR(static_cast<u32>(m_half) * (kScratchpadBytes / 2U));
    *R_EE_D8_MADR = DMA_SET_MADR(PhysicalAddress(m_ring->base + m_ringOffset), 0);
    *R_EE_D8_QWC  = DMA_SET_QWC(used);
    *R_EE_D8_CHCR = DMA_SET_CHCR(0, 0, 0, 0, 0, 1, 0);

    m_ringOffset += used * 16U;
    m_half ^= 1;
    m_cursor = HalfBase(m_half);
    ++m_sprDrains;
}

qword_t * DisplayList::HalfBase(int half) const
{
    return reinterpret_cast<qword_t *>(kScratchpadBase + static_cast<u32>(half) * (kScratchpadBytes / 2U));
}

} // namespace ps2::renderer::dma
