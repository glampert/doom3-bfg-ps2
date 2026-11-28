// ================================================================================================
// File: map_data.cpp
// Brief: ps2_mapdata MAP loads a map's game data step by step as idGameLocal::LoadMap and
// idCommonLocal::ExecuteMapChange do (the map's resource container, its cooked entities, the
// collision models from the retail .bcm, every cooked AAS type), and
// prints the allocation ledgers after each step, the step's peak included; then it frees all of it
// in MapShutdown's order. ps2_verifyAAS MAP loads each AAS type both from its cooked file and
// through id's text parse, and compares every table. Debug builds only: they measure and check,
// and the game's own load replaces them.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/game/map_data.h"

#if PS2_D3BFG_DEBUG

#include "ps2/system/heap.h"
#include "ps2/system/lifecycle.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>

// Kept after precompiled.h, which declares the AAS file's base class.
#include <aas/AASFile_local.h>

#include <cstring>

namespace
{

// The collision file's header: a big-endian model count, the map's name as a little-endian length
// and its characters, then the big-endian CRC (idCollisionModelManagerLocal::LoadCollisionModelFile).
static constexpr int kCollisionHeaderBytes = 256;

// At most this many AAS types (aas_types' "type" keys); the retail declaration names a handful.
static constexpr int kMaxAasTypes = 16;

// Ends a step: the ledgers under mapdata-STEP with the step's own peaks, which the next step resets.
PS2_COLD_FUNC void Checkpoint(const char * step, int startMs)
{
    const int elapsed = Sys_Milliseconds() - startMs;
    char label[64] = {};
    idStr::snPrintf(label, static_cast<int>(sizeof(label)), "mapdata-%s", step);

    ps2::system::lifecycle::PrintMemory(label);
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] MAPDATA_STEP %s ms=%d\n", step, elapsed);
    ps2::system::heap::ResetPeaks();
}

// The CRC the map's .bcm was built against, read from its header alone (opening a container entry
// would read the whole 5 MB file); false when the file is missing or its header is malformed.
PS2_COLD_FUNC bool ReadCollisionCrc(const char * mapFileName, unsigned int & crc)
{
    idStrStatic<MAX_OSPATH> path = mapFileName;
    path.Insert("generated/", 0);
    path.SetFileExtension("bcm");

    byte header[kCollisionHeaderBytes] = {};
    if (fileSystem->GetFileLength(path) < kCollisionHeaderBytes ||
        !fileSystem->ReadFileRange(path, 0, header, kCollisionHeaderBytes)) [[unlikely]]
    {
        return false;
    }

    const int nameLength = header[4] | (header[5] << 8) | (header[6] << 16) | (header[7] << 24);
    if (nameLength < 0 || nameLength > kCollisionHeaderBytes - 12) [[unlikely]]
    {
        return false;
    }

    const byte * const big = header + 8 + nameLength;
    crc = (static_cast<unsigned int>(big[0]) << 24) | (static_cast<unsigned int>(big[1]) << 16) |
          (static_cast<unsigned int>(big[2]) << 8) | static_cast<unsigned int>(big[3]);
    return true;
}

