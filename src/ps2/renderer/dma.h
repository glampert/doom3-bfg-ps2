// ================================================================================================
// File: dma.h
// Brief: The display list system: owns the two per-frame chain rings in main RAM, builds each
//        frame's chain through the scratchpad DisplayList, kicks it through VIF1 and fences on the
//        GS FINISH. Today every draw is a DIRECT block to PATH2; VU batches join the same chain.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/renderer/gif_writer.h"

namespace ps2::renderer::dma
{

// Two chain rings of 'ringBytes' each (64-byte multiple), one per in-flight frame.
PS2_COLD_FUNC bool Init(u32 ringBytes);
PS2_COLD_FUNC void Shutdown();
bool IsInitialized();

void BeginFrame();
void EndFrame();

// A CNT block whose payload is GIF data sent through DIRECT. Every block is closed before another
// reservation can drain or invalidate its writer.
gs::GifWriter BeginDirect(int payloadQwords);
void EndDirect(const gs::GifWriter & writer);

// Kick: close the chain built so far (FLUSH, FINISH, END) and start VIF1 on it; later blocks
// start a new chain in the same ring. WaitIdle: the kicked chain has run and the GS has finished.
void Kick();
void WaitIdle();

struct Stats
{
    u32 peakBytes; // most ring bytes a frame's chains occupied
    u32 kicks;     // VIF1 chains started
    u32 drains;    // full rings that forced a kick, wait and restart mid-frame
    u32 sprDrains; // scratchpad halves drained by the fromSPR DMA
};

const Stats & GetStats();

} // namespace ps2::renderer::dma
