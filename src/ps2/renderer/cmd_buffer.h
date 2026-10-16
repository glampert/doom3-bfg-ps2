// ================================================================================================
// File: cmd_buffer.h
// Brief: Own aligned double-buffered VIF1 DIRECT chains and their DMA/GS completion fences.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once
#include "ps2/renderer/gif_writer.h"

namespace ps2::cmdbuf
{
struct Stats
{
    u32 peakBytes;
    u32 kicks;
    u32 drains;
};

bool Init(u32 halfBytes);
void Shutdown();
bool IsInitialized();
void BeginFrame();
void EndFrame();
// Every block is closed before another reservation can drain or invalidate its writer.
GifWriter BeginDirect(int payloadQwords);
void EndDirect(const GifWriter & writer);
void Kick();
void WaitIdle();
Stats GetStats();
} // namespace ps2::cmdbuf
