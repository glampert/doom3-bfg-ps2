// ================================================================================================
// File: render_system.cpp
// Brief: Alternate GS contexts, clear, draw and immediately present bounded CPU-only frames.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/render_system.h"
#include "ps2/renderer/ui_draw.h"

namespace ps2::rs
{
namespace
{
static bool s_initialized = false;
static bool s_frame = false;
static gs::DrawContext s_context = gs::DrawContext::Ctx0;
static u32 s_frames = 0;
} // namespace

bool IsInitialized() { return s_initialized; }
bool FrameStarted() { return s_frame; }
u32 FrameCount() { return s_frames; }
gs::DrawContext LastPresentedContext()
{
    if (s_frame || s_frames == 0) { FatalError("no completed GS frame"); }
    return s_context;
}

PS2_COLD_FUNC bool Init(const Config & config)
{
    if (s_initialized) { FatalError("render system initialized twice"); }
    // Both sixteen-qword environments plus chain terminators must fit the first half.
    if (!video::IsValid(config.standard) || config.commandHalfBytes < 640U || !cmdbuf::Init(config.commandHalfBytes)) { return false; }
    if (!gs::Init({config.standard, 512, video::VisibleLines(config.standard)}))
    {
        cmdbuf::Shutdown();
        return false;
    }
    if (!tex::Init(config.textureVramBytes, config.texturePixelBytes))
    {
        cmdbuf::Shutdown();
        gs::Shutdown();
        return false;
    }
    s_context = gs::DrawContext::Ctx0;
    s_frames = 0;
    s_initialized = true;
    return true;
}

void BeginFrame(gs::Color clearColor)
{
    if (!s_initialized || s_frame || clearColor.a != 255) { FatalError("invalid 2D BeginFrame state"); }
    cmdbuf::BeginFrame();
    s_context = s_context == gs::DrawContext::Ctx0 ? gs::DrawContext::Ctx1 : gs::DrawContext::Ctx0;
    s_frame = true;
    ui::BeginGeometryFrame();
    gs::FillRect(s_context, 0, 0, gs::Width(), gs::Height(), clearColor);
}
void FillRect(int x, int y, int width, int height, gs::Color color)
{
    if (!s_frame) { FatalError("2D fill outside a frame"); }
    ui::Flush();
    gs::FillRect(s_context, x, y, width, height, color);
}
void EndFrame()
{
    if (!s_frame) { FatalError("2D EndFrame without BeginFrame"); }
    ui::Flush();
    cmdbuf::EndFrame();
    gs::Present(s_context);
    s_frame = false;
    ++s_frames;
}
bool TexturedRect(tex::Handle texture, int x, int y, int width, int height, int u0, int v0, int u1, int v1, gs::Color color)
{
    if (!s_frame) { FatalError("2D texture draw outside a frame"); }
    ui::Flush();
    gs::TextureBinding binding = {};
    if (!tex::Bind(texture, binding)) { return false; }
    gs::TexturedRect(s_context, binding, x, y, width, height, u0, v0, u1, v1, color);
    return true;
}
PS2_COLD_FUNC void Shutdown()
{
    if (s_frame) { EndFrame(); }
    tex::Shutdown();
    // Partial startup and repeated shutdown both leave no DMA references or heap ownership.
    cmdbuf::Shutdown();
    gs::Shutdown();
    s_initialized = false;
    s_frames = 0;
}
bool TexturedRectFixed(tex::Handle texture, const ui::FixedRect & rect, gs::Color color, gs::BlendMode blend)
{
    if (!s_frame) { FatalError("2D fixed texture draw outside a frame"); }
    ui::Flush();
    gs::TextureBinding binding = {};
    if (!tex::Bind(texture, binding)) { return false; }
    gs::TexturedRectFixed(s_context, binding, rect.x0, rect.y0, rect.x1, rect.y1,
                         rect.u0, rect.v0, rect.u1, rect.v1, color, blend);
    return true;
}
bool TexturedTriangleFixed(tex::Handle texture, const ui::FixedTriangle & triangle, gs::BlendMode blend)
{
    if (!s_frame) { FatalError("2D triangle draw outside a frame"); }
    ui::Flush();
    gs::TextureBinding binding = {};
    if (!tex::Bind(texture, binding)) { return false; }
    gs::TexturedTriangleFixed(s_context, binding, triangle, blend);
    return true;
}
} // namespace ps2::rs
