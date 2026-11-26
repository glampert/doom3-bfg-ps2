// ================================================================================================
// File: world.h
// Brief: A cooked world (D3WD) loaded whole into one DMA-ready allocation and drawn by the VU1
// world program: the display list REFs every batch packet in place, so a frame costs one tag per
// batch on the EE however many vertices the batch holds.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include "ps2/math/vec_mat.h"
#include "ps2/renderer/gs.h"
#include "ps2/renderer/vu.h"
#include "ps2/renderer/world_format.h"

namespace ps2::renderer::world
{

// The whole file in one 64-byte-aligned TAG_RENDER allocation, validated and written back from
// the data cache once: the packets are DMA territory from then on and the EE only reads records.
struct Textures;

struct Level
{
    void * storage = nullptr;
    u32 bytes = 0;
    Sections sections = {};
    // The map's D3TX (world_textures.h), when it has one; batches draw untextured without it.
    Textures * textures = nullptr;
    // The batches a pass draws, in its order (PlanArea fills it, BeginPass sorts it): one entry a
    // batch of the level.
    u32 * passOrder = nullptr;
};

// Reads and validates 'path' (every build); false, with nothing held, if it cannot be used.
PS2_COLD_FUNC bool Load(const char * path, Level & level);

// Opens the texture package at 'path' (an OS path) for the loaded 'level' (OpenTextures); false, with
// the level untextured, if it cannot be used.
PS2_COLD_FUNC bool LoadTextures(const char * path, Level & level);

// Reads 'path' whole into a 64-byte aligned TAG_RENDER allocation of at most 'limit' bytes; null
// (logged) when it cannot.
PS2_COLD_FUNC void * ReadWhole(const char * path, u32 limit, u32 & bytes);

// Takes a file already read into 'storage' (a TAG_RENDER allocation of kPacketAlignment):
// validates it and writes it back from the data cache, or frees it and returns false.
PS2_COLD_FUNC bool Adopt(void * storage, u32 bytes, const char * name, Level & level);

// The caller has ended its frames, or none is in flight.
PS2_COLD_FUNC void Unload(Level & level);

// The frame constants for drawing with 'viewProjection' (row vectors, math::PerspectiveProjection's
// clip space) into 'context': the 4096-unit GS window as the guard band and depth 0xFFFF at the
// near plane. The world programs take their drawing tags from each batch's state.
vu::FrameConstants MakeFrameConstants(const math::Mat4 & viewProjection, gs::DrawContext context);

// The world pass inside the open renderer frame. PlanArea first names each area the pass will draw
// (once each); BeginPass orders their batches by texture and lightmap page and plans them, their
// textures and pages going resident before anything draws (world_textures.h), then sets the frame
// constants, draws the sky box when 'sky' (a planned area shows the sky), then the depth test and the
// level's program once (world_lm.vusl, or world_strips.vusl for a plain-strips cook). DrawAreaSky
// writes the depth of one area's sky surfaces, for every such area before EndPass, so whatever lies
// behind a sky surface stays hidden. EndPass sends every planned batch after its texture state
// (uploading textures as they become needed), then each batch's material passes, and starts the last
// batch. 'time' (seconds, the view's) drives the material passes' registers, and
// 'viewArea' (the area the eye is in, -1 for none) names the area whose portal neighbours' textures
// are read ahead (world_textures.h).
bool PlanArea(const Level & level, u32 area); // whether the textured pass draws sky surfaces in it
void BeginPass(const Level & level, const math::Mat4 & viewProjection, float time, bool sky = false, int viewArea = -1);
void DrawAreaSky(const Level & level, u32 area);
void EndPass();

// One pass over every area of 'level'.
void Draw(const Level & level, const math::Mat4 & viewProjection, float time);

// Whether passes draw the level's textures (on by default); off, every batch draws untextured, as
// the vertex-colour references expect.
void SetTexturesEnabled(bool enabled);
bool TexturesEnabled();

// The lightmap pass of lit batches (on by default; off draws the vertex colour's chroma alone).
void SetLightmapsEnabled(bool enabled);
bool LightmapsEnabled();

#if PS2_D3BFG_GPU_SAMPLER
// The GS fill A/B: off, a pass draws everything as usual into a one-pixel scissor (its sky box too),
// so VU1 and the GIF do the pass's work while the GS fills almost nothing (on by default).
void SetFillEnabled(bool enabled);
bool FillEnabled();
#endif // PS2_D3BFG_GPU_SAMPLER

// The texture residency of the last pass's level (zero when it had no textures or they were off).
struct TextureStats;
const TextureStats & LastPassTextureStats();

// What every world pass sent, cumulative since boot (the frame log's draw columns, counted with
// PS2_D3BFG_FRAME_LOG only): the batches (sky
// depth and material passes included), the lit ones among them (walked a second time for the
// lightmap pass), the material pass batches, and the vertices and packet qwords of all of them.
struct DrawStats
{
    u32 batches;
    u32 litBatches;
    u32 passBatches;
    u32 vertices;
    u32 packetQwords;
};

const DrawStats & GetDrawStats();

} // namespace ps2::renderer::world
