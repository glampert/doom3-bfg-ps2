// ================================================================================================
// File: video_mode.cpp
// Brief: Resolve the console ROM region without requiring engine cvars or a display device.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/video_mode.h"
#include <graph.h>
#include <rom0_info.h>

namespace ps2::video
{
Standard ConsoleStandard()
{
    char romName[16] = {};
    GetRomName(romName);
    return romName[4] == 'E' ? Standard::Pal : Standard::Ntsc;
}
bool IsValid(Standard standard) { return standard == Standard::Ntsc || standard == Standard::Pal; }
const char * Name(Standard standard) { return standard == Standard::Pal ? "PAL" : "NTSC"; }
int VisibleLines(Standard standard) { return standard == Standard::Pal ? 512 : 448; }
int GraphMode(Standard standard) { return standard == Standard::Pal ? GRAPH_MODE_PAL : GRAPH_MODE_NTSC; }
} // namespace ps2::video
