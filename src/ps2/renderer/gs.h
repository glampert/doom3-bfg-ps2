// ================================================================================================
// File: gs.h
// Brief: Owns the drawing contexts in a 16- or 32-bit frame-buffer profile and emit clipped direct
//        RGBA sprites and triangles into the caller's display list, and describes the window and
//        drawing tags VU1 programs output.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/math/vec_mat.h"
#include "ps2/renderer/dma.h"
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
constexpr u8 kWriteRed   = 1;
constexpr u8 kWriteGreen = 2;
constexpr u8 kWriteBlue  = 4;
constexpr u8 kWriteRgb   = 7;

// The colour buffers and the depth buffer (RENDERING_ARCHITECTURE.md 4.3), named draw/display
// bits. The GS pairs PSMCT32 only with depth formats of its own group, so the 32-bit profiles'
// depth is PSMZ16S.
enum class FrameFormat : u8
{
    Color16,         // 16/16: two PSMCT16 buffers + PSMZ16, five bits a channel, one alpha bit
    Color32,         // 32/32: two PSMCT32 buffers + PSMZ16S, eight bits a channel and destination alpha
    Color32Display16 // 32/16: one PSMCT32 draw buffer + PSMZ16S, copied at present into one PSMCT16S
                     // display buffer with dithering: 32-bit blending for two thirds of 32/32's VRAM
};

