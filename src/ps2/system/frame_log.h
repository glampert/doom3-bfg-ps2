// ================================================================================================
// File: frame_log.h
// Brief: The frame log: per-frame timings and draw, texture and loader counters held in EE memory
//        while a run records, then written once to the package's framelog.txt as FLOG rows
//        (quake2-ps2's format, read by src/tools/scripts/frame_log/). It is how a timing leaves a
//        console, which has no stdout anyone reads: one file write after the measured frames,
//        where a log line per frame would cost a USB write each (about 23 ms). Release builds keep
//        it: the hardware numbers that matter are release ones.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include <tamtypes.h>

namespace ps2::system::frame_log
{

// The points of a Common frame the presentation marks, in order. The frame starts at BeginFrame;
// Present marks where drawing starts, View where the 3D views are built, Ui where the screens and
// console are, and Presented once the chain ran and the frame was flipped.
enum class Point : u8
{
    Present,
    View,
    Ui,
    Presented,
    Count
};

// Holds up to 'frames' rows (1 to kMaxFrames) in a TAG_DEBUG allocation and starts recording at the
// next frame; 'label' (the map and its load time, say) goes into the file's notes. False, logged,
// when a log already records or there is no memory for it.
static constexpr u32 kMaxFrames = 16384;
PS2_COLD_FUNC bool Start(u32 frames, const char * label);

// Writes the rows recorded so far to <package>/framelog.txt and frees them (a full log stops by
// itself). False, logged, when nothing recorded or the file could not be written.
PS2_COLD_FUNC bool Stop();

bool Recording();

// A Common frame starts: the frame before it is complete and becomes a row.
void BeginFrame();

void Mark(Point point);

// The view the frame drew from: its eye and yaw, written with the row.
void SetView(float x, float y, float z, float yaw);

// Logs whether a log records and how many rows it holds.
PS2_COLD_FUNC void LogStatus();

} // namespace ps2::system::frame_log