// ps2_mapdata MAP: MAP as the map command takes it (game/mars_city1).
PS2_COLD_FUNC void MapData(const idCmdArgs & args)
{
    if (args.Argc() != 2) [[unlikely]]
    {
        common->Printf("usage: ps2_mapdata MAP (as the map command names it, such as game/mars_city1)\n");
        return;
    }

    if (game->IsInGame()) [[unlikely]]
    {
        common->Printf("ps2_mapdata: the game holds a map; it measures only from NOMAP\n");
        return;
    }

    idStrStatic<MAX_OSPATH> mapFileName = "maps/";
    mapFileName += args.Argv(1);
    mapFileName.SetFileExtension("map");

    idStrStatic<MAX_OSPATH> mapName = args.Argv(1);
    mapName.StripPath();

    ps2::system::heap::ResetPeaks();
    ps2::system::lifecycle::PrintMemory("mapdata-begin");

    // The map's resource container, mounted as ExecuteMapChange's BeginLevelLoad does.
    int start = Sys_Milliseconds();
    fileSystem->BeginLevelLoad(args.Argv(1), nullptr, 0);
    fileSystem->EndLevelLoad();
    Checkpoint("container", start);

    // The map's entities and geometry CRC, from its cooked D3ME (idMapFile::Parse on the PS2).
    start = Sys_Milliseconds();
    idMapFile * const mapFile = new (TAG_GAME) idMapFile;
    if (!mapFile->Parse(mapFileName)) [[unlikely]]
    {
        delete mapFile;
        fileSystem->UnloadMapResources(mapName);
        common->Printf("ps2_mapdata: could not load %s\n", mapFileName.c_str());
        return;
    }

    const unsigned int geometryCrc = mapFile->GetGeometryCRC();
    Checkpoint("entities", start);

    // The collision models, only from the cooked file: a CRC that differs would rebuild them from the
    // brushes and write a .cm and a .bcm into the package.
    unsigned int collisionCrc = 0;
    const bool haveCollision = ReadCollisionCrc(mapFile->GetName(), collisionCrc);
    const bool collision = haveCollision && collisionCrc == geometryCrc;
    if (collision)
    {
        start = Sys_Milliseconds();
        collisionModelManager->LoadMap(mapFile);
        collisionModelManager->Preload(mapName);
        Checkpoint("collision", start);
    }

    // Every AAS type the game allocates (idGameLocal::Init), loaded against the geometry CRC.
    start = Sys_Milliseconds();
    idAASFile * aas[kMaxAasTypes] = {};
    int aasTypes = 0;
    int aasLoaded = 0;
    const idDecl * const typesDecl = declManager->FindType(DECL_ENTITYDEF, "aas_types", false);
    const idDict * const types = typesDecl != nullptr ? &static_cast<const idDeclEntityDef *>(typesDecl)->dict : nullptr;
    for (const idKeyValue * kv = types != nullptr ? types->MatchPrefix("type") : nullptr;
         kv != nullptr && aasTypes < kMaxAasTypes; kv = types->MatchPrefix("type", kv))
    {
        idStrStatic<MAX_OSPATH> aasName = mapFile->GetName();
        aasName.SetFileExtension(kv->GetValue());
        aas[aasTypes] = AASFileManager->LoadAAS(aasName, geometryCrc);
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] MAPDATA_AAS %s loaded=%d\n", aasName.c_str(), aas[aasTypes] != nullptr ? 1 : 0);
        aasLoaded += aas[aasTypes] != nullptr ? 1 : 0;
        ++aasTypes;
    }

    Checkpoint("aas", start);

    ps2::Log(ps2::LogLevel::Info,
             "[D3BFG] MAPDATA map=%s entities=%d geometry_crc=0x%08x collision_crc=0x%08x collision=%d aas_types=%d "
             "aas_loaded=%d\n",
             args.Argv(1), mapFile->GetNumEntities(), geometryCrc, collisionCrc, collision ? 1 : 0, aasTypes, aasLoaded);

    // Freed in MapShutdown's order: the AAS, the collision models, the map, then its container.
    start = Sys_Milliseconds();
    for (int i = 0; i < aasTypes; ++i)
    {
        if (aas[i] != nullptr)
        {
            AASFileManager->FreeAAS(aas[i]);
        }
    }

    const int aasFreed = Sys_Milliseconds();
    collisionModelManager->FreeMap();
    const int collisionFreed = Sys_Milliseconds();
    delete mapFile;
    const int mapFreed = Sys_Milliseconds();
    fileSystem->UnloadMapResources(mapName);
    const int unloaded = Sys_Milliseconds();
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] MAPDATA_FREE aas_ms=%d collision_ms=%d map_ms=%d unload_ms=%d\n", aasFreed - start,
             collisionFreed - aasFreed, mapFreed - collisionFreed, unloaded - mapFreed);
    Checkpoint("end", start);
}

// ------------------------------------------------------------------------------------------------
// ps2_verifyAAS
// ------------------------------------------------------------------------------------------------

// Two loads of one AAS file compared field by field: integers exactly; floats within a relative
// 1e-4, since the cook reads numbers to the nearest float and the EE's lexer is off in the last bits.
struct AasComparison
{
    int intMismatches = 0;
    int floatMismatches = 0;
    int floatDifferences = 0;
    float maxDelta = 0.0f;
    const char * first = "none";

