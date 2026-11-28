// ================================================================================================
// File: map_data.h
// Brief: ps2_mapdata, a debug command that loads a retail map's game data (its resource container,
// the .map, the collision models and the AAS) the way the game's map load does, with a memory
// checkpoint after each step, then frees it: what M3's map load costs before Common runs it. And
// ps2_verifyAAS, which compares each cooked AAS file with id's text parse of its source.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

namespace ps2::game
{

#if PS2_D3BFG_DEBUG
// Registers ps2_mapdata and ps2_verifyAAS with the command system.
PS2_COLD_FUNC void InitMapData();
#else
inline void InitMapData()
{
}
#endif // PS2_D3BFG_DEBUG

} // namespace ps2::game
