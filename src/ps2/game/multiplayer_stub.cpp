// ================================================================================================
// File: multiplayer_stub.cpp
// Brief: Keep campaign multiplayer state empty and reject deferred match operations by native method.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/log.h"
#include <idlib/precompiled.h>

// Native multiplayer and leaderboard declarations depend on the complete game header.
#include <d3xp/Game_local.h>

namespace
{
[[noreturn]] PS2_COLD_FUNC void Unsupported(const char * method)
{
    ps2::FatalError("multiplayer capability unavailable: %s", method);
}

void RequireOffline(const char * method)
{
    if (common->IsMultiplayer() || common->IsServer() || common->IsClient())
    {
        Unsupported(method);
    }
}
} // namespace

idMultiplayerGame::idMultiplayerGame()
    : scoreboardManager(nullptr)
{
    // gameLocal embeds this value before main; do not query other global objects here.
    Clear();
}

void idMultiplayerGame::ClearChatData()
{
    for (mpChatLine_t & chat : chatHistory)
    {
        chat.line.Clear();
        chat.fade = 0;
    }
    chatHistoryIndex = chatHistorySize = lastChatLineTime = 0;
    chatDataUpdated = false;
}

void idMultiplayerGame::Clear()
{
    CleanupScoreboard();
    gameState = nextState = INACTIVE;
    for (mpPlayerState_t & player : playerState)
    {
        player = {};
    }
    for (idPlayer *& player : rankedPlayers)
    {
        player = nullptr;
    }
    nextStateSwitch = warmupEndTime = matchStartedTime = 0;
    currentTourneyPlayer[0] = currentTourneyPlayer[1] = lastWinner = -1;
    one = two = three = pureReady = false;
    numRankedPlayers = fragLimitTimeout = voiceChatThrottle = 0;
    startFragLimit = -1;
    teamFlags[0] = teamFlags[1] = nullptr;
    teamPoints[0] = teamPoints[1] = 0;
    player_red_flag = player_blue_flag = -1;
    flagMsgOn = false;
    ClearChatData();
}

void idMultiplayerGame::Reset()
{
    // Native single-player map startup calls Reset and Precache unconditionally.
    RequireOffline("idMultiplayerGame::Reset");
    Clear();
}
void idMultiplayerGame::Precache() { RequireOffline("idMultiplayerGame::Precache"); }
void idMultiplayerGame::Shutdown() { Clear(); }
void idMultiplayerGame::CleanupScoreboard()
{
    if (scoreboardManager != nullptr)
    {
        Unsupported("idMultiplayerGame::CleanupScoreboard with active scoreboard");
    }
}
bool idMultiplayerGame::IsScoreboardActive()
{
    CleanupScoreboard();
    return false;
}
void idMultiplayerGame::SetScoreboardActive(bool active)
{
    if (active)
    {
        Unsupported("idMultiplayerGame::SetScoreboardActive");
    }
    CleanupScoreboard();
}
bool idMultiplayerGame::IsGametypeFlagBased()
{
    RequireOffline("idMultiplayerGame::IsGametypeFlagBased");
    return false;
}
bool idMultiplayerGame::IsGametypeTeamBased()
{
    RequireOffline("idMultiplayerGame::IsGametypeTeamBased");
    return false;
}
bool idMultiplayerGame::IsFlagMsgOn() { return flagMsgOn; }

// Campaign lifecycle can register zero online boards. No map/mode enumeration,
// title-storage query, leaderboard definition or remote request is performed.
void LeaderboardLocal_Init() { RequireOffline("LeaderboardLocal_Init"); }
void LeaderboardLocal_Shutdown() { RequireOffline("LeaderboardLocal_Shutdown"); }

#define MP_UNSUPPORTED(result, method, parameters) \
    result idMultiplayerGame::method parameters { Unsupported("idMultiplayerGame::" #method); }

MP_UNSUPPORTED(void, Run, ())
MP_UNSUPPORTED(bool, Draw, (int))
MP_UNSUPPORTED(void, SpawnPlayer, (int))
MP_UNSUPPORTED(void, PlayerDeath, (idPlayer *, idPlayer *, bool))
MP_UNSUPPORTED(void, AddChatLine, (const char *, ...))
MP_UNSUPPORTED(void, WriteToSnapshot, (idBitMsg &) const)
MP_UNSUPPORTED(void, ReadFromSnapshot, (const idBitMsg &))
MP_UNSUPPORTED(void, PlayGlobalSound, (int, snd_evt_t, const char *))
MP_UNSUPPORTED(void, PlayTeamSound, (int, snd_evt_t, const char *))
MP_UNSUPPORTED(void, PrintMessageEvent, (msg_evt_t, int, int))
MP_UNSUPPORTED(void, DisconnectClient, (int))
MP_UNSUPPORTED(void, MessageMode_f, (const idCmdArgs &))
MP_UNSUPPORTED(void, VoiceChat_f, (const idCmdArgs &))
MP_UNSUPPORTED(void, VoiceChatTeam_f, (const idCmdArgs &))
MP_UNSUPPORTED(int, NumActualClients, (bool, int *))
MP_UNSUPPORTED(void, DropWeapon, (int))
MP_UNSUPPORTED(void, MapRestart, ())
MP_UNSUPPORTED(void, SwitchToTeam, (int, int, int))
MP_UNSUPPORTED(void, ProcessChatMessage, (int, bool, const char *, const char *, const char *))
MP_UNSUPPORTED(void, ProcessVoiceChat, (int, bool, int))
MP_UNSUPPORTED(bool, HandleGuiEvent, (const sysEvent_t *))
MP_UNSUPPORTED(void, ToggleSpectate, ())
MP_UNSUPPORTED(void, GetSpectateText, (idPlayer *, idStr *, bool))
MP_UNSUPPORTED(void, ServerWriteInitialReliableMessages, (int, lobbyUserID_t))
MP_UNSUPPORTED(void, ClientReadStartState, (const idBitMsg &))
MP_UNSUPPORTED(void, ClientReadWarmupTime, (const idBitMsg &))
MP_UNSUPPORTED(void, ClientReadMatchStartedTime, (const idBitMsg &))
MP_UNSUPPORTED(void, ClientReadAchievementUnlock, (const idBitMsg &))
MP_UNSUPPORTED(void, ServerClientConnect, (int))
MP_UNSUPPORTED(int, GetFlagPoints, (int))
MP_UNSUPPORTED(void, PlayerStats, (int, char *, const int))
MP_UNSUPPORTED(const char *, GetSkinName, (int) const)
MP_UNSUPPORTED(idItemTeam *, GetTeamFlag, (int))
MP_UNSUPPORTED(flagStatus_t, GetFlagStatus, (int))
MP_UNSUPPORTED(void, TeamScoreCTF, (int, int))
MP_UNSUPPORTED(void, PlayerScoreCTF, (int, int))
MP_UNSUPPORTED(int, GetFlagCarrier, (int))
MP_UNSUPPORTED(void, ReloadScoreboard, ())
MP_UNSUPPORTED(int, GetGameModes, (const char ***, const char ***))

#undef MP_UNSUPPORTED
