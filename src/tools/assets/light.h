// ================================================================================================
// File: light.h
// Brief: A map's lights as id's renderer derives them (idGameEdit::ParseSpawnArgsToRenderLight,
// R_DeriveLightData's projection and bounds), their classes A to F for the PS2's lighting split
// (RENDERING_ARCHITECTURE.md 8.1), and a light's colour at a point from its material's stages and
// images, which the lightmap baker sums.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "assets/image.h"
#include "assets/map_entities.h"
#include "assets/material.h"

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace assets
{

using Vec3 = std::array<float, 3>;

// Plan section 8.1.
enum class LightClass : std::uint8_t
{
    // A: static and always on; baked into the lightmap.
    Static,
    // B: scalar-animated by a table or time.
    Animated,
    // C: switchable: start_off, breakable, levels, targeted or scripted.
    Switched,
    // D: driven by sound.
    Sound,
    // E: moving (bound to a mover or joint), a rotating or scrolling texture, or a script that
    // changes its shader, parms or radius.
    Moving,
    // F: fog and blend lights.
    Fog,
    Count
};

const char * LightClassName(LightClass value);

// renderLight_t's fields the bake reads.
struct RenderLight
{
    std::string name;
    // Lowercase; empty when the material is absent (id then uses the default for the type).
    std::string shader;
    Vec3 origin = {};
    std::array<Vec3, 3> axis = { Vec3{ 1.0f, 0.0f, 0.0f }, Vec3{ 0.0f, 1.0f, 0.0f }, Vec3{ 0.0f, 0.0f, 1.0f } };
    bool pointLight = false;
    bool parallel = false;
    Vec3 lightRadius = {};
    Vec3 lightCenter = {};
    Vec3 target = {};
    Vec3 up = {};
    Vec3 right = {};
    Vec3 start = {};
    Vec3 end = {};
    bool noShadows = false;
    bool noSpecular = false;
    std::array<float, 12> parms = {};
};

// idGameEdit::ParseSpawnArgsToRenderLight; false (with a reason) for bad target info, as id refuses
// it. shaderParm4 keeps its own default of 0 (id offsets it by the spawn time).
bool ParseRenderLight(const MapEntity & entity, RenderLight & output, std::string & error);

// R_DeriveLightData: the old style lightProject (world planes S, T, Q, then the falloff
// coordinate), the light origin for N.L, and the bounds of the projected volume.
struct LightVolume
{
    std::array<std::array<float, 4>, 4> project = {};
    Vec3 origin = {};
    Vec3 boundsMin = {};
    Vec3 boundsMax = {};
};

bool DeriveLightVolume(const RenderLight & light, LightVolume & output, std::string & error);

// What classification reads from the rest of the map.
struct LightContext
{
    // Lowercase names that some entity's target* key names.
    std::set<std::string> targeted;
    // The map script, lowercase.
    std::string script;
};

LightContext MakeLightContext(const std::vector<MapEntity> & entities, const std::string & script);

// Whether the script names the entity as $name (any case), so it may change it at run time.
bool ScriptMentions(const LightContext & context, const std::string & name);

// 'material' is the light's shader (null when undeclared, which classifies as Moving: nothing can
// be baked). 'reason' names the rule that decided.
LightClass ClassifyLight(const MapEntity & entity, const RenderLight & light, const MaterialInfo * material, const LightContext & context,
                         std::string & reason);

// A stage that draws, its RGB evaluated once with the light's parms (time 0), times the
// brightness scale (r_lightScale): idRenderBackend's per-stage lightColor.
struct LightStageColor
{
    std::string image;
    bool zeroClamp = false;
    Vec3 color = {};
};

std::vector<LightStageColor> EvaluateLightStages(const MaterialInfo & material, const RenderLight & light, const MaterialTables * tables,
                                                 float lightScale);

// A light image as linear RGB in [0, 1], sampled bilinearly as GL does (texel centres at +0.5),
// outside [0, 1] black ('zero') or the edge texels.
struct LightImage
{
    int width = 0;
    int height = 0;
    std::vector<float> rgb;

    static LightImage FromRgba(const RgbaImage & image);
    Vec3 Sample(float s, float t, bool zero) const;
};

// One light ready to evaluate: its volume, falloff image and drawn stages with their images.
struct LightSource
{
    std::string name;
    LightVolume volume;
    bool castsShadows = true;
    bool ambient = false;
    const LightImage * falloff = nullptr;
    std::vector<LightStageColor> stages;
    std::vector<const LightImage *> images;
};

// The unshadowed light at 'point' on a surface facing 'normal': per stage colour x projection x
// falloff x N.L, N.L from the light origin (1 for ambient lights), as id's interaction computes it
// with the surface's geometric normal.
Vec3 EvaluateLight(const LightSource & light, const Vec3 & point, const Vec3 & normal);

} // namespace assets
