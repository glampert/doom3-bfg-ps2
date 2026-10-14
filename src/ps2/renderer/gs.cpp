// ================================================================================================
// File: gs.cpp
// Brief: Adapt the reference GS setup and fills without Quake palettes, splash, textures or VU.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/gs.h"
#include "ps2/renderer/cmd_buffer.h"
#include "ps2/renderer/pipeline_wait.h"
#include <graph.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <kernel.h>

namespace ps2::gs
{
namespace
{
static framebuffer_t s_frames[2] = {};
static zbuffer_t s_depth = {};
static int s_width = 0;
static int s_height = 0;
static u32 s_fixedVramBytes = 0;
static bool s_initialized = false;

int Index(DrawContext context)
{
    const int index = static_cast<int>(context);
    if (index < 0 || index > 1) { FatalError("invalid GS drawing context"); }
    return index;
}
u64 ContextReg(int reg, DrawContext context) { return static_cast<u64>(reg + Index(context)); }
} // namespace

bool IsInitialized() { return s_initialized; }
int Width() { return s_width; }
int Height() { return s_height; }
u32 FixedVramBytes() { return s_fixedVramBytes; }

PS2_COLD_FUNC bool Init(const Config & config)
{
    if (s_initialized || !cmdbuf::IsInitialized()) { FatalError("invalid GS initialization order"); }
    // Narrow first device profile: all dimensions are page-aligned and fit the TV standard.
    if (!video::IsValid(config.standard) || config.width != 512 || config.height != video::VisibleLines(config.standard))
    {
        Log(LogLevel::Error, "unsupported GS device configuration\n");
        return false;
    }
    graph_vram_clear();
    const int first = graph_vram_allocate(config.width, config.height, GS_PSM_16, GRAPH_ALIGN_PAGE);
    const int second = graph_vram_allocate(config.width, config.height, GS_PSM_16, GRAPH_ALIGN_PAGE);
    const int depth = graph_vram_allocate(config.width, config.height, GS_ZBUF_16, GRAPH_ALIGN_PAGE);
    if (first < 0 || second < 0 || depth < 0)
    {
        graph_vram_clear();
        return false;
    }
    s_width = config.width;
    s_height = config.height;
    s_fixedVramBytes = static_cast<u32>(depth + graph_vram_size(s_width, s_height, GS_ZBUF_16, GRAPH_ALIGN_PAGE)) * 4U;
    for (int index = 0; index < 2; ++index)
    {
        s_frames[index].width = static_cast<unsigned int>(s_width);
        s_frames[index].height = static_cast<unsigned int>(s_height);
        s_frames[index].address = static_cast<unsigned int>(index == 0 ? first : second);
        s_frames[index].psm = GS_PSM_16;
        s_frames[index].mask = 0;
    }
    s_depth.address = static_cast<unsigned int>(depth);
    s_depth.zsm = GS_ZBUF_16;
    s_depth.mask = 1;
    s_depth.enable = DRAW_DISABLE;
    s_depth.method = ZTEST_METHOD_ALLPASS;
    if (graph_set_mode(GRAPH_MODE_INTERLACED, video::GraphMode(config.standard), GRAPH_MODE_FIELD, GRAPH_ENABLE) != 0)
    {
        graph_shutdown();
        graph_vram_clear();
        s_width = s_height = 0;
        s_fixedVramBytes = 0;
        return false;
    }
    graph_disable_output();
    // Poll CSR directly; no interrupt handler may consume our FINISH/VSYNC events.
    GsPutIMR(0x7F00);
    graph_set_screen(0, 0, s_width, s_height);
    graph_set_bgcolor(0, 0, 0);
    graph_set_framebuffer_filtered(first, s_width, GS_PSM_16, 0, 0);
    cmdbuf::BeginFrame();
    GifWriter writer = cmdbuf::BeginDirect(32);
    writer.SetupEnvironment(0, s_frames[0], s_depth);
    writer.SetupEnvironment(1, s_frames[1], s_depth);
    cmdbuf::EndDirect(writer);
    s_initialized = true;
    FillRect(DrawContext::Ctx0, 0, 0, s_width, s_height, {0, 0, 0, 255});
    cmdbuf::EndFrame();
    Log(LogLevel::Info, "[D3BFG] GS ready %s %dx%d PSMCT16/Z16 fixed_vram=%u\n",
        video::Name(config.standard), s_width, s_height, s_fixedVramBytes);
    return true;
}

void FillRect(DrawContext context, int x, int y, int width, int height, Color color)
{
    if (!s_initialized || width < 0 || height < 0) { FatalError("invalid GS fill rectangle"); }
    (void)Index(context);
    // Wide arithmetic before clipping: untrusted dimensions must not wrap GS coordinates.
    const s64 right = static_cast<s64>(x) + width;
    const s64 bottom = static_cast<s64>(y) + height;
    const int left = x > 0 ? x : 0;
    const int top = y > 0 ? y : 0;
    const int endX = static_cast<int>(right < s_width ? right : s_width);
    const int endY = static_cast<int>(bottom < s_height ? bottom : s_height);
    if (left >= endX || top >= endY || color.a == 0) { return; }
    GifWriter writer = cmdbuf::BeginDirect(10);
    writer.SetRegister(ContextReg(GS_REG_ALPHA, context), GS_SET_ALPHA(0, 1, 0, 1, 0x80));
    const int blend = color.a == 255 ? 0 : 1;
    const u8 alpha = static_cast<u8>((static_cast<u32>(color.a) * 128U + 127U) / 255U);
    writer.SetRegister(GS_REG_PRIM, GS_SET_PRIM(PRIM_SPRITE, 0, 0, 0, blend, 0, 1, Index(context), 0));
    writer.SetRegister(GS_REG_RGBAQ, GS_SET_RGBAQ(color.r, color.g, color.b, alpha, 0x3F800000U));
    writer.SetRegister(GS_REG_XYZ2, GS_SET_XYZ((left + 2048) * 16, (top + 2048) * 16, 0));
    writer.SetRegister(GS_REG_XYZ2, GS_SET_XYZ((endX + 2048) * 16, (endY + 2048) * 16, 0));
    cmdbuf::EndDirect(writer);
}

void Present(DrawContext context)
{
    if (!s_initialized) { FatalError("GS present before initialization"); }
    cmdbuf::WaitIdle();
    *GS_REG_CSR = 8; // Fresh field boundary, without clearing FINISH or other events.
    WaitUntil([] { return (*GS_REG_CSR & 8U) != 0; }, "VSYNC");
    graph_set_framebuffer_filtered(static_cast<int>(s_frames[Index(context)].address), s_width, GS_PSM_16, 0, 0);
    graph_enable_output();
}

PS2_COLD_FUNC void Shutdown()
{
    if (!s_initialized) { return; }
    cmdbuf::WaitIdle();
    graph_shutdown();
    graph_vram_clear();
    s_frames[0] = {};
    s_frames[1] = {};
    s_depth = {};
    s_width = s_height = 0;
    s_fixedVramBytes = 0;
    s_initialized = false;
}
} // namespace ps2::gs
