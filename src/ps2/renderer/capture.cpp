// ================================================================================================
// File: capture.cpp
// Brief: Reverse the owned VIF1/GS bus only for bounded row transfers, restoring it on every failure.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/capture.h"
#include "ps2/renderer/pipeline_wait.h"
#include "ps2/renderer/render_system.h"

#include <cstdio>
#include <dma.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <kernel.h>
#include <vif_codes.h>

namespace ps2::renderer::gs
{
namespace
{

// TODO: Make this debug only; holds large static buffers - not needed besides dev/testing.
// Also switch to using heap-allocated temp buffers rather than always allocated static buffers.
static constexpr int kWidth = 512;
static constexpr int kStripRows = 32;
static constexpr u32 kDmaStart = 1U << 8;
static constexpr u32 kDmacSuspend = 1U << 16;
static constexpr u32 kVifDirection = 1U << 23;
static constexpr u32 kVifFifoCount = 0x1FU << 24;
static constexpr u32 kPath3VifMask = 1U << 1;
static constexpr u64 kFinish = 1U << 1;
static constexpr u64 kFinishMask = 1U << 9;
static constexpr u32 kTimeoutCycles = 294912000U;

// Complete cache lines remain owned even after a stalled receive. Never release a target
// to the heap while the DMA engine might still address it. PSMCT16 plus BGR costs 80 KiB.
alignas(64) static u16 s_pixels[kWidth * kStripRows];
alignas(64) static u8 s_bgr[kWidth * kStripRows * 3];
alignas(64) static u64 s_request[16];
alignas(64) static u32 s_restorePath3[16];
static bool s_active = false;

#if PS2_D3BFG_TESTS
static bool s_injectReadbackTimeout = false;
#endif // PS2_D3BFG_TESTS

template<typename Ready>
bool TryWait(Ready ready, const char * name)
{
    const u32 start = gs::ReadCycles();
    while (!ready())
    {
        if (gs::ReadCycles() - start >= kTimeoutCycles)
        {
            Log(LogLevel::Error, "GS capture timeout: %s; D1_CHCR=%08x VIF1_STAT=%08x GIF_STAT=%08x CSR=%08llx",
                name, *R_EE_D1_CHCR, *R_EE_VIF1_STAT, *R_EE_GIF_STAT, *GS_REG_CSR);
            return false;
        }
    }
    return true;
}

struct SavedPipeline
{
    u64 imr;
    u64 busDirection;
    u32 vifDirection;
    u32 channelControl;
    u32 gifMode;
};

void RestoreDirection(const SavedPipeline & saved)
{
    *R_EE_VIF1_STAT = saved.vifDirection;
    *GS_REG_BUSDIR = saved.busDirection;
    EE_SYNCL();
}

PS2_COLD_FUNC void RecoverPipeline(const SavedPipeline & saved)
{
    // ps2sdk's MPEG path suspends the DMAC before clearing an active channel's STR.
    // Keep other channels' global suspend and interrupt state intact around that operation.
    const int interrupts = DIntr();
    const u32 enable = *R_EE_D_ENABLER;
    *R_EE_D_ENABLEW = enable | kDmacSuspend;
    EE_SYNCL();
    *R_EE_D1_CHCR &= ~kDmaStart;
    *R_EE_D1_QWC = 0;
    EE_SYNCL();
    *R_EE_D_ENABLEW = enable;
    if (interrupts != 0)
    {
        EIntr();
    }

    // No frame or PATH3 upload is in flight here. Discard incomplete capture commands
    // rather than let a delayed DIRECT turn the bus around after its caller returned.
    *R_EE_VIF1_FBRST = 1;
    *R_EE_VIF1_FBRST = 0;
    *R_EE_GIF_CTRL = 1;
    *R_EE_GIF_CTRL = 0;
    *R_EE_GIF_MODE = saved.gifMode;
    RestoreDirection(saved);
    *R_EE_D1_CHCR = saved.channelControl;
    *GS_REG_CSR = kFinish;
    GsPutIMR(saved.imr);
    EE_SYNCP();
}

PS2_COLD_FUNC bool ReadRows(const gs::FramebufferInfo & frame, int y, int rows)
{
    PS2_Assert(y >= 0 && rows > 0 && rows <= kStripRows && y + rows <= frame.height);

    // The profile uses full, aligned rows, so neither the request nor target shares a
    // cache line with unrelated data. Both DMA channels must already have drained.
    if (frame.width != kWidth || frame.pixelFormat != GS_PSM_16 || y < 0 || rows <= 0 ||
        rows > kStripRows || y > frame.height - rows ||
        (*R_EE_D1_CHCR & kDmaStart) != 0 || (*R_EE_D2_CHCR & kDmaStart) != 0 ||
        (*R_EE_VIF1_STAT & (kVifDirection | kVifFifoCount)) != 0 ||
        (*GS_REG_BUSDIR & 1U) != 0 || (*R_EE_GIF_STAT & kPath3VifMask) != 0)
    {
        Log(LogLevel::Error, "GS capture requires an idle forward, unmasked pipeline");
        return false;
    }

    const SavedPipeline saved = {
        GsGetIMR(), *GS_REG_BUSDIR & 1U, *R_EE_VIF1_STAT & kVifDirection,
        *R_EE_D1_CHCR, *R_EE_GIF_MODE
    };

    const int bytes = frame.width * rows * static_cast<int>(sizeof(u16));
    u32 * const codes = static_cast<u32 *>(static_cast<void *>(s_request));

    codes[0] = VIF_CODE(0, 0, VIF_CMD_NOP, 0);
    codes[1] = VIF_CODE(0x8000, 0, VIF_CMD_MSKPATH3, 0);
    codes[2] = VIF_CODE(0, 0, VIF_CMD_FLUSHA, 0);
    codes[3] = VIF_CODE(6, 0, VIF_CMD_DIRECT, 0);
    s_request[2] = GIF_SET_TAG(5, 1, 0, 0, GIF_FLG_PACKED, 1);
    s_request[3] = GIF_REG_AD;
    s_request[4] = GS_SET_BITBLTBUF(frame.address >> 6, frame.width >> 6, frame.pixelFormat, 0, 0, frame.pixelFormat);
    s_request[5] = GS_REG_BITBLTBUF;
    s_request[6] = GS_SET_TRXPOS(0, y, 0, 0, 0);
    s_request[7] = GS_REG_TRXPOS;
    s_request[8] = GS_SET_TRXREG(frame.width, rows);
    s_request[9] = GS_REG_TRXREG;
    s_request[10] = 0;
    s_request[11] = GS_REG_FINISH;
    s_request[12] = GS_SET_TRXDIR(1);
    s_request[13] = GS_REG_TRXDIR;
    s_restorePath3[0] = VIF_CODE(0, 0, VIF_CMD_MSKPATH3, 0);
    s_restorePath3[1] = s_restorePath3[2] = s_restorePath3[3] = VIF_CODE(0, 0, VIF_CMD_NOP, 0);

    GsPutIMR(saved.imr | kFinishMask);
    *GS_REG_CSR = kFinish;
    const bool requested = dma_channel_send_normal(DMA_CHANNEL_VIF1, s_request, 7, 0, 0) == 0 &&
                           TryWait([] { return (*R_EE_D1_CHCR & kDmaStart) == 0; }, "request DMA") &&
                           TryWait([] { return (*GS_REG_CSR & kFinish) != 0; }, "request FINISH") &&
                           TryWait([] { return (*R_EE_VIF1_STAT & kVifFifoCount) == 0; }, "request FIFO");
    if (!requested)
    {
        RecoverPipeline(saved);
        return false;
    }

    *R_EE_VIF1_STAT = kVifDirection;
    *GS_REG_BUSDIR = GS_SET_BUSDIR(1);
    EE_SYNCL();
    void * const end = static_cast<u8 *>(static_cast<void *>(s_pixels)) + bytes;
    SyncDCache(s_pixels, end);
    bool injected = false;

#if PS2_D3BFG_TESTS
    injected = s_injectReadbackTimeout;
    s_injectReadbackTimeout = false;
#endif // PS2_D3BFG_TESTS

    const bool received = dma_channel_receive_normal(DMA_CHANNEL_VIF1, s_pixels, bytes, 0, 0) == 0 &&
                          TryWait([injected] { return !injected && (*R_EE_D1_CHCR & kDmaStart) == 0; }, "receive DMA");
    if (!received)
    {
        RecoverPipeline(saved);
        return false;
    }

    InvalidDCache(s_pixels, end);
    RestoreDirection(saved);
    *GS_REG_CSR = kFinish;

    const bool restored = dma_channel_send_normal(DMA_CHANNEL_VIF1, s_restorePath3, 1, 0, 0) == 0 &&
                          TryWait([] { return (*R_EE_D1_CHCR & kDmaStart) == 0; }, "PATH3 restore DMA") &&
                          TryWait([] { return (*R_EE_GIF_STAT & kPath3VifMask) == 0; }, "PATH3 restore") &&
                          TryWait([] { return (*R_EE_VIF1_STAT & kVifFifoCount) == 0; }, "PATH3 restore FIFO");
    if (!restored)
    {
        RecoverPipeline(saved);
        return false;
    }

    *R_EE_D1_CHCR = saved.channelControl;
    GsPutIMR(saved.imr);
    return true;
}

PS2_COLD_FUNC void ToBgr(int count)
{
    for (int i = 0; i < count; ++i)
    {
        const u32 r = s_pixels[i] & 31U;
        const u32 g = (s_pixels[i] >> 5) & 31U;
        const u32 b = (s_pixels[i] >> 10) & 31U;
        // Repeat the upper bits when expanding RGB5, so the maximum remains 255.
        s_bgr[i * 3] = static_cast<u8>((b << 3) | (b >> 2));
        s_bgr[i * 3 + 1] = static_cast<u8>((g << 3) | (g >> 2));
        s_bgr[i * 3 + 2] = static_cast<u8>((r << 3) | (r >> 2));
    }
}

PS2_COLD_FUNC bool WriteFrame(std::FILE * file, const gs::FramebufferInfo & frame)
{
    u8 header[18] = {};
    header[2] = 2;
    header[12] = static_cast<u8>(frame.width & 255);
    header[13] = static_cast<u8>(frame.width >> 8);
    header[14] = static_cast<u8>(frame.height & 255);
    header[15] = static_cast<u8>(frame.height >> 8);
    header[16] = 24;
    header[17] = 0x20; // Top-down rows, matching native GS coordinates.

    if (std::fwrite(header, 1, sizeof(header), file) != sizeof(header))
    {
        return false;
    }

    for (int y = 0; y < frame.height; y += kStripRows)
    {
        const int rows = frame.height - y < kStripRows ? frame.height - y : kStripRows;
        if (!ReadRows(frame, y, rows))
        {
            return false;
        }

        const int count = frame.width * rows;
        ToBgr(count);

        const size_t bytes = static_cast<size_t>(count) * 3U;
        if (std::fwrite(s_bgr, 1, bytes, file) != bytes)
        {
            return false;
        }
    }

    return std::ferror(file) == 0;
}

} // namespace

PS2_COLD_FUNC bool CaptureTga(const char * path, gs::DrawContext context)
{
    if (path == nullptr || path[0] == '\0' || s_active || !renderer::IsInitialized() ||
        !gs::IsInitialized() || renderer::FrameStarted() || renderer::FrameCount() == 0 ||
        context != renderer::LastPresentedContext())
    {
        Log(LogLevel::Error, "GS capture requires a path and the most recently completed frame");
        return false;
    }

    const gs::FramebufferInfo frame = gs::GetFramebuffer(context);
    if (frame.width != kWidth || frame.height <= 0 || frame.height > 512 || frame.pixelFormat != GS_PSM_16)
    {
        Log(LogLevel::Error, "GS capture supports only the fixed PSMCT16 device profile");
        return false;
    }

    std::FILE * const file = std::fopen(path, "wb");
    if (file == nullptr)
    {
        Log(LogLevel::Error, "GS capture could not open %s", path);
        return false;
    }

    s_active = true;
    bool written = WriteFrame(file, frame);
    if (std::fclose(file) != 0)
    {
        written = false;
    }
    s_active = false;

    if (!written)
    {
        Log(LogLevel::Error, "GS capture failed: %s", path);
    }
    return written;
}

#if PS2_D3BFG_TESTS
PS2_COLD_FUNC void InjectReadbackTimeoutOnce()
{
    s_injectReadbackTimeout = true;
}
#endif // PS2_D3BFG_TESTS

} // namespace ps2::renderer::gs
