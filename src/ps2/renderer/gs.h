// ================================================================================================
// File: gs.h
// Brief: Own fixed 16-bit drawing contexts and emit clipped direct RGBA sprites and triangles.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once
#include "ps2/renderer/gif_writer.h"
#include "ps2/renderer/texture_layout.h"
#include "ps2/system/video_mode.h"
#include "ps2/ui/triangles.h"

namespace ps2::renderer::gs
{
enum class DrawContext : int
{
    Ctx0,
    Ctx1
};
enum class BlendMode : u8
{
    Replace,
    StraightAlpha,
    // Straight RGBA input; matches the native SWF premultiplied shader's ONE/ONE RGB sum.
    AdditiveAlpha,
    // Cd - Cs * As: the negative part of a native SWF additive color term.
    SubtractAlpha,
    // Cd + Cd * As: the PC renderer's SWF screen mode (DST_COLOR, ONE), one channel per pass.
    Screen
};

// Framebuffer color channels a pass writes (FBMSK).
static constexpr u8 kWriteRed = 1;
static constexpr u8 kWriteGreen = 2;
static constexpr u8 kWriteBlue = 4;
static constexpr u8 kWriteRgb = 7;

struct Config
{
    system::video::Standard standard;
    int width;
    int height;
};

struct Color
{
    u8 r;
    u8 g;
    u8 b;
    u8 a;
};

// One GS pass: how a primitive's pixels combine with the framebuffer.
struct DrawState
{
    BlendMode blend = BlendMode::StraightAlpha;
    // Fog at full strength replaces the texture function's RGB with color; alpha is unchanged.
    bool constantColor = false;
    Color color = {};
    // Only pixels inside the active depth mask pass.
    bool masked = false;
    u8 channels = kWriteRgb;
};

struct FramebufferInfo
{
    u32 address;
    int width;
    int height;
    int pixelFormat;
};

struct TextureBinding
{
    u32 address;
    tex::Layout layout;
    tex::Sampler sampler;
    // Paletted layouts: the GS word address of a CSM1 PSMCT32 CLUT (16 or 256 entries).
    u32 clutAddress = 0;
    // When set, sampling clamps to this texel rectangle instead of the whole layout.
    bool region = false;
    u16 regionMinU = 0;
    u16 regionMaxU = 0;
    u16 regionMinV = 0;
    u16 regionMaxV = 0;
};

bool Init(const Config & config);
void Shutdown();
bool IsInitialized();
int Width();
int Height();
u32 FixedVramBytes();
FramebufferInfo GetFramebuffer(DrawContext context);
// Uploads a binding's complete storage: padded RGBA32, or PSMT8/PSMT4 index rows.
void Upload(const TextureBinding & binding, const void * pixels);
void TexturedRect(DrawContext context, const TextureBinding & binding, int x, int y, int width, int height,
                  int u0, int v0, int u1, int v1, Color color);
// Coordinates exclude XYOFFSET and are already clipped, in GS 1/16 pixel/texel units.
void TexturedRectFixed(DrawContext context, const TextureBinding & binding, int x0, int y0, int x1, int y1,
                       int u0, int v0, int u1, int v1, Color color, const DrawState & state);
void TexturedTriangleFixed(DrawContext context, const TextureBinding & binding,
                           const ui::FixedTriangle & triangle, const DrawState & state);
// An untextured Gouraud line: the GS draws its start pixel but not its end pixel.
void LineFixed(DrawContext context, const ui::FixedLine & line, const DrawState & state);
void FillRect(DrawContext context, int x, int y, int width, int height, Color color);
// Fills the color buffer and resets the depth mask: no pixel is inside it afterwards.
void ClearFrame(DrawContext context, Color color);
// The depth buffer holds one mask: these write coverage into it without touching color, and
// ClearMask removes it from a pixel rectangle. A masked DrawState passes only inside it.
void MaskTriangleFixed(DrawContext context, const ui::FixedTriangle & triangle);
void MaskLineFixed(DrawContext context, const ui::FixedLine & line);
void ClearMask(DrawContext context, int x0, int y0, int x1, int y1);
void Present(DrawContext context);
} // namespace ps2::renderer::gs
