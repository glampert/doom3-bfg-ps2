// ================================================================================================
// File: map_boot.cpp
// Brief: Exercise native map startup, logical world/PVS, collision, AAS and one script-driven AI.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/map_boot.h"
#include "ps2/renderer/null_render.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>

// Game declarations require the complete native precompiled header first.
#include <d3xp/Game_local.h>

namespace
{
bool Check(const char * name, bool passed)
{
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] CHECK map/%s %s\n", name, passed ? "PASS" : "FAIL");
    return passed;
}

bool WorldQueries(idRenderWorld & world)
{
    const idVec3 left(-32.0f, 0.0f, 16.0f), right(32.0f, 0.0f, 16.0f);
    int areas[2] = { -1, -1 };
    bool passed = Check("world-areas", world.NumAreas() == 2 && world.PointInArea(left) == 0 && world.PointInArea(right) == 1 &&
                                       world.BoundsInAreas(idBounds(left, right), areas, 2) == 2 && areas[0] != areas[1]);
    const auto * model = renderModelManager->CheckModel("_area0");
    passed = Check("world-model", model != nullptr && !model->IsDefaultModel() && model->IsStaticWorldModel() &&
                                  model->NumSurfaces() == 1 && model->Surface(0)->geometry->numVerts == 4 &&
                                  model->Surface(0)->geometry->numIndexes == 6) &&
             passed;
    modelTrace_t trace{};
    const bool hit = world.ModelTrace(trace, 0, idVec3(-32.0f, 0.0f, 8.0f), idVec3(-32.0f, 0.0f, -8.0f), 0.0f);
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] MAP_MODEL hit=%d fraction=%.3f flags=%d base=%d normal_z=%.3f\n",
             static_cast<int>(hit), static_cast<double>(trace.fraction),
             model != nullptr ? model->Surface(0)->shader->GetSurfaceFlags() : 0,
             model != nullptr ? model->NumBaseSurfaces() : 0, static_cast<double>(trace.normal.z));
    passed = Check("model-trace", hit &&
                                  idMath::Fabs(trace.fraction - 0.5f) < 0.01f &&
                                  trace.material == declManager->FindMaterial("textures/fixture/solid", false)) &&
             passed;
    const qhandle_t portal = world.FindPortal(idBounds(idVec3(-1.0f, -64.0f, 0.0f), idVec3(1.0f, 64.0f, 64.0f)));
    passed = Check("world-portal", portal > 0 && world.NumPortalsInArea(0) == 1 && world.NumPortalsInArea(1) == 1 &&
                                   world.GetPortal(0, 0).areas[0] == 0 && world.GetPortalState(portal) == PS_BLOCK_NONE) &&
             passed;
    auto pvs = gameLocal.pvs.SetupCurrentPVS(left);
    passed = Check("pvs-open", gameLocal.pvs.InCurrentPVS(pvs, right)) && passed;
    gameLocal.pvs.FreeCurrentPVS(pvs);
    world.SetPortalState(portal, PS_BLOCK_VIEW);
    pvs = gameLocal.pvs.SetupCurrentPVS(left);
    passed = Check("pvs-closed", gameLocal.pvs.InCurrentPVS(pvs, left) && !gameLocal.pvs.InCurrentPVS(pvs, right)) && passed;
    gameLocal.pvs.FreeCurrentPVS(pvs);
    world.SetPortalState(portal, PS_BLOCK_NONE);
    pvs = gameLocal.pvs.SetupCurrentPVS(left);
    passed = Check("pvs-reopened", gameLocal.pvs.InCurrentPVS(pvs, right)) && passed;
    gameLocal.pvs.FreeCurrentPVS(pvs);
    return passed;
}
} // namespace

