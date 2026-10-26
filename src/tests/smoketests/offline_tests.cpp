// ================================================================================================
// File: offline_tests.cpp
// Brief: Exercise the real Common ABI, local user/profile state and bounded offline loading protocol.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/offline_tests.h"
#include "ps2/system/core.h"
#include "ps2/system/heap.h"
#include "ps2/system/lifecycle.h"
#include "ps2/system/log.h"
#include "ps2/system/offline_session.h"
#include "tests/smoketests/audio_tests.h"
#include "tests/smoketests/codec_tests.h"
#include <cstring>
#include <idlib/precompiled.h>

// Common_local.h relies on the complete engine precompiled header.
#include <framework/Common_local.h>

namespace ps2::tests
{
namespace
{

bool Check(const char * name, bool passed)
{
    Log(LogLevel::Info, "[D3BFG] CHECK offline/%s %s\n", name, passed ? "PASS" : "FAIL");
    return passed;
}

idMatchParameters CampaignParameters()
{
    idMatchParameters parms;
    parms.numSlots = 1;
    parms.gameMode = GAME_MODE_SINGLEPLAYER;
    parms.gameMap = GAME_MAP_SINGLEPLAYER;
    parms.mapName = "fixture/test";
    parms.serverInfo.Set("fixture_key", "copied");
    return parms;
}

} // namespace

bool RunOfflineTests()
{
    const system::heap::Stats resetBaseline = system::heap::GetTotalStats();
    bool resetPassed = true;
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        commonLocal.StopPlayingRenderDemo();
        commonLocal.StopRecordingRenderDemo();
        const system::heap::Stats after = system::heap::GetTotalStats();
        resetPassed = resetPassed && !common->IsMultiplayer() && !common->IsServer() && !common->IsClient() &&
                      after.requestedBytes == resetBaseline.requestedBytes && after.backingBytes == resetBaseline.backingBytes &&
                      after.allocationCount == resetBaseline.allocationCount;
    }
    const bool resetChecked = Check("common-idle-demo-ledger", resetPassed);

    const idCVar * smp = cvarSystem->Find("com_smp");
    bool passed = Check("real-common", common == &commonLocal && common->IsInitialized() &&
                                       system::lifecycle::CompletedStage() == system::lifecycle::Stage::Session && common->Session() == session &&
                                       common->Game() == nullptr && common->RW() == nullptr && common->SW() == nullptr &&
                                       common->MenuSW() == nullptr && common->GetCurrentGame() == DOOM3_BFG &&
                                       smp != nullptr && !smp->GetBool() && (smp->GetFlags() & CVAR_ROM) != 0);
    idSignInManagerBase & signIn = session->GetSignInManager();
    idLocalUser * user = signIn.GetMasterLocalUser();
    passed = Check("local-user", system::session::IsInitialized() && session->GetState() == idSession::IDLE &&
                                 signIn.GetNumLocalUsers() == 1 && user != nullptr && !user->IsOnline() && !user->IsPersistent() &&
                                 user->GetOnlineCaps() == 0 && user->IsProfileReady() && !user->IsStorageDeviceAvailable() &&
                                 signIn.GetLocalUserByIndex(-1) == nullptr && signIn.GetLocalUserByIndex(1) == nullptr &&
                                 signIn.GetLocalUserByHandle(user->GetLocalUserHandle()) == user && signIn.RequirePersistentMaster()) &&
             passed;
    if (user == nullptr)
    {
        return false;
    }
    const localUserHandle_t originalHandle = user->GetLocalUserHandle();
    signIn.RegisterLocalUser(0);
    passed = Check("registration-idempotent", signIn.GetNumLocalUsers() == 1 && signIn.GetMasterLocalUser() == user) && passed;
    user->SetStatInt(3, 27);
    user->SetStatFloat(4, 0.25f);
    idAchievementSystem & achievements = session->GetAchievementSystem();
    achievements.AchievementUnlock(user, 0);
    achievements.AchievementUnlock(user, 63);
    achievements.AchievementUnlock(user, 64);
    achievements.AchievementUnlock(user, 127);
    achievements.AchievementLock(user, 63);
    const bool defaultProfilePreserved = signIn.GetDefaultProfile() == user->GetProfile() && user->GetStatInt(3) == 27;
    idArray<bool, idAchievementSystem::MAX_ACHIEVEMENTS> bits;
    passed = Check("transient-profile-achievements", user->GetStatInt(3) == 27 && user->GetStatFloat(4) == 0.25f &&
                                                     defaultProfilePreserved && achievements.GetAchievementState(user, bits) &&
                                                     bits[0] && !bits[63] && bits[64] && bits[127]) &&
             passed;
    idPlayerProfile * profile = user->GetProfile();
    profile->SaveSettings(true);
    session->UpdateSignInManager();
    passed = Check("persistence-unavailable", profile->GetState() == idPlayerProfile::ERR &&
                                              profile->GetRequestedState() == idPlayerProfile::IDLE && profile->GetAchievement(127) &&
                                              !session->IsSaveGameCompletedFromHandle(saveGameHandle_t(1)) && !session->IsTitleStorageLoaded() &&
                                              session->GetTitleStorageInt("absent", 73) == 73) &&
             passed;