    void Int(long long cooked, long long text, const char * what)
    {
        if (cooked != text)
        {
            first = intMismatches + floatMismatches == 0 ? what : first;
            ++intMismatches;
        }
    }

    void Float(float cooked, float text, const char * what)
    {
        if (std::memcmp(&cooked, &text, sizeof(float)) == 0)
        {
            return;
        }

        ++floatDifferences;
        const float delta = idMath::Fabs(cooked - text);
        const float scale = idMath::Fabs(text) > 1.0f ? idMath::Fabs(text) : 1.0f;
        maxDelta = delta > maxDelta ? delta : maxDelta;

        if (delta > 1e-4f * scale)
        {
            first = intMismatches + floatMismatches == 0 ? what : first;
            ++floatMismatches;
        }
    }

    void Vec(const idVec3 & cooked, const idVec3 & text, const char * what)
    {
        for (int i = 0; i < 3; ++i)
        {
            Float(cooked[i], text[i], what);
        }
    }

    void Bounds(const idBounds & cooked, const idBounds & text, const char * what)
    {
        Vec(cooked[0], text[0], what);
        Vec(cooked[1], text[1], what);
    }
};

PS2_COLD_FUNC void CompareReach(AasComparison & compare, const idReachability * cooked, const idReachability * text)
{
    compare.Int(cooked->travelType, text->travelType, "reach.travelType");
    compare.Int(cooked->toAreaNum, text->toAreaNum, "reach.toAreaNum");
    compare.Int(cooked->fromAreaNum, text->fromAreaNum, "reach.fromAreaNum");
    compare.Vec(cooked->start, text->start, "reach.start");
    compare.Vec(cooked->end, text->end, "reach.end");
    compare.Int(cooked->edgeNum, text->edgeNum, "reach.edgeNum");
    compare.Int(cooked->travelTime, text->travelTime, "reach.travelTime");
    compare.Int(cooked->number, text->number, "reach.number");
    compare.Int(cooked->disableCount, text->disableCount, "reach.disableCount");

    if (cooked->travelType == TFL_SPECIAL && text->travelType == TFL_SPECIAL)
    {
        const idDict & cookedDict = static_cast<const idReachability_Special *>(cooked)->dict;
        const idDict & textDict = static_cast<const idReachability_Special *>(text)->dict;
        compare.Int(cookedDict.GetNumKeyVals(), textDict.GetNumKeyVals(), "reach.dict");

        for (int i = 0; i < cookedDict.GetNumKeyVals() && i < textDict.GetNumKeyVals(); ++i)
        {
            const idKeyValue * const a = cookedDict.GetKeyVal(i);
            const idKeyValue * const b = textDict.GetKeyVal(i);
            compare.Int(a->GetKey().Cmp(b->GetKey()), 0, "reach.dict.key");
            compare.Int(a->GetValue().Cmp(b->GetValue()), 0, "reach.dict.value");
        }
    }
}

