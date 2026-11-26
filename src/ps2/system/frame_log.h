// ================================================================================================
// File: frame_log.h
// Brief: The frame log: per-frame timings and draw, texture and loader counters held in EE memory
//        while a run records, then written once to the package's framelog.txt as FLOG rows
//        (quake2-ps2's format, read by src/tools/scripts/frame_log.py). It is how a timing leaves a
//        console, which has no stdout anyone reads: one file write after the measured frames,
//        where a log line per frame would cost a USB write each (about 23 ms). Built with
//        PS2_D3BFG_FRAME_LOG, on in every configuration until a Profile build carries it: the
//        hardware numbers that matter are release ones.
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

static constexpr u32 kMaxFrames = 16384;

// The package file a log goes to when its Start names none.
static constexpr const char * kDefaultFileName = "framelog.txt";

#if PS2_D3BFG_FRAME_LOG
// Holds up to 'frames' rows (1 to kMaxFrames) in a TAG_DEBUG allocation and starts recording at the
// next frame; 'label' (the map and its load time, say) goes into the file's notes, and the rows to
// the package's 'fileName' (kDefaultFileName when null). False, logged, when a log already records
// or there is no memory for it.
PS2_COLD_FUNC bool Start(u32 frames, const char * label, const char * fileName = nullptr);

// Writes the rows recorded so far to the package file Start named and frees them (a full log stops
// by itself). False, logged, when nothing recorded or the file could not be written.
PS2_COLD_FUNC bool Stop();

bool Recording();

// The package file the log records to: valid while it records.
const char * FileName();

// A Common frame starts: the frame before it is complete and becomes a row.
void BeginFrame();

void Mark(Point point);

// The view the frame drew from: its eye and yaw, written with the row.
void SetView(float x, float y, float z, float yaw);

// Logs whether a log records and how many rows it holds.
PS2_COLD_FUNC void LogStatus();
#else // PS2_D3BFG_FRAME_LOG
// Built without the frame log, nothing records and the presentation's marks cost nothing.
inline bool Recording()
{
    return false;
}

inline const char * FileName()
{
    return kDefaultFileName;
}

inline bool Stop()
{
    return false;
}

inline void BeginFrame()
{
}

inline void Mark(Point)
{
}

inline void SetView(float, float, float, float)
{
}
#endif // PS2_D3BFG_FRAME_LOG

} // namespace ps2::system::frame_log
