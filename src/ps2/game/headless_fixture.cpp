// ================================================================================================
// File: headless_fixture.cpp
// Brief: Load only a bounded logic world and run native spawning/scripts without presentation or collision.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/game/headless_fixture.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>

// Game members depend on the full engine declarations.
#include <d3xp/Game_local.h>

namespace ps2::gamefixture
{
namespace
{
static bool s_enabled = false;
}
void Enable()
{
    if (s_enabled || common->IsInitialized())
    {
        FatalError("headless game fixture must be selected before startup");
    }
    s_enabled = true;
}
bool IsEnabled() { return s_enabled; }
} // namespace ps2::gamefixture

void idGameLocal::InitHeadlessFixture(const char * mapName)
{
    if (!ps2::gamefixture::IsEnabled() || gamestate != GAMESTATE_NOMAP ||
        mapName == nullptr || idStr::Cmp(mapName, "maps/logic") != 0)
    {
        ps2::FatalError("invalid headless game fixture startup");
    }
    const int length = fileSystem->GetFileLength("maps/logic.map");
    if (length <= 0 || length > 65536)
    {
        ps2::FatalError("headless game fixture map is missing or oversized");
    }
    delete mapFile;
    mapFile = new (TAG_GAME) idMapFile;
    if (!mapFile->Parse("maps/logic.map", false, false) || mapFile->GetNumEntities() != 1)
    {
        ps2::FatalError("headless game fixture requires one logic worldspawn");
    }
    const idMapEntity * entity = mapFile->GetEntity(0);
    if (entity->GetNumPrimitives() != 0 || idStr::Cmp(entity->epairs.GetString("classname"), "worldspawn") != 0)
    {
        ps2::FatalError("headless game fixture does not support geometry or other map entities");
    }
    // Reject media/physics keys before native precaching or spawning can acquire resources.
    for (int i = 0; i < entity->epairs.GetNumKeyVals(); ++i)
    {
        const char * key = entity->epairs.GetKeyVal(i)->GetKey().c_str();
        if (idStr::Cmp(key, "classname") != 0 && idStr::Cmp(key, "name") != 0)
        {
            ps2::FatalError("headless game fixture world key is unsupported: %s", key);
        }
    }
    mapFileName = mapFile->GetName();
    gameRenderWorld = nullptr;
    gameSoundWorld = nullptr;
    gameType = GAME_SP;
    gamestate = GAMESTATE_STARTUP;
    numClients = 0;
    num_entities = firstFreeEntityIndex[0] = MAX_CLIENTS;
    spawnCount = INITIAL_SPAWN_COUNT;
    framenum = time = previousTime = 0;
    random.SetSeed(0);
    ResetSlowTimeVars();
    playerPVS.i = playerConnectedAreas.i = -1;
    InitScriptForMap();
    SpawnMapEntities();
    idEvent::ServiceEvents();
    SetScriptFPS(com_engineHz_latched);
    gamestate = GAMESTATE_ACTIVE;
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME logic map ready; players/collision/PVS/presentation deferred\n");
}
