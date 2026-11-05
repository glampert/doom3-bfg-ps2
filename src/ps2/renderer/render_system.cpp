// ================================================================================================
// File: render_system.cpp
// Brief: Alternate GS contexts, clear, draw and immediately present bounded CPU-only frames.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/render_system.h"
#include "ps2/renderer/vram.h"
#include "ps2/ui/draw.h"

namespace ps2::renderer
{
namespace
{
static bool s_initialized = false;
static bool s_frame = false;
static gs::DrawContext s_context = gs::DrawContext::Ctx0;
static u32 s_frames = 0;
static u32 s_textureVramBytes = 0;
static system::video::Standard s_standard = system::video::Standard::Ntsc;
static constexpr u8 kChannelBits[] = { gs::kWriteRed, gs::kWriteGreen, gs::kWriteBlue };

// The configured texture range, or all VRAM the frame buffers leave when that is less.
u32 TextureVramBytes()
{
    const u32 free = gs::kVramBytes - gs::FixedVramBytes();
    return s_textureVramBytes < free ? s_textureVramBytes : free;
}

u8 ChannelValue(const ui::FixedVertex & vertex, int channel)
{
    return channel == 0 ? vertex.r : channel == 1 ? vertex.g : vertex.b;
}

gs::Color AddColor(const Shading & shading, int sign)
{
    u8 channels[3] = {};

    for (int channel = 0; channel < 3; ++channel)
    {
        const int value = shading.add[channel] * sign;
        channels[channel] = static_cast<u8>(value > 0 ? (value < 255 ? value : 255) : 0);
    }

    return { channels[0], channels[1], channels[2], 255 };
}

// Expands one primitive (triangle or line vertices) into its GS passes. emit(primitive, state,
// channel) draws a pass; channel is the color channel a screen pass carries in alpha, else -1.
template <typename Primitive, typename Emit>
bool ForEachPass(const Primitive & primitive, const Shading & shading, Emit emit)
{
    bool hasAdd = false;
    bool hasSubtract = false;

    for (const s16 value : shading.add)
    {
        PS2_AssertMsg(value >= -255 && value <= 255, "additive color outside one unit");
        hasAdd = hasAdd || value > 0;
        hasSubtract = hasSubtract || value < 0;
    }

    if (shading.blend == gs::BlendMode::Screen)
    {
        PS2_AssertMsg(!hasAdd && !hasSubtract && !shading.replaceColor, "screen blending with an additive color");

        // The screen source is premultiplied color: each channel's vertex color scales its alpha.
        for (int channel = 0; channel < 3; ++channel)
        {
            Primitive pass = primitive;

            for (auto & vertex : pass.vertices)
            {
                vertex.a = static_cast<u8>((static_cast<u32>(vertex.a) * ChannelValue(vertex, channel) + 127U) / 255U);
            }

            gs::DrawState state;
            state.blend = gs::BlendMode::Screen;
            state.masked = shading.masked;
            state.channels = kChannelBits[channel];

            if (!emit(pass, state, channel))
            {
                return false;
            }
        }

        return true;
    }

    PS2_AssertMsg((!hasAdd && !hasSubtract && !shading.replaceColor) || shading.blend == gs::BlendMode::StraightAlpha ||
                      shading.blend == gs::BlendMode::AdditiveAlpha,
                  "additive color requires straight or additive blending");

    gs::DrawState state;
    state.blend = shading.blend;
    state.masked = shading.masked;

    if (shading.replaceColor)
    {
        PS2_AssertMsg(!hasSubtract, "replacement color cannot subtract");

        state.constantColor = true;
        state.color = AddColor(shading, 1);
        return emit(primitive, state, -1);
    }

    if (!emit(primitive, state, -1))
    {
        return false;
    }

    static constexpr int kSigns[] = { 1, -1 };

    for (const int sign : kSigns)
    {
        if ((sign > 0 && !hasAdd) || (sign < 0 && !hasSubtract))
        {
            continue;
        }

        gs::DrawState extra;
        extra.blend = sign > 0 ? gs::BlendMode::AdditiveAlpha : gs::BlendMode::SubtractAlpha;
        extra.constantColor = true;
        extra.color = AddColor(shading, sign);
        extra.masked = shading.masked;

        if (!emit(primitive, extra, -1))
        {
            return false;
        }
    }

    return true;
}

} // namespace

bool IsInitialized()
{
    return s_initialized;
}

bool FrameStarted()
{
    return s_frame;
}

u32 FrameCount()
{
    return s_frames;
}

gs::DrawContext DrawingContext()
{
    PS2_AssertMsg(s_frame, "no open GS frame");
    return s_context;
}

gs::DrawContext LastPresentedContext()
{
    PS2_AssertMsg(!s_frame && s_frames != 0, "no completed GS frame");
    return s_context;
}

PS2_COLD_FUNC bool Init(const Config & config)
{
    if (s_initialized) [[unlikely]]
    {
        FatalError("render system initialized twice");
    }

    // Both sixteen-qword environments plus the chain terminator must fit one ring.
    if (!system::video::IsValid(config.standard) || config.chainRingBytes < 640U || !dma::Init(config.chainRingBytes))
    {
        return false;
    }

    if (!vu::Init())
    {
        dma::Shutdown();
        return false;
    }

    if (!gs::Init({ config.standard, 512, system::video::VisibleLines(config.standard), config.frameFormat }))
    {
        vu::Shutdown();
        dma::Shutdown();
        return false;
    }

    s_standard = config.standard;
    s_textureVramBytes = config.textureVramBytes;

    if (!tex::Init(TextureVramBytes(), config.texturePixelBytes))
    {
        vu::Shutdown();
        dma::Shutdown();
        gs::Shutdown();
        return false;
    }

    s_context = gs::DrawContext::Ctx0;
    s_frames = 0;
    s_initialized = true;
    return true;
}

PS2_COLD_FUNC void Shutdown()
{
    if (s_frame)
    {
        EndFrame();
    }

    tex::Shutdown();

    // Partial startup and repeated shutdown both leave no DMA references or heap ownership.
    vu::Shutdown();
    dma::Shutdown();
    gs::Shutdown();

    s_initialized = false;
    s_frames = 0;
    s_textureVramBytes = 0;
}

PS2_COLD_FUNC void SetFrameFormat(gs::FrameFormat format)
{
    if (!s_initialized || s_frame) [[unlikely]]
    {
        FatalError("frame-buffer profile changed outside the frame boundary");
    }

    if (format == gs::GetFrameFormat())
    {
        return;
    }

    // A GS restart, not buffers laid out again under a running device: the reset leaves no state
    // of the old layout behind (an emulator's render-target cache keeps old-format buffers
    // otherwise), at the cost of a blank frame. No queued TEX0 may name a page the larger buffers
    // cover, and the VIF1 reset leaves VU1 micro memory unknown.
    dma::WaitIdle();
    gs::Shutdown();

    if (!gs::Init({ s_standard, 512, system::video::VisibleLines(s_standard), format })) [[unlikely]]
    {
        FatalError("cannot restart the GS in the %s profile", gs::FrameFormatName(format));
    }

    vu::Invalidate();
    tex::MoveVram(TextureVramBytes());
}

void BeginFrame(gs::Color clearColor)
{
    PS2_AssertMsg(s_initialized && !s_frame && clearColor.a == 255, "invalid 2D BeginFrame state");

    dma::BeginFrame();

    s_context = s_context == gs::DrawContext::Ctx0 ? gs::DrawContext::Ctx1 : gs::DrawContext::Ctx0;
    s_frame = true;

    ui::BeginGeometryFrame();
    gs::ClearFrame(dma::List(), s_context, clearColor);
}

void EndFrame()
{
    PS2_AssertMsg(s_frame, "2D EndFrame without BeginFrame");

    ui::EndGeometryFrame();
    dma::EndFrame();
    gs::Present(s_context);

    s_frame = false;
    ++s_frames;
}

void FillRect(int x, int y, int width, int height, gs::Color color)
{
    PS2_AssertMsg(s_frame, "2D fill outside a frame");

    ui::Flush();
    gs::FillRect(dma::List(), s_context, x, y, width, height, color);
}

bool TexturedRect(tex::Handle texture, int x, int y, int width, int height, int u0, int v0, int u1, int v1, gs::Color color)
{
    PS2_AssertMsg(s_frame, "2D texture draw outside a frame");

    ui::Flush();
    gs::TextureBinding binding = {};
    if (!tex::Bind(texture, binding))
    {
        return false;
    }

    gs::TexturedRect(dma::List(), s_context, binding, x, y, width, height, u0, v0, u1, v1, color);
    return true;
}

bool TexturedRectFixed(tex::Handle texture, const ui::FixedRect & rect, gs::Color color, gs::BlendMode blend)
{
    PS2_AssertMsg(s_frame, "2D fixed texture draw outside a frame");

    ui::Flush();
    gs::TextureBinding binding = {};
    if (!tex::Bind(texture, binding))
    {
        return false;
    }

    gs::DrawState state;
    state.blend = blend;
    gs::TexturedRectFixed(dma::List(), s_context, binding, rect.x0, rect.y0, rect.x1, rect.y1,
                          rect.u0, rect.v0, rect.u1, rect.v1, color, state);
    return true;
}

bool PalettedTriangleFixed(const PalettedDraw & draw, const ui::FixedTriangle & triangle, const Shading & shading)
{
    PS2_AssertMsg(s_frame, "2D paletted triangle draw outside a frame");

    ui::Flush();
    const auto Emit = [&draw](const ui::FixedTriangle & pass, const gs::DrawState & state, int channel)
    {
        // A screen pass samples the CLUT whose alpha carries its channel; white images need none.
        tex::Handle cluts = draw.cluts;

        if (channel >= 0 && !draw.white)
        {
            cluts = draw.channelCluts[channel];
            PS2_AssertMsg(cluts.id != 0, "paletted screen blending without channel CLUTs");
        }

        gs::TextureBinding binding = {};
        gs::TextureBinding clutBinding = {};

        if (!tex::BindPair(draw.page, cluts, binding, clutBinding))
        {
            return false;
        }

        // CLUT blocks are 256 bytes, 64 GS words, inside the CLUT texture's allocation.
        PS2_AssertMsg(draw.clutBlock < clutBinding.layout.bytes / 256U, "paletted draw CLUT block outside its texture");

        binding.clutAddress = clutBinding.address + draw.clutBlock * 64U;
        binding.region = true;
        binding.regionMinU = draw.regionMinU;
        binding.regionMaxU = draw.regionMaxU;
        binding.regionMinV = draw.regionMinV;
        binding.regionMaxV = draw.regionMaxV;
        gs::TexturedTriangleFixed(dma::List(), s_context, binding, pass, state);
        return true;
    };

    return ForEachPass(triangle, shading, Emit);
}

bool TexturedTriangleFixed(tex::Handle texture, const ui::FixedTriangle & triangle, const Shading & shading)
{
    PS2_AssertMsg(s_frame, "2D triangle draw outside a frame");

    ui::Flush();
    const auto Emit = [texture](const ui::FixedTriangle & pass, const gs::DrawState & state, int)
    {
        gs::TextureBinding binding = {};

        if (!tex::Bind(texture, binding))
        {
            return false;
        }

        gs::TexturedTriangleFixed(dma::List(), s_context, binding, pass, state);
        return true;
    };

    return ForEachPass(triangle, shading, Emit);
}

bool LineFixed(const ui::FixedLine & line, const Shading & shading)
{
    PS2_AssertMsg(s_frame, "2D line draw outside a frame");

    ui::Flush();
    const auto Emit = [](const ui::FixedLine & pass, const gs::DrawState & state, int)
    {
        gs::LineFixed(dma::List(), s_context, pass, state);
        return true;
    };

    return ForEachPass(line, shading, Emit);
}

void MaskTriangleFixed(const ui::FixedTriangle & triangle)
{
    PS2_AssertMsg(s_frame, "2D mask draw outside a frame");

    ui::Flush();
    gs::MaskTriangleFixed(dma::List(), s_context, triangle);
}

void MaskLineFixed(const ui::FixedLine & line)
{
    PS2_AssertMsg(s_frame, "2D mask draw outside a frame");

    ui::Flush();
    gs::MaskLineFixed(dma::List(), s_context, line);
}

void ClearMask(int x0, int y0, int x1, int y1)
{
    PS2_AssertMsg(s_frame, "2D mask clear outside a frame");

    ui::Flush();
    gs::ClearMask(dma::List(), s_context, x0, y0, x1, y1);
}

} // namespace ps2::renderer
