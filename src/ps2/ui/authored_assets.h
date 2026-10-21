// ================================================================================================
// File: authored_assets.h
// Brief: Admit exact bounded authored UI inputs before the legacy native resource parsers run.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once
class idFile;

namespace ps2::ui::authored
{
// Returns an owned read-only memory file. Unknown, missing or changed resources fail explicitly.
idFile * OpenAsset(const char * path);
} // namespace ps2::ui::authored
