// ================================================================================================
// File: vu_memory.h
// Brief: The VU1 data memory map (qword addresses), shared by the microprograms (through vclpp)
//        and the EE code that fills it (through ps2/renderer/vu.h). Only plain #defines and
//        // comments, which both preprocessors accept; the kVu prefix stands in for a namespace.
//
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

// Frame constants: MVP rows, GS scale and offset, clip scale, fog.
#define kVuFrameConstantsAddr 0

// Per-pass GIF tag and A+D templates. The first is a null GIF tag (NLOOP 0, EOP 1): the drain
// guard passthrough.vcl kicks so a program ends only once its packet has left VU memory.
#define kVuGifTemplatesAddr 8
#define kVuNullGifTagAddr   8
