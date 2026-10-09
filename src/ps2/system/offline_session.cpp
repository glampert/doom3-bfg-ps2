// ================================================================================================
// File: offline_session.cpp
// Brief: Implement one local campaign user, transient profile and synchronous lobby/load transitions.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/offline_session.h"
#include "ps2/system/filesystem.h"
#include "ps2/system/log.h"
#include <climits>
#include <idlib/precompiled.h>

namespace
{

[[noreturn]] void Unsupported(const char * operation)
{
    ps2::FatalError("unsupported offline session operation: %s", operation);
}
void Require(bool condition, const char * operation)
{
    PS2_AssertMsg(condition, operation);
    if (!condition)
    {
        ps2::FatalError("invalid offline session operation: %s", operation);
    }
}

#define OFFLINE_UNSUPPORTED(result, method, parameters) \
    result method parameters override { Unsupported(#method); }

class OfflineUser final : public idLocalUser
{
  public:
    void PumpPlatform() override {}
    bool IsPersistent() const override { return false; }
    bool IsProfileReady() const override { return GetProfile() != nullptr; }
    bool IsOnline() const override { return false; }
    uint32 GetOnlineCaps() const override { return 0; }
    int GetInputDevice() const override { return 0; }
    const char * GetGamerTag() const override { return "Local player"; }
    bool IsInParty() const override { return false; }
    int GetPartyCount() const override { return 0; }
    bool IsStorageDeviceAvailable() const override { return false; }
    bool StorageSizeAvailable(uint64 bytes, int64 & needed) override
    {
        needed = bytes > static_cast<uint64>(LLONG_MAX) ? LLONG_MAX : static_cast<int64>(bytes);
        return false;
    }
};

class OfflineSignIn final : public idSignInManagerBase
{
  public:
    ~OfflineSignIn() override { Shutdown(); }
    void Pump() override
    {
        if (m_user != nullptr)
        {
            m_user->Pump();
        }
    }
    int GetNumLocalUsers() const override { return m_user == nullptr ? 0 : 1; }
    idLocalUser * GetLocalUserByIndex(int index) override { return index == 0 ? m_user : nullptr; }
    const idLocalUser * GetLocalUserByIndex(int index) const override { return index == 0 ? m_user : nullptr; }
    void RegisterLocalUser(int device) override
    {
        Require(device == 0, "only input device zero is supported");
        if (m_user != nullptr)
        {
            return;
        }
        Require(m_nextHandle != 0, "local user handle exhaustion");
        m_user = new OfflineUser;
        m_user->SetLocalUserHandle(localUserHandle_t(m_nextHandle++));
        session->OnLocalUserSignin(m_user);
        session->OnMasterLocalUserSignin();
    }
    void RemoveLocalUserByIndex(int index) override
    {
        Require(index == 0 && m_user != nullptr, "local user removal");
        session->OnLocalUserSignout(m_user);
        session->OnMasterLocalUserSignout();
        delete m_user;
        m_user = nullptr;
        defaultProfile = nullptr;
    }
    void Shutdown() override
    {
        delete m_user;
        m_user = nullptr;
        defaultProfile = nullptr;
    }
    void SignIn() override { RegisterLocalUser(0); }

  private:
    OfflineUser * m_user = nullptr;
    uint32 m_nextHandle = 1;
};

class OfflineAchievements final : public idAchievementSystem
{
  public:
    void Init() override { m_initialized = true; }
    void Shutdown() override
    {
        users.Clear();
        m_initialized = false;
    }
    bool IsInitialized() override { return m_initialized; }
    void Pump() override {}
    void RegisterLocalUser(idLocalUser * user) override
    {
        Require(user != nullptr && users.Num() == 0, "achievement user registration");
        users.Append(user);
    }
    void RemoveLocalUser(idLocalUser * user) override { users.Remove(user); }
    void AchievementUnlock(idLocalUser * user, int id) override
    {
        Require(m_initialized && user != nullptr && users.FindIndex(user) >= 0 && id >= 0 && id < MAX_ACHIEVEMENTS,
                "local achievement unlock");
        user->GetProfile()->SetAchievement(id);
    }
    void AchievementLock(idLocalUser * user, int id) override
    {
        Require(m_initialized && user != nullptr && users.FindIndex(user) >= 0 && id >= 0 && id < MAX_ACHIEVEMENTS,
                "local achievement lock");
        user->GetProfile()->ClearAchievement(id);
    }
    bool GetAchievementState(idLocalUser * user, idArray<bool, MAX_ACHIEVEMENTS> & bits) const override
    {
        if (!m_initialized || user == nullptr || users.FindIndex(user) < 0)
        {
            bits.Zero();
            return false;
        }
        for (int index = 0; index < MAX_ACHIEVEMENTS; ++index)
        {
            bits[index] = user->GetProfile()->GetAchievement(index);
        }
        return true;
    }

  private:
    bool m_initialized = false;
};

class OfflineLobby final : public idLobbyBase
{
  public:
    explicit OfflineLobby(byte type)
        : m_type(type) {}
    void Open(const idMatchParameters & parameters)
    {
        m_parms = parameters;
        m_active = true;
        m_loaded = false;
    }
    void Reset()
    {
        m_active = false;
        m_loaded = false;
        m_parms = idMatchParameters();
    }
    void Loaded() { m_loaded = true; }
    void BeginLoading() { m_loaded = false; }
    lobbyUserID_t UserId() const
    {
        const idLocalUser * user = session->GetSignInManager().GetMasterLocalUser();
        return m_active && user != nullptr ? lobbyUserID_t(user->GetLocalUserHandle(), m_type) : lobbyUserID_t();
    }
    bool Valid(lobbyUserID_t id) const { return m_active && id.IsValid() && id == UserId(); }
    bool IsHost() const override { return m_active; }
    bool IsPeer() const override { return false; }
    bool HasActivePeers() const override { return false; }
    int GetNumLobbyUsers() const override { return m_active ? 1 : 0; }
    int GetNumActiveLobbyUsers() const override { return GetNumLobbyUsers(); }
    bool IsLobbyUserConnected(int index) const override { return m_active && index == 0; }
    lobbyUserID_t GetLobbyUserIdByOrdinal(int index) const override { return index == 0 ? UserId() : lobbyUserID_t(); }
    int GetLobbyUserIndexFromLobbyUserID(lobbyUserID_t id) const override { return Valid(id) ? 0 : -1; }
    const char * GetLobbyUserName(lobbyUserID_t id) const override { return Valid(id) ? "Local player" : ""; }
    bool IsLobbyUserValid(lobbyUserID_t id) const override { return Valid(id); }
    bool IsLobbyUserLoaded(lobbyUserID_t id) const override { return Valid(id) && m_loaded; }
    bool LobbyUserHasFirstFullSnap(lobbyUserID_t id) const override { return Valid(id) && m_loaded; }
    int GetLobbyUserSkinIndex(lobbyUserID_t id) const override
    {
        Require(Valid(id), "skin query");
        return 0;
    }
    bool GetLobbyUserWeaponAutoReload(lobbyUserID_t id) const override
    {
        Require(Valid(id), "auto reload query");
        return true;
    }
    bool GetLobbyUserWeaponAutoSwitch(lobbyUserID_t id) const override
    {
        Require(Valid(id), "auto switch query");
        return true;
    }
    int GetLobbyUserLevel(lobbyUserID_t id) const override
    {
        Require(Valid(id), "level query");
        return 0;
    }
    int GetLobbyUserQoS(lobbyUserID_t id) const override
    {
        Require(Valid(id), "QoS query");
        return QOS_RESULT_GREAT;
    }
    int GetLobbyUserTeam(lobbyUserID_t id) const override
    {
        Require(Valid(id), "team query");
        return 0;
    }
    bool SetLobbyUserTeam(lobbyUserID_t id, int team) override { return Valid(id) && team == 0; }
    int GetLobbyUserPartyToken(lobbyUserID_t id) const override
    {
        Require(Valid(id), "party token query");
        return 0;
    }
    idLocalUser * GetLocalUserFromLobbyUser(lobbyUserID_t id) override
    {
        return Valid(id) ? session->GetSignInManager().GetMasterLocalUser() : nullptr;
    }
    idPlayerProfile * GetProfileFromLobbyUser(lobbyUserID_t id) override
    {
        idLocalUser * user = GetLocalUserFromLobbyUser(id);
        return user == nullptr ? nullptr : user->GetProfile();
    }
    int GetNumLobbyUsersOnTeam(int team) const override { return team == 0 ? GetNumLobbyUsers() : 0; }
    int PeerIndexFromLobbyUser(lobbyUserID_t) const override { return -1; }
    int PeerIndexForHost() const override { return -1; }
    bool GetLobbyUserIsBot(lobbyUserID_t) const override { return false; }
    const char * GetHostUserName() const override { return m_active ? "Local player" : ""; }
    const idMatchParameters & GetMatchParms() const override { return m_parms; }
    bool IsLobbyFull() const override { return m_active; }
    bool EnsureAllPeersHaveBaseState() override { return true; }
    bool AllPeersInGame() const override { return true; }
    int GetNumConnectedPeers() const override { return 0; }
    int GetNumConnectedPeersInGame() const override { return 0; }
    int PeerIndexOnHost() const override { return -1; }
    bool IsPeerDisconnected(int) const override { return true; }
    bool AllPeersHaveStaleSnapObj(int) override { return true; }
    bool AllPeersHaveExpectedSnapObj(int) override { return true; }
    OFFLINE_UNSUPPORTED(void, SendReliable, (int, idBitMsg &, bool, peerMask_t))
    OFFLINE_UNSUPPORTED(void, SendReliableToLobbyUser, (lobbyUserID_t, int, idBitMsg &))
    OFFLINE_UNSUPPORTED(void, SendReliableToHost, (int, idBitMsg &))
    OFFLINE_UNSUPPORTED(void, KickLobbyUser, (lobbyUserID_t))
    OFFLINE_UNSUPPORTED(void, EnableSnapshotsForLobbyUser, (lobbyUserID_t))
    OFFLINE_UNSUPPORTED(int, GetPeerTimeSinceLastPacket, (int) const)
    OFFLINE_UNSUPPORTED(lobbyUserID_t, AllocLobbyUserSlotForBot, (const char *))
    OFFLINE_UNSUPPORTED(void, RemoveBotFromLobbyUserList, (lobbyUserID_t))
    OFFLINE_UNSUPPORTED(void, RefreshSnapObj, (int))
    OFFLINE_UNSUPPORTED(void, MarkSnapObjDeleted, (int))
    OFFLINE_UNSUPPORTED(void, AddSnapObjTemplate, (int, idBitMsg &))
    OFFLINE_UNSUPPORTED(void, DrawDebugNetworkHUD, () const)
    OFFLINE_UNSUPPORTED(void, DrawDebugNetworkHUD2, () const)
    OFFLINE_UNSUPPORTED(void, DrawDebugNetworkHUD_ServerSnapshotMetrics, (bool))

  private:
    byte m_type;
    bool m_active = false;
    bool m_loaded = false;
    idMatchParameters m_parms;
};

class OfflineSession final : public idSession
{
  public:
    OfflineSession()
        : m_party(0), m_game(1)
    {
        signInManager = &m_signIn;
        achievementSystem = &m_achievements;
    }
    ~OfflineSession() override
    {
        Shutdown();
        signInManager = nullptr;
        achievementSystem = nullptr;
    }
    void Initialize() override
    {
        Require(!m_initialized, "repeated session initialization");
        m_initialized = true;
        m_achievements.Init();
        m_signIn.SetDesiredLocalUsers(1, 1);
        m_signIn.RegisterLocalUser(0);
    }
    void Shutdown() override
    {
        m_party.Reset();
        m_game.Reset();
        m_signIn.Shutdown();
        m_achievements.Shutdown();
        m_state = PRESS_START;
        m_initialized = false;
        m_saveSlot.Clear();
    }
    bool Initialized() const { return m_initialized; }
    void InitializeSoundRelatedSystems() override { Unsupported("sound device initialization"); }
    void ShutdownSoundRelatedSystems() override { Unsupported("sound device shutdown"); }
    void CreatePartyLobby(const idMatchParameters & parms) override
    {
        Validate(parms);
        Require(m_state == IDLE || m_state == PARTY_LOBBY, "create party lobby state");
        m_party.Open(parms);
        m_state = PARTY_LOBBY;
    }
    void CreateMatch(const idMatchParameters & parms) override
    {
        Validate(parms);
        Require(m_state == IDLE || m_state == PARTY_LOBBY || m_state == GAME_LOBBY, "create match state");
        m_game.Open(parms);
        m_state = GAME_LOBBY;
    }
    void UpdateMatchParms(const idMatchParameters & parms) override
    {
        Validate(parms);
        Require(m_state == GAME_LOBBY, "update match state");
        m_game.Open(parms);
    }
    void UpdatePartyParms(const idMatchParameters & parms) override
    {
        Validate(parms);
        Require(m_state == PARTY_LOBBY, "update party state");
        m_party.Open(parms);
    }
    void StartMatch() override
    {
        Require(m_state == GAME_LOBBY && m_loadingId != INT_MAX, "start match state/loading ID");
        ++m_loadingId;
        m_game.BeginLoading();
        m_state = LOADING;
    }
    void LoadingFinished() override
    {
        Require(m_state == LOADING, "loading completion state");
        m_game.Loaded();
        m_state = INGAME;
    }
    void EndMatch(bool) override
    {
        Require(m_state == INGAME, "end match state");
        m_state = GAME_LOBBY;
    }
    void QuitMatch() override { QuitMatchToTitle(); }
    void QuitMatchToTitle() override
    {
        Require(m_initialized, "quit before session initialization");
        m_party.Reset();
        m_game.Reset();
        m_state = m_signIn.GetNumLocalUsers() == 0 ? PRESS_START : IDLE;
    }
    void Cancel() override { QuitMatchToTitle(); }
    void MoveToPressStart() override
    {
        QuitMatchToTitle();
        m_signIn.Shutdown();
        m_achievements.Shutdown();
        m_achievements.Init();
        m_state = PRESS_START;
    }
    void FinishDisconnect() override { QuitMatchToTitle(); }
    sessionState_t GetBackState() override { return m_state == PRESS_START ? PRESS_START : IDLE; }
    bool IsCurrentLobbyMigrating() const override { return false; }
    bool IsLosingConnectionToHost() const override { return false; }
    bool WasMigrationGame() const override { return false; }
    bool ShouldRelaunchMigrationGame() const override { return false; }
    bool WasGameLobbyCoalesced() const override { return false; }
    bool GetMatchParamUpdate(int & peer, int & message) override
    {
        peer = -1;
        message = -1;
        return false;
    }
    void Pump() override { Require(m_initialized, "pump before session initialization"); }
    bool IsPlatformPartyInLobby() override { return false; }
    idLobbyBase & GetPartyLobbyBase() override { return m_party; }
    idLobbyBase & GetGameLobbyBase() override { return m_game; }
    idLobbyBase & GetActingGameStateLobbyBase() override { return m_game; }
    idLobbyBase & GetActivePlatformLobbyBase() override { return m_state == PARTY_LOBBY ? m_party : m_game; }
    idLobbyBase & GetLobbyFromLobbyUserID(lobbyUserID_t id) override
    {
        Require(id.GetLobbyType() <= 1, "lobby type query");
        return id.GetLobbyType() == 0 ? m_party : m_game;
    }
    idPlayerProfile * GetProfileFromMasterLocalUser() override
    {
        idLocalUser * user = m_signIn.GetMasterLocalUser();
        return user == nullptr ? nullptr : user->GetProfile();
    }
    bool ProcessInputEvent(const sysEvent_t *) override { return false; }
    float GetUpstreamDropRate() override { return 0.0f; }
    float GetUpstreamQueueRate() override { return 0.0f; }
    int GetQueuedBytes() override { return 0; }
    int GetLoadingID() override { return m_loadingId; }
    bool IsAboutToLoad() const override { return m_state == LOADING; }
    const char * GetLocalUserName(int index) const override { return index == 0 && m_signIn.GetNumLocalUsers() > 0 ? "Local player" : ""; }
    sessionState_t GetState() const override { return m_state; }
    const char * GetStateString() const override
    {
        switch (m_state)
        {
        case PRESS_START:
            return "PRESS_START";
        case IDLE:
            return "IDLE";
        case PARTY_LOBBY:
            return "PARTY_LOBBY";
        case GAME_LOBBY:
            return "GAME_LOBBY";
        case LOADING:
            return "LOADING";
        case INGAME:
            return "INGAME";
        default:
            Unsupported("invalid state");
        }
    }
    int NumServers() const override { return 0; }
    const serverInfo_t * ServerInfo(int) const override { return nullptr; }
    const idList<idStr> * ServerPlayerList(int) override { return nullptr; }
    void EnumerateDownloadableContent() override { Unsupported("downloadable content enumeration"); }
    int GetNumContentPackages() const override { return 0; }
    int GetContentPackageIndexForID(int) const override { return -1; }
    bool GetSystemMarketplaceHasNewContent() const override { return false; }
    float GetTitleStorageFloat(const char *, float fallback) const override { return fallback; }
    int GetTitleStorageInt(const char *, int fallback) const override { return fallback; }
    bool GetTitleStorageBool(const char *, bool fallback) const override { return fallback; }
    const char * GetTitleStorageString(const char *, const char * fallback) const override { return fallback; }
    using idSession::GetTitleStorageBool;
    using idSession::GetTitleStorageFloat;
    using idSession::GetTitleStorageInt;
    using idSession::GetTitleStorageString;
    bool IsTitleStorageLoaded() override { return false; }
    bool IsSaveGameCompletedFromHandle(const saveGameHandle_t &) const override { return false; }
    bool IsEnumerating() const override { return false; }
    saveGameHandle_t GetEnumerationHandle() const override { return 0; }
    const saveGameDetailsList_t & GetEnumeratedSavegames() const override { Unsupported("save enumeration"); }
    void SetCurrentSaveSlot(const char * slot) override
    {
        Require(slot != nullptr && idStr::Length(slot) < MAX_OSPATH, "save slot length");
        m_saveSlot = slot;
    }
    const char * GetCurrentSaveSlot() const override { return m_saveSlot.c_str(); }
    bool IsDLCAvailable(const char *) override { return false; }
    void UpdateRichPresence() override { /* No platform presence exists for offline users. */ }
    int GetInputRouting(int routing[MAX_INPUT_DEVICES]) override
    {
        for (int index = 0; index < MAX_INPUT_DEVICES; ++index)
        {
            routing[index] = -1;
        }
        if (m_signIn.GetNumLocalUsers() == 0)
        {
            return 0;
        }
        routing[0] = 0;
        return 1;
    }
    void UpdateSignInManager() override
    {
        Require(m_initialized, "sign-in pump before initialization");
        m_signIn.Pump();
    }
    bool IsSystemUIShowing() const override { return false; }
    void SetSystemUIShowing(bool show) override
    {
        if (show)
        {
            Unsupported("system UI");
        }
    }
    voiceState_t GetLobbyUserVoiceState(lobbyUserID_t) override { return VOICECHAT_STATE_NO_MIC; }
    voiceStateDisplay_t GetDisplayStateFromVoiceState(voiceState_t) const override { return VOICECHAT_DISPLAY_NONE; }
    float GetIncomingByteRate() override { return 0.0f; }
    void ClearBootableInvite() override {}
    void ClearPendingInvite() override {}
    bool HasPendingBootableInvite() override { return false; }
    void * GetDiscSwapMPInviteParms() override { return nullptr; }
    bool IsDiscSwapMPInviteRequested() const override { return false; }
    void OnLocalUserSignin(idLocalUser * user) override { m_achievements.RegisterLocalUser(user); }
    void OnLocalUserSignout(idLocalUser * user) override
    {
        m_achievements.RemoveLocalUser(user);
        QuitMatchToTitle();
        m_state = PRESS_START;
    }
    void OnMasterLocalUserSignin() override { m_state = IDLE; }
    void OnMasterLocalUserSignout() override { m_state = PRESS_START; }
    void OnLocalUserProfileLoaded(idLocalUser *) override {}
    OFFLINE_UNSUPPORTED(void, FindOrCreateMatch, (const idMatchParameters &))
    OFFLINE_UNSUPPORTED(void, CreateGameStateLobby, (const idMatchParameters &))
    OFFLINE_UNSUPPORTED(void, MatchFinished, ())
    OFFLINE_UNSUPPORTED(void, SetSessionOption, (sessionOption_t))
    OFFLINE_UNSUPPORTED(void, ClearSessionOption, (sessionOption_t))
    OFFLINE_UNSUPPORTED(bool, GetMigrationGameData, (idBitMsg &, bool))
    OFFLINE_UNSUPPORTED(bool, GetMigrationGameDataUser, (lobbyUserID_t, idBitMsg &, bool))
    OFFLINE_UNSUPPORTED(void, ProcessSnapAckQueue, ())
    OFFLINE_UNSUPPORTED(void, InviteFriends, ())
    OFFLINE_UNSUPPORTED(void, InviteParty, ())
    OFFLINE_UNSUPPORTED(void, ShowPartySessions, ())
    OFFLINE_UNSUPPORTED(void, ListServers, (const idCallback &))
    OFFLINE_UNSUPPORTED(void, CancelListServers, ())
    OFFLINE_UNSUPPORTED(void, ConnectToServer, (int))
    OFFLINE_UNSUPPORTED(void, ShowServerGamerCardUI, (int))
    OFFLINE_UNSUPPORTED(void, ShowOnlineSignin, ())
    OFFLINE_UNSUPPORTED(void, DropClient, (int, int))
    OFFLINE_UNSUPPORTED(void, JoinAfterSwap, (void *))
    OFFLINE_UNSUPPORTED(int, GetContentPackageID, (int) const)
    OFFLINE_UNSUPPORTED(const char *, GetContentPackagePath, (int) const)
    OFFLINE_UNSUPPORTED(void, ShowSystemMarketplaceUI, () const)
    OFFLINE_UNSUPPORTED(void, SetSystemMarketplaceHasNewContent, (bool))
    OFFLINE_UNSUPPORTED(void, LeaderboardUpload, (lobbyUserID_t, const leaderboardDefinition_t *, const column_t *, const idFile_Memory *))
    OFFLINE_UNSUPPORTED(void, LeaderboardDownload, (int, const leaderboardDefinition_t *, int, int, const idLeaderboardCallback &))
    OFFLINE_UNSUPPORTED(void, LeaderboardDownloadAttachment, (int, const leaderboardDefinition_t *, int64))
    OFFLINE_UNSUPPORTED(void, LeaderboardFlush, ())
    OFFLINE_UNSUPPORTED(void, SetLobbyUserRelativeScore, (lobbyUserID_t, int, int))
    OFFLINE_UNSUPPORTED(saveGameHandle_t, SaveGameSync, (const char *, const saveFileEntryList_t &, const idSaveGameDetails &))
    OFFLINE_UNSUPPORTED(saveGameHandle_t, SaveGameAsync, (const char *, const saveFileEntryList_t &, const idSaveGameDetails &))
    OFFLINE_UNSUPPORTED(saveGameHandle_t, LoadGameSync, (const char *, saveFileEntryList_t &))
    OFFLINE_UNSUPPORTED(saveGameHandle_t, EnumerateSaveGamesSync, ())
    OFFLINE_UNSUPPORTED(saveGameHandle_t, EnumerateSaveGamesAsync, ())
    OFFLINE_UNSUPPORTED(saveGameHandle_t, DeleteSaveGameSync, (const char *))
    OFFLINE_UNSUPPORTED(saveGameHandle_t, DeleteSaveGameAsync, (const char *))
    OFFLINE_UNSUPPORTED(void, CancelSaveGameWithHandle, (const saveGameHandle_t &))
    OFFLINE_UNSUPPORTED(bool, LoadGameCheckDiscNumber, (idSaveLoadParms &))
    OFFLINE_UNSUPPORTED(void, ShowLobbyUserGamerCardUI, (lobbyUserID_t))
    OFFLINE_UNSUPPORTED(void, SendUsercmds, (idBitMsg &))
    OFFLINE_UNSUPPORTED(void, SendSnapshot, (class idSnapShot &))
    OFFLINE_UNSUPPORTED(void, ToggleLobbyUserVoiceMute, (lobbyUserID_t))
    OFFLINE_UNSUPPORTED(void, SetActiveChatGroup, (int))
    OFFLINE_UNSUPPORTED(void, CheckVoicePrivileges, ())
    OFFLINE_UNSUPPORTED(void, SetVoiceGroupsToTeams, ())
    OFFLINE_UNSUPPORTED(void, ClearVoiceGroups, ())
    OFFLINE_UNSUPPORTED(bool, StartOrContinueBandwidthChallenge, (bool))
    OFFLINE_UNSUPPORTED(void, DebugSetPeerSnaprate, (int, int))
    OFFLINE_UNSUPPORTED(void, HandleBootableInvite, (int64))
    OFFLINE_UNSUPPORTED(void, HandleExitspawnInvite, (const lobbyConnectInfo_t &))
    OFFLINE_UNSUPPORTED(void, SetDiscSwapMPInvite, (void *))

  private:
    void Validate(const idMatchParameters & parms) const
    {
        Require(m_initialized && m_signIn.GetNumLocalUsers() == 1, "match requires local user");
        Require(parms.gameMode == GAME_MODE_SINGLEPLAYER && parms.gameMap == GAME_MAP_SINGLEPLAYER && parms.numSlots == 1,
                "only one-player campaign matches are supported");
        Require((parms.matchFlags & ~MATCH_PRIVATE) == 0, "online/stats/party match flags");
        Require(ps2::filesystem::IsRelativePath(parms.mapName.c_str()), "campaign map path");
    }
    OfflineSignIn m_signIn;
    OfflineAchievements m_achievements;
    OfflineLobby m_party;
    OfflineLobby m_game;
    sessionState_t m_state = PRESS_START;
    bool m_initialized = false;
    int m_loadingId = 0;
    idStr m_saveSlot;
};

#undef OFFLINE_UNSUPPORTED
static OfflineSession s_session;

} // namespace

// This replacement is the sole owner of the idSession ABI; desktop session sources are excluded.
idSession::~idSession() = default;
idSession * session = &s_session;

// Offline profiles have no save processors or storage allocation. The profile itself is engine-owned.
idProfileMgr::idProfileMgr()
    : user(nullptr), profile(nullptr), handle(0) {}
idProfileMgr::~idProfileMgr() = default;
void idProfileMgr::Init(idLocalUser * localUser)
{
    user = localUser;
    handle = 0;
}
idPlayerProfile * idProfileMgr::GetProfile()
{
    Require(user != nullptr, "profile manager owner");
    if (profile == nullptr)
    {
        profile = idPlayerProfile::CreatePlayerProfile(user->GetInputDevice());
    }
    return profile;
}
void idProfileMgr::Pump()
{
    if (profile == nullptr)
    {
        return;
    }
    if (profile->GetRequestedState() == idPlayerProfile::LOAD_REQUESTED)
    {
        profile->SetDefaults();
        profile->SetState(idPlayerProfile::IDLE);
        profile->SetRequestedState(idPlayerProfile::IDLE);
        session->OnLocalUserProfileLoaded(user);
    }
    else if (profile->GetRequestedState() == idPlayerProfile::SAVE_REQUESTED)
    {
        ps2::Log(ps2::LogLevel::Error, "[D3BFG] profile persistence unsupported; transient state retained\n");
        profile->SetState(idPlayerProfile::ERR);
        profile->SetRequestedState(idPlayerProfile::IDLE);
    }
}
void idAchievementSystem::SyncAchievementBits(idLocalUser *)
{
    // There is no online achievement cache to synchronize. Local bits remain in the transient profile.
}

namespace ps2::offline
{
void Init() { session->Initialize(); }
void Shutdown() { session->Shutdown(); }
void Frame()
{
    session->UpdateSignInManager();
    session->Pump();
}
bool IsInitialized() { return s_session.Initialized(); }
} // namespace ps2::offline
