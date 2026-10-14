// ================================================================================================
// File: renderer_gs_tests.cpp
// Brief: Verify context flips, clipping, solid alpha, pressure and exact cleanup in one GS scene.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/renderer_gs_tests.h"
#include "ps2/renderer/render_system.h"
#include "ps2/renderer/pipeline_wait.h"
#include "ps2/system/heap.h"
#include <kernel.h>
#include <delaythread.h>
#include <stdio.h>
#include <string.h>
#include <timer.h>
#include <climits>

namespace ps2::smoketests
{
namespace
{
static constexpr gs::Color kColors[] = {
    {255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 255, 255},
    {255, 255, 0, 255}, {0, 255, 255, 255}, {255, 0, 255, 255}, {128, 128, 128, 255},
    {32, 32, 32, 255}, {64, 64, 64, 255}, {128, 128, 128, 255}, {192, 192, 192, 255},
    {255, 128, 0, 255}, {0, 128, 255, 255}, {128, 0, 255, 255}, {0, 0, 0, 255}
};

bool Check(const char * name, bool passed)
{
    Log(LogLevel::Info, "[D3BFG] CHECK renderer-gs/%s %s\n", name, passed ? "PASS" : "FAIL");
    return passed;
}
bool SameLedger(const heap::Stats & before)
{
    const auto after = heap::GetTotalStats();
    return after.requestedBytes == before.requestedBytes && after.backingBytes == before.backingBytes &&
           after.allocationCount == before.allocationCount;
}
void DrawScene(u32 frame)
{
    rs::BeginFrame({16, 24, 40, 255});
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            rs::FillRect(24 + column * 120, 24 + row * 64, 104, 48, kColors[row * 4 + column]);
        }
    }
    // Opaque primaries, a 50% white layer and a 50% black layer; order is intentional.
    rs::FillRect(24, 296, 224, 88, {255, 0, 0, 255});
    rs::FillRect(136, 312, 224, 56, {255, 255, 255, 128});
    rs::FillRect(304, 296, 184, 88, {0, 0, 255, 255});
    rs::FillRect(360, 312, 128, 56, {0, 0, 0, 128});
    // Offscreen and enormous legal coordinates cannot wrap back into the picture.
    rs::FillRect(INT_MAX, INT_MAX, INT_MAX, INT_MAX, {255, 0, 255, 255});
    rs::FillRect(16, 16, 16, 16, {255, 0, 255, 0});
    rs::FillRect(-10, -10, gs::Width() + 20, 14, {255, 255, 255, 255});
    rs::FillRect(-10, gs::Height() - 4, gs::Width() + 20, 14, {255, 255, 255, 255});
    rs::FillRect(-10, 0, 14, gs::Height(), {255, 255, 255, 255});
    rs::FillRect(gs::Width() - 4, 0, 14, gs::Height(), {255, 255, 255, 255});
    // A moving marker proves distinct frames; yellow marks the final 120-frame picture.
    rs::FillRect(24 + static_cast<int>(frame % 440U), 400, 16, 16,
                 frame == 119U ? gs::Color{255, 255, 0, 255} : gs::Color{255, 255, 255, 255});
    rs::EndFrame();
}
bool HoldPresentedScene(video::Standard standard)
{
    FILE * file = fopen("host:renderer.ready", "wb");
    if (file == nullptr) { return false; }
    const int written = fprintf(file, "GS_SCENE 1 %s 512 %d 120\n", video::Name(standard), gs::Height());
    const bool closed = fclose(file) == 0;
    if (written <= 0 || !closed) { return false; }
    Log(LogLevel::Info, "[D3BFG] GS scene presented; waiting for renderer.resume\n");
    const u64 start = GetTimerSystemTime();
    // The SDK multiplies its seconds argument in u32 before returning u64.
    const u64 timeout = TimerUSec2BusClock(1, 0) * 90U;
    while (GetTimerSystemTime() - start < timeout)
    {
        FILE * resume = fopen("host:renderer.resume", "rb");
        if (resume != nullptr) { return fclose(resume) == 0; }
        if (DelayThread(10000) < 0) { return false; }
    }
    return false;
}
} // namespace

