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
// state and the GIF tag templates (qwords 0-12): 0-3 the view-projection rows (row vectors),
// 4 GS scale (w: the clipper's plane shrink), 5 GS offset (w: the clipper's distance scale),
// 6 the guard band judgement scale.
#define kVuFrameConstantsAddr 0
#define kVuFrameBlockQwords   13

// The output window state, kept across batches so consecutive batches alternate windows:
// x the window the next packet opens, y the signed step to the other one.
#define kVuWindowStateAddr 7

// Per-pass GIF tag and A+D templates. The first is a null GIF tag (NLOOP 0, EOP 1): the drain
// guard passthrough.vcl kicks so a program ends only once its packet has left VU memory. Then
// the world programs' drawing tags, for strips and for the clipper's triangles, and the lightmap
// pass's in the other GS context, both ST, RGBAQ and XYZ2: DECAL ignores the colour, but the RGBAQ
// write is what stores the Q of a PACKED ST.
#define kVuGifTemplatesAddr     8
#define kVuNullGifTagAddr       8
#define kVuStripTagAddr         9
#define kVuListTagAddr          10
#define kVuLightmapStripTagAddr 11
#define kVuLightmapListTagAddr  12

// The VIF1 double buffer: BASE and OFFSET. A batch, its last write included, fits one half
// before its last 11 qwords, which DisplayList::DrawBatch fills with the batch's state: the
// lightmap page's A+D packet (GIF tag, then the other context's TEX0, whose word 3 is non-zero
// when the batch has a lightmap pass), which the program kicks before that pass; the material's
// A+D packet (GIF tag, TEX0, TEX1, MIPTBP1, MIPTBP2, CLAMP, TEST), kicked first; then the
// batch's strip and list drawing tags. Offsets from XTOP. The two contexts share one CLUT
// buffer, so the page's TEX0 must not load its CLUT before the base pass draws.
#define kVuInputBase       40
#define kVuInputHalfQwords 306
#define kVuBatchLightmapOffset     295
#define kVuBatchLightmapFlagOffset 296
#define kVuBatchStateOffset        297
#define kVuBatchStripTagOffset     304
#define kVuBatchListTagOffset      305

// Two output windows, absolute and shared by both halves: one tag qword, then ST (with Q), RGBAQ
// and XYZ2 per vertex. A program fills one, kicks it and continues
// in the other; an XGKICK stalls while the previous transfer runs, so the window being written has
// always drained.
#define kVuWindowA                652
#define kVuWindowQwords           151
#define kVuWindowVertices         50

// Clip scratch (954-1008): the list path's walk state (954 x entries left, y next stream qword,
// z lanes left, w the previous vertex; 955 x the vertex before, y the address of the pass's
// drawing tag, z the pass, 0 base or 1 lightmap; 956 the rotating stream qword), then the
// clipper's ping-pong buffers of three qwords per corner (clip position, float colour, UV) and its
// survivor count.
#define kVuWalkStateAddr   954
#define kVuWalkPrevAddr    955
#define kVuWalkIndicesAddr 956
#define kVuClipBufA        957
#define kVuClipBufB        981
#define kVuClipCountAddr   1008
