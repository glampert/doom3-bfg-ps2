// ================================================================================================
// File: dma.h
// Brief: The display list system: owns the aligned double-buffered VIF1 DIRECT chains the GS is
//        fed through today and their DMA/GS completion fences. The scratchpad DisplayList builder
//        and the chain rings of the 3D renderer join this namespace.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/renderer/gif_writer.h"

namespace ps2::renderer::dma
{

PS2_COLD_FUNC bool Init(u32 halfBytes);
PS2_COLD_FUNC void Shutdown();
bool IsInitialized();

void BeginFrame();
void EndFrame();

// Every block is closed before another reservation can drain or invalidate its writer.
gs::GifWriter BeginDirect(int payloadQwords);
void EndDirect(const gs::GifWriter & writer);

void Kick();
void WaitIdle();

struct Stats
{
    u32 peakBytes;
    u32 kicks;
    u32 drains;
};

const Stats & GetStats();

} // namespace ps2::renderer::dma
