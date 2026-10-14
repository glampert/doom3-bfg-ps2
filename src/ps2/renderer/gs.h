// ================================================================================================
// File: gs.h
// Brief: Own two fixed 16-bit GS drawing contexts and emit clipped screen-space solid sprites.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once
#include "ps2/renderer/gif_writer.h"
#include "ps2/system/video_mode.h"

namespace ps2::gs
{
enum class DrawContext : int { Ctx0, Ctx1 };
struct Config { video::Standard standard; int width; int height; };
struct Color { u8 r; u8 g; u8 b; u8 a; };
bool Init(const Config & config);
void Shutdown();
bool IsInitialized();
int Width();
int Height();
u32 FixedVramBytes();
void FillRect(DrawContext context, int x, int y, int width, int height, Color color);
void Present(DrawContext context);
} // namespace ps2::gs
