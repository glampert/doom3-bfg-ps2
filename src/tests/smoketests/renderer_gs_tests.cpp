// ================================================================================================
// File: renderer_gs_tests.cpp
// Brief: Verify GS frames, textured alpha, residency hazards, native captures and exact cleanup.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/renderer_gs_tests.h"
#include "ps2/renderer/render_system.h"
#include "ps2/renderer/pipeline_wait.h"
#include "ps2/renderer/capture.h"
#include "ps2/renderer/vram.h"
#include "ps2/system/heap.h"
#include <kernel.h>
#include <delaythread.h>
#include <stdio.h>
#include <string.h>
#include <timer.h>
#include <climits>
#include <idlib/precompiled.h>

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
static tex::Handle s_textures[8] = {};
static u8 s_npot[19][40][4] = {};
static u8 s_checker[8][8][4] = {};
static u8 s_pressure[64][64][4] = {};
bool SameLedger(const heap::Stats & before);
bool SplitUpload(video::Standard standard)
{
    const auto before = heap::GetTotalStats();
    if (!rs::Init({standard, 1024U, 1024U * 1024U, 1024U * 1024U})) { return false; }
    // 512 KiB crosses the SDK's 32767-qword IMAGE limit and requires two REF chunks.
    auto * const pixels = static_cast<gs::Color *>(heap::TryAlloc(512U * 256U * 4U, TAG_RENDER, 64));
    if (pixels == nullptr) { rs::Shutdown(); return false; }
    for (int y = 0; y < 256; ++y)
    {
        for (int x = 0; x < 512; ++x)
        {
            pixels[y * 512 + x] = y < 128 ? (x < 256 ? gs::Color{255, 0, 0, 255} : gs::Color{0, 255, 0, 255}) :
                                            (x < 256 ? gs::Color{0, 0, 255, 255} : gs::Color{255, 255, 0, 255});
        }
    }
    const auto handle = tex::Create(512, 256, {}, pixels, 512U * 256U * 4U, 512U * 4U);
    heap::Free(pixels);
    bool passed = handle.id != 0;
    if (passed)
    {
        rs::BeginFrame({0, 0, 0, 255});
        passed = rs::TexturedRect(handle, 0, 0, 512, 256, 0, 0, 512, 256);
        rs::EndFrame();
        passed = capture::WriteTga("host:renderer-upload.tga", rs::LastPresentedContext()) && passed;
    }
    rs::Shutdown();
    return SameLedger(before) && passed;
}

