// ================================================================================================
// File: headless_fixture.cpp
// Brief: Load a fixed collision world and run native spawning/scripts without players or presentation.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/game/headless_fixture.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>

// Game members depend on the full engine declarations.
#include <d3xp/Game_local.h>

namespace
{
// Only these two axial boxes reach the native brush converter: floor, then wall.
static constexpr float kBrushBounds[2][2][3] = {
    { { -64.0f, -64.0f, -8.0f }, { 64.0f, 64.0f, 0.0f } },
    { { 24.0f, -64.0f, 0.0f }, { 32.0f, 64.0f, 64.0f } },
};

void ValidateBrushes(const idMapEntity & entity)
{
    if (entity.GetNumPrimitives() != 2)
    {
        ps2::FatalError("headless game fixture requires two six-sided collision brushes");
    }
    for (int brushIndex = 0; brushIndex < 2; ++brushIndex)
    {
        const idMapPrimitive * primitive = entity.GetPrimitive(brushIndex);
        if (primitive->GetType() != idMapPrimitive::TYPE_BRUSH)
        {
            ps2::FatalError("headless game fixture requires two six-sided collision brushes");
        }
        const auto * brush = static_cast<const idMapBrush *>(primitive);
        if (brush->GetNumSides() != 6 || brush->epairs.GetNumKeyVals() != 0)
        {
            ps2::FatalError("headless game fixture requires two six-sided collision brushes");
        }
        for (int sideIndex = 0; sideIndex < 6; ++sideIndex)
        {
            const idMapBrushSide * side = brush->GetSide(sideIndex);
            if (idStr::Cmp(side->GetMaterial(), "textures/fixture/solid") != 0)
            {
                ps2::FatalError("headless game fixture brush material is unsupported");
            }
            const int axis = sideIndex / 2;
            const bool positive = sideIndex % 2 == 0;
            idPlane expected;
            expected.Zero();
            expected[axis] = positive ? 1.0f : -1.0f;
            expected[3] = positive ? -kBrushBounds[brushIndex][1][axis] : kBrushBounds[brushIndex][0][axis];
            if (!side->GetPlane().Compare(expected, 0.001f))
            {
                ps2::FatalError("headless game fixture brush planes are unsupported");
            }
        }
    }
}
} // namespace

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
    if (idStr::Cmp(entity->epairs.GetString("classname"), "worldspawn") != 0 ||
        idStr::Cmp(entity->epairs.GetString("name"), "worldMap") != 0)
    {
        ps2::FatalError("headless game fixture requires a worldMap worldspawn");
    }
    ValidateBrushes(*entity);
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
    gravity.Set(0.0f, 0.0f, -g_gravity.GetFloat());
    playerPVS.i = playerConnectedAreas.i = -1;
    collisionModelManager->LoadMap(mapFile);
    clip.Init();
    InitScriptForMap();
    SpawnMapEntities();
    idEvent::ServiceEvents();
    SetScriptFPS(com_engineHz_latched);
    gamestate = GAMESTATE_ACTIVE;
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME collision map ready; players/AAS/PVS/presentation deferred\n");
}
