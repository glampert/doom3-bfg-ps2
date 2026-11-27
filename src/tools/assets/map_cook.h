// ================================================================================================
// File: map_cook.h
// Brief: Cook a map's entities into a D3ME file (src/ps2/game/map_format.h), which the PS2 loads
// into idMapFile without parsing text. It carries the geometry CRC the map's collision and AAS files
// were built against, read from them: the PS2 parses no brushes to compute one.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "assets/map_entities.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace assets
{

// The CRC a .bcm was built against: its header's big-endian model count, the map name (a
// little-endian length and its characters) and then the big-endian CRC.
bool CollisionGeometryCrc(const std::vector<std::uint8_t> & bcm, std::uint32_t & crc, std::string & error);

// The CRC an .aas file names: the decimal number after its "DewmAAS" version line.
bool AasGeometryCrc(std::string_view text, std::uint32_t & crc, std::string & error);

// The D3ME bytes for 'entities' in order, each pair as the file spells it with trailing whitespace
// stripped (idMapEntity::Parse), and their strings stored once.
bool WriteCookedMap(const std::vector<MapEntity> & entities, std::uint32_t geometryCrc, std::vector<std::uint8_t> & output,
                    std::string & error);

} // namespace assets
