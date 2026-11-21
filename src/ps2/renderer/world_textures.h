// ================================================================================================
// File: world_textures.h
// Brief: A cooked world's textures (the map's D3TX): residency in one VRAM arena with LRU eviction
// and in-chain uploads, and each batch's state, the VIF blocks the display list unpacks into the
// batch's input half for the world program to kick: the material's texture registers and drawing
// tags, and the lightmap page's TEX0 for the other context, or for a material pass the pass's
// texture with blending tags and its colour.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include "ps2/renderer/dma_list.h"
#include "ps2/renderer/gs.h"
#include "ps2/renderer/world_format.h"

namespace ps2::renderer::world
{

// One VIFcode qword (STCYCL 1,1; UNPACK V4-32 9 at XTOP + vu::kBatchStateOffset) and the 9 qwords it
// writes: the A+D packet (TEX0, TEX1, MIPTBP1, MIPTBP2, CLAMP, TEST) and the strip and list tags.
static constexpr u32 kBatchStateBlockQwords = 10;

// One VIFcode qword (STCYCL 1,1; UNPACK V4-32 2 at XTOP + vu::kBatchLightmapOffset) and the 2 qwords
// it writes: an A+D GIF tag and the page's TEX0 for the other context, whose word 3 is 1 when the
// batch draws a lightmap pass and 0 when it does not; or for a material pass its colour, which every
// vertex takes, and a second qword whose word 3 is negative.
static constexpr u32 kPageStateBlockQwords = 3;

struct Textures;

// Validates a D3TX read into 'storage' (a 64-byte aligned TAG_RENDER allocation) against the world's
// material names and takes it; frees it and returns null on refusal.
PS2_COLD_FUNC Textures * AdoptTextures(void * storage, u32 bytes, const char * name, const Sections & sections);

// No chain may still read the textures' blocks or streams.
PS2_COLD_FUNC void FreeTextures(Textures * textures);

// Starts a world pass in 'context': the states are written for its registers and LOD, where
// 'focal' is GS pixels per world unit at depth 1, and the material passes time drives are evaluated
// at 'time' (seconds). 'textures' may be null (every batch untextured and unlit); 'textured' and
// 'lightmapped' switch the diffuse textures (with the material passes) and the lightmap pass.
void BeginTextures(Textures * textures, gs::DrawContext context, float focal, bool textured, bool lightmapped, float time);

// The state for a batch of 'material': the material's texture made resident first (an upload in
// the list, evicting the least recently used textures this frame has not drawn), or untextured
// when it has none or the arena cannot hold it; null when the textured pass does not draw it.
const void * BatchState(Textures * textures, dma::DisplayList & list, u32 material);

// The page state for a batch of lightmap 'page' (kNoLightmap for none): the page made resident
// first, or the state without a lightmap pass when it cannot be.
const void * PageState(Textures * textures, dma::DisplayList & list, u32 page);

// The additive passes a batch of 'material' draws after the base pass: 0 untextured.
u32 PassCount(const Textures * textures, u32 material);

// Pass 'pass' of 'material' (below PassCount): its state, in BatchState's form, and its colour
// block, in PageState's place, with its texture made resident first. False when it does not draw
// this frame (its condition, or a texture the arena cannot hold, counted in passFallbacks).
bool PassBlocks(Textures * textures, dma::DisplayList & list, u32 material, u32 pass, const void *& state, const void *& color);

struct TextureStats
{
    u32 arenaBytes;
    u32 residentBytes;
    u32 resident;
    u32 uploads;
    u32 evictions;
    u32 untexturedFallbacks;
    u32 unlitFallbacks;
    u32 pagesResident;
    // Material passes drawn, and those left out because the arena could not hold their texture.
    u32 passes;
    u32 passFallbacks;
};

TextureStats GetTextureStats(const Textures * textures);

} // namespace ps2::renderer::world
