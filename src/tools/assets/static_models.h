// ================================================================================================
// File: static_models.h
// Brief: Merge the brush models of func_static entities that can never change into the area models
// they stand in, in world space, so the world cook draws, lights and traces them like the areas'
// own surfaces. Entities a script, trigger, bind, GUI or skin may change keep their own models.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "assets/light.h"
#include "assets/map_entities.h"
#include "assets/material.h"
#include "assets/proc.h"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace assets
{

struct StaticModelStats
{
    // func_static entities whose model is one of the proc's inline (brush) models.
    std::size_t inlineModels = 0;
    // Models merged whole, and models merged but for the surfaces that stay with the entity.
    std::size_t merged = 0;
    std::size_t partial = 0;
    // Translucent surfaces left in their entity's model because their materials read its parms.
    std::size_t keptSurfaces = 0;
    // Opaque surfaces merged although their materials read parms the entity sets: what the world
    // pass draws (the diffuse) ignores them, a material pass's glow stages will not.
    std::size_t parmSurfaces = 0;
    std::size_t triangles = 0;
    // Triangles whose centroid, corners and entity origin all lie in solid space: dropped.
    std::size_t solidTriangles = 0;
    // Area surfaces added (a merged triangle joins an area surface of its material while it has room).
    std::size_t surfacesAdded = 0;
    // Inline models left out whole, by reason ("parms": every surface stays with the entity).
    std::map<std::string, std::size_t> kept;
};

// Why the func_static 'entity' may change at run time, or null when it never can: bound, hidden at
// spawn, a GUI, a skin, targeted, or named in the script.
const char * StaticModelRefusal(const MapEntity & entity, const LightContext & context);

// Whether 'surface' draws differently under the entity's parms: the entity sets _color or a
// shaderParmN and the surface's material reads parms. 'materials' is keyed by lowercase name.
bool SurfaceReadsEntityParms(const MapEntity & entity, const ProcSurface & surface, const std::map<std::string, MaterialInfo> & materials);

// Whether the surface must stay in the entity's model: translucent and reading its parms (a flare,
// glass, a glow decal), whose whole look is the entity's colour.
bool SurfaceStaysWithEntity(const MapEntity & entity, const ProcSurface & surface, const std::map<std::string, MaterialInfo> & materials);

// Moves every static func_static's inline model into the area models: each triangle, placed by
// the entity's origin and axis, joins the area holding its centroid (else a corner, else the
// origin). SurfaceStaysWithEntity's surfaces stay in its model; a model merged whole leaves
// proc.models, so later passes see its triangles only as area geometry.
bool MergeStaticModels(ProcFile & proc, const std::vector<MapEntity> & entities, const LightContext & context,
                       const std::map<std::string, MaterialInfo> & materials, StaticModelStats & stats, std::string & error);

} // namespace assets
