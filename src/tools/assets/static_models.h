// ================================================================================================
// File: static_models.h
// Brief: Merge the brush models of func_static entities that can never change into the area models
// they stand in, in world space, so the world cook draws, lights and traces them like the areas'
// own surfaces. Entities a script, trigger, bind, GUI or skin may change keep their own models.
// Also the census of the map's mesh models (.lwo and .ase props, doors and movers), imported from
// the retail binary render models, that decides how they join the cook.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "assets/light.h"
#include "assets/map_entities.h"
#include "assets/material.h"
#include "assets/proc.h"

#include <cstddef>
#include <cstdint>
#include <functional>
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
    // Opaque surfaces merged although their materials read parms the entity sets: they join area
    // surfaces of their material and parms (ProcSurface::parms), so the material passes see them.
    std::size_t parmSurfaces = 0;
    // Distinct entity parm sets those surfaces draw with (ProcFile::parmSets).
    std::size_t parmSets = 0;
    std::size_t triangles = 0;
    // Triangles whose centroid, corners and entity origin all lie in solid space: dropped.
    std::size_t solidTriangles = 0;
    // Area surfaces added (a merged triangle joins an area surface of its material while it has room).
    std::size_t surfacesAdded = 0;
    // Inline models left out whole, by reason ("surfaces": every surface stays with the entity).
    std::map<std::string, std::size_t> kept;
    // Mesh models (.lwo, .ase), when the merge reads them: the func_statics naming one, those merged
    // whole and in part, the triangles placed and dropped in solid space, and those left out by
    // reason ("missing": no container holds the file).
    std::size_t meshModels = 0;
    std::size_t meshMerged = 0;
    std::size_t meshPartial = 0;
    std::size_t meshTriangles = 0;
    std::size_t meshSolidTriangles = 0;
    std::map<std::string, std::size_t> meshKept;
    // Entities recorded in ProcFile::mergedEntities.
    std::size_t mergedEntities = 0;
    // Rigid entities drawn at their rest pose (PlaceEntityModels), their triangles, and those left
    // out by reason.
    std::size_t drawnEntities = 0;
    std::size_t drawnTriangles = 0;
    std::map<std::string, std::size_t> drawnKept;
};

// The map's entities with mesh models (a 'model' key naming a .lwo or .ase), each imported from its
// binary render model (ParseBinaryModel) once.
struct MeshModelStats
{
    struct Count
    {
        std::size_t entities = 0;
        // What the entities place: each one's model counted once per entity.
        std::size_t triangles = 0;
        std::size_t vertices = 0;
    };

    // By class.
    std::map<std::string, Count> classes;
    // Distinct model files read, their triangles counted once each, and their bytes; entities whose
    // model no container holds.
    std::size_t models = 0;
    std::size_t modelTriangles = 0;
    std::size_t modelBytes = 0;
    std::size_t missing = 0;
    // func_static meshes that never change (StaticModelRefusal is null): the candidates to merge into
    // the areas like the inline models. The rest, by StaticModelRefusal's reason.
    Count staticMeshes;
    std::map<std::string, Count> kept;
    // Of the static meshes' triangles: those of translucent materials, of materials reading the
    // entity's parms, of materials the scan does not declare, and those whose centroid, corners and
    // entity origin all lie in solid space.
    std::size_t translucentTriangles = 0;
    std::size_t parmTriangles = 0;
    std::size_t undeclaredTriangles = 0;
    std::size_t solidTriangles = 0;
    // The static meshes' triangles by the area MergeStaticModels' placement would give each one.
    std::vector<std::size_t> areaTriangles;
    // The model with the most triangles a static entity places.
    std::string largest;
    std::size_t largestTriangles = 0;
};

// The binary render model a mesh model key names ("models/x/y.lwo" -> generated/rendermodels/
// models/x/y.blwo), or empty when the key names no .lwo or .ase.
std::string GeneratedModelName(const std::string & model);

