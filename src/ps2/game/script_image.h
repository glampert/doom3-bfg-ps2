// ================================================================================================
// File: script_image.h
// Brief: Loads the cooked script program (.d3sp) into idProgram: checks every record, then points
// the program at the image's statements and defs in place, so nothing is compiled at boot.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"

class idProgram;
class idStr;

namespace ps2::game::script
{

class ImageLoader final
{
public:
    // Loads the image at the game-relative path into an empty program. Returns false with the
    // reason when the file is missing or malformed, or its events differ from the game's.
    PS2_COLD_FUNC static bool Load(idProgram & program, const char * path, idStr & error);
};

// The production check variant (make SCRIPT_CHECK=1), which still compiles at boot: logs the size
// and FNV-1a of the image the host cook's writer makes of the compiled program, so the runner can
// hold the host cook's file to it.
PS2_COLD_FUNC void ReportCompiledImage(const idProgram & program);

} // namespace ps2::game::script
