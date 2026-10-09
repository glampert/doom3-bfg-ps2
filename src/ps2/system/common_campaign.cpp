// ================================================================================================
// File: common_campaign.cpp
// Brief: Preserve offline Common state cleanup while rejecting deferred network, demo and shell entry.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/log.h"
#include <idlib/precompiled.h>

// The shared class has the same layout in core and resident campaign builds.
#include <framework/Common_local.h>

namespace
{
[[noreturn]] PS2_COLD_FUNC void Unsupported(const char * method)
{
    ps2::FatalError("Common campaign capability unavailable: %s", method);
}
} // namespace

// Preserve values referenced by retained frame code; they never enable network services.
idCVar net_clientMaxPrediction("net_clientMaxPrediction", "5000", CVAR_SYSTEM | CVAR_INTEGER | CVAR_NOCHEAT,
                               "maximum client prediction in milliseconds; online services are unavailable");
idCVar net_ucmdRate("net_ucmdRate", "40", CVAR_SYSTEM | CVAR_INTEGER,
                    "usercmd send interval in milliseconds; online services are unavailable");

bool idCommonLocal::IsMultiplayer() { return false; }
bool idCommonLocal::IsServer() { return false; }
bool idCommonLocal::IsClient() { return false; }

void idCommonLocal::ResetNetworkingState()
{
    // A local map reload still owns snapshot/interpolation and user-command storage.
    // Reset those value types even though this backend never creates network peers.
    snapTime = snapTimeWrite = snapCurrentTime = 0;
    snapCurrentResidual = snapTimeBuffered = effectiveSnapRate = 0.0f;
    totalBufferedTime = totalRecvTime = 0;
    readSnapshotIndex = writeSnapshotIndex = 0;
    snapRate = 100000;
    optimalTimeBuffered = optimalTimeBufferedWindow = 0.0f;
    optimalPCTBuffer = 0.5f;
    for (int index = 0; index < receivedSnaps.Num(); ++index)
    {
        receivedSnaps[index].Clear();
    }
    userCmdMgr.SetDefaults();
    snapCurrent.localTime = snapPrevious.localTime = -1;
    snapCurrent.serverTime = snapPrevious.serverTime = -1;
    oldss.Clear();
    gameFrame = clientPrediction = nextUsercmdSendTime = nextSnapshotSendTime = 0;
}

void idCommonLocal::StopPlayingRenderDemo()
{
    if (readDemo != nullptr)
    {
        Unsupported("StopPlayingRenderDemo with active demo");
    }
    timeDemo = TD_NO;
}
void idCommonLocal::StopRecordingRenderDemo()
{
    if (writeDemo != nullptr)
    {
        Unsupported("StopRecordingRenderDemo with active demo");
    }
}

#define CAMPAIGN_UNSUPPORTED(result, method, parameters) \
    result idCommonLocal::method parameters { Unsupported(#method); }

CAMPAIGN_UNSUPPORTED(int, GetSnapRate, ())
CAMPAIGN_UNSUPPORTED(void, NetReceiveReliable, (int, int, idBitMsg &))
CAMPAIGN_UNSUPPORTED(void, NetReceiveSnapshot, (idSnapShot &))
CAMPAIGN_UNSUPPORTED(void, NetReceiveUsercmds, (int, idBitMsg &))
CAMPAIGN_UNSUPPORTED(void, OnStartHosting, (idMatchParameters &))
CAMPAIGN_UNSUPPORTED(void, InitializeMPMapsModes, ())
CAMPAIGN_UNSUPPORTED(void, StartMenu, (bool))
CAMPAIGN_UNSUPPORTED(void, ExitMenu, ())
CAMPAIGN_UNSUPPORTED(bool, MenuEvent, (const sysEvent_t *))

#undef CAMPAIGN_UNSUPPORTED