PS2_COLD_FUNC void CompareSettings(AasComparison & compare, const idAASSettings & cooked, const idAASSettings & text)
{
    compare.Int(cooked.numBoundingBoxes, text.numBoundingBoxes, "settings.numBoundingBoxes");
    for (int i = 0; i < cooked.numBoundingBoxes && i < text.numBoundingBoxes && i < MAX_AAS_BOUNDING_BOXES; ++i)
    {
        compare.Bounds(cooked.boundingBoxes[i], text.boundingBoxes[i], "settings.boundingBoxes");
    }

    compare.Int(cooked.usePatches, text.usePatches, "settings.usePatches");
    compare.Int(cooked.writeBrushMap, text.writeBrushMap, "settings.writeBrushMap");
    compare.Int(cooked.playerFlood, text.playerFlood, "settings.playerFlood");
    compare.Int(cooked.noOptimize, text.noOptimize, "settings.noOptimize");
    compare.Int(cooked.allowSwimReachabilities, text.allowSwimReachabilities, "settings.allowSwimReachabilities");
    compare.Int(cooked.allowFlyReachabilities, text.allowFlyReachabilities, "settings.allowFlyReachabilities");
    compare.Int(cooked.fileExtension.Cmp(text.fileExtension), 0, "settings.fileExtension");
    compare.Vec(cooked.gravity, text.gravity, "settings.gravity");
    compare.Vec(cooked.gravityDir, text.gravityDir, "settings.gravityDir");
    compare.Vec(cooked.invGravityDir, text.invGravityDir, "settings.invGravityDir");
    compare.Float(cooked.gravityValue, text.gravityValue, "settings.gravityValue");
    compare.Float(cooked.maxStepHeight, text.maxStepHeight, "settings.maxStepHeight");
    compare.Float(cooked.maxBarrierHeight, text.maxBarrierHeight, "settings.maxBarrierHeight");
    compare.Float(cooked.maxWaterJumpHeight, text.maxWaterJumpHeight, "settings.maxWaterJumpHeight");
    compare.Float(cooked.maxFallHeight, text.maxFallHeight, "settings.maxFallHeight");
    compare.Float(cooked.minFloorCos, text.minFloorCos, "settings.minFloorCos");
    compare.Int(cooked.tt_barrierJump, text.tt_barrierJump, "settings.tt_barrierJump");
    compare.Int(cooked.tt_startCrouching, text.tt_startCrouching, "settings.tt_startCrouching");
    compare.Int(cooked.tt_waterJump, text.tt_waterJump, "settings.tt_waterJump");
    compare.Int(cooked.tt_startWalkOffLedge, text.tt_startWalkOffLedge, "settings.tt_startWalkOffLedge");
}

