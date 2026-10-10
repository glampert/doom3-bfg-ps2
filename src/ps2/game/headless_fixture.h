// ================================================================================================
// File: headless_fixture.h
// Brief: Select an authored, playerless game simulation before presentation services exist.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

namespace ps2::gamefixture
{
// Explicit process-wide selection before Common/game startup; never selected by ordinary boot.
void Enable();
bool IsEnabled();
} // namespace ps2::gamefixture