    system::heap::Stats baseline = {};
    bool stable = true;
    bool transitions = true;
    // Doom string pools retain capacity until idLib shutdown. Warm them once before comparing reloads.
    for (int cycle = 0; cycle < 4; ++cycle)
    {
        {
            idMatchParameters parms = CampaignParameters();
            session->CreatePartyLobby(parms);
            transitions = transitions && session->GetState() == idSession::PARTY_LOBBY;
            session->CreateMatch(parms);
            parms.mapName = "changed";
            parms.serverInfo.Set("fixture_key", "changed");
        }
        idLobbyBase & lobby = session->GetActingGameStateLobbyBase();
        const lobbyUserID_t id = lobby.GetLobbyUserIdByOrdinal(0);
        transitions = transitions && session->GetState() == idSession::GAME_LOBBY && lobby.IsHost() &&
                      !lobby.IsPeer() && !lobby.HasActivePeers() && lobby.GetNumLobbyUsers() == 1 &&
                      lobby.GetNumConnectedPeers() == 0 && lobby.PeerIndexFromLobbyUser(id) == -1 &&
                      lobby.GetLocalUserFromLobbyUser(id) == user && lobby.GetProfileFromLobbyUser(id) == profile &&
                      !lobby.GetLobbyUserIdByOrdinal(1).IsValid() && !lobby.IsLobbyUserValid(lobbyUserID_t()) &&
                      idStr::Cmp(lobby.GetMatchParms().mapName, "fixture/test") == 0 &&
                      idStr::Cmp(lobby.GetMatchParms().serverInfo.GetString("fixture_key"), "copied") == 0;
        const int loadingId = session->GetLoadingID();
        session->StartMatch();
        common->Frame();
        transitions = transitions && session->GetState() == idSession::LOADING && session->IsAboutToLoad() &&
                      session->GetLoadingID() == loadingId + 1 && !lobby.IsLobbyUserLoaded(id);
        session->LoadingFinished();
        transitions = transitions && session->GetState() == idSession::INGAME && lobby.IsLobbyUserLoaded(id);
        session->EndMatch(false);
        transitions = transitions && session->GetState() == idSession::GAME_LOBBY;
        session->StartMatch();
        transitions = transitions && session->GetState() == idSession::LOADING && !lobby.IsLobbyUserLoaded(id);
        session->Cancel();
        session->QuitMatchToTitle();
        transitions = transitions && session->GetState() == idSession::IDLE && lobby.GetNumLobbyUsers() == 0 &&
                      !lobby.IsLobbyUserValid(id) && !lobby.IsHost();
        const system::heap::Stats after = system::heap::GetTotalStats();
        if (cycle == 0)
        {
            baseline = after;
        }
        else
        {
            stable = stable && baseline.requestedBytes == after.requestedBytes &&
                     baseline.backingBytes == after.backingBytes && baseline.allocationCount == after.allocationCount;
        }
    }
    passed = Check("campaign-transitions-copy", transitions) && passed;
    passed = Check("match-reload-ledger", stable) && passed;
    int routing[MAX_INPUT_DEVICES];
    const bool routed = session->GetInputRouting(routing) == 1 && routing[0] == 0 && routing[1] == -1;
    signIn.RemoveLocalUserByIndex(0);
    const bool signedOut = session->GetState() == idSession::PRESS_START && signIn.GetNumLocalUsers() == 0 &&
                           signIn.GetLocalUserByHandle(originalHandle) == nullptr && session->GetInputRouting(routing) == 0 && routing[0] == -1;
    signIn.RegisterLocalUser(0);
    user = signIn.GetMasterLocalUser();
    passed = Check("signout-routing-stale-handle", routed && signedOut && user != nullptr &&
                                                   !(user->GetLocalUserHandle() == originalHandle) && session->GetState() == idSession::IDLE &&
                                                   !user->GetProfile()->GetAchievement(127) && user->GetStatInt(3) == 0) &&
             passed;
    return passed && resetChecked;
}

