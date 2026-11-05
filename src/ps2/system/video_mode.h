// ================================================================================================
// File: video_mode.h
// Brief: Share the interlaced NTSC/PAL geometry used by GS startup and presentation: the
//        standards, the frame-buffer resolutions each can show and how a field reads the buffer.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <tamtypes.h>

namespace ps2::system::video
{

enum class Standard : u8
{
    Ntsc,
    Pal
};

// SMODE2.FFMD: what each field of the interlaced output reads.
enum class Interlace : u8
{
    Field, // every other line of a full-height buffer, flicker-filtered: the standard's visible lines
    Frame  // every line of a half-height buffer, the same picture on both fields: no interlace flicker
};

// A frame buffer the standard can show. Every TV line is 2560 video clocks, magnified by a whole
// step, so a width must divide 2560 (5 for 512, 4 for 640); the height is the standard's visible
// lines, which FIELD mode draws in full and FRAME mode halves (FrameHeight).
struct Resolution
{
    int width;
    int height;
};

Standard ConsoleStandard();
const char * Name(Standard standard);
int VisibleLines(Standard standard);
int GraphMode(Standard standard);
bool IsValid(Standard standard);

// The standard's resolutions, index 0 the default. FindResolution returns the index of a pair or
// -1 when the standard cannot show it.
int ResolutionCount(Standard standard);
Resolution ResolutionAt(Standard standard, int index);
int FindResolution(Standard standard, Resolution resolution);

bool IsValid(Interlace interlace);
const char * InterlaceName(Interlace interlace); // "FIELD" or "FRAME"

// The lines the frame buffer holds: the resolution's in FIELD mode, half of them in FRAME mode.
int FrameHeight(Resolution resolution, Interlace interlace);

} // namespace ps2::system::video
