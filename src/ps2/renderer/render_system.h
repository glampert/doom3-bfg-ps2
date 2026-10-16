// ================================================================================================
// File: render_system.h
// Brief: Keep CPU 2D frame ownership separate from Doom's logical render-system provider.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once
#include "ps2/renderer/cmd_buffer.h"
#include "ps2/renderer/gs.h"
#include "ps2/renderer/texture.h"
#include "ps2/renderer/ui_geometry.h"

namespace ps2::rs
{
struct Config
{
    video::Standard standard = video::Standard::Ntsc;
    u32 commandHalfBytes = 128U * 1024U;
    u32 textureVramBytes = 2U * 1024U * 1024U;
    u32 texturePixelBytes = 2U * 1024U * 1024U;
};

bool Init(const Config & config);
void Shutdown();
bool IsInitialized();
bool FrameStarted();
void BeginFrame(gs::Color clearColor);
void FillRect(int x, int y, int width, int height, gs::Color color);
bool TexturedRect(tex::Handle texture, int x, int y, int width, int height, int u0, int v0, int u1, int v1,
                  gs::Color color = { 255, 255, 255, 255 });
bool TexturedRectFixed(tex::Handle texture, const ui::FixedRect & rect, gs::Color color, gs::BlendMode blend);
bool TexturedTriangleFixed(tex::Handle texture, const ui::FixedTriangle & triangle, gs::BlendMode blend);
void EndFrame(); // Immediate completed-frame presentation; deferred mode is a later gate.
u32 FrameCount();
gs::DrawContext LastPresentedContext();
} // namespace ps2::rs
