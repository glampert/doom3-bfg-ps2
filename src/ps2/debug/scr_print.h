// ================================================================================================
// File: scr_print.h
// Brief: Crude text on screen for the crash reports (crash_report.h): it takes the GS over with a
//        private 640x224 frame buffer and blits glyphs by hand, so it works after the renderer has
//        fallen over. Assertion builds only.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include "ps2/system/video_mode.h"
#include <tamtypes.h>

#if PS2_D3BFG_ASSERTS

namespace ps2::debug
{

// Glyphs sit in cells of kScrCharSize + 2 pixels of the 640x224 buffer: 64 columns, 22 rows. They
// go up as HOST->LOCAL transfers, which the scissor does not clip, so the bounds are checked.
static constexpr int kScrCharSize = 8;
static constexpr int kScrMaxX = 64;
static constexpr int kScrMaxY = 22;

// The TV standard the screen drives: the console's own until the GS starts in one (gs::Init sets
// it), since a screen in a standard the TV can't take would show nothing at all.
void ScrSetStandard(system::video::Standard standard);

// Resets every path into the GS and installs the private buffer, cleared to the background.
// Called by the first print otherwise.
PS2_COLD_FUNC void ScrInit();

PS2_COLD_FUNC void ScrPrintChar(int x, int y, u32 color, int ch);

// Prints at the cursor in the text and background colors, moving it and handling '\n' and '\t'.
// Past the last row it wraps to the first, clearing each line it reaches.
PS2_COLD_FUNC PS2_PRINTF_FUNC(1, 2) void ScrPrintf(const char * format, ...);

int ScrGetPrintPosX();
int ScrGetPrintPosY();
void ScrSetPrintPos(int x, int y);

// The background for ScrPrintf and ScrPrintChar, the text color for ScrPrintf; 0xAABBGGRR.
void ScrSetBgColor(u32 color);
u32 ScrGetBgColor();
void ScrSetTextColor(u32 color);
u32 ScrGetTextColor();

// ScrClear also moves the cursor to (0, 0); ScrClearLine leaves it.
PS2_COLD_FUNC void ScrClear();
PS2_COLD_FUNC void ScrClearLine(int y);

} // namespace ps2::debug

#endif // PS2_D3BFG_ASSERTS
