// ================================================================================================
// File: vu_memory.h
// Brief: The VU1 data memory map (qword addresses), shared by the microprograms (through vclpp)
//        and the EE code that fills it (through ps2/renderer/vu.h). Only plain #defines and
//        // comments, which both preprocessors accept; the kVu prefix stands in for a namespace.
//
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

// Frame constants, unpacked once per frame by DisplayList::SetFrameConstants with the window
// state and the GIF tag templates (qwords 0-10): 0-3 the view-projection rows (row vectors),
// 4 GS scale (w: the clipper's plane shrink), 5 GS offset (w: the clipper's distance scale),
// 6 the guard band judgement scale.
#define kVuFrameConstantsAddr 0
#define kVuFrameBlockQwords   11

// The output window state, kept across batches so consecutive batches alternate windows:
// x the window the next packet opens, y the signed step to the other one.
#define kVuWindowStateAddr 7

// Per-pass GIF tag and A+D templates. The first is a null GIF tag (NLOOP 0, EOP 1): the drain
// guard passthrough.vcl kicks so a program ends only once its packet has left VU memory. Then
// the world programs' drawing tags, for strips and for the clipper's triangles.
#define kVuGifTemplatesAddr 8
#define kVuNullGifTagAddr   8
#define kVuStripTagAddr     9
#define kVuListTagAddr      10

// The VIF1 double buffer: BASE and OFFSET. A batch, its last write included, fits one half
// before its last 9 qwords, which DisplayList::DrawBatch fills with the batch's texture state: an
// A+D packet (GIF tag, TEX0, TEX1, MIPTBP1, MIPTBP2, CLAMP, TEST) the program kicks first, then
// the batch's strip and list drawing tags. Offsets from XTOP.
#define kVuInputBase       40
#define kVuInputHalfQwords 306
#define kVuBatchStateOffset    297
#define kVuBatchStripTagOffset 304
#define kVuBatchListTagOffset  305

// Two output windows, absolute and shared by both halves: one tag qword, then ST (with Q), RGBAQ
// and XYZ2 per vertex. A program fills one, kicks it and continues in the other; an XGKICK stalls
// while the previous transfer runs, so the window being written has always drained.
#define kVuWindowA        652
#define kVuWindowQwords   151
#define kVuWindowVertices 50

// Clip scratch (954-1008): the list path's walk state, then the clipper's ping-pong buffers of
// three qwords per corner (clip position, float colour, UV) and its survivor count.
#define kVuWalkStateAddr   954
#define kVuWalkPrevAddr    955
#define kVuWalkIndicesAddr 956
#define kVuClipBufA        957
#define kVuClipBufB        981
#define kVuClipCountAddr   1008
