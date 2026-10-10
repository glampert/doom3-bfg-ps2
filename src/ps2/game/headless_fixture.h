// ================================================================================================
// File: headless_fixture.h
// Brief: Select a bounded authored game simulation before presentation services exist.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

class idTypeInfo;
class idDict;

namespace ps2::gamefixture
{
// Explicit process-wide selection before Common/game startup; never selected by ordinary boot.
void Enable();
bool IsEnabled();
// Validate before the native factory/registration can allocate media or overwrite a client slot.
void ValidatePlayerSpawn(const idTypeInfo & type, const idDict * args);
void ValidateFrame();
} // namespace ps2::gamefixture
