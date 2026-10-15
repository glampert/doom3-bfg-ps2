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
#include "ps2/renderer/ui_draw.h"
#include "ps2/renderer/image_backend.h"
#include "ps2/system/heap.h"
#include <kernel.h>
#include <delaythread.h>
#include <stdio.h>
#include <string.h>
#include <timer.h>
#include <bit>
#include <climits>
#include <idlib/precompiled.h>
#include <renderer/tr_local.h>

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
static constexpr triIndex_t kQuadIndices[] = {0, 1, 2, 0, 2, 3};
static constexpr triIndex_t kTriangleIndices[] = {0, 1, 2};
bool SameLedger(const heap::Stats & before);

void IndexedVertex(idDrawVert & vertex, float x, float y, float s, float t, gs::Color color)
{
    vertex.Clear();
    vertex.xyz.Set(x, y, 0.0f);
    // SetTexCoord stores native half-floats; reading them as floats cannot produce this scene.
    vertex.SetTexCoord(s, t);
    vertex.SetNativeOrderColor(static_cast<dword>(color.r) | (static_cast<dword>(color.g) << 8U) |
        (static_cast<dword>(color.b) << 16U) | (static_cast<dword>(color.a) << 24U));
    vertex.ClearColor2();
}

void IndexedQuad(idDrawVert * vertices, const idVec2 (&positions)[4], float s0, float t0, float s1, float t1, gs::Color color)
{
    IndexedVertex(vertices[0], positions[0].x, positions[0].y, s0, t0, color);
    IndexedVertex(vertices[1], positions[1].x, positions[1].y, s1, t0, color);
    IndexedVertex(vertices[2], positions[2].x, positions[2].y, s1, t1, color);
    IndexedVertex(vertices[3], positions[3].x, positions[3].y, s0, t1, color);
}

