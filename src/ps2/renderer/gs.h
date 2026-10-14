// ================================================================================================
// File: gs.h
// Brief: Own fixed 16-bit drawing contexts and emit clipped solid and direct RGBA sprites.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once
#include "ps2/renderer/gif_writer.h"
#include "ps2/system/video_mode.h"
#include "ps2/renderer/texture_layout.h"

namespace ps2::gs
{
enum class DrawContext : int { Ctx0, Ctx1 };
struct Config { video::Standard standard; int width; int height; };
struct Color { u8 r; u8 g; u8 b; u8 a; };
struct FramebufferInfo { u32 address; int width; int height; int pixelFormat; };
struct TextureBinding { u32 address; tex::Layout layout; tex::Sampler sampler; };
bool Init(const Config & config);
void Shutdown();
bool IsInitialized();
int Width();
int Height();
u32 FixedVramBytes();
FramebufferInfo GetFramebuffer(DrawContext context);
void UploadRgba32(const TextureBinding & binding, const void * pixels);
void TexturedRect(DrawContext context, const TextureBinding & binding, int x, int y, int width, int height,
                  int u0, int v0, int u1, int v1, Color color);
void FillRect(DrawContext context, int x, int y, int width, int height, Color color);
void Present(DrawContext context);
} // namespace ps2::gs
