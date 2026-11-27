// ================================================================================================
// File: map_data.cpp
// Brief: ps2_mapdata MAP loads a map's game data step by step as idGameLocal::LoadMap and
// idCommonLocal::ExecuteMapChange do (the map's resource container, its cooked entities, the
// collision models from the retail .bcm, every AAS type), and
// prints the allocation ledgers after each step, the step's peak included; then it frees all of it
// in MapShutdown's order. Debug builds only: it measures, and the game's own load replaces it.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/game/map_data.h"

#if PS2_D3BFG_DEBUG

#include "ps2/system/heap.h"
#include "ps2/system/lifecycle.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>

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

    collisionModelManager->FreeMap();
    delete mapFile;
    fileSystem->UnloadMapResources(mapName);
    Checkpoint("end", start);
}

} // namespace

namespace ps2::game
{

PS2_COLD_FUNC void InitMapData()
{
    cmdSystem->AddCommand("ps2_mapdata", MapData, CMD_FL_SYSTEM,
                          CMD_DESC("load a map's game data step by step with memory checkpoints, then free it"));
}

} // namespace ps2::game

#endif // PS2_D3BFG_DEBUG