bool NativeIndexed(video::Standard standard)
{
    const auto before = heap::GetTotalStats();
    bool passed = true;
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        if (!rs::Init({standard, 1024U, 1048576U, 524288U}) || !ui::Init()) { rs::Shutdown(); return false; }
        auto * const pressureVertices = static_cast<idDrawVert *>(heap::TryAlloc(1024U * sizeof(idDrawVert), TAG_RENDER, 16));
        auto * const pressureIndices = static_cast<triIndex_t *>(heap::TryAlloc(3072U * sizeof(triIndex_t), TAG_RENDER, 16));
        if (pressureVertices == nullptr || pressureIndices == nullptr)
        {
            heap::Free(pressureVertices);
            heap::Free(pressureIndices);
            ui::Shutdown();
            rs::Shutdown();
            return false;
        }
        for (int vertex = 0; vertex < 1024; ++vertex)
        {
            const int corner = vertex % 3;
            IndexedVertex(pressureVertices[vertex], corner == 0 ? 608.0f : 640.0f, corner == 2 ? 448.0f : 416.0f,
                0, 0, {0, 255, 0, 255});
        }
        for (int index = 0; index < 3072; ++index) { pressureIndices[index] = static_cast<triIndex_t>(index % 3); }
        const auto warm = heap::GetTotalStats();
        const idImage * const white = render::ConsoleWhiteImage();
        const idImage * const chars = render::ConsoleCharsetImage();
        const u32 frames = cycle == 0 ? 120U : 4U;
        for (u32 frame = 0; frame < frames; ++frame)
        {
            rs::BeginFrame({16, 24, 40, 255});
            renderSystem->SetGLState(GLS_DEFAULT);
            renderSystem->DrawSmallStringExt(32, 16, "Indexed GUI: rotation / clipping / color / alpha", colorWhite, true);
            idDrawVert vertices[4];
            const idVec2 rotated[4] = {idVec2(64, 48), idVec2(192, 80), idVec2(160, 208), idVec2(32, 176)};
            IndexedQuad(vertices, rotated, 1.0f / 16.0f, 4.0f / 16.0f, 2.0f / 16.0f, 5.0f / 16.0f, {255, 255, 255, 255});
            ui::DrawIndexed(vertices, 4, kQuadIndices, 6, chars, gs::BlendMode::StraightAlpha);
            const idVec2 clipped[4] = {idVec2(-80, 256), idVec2(80, 224), idVec2(112, 352), idVec2(-48, 384)};
            IndexedQuad(vertices, clipped, 1.0f / 16.0f, 4.0f / 16.0f, 2.0f / 16.0f, 5.0f / 16.0f, {0, 255, 255, 255});
            ui::DrawIndexed(vertices, 4, kQuadIndices, 6, chars, gs::BlendMode::StraightAlpha);
            IndexedVertex(vertices[0], 248, 48, 0, 0, {255, 0, 0, 255});
            IndexedVertex(vertices[1], 408, 48, 1, 0, {0, 255, 0, 255});
            IndexedVertex(vertices[2], 248, 208, 0, 1, {0, 0, 255, 255});
            ui::DrawIndexed(vertices, 3, kTriangleIndices, 3, white, gs::BlendMode::Replace);
            const idVec2 diamond[4] = {idVec2(528, 48), idVec2(608, 128), idVec2(528, 208), idVec2(448, 128)};
            IndexedQuad(vertices, diamond, 0, 0, 1, 1, {255, 255, 0, 255});
            ui::DrawIndexed(vertices, 4, kQuadIndices, 6, white, gs::BlendMode::Replace);
            IndexedVertex(vertices[0], 288, 176, 0, 0, {255, 255, 255, 0});
            IndexedVertex(vertices[1], 416, 176, 1, 0, {255, 255, 255, 255});
            IndexedVertex(vertices[2], 416, 240, 1, 1, {255, 255, 255, 255});
            ui::DrawIndexed(vertices, 3, kTriangleIndices, 3, white, gs::BlendMode::StraightAlpha);
            renderSystem->DrawFilled(colorRed, 192, 256, 160, 112);
            renderSystem->DrawFilled(colorBlue, 448, 256, 160, 112);
            // AllocTris returns unfilled storage. Drawing the next sprite must consume the final data.
            idDrawVert * const delayed = ui::AllocTrisForImage(4, kQuadIndices, 6, white, gs::BlendMode::StraightAlpha);
            const idVec2 overlap[4] = {idVec2(256, 272), idVec2(480, 272), idVec2(480, 352), idVec2(256, 352)};
            IndexedQuad(delayed, overlap, 0, 0, 1, 1, {255, 255, 255, 128});
            renderSystem->DrawFilled(idVec4(0, 0, 0, 0.5f), 320, 288, 64, 48);
            // The last queued triangle must survive EndFrame even when no later sprite forces it out.
            IndexedVertex(vertices[0], 608, 416, 0, 0, {0, 255, 0, 255});
            IndexedVertex(vertices[1], 640, 416, 1, 0, {0, 255, 0, 255});
            IndexedVertex(vertices[2], 640, 448, 1, 1, {0, 255, 0, 255});
            ui::DrawIndexed(vertices, 3, kTriangleIndices, 3, white, gs::BlendMode::Replace);
            if (frame == frames - 1U)
            {
                // Isolate each bounded pool so all three independent recycling paths are exercised.
                ui::Flush();
                for (int draw = 0; draw < 5; ++draw)
                {
                    ui::DrawIndexed(pressureVertices, 1024, kTriangleIndices, 3, white, gs::BlendMode::Replace);
                }
                ui::Flush();
                for (int draw = 0; draw < 3; ++draw)
                {
                    ui::DrawIndexed(pressureVertices, 3, pressureIndices, 3072, white, gs::BlendMode::Replace);
                }
                ui::Flush();
                for (int draw = 0; draw < 130; ++draw)
                {
                    ui::DrawIndexed(vertices, 3, kTriangleIndices, 3, white, gs::BlendMode::Replace);
                }
            }
            // EndFrame must consume the last queued triangles without an explicit fixture flush.
            rs::EndFrame();
            passed = SameLedger(warm) && passed;
        }
        passed = capture::WriteTga(cycle == 0 ? "host:renderer-indexed.tga" : "host:renderer-indexed-reload.tga",
            rs::LastPresentedContext()) && passed;
        const auto chains = cmdbuf::GetStats();
        const auto geometry = ui::GetGeometryStats();
        passed = chains.drains > 0 && chains.peakBytes <= 1024U &&
            geometry.peakVertices == 4096U && geometry.peakIndexes == 6144U && geometry.peakDraws == 128U &&
            geometry.flushes > frames && passed;
        Log(LogLevel::Info, "[D3BFG] UI indexed cycle=%d frames=%u vertices=%u indices=%u draws=%u flushes=%u triangles=%u drains=%u\n",
            cycle, frames, geometry.peakVertices, geometry.peakIndexes, geometry.peakDraws,
            geometry.flushes, geometry.triangles, chains.drains);
        heap::Free(pressureVertices);
        heap::Free(pressureIndices);
        ui::Shutdown();
        rs::Shutdown();
        passed = SameLedger(before) && passed;
    }
    return passed;
}