// The frame buffers: one of the standard's resolutions (system::video::ResolutionAt) at the lines
// its interlace mode draws (system::video::FrameHeight), in a frame-buffer profile.
struct Config
{
    system::video::Standard standard;
    int width;
    int height;
    FrameFormat format = FrameFormat::Color16;
    system::video::Interlace interlace = system::video::Interlace::Field;
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

PS2_COLD_FUNC bool Init(const Config & config);
PS2_COLD_FUNC void Shutdown();
bool IsInitialized();

int Width();
int Height();
system::video::Interlace GetInterlace();

// The frame buffers occupy VRAM from address 0 up to FixedVramBytes(); textures live above.
u32 FixedVramBytes();
FramebufferInfo GetFramebuffer(DrawContext context);

FrameFormat GetFrameFormat();
const char * FrameFormatName(FrameFormat format);  // "PSMCT32/Z16S" and the like
const char * FrameFormatLabel(FrameFormat format); // "16/16", "32/16" or "32/32"
int FrameFormatBits(FrameFormat format);           // the draw buffer's bits, which captures read

// The buffer the display circuits read. In the 32/16 profile it is the PSMCT16S copy that Present
// writes; otherwise it is the colour buffer presented last.
FramebufferInfo GetDisplayFramebuffer();

// Immediate: wait for the queued chain, then flip at the next vsync (the 2D path, which needs
// no following frame). Pipelined: the context's chain already ran (the caller waited for it, with
// the next frame's chain built and not yet sent); flip at the next vsync. In the 32/16 profile the
// flip is a dithered copy of the draw buffer into the display buffer, made in the vertical blank
// before the display reads it again.
enum class PresentMode : u8
{
    Immediate,
    Pipelined
};

void Present(DrawContext context, PresentMode mode = PresentMode::Immediate);

// The presents and their waits for the vertical blank, cumulative (the frame log's vsync column;
// the waits are counted with PS2_D3BFG_FRAME_LOG only).
struct PresentStats
{
    u32 presents;
    u32 vsyncs;      // vertical blanks waited for
    u64 vsyncCycles; // EE cycles spent waiting for them (COP0 Count)
};

const PresentStats & GetPresentStats();

// The window transform VU1 programs apply to clip coordinates over w before ftoi4 (x16, 12.4
// fixed point): x and y of +-1 reach 2048 pixels either side of the screen centre, and z from +1
// (near) to -1 (far) spans the Z16 buffer, larger nearer (GEQUAL). The w lanes are zero.
math::Vec4 WindowScale();
math::Vec4 WindowOffset();

// GS primitive types (PRIM.PRIM).
enum class Primitive : u8
{
    Point,
    Line,
    LineStrip,
    Triangle,
    TriangleStrip,
    TriangleFan,
    Sprite
};

// The registers each vertex of a PACKED drawing tag writes, in order.
enum class VertexFormat : u8
{
    ColorPosition,   // RGBAQ, XYZ2
    StColorPosition  // ST (with Q), RGBAQ, XYZ2: PACKED ST's Q reaches the GS only with the RGBAQ write
};

// A PACKED GIF tag whose PRE sets a Gouraud-shaded 'primitive' in 'context', sampling the bound
// texture through STQ when 'textured' and blending through the context's ALPHA when 'blended';
// NLOOP and EOP are zero, for the VU1 program to fill per kick.
qword_t DrawingTag(Primitive primitive, VertexFormat format, DrawContext context, bool textured = false, bool blended = false);

// The world's lightmap pass for a frame drawn in 'context': the same, textured and alpha blended,
// in the other context, whose registers BeginDepthPass sets to multiply the frame by the
// lightmap's alpha (Cd x As).
qword_t LightmapTag(Primitive primitive, VertexFormat format, DrawContext context);

DrawContext OtherContext(DrawContext context);

// Uploads a binding's complete storage: padded RGBA32, or PSMT8/PSMT4 index rows.
void Upload(const TextureBinding & binding, const void * pixels);

// 2D drawing, appended to 'list':
void TexturedRect(dma::DisplayList & list, DrawContext context, const TextureBinding & binding,
                  int x, int y, int width, int height,
                  int u0, int v0, int u1, int v1, Color color);

// Coordinates exclude XYOFFSET and are already clipped, in GS 1/16 pixel/texel units.
void TexturedRectFixed(dma::DisplayList & list, DrawContext context, const TextureBinding & binding,
                       int x0, int y0, int x1, int y1, int u0, int v0, int u1, int v1, Color color, const DrawState & state);

void TexturedTriangleFixed(dma::DisplayList & list, DrawContext context, const TextureBinding & binding,
                           const ui::FixedTriangle & triangle, const DrawState & state);

// An untextured Gouraud line: the GS draws its start pixel but not its end pixel.
void LineFixed(dma::DisplayList & list, DrawContext context, const ui::FixedLine & line, const DrawState & state);
void FillRect(dma::DisplayList & list, DrawContext context, int x, int y, int width, int height, Color color);

// Fills the color buffer and resets the depth mask: no pixel is inside it afterwards.
void ClearFrame(dma::DisplayList & list, DrawContext context, Color color);

// The world's depth state for 'context': depth written and tested GEQUAL (larger is nearer). The
// other context becomes the lightmap pass's: the same frame with its alpha masked, depth tested
// GEQUAL and not written, ALPHA Cd x As, bilinear and clamped at a fixed LOD. End restores the 2D
// environment (depth writes masked, the other context's own frame). Each begins with a VIF FLUSH,
// so it lands between whole PATH1 packets: after every batch before it, before any after it.
void BeginDepthPass(dma::DisplayList & list, DrawContext context);
void EndDepthPass(dma::DisplayList & list, DrawContext context);

#if PS2_D3BFG_GPU_SAMPLER
// The fill A/B (world::SetFillEnabled): 'limited' scissors both contexts to the frame's first pixel,
// so the GS takes every primitive and fills next to nothing; false gives them the whole frame back,
// as the environment set it. After a VIF FLUSH, like the depth pass.
void LimitFill(dma::DisplayList & list, bool limited);
#endif // PS2_D3BFG_GPU_SAMPLER

// How the world pass's blended batches in 'context' mix with the frame: their colour added (the
// state BeginDepthPass leaves), the frame multiplied by their alpha (Cd x As), or the two mixed by
// it. Between batches of a depth pass: the pending batch starts and a VIF FLUSH waits for it. Depth
// stays tested GEQUAL and, past an opaque material's passes, is no longer written. EndDepthPass
// ends both.
enum class PassBlend : u8
{
    Add,
    Multiply,
    Alpha
};

void SetPassBlend(dma::DisplayList & list, DrawContext context, PassBlend blend, bool depthWrites);

// The depth buffer holds one mask: these write coverage into it without touching color, and
// ClearMask removes it from a pixel rectangle. A masked DrawState passes only inside it.
void MaskTriangleFixed(dma::DisplayList & list, DrawContext context, const ui::FixedTriangle & triangle);
void MaskLineFixed(dma::DisplayList & list, DrawContext context, const ui::FixedLine & line);
void ClearMask(dma::DisplayList & list, DrawContext context, int x0, int y0, int x1, int y1);

} // namespace ps2::renderer::gs
