// ================================================================================================
// File: aas_cook.h
// Brief: Cook an .aas file's text into a D3AS file (src/ps2/game/aas_format.h), which the PS2
// streams into idAASFileLocal's lists without its lexer. The parse follows idAASFileLocal::Load
// token for token; its numbers are read to the nearest float, as id's PC parse in doubles did.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace assets
{

// The D3AS bytes for an .aas file's text, checked with the target's own validator; 'geometryCrc'
// receives the CRC its header names.
bool CookAas(std::string_view text, std::vector<std::uint8_t> & output, std::uint32_t & geometryCrc, std::string & error);

} // namespace assets