bool NativeAdapters(video::Standard standard)
{
    const auto before = heap::GetTotalStats();
    bool passed = !ui::IsInitialized() && !R_IsInitialized();
    // Startup failure must not claim native capabilities or retain partial image ownership.
    if (!rs::Init({standard, 1024U, 65536U, 524288U})) { return false; }
    passed = !ui::Init() && !tr.registered && !render::ConsoleImagesInitialized() && passed;
    rs::Shutdown();
    passed = SameLedger(before) && passed;
    if (!rs::Init({standard, 1024U, 1048576U, 131072U})) { return false; }
    passed = !ui::Init() && !tr.registered && !render::ConsoleImagesInitialized() && passed;
    rs::Shutdown();
    passed = SameLedger(before) && passed;
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        if (!rs::Init({standard, 1024U, 1048576U, 524288U}) || !ui::Init()) { rs::Shutdown(); return false; }
        const idImage * const white = render::ConsoleWhiteImage();
        const idImage * const chars = render::ConsoleCharsetImage();
        passed = white != nullptr && chars != nullptr && white->IsLoaded() && chars->IsLoaded() &&
            white->StorageSize() == 8192 && chars->StorageSize() == 262144 && tr.registered && R_IsInitialized() &&
            !renderSystem->IsOpenGLRunning() && renderSystem->IsFullScreen() && renderSystem->GetWidth() == 512 &&
            renderSystem->GetHeight() == video::VisibleLines(standard) && passed;
        renderSystem->SetColor(idVec4(0.5f, 0.25f, 1.0f, 0.75f));
        passed = renderSystem->GetColor() == 0xBFFF3F7FU && passed;
        const u32 frames = cycle == 0 ? 120U : 4U;
        for (u32 frame = 0; frame < frames; ++frame)
        {
            rs::BeginFrame({16, 24, 40, 255});
            renderSystem->SetGLState(GLS_DEFAULT);
            renderSystem->DrawBigStringExt(32, 24, "D3BFG PS2", colorCyan, true);
            renderSystem->DrawSmallStringExt(32, 56, "Native draw adapters - 640x480 canvas", colorWhite, true);
            renderSystem->DrawSmallStringExt(32, 80, "^1RED ^2GREEN ^4BLUE ^7WHITE", colorWhite, false);
            renderSystem->DrawSmallStringExt(32, 104, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", colorWhite, true);
            renderSystem->DrawSmallStringExt(32, 128, "abcdefghijklmnopqrstuvwxyz 0123456789", colorWhite, true);
            renderSystem->DrawSmallStringExt(32, 152, "[]{}()<>!?+-*/=.,:;_", colorWhite, true);
            renderSystem->SetColor(colorWhite);
            renderSystem->DrawSmallChar(32, 176, 10);
            renderSystem->DrawSmallChar(56, 176, 11);
            renderSystem->SetGLState(GLS_OVERRIDE | GLS_DEFAULT);
            renderSystem->DrawFilled(idVec4(1.0f, 0.0f, 0.0f, 0.0f), 32.0f, 208.0f, 128.0f, 32.0f);
            renderSystem->SetGLState(GLS_DEFAULT);
            renderSystem->DrawFilled(idVec4(0.0f, 1.0f, 0.0f, 0.5f), 176.0f, 208.0f, 128.0f, 32.0f);
            renderSystem->DrawFilled(colorWhite, 320.25f, 208.125f, 80.5f, 31.875f);
            renderSystem->DrawFilled(colorBlue, -16.25f, 256.0f, 48.5f, 32.0f);
            renderSystem->DrawSmallStringExt(32, 304, "]echo authored input line", colorWhite, true);
            renderSystem->SetColor(colorWhite);
            renderSystem->DrawSmallChar(32 + 25 * SMALLCHAR_WIDTH, 304, frame == frames - 1U ? 11 : 10);
            renderSystem->SetColor(colorWhite);
            rs::EndFrame();
        }
        passed = capture::WriteTga(cycle == 0 ? "host:renderer-ui.tga" : "host:renderer-ui-reload.tga", rs::LastPresentedContext()) && passed;
        Log(LogLevel::Info, "[D3BFG] UI scene cycle=%d frames=%u pixel_bytes=%u\n", cycle, frames, tex::GetStats().pixelBytes);
        ui::Shutdown();
        ui::Shutdown();
        rs::Shutdown();
        passed = !ui::IsInitialized() && !R_IsInitialized() && !tr.registered && !render::ConsoleImagesInitialized() && SameLedger(before) && passed;
    }
    return passed;
}
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
    drawn = rs::TexturedRect(s_textures[4], 200, 328, 48, 24, 0, 0, 1, 1) && drawn;
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
    passed = Check("native-adapters", NativeAdapters(standard)) && passed;
    passed = Check("native-indexed", NativeIndexed(standard)) && passed;
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
        // Destroy while a draw still owns a queued TEX0 reference; finish it before release.
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
        strcmp(name, "renderer-wait-timeout") != 0 && strcmp(name, "renderer-ui-state") != 0 &&
        strcmp(name, "renderer-ui-bounds") != 0 && strcmp(name, "renderer-ui-quad") != 0 &&
        strcmp(name, "renderer-ui-text") != 0 && strcmp(name, "renderer-ui-indices") != 0 &&
        strcmp(name, "renderer-ui-capacity") != 0 && strcmp(name, "renderer-ui-depth") != 0 &&
        strcmp(name, "renderer-ui-half") != 0 && strcmp(name, "renderer-ui-color2") != 0) { return false; }
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
    else
    {
        if (!rs::Init({video::Standard::Ntsc, 1024U}) || !ui::Init()) { FatalError("probe UI initialization failed"); }
        if (strcmp(name, "renderer-ui-state") == 0)
        {
            renderSystem->SetGLState(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
        }
        rs::BeginFrame({0, 0, 0, 255});
        if (strcmp(name, "renderer-ui-bounds") == 0)
        {
            renderSystem->DrawFilled(colorWhite, 0.0f, 0.0f, -1.0f, 16.0f);
        }
        else if (strcmp(name, "renderer-ui-quad") == 0)
        {
            renderSystem->DrawStretchPic(idVec4(std::bit_cast<float>(0x7F800000U), 0, 0, 0), idVec4(16, 0, 1, 0),
                idVec4(17, 16, 1, 1), idVec4(0, 16, 0, 1), nullptr);
        }
        else if (strcmp(name, "renderer-ui-text") == 0)
        {
            static char oversized[MAX_STRING_CHARS + 1] = {};
            memset(oversized, 'A', MAX_STRING_CHARS);
            renderSystem->DrawSmallStringExt(0, 0, oversized, colorWhite, true);
        }
        else if (strcmp(name, "renderer-ui-capacity") == 0)
        {
            (void)ui::AllocTrisForImage(ui::kMaxVertices + 1, kTriangleIndices, 3,
                render::ConsoleWhiteImage(), gs::BlendMode::Replace);
        }
        else
        {
            idDrawVert vertices[3];
            IndexedVertex(vertices[0], 0, 0, 0, 0, {255, 255, 255, 255});
            IndexedVertex(vertices[1], 32, 0, 1, 0, {255, 255, 255, 255});
            IndexedVertex(vertices[2], 0, 32, 0, 1, {255, 255, 255, 255});
            static constexpr triIndex_t badIndices[] = {0, 1, 3};
            if (strcmp(name, "renderer-ui-depth") == 0) { vertices[0].xyz.z = 1.0f; }
            if (strcmp(name, "renderer-ui-half") == 0) { vertices[0].st[0] = static_cast<halfFloat_t>(0x7C00U); }
            if (strcmp(name, "renderer-ui-color2") == 0) { vertices[0].color2[0] = 0; }
            ui::DrawIndexed(vertices, 3, strcmp(name, "renderer-ui-indices") == 0 ? badIndices : kTriangleIndices,
                3, render::ConsoleWhiteImage(), gs::BlendMode::Replace);
            ui::Flush();
        }
    }
    FatalError("GS failure probe unexpectedly returned");
}
} // namespace ps2::smoketests
