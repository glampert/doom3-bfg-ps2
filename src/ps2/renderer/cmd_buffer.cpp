// ================================================================================================
// File: cmd_buffer.cpp
// Brief: Submit bounded CPU GIF chains without VU programs or world-loader scratch ownership.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/cmd_buffer.h"
#include "ps2/renderer/pipeline_wait.h"
#include "ps2/system/heap.h"
#include <dma.h>
#include <gs_gp.h>
#include <idlib/precompiled.h>
#include <kernel.h>
#include <packet2_chain.h>
#include <packet2_utils.h>
#include <packet2_vif.h>

namespace ps2::cmdbuf
{
namespace
{
static constexpr int kTerminatorQwords = 4;
static constexpr u32 kDmaStart = 1U << 8;
static packet2_t s_packets[2] = {};
static void * s_storage = nullptr;
static int s_half = 0;
static int s_kickedQwords = 0;
static bool s_frame = false;
static bool s_inFlight = false;
static qword_t * s_directBase = nullptr;
static int s_directCapacity = 0;
static Stats s_stats = {};

packet2_t * Current()
{
    return &s_packets[s_half];
}

int Used()
{
    return static_cast<int>(packet2_get_qw_count(Current()));
}

void Reset()
{
    packet2_reset(Current(), 0);
    s_kickedQwords = 0;
}

void RecordPeak()
{
    const u32 bytes = packet2_get_qw_count(Current()) * 16U;
    if (bytes > s_stats.peakBytes)
    {
        s_stats.peakBytes = bytes;
    }
}
} // namespace

bool IsInitialized()
{
    return s_storage != nullptr;
}

PS2_COLD_FUNC bool Init(u32 halfBytes)
{
    if (IsInitialized())
    {
        FatalError("command buffer initialized twice");
    }
    if (halfBytes < 256U || (halfBytes & 63U) != 0 || halfBytes / 16U > 65535U)
    {
        Log(LogLevel::Error, "invalid command half size: %u\n", halfBytes);
        return false;
    }
    s_storage = heap::TryAlloc(static_cast<size_t>(halfBytes) * 2U, TAG_RENDER, 64);
    if (s_storage == nullptr)
    {
        return false;
    }
    u8 * const bytes = static_cast<u8 *>(s_storage);
    for (int half = 0; half < 2; ++half)
    {
        // Own the packet headers directly too: packet2_create_from would malloc them.
        void * const address = bytes + static_cast<size_t>(half) * halfBytes;
        s_packets[half] = {};
        s_packets[half].max_qwords_count = static_cast<u16>(halfBytes / 16U);
        s_packets[half].type = P2_TYPE_NORMAL;
        s_packets[half].mode = P2_MODE_CHAIN;
        s_packets[half].tte = 1;
        s_packets[half].base = static_cast<qword_t *>(address);
        packet2_reset(&s_packets[half], 0);
    }
    s_half = 0;
    s_kickedQwords = 0;
    s_stats = {};
    gs::WaitUntil([]
                  { return (*R_EE_D1_CHCR & kDmaStart) == 0; }, "initial VIF1 DMA");
    if (dma_channel_initialize(DMA_CHANNEL_VIF1, nullptr, 0) < 0)
    {
        heap::Free(s_storage);
        s_storage = nullptr;
        s_packets[0] = {};
        s_packets[1] = {};
        return false;
    }
    return true;
}

void WaitIdle()
{
    if (!s_inFlight)
    {
        return;
    }
    gs::WaitUntil([]
                  { return (*R_EE_D1_CHCR & kDmaStart) == 0; }, "VIF1 DMA");
    gs::WaitUntil([]
                  { return (*GS_REG_CSR & 2U) != 0; }, "GS FINISH");
    s_inFlight = false;
}

PS2_COLD_FUNC void Shutdown()
{
    if (!IsInitialized())
    {
        return;
    }
    if (s_directBase != nullptr)
    {
        FatalError("command shutdown with an open DIRECT payload");
    }
    // Preserve any completed work even during partial frame teardown.
    Kick();
    WaitIdle();
    dma_channel_shutdown(DMA_CHANNEL_VIF1, 0);
    heap::Free(s_storage);
    s_storage = nullptr;
    s_packets[0] = {};
    s_packets[1] = {};
    s_frame = false;
    s_half = 0;
    s_kickedQwords = 0;
}

void BeginFrame()
{
    if (!IsInitialized() || s_frame || s_directBase != nullptr || Used() != s_kickedQwords)
    {
        FatalError("invalid command BeginFrame state");
    }
    WaitIdle();
    s_half ^= 1;
    Reset();
    s_frame = true;
}

GifWriter BeginDirect(int payloadQwords)
{
    if (!s_frame || s_directBase != nullptr || payloadQwords < 0)
    {
        FatalError("invalid DIRECT reservation state");
    }
    const int capacity = Current()->max_qwords_count - kTerminatorQwords;
    // One CNT tag and one final EOP qword belong to this block.
    if (payloadQwords > capacity - 2)
    {
        FatalError("DIRECT request exceeds command half capacity");
    }
    const int request = payloadQwords + 2;
    if (Used() > capacity - request)
    {
        Kick();
        WaitIdle();
        s_half ^= 1;
        Reset();
        ++s_stats.drains;
    }
    packet2_t * const packet = Current();
    packet2_chain_open_cnt(packet, 0, 0, 0);
    packet2_vif_nop(packet, 0);
    packet2_vif_open_direct(packet, 0);
    s_directBase = packet->next;
    s_directCapacity = payloadQwords;
    return GifWriter(s_directBase, payloadQwords);
}

void EndDirect(const GifWriter & writer)
{
    if (s_directBase == nullptr || writer.Base() != s_directBase || writer.QwordCount() > s_directCapacity)
    {
        FatalError("closing a foreign or oversized DIRECT payload");
    }
    packet2_t * const packet = Current();
    packet->next = writer.Cursor();
    // Close every GIF packet even if the payload was empty. No competing PATH3 writer.
    PACK_GIFTAG(packet->next, GIF_SET_TAG(0, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
    ++packet->next;
    packet2_vif_close_direct_manual(packet, static_cast<u32>(writer.QwordCount() + 1));
    packet2_chain_close_tag(packet);
    s_directBase = nullptr;
    s_directCapacity = 0;
    RecordPeak();
}

void Kick()
{
    if (!IsInitialized() || s_directBase != nullptr)
    {
        FatalError("invalid command Kick state");
    }
    if (Used() == s_kickedQwords)
    {
        return;
    }
    WaitIdle();
    PS2_Assert(Current()->tag_opened_at == nullptr && Current()->vif_code_opened_at == nullptr);
    packet2_t * const packet = Current();
    // Reserved ahead of every writer: CNT + FINISH tag/data + END = four qwords.
    packet2_chain_open_cnt(packet, 0, 0, 0);
    packet2_vif_flush(packet, 0);
    packet2_vif_open_direct(packet, 0);
    PACK_GIFTAG(packet->next, GIF_SET_TAG(1, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
    ++packet->next;
    PACK_GIFTAG(packet->next, 1, GS_REG_FINISH);
    ++packet->next;
    packet2_vif_close_direct_manual(packet, 2);
    packet2_chain_close_tag(packet);
    packet2_utils_vu_add_end_tag(packet);
    RecordPeak();
    FlushCache(0); // SDK chain send uses qwc=0, so its implicit writeback is insufficient.
    void * const segment = reinterpret_cast<void *>(reinterpret_cast<uiptr>(packet->base + s_kickedQwords) & 0x0FFFFFFFU);
    *GS_REG_CSR = 2; // Clear only FINISH: all event bits are write-one-to-clear.
    dma_channel_send_chain(DMA_CHANNEL_VIF1, segment, 0, DMA_FLAG_TRANSFERTAG, 0);
    s_kickedQwords = Used();
    s_inFlight = true;
    ++s_stats.kicks;
}

void EndFrame()
{
    if (!s_frame || s_directBase != nullptr)
    {
        FatalError("invalid command EndFrame state");
    }
    Kick();
    WaitIdle();
    s_frame = false;
}

Stats GetStats()
{
    return s_stats;
}
} // namespace ps2::cmdbuf
