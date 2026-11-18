// ================================================================================================
// File: cook_engine.h
// Brief: Starts the engine services the host script cook gives id's compiler: output, errors and
// files read through the cook's own source reader.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstdint>
#include <vector>

namespace cook
{

// Fills data with the named source and returns true, or returns false when it does not exist.
using SourceReader = bool (*)(const char * name, std::vector<std::uint8_t> & data);

// Points idLib's and the engine's common and fileSystem at the cook's implementations.
void InitEngine(SourceReader reader, bool verbose);

} // namespace cook
