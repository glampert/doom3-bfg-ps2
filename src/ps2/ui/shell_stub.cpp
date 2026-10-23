// ================================================================================================
// File: shell_stub.cpp
// Brief: Supply the deferred load and save screens' enumeration contract, which the retained
// save-information widget calls, while those screens stay outside the reviewed shell.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/log.h"
#include <idlib/precompiled.h>

// Menu contracts depend on game, widget, SWF and save-description declarations.
#include <d3xp/Game_local.h>

void idMenuScreen_Shell_Load::UpdateSaveEnumerations()
{
    ps2::FatalError("shell capability unavailable: idMenuScreen_Shell_Load::UpdateSaveEnumerations");
}

void idMenuScreen_Shell_Save::UpdateSaveEnumerations()
{
    ps2::FatalError("shell capability unavailable: idMenuScreen_Shell_Save::UpdateSaveEnumerations");
}
