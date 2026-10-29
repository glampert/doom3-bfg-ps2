// ================================================================================================
// File: save_metadata.cpp
// Brief: Preserve native save-description ownership without enabling storage, compression or
//        retry dialogs.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/log.h"
#include <idlib/precompiled.h>
#include <sys/sys_savegame.h>

idCVar saveGame_enable("saveGame_enable", "0", CVAR_BOOL | CVAR_ROM,
                       "save storage is unavailable; changing this value does not enable the save manager");

idSaveGameDetails::idSaveGameDetails()
{
    Clear();
}

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

// id's File_SaveGame.cpp streams saves through zlib, which is not linked. Compressed saves return
// with miniz; until then only the members the campaign code names exist, and each one fails.
[[noreturn]] static PS2_COLD_FUNC void SaveFileUnavailable(const char * method)
{
    ps2::FatalError("save capability unavailable: idFile_SaveGamePipelined::%s", method);
}

idFile_SaveGamePipelined::idFile_SaveGamePipelined()
{
    SaveFileUnavailable("idFile_SaveGamePipelined");
}

idFile_SaveGamePipelined::~idFile_SaveGamePipelined()
{
    SaveFileUnavailable("~idFile_SaveGamePipelined");
}

PS2_COLD_FUNC bool idFile_SaveGamePipelined::OpenForReading(idFile *)
{
    SaveFileUnavailable("OpenForReading");
}

PS2_COLD_FUNC bool idFile_SaveGamePipelined::OpenForWriting(idFile *)
{
    SaveFileUnavailable("OpenForWriting");
}

void idFile_SaveGamePipelined::Finish()
{
    SaveFileUnavailable("Finish");
}

int idFile_SaveGamePipelined::Read(void *, int)
{
    SaveFileUnavailable("Read");
}

int idFile_SaveGamePipelined::Write(const void *, int)
{
    SaveFileUnavailable("Write");
}