bool IsCommonProbe(const char * path)
{
    return idStr::Cmpn(path, "host:probe-", 11) == 0;
}

bool RunCommonProbe(const char * path)
{
    const char * name = path + 11;
    if (idStr::Cmpn(name, "lifecycle-", 10) == 0)
    {
        const char * stageName = name + 10;
        system::lifecycle::Stage stop = system::lifecycle::Stage::None;
        for (const system::lifecycle::Stage stage : { system::lifecycle::Stage::System, system::lifecycle::Stage::Idlib,
                                              system::lifecycle::Stage::Commands, system::lifecycle::Stage::Cvars, system::lifecycle::Stage::Filesystem,
                                              system::lifecycle::Stage::Jobs, system::lifecycle::Stage::Session })
        {
            if (idStr::Cmp(stageName, system::lifecycle::StageName(stage)) == 0)
            {
                stop = stage;
            }
        }
        if (stop == system::lifecycle::Stage::None)
        {
            FatalError("invalid lifecycle probe");
        }
        const system::heap::Stats baseline = system::heap::GetTotalStats();
        system::lifecycle::StopAfterForTest(stop);
        common->Init(0, nullptr, nullptr);
        common->Shutdown(); // A stopped Init already shut down; this must be idempotent.
        const system::heap::Stats after = system::heap::GetTotalStats();
        const bool passed = common == &commonLocal && !common->IsInitialized() && common->IsShuttingDown() &&
                            system::lifecycle::CompletedStage() == system::lifecycle::Stage::None && !fileSystem->IsInitialized() &&
                            !cvarSystem->IsInitialized() && !system::session::IsInitialized() &&
                            baseline.requestedBytes == after.requestedBytes && baseline.backingBytes == after.backingBytes &&
                            baseline.allocationCount == after.allocationCount;
        Log(LogLevel::Info, "[D3BFG] CHECK probe/partial-shutdown %s\n", passed ? "PASS" : "FAIL");
        return passed;
    }
    system::lifecycle::InitCore();
    Log(LogLevel::Info, "[D3BFG] CHECK probe/ready PASS\n[D3BFG] PROBE %s BEGIN\n", name);
    idMatchParameters parms = CampaignParameters();
    if (idStr::Cmp(name, "online-flags") == 0)
    {
        parms.matchFlags = MATCH_ONLINE;
        session->CreateMatch(parms);
    }
    else if (idStr::Cmp(name, "no-user") == 0)
    {
        session->MoveToPressStart();
        session->CreateMatch(parms);
    }
    else if (idStr::Cmp(name, "bad-load-order") == 0)
    {
        session->LoadingFinished();
    }
    else if (idStr::Cmp(name, "bad-device") == 0)
    {
        session->GetSignInManager().RegisterLocalUser(1);
    }
    else if (idStr::Cmp(name, "platform-negative-duration") == 0)
    {
        (void)Sys_SecToStr(-1);
    }
    else if (!RunAudioFailureProbe(name) && !RunCodecFailureProbe(name))
    {
        FatalError("invalid Common negative probe");
    }
    Log(LogLevel::Error, "[D3BFG] PROBE %s returned unexpectedly\n", name);
    system::lifecycle::ShutdownCore();
    return false;
}

} // namespace ps2::tests
