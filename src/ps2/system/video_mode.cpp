// ================================================================================================
// File: video_mode.cpp
// Brief: Resolve the console ROM region without requiring engine cvars or a display device, and
//        hold the table of frame-buffer resolutions each TV standard can show.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/video_mode.h"
#include "ps2/common.h"
#include <graph.h>
#include <rom0_info.h>

namespace ps2::system::video
{
namespace
{

// The widths the display magnifies by a whole step (2560 / 5 and 2560 / 4), each with the
// standard's full interlaced lines. 512 is the default: 640 costs 25% more frame-buffer VRAM and
// fill (RENDERING_ARCHITECTURE.md 4.2).
static constexpr Resolution kNtsc[] = { { 512, 448 }, { 640, 448 } };
static constexpr Resolution kPal[]  = { { 512, 512 }, { 640, 512 } };

static_assert(ArrayLength(kNtsc) == ArrayLength(kPal), "both standards offer the same choices");

} // namespace

Standard ConsoleStandard()
{
    char romName[16] = {};
    GetRomName(romName);
    return romName[4] == 'E' ? Standard::Pal : Standard::Ntsc;
}

bool IsValid(Standard standard)
{
    return standard == Standard::Ntsc || standard == Standard::Pal;
}

const char * Name(Standard standard)
{
    return standard == Standard::Pal ? "PAL" : "NTSC";
}

int VisibleLines(Standard standard)
{
    return standard == Standard::Pal ? 512 : 448;
}

int GraphMode(Standard standard)
{
    return standard == Standard::Pal ? GRAPH_MODE_PAL : GRAPH_MODE_NTSC;
}

int ResolutionCount(Standard standard)
{
    (void)standard;
    return ArrayLength(kNtsc);
}

Resolution ResolutionAt(Standard standard, int index)
{
    PS2_AssertMsg(index >= 0 && index < ResolutionCount(standard), "video resolution index out of range");
    return standard == Standard::Pal ? kPal[index] : kNtsc[index];
}

int FindResolution(Standard standard, Resolution resolution)
{
    for (int index = 0; index < ResolutionCount(standard); ++index)
    {
        const Resolution candidate = ResolutionAt(standard, index);

        if (candidate.width == resolution.width && candidate.height == resolution.height)
        {
            return index;
        }
    }

    return -1;
}

bool IsValid(Interlace interlace)
{
    return interlace == Interlace::Field || interlace == Interlace::Frame;
}

const char * InterlaceName(Interlace interlace)
{
    return interlace == Interlace::Frame ? "FRAME" : "FIELD";
}

int FrameHeight(Resolution resolution, Interlace interlace)
{
    return interlace == Interlace::Frame ? resolution.height / 2 : resolution.height;
}

} // namespace ps2::system::video