bool RunGsTests(video::Standard standard)
{
    const auto baseline = heap::GetTotalStats();
    bool passed = Check("inactive-start", !rs::IsInitialized() && !gs::IsInitialized() && !cmdbuf::IsInitialized());
    rs::Shutdown();
    const bool invalid = !rs::Init({static_cast<video::Standard>(99), 128U * 1024U}) && !cmdbuf::Init(65);
    const bool partial = cmdbuf::Init(1024) && !gs::Init({standard, 640, video::VisibleLines(standard)});
    rs::Shutdown();
    passed = Check("rejected-partial-init", invalid && partial && SameLedger(baseline) && !cmdbuf::IsInitialized()) && passed;
    bool contexts = true;
    bool bounds = true;
    bool pressure = true;
    bool cleanup = true;
    bool held = true;
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        const u32 halfBytes = cycle == 0 ? 128U * 1024U : 1024U;
        if (!rs::Init({standard, halfBytes})) { return Check("device-init", false); }
        bounds = bounds && gs::Width() == 512 && gs::Height() == video::VisibleLines(standard) &&
                 gs::FixedVramBytes() == (standard == video::Standard::Pal ? 1572864U : 1376256U);
        const u32 frames = cycle == 0 ? 120U : 4U;
        for (u32 frame = 0; frame < frames; ++frame)
        {
            DrawScene(frame);
            const auto expected = (frame & 1U) == 0 ? gs::DrawContext::Ctx1 : gs::DrawContext::Ctx0;
            contexts = contexts && !rs::FrameStarted() && rs::FrameCount() == frame + 1U &&
                       rs::LastPresentedContext() == expected;
        }
        Log(LogLevel::Info, "[D3BFG] GS scene %s cycle=%d frames=%u context=%d\n",
            video::Name(standard), cycle, rs::FrameCount(), static_cast<int>(rs::LastPresentedContext()));
        if (cycle == 0) { held = HoldPresentedScene(standard); }
        // Force one 128 KiB half to overflow, and many 1 KiB halves on the reload.
        rs::BeginFrame({0, 0, 0, 255});
        for (int draw = 0; draw < 1024; ++draw)
        {
            rs::FillRect(draw % 512, (draw / 512) * 8, 1, 8, {255, 255, 255, 128});
        }
        rs::EndFrame();
        const auto stats = cmdbuf::GetStats();
        pressure = pressure && stats.drains > 0 && stats.kicks > frames + 1U && stats.peakBytes <= halfBytes;
        Log(LogLevel::Info, "[D3BFG] GS chains half=%u peak=%u kicks=%u drains=%u\n",
            halfBytes, stats.peakBytes, stats.kicks, stats.drains);
        const auto live = heap::GetTotalStats();
        const auto arena = heap::GetArenaStats();
        Log(LogLevel::Info, "[D3BFG] GS heap cycle=%d live=%zu/%zu/%zu peak=%zu/%zu arena_peak=%zu\n",
            cycle, live.requestedBytes, live.backingBytes, live.allocationCount,
            live.peakRequestedBytes, live.peakBackingBytes, arena.peakCommittedBytes);
        rs::Shutdown();
        rs::Shutdown();
        cleanup = cleanup && !rs::IsInitialized() && !gs::IsInitialized() && !cmdbuf::IsInitialized() && SameLedger(baseline);
    }
    passed = Check("device-init", bounds) && passed;
    passed = Check("context-frames", contexts) && passed;
    passed = Check("presented-scene", held) && passed;
    passed = Check("bounded-pressure", pressure) && passed;
    passed = Check("reload-shutdown-ledger", cleanup) && passed;
    Log(LogLevel::Info, "[D3BFG] GS memory baseline=%zu/%zu/%zu final=%zu/%zu/%zu\n",
        baseline.requestedBytes, baseline.backingBytes, baseline.allocationCount,
        heap::GetTotalStats().requestedBytes, heap::GetTotalStats().backingBytes, heap::GetTotalStats().allocationCount);
    return passed;
}

bool RunGsFailureProbe(const char * name)
{
    if (strcmp(name, "renderer-gif-overflow") != 0 && strcmp(name, "renderer-direct-overflow") != 0 &&
        strcmp(name, "renderer-wait-timeout") != 0) { return false; }
    Log(LogLevel::Info, "[D3BFG] PROBE %s BEGIN\n", name);
    Log(LogLevel::Info, "[D3BFG] CHECK probe/ready PASS\n");
    if (strcmp(name, "renderer-gif-overflow") == 0)
    {
        alignas(64) qword_t memory[2] = {};
        GifWriter writer(memory, 1);
        writer.SetRegister(0, 0);
    }
    else if (strcmp(name, "renderer-direct-overflow") == 0)
    {
        if (!cmdbuf::Init(1024)) { FatalError("probe command initialization failed"); }
        cmdbuf::BeginFrame();
        (void)cmdbuf::BeginDirect(INT_MAX);
    }
    else if (strcmp(name, "renderer-wait-timeout") == 0)
    {
        gs::WaitUntil([] { return false; }, "injected wait");
    }
    else { return false; }
    FatalError("GS failure probe unexpectedly returned");
}
} // namespace ps2::smoketests
