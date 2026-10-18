// ================================================================================================
// File: gs.cpp
// Brief: Own GS contexts, direct RGBA uploads and ordered CPU primitives without Quake policy or VU.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/gs.h"
#include "ps2/renderer/cmd_buffer.h"
#include "ps2/renderer/pipeline_wait.h"
#include <dma.h>
#include <dma_tags.h>
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
static u64 s_previousImr = 0;
alignas(64) static qword_t s_upload[128] = {};

int Index(DrawContext context)
{
    const int index = static_cast<int>(context);
    if (index < 0 || index > 1)
    {
        FatalError("invalid GS drawing context");
    }
    return index;
}

u64 ContextReg(int reg, DrawContext context)
{
    return static_cast<u64>(reg + Index(context));
}

u64 AlphaState(BlendMode blend)
{
    // GS computes (A - B) * As + Cd. SWF adds its premultiplied source to Cd;
    // straight input therefore selects zero for B, while ordinary alpha selects Cd.
    const int subtract = blend == BlendMode::AdditiveAlpha ? 2 : 1;
    return GS_SET_ALPHA(0, subtract, 0, 1, 0x80);
}

tex::Layout ValidateBinding(const TextureBinding & binding)
{
    tex::Layout verified = {};
    const auto & input = binding.layout;
    if (!s_initialized || !tex::MakeLayout(input.width, input.height, binding.sampler, verified) ||
        verified.bytes != input.bytes || verified.stridePixels != input.stridePixels ||
        verified.storageHeight != input.storageHeight || verified.potWidth != input.potWidth ||
        verified.potHeight != input.potHeight || verified.widthLog2 != input.widthLog2 ||
        verified.heightLog2 != input.heightLog2 || (binding.address & 2047U) != 0 ||
        binding.address < s_fixedVramBytes / 4U || binding.address > (4U * 1024U * 1024U - verified.bytes) / 4U)
    {
        FatalError("invalid RGBA32 texture binding");
    }
    return verified;
}
} // namespace

bool IsInitialized()
{
    return s_initialized;
}

int Width()
{
    return s_width;
}

int Height()
{
    return s_height;
}

u32 FixedVramBytes()
{
    return s_fixedVramBytes;
}

FramebufferInfo GetFramebuffer(DrawContext context)
{
    if (!s_initialized)
    {
        FatalError("framebuffer query before GS initialization");
    }
    return { s_frames[Index(context)].address, s_width, s_height, GS_PSM_16 };
}

PS2_COLD_FUNC bool Init(const Config & config)
{
    if (s_initialized || !cmdbuf::IsInitialized())
    {
        FatalError("invalid GS initialization order");
    }
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
    s_previousImr = GsGetIMR();
    if (graph_set_mode(GRAPH_MODE_INTERLACED, video::GraphMode(config.standard), GRAPH_MODE_FIELD, GRAPH_ENABLE) != 0)
    {
        graph_shutdown();
        GsPutIMR(s_previousImr);
        graph_vram_clear();
        s_width = s_height = 0;
        s_fixedVramBytes = 0;
        return false;
    }
    graph_disable_output();
    // Claim the VIF/GIF interfaces as well as the GS. A GS CSR reset alone left
    // inherited loader state that blocked our first PATH2 DIRECT on the target.
    WaitUntil([]
              { return (*R_EE_D2_CHCR & (1U << 8)) == 0; }, "initial GIF DMA");
    Log(LogLevel::Info, "[D3BFG] GS claim GIF_STAT=%08x VIF1_STAT=%08x\n", *R_EE_GIF_STAT, *R_EE_VIF1_STAT);
    *R_EE_GIF_CTRL = 1;
    *R_EE_GIF_CTRL = 0;
    *R_EE_VIF1_FBRST = 1;
    *R_EE_VIF1_FBRST = 0;
    EE_SYNCP();
    // Poll CSR directly; no interrupt handler may consume our FINISH/VSYNC events.
    GsPutIMR(0x7F00);
    graph_set_screen(0, 0, s_width, s_height);
    graph_set_bgcolor(0, 0, 0);
    graph_set_framebuffer_filtered(first, s_width, GS_PSM_16, 0, 0);
    s_initialized = true;
    if (dma_channel_initialize(DMA_CHANNEL_GIF, nullptr, 0) < 0)
    {
        Shutdown();
        return false;
    }
    cmdbuf::BeginFrame();
    GifWriter writer = cmdbuf::BeginDirect(32);
    writer.SetupEnvironment(0, s_frames[0], s_depth);
    writer.SetupEnvironment(1, s_frames[1], s_depth);
    cmdbuf::EndDirect(writer);
    FillRect(DrawContext::Ctx0, 0, 0, s_width, s_height, { 0, 0, 0, 255 });
    cmdbuf::EndFrame();
    Log(LogLevel::Info, "[D3BFG] GS ready %s %dx%d PSMCT16/Z16 fixed_vram=%u\n",
        video::Name(config.standard), s_width, s_height, s_fixedVramBytes);
    return true;
}

