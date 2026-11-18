// ================================================================================================
// File: cook_events.h
// Brief: Loads events.txt, the game's event registry as the debug production ELF lists it, which
// stands in for the game classes the host script cook does not build.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <string>

namespace cook
{

// Parses the table's text; afterwards idEventDef::FindEvent answers from it.
bool LoadEvents(const std::string & text, std::string & error);

} // namespace cook
