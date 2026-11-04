// ================================================================================================
// File: dma.cpp
// Brief: The display list system over the scratchpad DisplayList: per-frame chain rings, the
//        frame boundaries and the VIF1 kick of each chain the list closes. The rings are written by
//        the fromSPR DMA alone, so a kick needs no cache writeback; the EE writes the scratchpad.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/dma.h"
#include "ps2/renderer/dma_list.h"
#include "ps2/renderer/pipeline_wait.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <idlib/precompiled.h> // memTag_t for the ring allocation
#include <dma.h>
#include <ee_regs.h>
#include <gs_privileged.h>
#include <kernel.h>

#include <algorithm>

namespace ps2::renderer::dma
{
namespace
{

static constexpr u32 kDmaStart = 1U << 8;
static ChainRing s_rings[2] = {};
static void * s_storage = nullptr;
static int s_ring = 0;
static Stats s_stats = {};

} // namespace

namespace detail
{

DisplayList g_list;
bool g_inFrame = false;
bool g_inFlight = false;

} // namespace detail

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

    s_ring = 0;
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

    detail::g_list.Begin(s_rings[s_ring]);
    detail::g_list.ResetCounters();
    return true;
}

PS2_COLD_FUNC void Shutdown()
{
    if (!IsInitialized())
    {
        return;
    }

    if (detail::g_list.InDirect()) [[unlikely]]
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
    detail::g_inFrame = false;
    s_ring = 0;
}

bool IsInitialized()
{
    return s_storage != nullptr;
}

void BeginFrame()
{
    PS2_AssertMsg(IsInitialized() && !detail::g_inFrame && !detail::g_list.HasChain(), "invalid BeginFrame state");

    // No wait: Kick ran the chain before the last one to completion before it sent the last one,
    // so the ring about to be reused (two frames back) is free while the last chain still runs.
    s_ring ^= 1;
    detail::g_list.Begin(s_rings[s_ring]);
    detail::g_inFrame = true;
}

void EndFrame()
{
    PS2_AssertMsg(detail::g_inFrame && !detail::g_list.InDirect(), "invalid EndFrame state");

    // The chain is sent, not awaited: the EE may build the next frame while it runs. An immediate
    // present (gs::Present) or WaitIdle fences on it.
    Kick();
    detail::g_inFrame = false;
}

void Kick()
{
    PS2_AssertMsg(IsInitialized() && !detail::g_list.InDirect(), "invalid display list Kick state");

    if (!detail::g_list.HasChain())
    {
        return;
    }

    WaitIdle();

    const void * const chain = detail::g_list.CloseChain();
    s_stats.peakBytes = std::max(s_stats.peakBytes, detail::g_list.RingUsedBytes());

    *GS_REG_CSR = 2; // Clear only FINISH: all event bits are write-one-to-clear.
    dma_channel_send_chain(DMA_CHANNEL_VIF1, reinterpret_cast<void *>(PhysicalAddress(chain)), 0, DMA_FLAG_TRANSFERTAG, 0);

    detail::g_inFlight = true;
    ++s_stats.kicks;
}

void WaitIdle()
{
    if (!detail::g_inFlight)
    {
        return;
    }

    gs::WaitUntil([] { return (*R_EE_D1_CHCR & kDmaStart) == 0; }, "VIF1 DMA");
    gs::WaitUntil([] { return (*GS_REG_CSR & 2U) != 0; }, "GS FINISH");

    detail::g_inFlight = false;
}

const Stats & GetStats()
{
    s_stats.drains = detail::g_list.RingRestarts();
    s_stats.sprDrains = detail::g_list.SprDrains();
    return s_stats;
}

} // namespace ps2::renderer::dma
