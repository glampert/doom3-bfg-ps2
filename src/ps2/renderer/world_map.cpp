// ================================================================================================
// File: world_map.cpp
// Brief: Build id's render world from a cooked D3WD in place of its .proc: the areas, portals and
// nodes the portal flow, area queries and traces use, and one _areaN model per area whose surfaces
// borrow the file's s16 position and u16 index mirror instead of holding idDrawVerts.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/dma.h"
#include "ps2/renderer/render_system.h"
#include "ps2/renderer/world.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <idlib/precompiled.h>
#include <renderer/tr_local.h>

// Native world declarations depend on frontend types from tr_local.h.
#include <renderer/Model_local.h>
#include <renderer/RenderWorld_local.h>

namespace
{

using ps2::renderer::world::Level;
using ps2::renderer::world::Sections;

// Reads maps/<name>.d3wd whole into a DMA-ready allocation. 'found' tells a missing file from a
// refused one.
PS2_COLD_FUNC Level * ReadLevel(const char * path, bool & found)
{
    found = false;
    idFile * file = fileSystem->OpenFileRead(path);

    if (file == nullptr)
    {
        return nullptr;
    }

    found = true;
    const int length = file->Length();

    if (length <= 0 || static_cast<u32>(length) > ps2::renderer::world::kMaxFileBytes) [[unlikely]]
    {
        fileSystem->CloseFile(file);
        ps2::Log(ps2::LogLevel::Error, "unusable world file size in %s\n", path);
        return nullptr;
    }

    const u32 bytes = static_cast<u32>(length);
    void * storage = ps2::system::heap::TryAlloc(bytes, TAG_RENDER, ps2::renderer::world::kPacketAlignment);

    if (storage == nullptr) [[unlikely]]
    {
        fileSystem->CloseFile(file);
        ps2::Log(ps2::LogLevel::Error, "no memory for world %s (%u bytes)\n", path, bytes);
        return nullptr;
    }

    const bool read = file->Read(storage, length) == length;
    fileSystem->CloseFile(file);

    if (!read) [[unlikely]]
    {
        ps2::system::heap::Free(storage);
        ps2::Log(ps2::LogLevel::Error, "world %s refused: read failed\n", path);
        return nullptr;
    }

    auto * level = new (TAG_RENDER) Level;

    if (!ps2::renderer::world::Adopt(storage, bytes, path, *level)) [[unlikely]]
    {
        delete level;
        return nullptr;
    }

    return level;
}

PS2_COLD_FUNC idBounds Bounds(const float (&min)[3], const float (&max)[3])
{
    return idBounds(idVec3(min[0], min[1], min[2]), idVec3(max[0], max[1], max[2]));
}

// One area's model: a surface per cooked surface, its triangles naming the file's mirror (the
// indexes are referenced, so freeing the model leaves them), and the area's bounds.
PS2_COLD_FUNC idRenderModel * AreaModel(const Sections & sections, u32 area)
{
    const auto & record = sections.areas[area];
    idRenderModel * model = renderModelManager->AllocModel();
    auto * staticModel = ps2::DynCast<idRenderModelStatic *>(model);

    if (staticModel == nullptr) [[unlikely]]
    {
        ps2::FatalError("the model manager did not allocate a static model for a world area");
    }

    model->InitEmpty(va("_area%u", area));

    for (u32 index = record.firstSurface; index < record.firstSurface + record.surfaceCount; ++index)
    {
        const auto & surface = sections.surfaces[index];
        const auto & material = sections.materials[surface.material];

        srfTriangles_t * tri = R_AllocStaticTriSurf();
        tri->bounds = Bounds(surface.boundsMin, surface.boundsMax);
        tri->numVerts = static_cast<int>(surface.vertexCount);
        tri->numIndexes = static_cast<int>(surface.indexCount);
        tri->indexes = const_cast<triIndex_t *>(sections.mirrorIndices + surface.firstIndex);
        tri->referencedIndexes = true;
        tri->ps2Positions = sections.mirrorPositions + 3U * surface.firstVertex;
        tri->ps2PositionOffset = idVec3(record.mirrorOffset[0], record.mirrorOffset[1], record.mirrorOffset[2]);
        tri->ps2PositionScale = record.mirrorScale;

        modelSurface_t entry;
        entry.id = 0;
        entry.shader = declManager->FindMaterial(sections.strings + material.nameOffset);
        entry.geometry = tri;
        model->AddSurface(entry);
    }

    // AddSurface grows the bounds InitEmpty zeroed, which would take in the origin.
    staticModel->bounds = Bounds(record.boundsMin, record.boundsMax);
    return model;
}

} // namespace

