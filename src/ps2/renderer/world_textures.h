// ================================================================================================
// File: world_textures.h
// Brief: A cooked world's textures (the map's D3TX): streamed into EE memory a texture at a time by
// the background loader (the areas a pass draws first, then the view's portal neighbours), residency
// in one VRAM arena with LRU eviction and in-chain uploads, and each batch's state, the VIF blocks the display list unpacks into the
// batch's input half for the world program to kick: the material's texture registers and drawing
// tags, and the lightmap page's TEX0 for the other context, or for a material pass the pass's
// texture with blending tags and its colour.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include "ps2/math/vec_mat.h"
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

// Opens the D3TX at 'path' (an OS path the loader thread reads; 'name' is logged), reads and
// validates its header and tables against the world's material names, and keeps the descriptor for
// streaming the textures; null (logged) on refusal. Each texture is validated as it arrives.
PS2_COLD_FUNC Textures * OpenTextures(const char * path, const char * name, const Sections & sections);

// Closes the package once its reads are cancelled. No chain may still read the textures' blocks or
// streams.
PS2_COLD_FUNC void FreeTextures(Textures * textures);

// Plans the next pass (optional, before BeginTextures): BeginPlan, then for every area the pass will
// draw, in its order, PlanAreaTextures and PlanBatch for each of its batches, with the pass's
// textured and lightmapped switches. The area's textures are read first, and the pass never evicts
// a planned texture from VRAM: BeginTextures makes them resident first.
void BeginPlan(Textures * textures);
void PlanAreaTextures(Textures * textures, u32 area);
void PlanBatch(Textures * textures, u32 material, u32 page, bool textured, bool lightmapped);

// The phases a world pass draws its material passes in, after its batches. First an opaque
// material's additive passes over its base pass, depth still written. Then a translucent material's
// stages over the frame, depth no longer written: its lit diffuse (added, as id lights translucent
// surfaces before its other stages), then its multiply stages (decals), add stages and alpha blends.
enum class PassPhase : u8
{
    Glow,
    Lit,
    Multiply,
    Add,
    Alpha,
    Count
};

// The blend a phase's passes draw with (gs::SetPassBlend).
gs::PassBlend PhaseBlend(PassPhase phase);

// Plans the material passes in 'phase' of a batch of 'material' (a textured pass, after every
// PlanBatch, phase by phase), in the order they draw: one AdvanceBatch each, before its PassBlocks.
void PlanMaterialPasses(Textures * textures, u32 material, PassPhase phase);

// The order a pass draws its batches in: by diffuse texture, then lightmap page (null textures: by
// page alone).
u32 BatchOrderKey(const Textures * textures, u32 material, u32 page);

// Starts a world pass in 'context': the states are written for its registers and LOD, where
// 'focal' is GS pixels per world unit at depth 1, and the material passes time drives are evaluated
// at 'time' (seconds). 'textures' may be null (every batch untextured and unlit); 'textured' and
// 'lightmapped' switch the diffuse textures (with the material passes) and the lightmap pass.
//
// Streaming: reads that landed are validated and kept; the planned areas' textures are requested
// first, then those of the portal neighbours of 'viewArea' (the area the eye is in, -1 for none),
// in the background. A texture whose read has not landed draws as one the arena cannot hold. After
// a jump (the package's first pass, a view area that is not the last one or its neighbour, or a
// switch turned on) the pass waits for its planned areas' textures instead. Copies beyond a budget
// leave, least recently wanted first, once no chain reads them.
//
// A planned pass's textures are then made resident, in the list: when a view brings in new ones,
// the textures it does not need leave, and if the rest cannot be placed around the ones staying,
// the arena empties and takes the planned set in draw order (a repack).
void BeginTextures(Textures * textures, dma::DisplayList & list, gs::DrawContext context, float focal, bool textured, bool lightmapped,
                   float time, int viewArea);

// Steps to the next planned batch, before its BatchState (or a planned material pass, before its
// PassBlocks): once per PlanBatch entry, then per PlanMaterialPasses entry, in the same order. Its
// textures are pinned, and what lies ahead in the plan decides what a mid-pass eviction gives up.
void AdvanceBatch(Textures * textures);

