// ================================================================================================
// File: scene.h
// Brief: Draw id's views on the PS2. id's portal flow (FindViewLightsAndEntities) lists what a
// view can see; RenderView sends the world areas it reached through the VU1 world pass, in place
// of id's light, model and draw-surface passes (RENDERING_ARCHITECTURE.md 5.2, 5.3).
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

// Included by id's frontend, which has no tamtypes.h.
#include <cstdint>

struct viewDef_t;
class idRenderWorld;

namespace ps2::renderer::scene
{

// The frame arena id's frontend allocates views from (R_InitFrameData), for visible
// presentation only; Shutdown releases it.
PS2_COLD_FUNC void Init();
PS2_COLD_FUNC void Shutdown();
bool IsInitialized();

// Resets the frame arena: once per renderer frame, before any view.
void BeginFrame();

// Draws one view into the open renderer frame; R_RenderView calls it after the portal flow.
void RenderView(const viewDef_t & view);

struct Stats
{
    std::uint32_t views      = 0;  // views drawn since Init
    std::uint32_t frame      = 0;  // the renderer frame count once the last view's frame is presented
    int           area       = -1; // the area the last view started in, -1 outside the world
    std::uint32_t entities   = 0;  // view entities the portal flow listed for the last view
    std::uint32_t areas      = 0;  // world areas the last view drew
    std::uint32_t batches    = 0;  // world batches the last view drew
    std::uint32_t arenaBytes = 0;  // the frame arena's largest use in one frame
};

const Stats & GetStats();

// The decals a render world's entities hold (built when their entity was last seen), for the world
// viewer's report: their count, geometry, total area and world-space bounds.
struct DecalStats
{
    std::uint32_t entities  = 0; // entities holding at least one decal
    std::uint32_t decals    = 0;
    std::uint32_t vertices  = 0;
    std::uint32_t triangles = 0; // without the degenerate ones padding each index list
    float         area      = 0.0f;
    float         boundsMin[3] = {};
    float         boundsMax[3] = {};
};

PS2_COLD_FUNC DecalStats MeasureDecals(const idRenderWorld & world);

} // namespace ps2::renderer::scene