PS2_COLD_FUNC bool idRenderWorldLocal::InitFromCookedMap(const char * name, bool & found)
{
    PS2_AssertMsg(cookedLevel == nullptr && portalAreas == nullptr && areaNodes == nullptr && localModels.Num() == 0,
                  "cooked map loaded over a live world");

    idStrStatic<MAX_OSPATH> path = name;
    path.SetFileExtension("d3wd");

    Level * level = ReadLevel(path.c_str(), found);

    if (level == nullptr)
    {
        return false;
    }

    const Sections & sections = level->sections;

    // Areas, each with its portal list and empty reference lists, as ParseInterAreaPortals builds them.
    numPortalAreas = static_cast<int>(sections.areaCount);
    portalAreas = static_cast<portalArea_t *>(R_ClearedStaticAlloc(numPortalAreas * static_cast<int>(sizeof(portalAreas[0]))));
    areaScreenRect = static_cast<idScreenRect *>(R_ClearedStaticAlloc(numPortalAreas * static_cast<int>(sizeof(idScreenRect))));
    SetupAreaRefs();

    numInterAreaPortals = static_cast<int>(sections.portalCount);
    doublePortals = static_cast<doublePortal_t *>(R_ClearedStaticAlloc(numInterAreaPortals * static_cast<int>(sizeof(doublePortals[0]))));

    for (int index = 0; index < numInterAreaPortals; ++index)
    {
        const auto & record = sections.portals[index];
        const int a1 = static_cast<int>(record.areas[0]);
        const int a2 = static_cast<int>(record.areas[1]);
        const int points = static_cast<int>(record.pointCount);

        auto * winding = new (TAG_RENDER_WINDING) idWinding(points);
        winding->SetNumPoints(points);

        for (int point = 0; point < points; ++point)
        {
            const float * xyz = sections.portalPoints + 3U * (record.firstPoint + static_cast<u32>(point));
            (*winding)[point] = idVec5(idVec3(xyz[0], xyz[1], xyz[2]), idVec2(0.0f, 0.0f));
        }

        // The portal into a2 belongs to a1, and its reverse to a2.
        auto * forward = static_cast<portal_t *>(R_ClearedStaticAlloc(static_cast<int>(sizeof(portal_t))));
        forward->intoArea = a2;
        forward->doublePortal = &doublePortals[index];
        forward->w = winding;
        forward->w->GetPlane(forward->plane);
        forward->next = portalAreas[a1].portals;
        portalAreas[a1].portals = forward;
        doublePortals[index].portals[0] = forward;

        auto * reverse = static_cast<portal_t *>(R_ClearedStaticAlloc(static_cast<int>(sizeof(portal_t))));
        reverse->intoArea = a1;
        reverse->doublePortal = &doublePortals[index];
        reverse->w = winding->Reverse();
        reverse->w->GetPlane(reverse->plane);
        reverse->next = portalAreas[a2].portals;
        portalAreas[a2].portals = reverse;
        doublePortals[index].portals[1] = reverse;
    }

    numAreaNodes = static_cast<int>(sections.nodeCount);
    areaNodes = static_cast<areaNode_t *>(R_ClearedStaticAlloc(numAreaNodes * static_cast<int>(sizeof(areaNodes[0]))));

    for (int index = 0; index < numAreaNodes; ++index)
    {
        const auto & record = sections.nodes[index];
        areaNodes[index].plane = idPlane(record.plane[0], record.plane[1], record.plane[2], record.plane[3]);
        areaNodes[index].children[0] = record.children[0];
        areaNodes[index].children[1] = record.children[1];
    }

    for (u32 area = 0; area < sections.areaCount; ++area)
    {
        idRenderModel * model = AreaModel(sections, area);
        renderModelManager->AddModel(model);
        localModels.Append(model);
    }

    cookedLevel = level;
    return true;
}

PS2_COLD_FUNC void idRenderWorldLocal::FreeCookedMap()
{
    if (cookedLevel == nullptr)
    {
        return;
    }

    // The display list REFs the packets in place: no chain may still read them, and no open frame
    // may have queued them.
    PS2_AssertMsg(!ps2::renderer::FrameStarted(), "cooked world freed inside a renderer frame");

    if (ps2::renderer::dma::IsInitialized())
    {
        ps2::renderer::dma::WaitIdle();
    }

    ps2::renderer::world::Unload(*cookedLevel);
    delete cookedLevel;
    cookedLevel = nullptr;
}