// Ends the pass BeginTextures began: the arena may give pages back to the UI's textures again.
void EndTextures(Textures * textures);

// The state for a batch of 'material': the material's texture made resident first (an upload in
// the list, evicting the least recently used textures this frame has not drawn), or untextured
// when it has none or the arena cannot hold it; null when the textured pass does not draw it (a
// translucent material, which only its passes draw, or a sky, which SkyDepthState draws). When no other
// texture can go, the one the pass's plan needs again furthest ahead gives way, drawn or not: the
// chain uploads in order, so its earlier batches drew before its blocks change, and a texture that
// moved within the pass takes a moved state block.
const void * BatchState(Textures * textures, dma::DisplayList & list, u32 material);

// Whether a textured pass draws 'material' as a sky: its surfaces write depth alone, before the
// rest of the pass, and the sky box (DrawSkyBox, drawn under the whole pass) shows through them.
// An untextured pass draws a sky as any untextured surface (BatchState).
bool IsSky(const Textures * textures, u32 material);

// The state of a sky batch in the pass's context: untextured, every pixel failing an alpha test
// that keeps depth (AFAIL ZB_ONLY), so it writes depth GEQUAL and no colour.
const void * SkyDepthState();

// The page state for a batch of lightmap 'page' (kNoLightmap for none): the page made resident
// first, or the state without a lightmap pass when it cannot be.
const void * PageState(Textures * textures, dma::DisplayList & list, u32 page);

// The passes a batch of 'material' draws after the batches: 0 untextured.
u32 PassCount(const Textures * textures, u32 material);

// The phase pass 'pass' of 'material' (below PassCount) draws in.
PassPhase PassPhaseOf(const Textures * textures, u32 material, u32 pass);

// Pass 'pass' of 'material' (below PassCount): its state, in BatchState's form, and its colour
// block, in PageState's place (the unlit page block for a pass that takes the batch's vertex
// colours), with its texture made resident first. False when it does not draw this frame (its
// condition, or a texture the arena cannot hold, counted in passFallbacks).
bool PassBlocks(Textures * textures, dma::DisplayList & list, u32 material, u32 pass, const void *& state, const void *& color);

// The sky box of the textured pass in 'context' through the clip space of 'viewProjection' (its
// translation ignored: the box is the directions around the eye), drawn in the list before
// BeginDepthPass with depth neither tested nor written, its faces made resident first; a face the
// arena cannot hold is left out (skyFallbacks). Nothing without a sky or textures.
void DrawSkyBox(Textures * textures, dma::DisplayList & list, gs::DrawContext context, const math::Mat4 & viewProjection);

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
    // Sky box faces drawn (clipped to the guard band, any part on screen), and those left out.
    u32 skyFaces;
    u32 skyFallbacks;
    // Passes whose planned textures did not fit around the ones resident: the arena emptied first.
    u32 repacks;
    // Passes whose planned textures exceed the arena even empty (they load as their batches draw),
    // textures given up mid-pass for one a batch needed (FurthestUse), the state blocks re-placed
    // textures took, and the batches left out because none was left; and the bytes uploaded.
    u32 overflows;
    u32 passEvictions;
    // The arena's share of the texture range: times it gave pages back to UI textures, the bytes it
    // gave, and times it grew back over pages they freed.
    u32 vramYields;
    u32 yieldedBytes;
    u32 regrows;
    u32 movedStates;
    u32 movedFallbacks;
    u32 uploadBytes;
    // Streaming: textures read in and their bytes; copies in EE memory now, their bytes and the most
    // they held; reads refused (a read error or a file that failed validation: never read again);
    // draws left out because a texture's read had not landed (counted in the fallbacks above too);
    // the passes that waited for their textures after a jump, with the time they waited; and the
    // loader requests the loads took (neighbouring files gather into one).
    u32 streamLoads;
    u32 streamBytes;
    u32 copies;
    u32 copyBytes;
    u32 copyPeakBytes;
    u32 streamFailures;
    u32 streamMisses;
    u32 streamWaits;
    u32 streamWaitMs;
    u32 streamReads;
};

TextureStats GetTextureStats(const Textures * textures);

} // namespace ps2::renderer::world