// Every table through idAASFile's getters; the portals' maxAreaTravelTime is the routing's (the
// text parse leaves it unset), so it is not compared. Returns the reachabilities compared.
PS2_COLD_FUNC int CompareAas(AasComparison & compare, idAASFile & cooked, idAASFile & text)
{
    CompareSettings(compare, cooked.GetSettings(), text.GetSettings());

    compare.Int(cooked.GetNumPlanes(), text.GetNumPlanes(), "planes");
    for (int i = 0; i < cooked.GetNumPlanes() && i < text.GetNumPlanes(); ++i)
    {
        compare.Vec(cooked.GetPlane(i).Normal(), text.GetPlane(i).Normal(), "plane.normal");
        compare.Float(cooked.GetPlane(i).Dist(), text.GetPlane(i).Dist(), "plane.dist");
    }

    compare.Int(cooked.GetNumVertices(), text.GetNumVertices(), "vertices");
    for (int i = 0; i < cooked.GetNumVertices() && i < text.GetNumVertices(); ++i)
    {
        compare.Vec(cooked.GetVertex(i), text.GetVertex(i), "vertex");
    }

    compare.Int(cooked.GetNumEdges(), text.GetNumEdges(), "edges");
    for (int i = 0; i < cooked.GetNumEdges() && i < text.GetNumEdges(); ++i)
    {
        compare.Int(cooked.GetEdge(i).vertexNum[0], text.GetEdge(i).vertexNum[0], "edge.vertexNum");
        compare.Int(cooked.GetEdge(i).vertexNum[1], text.GetEdge(i).vertexNum[1], "edge.vertexNum");
    }

    compare.Int(cooked.GetNumEdgeIndexes(), text.GetNumEdgeIndexes(), "edgeIndex");
    for (int i = 0; i < cooked.GetNumEdgeIndexes() && i < text.GetNumEdgeIndexes(); ++i)
    {
        compare.Int(cooked.GetEdgeIndex(i), text.GetEdgeIndex(i), "edgeIndex");
    }

    compare.Int(cooked.GetNumFaces(), text.GetNumFaces(), "faces");
    for (int i = 0; i < cooked.GetNumFaces() && i < text.GetNumFaces(); ++i)
    {
        const aasFace_t & a = cooked.GetFace(i);
        const aasFace_t & b = text.GetFace(i);
        compare.Int(a.planeNum, b.planeNum, "face.planeNum");
        compare.Int(a.flags, b.flags, "face.flags");
        compare.Int(a.numEdges, b.numEdges, "face.numEdges");
        compare.Int(a.firstEdge, b.firstEdge, "face.firstEdge");
        compare.Int(a.areas[0], b.areas[0], "face.areas");
        compare.Int(a.areas[1], b.areas[1], "face.areas");
    }

    compare.Int(cooked.GetNumFaceIndexes(), text.GetNumFaceIndexes(), "faceIndex");
    for (int i = 0; i < cooked.GetNumFaceIndexes() && i < text.GetNumFaceIndexes(); ++i)
    {
        compare.Int(cooked.GetFaceIndex(i), text.GetFaceIndex(i), "faceIndex");
    }

    int reaches = 0;
    compare.Int(cooked.GetNumAreas(), text.GetNumAreas(), "areas");
    for (int i = 0; i < cooked.GetNumAreas() && i < text.GetNumAreas(); ++i)
    {
        const aasArea_t & a = cooked.GetArea(i);
        const aasArea_t & b = text.GetArea(i);
        compare.Int(a.numFaces, b.numFaces, "area.numFaces");
        compare.Int(a.firstFace, b.firstFace, "area.firstFace");
        compare.Bounds(a.bounds, b.bounds, "area.bounds");
        compare.Vec(a.center, b.center, "area.center");
        compare.Int(a.flags, b.flags, "area.flags");
        compare.Int(a.contents, b.contents, "area.contents");
        compare.Int(a.cluster, b.cluster, "area.cluster");
        compare.Int(a.clusterAreaNum, b.clusterAreaNum, "area.clusterAreaNum");
        compare.Int(a.travelFlags, b.travelFlags, "area.travelFlags");

        const idReachability * x = a.reach;
        const idReachability * y = b.reach;
        for (; x != nullptr && y != nullptr; x = x->next, y = y->next, ++reaches)
        {
            CompareReach(compare, x, y);
        }

        compare.Int(x != nullptr, y != nullptr, "area.reach");

        // The reversed lists, by the areas they join.
        x = a.rev_reach;
        y = b.rev_reach;
        for (; x != nullptr && y != nullptr; x = x->rev_next, y = y->rev_next)
        {
            compare.Int(x->fromAreaNum, y->fromAreaNum, "area.rev_reach");
            compare.Int(x->toAreaNum, y->toAreaNum, "area.rev_reach");
        }

        compare.Int(x != nullptr, y != nullptr, "area.rev_reach");
    }

    compare.Int(cooked.GetNumNodes(), text.GetNumNodes(), "nodes");
    for (int i = 0; i < cooked.GetNumNodes() && i < text.GetNumNodes(); ++i)
    {
        compare.Int(cooked.GetNode(i).planeNum, text.GetNode(i).planeNum, "node.planeNum");
        compare.Int(cooked.GetNode(i).children[0], text.GetNode(i).children[0], "node.children");
        compare.Int(cooked.GetNode(i).children[1], text.GetNode(i).children[1], "node.children");
    }

    compare.Int(cooked.GetNumPortals(), text.GetNumPortals(), "portals");
    for (int i = 0; i < cooked.GetNumPortals() && i < text.GetNumPortals(); ++i)
    {
        const aasPortal_t & a = cooked.GetPortal(i);
        const aasPortal_t & b = text.GetPortal(i);
        compare.Int(a.areaNum, b.areaNum, "portal.areaNum");
        compare.Int(a.clusters[0], b.clusters[0], "portal.clusters");
        compare.Int(a.clusters[1], b.clusters[1], "portal.clusters");
        compare.Int(a.clusterAreaNum[0], b.clusterAreaNum[0], "portal.clusterAreaNum");
        compare.Int(a.clusterAreaNum[1], b.clusterAreaNum[1], "portal.clusterAreaNum");
    }

    compare.Int(cooked.GetNumPortalIndexes(), text.GetNumPortalIndexes(), "portalIndex");
    for (int i = 0; i < cooked.GetNumPortalIndexes() && i < text.GetNumPortalIndexes(); ++i)
    {
        compare.Int(cooked.GetPortalIndex(i), text.GetPortalIndex(i), "portalIndex");
    }

    compare.Int(cooked.GetNumClusters(), text.GetNumClusters(), "clusters");
    for (int i = 0; i < cooked.GetNumClusters() && i < text.GetNumClusters(); ++i)
    {
        const aasCluster_t & a = cooked.GetCluster(i);
        const aasCluster_t & b = text.GetCluster(i);
        compare.Int(a.numAreas, b.numAreas, "cluster.numAreas");
        compare.Int(a.numReachableAreas, b.numReachableAreas, "cluster.numReachableAreas");
        compare.Int(a.numPortals, b.numPortals, "cluster.numPortals");
        compare.Int(a.firstPortal, b.firstPortal, "cluster.firstPortal");
    }

    return reaches;
}

