// ================================================================================================
// File: headless_fixture.cpp
// Brief: Load a fixed collision world and bounded native player/scripts without presentation.
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

void ValidatePlayerSpawn(const idTypeInfo & type, const idDict * args)
{
    if (!IsEnabled() || &type != &idPlayer::Type || args == nullptr || gameLocal.GameState() != GAMESTATE_ACTIVE ||
        gameLocal.world == nullptr || gameRenderWorld != nullptr || gameSoundWorld != nullptr ||
        gameLocal.numClients != 0 || gameLocal.entities[0] != nullptr)
    {
        FatalError("invalid headless player spawn state");
    }
    struct Key
    {
        const char * name;
        const char * value;
    };
    static constexpr Key kKeys[] = {
        { "classname", "fixture_player" },
        { "name", "native_player" },
        { "spawn_entnum", "0" },
        { "noclipmodel", "1" },
        { "scriptobject", "fixture_player" },
        { "origin", "-32 0 0.25" },
        { "health", "100" },
    };
    if (args->GetNumKeyVals() != static_cast<int>(ArrayLength(kKeys)))
    {
        FatalError("headless player spawn arguments are unsupported");
    }
    for (const auto & key : kKeys)
    {
        if (idStr::Cmp(args->GetString(key.name), key.value) != 0)
        {
            FatalError("headless player spawn arguments are unsupported: %s", key.name);
        }
    }
}

void ValidateFrame()
{
    const idPlayer * player = gameLocal.GetLocalPlayer();
    if (!IsEnabled() || gameLocal.GameState() != GAMESTATE_ACTIVE || gameRenderWorld != nullptr || gameSoundWorld != nullptr ||
        gameLocal.numClients < 0 || gameLocal.numClients > 1 ||
        (gameLocal.numClients == 0 ? gameLocal.entities[0] != nullptr : player == nullptr || player->GetType() != &idPlayer::Type))
    {
        FatalError("invalid headless fixture frame state");
    }
}
} // namespace ps2::gamefixture

void idPlayer::SpawnHeadlessFixture()
{
    if (!ps2::gamefixture::IsEnabled() || entityNumber != 0 || gameLocal.entities[0] != this || gameLocal.numClients != 0 ||
        hudManager != nullptr || pdaMenu != nullptr || renderEntity.hModel != nullptr || !scriptObject.HasObject())
    {
        ps2::FatalError("invalid headless player initialization");
    }
    // Entity and Actor CallSpawn have already installed native identity, script storage
    // and manual actor/animation threads. Only this bounded simulation subset follows.
    cinematic = true;
    physicsObj.SetSelf(this);
    SetClipModel();
    physicsObj.SetMass(100.0f);
    physicsObj.SetContents(CONTENTS_BODY);
    physicsObj.SetClipMask(MASK_PLAYERSOLID);
    physicsObj.SetGravity(gameLocal.GetGravity());
    SetPhysics(&physicsObj);
    SetOrigin(spawnArgs.GetVector("origin"));
    InitAASLocation(); // The fixture has no AAS instances; no navigation data is acquired.
    SetEyeHeight(pm_normalviewheight.GetFloat());
    stamina = pm_stamina.GetFloat();
    inventory.Clear();
    inventory.maxHealth = health;
    fl.takedamage = true;
    weaponEnabled = false;
    playerView.SetPlayerEntity(this);
    LinkScriptVariables();
    ConstructScriptObject()->Execute();
    SetState("FixtureIdle");
    gameLocal.numClients = 1;
    BecomeActive(TH_THINK | TH_PHYSICS);
}

void idPlayer::ThinkHeadlessFixture()
{
    ps2::gamefixture::ValidateFrame();
    if (entityNumber != 0 || gameLocal.GetLocalPlayer() != this || health != 100 ||
        usercmd.rightmove != 0 || usercmd.buttons != 0 || usercmd.impulse != 0 || usercmd.impulseSequence != 0 ||
        usercmd.angles[0] != 0 || usercmd.angles[1] != 0 || usercmd.angles[2] != 0 ||
        (usercmd.forwardmove != 0 && usercmd.forwardmove != 127))
    {
        ps2::FatalError("unsupported headless player command or state");
    }
    // Keep native player speed, Move/physics, linked condition variables and actor
    // interpreter state. Weapon/view/UI/audio/trigger/campaign behavior remains deferred.
    AdjustSpeed();
    Move();
    UpdateConditions();
    UpdateScript();
    oldButtons = usercmd.buttons;
    usercmd.pos = physicsObj.GetOrigin();
    playedTimeResidual += gameLocal.time - gameLocal.previousTime;
    playedTimeSecs += playedTimeResidual / 1000;
    playedTimeResidual %= 1000;
}

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
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME collision map ready; AAS/PVS/presentation deferred\n");
}
