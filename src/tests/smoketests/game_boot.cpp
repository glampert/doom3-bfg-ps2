// ================================================================================================
// File: game_boot.cpp
// Brief: Prove native initialization, entity thinking, script waits and stable logic-map reloads on the EE.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/game_boot.h"
#include "ps2/game/headless_fixture.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>

// Use the retained game class/event hierarchy and interpreter, not the compiler-only substitute.
#include <d3xp/Game_local.h>

namespace
{
class LogicProbe final : public idEntity
{
    CLASS_PROTOTYPE(LogicProbe);

  public:
    void Spawn() { BecomeActive(TH_THINK); }
    void Think() override { ++ticks; }
    int ticks = 0;
};
CLASS_DECLARATION(idEntity, LogicProbe)
END_CLASS

bool Check(const char * name, bool passed)
{
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] CHECK game/%s %s\n", name, passed ? "PASS" : "FAIL");
    return passed;
}
bool Same(const ps2::heap::Stats & a, const ps2::heap::Stats & b)
{
    return a.requestedBytes == b.requestedBytes && a.backingBytes == b.backingBytes && a.allocationCount == b.allocationCount;
}
void Memory(const char * stage)
{
    const auto stats = ps2::heap::GetTotalStats();
    const auto arena = ps2::heap::GetArenaStats();
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_MEMORY %s requested=%zu backing=%zu allocations=%zu peak_requested=%zu peak_backing=%zu arena=%zu\n",
             stage, stats.requestedBytes, stats.backingBytes, stats.allocationCount,
             stats.peakRequestedBytes, stats.peakBackingBytes, arena.committedBytes);
}
} // namespace

namespace ps2::smoketests
{
bool RunGameTests(const char * mode)
{
    ::game->Init(); // Installs the versioned imports and initializes native class/event/script registries.
    Memory("initialized");
    bool passed = Check("native-init", ::game == &gameLocal && gameLocal.GameState() == GAMESTATE_NOMAP &&
                                       gameLocal.program.FindFunction("doom_main") != nullptr && idClass::GetClass("idWorldspawn") != nullptr);
    if (idStr::Cmp(mode, "game") != 0)
    {
        gameLocal.InitHeadlessFixture("maps/logic");
        ps2::FatalError("game failure probe returned unexpectedly");
    }
    // This buffer is native and deliberately heap allocated instead of consuming the EE stack.
    idUserCmdMgr * commands = new (TAG_GAME) idUserCmdMgr;
    ps2::heap::Stats warmed{};
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        gameLocal.InitHeadlessFixture("maps/logic");
        passed = Check("logic-world", gameLocal.world != nullptr && gameLocal.GameState() == GAMESTATE_ACTIVE &&
                                      gameRenderWorld == nullptr && gameSoundWorld == nullptr && gameLocal.numClients == 0) &&
                 passed;
        idDict args;
        args.Set("name", "logic_probe");
        args.Set("noclipmodel", "1");
        LogicProbe * probe = static_cast<LogicProbe *>(gameLocal.SpawnEntityType(LogicProbe::Type, &args));
        args.Clear();
        idVarDef * value = gameLocal.program.GetDef(&type_float, "fixtureTicks", &def_namespace);
        passed = value != nullptr && *value->value.floatPtr == 0.0f && passed;
        for (int frame = 1; frame <= 8; ++frame)
        {
            gameReturn_t result{};
            ::game->RunFrame(*commands, result);
            ps2::Log(ps2::LogLevel::Info, "[D3BFG] GAME_TICK cycle=%d frame=%d time=%d expected=%d think=%d script=%.0f\n",
                     cycle, gameLocal.GetFrameNum(), gameLocal.time, FRAME_TO_MSEC(frame), probe->ticks,
                     static_cast<double>(value != nullptr ? *value->value.floatPtr : -1.0f));
            passed = gameLocal.GetFrameNum() == frame && gameLocal.time == FRAME_TO_MSEC(frame) &&
                     probe->ticks == frame && value != nullptr && *value->value.floatPtr == static_cast<float>(frame) &&
                     result.sessionCommand[0] == '\0' && result.vibrationLow == 0 && result.vibrationHigh == 0 && passed;
        }
        passed = Check("native-ticks-script-events", passed) && passed;
        Memory("ticked");
        ::game->MapShutdown();
        passed = Check("map-shutdown", gameLocal.GameState() == GAMESTATE_NOMAP && gameLocal.world == nullptr &&
                                       gameLocal.FindEntity("logic_probe") == nullptr && gameLocal.program.FindFunction("main") == nullptr) &&
                 passed;
        const auto stats = ps2::heap::GetTotalStats();
        if (cycle == 0)
        {
            warmed = stats;
        }
        else
        {
            passed = Check("reload-ledger", Same(warmed, stats)) && passed;
        }
    }
    delete commands;
    ::game->Shutdown();
    Memory("shutdown");
    return passed;
}
} // namespace ps2::smoketests
