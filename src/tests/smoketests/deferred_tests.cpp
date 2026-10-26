// ================================================================================================
// File: deferred_tests.cpp
// Brief: Exercise native offline multiplayer lifecycle, save descriptions and disabled save policy.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/deferred_tests.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>

// The test uses the same native contracts as the retained campaign callers.
#include <d3xp/Game_local.h>

namespace ps2::tests
{
namespace
{
bool Check(const char * name, bool passed)
{
    Log(LogLevel::Info, "[D3BFG] CHECK deferred/%s %s\n", name, passed ? "PASS" : "FAIL");
    return passed;
}

bool SameLedger(const system::heap::Stats & before)
{
    const system::heap::Stats after = system::heap::GetTotalStats();
    return after.requestedBytes == before.requestedBytes && after.backingBytes == before.backingBytes &&
           after.allocationCount == before.allocationCount;
}

bool SaveMetadataCycle()
{
    idSaveGameDetails original;
    bool passed = original.descriptors.GetNumKeyVals() == 0 && !original.damaged && original.date == 0 &&
                  original.slotName.IsEmpty() && original.GetDifficulty() == -1 && original.GetPlaytime() == 0;
    original.slotName = "fixture-save-slot";
    original.damaged = true;
    original.date = 123;
    original.descriptors.Set(SAVEGAME_DETAIL_FIELD_MAP, "fixture/map-with-a-long-description-for-owned-string-storage");
    original.descriptors.Set(SAVEGAME_DETAIL_FIELD_LANGUAGE, ID_LANG_ENGLISH);
    original.descriptors.SetInt(SAVEGAME_DETAIL_FIELD_DIFFICULTY, 2);
    original.descriptors.SetInt(SAVEGAME_DETAIL_FIELD_PLAYTIME, 42);
    idSaveGameDetails copy;
    copy = original;
    original.Clear();
    original.Clear();
    passed = passed && original.descriptors.GetNumKeyVals() == 0 && original.slotName.IsEmpty() &&
             !original.damaged && original.date == 0 && copy.damaged && copy.date == 123 &&
             copy.slotName == "fixture-save-slot" && copy.GetMapName() == "fixture/map-with-a-long-description-for-owned-string-storage" &&
             copy.GetLanguage() == ID_LANG_ENGLISH && copy.GetDifficulty() == 2 && copy.GetPlaytime() == 42;
    return passed;
}
} // namespace

bool RunDeferredTests()
{
    const system::heap::Stats before = system::heap::GetTotalStats();
    bool inactive = true;
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        {
            idMultiplayerGame multiplayer;
            multiplayer.Reset();
            multiplayer.Precache();
            multiplayer.SetScoreboardActive(false);
            multiplayer.CleanupScoreboard();
            LeaderboardLocal_Init();
            LeaderboardLocal_Shutdown();
            inactive = inactive && multiplayer.GetGameState() == idMultiplayerGame::INACTIVE &&
                       !multiplayer.IsPureReady() && !multiplayer.IsScoreboardActive() && !multiplayer.IsFlagMsgOn() &&
                       !multiplayer.IsGametypeTeamBased() && !multiplayer.IsGametypeFlagBased() &&
                       multiplayer.player_red_flag == -1 && multiplayer.player_blue_flag == -1;
            multiplayer.Shutdown();
            multiplayer.Shutdown();
        }
        inactive = inactive && SameLedger(before);
    }
    bool passed = Check("offline-multiplayer-ledger", inactive);

    // Native dictionary pools retain reusable capacity; warm it before checking repeated ownership.
    bool metadata = SaveMetadataCycle();
    const system::heap::Stats warmed = system::heap::GetTotalStats();
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        metadata = SaveMetadataCycle() && metadata;
        metadata = SameLedger(warmed) && metadata;
    }
    passed = Check("save-metadata-ledger", metadata) && passed;

    const idCVar * enabled = cvarSystem->Find("saveGame_enable");
    cmdSystem->BufferCommandText(CMD_EXEC_NOW, "saveGame_enable 1\n");
    passed = Check("disabled-save-policy", enabled != nullptr && !enabled->GetBool() &&
                                           (enabled->GetFlags() & (CVAR_BOOL | CVAR_ROM)) == (CVAR_BOOL | CVAR_ROM)) &&
             passed;
    return passed;
}

} // namespace ps2::tests
