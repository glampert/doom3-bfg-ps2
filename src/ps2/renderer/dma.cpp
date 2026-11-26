// ================================================================================================
// File: dma.cpp
// Brief: The display list system over the scratchpad DisplayList: per-frame chain rings, the
//        frame boundaries and the VIF1 kick of each chain the list closes. The rings are written by
//        the fromSPR DMA alone, so a kick needs no cache writeback; the EE writes the scratchpad.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/dma.h"
#include "ps2/debug/log_mirror.h"
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

// Chains kicked since boot, never reset (ChainDone compares against numbers taken before an Init).
static u32 s_chains = 0;

// BeforeNextSend's action, run by the Kick that sends the next chain.
static void (*s_beforeSend)() = nullptr;

#if PS2_D3BFG_GPU_SAMPLER
static GpuStats s_gpu = {};

// What holds the chain up now (GpuState). VPU-STAT is VU0 control register 29: VBS1 (bit 8) VU1
// runs, VGW1 (bit 12) it waits on an XGKICK. VIF1_STAT: VGW (bit 3) a FLUSH, FLUSHA, DIRECT or MSCALF
// waits for the GIF; VPS (bits 1:0) not idle. GIF_STAT: APATH (bits 11:10) the path sending.
static GpuState Classify()
{
    u32 vpu;
    asm volatile("cfc2 %0, $29" : "=r"(vpu));

    if ((vpu & (1U << 8)) != 0)
    {
        return (vpu & (1U << 12)) != 0 ? GpuState::Gs : GpuState::Vu;
    }

    const u32 vif = *R_EE_VIF1_STAT;

    if ((vif & (1U << 3)) != 0)
    {
        return GpuState::Gs;
    }

    const u32 path = (*R_EE_GIF_STAT >> 10) & 3U;

    if (path == 1U)
    {
        return GpuState::Gs;
    }

    if (path != 0U)
    {
        return GpuState::Direct;
    }

    return (vif & 3U) != 0 || (*R_EE_D1_CHCR & kDmaStart) != 0 ? GpuState::Vif : GpuState::Tail;
}

// Charges the cycles since 'last' to what holds the chain up now.
static void Sample(u32 & last)
{
    const GpuState state = Classify();
    const u32 now = gs::ReadCycles();
    s_gpu.cycles[static_cast<int>(state)] += now - last;
    ++s_gpu.samples;
    last = now;
}

// VU1 data memory as the EE sees it (EE User's Manual, the memory map: VU Mem1).
static constexpr uintptr_t kVu1DataMemory = 0x1100C000U;

// world_lm's list-path counts in VU1 data memory (vu::FrameConstants::stats, four floats).
static volatile float * ListPathCounts()
{
    return reinterpret_cast<volatile float *>(kVu1DataMemory + vu::kListStatsQword * 16U);
}

// Zeroes the counts: at Init, over whatever VU1 memory held, and after each read. Only with VU1
// and the DMA idle.
static void ClearListPathStats()
{
    volatile float * const counts = ListPathCounts();

    for (int lane = 0; lane < 4; ++lane)
    {
        counts[lane] = 0.0f;
    }
}

// Adds what world_lm counted since the last read and zeroes it, so a chain the frame constants did
// not open (a ring drain's second part, a frame without the world) counts on from zero.
static void TakeListPathStats()
{
    // Whole counts below 2^24: through s32, a cvt.w.s, rather than libgcc's unsigned conversion.
    const volatile float * const counts = ListPathCounts();
    ListPathStats & stats = s_gpu.listPath;
    stats.batches += static_cast<u32>(static_cast<s32>(counts[0]));
    stats.entries += static_cast<u32>(static_cast<s32>(counts[1]));
    stats.clips += static_cast<u32>(static_cast<s32>(counts[2]));
    stats.empty += static_cast<u32>(static_cast<s32>(counts[3]));
    ClearListPathStats();
}
#endif // PS2_D3BFG_GPU_SAMPLER

} // namespace

namespace detail
{

DisplayList g_list;
bool g_inFrame = false;
bool g_inFlight = false;

} // namespace detail

PS2_COLD_FUNC bool Init(u32 ringBytes)
{
    // The boot screen drives the GIF's channel by hand; from here on the display list owns it.
    ps2::debug::EndBootScreen();

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

#if PS2_D3BFG_GPU_SAMPLER
    ClearListPathStats();
#endif

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
    s_beforeSend = nullptr;
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
    // present (gs::Present) or WaitIdle fences on it; a pipelined one runs in the next frame's Kick.
#if PS2_D3BFG_FRAME_LOG
    s_stats.frameBytes = detail::g_list.RingUsedBytes();
#endif // PS2_D3BFG_FRAME_LOG
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

    if (s_beforeSend != nullptr)
    {
        void (*const action)() = s_beforeSend;
        s_beforeSend = nullptr;
        action();
    }

    const void * const chain = detail::g_list.CloseChain();
    s_stats.peakBytes = std::max(s_stats.peakBytes, detail::g_list.RingUsedBytes());

    *GS_REG_CSR = 2; // Clear only FINISH: all event bits are write-one-to-clear.
    dma_channel_send_chain(DMA_CHANNEL_VIF1, reinterpret_cast<void *>(PhysicalAddress(chain)), 0, DMA_FLAG_TRANSFERTAG, 0);

    detail::g_inFlight = true;
    ++s_stats.kicks;
    ++s_chains;
}

void BeforeNextSend(void (*action)())
{
    s_beforeSend = action;
}

u32 OpenChain()
{
    return s_chains + 1U;
}

bool ChainDone(u32 chain)
{
    return s_chains > chain || (s_chains == chain && !detail::g_inFlight);
}

void WaitIdle()
{
    if (!detail::g_inFlight)
    {
        return;
    }

    // The frame log's GPU column: how long the EE stood still for VU1 and the GS.
#if PS2_D3BFG_FRAME_LOG || PS2_D3BFG_GPU_SAMPLER
    const u32 start = gs::ReadCycles();
#endif

#if PS2_D3BFG_GPU_SAMPLER
    u32 last = start;
    gs::WaitUntil([&last]
                  {
                      Sample(last);
                      return (*R_EE_D1_CHCR & kDmaStart) == 0;
                  },
                  "VIF1 DMA");
    gs::WaitUntil([&last]
                  {
                      Sample(last);
                      return (*GS_REG_CSR & 2U) != 0;
                  },
                  "GS FINISH");
#else // PS2_D3BFG_GPU_SAMPLER
    gs::WaitUntil([] { return (*R_EE_D1_CHCR & kDmaStart) == 0; }, "VIF1 DMA");
    gs::WaitUntil([] { return (*GS_REG_CSR & 2U) != 0; }, "GS FINISH");
#endif // PS2_D3BFG_GPU_SAMPLER

#if PS2_D3BFG_FRAME_LOG
    s_stats.waitCycles += gs::ReadCycles() - start;
    ++s_stats.waits;
#endif // PS2_D3BFG_FRAME_LOG

    // After the wait is timed: the sampler's split covers the wait alone.
#if PS2_D3BFG_GPU_SAMPLER
    TakeListPathStats();
#endif // PS2_D3BFG_GPU_SAMPLER

    detail::g_inFlight = false;
}

#if PS2_D3BFG_GPU_SAMPLER
const GpuStats & GetGpuStats()
{
    return s_gpu;
}
#endif // PS2_D3BFG_GPU_SAMPLER

const Stats & GetStats()
{
    s_stats.drains = detail::g_list.RingRestarts();
    s_stats.sprDrains = detail::g_list.SprDrains();
    return s_stats;
}

} // namespace ps2::renderer::dma
