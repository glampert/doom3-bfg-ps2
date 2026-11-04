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
struct Level
{
    void * storage = nullptr;
    u32 bytes = 0;
    Sections sections = {};
};

// Reads and validates 'path' (every build); false, with nothing held, if it cannot be used.
PS2_COLD_FUNC bool Load(const char * path, Level & level);
PS2_COLD_FUNC void Unload(Level & level);

// The frame constants for drawing with 'viewProjection' (row vectors, math::PerspectiveProjection's
// clip space) into 'context': the 4096-unit GS window as the guard band, depth 0xFFFF at the near
// plane, and the strip and triangle drawing tags.
vu::FrameConstants MakeFrameConstants(const math::Mat4 & viewProjection, gs::DrawContext context);

// Draws every batch of 'level' into the open renderer frame, depth tested, through world_lm.vcl.
// No culling yet: every area and batch is sent.
void Draw(const Level & level, const math::Mat4 & viewProjection);

} // namespace ps2::renderer::world
