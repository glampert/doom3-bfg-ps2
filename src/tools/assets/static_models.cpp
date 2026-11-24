// ================================================================================================
// File: static_models.cpp
// Brief: Classify func_static brush models and merge the static ones into the area models.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/static_models.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <set>
#include <string>

namespace assets
{
namespace
{

using Vec3f = std::array<float, 3>;
using Axis = std::array<Vec3f, 3>;

// A surface keeps 16-bit indices on the PS2 (proc.h).
static constexpr std::size_t kMaxSurfaceVertices = 65535;

static std::string Lower(std::string text)
{
    for (char & character : text)
    {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    return text;
}

static Vec3f Rotate(const Axis & axis, const Vec3f & local)
{
    Vec3f world = {};

    for (std::size_t component = 0; component < 3; ++component)
    {
        world[component] = local[0] * axis[0][component] + local[1] * axis[1][component] + local[2] * axis[2][component];
    }

    return world;
}

// Whether the entity sets any of the parms a material may read: _color (parms 0-2) or shaderParmN.
static bool SetsParms(const MapEntity & entity)
{
    Vec3f color = { 1.0f, 1.0f, 1.0f };

    if (entity.Vector("_color", color) && (color[0] != 1.0f || color[1] != 1.0f || color[2] != 1.0f))
    {
        return true;
    }

    for (const auto & [key, value] : entity.keys)
    {
        if (key.rfind("shaderparm", 0) == 0)
        {
            return true;
        }
    }

    return false;
}

static bool ReadsParms(const MaterialInfo & material)
{
    for (const MaterialStage & stage : material.stages)
    {
        unsigned reads = ExpressionReads(stage.condition);

        for (const MaterialExpression & channel : stage.color)
        {
            reads |= ExpressionReads(channel);
        }

        if ((reads & kExpressionReadsParms) != 0)
        {
            return true;
        }
    }

    return false;
}

} // namespace

ShaderParms EntityShaderParms(const MapEntity & entity)
{
    ShaderParms parms = {};
    Vec3f color = { 1.0f, 1.0f, 1.0f };
    (void)entity.Vector("_color", color);
    parms[0] = color[0];
    parms[1] = color[1];
    parms[2] = color[2];

    for (std::size_t index = 3; index < parms.size(); ++index)
    {
        parms[index] = entity.Float("shaderparm" + std::to_string(index), index == 3 ? 1.0f : 0.0f);
    }

    return parms;
}

const char * StaticModelRefusal(const MapEntity & entity, const LightContext & context)
{
    const std::string name = Lower(entity.String("name"));

    if (entity.Find("bind") != nullptr || entity.Find("bindtojoint") != nullptr || entity.Find("bindorientated") != nullptr)
    {
        return "bound";
    }

    if (entity.Bool("hide", false))
    {
        return "hidden";
    }

    if (entity.Find("gui") != nullptr || entity.Find("gui2") != nullptr || entity.Find("gui3") != nullptr)
    {
        return "gui";
    }

    if (entity.Find("skin") != nullptr)
    {
        return "skin";
    }

    if (context.targeted.count(name) != 0)
    {
        return "targeted";
    }

    if (ScriptMentions(context, name))
    {
        return "scripted";
    }

    return nullptr;
}

bool SurfaceReadsEntityParms(const MapEntity & entity, const ProcSurface & surface, const std::map<std::string, MaterialInfo> & materials)
{
    if (!SetsParms(entity))
    {
        return false;
    }

    const auto found = materials.find(Lower(surface.material));
    return found != materials.end() && ReadsParms(found->second);
}

bool SurfaceStaysWithEntity(const MapEntity & entity, const ProcSurface & surface, const std::map<std::string, MaterialInfo> & materials)
{
    const auto found = materials.find(Lower(surface.material));
    return SurfaceReadsEntityParms(entity, surface, materials) && found->second.translucent;
}

bool MergeStaticModels(ProcFile & proc, const std::vector<MapEntity> & entities, const LightContext & context,
                       const std::map<std::string, MaterialInfo> & materials, StaticModelStats & stats, std::string & error)
{
    stats = StaticModelStats();
    std::map<std::string, std::size_t> models;
    std::vector<std::size_t> areaModels(static_cast<std::size_t>(proc.areaCount), proc.models.size());

    for (std::size_t index = 0; index < proc.models.size(); ++index)
    {
        const std::string name = Lower(proc.models[index].name);

        if (name.rfind("_area", 0) == 0)
        {
            char * end = nullptr;
            const long area = std::strtol(name.c_str() + 5, &end, 10);

            if (end != name.c_str() + 5 && *end == '\0' && area >= 0 && area < proc.areaCount)
            {
                areaModels[static_cast<std::size_t>(area)] = index;
            }

            continue;
        }

        models.emplace(name, index);
    }

    if (std::find(areaModels.begin(), areaModels.end(), proc.models.size()) != areaModels.end())
    {
        error = "an area has no model";
        return false;
    }

    std::set<std::size_t> merged;

    for (const MapEntity & entity : entities)
    {
        const auto found = models.find(Lower(entity.String("model")));

        if (Lower(entity.String("classname")) != "func_static" || found == models.end())
        {
            continue;
        }

        ++stats.inlineModels;
        ProcModel & model = proc.models[found->second];

        if (const char * reason = StaticModelRefusal(entity, context))
        {
            ++stats.kept[reason];
            continue;
        }

        if (merged.count(found->second) != 0)
        {
            // Two entities naming one inline model: id would draw it twice; merge it once.
            continue;
        }

        // Translucent surfaces coloured by the entity's parms (flares, glass, glow decals) stay in
        // its model, for the entity to draw with them; the rest join the areas.
        std::vector<ProcSurface> kept;

        Vec3f origin = {};
        Axis axis = {};
        entity.Placement(origin, axis);
        const int originArea = PointInArea(proc, origin);
        const ShaderParms entityParms = EntityShaderParms(entity);

        for (const ProcSurface & surface : model.surfaces)
        {
            if (SurfaceStaysWithEntity(entity, surface, materials))
            {
                kept.push_back(surface);
                continue;
            }

            // A surface whose material reads the parms keeps the entity's; the rest draw as the world's.
            std::uint32_t parms = 0;

            if (SurfaceReadsEntityParms(entity, surface, materials))
            {
                ++stats.parmSurfaces;
                const auto known = std::find(proc.parmSets.begin(), proc.parmSets.end(), entityParms);
                parms = static_cast<std::uint32_t>(known - proc.parmSets.begin()) + 1U;

                if (known == proc.parmSets.end())
                {
                    proc.parmSets.push_back(entityParms);
                }
            }

            std::vector<ProcVertex> world = surface.vertices;

            for (ProcVertex & vertex : world)
            {
                const Vec3f position = Rotate(axis, vertex.position);

                for (std::size_t component = 0; component < 3; ++component)
                {
                    vertex.position[component] = origin[component] + position[component];
                }

                vertex.normal = Rotate(axis, vertex.normal);
            }

            // Each triangle's area, then each area's share of the surface.
            std::map<int, std::vector<std::size_t> > byArea;

            for (std::size_t first = 0; first + 2 < surface.indices.size(); first += 3)
            {
                Vec3f centroid = {};

                for (std::size_t corner = 0; corner < 3; ++corner)
                {
                    for (std::size_t component = 0; component < 3; ++component)
                    {
                        centroid[component] += world[surface.indices[first + corner]].position[component] / 3.0f;
                    }
                }

                int area = PointInArea(proc, centroid);

                for (std::size_t corner = 0; corner < 3 && area < 0; ++corner)
                {
                    area = PointInArea(proc, world[surface.indices[first + corner]].position);
                }

                area = area >= 0 ? area : originArea;

                if (area < 0)
                {
                    ++stats.solidTriangles;
                    continue;
                }

                byArea[area].push_back(first);
                ++stats.triangles;
            }

            for (const auto & [area, triangles] : byArea)
            {
                ProcModel & target = proc.models[areaModels[static_cast<std::size_t>(area)]];
                // At most three new vertices a triangle.
                const std::size_t needed = std::min(world.size(), 3 * triangles.size());
                ProcSurface * into = nullptr;

                for (ProcSurface & candidate : target.surfaces)
                {
                    if (candidate.material == surface.material && candidate.parms == parms && candidate.vertices.size() + needed <= kMaxSurfaceVertices)
                    {
                        into = &candidate;
                        break;
                    }
                }

                if (into == nullptr)
                {
                    target.surfaces.push_back(ProcSurface{ surface.material, {}, {}, parms });
                    into = &target.surfaces.back();
                    ++stats.surfacesAdded;
                }

                std::vector<std::uint32_t> remap(world.size(), std::numeric_limits<std::uint32_t>::max());

                for (const std::size_t first : triangles)
                {
                    for (std::size_t corner = 0; corner < 3; ++corner)
                    {
                        const std::uint32_t source = surface.indices[first + corner];

                        if (remap[source] == std::numeric_limits<std::uint32_t>::max())
                        {
                            remap[source] = static_cast<std::uint32_t>(into->vertices.size());
                            into->vertices.push_back(world[source]);
                        }

                        into->indices.push_back(remap[source]);
                    }
                }
            }
        }

        if (kept.empty())
        {
            merged.insert(found->second);
            ++stats.merged;
            continue;
        }

        stats.keptSurfaces += kept.size();

        if (kept.size() == model.surfaces.size())
        {
            ++stats.kept["parms"];
            continue;
        }

        model.surfaces = std::move(kept);
        ++stats.partial;
    }

    stats.parmSets = proc.parmSets.size();

    // Merged models leave the file, from the back so indices stay valid.
    for (auto index = merged.rbegin(); index != merged.rend(); ++index)
    {
        proc.models.erase(proc.models.begin() + static_cast<std::ptrdiff_t>(*index));
    }

    return true;
}

std::string GeneratedModelName(const std::string & model)
{
    std::string name = Lower(model);

    for (char & character : name)
    {
        character = character == '\\' ? '/' : character;
    }

    const std::size_t dot = name.rfind('.');

    if (dot == std::string::npos || (name.compare(dot, std::string::npos, ".lwo") != 0 && name.compare(dot, std::string::npos, ".ase") != 0))
    {
        return std::string();
    }

    return "generated/rendermodels/" + name.substr(0, dot) + ".b" + name.substr(dot + 1);
}

bool CensusMeshModels(const ProcFile & proc, const std::vector<MapEntity> & entities, const LightContext & context,
                      const std::map<std::string, MaterialInfo> & materials, const ResourceReader & read, MeshModelStats & stats,
                      std::string & error)
{
    stats = MeshModelStats();
    stats.areaTriangles.assign(static_cast<std::size_t>(proc.areaCount), 0);
    std::map<std::string, ProcModel> models;
    std::set<std::string> missing;

    for (const MapEntity & entity : entities)
    {
        const std::string key = entity.String("model");
        const std::string name = GeneratedModelName(key);

        if (name.empty())
        {
            continue;
        }

        auto found = models.find(name);

        if (found == models.end())
        {
            if (missing.count(name) != 0)
            {
                ++stats.missing;
                continue;
            }

            std::vector<std::uint8_t> bytes;
            std::string readError;

            if (!read(name, bytes, readError))
            {
                if (!readError.empty())
                {
                    error = name + ": " + readError;
                    return false;
                }

                missing.insert(name);
                ++stats.missing;
                continue;
            }

            ProcModel model;

            if (!ParseBinaryModel(bytes, model, error))
            {
                error = name + ": " + error;
                return false;
            }

            if (Lower(model.name) != Lower(key))
            {
                error = name + " names itself " + model.name + ", not " + key;
                return false;
            }

            std::size_t triangles = 0;

            for (const ProcSurface & surface : model.surfaces)
            {
                triangles += surface.indices.size() / 3;
            }

            ++stats.models;
            stats.modelTriangles += triangles;
            stats.modelBytes += bytes.size();
            found = models.emplace(name, std::move(model)).first;
        }

        const ProcModel & model = found->second;
        MeshModelStats::Count placed;
        placed.entities = 1;

        for (const ProcSurface & surface : model.surfaces)
        {
            placed.triangles += surface.indices.size() / 3;
            placed.vertices += surface.vertices.size();
        }

        const std::string classname = Lower(entity.String("classname"));
        MeshModelStats::Count & byClass = stats.classes[classname];
        byClass.entities += 1;
        byClass.triangles += placed.triangles;
        byClass.vertices += placed.vertices;

        if (classname != "func_static")
        {
            continue;
        }

        if (const char * reason = StaticModelRefusal(entity, context))
        {
            MeshModelStats::Count & refused = stats.kept[reason];
            refused.entities += 1;
            refused.triangles += placed.triangles;
            refused.vertices += placed.vertices;
            continue;
        }

        stats.staticMeshes.entities += 1;
        stats.staticMeshes.triangles += placed.triangles;
        stats.staticMeshes.vertices += placed.vertices;

        if (placed.triangles > stats.largestTriangles)
        {
            stats.largestTriangles = placed.triangles;
            stats.largest = key;
        }

        // Where MergeStaticModels would put each triangle: the area holding its centroid, else a
        // corner, else the entity's origin.
        Vec3f origin = {};
        Axis axis = {};
        entity.Placement(origin, axis);
        const int originArea = PointInArea(proc, origin);

        for (const ProcSurface & surface : model.surfaces)
        {
            const std::size_t triangles = surface.indices.size() / 3;
            const auto material = materials.find(Lower(surface.material));

            if (material == materials.end())
            {
                stats.undeclaredTriangles += triangles;
            }
            else if (material->second.translucent)
            {
                stats.translucentTriangles += triangles;
            }

            if (SurfaceReadsEntityParms(entity, surface, materials))
            {
                stats.parmTriangles += triangles;
            }

            for (std::size_t first = 0; first + 2 < surface.indices.size(); first += 3)
            {
                Vec3f corners[3] = {};
                Vec3f centroid = {};

                for (std::size_t corner = 0; corner < 3; ++corner)
                {
                    const Vec3f local = Rotate(axis, surface.vertices[surface.indices[first + corner]].position);

                    for (std::size_t component = 0; component < 3; ++component)
                    {
                        corners[corner][component] = origin[component] + local[component];
                        centroid[component] += corners[corner][component] / 3.0f;
                    }
                }

                int area = PointInArea(proc, centroid);

                for (std::size_t corner = 0; corner < 3 && area < 0; ++corner)
                {
                    area = PointInArea(proc, corners[corner]);
                }

                area = area >= 0 ? area : originArea;

                if (area < 0)
                {
                    ++stats.solidTriangles;
                    continue;
                }

                ++stats.areaTriangles[static_cast<std::size_t>(area)];
            }
        }
    }

    return true;
}

} // namespace assets
