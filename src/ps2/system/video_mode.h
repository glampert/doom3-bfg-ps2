// ================================================================================================
// File: video_mode.h
// Brief: Share the interlaced NTSC/PAL geometry used by GS startup and presentation.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once
#include <tamtypes.h>

namespace ps2::video
{
enum class Standard : u8
{
    Ntsc,
    Pal
};
Standard ConsoleStandard();
const char * Name(Standard standard);
int VisibleLines(Standard standard);
int GraphMode(Standard standard);
bool IsValid(Standard standard);
} // namespace ps2::video
