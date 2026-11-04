; =================================================================================================
; vu_window.i
;
; Macros the world programs share: the frame constants and the two output windows a GS packet is
; built in. Adapted from quake2-ps2's vu_common.i; the windows here are absolute (vu_memory.h)
; rather than inside each input half, carry one tag qword, and keep their state across batches.
;
; This source code is released under the GNU GPL-3.0-or-later license.
; =================================================================================================

; The frame constants, at the fixed low addresses DisplayList::SetFrameConstants writes. The fcset comes
; with them because VCL requires zeroed clip flags before any CLIP instruction.
#macro LoadFrameConstants
    fcset 0x000000

    lq fMVP0,      kVuFrameConstantsAddr + 0(vi00)
    lq fMVP1,      kVuFrameConstantsAddr + 1(vi00)
    lq fMVP2,      kVuFrameConstantsAddr + 2(vi00)
    lq fMVP3,      kVuFrameConstantsAddr + 3(vi00)
    lq fGSScale,   kVuFrameConstantsAddr + 4(vi00)
    lq fGSOffset,  kVuFrameConstantsAddr + 5(vi00)
    lq fClipScale, kVuFrameConstantsAddr + 6(vi00)
#endmacro

; ---------------------------------------------------------------------
; Output windows
;
; A packet is one drawing tag, copied from its template, then RGBAQ and
; XYZ2 per vertex. The program fills a window, patches the tag's NLOOP
; and EOP, kicks it and continues in the other window. The state (which
; window is next, and the signed step to the other) lives in a spill
; qword rather than VI registers, which the clipper runs out of first,
; and it survives the program: the next batch opens the window after
; the one this batch kicked last, which the XGKICK stall has drained.
;
;   qword * win = vuMem[kVuWindowStateAddr].x; // the window to open
;   int step    = vuMem[kVuWindowStateAddr].y; // +/- kVuWindowQwords
; ---------------------------------------------------------------------

; Starts a packet in the next window: its drawing tag, then the write
; cursor and the room left.
;
;   out = win; *out++ = vuMem[tagAddr]; vertsLeft = kVuWindowVertices;
#macro OpenOutputWindow: tagAddr
    ilw.x  iOutPtr, kVuWindowStateAddr(vi00)
    lq     fTag,    tagAddr(vi00)
    sqi    fTag,    (iOutPtr++)
    iaddiu iVertsLeft, vi00, kVuWindowVertices
#endmacro

; Patches the drawing tag with the vertices written, kicks the window and
; moves to the other one; an empty window is not sent (quake2-ps2: VIF1
; would wait at the chain's FLUSH for a PATH1 transfer that never ends).
;
; NLOOP and EOP are the only fields of the tag's low word, so isw.x cannot
; disturb PRE, PRIM, FLG or NREG; EOP's 0x8000 takes two steps because
; iaddiu's immediate is 15 bits. The kick takes its address from a copy
; made before the advance, so the stores in its shadow cannot race it.
;
;   int n = kVuWindowVertices - vertsLeft;
;   if (n != 0) { win->nloop = n; win->eop = 1; xgkick(win); win += step; step = -step; }
;
; 'lblEmpty' must be unique to each invocation. The first instruction must
; stay harmless in a branch delay slot.
#macro CloseOutputWindowAndKick: lblEmpty
    iaddiu iNloop, vi00,   kVuWindowVertices
    isub   iNloop, iNloop, iVertsLeft

    ibeq   iNloop, vi00, lblEmpty

    ilw.x  iWin,   kVuWindowStateAddr(vi00)
    ilw.y  iDelta, kVuWindowStateAddr(vi00)

    iaddiu iNloop, iNloop, 0x7FFF
    iaddiu iNloop, iNloop, 1
    isw.x  iNloop, 0(iWin)

    --barrier

    iaddiu iKickAt, iWin, 0
    iadd   iWin,    iWin, iDelta
    isub   iDelta,  vi00, iDelta
    isw.x  iWin,    kVuWindowStateAddr(vi00)
    isw.y  iDelta,  kVuWindowStateAddr(vi00)

    --barrier

    xgkick iKickAt

    lblEmpty:
#endmacro

; Whole-triangle guard band reject for the clipper's fan, written to the
; three XYZ2 .w words of the triangle at iOutPtr: if any of the last three
; judgements left the band, 0x7FFF + flag reaches bit 15, the ADC bit, and
; the GS skips the triangle. Must run in the block of the clipw it reads.
;
;   adc = 0x7FFF + (clipFlags.last3() != 0); out[1].w = out[3].w = out[5].w = adc;
#macro WholeTriangleReject
    fcand  vi01, 0x3FFFF
    iaddiu iADC, vi01, 0x7FFF
    isw.w  iADC, 1(iOutPtr)
    isw.w  iADC, 3(iOutPtr)
    isw.w  iADC, 5(iOutPtr)
#endmacro