void UploadRgba32(const TextureBinding & binding, const void * pixels)
{
    const auto verified = ValidateBinding(binding);
    if (pixels == nullptr || (reinterpret_cast<uiptr>(pixels) & 63U) != 0)
    {
        FatalError("invalid RGBA32 texture upload");
    }
    // Retire queued PATH2 references before PATH3 uploads or overwrites an address.
    cmdbuf::Kick();
    cmdbuf::WaitIdle();
    WaitUntil([]
              { return (*R_EE_D2_CHCR & (1U << 8)) == 0; }, "texture GIF DMA idle");
    // Audited draw.c: six setup qwords and three per IMAGE/REF chunk. Preflight
    // the maximum 512x512 layout before calling the SDK, rather than postchecking only.
    const u32 chunks = (verified.bytes / 16U + GIF_BLOCK_SIZE - 1U) / GIF_BLOCK_SIZE;
    const u32 transferQwords = 6U + 3U * chunks;
    if (transferQwords + 4U > static_cast<u32>(ArrayLength(s_upload)))
    {
        FatalError("texture upload chain overflow");
    }
    qword_t * end = draw_texture_transfer(s_upload, const_cast<void *>(pixels), verified.stridePixels,
                                          verified.storageHeight, GS_PSM_32, static_cast<int>(binding.address),
                                          verified.stridePixels);
    if (end != s_upload + transferQwords)
    {
        FatalError("unexpected SDK texture packet size");
    }
    DMATAG_END(end, 3, 0, 0, 0);
    ++end;
    PACK_GIFTAG(end, GIF_SET_TAG(2, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
    ++end;
    PACK_GIFTAG(end, 0, GS_REG_TEXFLUSH);
    ++end;
    PACK_GIFTAG(end, 1, GS_REG_FINISH);
    // Both the chain and REF pixels require writeback before the synchronous upload.
    FlushCache(0);
    *GS_REG_CSR = 2;
    dma_channel_send_chain(DMA_CHANNEL_GIF, s_upload, 0, 0, 0);
    WaitUntil([]
              { return (*R_EE_D2_CHCR & (1U << 8)) == 0; }, "texture GIF DMA");
    WaitUntil([]
              { return (*GS_REG_CSR & 2U) != 0; }, "texture GS FINISH");
}

void TexturedRect(DrawContext context, const TextureBinding & binding, int x, int y, int width, int height,
                  int u0, int v0, int u1, int v1, Color color)
{
    if (!s_initialized || width < 0 || height < 0 || u0 < 0 || v0 < 0 || u1 < u0 || v1 < v0 ||
        u1 > 1023 || v1 > 1023)
    {
        FatalError("invalid GS textured rectangle");
    }
    (void)Index(context);
    (void)ValidateBinding(binding);
    const s64 right = static_cast<s64>(x) + width;
    const s64 bottom = static_cast<s64>(y) + height;
    const int left = x > 0 ? x : 0;
    const int top = y > 0 ? y : 0;
    const int endX = static_cast<int>(right < s_width ? right : s_width);
    const int endY = static_cast<int>(bottom < s_height ? bottom : s_height);
    if (left >= endX || top >= endY || color.a == 0)
    {
        return;
    }
    // Interpolate UV in wide fixed point after screen clipping; no wrapped coordinates.
    const auto Coord = [](int start, int end, s64 offset, int extent)
    {
        return static_cast<int>(static_cast<s64>(start) * 16 + static_cast<s64>(end - start) * 16 * offset / extent);
    };
    const int startU = Coord(u0, u1, static_cast<s64>(left) - x, width);
    const int startV = Coord(v0, v1, static_cast<s64>(top) - y, height);
    const int endU = Coord(u0, u1, static_cast<s64>(endX) - x, width);
    const int endV = Coord(v0, v1, static_cast<s64>(endY) - y, height);
    TexturedRectFixed(context, binding, left * 16, top * 16, endX * 16, endY * 16,
                      startU, startV, endU, endV, color, BlendMode::StraightAlpha);
}

void TexturedRectFixed(DrawContext context, const TextureBinding & binding, int x0, int y0, int x1, int y1,
                       int u0, int v0, int u1, int v1, Color color, BlendMode blend)
{
    (void)ValidateBinding(binding);
    (void)Index(context);
    if (x0 < 0 || y0 < 0 || x1 < x0 || y1 < y0 || x1 > s_width * 16 || y1 > s_height * 16 ||
        u0 < 0 || v0 < 0 || u1 < u0 || v1 < v0 || u1 > 16383 || v1 > 16383 ||
        (blend != BlendMode::Replace && blend != BlendMode::StraightAlpha && blend != BlendMode::AdditiveAlpha))
    {
        FatalError("invalid fixed GS textured rectangle");
    }
    if (x0 == x1 || y0 == y1 || (color.a == 0 && blend != BlendMode::Replace))
    {
        return;
    }
    const auto & layout = binding.layout;
    const auto & sampler = binding.sampler;
    const int filter = sampler.filter == tex::Filter::Linear ? 1 : 0;
    GifWriter writer = cmdbuf::BeginDirect(22);
    writer.SetRegister(ContextReg(GS_REG_TEX1, context), GS_SET_TEX1(1, 0, filter, filter, 0, 0, 0));
    writer.SetRegister(ContextReg(GS_REG_TEX0, context), GS_SET_TEX0(binding.address >> 6,
                                                                     static_cast<u32>(layout.stridePixels) >> 6, GS_PSM_32, layout.widthLog2, layout.heightLog2, 1, 0, 0, 0, 0, 0, 0));
    writer.SetRegister(ContextReg(GS_REG_CLAMP, context), GS_SET_CLAMP(
                                                          sampler.wrapS == tex::Wrap::Clamp ? 2 : 0, sampler.wrapT == tex::Wrap::Clamp ? 2 : 0,
                                                          0, layout.width - 1, 0, layout.height - 1));
    writer.SetRegister(ContextReg(GS_REG_ALPHA, context), AlphaState(blend));
    // A zero-alpha texel still replaces RGB in ONE/ZERO mode. Environment cutout is
    // not the native GUI contract, so disable it explicitly for every UI sprite.
    writer.SetRegister(ContextReg(GS_REG_TEST, context), GS_SET_TEST(0, 0, 0, 0, 0, 0, 0, 1));
    writer.SetRegister(GS_REG_PRIM, GS_SET_PRIM(PRIM_SPRITE, 0, 1, 0,
                                                blend != BlendMode::Replace ? 1 : 0, 0, 1, Index(context), 0));
    const auto Modulate = [](u8 value)
    { return static_cast<u8>((static_cast<u32>(value) * 128U + 127U) / 255U); };
    writer.SetRegister(GS_REG_RGBAQ, GS_SET_RGBAQ(Modulate(color.r), Modulate(color.g), Modulate(color.b),
                                                  Modulate(color.a), 0x3F800000U));
    writer.SetRegister(GS_REG_UV, GS_SET_UV(u0, v0));
    writer.SetRegister(GS_REG_XYZ2, GS_SET_XYZ(x0 + 2048 * 16, y0 + 2048 * 16, 0));
    writer.SetRegister(GS_REG_UV, GS_SET_UV(u1, v1));
    writer.SetRegister(GS_REG_XYZ2, GS_SET_XYZ(x1 + 2048 * 16, y1 + 2048 * 16, 0));
    cmdbuf::EndDirect(writer);
}

void TexturedTriangleFixed(DrawContext context, const TextureBinding & binding,
                           const ui::FixedTriangle & triangle, BlendMode blend)
{
    (void)ValidateBinding(binding);
    (void)Index(context);
    if (blend != BlendMode::Replace && blend != BlendMode::StraightAlpha && blend != BlendMode::AdditiveAlpha)
    {
        FatalError("invalid fixed GS textured triangle blend");
    }
    for (const auto & vertex : triangle.vertices)
    {
        if (vertex.x < 0 || vertex.y < 0 || vertex.x > s_width * 16 || vertex.y > s_height * 16 ||
            vertex.u < 0 || vertex.v < 0 || vertex.u > 16383 || vertex.v > 16383)
        {
            FatalError("invalid fixed GS textured triangle vertex");
        }
    }
    const auto & first = triangle.vertices[0];
    const auto & second = triangle.vertices[1];
    const auto & third = triangle.vertices[2];
    const s64 area = static_cast<s64>(second.x - first.x) * (third.y - first.y) -
                     static_cast<s64>(second.y - first.y) * (third.x - first.x);
    if (area == 0 || (blend != BlendMode::Replace && first.a == 0 && second.a == 0 && third.a == 0))
    {
        return;
    }
    const auto & layout = binding.layout;
    const auto & sampler = binding.sampler;
    const int filter = sampler.filter == tex::Filter::Linear ? 1 : 0;
    // Six complete state writes and three RGBAQ/UV/XYZ2 vertices: thirty qwords.
    // A drain may alternate command halves; never depend on the previous primitive's state.
    GifWriter writer = cmdbuf::BeginDirect(30);
    writer.SetRegister(ContextReg(GS_REG_TEX1, context), GS_SET_TEX1(1, 0, filter, filter, 0, 0, 0));
    writer.SetRegister(ContextReg(GS_REG_TEX0, context), GS_SET_TEX0(binding.address >> 6,
                                                                     static_cast<u32>(layout.stridePixels) >> 6, GS_PSM_32, layout.widthLog2, layout.heightLog2, 1, 0, 0, 0, 0, 0, 0));
    writer.SetRegister(ContextReg(GS_REG_CLAMP, context), GS_SET_CLAMP(
                                                          sampler.wrapS == tex::Wrap::Clamp ? 2 : 0, sampler.wrapT == tex::Wrap::Clamp ? 2 : 0,
                                                          0, layout.width - 1, 0, layout.height - 1));
    writer.SetRegister(ContextReg(GS_REG_ALPHA, context), AlphaState(blend));
    // ONE/ZERO replaces RGB even where interpolated or texture alpha is zero.
    writer.SetRegister(ContextReg(GS_REG_TEST, context), GS_SET_TEST(0, 0, 0, 0, 0, 0, 0, 1));
    writer.SetRegister(GS_REG_PRIM, GS_SET_PRIM(PRIM_TRIANGLE, PRIM_SHADE_GOURAUD, 1, 0,
                                                blend != BlendMode::Replace ? 1 : 0, 0, PRIM_MAP_UV, Index(context), 0));
    const auto Modulate = [](u8 value)
    { return static_cast<u8>((static_cast<u32>(value) * 128U + 127U) / 255U); };
    for (const auto & vertex : triangle.vertices)
    {
        writer.SetRegister(GS_REG_RGBAQ, GS_SET_RGBAQ(Modulate(vertex.r), Modulate(vertex.g), Modulate(vertex.b),
                                                      Modulate(vertex.a), 0x3F800000U));
        writer.SetRegister(GS_REG_UV, GS_SET_UV(vertex.u, vertex.v));
        writer.SetRegister(GS_REG_XYZ2, GS_SET_XYZ(vertex.x + 2048 * 16, vertex.y + 2048 * 16, 0));
    }
    cmdbuf::EndDirect(writer);
}

void FillRect(DrawContext context, int x, int y, int width, int height, Color color)
{
    if (!s_initialized || width < 0 || height < 0)
    {
        FatalError("invalid GS fill rectangle");
    }
    (void)Index(context);
    // Wide arithmetic before clipping: untrusted dimensions must not wrap GS coordinates.
    const s64 right = static_cast<s64>(x) + width;
    const s64 bottom = static_cast<s64>(y) + height;
    const int left = x > 0 ? x : 0;
    const int top = y > 0 ? y : 0;
    const int endX = static_cast<int>(right < s_width ? right : s_width);
    const int endY = static_cast<int>(bottom < s_height ? bottom : s_height);
    if (left >= endX || top >= endY || color.a == 0)
    {
        return;
    }
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
    if (!s_initialized)
    {
        FatalError("GS present before initialization");
    }
    cmdbuf::WaitIdle();
    *GS_REG_CSR = 8; // Fresh field boundary, without clearing FINISH or other events.
    WaitUntil([]
              { return (*GS_REG_CSR & 8U) != 0; }, "VSYNC");
    graph_set_framebuffer_filtered(static_cast<int>(s_frames[Index(context)].address), s_width, GS_PSM_16, 0, 0);
    graph_enable_output();
}

PS2_COLD_FUNC void Shutdown()
{
    if (!s_initialized)
    {
        return;
    }
    cmdbuf::WaitIdle();
    dma_channel_shutdown(DMA_CHANNEL_GIF, 0);
    graph_shutdown();
    GsPutIMR(s_previousImr);
    graph_vram_clear();
    s_frames[0] = {};
    s_frames[1] = {};
    s_depth = {};
    s_width = s_height = 0;
    s_fixedVramBytes = 0;
    s_initialized = false;
}
} // namespace ps2::gs