// Reads a container file: false when it is missing ('error' empty) or unreadable.
using ResourceReader = std::function<bool(const std::string & name, std::vector<std::uint8_t> & bytes, std::string & error)>;

// Counts the map's mesh models without changing 'proc'; false when a model file is malformed or
// names a different source than its entity.
bool CensusMeshModels(const ProcFile & proc, const std::vector<MapEntity> & entities, const LightContext & context,
                      const std::map<std::string, MaterialInfo> & materials, const ResourceReader & read, MeshModelStats & stats,
                      std::string & error);

// Why the func_static 'entity' may change at run time, or null when it never can: bound, hidden at
// spawn, targeted, or named in the script. A GUI or a skin does not keep it out: its GUI surfaces stay
// with it (SurfaceStaysWithEntity), and a skin's materials replace the model's.

// id's canonical declaration name (idDeclManagerLocal::MakeNameCanonical): lowercase, '/' for '\\',
// the text from the last '.' dropped. Skins are found by it.
std::string CanonicalDeclName(const std::string & name);
const char * StaticModelRefusal(const MapEntity & entity, const LightContext & context);

// The entity's shader parms as id spawns it (idGameEdit::ParseSpawnArgsToRenderEntity): _color in
// parms 0-2, then shaderParm3 (default 1) to shaderParm11 (default 0). Parm 4 is the spawn time,
// zero at map start, and none is randomised.
ShaderParms EntityShaderParms(const MapEntity & entity);

// Whether 'surface' draws differently under the entity's parms: the entity sets _color or a
// shaderParmN and the surface's material reads parms. 'materials' is keyed by lowercase name.
bool SurfaceReadsEntityParms(const MapEntity & entity, const ProcSurface & surface, const std::map<std::string, MaterialInfo> & materials);

// Whether the surface must stay in the entity's model: a GUI surface, which shows the entity's GUI, or
// one translucent and reading its parms (a flare, glass, a glow decal), whose whole look is the
// entity's colour.
bool SurfaceStaysWithEntity(const MapEntity & entity, const ProcSurface & surface, const std::map<std::string, MaterialInfo> & materials);

// Moves every static func_static's inline model into the area models: each triangle, placed by
// the entity's origin and axis, joins the area holding its centroid (else a corner, else the
// origin). SurfaceStaysWithEntity's surfaces stay in its model; a model merged whole leaves
// proc.models, so later passes see its triangles only as area geometry. A merged surface whose
// material reads the entity's parms keeps them (proc.parmSets). With 'readMesh', static func_statics
// naming a mesh model (.lwo, .ase) merge the same way from their binary render models, into
// surfaces of their own lit per vertex (ProcSurface::vertexLit). An entity naming a skin merges with
// the skin's materials ('skins', keyed by CanonicalDeclName), and is kept when no skin declares it. Every entity whose model the areas
// now draw, whole or in part, is listed in proc.mergedEntities.
bool MergeStaticModels(ProcFile & proc, const std::vector<MapEntity> & entities, const LightContext & context,
                       const std::map<std::string, MaterialInfo> & materials, StaticModelStats & stats, std::string & error,
                       const ResourceReader * readMesh = nullptr, const std::map<std::string, Skin> * skins = nullptr);

// Places the models of the rigid entities the merge left, which the game may move (doors, movers,
// lights and items with models, and func_statics kept for a bind, a target or the script), whole at
// their rest pose into the area holding their origin, in surfaces of their own lit per vertex, and
// lists them in proc.mergedEntities as drawn. Hidden entities, triggers and other classes are left
// out; so are GUI surfaces and translucent surfaces their parms colour (SurfaceStaysWithEntity).
// Call after MergeStaticModels, with the same inputs.
bool PlaceEntityModels(ProcFile & proc, const std::vector<MapEntity> & entities, const std::map<std::string, MaterialInfo> & materials,
                       StaticModelStats & stats, std::string & error, const ResourceReader * readMesh, const std::map<std::string, Skin> * skins);

} // namespace assets