// ps2_verifyAAS MAP: every AAS type with a cooked file, loaded both ways and compared.
PS2_COLD_FUNC void VerifyAas(const idCmdArgs & args)
{
    if (args.Argc() != 2) [[unlikely]]
    {
        common->Printf("usage: ps2_verifyAAS MAP (as the map command names it, such as game/mars_city1)\n");
        return;
    }

    if (game->IsInGame()) [[unlikely]]
    {
        common->Printf("ps2_verifyAAS: the game holds a map; it compares only from NOMAP\n");
        return;
    }

    idStrStatic<MAX_OSPATH> mapName = args.Argv(1);
    mapName.StripPath();
    fileSystem->BeginLevelLoad(args.Argv(1), nullptr, 0);
    fileSystem->EndLevelLoad();

    int files = 0;
    int failures = 0;
    const idDecl * const typesDecl = declManager->FindType(DECL_ENTITYDEF, "aas_types", false);
    const idDict * const types = typesDecl != nullptr ? &static_cast<const idDeclEntityDef *>(typesDecl)->dict : nullptr;
    for (const idKeyValue * kv = types != nullptr ? types->MatchPrefix("type") : nullptr; kv != nullptr; kv = types->MatchPrefix("type", kv))
    {
        idStrStatic<MAX_OSPATH> aasName = "maps/";
        aasName += args.Argv(1);
        aasName.SetFileExtension(kv->GetValue());

        idStrStatic<MAX_OSPATH> cookedName = aasName;
        cookedName += ".d3as";
        if (fileSystem->GetFileLength(cookedName) < 0)
        {
            continue;
        }

        ++files;
        idAASFileLocal * const cooked = new (TAG_AAS) idAASFileLocal;
        idAASFileLocal * const text = new (TAG_AAS) idAASFileLocal;
        int start = Sys_Milliseconds();
        const bool cookedLoaded = cooked->Load(aasName.c_str(), 0);
        const int cookedMs = Sys_Milliseconds() - start;
        start = Sys_Milliseconds();
        const bool textLoaded = text->LoadText(aasName.c_str(), 0);
        const int textMs = Sys_Milliseconds() - start;

        AasComparison compare;
        int reaches = 0;
        if (cookedLoaded && textLoaded)
        {
            reaches = CompareAas(compare, *cooked, *text);
        }

        const bool passed = cookedLoaded && textLoaded && compare.intMismatches == 0 && compare.floatMismatches == 0;
        failures += passed ? 0 : 1;
        ps2::Log(ps2::LogLevel::Info,
                 "[D3BFG] AAS_VERIFY file=%s loaded=%d/%d areas=%d reaches=%d planes=%d cooked_ms=%d text_ms=%d int_mismatches=%d "
                 "float_mismatches=%d float_differences=%d max_delta=%.6f first=%s\n",
                 aasName.c_str(), cookedLoaded ? 1 : 0, textLoaded ? 1 : 0, cooked->GetNumAreas(), reaches, cooked->GetNumPlanes(), cookedMs,
                 textMs, compare.intMismatches, compare.floatMismatches, compare.floatDifferences, static_cast<double>(compare.maxDelta),
                 compare.first);

        delete text;
        delete cooked;
    }

    fileSystem->UnloadMapResources(mapName);
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] AAS_VERIFY map=%s files=%d failures=%d\n", args.Argv(1), files, failures);
}

} // namespace

namespace ps2::game
{

PS2_COLD_FUNC void InitMapData()
{
    cmdSystem->AddCommand("ps2_mapdata", MapData, CMD_FL_SYSTEM,
                          CMD_DESC("load a map's game data step by step with memory checkpoints, then free it"));
    cmdSystem->AddCommand("ps2_verifyAAS", VerifyAas, CMD_FL_SYSTEM,
                          CMD_DESC("load a map's cooked AAS files and their text, and compare every table"));
}

} // namespace ps2::game

#endif // PS2_D3BFG_DEBUG
