// ================================================================================================
// File: shell_stub.cpp
// Brief: Supply the deferred native shell contract while rejecting presentation and save enumeration.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/log.h"
#include <idlib/precompiled.h>

// Menu contracts depend on game, widget, SWF and save-description declarations.
#include <d3xp/Game_local.h>

namespace
{
[[noreturn]] PS2_COLD_FUNC void Unsupported(const char * method)
{
    ps2::FatalError("shell capability unavailable: %s", method);
}
} // namespace

void idMenuHandler_Shell::Cleanup()
{
    if (gui != nullptr || cmdBar != nullptr || menuBar != nullptr || pacifier != nullptr ||
        introGui != nullptr || children.Num() != 0)
    {
        Unsupported("idMenuHandler_Shell::Cleanup with active resources");
    }
    for (idMenuScreen * screen : menuScreens)
    {
        if (screen != nullptr)
        {
            Unsupported("idMenuHandler_Shell::Cleanup with active screen");
        }
    }
    mpGameModes.Clear();
    mpGameMaps.Clear();
    navOptions.Clear();
    state = nextState = SHELL_STATE_INVALID;
    timeRemaining = nextPeerUpdateMs = 0;
    waitForBinding = continueWaitForEnumerate = showingIntro = false;
    waitBind = nullptr;
}

#define SHELL_UNSUPPORTED(result, owner, method, parameters) \
    result owner::method parameters { Unsupported(#owner "::" #method); }

SHELL_UNSUPPORTED(void, idMenuHandler_Shell, Update, ())
SHELL_UNSUPPORTED(void, idMenuHandler_Shell, ActivateMenu, (bool))
SHELL_UNSUPPORTED(void, idMenuHandler_Shell, Initialize, (const char *, idSoundWorld *))
SHELL_UNSUPPORTED(bool, idMenuHandler_Shell, HandleAction, (idWidgetAction &, const idWidgetEvent &, idMenuWidget *, bool))
SHELL_UNSUPPORTED(idMenuScreen *, idMenuHandler_Shell, GetMenuScreen, (int))
SHELL_UNSUPPORTED(bool, idMenuHandler_Shell, HandleGuiEvent, (const sysEvent_t *))
SHELL_UNSUPPORTED(void, idMenuHandler_Shell, SetCanContinue, (bool))
SHELL_UNSUPPORTED(void, idMenuHandler_Shell, UpdateSavedGames, ())
SHELL_UNSUPPORTED(void, idMenuHandler_Shell, UpdateLeaderboard, (const idLeaderboardCallback *))
SHELL_UNSUPPORTED(void, idMenuScreen_Shell_Load, UpdateSaveEnumerations, ())
SHELL_UNSUPPORTED(void, idMenuScreen_Shell_Save, UpdateSaveEnumerations, ())

#undef SHELL_UNSUPPORTED
