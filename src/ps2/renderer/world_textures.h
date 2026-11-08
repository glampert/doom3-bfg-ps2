// ================================================================================================
// File: world_textures.h
// Brief: A cooked world's textures (the map's D3TX): residency in one VRAM arena with LRU eviction
// and in-chain uploads, and each batch's texture state, the 10-qword VIF block the display list
// unpacks into the batch's input half for the world program to kick.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include "ps2/renderer/dma_list.h"
#include "ps2/renderer/gs.h"
#include "ps2/renderer/world_format.h"

namespace ps2::renderer::world
{

// One VIFcode qword (STCYCL 1,1; UNPACK V4-32 9 at XTOP + kVuBatchStateOffset) and the 9 qwords it
// writes: the A+D packet (TEX0, TEX1, MIPTBP1, MIPTBP2, CLAMP, TEST) and the strip and list tags.
static constexpr u32 kBatchStateBlockQwords = 10;

struct Textures;

// Validates a D3TX read into 'storage' (a 64-byte aligned TAG_RENDER allocation) against the world's
// material names and takes it; frees it and returns null on refusal.
PS2_COLD_FUNC Textures * AdoptTextures(void * storage, u32 bytes, const char * name, const Sections & sections);

// No chain may still read the textures' blocks or streams.
PS2_COLD_FUNC void FreeTextures(Textures * textures);

// Starts a world pass in 'context': the states are written for its registers and LOD, where
// 'focal' is GS pixels per world unit at depth 1. 'textures' may be null (every batch untextured).
void BeginTextures(Textures * textures, gs::DrawContext context, float focal);

// The state for a batch of 'material': the material's texture made resident first (an upload in
// the list, evicting the least recently used textures this frame has not drawn), or untextured
// when it has none or the arena cannot hold it.
const void * BatchState(Textures * textures, dma::DisplayList & list, u32 material);

struct TextureStats
{
    u32 arenaBytes;
    u32 residentBytes;
    u32 resident;
    u32 uploads;
    u32 evictions;
    u32 untexturedFallbacks;
};

TextureStats GetTextureStats(const Textures * textures);

} // namespace ps2::renderer::world
