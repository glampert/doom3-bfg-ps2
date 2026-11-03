// ================================================================================================
// File: dma.cpp
// Brief: The display list system over the scratchpad DisplayList: per-frame chain rings, DIRECT
//        blocks for the CPU 2D path, the FINISH fence and the VIF1 kick. The rings are written by
//        the fromSPR DMA alone, so a kick needs no cache writeback; the EE writes the scratchpad.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/dma.h"
#include "ps2/renderer/dma_list.h"
#include "ps2/renderer/pipeline_wait.h"
#include "ps2/qwords.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <idlib/precompiled.h> // memTag_t for the ring allocation
#include <dma.h>
#include <dma_tags.h>
#include <ee_regs.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <gs_privileged.h>
#include <kernel.h>

#include <algorithm>

namespace ps2::renderer::dma
{
namespace
{

// Every chain ends with CNT { FLUSH, DIRECT 2 } + GIF tag + FINISH, then END: four qwords that
// every reservation leaves room for.
static constexpr u32 kTerminatorQwords = 4U;

// A DIRECT block is its CNT tag, the payload and one closing EOP GIF tag.
static constexpr u32 kDirectOverheadQwords = 2U;
static constexpr u32 kDmaStart = 1U << 8;

static DisplayList s_list;
static ChainRing s_rings[2] = {};
static void * s_storage = nullptr;
static u32 s_ringBytes = 0;
static int s_ring = 0;
static u32 s_chainStart = 0; // ring offset of the chain the next Kick sends
static bool s_frame = false;
static bool s_inFlight = false;
static qword_t * s_directTag = nullptr;
static qword_t * s_directBase = nullptr;
static int s_directCapacity = 0;
static Stats s_stats = {};

inline u32 PhysicalAddress(const void * pointer)
{
    return static_cast<u32>(reinterpret_cast<uiptr>(pointer) & 0x0FFFFFFFU);
}

inline void RecordPeak()
{
    s_stats.peakBytes = std::max(s_stats.peakBytes, s_list.RingUsedBytes());
}

// The ring is full: the chain queued so far must run before the ring is written over.
void RestartRing()
{
    Kick();
    WaitIdle();
    s_list.Begin(s_rings[s_ring], 0);
    s_chainStart = 0;
    ++s_stats.drains;
}

} // namespace

PS2_COLD_FUNC bool Init(u32 ringBytes)
{
    if (IsInitialized()) [[unlikely]]
    {
        FatalError("display list system initialized twice");
    }

    if (ringBytes < 256U || (ringBytes & 63U) != 0)
    {
        Log(LogLevel::Error, "invalid chain ring size: %u\n", ringBytes);
        return false;
    }

    s_storage = system::heap::TryAlloc(static_cast<size_t>(ringBytes) * 2U, TAG_RENDER, 64);
    if (s_storage == nullptr)
    {
        return false;
    }

    // The rings are DMA territory from here on: write back and drop whatever the cache holds of
    // them, so no later writeback lands on top of DMA'd chain bytes.
    u8 * const bytes = static_cast<u8 *>(s_storage);
    SyncDCache(bytes, bytes + static_cast<size_t>(ringBytes) * 2U);
    for (int ring = 0; ring < 2; ++ring)
    {
        s_rings[ring] = { bytes + static_cast<size_t>(ring) * ringBytes, ringBytes };
    }

    s_ringBytes = ringBytes;
    s_ring = 0;
    s_chainStart = 0;
    s_stats = {};

    gs::WaitUntil([] { return (*R_EE_D1_CHCR & kDmaStart) == 0; }, "initial VIF1 DMA");
    gs::WaitUntil([] { return (*R_EE_D8_CHCR & kDmaStart) == 0; }, "initial fromSPR DMA");

    if (dma_channel_initialize(DMA_CHANNEL_VIF1, nullptr, 0) < 0 || dma_channel_initialize(DMA_CHANNEL_fromSPR, nullptr, 0) < 0)
    {
        system::heap::Free(s_storage);
        s_storage = nullptr;
        s_rings[0] = {};
        s_rings[1] = {};
        return false;
    }

    s_list.Begin(s_rings[s_ring], 0);
    return true;
}

PS2_COLD_FUNC void Shutdown()
{
    if (!IsInitialized())
    {
        return;
    }

    if (s_directTag != nullptr) [[unlikely]]
    {
        FatalError("display list shutdown with an open DIRECT payload");
    }

    // Preserve any completed work even during partial frame teardown.
    Kick();
    WaitIdle();
    dma_channel_shutdown(DMA_CHANNEL_fromSPR, 0);
    dma_channel_shutdown(DMA_CHANNEL_VIF1, 0);

    system::heap::Free(s_storage);
    s_storage = nullptr;
    s_rings[0] = {};
    s_rings[1] = {};
    s_frame = false;
    s_ring = 0;
    s_chainStart = 0;
}

bool IsInitialized()
{
    return s_storage != nullptr;
}

void BeginFrame()
{
    PS2_AssertMsg(IsInitialized() && !s_frame && s_directTag == nullptr && s_list.RingUsedBytes() == s_chainStart,
                  "invalid BeginFrame state");

    // Synchronous for now: the previous frame has run before its ring's twin is reused.
    WaitIdle();
    s_ring ^= 1;
    s_list.Begin(s_rings[s_ring], 0);
    s_chainStart = 0;
    s_frame = true;
}

void EndFrame()
{
    PS2_AssertMsg(s_frame && s_directTag == nullptr, "invalid EndFrame state");

    Kick();
    WaitIdle();
    s_frame = false;
}

gs::GifWriter BeginDirect(int payloadQwords)
{
    PS2_AssertMsg(s_frame && s_directTag == nullptr && payloadQwords >= 0, "invalid DIRECT reservation state");

    // The block must fit one scratchpad half and leave the ring its terminator.
    const u32 largest = std::min(kHalfQwords, s_ringBytes / 16U - kTerminatorQwords) - kDirectOverheadQwords;
    PS2_AssertMsg(static_cast<u32>(payloadQwords) <= largest, "DIRECT request exceeds chain ring capacity");

    const u32 request = static_cast<u32>(payloadQwords) + kDirectOverheadQwords;
    if (!s_list.Fits(request + kTerminatorQwords))
    {
        RestartRing();
    }

    s_directTag = s_list.Reserve(request);
    s_directBase = s_directTag + 1;
    s_directCapacity = payloadQwords;

    return gs::GifWriter(s_directBase, payloadQwords);
}

void EndDirect(const gs::GifWriter & writer)
{
    PS2_AssertMsg(s_directTag != nullptr && writer.Base() == s_directBase && writer.QwordCount() <= s_directCapacity,
                  "closing a foreign or oversized DIRECT payload");

    // Close every GIF packet even if the payload was empty. No competing PATH3 writer.
    qword_t * const eop = writer.Cursor();
    PACK_GIFTAG(eop, GIF_SET_TAG(0, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);

    const u32 qwords = static_cast<u32>(writer.QwordCount()) + 1U;
    StoreQword(s_directTag, DMATAG(qwords, 0, DMA_TAG_CNT, 0, 0, 0), static_cast<u64>(kVifNop) | (static_cast<u64>(VifDirect(qwords)) << 32));
    s_list.Release(eop + 1);

    s_directTag = nullptr;
    s_directBase = nullptr;
    s_directCapacity = 0;

    RecordPeak();
}

void Kick()
{
    PS2_AssertMsg(IsInitialized() && s_directTag == nullptr, "invalid display list Kick state");

    if (s_list.RingUsedBytes() == s_chainStart)
    {
        return;
    }

    WaitIdle();

    // FLUSH waits for the microprogram and PATH1/PATH2, then FINISH marks the chain's end in CSR.
    qword_t * const fence = s_list.Cnt(2U, kVifFlush, VifDirect(2U));
    PACK_GIFTAG(fence, GIF_SET_TAG(1, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
    PACK_GIFTAG((fence + 1), 1, GS_REG_FINISH);
    s_list.End(kVifNop, kVifNop);

    RecordPeak();
    s_list.Flush();

    *GS_REG_CSR = 2; // Clear only FINISH: all event bits are write-one-to-clear.
    dma_channel_send_chain(DMA_CHANNEL_VIF1, reinterpret_cast<void *>(PhysicalAddress(s_rings[s_ring].base + s_chainStart)), 0,
                           DMA_FLAG_TRANSFERTAG, 0);

    s_chainStart = s_list.RingUsedBytes();
    s_inFlight = true;
    ++s_stats.kicks;
    s_stats.sprDrains = s_list.SprDrains();
}

void WaitIdle()
{
    if (!s_inFlight)
    {
        return;
    }

    gs::WaitUntil([] { return (*R_EE_D1_CHCR & kDmaStart) == 0; }, "VIF1 DMA");
    gs::WaitUntil([] { return (*GS_REG_CSR & 2U) != 0; }, "GS FINISH");

    s_inFlight = false;
}

const Stats & GetStats()
{
    return s_stats;
}

} // namespace ps2::renderer::dma
