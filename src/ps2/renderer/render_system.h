// ================================================================================================
// File: render_system.h
// Brief: Keep CPU 2D frame ownership separate from Doom's logical render-system provider.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once
#include "ps2/renderer/cmd_buffer.h"
#include "ps2/renderer/gs.h"
#include "ps2/renderer/texture.h"
#include "ps2/ui/geometry.h"

namespace ps2::renderer
{
struct Config
{
    system::video::Standard standard = system::video::Standard::Ntsc;
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

// What native drawing asks of one primitive; each primitive expands into one or more GS passes.
struct Shading
{
    gs::BlendMode blend = gs::BlendMode::StraightAlpha;
    // Pass only inside the depth mask.
    bool masked = false;
    // A native SWF color transform's additive RGB, 0..255 per unit and negative to subtract. A
    // constant-color pass weighted by the source alpha follows the textured pass; native GUI
    // shading adds it before premultiplying, so the sum equals one premultiplied pass.
    s16 add[3] = {};
    // Every vertex RGB is zero: one constant-color pass of the additive color replaces both.
    bool replaceColor = false;
};

bool TexturedRectFixed(tex::Handle texture, const ui::FixedRect & rect, gs::Color color, gs::BlendMode blend);
// Screen passes use the texture's alpha for every channel: only white RGB textures qualify.
bool TexturedTriangleFixed(tex::Handle texture, const ui::FixedTriangle & triangle, const Shading & shading);
bool LineFixed(const ui::FixedLine & line, const Shading & shading);

// One placement of a paletted page: the CLUT is a block of a separate CLUT texture, and sampling
// clamps to the placement's texels so neighbors packed with other palettes are never read.
struct PalettedDraw
{
    tex::Handle page;
    tex::Handle cluts;
    // Screen passes, per color channel: CLUT textures laid out like cluts whose alpha is that
    // channel times alpha. Without them only a white image (every CLUT entry RGB 255) qualifies.
    tex::Handle channelCluts[3] = {};
    bool white = false;
    u32 clutBlock = 0;
    u16 regionMinU = 0;
    u16 regionMaxU = 0;
    u16 regionMinV = 0;
    u16 regionMaxV = 0;
};
bool PalettedTriangleFixed(const PalettedDraw & draw, const ui::FixedTriangle & triangle, const Shading & shading);

// The depth mask: one at a time, written without color and cleared by pixel rectangle.
void MaskTriangleFixed(const ui::FixedTriangle & triangle);
void MaskLineFixed(const ui::FixedLine & line);
void ClearMask(int x0, int y0, int x1, int y1);
void EndFrame(); // Immediate completed-frame presentation; deferred mode is a later gate.
u32 FrameCount();
gs::DrawContext LastPresentedContext();
} // namespace ps2::renderer
