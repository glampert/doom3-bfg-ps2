// ================================================================================================
// File: lightmap.h
// Brief: Bake a map's static lights (class A) into Light8 pages for the PS2's lightmapped world
// (RENDERING_ARCHITECTURE.md 8.2, 8.3): charts of the lit area surfaces, each texel's light from
// id's projection and falloff images with N.L and ray-cast shadows, split into a per-vertex chroma
// (times a per-chart exposure) and a texel intensity the GS multiplies in with Cd x As.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "assets/light.h"
#include "assets/proc.h"
#include "assets/world.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace assets
{

struct LightmapPolicy
{
    // World units per texel; a chart too large for a page takes a coarser one.
    float luxelSize = 16.0f;
    int pageSize = 256;
    // Texels around each chart beyond the half texel bilinear filtering reads, so the GS's 1/16
    // texel UV steps never reach a neighbour.
    int border = 1;
    // Triangles join a chart while their normal stays within this many degrees of its first one.
    float chartAngle = 30.0f;
    // Shadow rays per texel for lights that cast shadows: 1, or 4 on a 2x2 grid inside the texel.
    int shadowSamples = 4;
    // Ray origins leave the surface by this much, along its normal.
    float rayOffset = 0.5f;
    // A chart whose texels stay within this many steps of each other becomes one texel.
    int flatSteps = 1;
    // The chart exposure E, the vertex colour's share of the light (texels hold I / E; each side
    // reaches 255 / 128 at most): E = max intensity / (255 / 128), clamped to [minExposure, 255 / 128].
    float minExposure = 0.25f;
};

// What the bake reads from a surface's material.
struct SurfaceShading
{
    bool lit = false;
    bool castsShadow = false;
    // Alpha-tested: the lightmap pass cannot test the diffuse's alpha (and the GS has no EQUAL
    // depth test), so it would darken whatever the holes show; these surfaces take their light
    // per vertex instead, without a page.
    bool perforated = false;
};

struct LightmapScene
{
    const ProcFile * proc = nullptr;
    // Keyed by the material name as the proc spells it; an absent material is neither lit nor
    // a shadow caster.
    std::map<std::string, SurfaceShading> materials;
    std::vector<LightSource> lights;
    // Shadow casters besides the area models (entity brush models placed in the world).
    std::vector<std::array<Vec3, 3> > occluders;
};

// One page's texels, row by row: the intensity over the chart's exposure, 128 = 1.0.
struct LightmapPage
{
    int size = 0;
    std::vector<std::uint8_t> texels;
};

struct LightmapStats
{
    std::size_t litSurfaces = 0;
    std::size_t vertexLitSurfaces = 0;
    std::size_t charts = 0;
    // Charts baked to a single value (small, unlit or flat), drawn from a 3x3 block.
    std::size_t pointCharts = 0;
    std::size_t scaledCharts = 0;
    std::size_t texels = 0;
    std::size_t pages = 0;
    std::size_t occluders = 0;
    std::size_t rays = 0;
    // Texels or vertex colours the exposure range clipped.
    std::size_t clippedTexels = 0;
    // Lights per lit surface (the ones whose volume and area flood reach it).
    std::size_t surfaceLights = 0;
    std::size_t maxSurfaceLights = 0;
    float maxIntensity = 0.0f;
};

// Bakes every area model of the scene's proc: 'lighting' gets one entry per area surface (lit or
// not), 'pages' the Light8 pages its triangles name.
bool BakeLightmaps(const LightmapScene & scene, const LightmapPolicy & policy, WorldLighting & lighting, std::vector<LightmapPage> & pages,
                   LightmapStats & stats, std::string & error);

} // namespace assets
