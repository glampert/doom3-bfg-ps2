// ================================================================================================
// File: save_metadata.cpp
// Brief: Preserve native save-description ownership without enabling storage or retry dialogs.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/log.h"
#include <idlib/precompiled.h>
#include <sys/sys_savegame.h>

idCVar saveGame_enable("saveGame_enable", "0", CVAR_BOOL | CVAR_ROM,
                       "save storage is unavailable; changing this value does not enable the save manager");

idSaveGameDetails::idSaveGameDetails() { Clear(); }

void idSaveGameDetails::Clear()
{
    descriptors.Clear();
    damaged = false;
    date = 0;
    slotName.Clear();
}

// Preserve the native spelling and signature; do not claim a retry was queued.
void idSaveGameManager::ShowRetySaveDialog(const char *, const int64)
{
    ps2::FatalError("save capability unavailable: idSaveGameManager::ShowRetySaveDialog");
}
