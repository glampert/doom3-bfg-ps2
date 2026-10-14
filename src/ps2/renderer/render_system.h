// ================================================================================================
// File: render_system.h
// Brief: Keep CPU 2D frame ownership separate from Doom's logical render-system provider.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once
#include "ps2/renderer/gs.h"
#include "ps2/renderer/cmd_buffer.h"

namespace ps2::rs
{
struct Config
{
    video::Standard standard = video::Standard::Ntsc;
    u32 commandHalfBytes = 128U * 1024U;
};
bool Init(const Config & config);
void Shutdown();
bool IsInitialized();
bool FrameStarted();
void BeginFrame(gs::Color clearColor);
void FillRect(int x, int y, int width, int height, gs::Color color);
void EndFrame(); // Immediate completed-frame presentation; deferred mode is a later gate.
u32 FrameCount();
gs::DrawContext LastPresentedContext();
} // namespace ps2::rs