bool CreateTextures()
{
    for (int y = 0; y < 19; ++y)
    {
        for (int x = 0; x < 37; ++x)
        {
            const gs::Color color = (x == 0 || y == 0) ? gs::Color{255, 0, 0, 255} :
                (x == 36 || y == 18) ? gs::Color{0, 0, 255, 255} :
                ((x / 4 + y / 4) & 1) != 0 ? gs::Color{0, 255, 0, 255} : gs::Color{255, 255, 0, 255};
            memcpy(s_npot[y][x], &color, 4);
        }
    }
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            const gs::Color color = ((x / 2 + y / 2) & 1) != 0 ? gs::Color{255, 0, 255, 255} : gs::Color{0, 255, 255, 255};
            memcpy(s_checker[y][x], &color, 4);
        }
    }
    const tex::Sampler linear = {tex::Filter::Linear, tex::Wrap::Clamp, tex::Wrap::Clamp};
    const tex::Sampler repeat = {tex::Filter::Nearest, tex::Wrap::Repeat, tex::Wrap::Repeat};
    s_textures[0] = tex::Create(37, 19, {}, s_npot, sizeof(s_npot), sizeof(s_npot[0]));
    s_textures[1] = tex::Create(37, 19, linear, s_npot, sizeof(s_npot), sizeof(s_npot[0]));
    s_textures[2] = tex::Create(8, 8, repeat, s_checker, sizeof(s_checker), sizeof(s_checker[0]));
    const gs::Color white = {255, 255, 255, 128}, black = {0, 0, 0, 128}, red = {255, 0, 0, 255};
    s_textures[3] = tex::Create(1, 1, {}, &white, sizeof(white), sizeof(white));
    s_textures[4] = tex::Create(1, 1, {}, &black, sizeof(black), sizeof(black));
    s_textures[5] = tex::Create(1, 1, {}, &red, sizeof(red), sizeof(red));
    for (int index = 6; index < 8; ++index)
    {
        const gs::Color color = index == 6 ? gs::Color{0, 0, 255, 255} : gs::Color{255, 255, 0, 255};
        for (auto & row : s_pressure) { for (auto & pixel : row) { memcpy(pixel, &color, 4); } }
        s_textures[index] = tex::Create(64, 64, {}, s_pressure, sizeof(s_pressure), sizeof(s_pressure[0]));
    }
    for (const auto handle : s_textures) { if (handle.id == 0) { return false; } }
    tex::Layout layout = {};
    const auto before = heap::GetTotalStats();
    return tex::MakeLayout(37, 19, {}, layout) && layout.stridePixels == 64 && layout.potWidth == 64 &&
        layout.potHeight == 32 && layout.bytes == 8192 &&
        tex::Create(37, 19, repeat, s_npot, sizeof(s_npot), sizeof(s_npot[0])).id == 0 &&
        tex::Create(37, 19, {}, s_npot, 1, sizeof(s_npot[0])).id == 0 &&
        tex::Create(512, 512, {}, s_npot, sizeof(s_npot), 2048).id == 0 &&
        !tex::Update(s_textures[0], s_npot, 1, sizeof(s_npot[0])) && SameLedger(before);
}

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
bool DrawScene(u32 frame, bool hazards)
{
    bool drawn = true;
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
    drawn = rs::TexturedRect(s_textures[3], 136, 312, 224, 56, 0, 0, 1, 1) && drawn;
    rs::FillRect(304, 296, 184, 88, {0, 0, 255, 255});
    drawn = rs::TexturedRect(s_textures[4], 360, 312, 128, 56, 0, 0, 1, 1) && drawn;
    drawn = rs::TexturedRect(s_textures[0], 24, 152, 148, 76, 0, 0, 37, 19) && drawn;
    drawn = rs::TexturedRect(s_textures[1], 184, 152, 148, 76, 0, 0, 37, 19) && drawn;
    drawn = rs::TexturedRect(s_textures[2], 344, 152, 144, 76, 0, 0, 32, 16) && drawn;
    // Region clamp must hold the blue NPOT right edge beyond its logical extent.
    drawn = rs::TexturedRect(s_textures[1], 24, 240, 148, 32, 37, 1, 74, 18) && drawn;
    drawn = rs::TexturedRect(s_textures[3], 344, 240, 144, 32, 0, 0, 1, 1, {128, 255, 128, 128}) && drawn;
    // Screen clipping preserves texture coordinates at the visible right edge.
    drawn = rs::TexturedRect(s_textures[0], -124, 280, 148, 12, 0, 0, 37, 19) && drawn;
    if (hazards)
    {
        const gs::Color red = {255, 0, 0, 255}, green = {0, 255, 0, 255};
        drawn = tex::Update(s_textures[5], &red, sizeof(red), sizeof(red)) && drawn;
        drawn = rs::TexturedRect(s_textures[5], 24, 400, 48, 16, 0, 0, 1, 1) && drawn;
        drawn = tex::Update(s_textures[5], &green, sizeof(green), sizeof(green)) && drawn;
        drawn = rs::TexturedRect(s_textures[5], 80, 400, 48, 16, 0, 0, 1, 1) && drawn;
        drawn = rs::TexturedRect(s_textures[6], 144, 400, 48, 16, 0, 0, 64, 64) && drawn;
        drawn = rs::TexturedRect(s_textures[7], 200, 400, 48, 16, 0, 0, 64, 64) && drawn;
        // Rebind an evicted image after queued references have used the same pages.
        drawn = rs::TexturedRect(s_textures[0], 264, 400, 74, 19, 0, 0, 37, 19) && drawn;
    }
    // Offscreen and enormous legal coordinates cannot wrap back into the picture.
    rs::FillRect(INT_MAX, INT_MAX, INT_MAX, INT_MAX, {255, 0, 255, 255});
    rs::FillRect(16, 16, 16, 16, {255, 0, 255, 0});
    rs::FillRect(-10, -10, gs::Width() + 20, 14, {255, 255, 255, 255});
    rs::FillRect(-10, gs::Height() - 4, gs::Width() + 20, 14, {255, 255, 255, 255});
    rs::FillRect(-10, 0, 14, gs::Height(), {255, 255, 255, 255});
    rs::FillRect(gs::Width() - 4, 0, 14, gs::Height(), {255, 255, 255, 255});
    // A moving marker proves distinct frames; yellow marks the final 120-frame picture.
    rs::FillRect(24 + static_cast<int>(frame % 440U), 432, 16, 12,
                 frame == 119U ? gs::Color{255, 255, 0, 255} : gs::Color{255, 255, 255, 255});
    rs::EndFrame();
    return drawn;
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
    passed = Check("split-upload", SplitUpload(standard)) && passed;
    bool contexts = true;
    bool bounds = true;
    bool pressure = true;
    bool cleanup = true;
    bool held = true;
    bool textures = true;
    bool captured = true;
    bool recovered = true;
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        const u32 halfBytes = cycle == 0 ? 128U * 1024U : 1024U;
        if (!rs::Init({standard, halfBytes, 64U * 1024U, 128U * 1024U})) { return Check("device-init", false); }
        textures = CreateTextures() && textures;
        if (!textures) { rs::Shutdown(); return Check("textures", false); }
        bounds = bounds && gs::Width() == 512 && gs::Height() == video::VisibleLines(standard) &&
                 gs::FixedVramBytes() == (standard == video::Standard::Pal ? 1572864U : 1376256U);
        const u32 frames = cycle == 0 ? 120U : 4U;
        for (u32 frame = 0; frame < frames; ++frame)
        {
            textures = DrawScene(frame, frame == frames - 1U) && textures;
            const auto expected = (frame & 1U) == 0 ? gs::DrawContext::Ctx1 : gs::DrawContext::Ctx0;
            contexts = contexts && !rs::FrameStarted() && rs::FrameCount() == frame + 1U &&
                       rs::LastPresentedContext() == expected;
        }
        Log(LogLevel::Info, "[D3BFG] GS scene %s cycle=%d frames=%u context=%d\n",
            video::Name(standard), cycle, rs::FrameCount(), static_cast<int>(rs::LastPresentedContext()));
        captured = capture::WriteTga(cycle == 0 ? "host:renderer.tga" : "host:renderer-reload.tga",
                                     rs::LastPresentedContext()) && captured;
        if (cycle == 0)
        {
            const u64 imr = GsGetIMR();
            capture::InjectReadbackTimeoutOnce();
            recovered = !capture::WriteTga("host:renderer-failed.tga", rs::LastPresentedContext()) &&
                GsGetIMR() == imr && (*GS_REG_BUSDIR & 1U) == 0 && (*R_EE_VIF1_STAT & (1U << 23)) == 0 &&
                (*R_EE_GIF_STAT & 2U) == 0 &&
                !capture::WriteTga("host:no-such-directory/capture.tga", rs::LastPresentedContext()) &&
                capture::WriteTga("host:renderer-recovered.tga", rs::LastPresentedContext());
            held = HoldPresentedScene(standard);
        }
        // Force one 128 KiB half to overflow, and many 1 KiB halves on the reload.
        rs::BeginFrame({0, 0, 0, 255});
        for (int draw = 0; draw < 1024; ++draw)
        {
            textures = rs::TexturedRect(s_textures[3], draw % 512, (draw / 512) * 8, 1, 8, 0, 0, 1, 1) && textures;
        }
        rs::EndFrame();
        captured = capture::WriteTga(cycle == 0 ? "host:renderer-pressure.tga" : "host:renderer-pressure-reload.tga",
                                     rs::LastPresentedContext()) && captured;
        const auto textureStats = tex::GetStats();
        const auto vramStats = vram::GetStats();
        textures = textureStats.evictions > 0 && textureStats.uploads >= 9U && textureStats.pixelBytes == 81920U &&
                   vramStats.usedBytes <= 65536U && textures;
        Log(LogLevel::Info, "[D3BFG] GS textures pixels=%u peak=%u count=%u uploads=%u evictions=%u vram=%u/%u\n",
            textureStats.pixelBytes, textureStats.peakPixelBytes, textureStats.textures, textureStats.uploads,
            textureStats.evictions, static_cast<u32>(vramStats.usedBytes), static_cast<u32>(vramStats.peakBytes));
        // Destroy while a draw still owns a queued TEX0 reference, then reuse the range.
        rs::BeginFrame({0, 0, 0, 255});
        textures = rs::TexturedRect(s_textures[0], 0, 0, 148, 76, 0, 0, 37, 19) && textures;
        tex::Destroy(s_textures[0]);
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
    passed = Check("textures", textures) && passed;
    passed = Check("native-capture", captured) && passed;
    passed = Check("capture-recovery", recovered) && passed;
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