namespace ps2::tests
{

bool RunMapTests()
{
    ::game->Init();
    ps2::renderer::InitLogical("textures/fixture/solid");
    auto * world = renderSystem->AllocRenderWorld();
    auto * sound = soundSystem->AllocSoundWorld(world); // Empty native bookkeeping only; s_noSound is set.
    auto * commands = new (TAG_GAME) idUserCmdMgr;
    bool passed = true;
    ps2::system::heap::Stats warmed{};
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        if (!world->InitFromMap("maps/headless"))
        {
            ps2::FatalError("authored headless render world could not load");
        }
        ::game->InitFromNewMap("maps/headless", world, sound, GAME_SP, 0);
        auto * entity = gameLocal.FindEntity("map_ai");
        auto * ai = entity != nullptr && entity->GetType() == &idAI::Type ? static_cast<idAI *>(entity) : nullptr;
        auto * aas = gameLocal.GetAAS("aas48");
        passed = Check("native-startup", gameLocal.GameState() == GAMESTATE_ACTIVE && gameLocal.world != nullptr &&
                                         gameLocal.numClients == 0 && ai != nullptr && ai->scriptObject.HasObject()) &&
                 passed;
        passed = WorldQueries(*world) && passed;
        trace_t floor{};
        gameLocal.clip.Translation(floor, idVec3(-64.0f, 0.0f, 8.0f), idVec3(-64.0f, 0.0f, -8.0f),
                                   nullptr, mat3_identity, CONTENTS_SOLID, nullptr);
        passed = Check("collision-floor", floor.fraction > 0.0f && floor.fraction < 1.0f &&
                                          floor.c.entityNum == ENTITYNUM_WORLD && floor.c.normal == idVec3(0.0f, 0.0f, 1.0f) &&
                                          idMath::Fabs(floor.endpos.z - CM_CLIP_EPSILON) < 0.05f) &&
                 passed;
        passed = Check("presentation-off", ps2::renderer::IsLogicalInitialized() && !renderSystem->IsOpenGLRunning() &&
                                           cvarSystem->GetCVarBool("s_noSound") && soundSystem->GetPlayingSoundWorld() == nullptr) &&
                 passed;
        const idVec3 start(-32.0f, 0.0f, 0.25f), goal(32.0f, 0.0f, 0.25f);
        aasPath_t path{};
        passed = Check("aas-route", aas != nullptr && aas->GetSettings() != nullptr && aas->PointAreaNum(start) == 1 &&
                                    aas->PointAreaNum(goal) == 2 && aas->PointAreaNum(idVec3(256.0f, 0.0f, 0.0f)) == 0 &&
                                    aas->WalkPathToGoal(path, 1, start, 2, goal, TFL_WALK | TFL_AIR)) &&
                 passed;
        bool ticked = ai != nullptr;
        for (int frame = 1; frame <= 60; ++frame)
        {
            gameReturn_t result{};
            ::game->RunFrame(*commands, result);
            const auto * ticks = gameLocal.program.GetDef(&type_float, "mapAITicks", &def_namespace);
            const auto * mapTicks = gameLocal.program.GetDef(&type_float, "mapTicks", &def_namespace);
            ticked = ai != nullptr && ticks != nullptr && *ticks->value.floatPtr == static_cast<float>(frame) &&
                     mapTicks != nullptr && *mapTicks->value.floatPtr == static_cast<float>(frame) &&
                     gameLocal.GetFrameNum() == frame && gameLocal.time == FRAME_TO_MSEC(frame) && ticked;
        }
        passed = Check("native-ai-ticks", ticked) && passed;
        const auto * reachable = gameLocal.program.GetDef(&type_float, "mapAITravel", &def_namespace);
        const auto * unreachable = gameLocal.program.GetDef(&type_float, "mapAIUnreachable", &def_namespace);
        passed = Check("ai-aas-binding", reachable != nullptr && *reachable->value.floatPtr > 0.0f &&
                                         unreachable != nullptr && *unreachable->value.floatPtr == -1.0f) &&
                 passed;
        passed = Check("ai-floor", ai != nullptr && ai->GetPhysics()->HasGroundContacts() &&
                                   idMath::Fabs(ai->GetPhysics()->GetOrigin().z - CM_CLIP_EPSILON) < 0.05f) &&
                 passed;
        const auto stats = ps2::system::heap::GetTotalStats();
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] MAP_TICK cycle=%d frames=%d ai_x=%.3f ai_z=%.3f heap=%zu/%zu/%zu\n",
                 cycle, gameLocal.GetFrameNum(), static_cast<double>(ai != nullptr ? ai->GetPhysics()->GetOrigin().x : -999.0f),
                 static_cast<double>(ai != nullptr ? ai->GetPhysics()->GetOrigin().z : -999.0f),
                 stats.requestedBytes, stats.backingBytes, stats.allocationCount);
        ::game->MapShutdown();
        passed = Check("native-map-shutdown", gameLocal.world == nullptr && gameLocal.FindEntity("map_ai") == nullptr &&
                                              gameLocal.GameState() == GAMESTATE_NOMAP && idClipModel::TraceModelCacheSize() == 0) &&
                 passed;
        const auto after = ps2::system::heap::GetTotalStats();
        if (cycle == 1)
        {
            warmed = after;
        }
        if (cycle == 2)
        {
            passed = Check("reload-ledger", after.requestedBytes == warmed.requestedBytes &&
                                            after.backingBytes == warmed.backingBytes && after.allocationCount == warmed.allocationCount) &&
                     passed;
        }
    }
    delete commands;
    ::game->Shutdown();
    soundSystem->FreeSoundWorld(sound);
    renderSystem->FreeRenderWorld(world);
    ps2::renderer::ShutdownLogical();
    const auto stats = ps2::system::heap::GetTotalStats();
    const auto arena = ps2::system::heap::GetArenaStats();
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] MAP_MEMORY peak=%zu/%zu arena=%zu shutdown=%zu/%zu/%zu\n",
             stats.peakRequestedBytes, stats.peakBackingBytes, arena.committedBytes,
             stats.requestedBytes, stats.backingBytes, stats.allocationCount);
    return passed;
}

} // namespace ps2::tests
