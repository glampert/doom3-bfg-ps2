; =================================================================================================
; world_lm.vcl
;
; One cooked world batch (D3WD, layout WA: src/tools/assets/world.h), vertex colour only until
; lightmaps exist. Preprocessed with vclpp; -j adds the VCL boilerplate.
;
; Batch layout at XTOP:
;   +0       position offset xyz, w the index qwords Q
;   +1       position scale x (UV scale and offset, unused until textures)
;   +2       the strip index stream: four entries per qword, each the vertex's qword offset in
;            this half with the GS ADC flag in bit 15 (entries past the last repeat the first
;            vertex with ADC set, so whole qwords can be walked)
;   +2+Q     N vertices (N from XITOP) of three qwords: position s16, UVs, RGBA bytes
;
; Phase 1 transforms every unique vertex once, in place: qword 0 becomes the clip-space
; position, qword 1 the projected XYZ2 (12.4 xy, z) with w 0x8000 when the vertex left the guard
; band, and qword 2 keeps the colour bytes, already PACKED RGBAQ.
;
; Phase 2 sends the batch to the GS. When every vertex is inside the band (nearly every batch)
; the strip path walks the index stream and emits each entry's vertex with its ADC flag as a
; triangle strip: about 1.5 vertices per triangle. Otherwise the list path walks the same
; stream as triangles: those with every corner inside go out as they are, the rest through the
; clipper (vu_clip.i) as a fan, all as a triangle list.
;
; Output goes through the two windows of vu_window.i, kept alternating across batches, so the
; program needs no drain guard: the window it writes next was drained by its last kick's stall.
;
; This source code is released under the GNU GPL-3.0-or-later license.
; =================================================================================================

#include "vu_memory.h"
#include "vu_window.i"
#include "vu_clip.i"

; Emits the vertex at iVertex into the window at 'off' (RGBAQ, then XYZ2) with ADC iAdc. Only raw
; moves: the colour words are integers, which an FMAC operation would flush as denormals. The
; XYZ2 store leaves .w to the separate isw so the two writes cannot be reordered onto each other.
;
;   out[off] = vertex[2]; out[off + 1].xyz = vertex[1].xyz; out[off + 1].w = adc;
#macro EmitStripVertex: off
    lq     fCol, 2(iVertex)
    lq     fXyz, 1(iVertex)
    sq     fCol, off(iOutPtr)
    sq.xyz fXyz, off + 1(iOutPtr)
    isw.w  iAdc, off + 1(iOutPtr)
#endmacro

; One strip entry, already in iEntry: its vertex out with its own ADC flag.
;
;   int offset = entry & 0x7FFF; adc = entry - offset; vertex = batch + offset;
;   EmitStripVertex(0); out += 2; --vertsLeft;
#macro StripEntry
    iaddiu iMask,   vi00,   0x7FFF
    iand   iOffset, iEntry, iMask
    isub   iAdc,    iEntry, iOffset
    iadd   iVertex, iBase,  iOffset

    EmitStripVertex{ 0 }

    iaddiu iOutPtr,    iOutPtr,     2
    iaddi  iVertsLeft, iVertsLeft, -1
#endmacro

; A strip entry re-sent at the head of a fresh window with ADC set: a new GIF packet restarts the
; GS vertex queue, so the two vertices the next triangle closes over are sent again, drawing nothing.
;
;   adc = 0x8000; vertex = batch + (entry & 0x7FFF); EmitStripVertex(0); out += 2; --vertsLeft;
#macro StripReentry
    iaddiu iMask,   vi00,   0x7FFF
    iand   iOffset, iEntry, iMask
    iadd   iVertex, iBase,  iOffset
    iaddiu iAdc,    vi00,   0x7FFF
    iaddiu iAdc,    iAdc,   1

    EmitStripVertex{ 0 }

    iaddiu iOutPtr,    iOutPtr,     2
    iaddi  iVertsLeft, iVertsLeft, -1
#endmacro

; Projects a clipped corner and writes it at 'offCol'/'offXyz': its colour back to bytes through
; ftoi0 (PACKED RGBAQ), its position through the same mapping phase 1 applies.
;
;   float q = 1 / pos.w;
;   out[offXyz].xyz = ftoi4(gsOffset.xyz + pos.xyz * q * gsScale.xyz);
;   out[offCol]     = ftoi0(col);
#macro EmitClipVertex: vPos, vCol, offCol, offXyz
    div       q,     vf00[w],   vPos[w]
    mul.xyz   fProj, vPos,      q
    mula.xyz  acc,   fGSOffset, vf00[w]
    madd.xyz  fProj, fProj,     fGSScale
    ftoi4.xyz fProj, fProj
    ftoi0     fRGBA, vCol

    sq     fRGBA, offCol(iOutPtr)
    sq.xyz fProj, offXyz(iOutPtr)
#endmacro

; A list path corner whose triangle is wholly inside: its phase 1 qwords copied as they are. The
; XYZ2 .w is the vertex's outside flag, zero here, so ADC stays clear and the triangle draws.
;
;   out[off] = vertex[2]; out[off + 1] = vertex[1];
#macro ListCorner: iCorner, off
    lq fCol, 2(iCorner)
    lq fXyz, 1(iCorner)
    sq fCol, off(iOutPtr)
    sq fXyz, off + 1(iOutPtr)
#endmacro

; C-like pseudo-code of the program ('vuMem' is VU1 data memory as qwords):
;
;   void world_lm()
;   {
;       qword * batch = xtop(); int n = xitop(); int q = batch[0].w;
;       qword * v = batch + 2 + q; bool anyOut = false;
;       do // Phase 1: every unique vertex once.
;       {
;           vec4 p    = itof0(v[0]).xyz * batch[1].x + batch[0].xyz;
;           vec4 clip = p * mvp;                                    // row vectors, w = 1
;           bool out  = clipw(clip.xyz * clipScale.xyz, clip.w);     // left the guard band
;           v[0] = clip;
;           v[1].xyz = ftoi4(gsOffset.xyz + clip.xyz / clip.w * gsScale.xyz);
;           v[1].w = out ? 0x8000 : 0; anyOut |= out;
;           v += 3;
;       } while (--n > 0);
;
;       if (!anyOut) // Phase 2, strip path.
;       {
;           OpenWindow(stripTag);
;           for (qword * e = batch + 2; q > 0; ++e, --q)
;           {
;               if (vertsLeft < 4) { CloseAndKick(); OpenWindow(stripTag); Reentry(e[-1].z); Reentry(e[-1].w); }
;               for (lane : x, y, z, w) StripEntry(e[0].lane);
;           }
;           CloseAndKick();
;       }
;       else // Phase 2, list path: the same stream as triangles.
;       {
;           OpenWindow(listTag);
;           for (int left = 4 * q; left > 0; --left)                 // state kept in vuMem
;           {
;               if (vertsLeft < 18) { CloseAndKick(); OpenWindow(listTag); }
;               entry = NextEntry(); vertex = batch + (entry & 0x7FFF);
;               (a, b) = (prev2, prev1); (prev2, prev1) = (prev1, vertex);
;               if (entry & 0x8000) continue;                       // opens a strip
;               if (!(a[1].w | b[1].w | vertex[1].w)) Emit(a, b, vertex);
;               else for (tri : Fan(Clip(a, b, vertex))) Emit(tri, adc = judged again);
;           }
;           CloseAndKick();
;       }
;   }
#vuprog world_lm

    LoadFrameConstants{ }

    ; -1.0, the lower end of the clipper's distance clamp (vf00.w is the upper one).
    sub.x fMinusOne, vf00, vf00[w]

    xtop   iBase
    xitop  iVertsToGo
    lq     fPosOffset, 0(iBase)
    lq     fPosScale,  1(iBase)
    ilw.w  iQwords,    0(iBase)

    iaddiu iVert, iBase, 2
    iadd   iVert, iVert, iQwords

    ; 0x8000, the ADC bit the outside flag is stored as; iaddiu's immediate is 15 bits.
    iaddiu iFlagBit, vi00,     0x7FFF
    iaddiu iFlagBit, iFlagBit, 1
    iaddiu iAnyOut,  vi00,     0

    ; ---------------------------------------------------------------------
    ; Phase 1: transform, judge and project every unique vertex in place.
    ; ---------------------------------------------------------------------
    lTransform:

        lq.xyz    fPos, 0(iVert)
        itof0.xyz fPos, fPos
        mul.xyz   fPos, fPos, fPosScale[x]
        add.xyz   fPos, fPos, fPosOffset

        mul  acc,   fMVP0, fPos[x]
        madd acc,   fMVP1, fPos[y]
        madd acc,   fMVP2, fPos[z]
        madd fClip, fMVP3, vf00[w]

        mul.xyz   fJudge, fClip, fClipScale
        clipw.xyz fJudge, fClip[w]

        div       q,     vf00[w],   fClip[w]
        mul.xyz   fProj, fClip,     q
        mula.xyz  acc,   fGSOffset, vf00[w]
        madd.xyz  fProj, fProj,     fGSScale
        ftoi4.xyz fProj, fProj

        ; This vertex's six flags, in the block of its clipw.
        fcand  vi01,     0x00003F
        iaddiu iOutside, vi01,     0x7FFF
        iand   iOutside, iOutside, iFlagBit
        ior    iAnyOut,  iAnyOut,  iOutside

        sq     fClip,    0(iVert)
        sq.xyz fProj,    1(iVert)
        isw.w  iOutside, 1(iVert)

        iaddiu iVert,      iVert,       3
        iaddi  iVertsToGo, iVertsToGo, -1
        ibgtz  iVertsToGo, lTransform

    ibne iAnyOut, vi00, lListPath

    ; ---------------------------------------------------------------------
    ; Phase 2, strip path: every vertex inside the guard band.
    ; ---------------------------------------------------------------------
    OpenOutputWindow{ kVuStripTagAddr }

    iaddiu iIdx,    iBase, 2
    ilw.w  iQwords, 0(iBase)

    lStripQword:

        ; Room for this qword's four entries. A fresh window starts with the
        ; previous qword's last two entries again, so a strip spanning the
        ; kick keeps its two pending vertices; the first qword always fits.
        iaddi iRoom, iVertsLeft, -4
        ibgez iRoom, lStripRoom
        CloseOutputWindowAndKick{ lStripFull }
        OpenOutputWindow{ kVuStripTagAddr }
        ilw.z iEntry, -1(iIdx)
        StripReentry{ }
        ilw.w iEntry, -1(iIdx)
        StripReentry{ }
        lStripRoom:

        ilw.x iEntry, 0(iIdx)
        StripEntry{ }
        ilw.y iEntry, 0(iIdx)
        StripEntry{ }
        ilw.z iEntry, 0(iIdx)
        StripEntry{ }
        ilw.w iEntry, 0(iIdx)
        StripEntry{ }

        iaddiu iIdx,    iIdx,     1
        iaddi  iQwords, iQwords, -1
        ibgtz  iQwords, lStripQword

    CloseOutputWindowAndKick{ lStripLast }
    b lDone

    ; ---------------------------------------------------------------------
    ; Phase 2, list path: some vertex left the guard band.
    ;
    ; The walk's state lives in VU memory, reloaded every entry, so nothing
    ; but the window cursor is carried in registers across the clipper:
    ;   kVuWalkStateAddr   x entries left, y next stream qword, z lanes left
    ;                      in the rotating qword, w the previous vertex
    ;   kVuWalkPrevAddr    x the vertex before that
    ;   kVuWalkIndicesAddr the current stream qword, rotated one lane per entry
    ; ---------------------------------------------------------------------
    lListPath:

    OpenOutputWindow{ kVuListTagAddr }

    ilw.w  iQwords, 0(iBase)
    iadd   iLeft,   iQwords, iQwords
    iadd   iLeft,   iLeft,   iLeft
    iaddiu iNext,   iBase,   2
    isw.x  iLeft,   kVuWalkStateAddr(vi00)
    isw.y  iNext,   kVuWalkStateAddr(vi00)
    isw.z  vi00,    kVuWalkStateAddr(vi00)

    --barrier

    lListEntry:

        ; Room for the most one clipped triangle can fan out to: five planes
        ; leave at most eight corners, six triangles. Asked once, before the
        ; corners load, as quake2-ps2's program does.
        iaddiu iRoom, vi00,       18
        isub   iRoom, iVertsLeft, iRoom
        ibgez  iRoom, lListRoom
        CloseOutputWindowAndKick{ lListFull }
        OpenOutputWindow{ kVuListTagAddr }
        lListRoom:

        ; The next entry: lane x of the rotating qword, refilled every four.
        lq     fIndices, kVuWalkIndicesAddr(vi00)
        ilw.z  iLanes,   kVuWalkStateAddr(vi00)
        ibne   iLanes,   vi00, lListHaveLanes
        ilw.y  iNext,    kVuWalkStateAddr(vi00)
        lq     fIndices, 0(iNext)
        iaddiu iNext,    iNext, 1
        iaddiu iLanes,   vi00,  4
        isw.y  iNext,    kVuWalkStateAddr(vi00)
        lListHaveLanes:

        mtir   iEntry,   fIndices[x]
        mr32   fIndices, fIndices
        iaddi  iLanes,   iLanes, -1

        xtop   iBase
        iaddiu iMask,    vi00,    0x7FFF
        iand   iOffset,  iEntry,  iMask
        isub   iAdc,     iEntry,  iOffset
        iadd   iVertex,  iBase,   iOffset

        ilw.w  iPrev1,   kVuWalkStateAddr(vi00)
        ilw.x  iPrev2,   kVuWalkPrevAddr(vi00)

        --barrier

        sq     fIndices, kVuWalkIndicesAddr(vi00)
        isw.z  iLanes,   kVuWalkStateAddr(vi00)
        isw.x  iPrev1,   kVuWalkPrevAddr(vi00)
        isw.w  iVertex,  kVuWalkStateAddr(vi00)

        ; An opening entry closes no triangle.
        ibne iAdc, vi00, lListNext

        ilw.w iF0, 1(iPrev2)
        ilw.w iF1, 1(iPrev1)
        ilw.w iF2, 1(iVertex)
        ior   iF0, iF0, iF1
        ior   iF0, iF0, iF2
        ibne  iF0, vi00, lListClip

        ; Every corner inside: the three phase 1 vertices as they are.
        ListCorner{ iPrev2,  0 }
        ListCorner{ iPrev1,  2 }
        ListCorner{ iVertex, 4 }

        iaddiu iOutPtr,    iOutPtr,     6
        iaddi  iVertsLeft, iVertsLeft, -3
        b lListNext

        lListClip:

        ClipSeedTriangle{ iPrev2, iPrev1, iVertex }

        ; Near first (it removes the most), then the four guard band sides;
        ; A->B->A->B->A->B leaves the survivors in B.
        ClipPlanePass{ fJnC[z], fJnN[z], kVuClipBufA, kVuClipBufB, lNear }
        ClipPlanePass{ fJnC[x], fJnN[x], kVuClipBufB, kVuClipBufA, lXl   }
        ClipPlanePass{ fJpC[x], fJpN[x], kVuClipBufA, kVuClipBufB, lXr   }
        ClipPlanePass{ fJnC[y], fJnN[y], kVuClipBufB, kVuClipBufA, lYb   }
        ClipPlanePass{ fJpC[y], fJpN[y], kVuClipBufA, kVuClipBufB, lYt   }

        ; Fan the survivors from corner 0: count - 2 triangles.
        ilw.x iCount,   kVuClipCountAddr(vi00)
        iaddi iFanLeft, iCount, -2
        ibltz iFanLeft, lListNext
        ibeq  iFanLeft, vi00, lListNext

        lq fPos0, kVuClipBufB + 0(vi00)
        lq fCol0, kVuClipBufB + 1(vi00)
        iaddiu iFan, vi00, kVuClipBufB + 2

        lFanLoop:

            lq fPos1, 0(iFan)
            lq fCol1, 1(iFan)
            lq fPos2, 2(iFan)
            lq fCol2, 3(iFan)

            ClipJudge{ fPos0 }
            ClipJudge{ fPos1 }
            ClipJudge{ fPos2 }

            EmitClipVertex{ fPos0, fCol0, 0, 1 }
            EmitClipVertex{ fPos1, fCol1, 2, 3 }
            EmitClipVertex{ fPos2, fCol2, 4, 5 }
            WholeTriangleReject{ }

            iaddiu iOutPtr,    iOutPtr,     6
            iaddi  iVertsLeft, iVertsLeft, -3

            iaddiu iFan,     iFan,     2
            iaddi  iFanLeft, iFanLeft, -1
            ibgtz  iFanLeft, lFanLoop

        lListNext:

        ilw.x iLeft, kVuWalkStateAddr(vi00)
        iaddi iLeft, iLeft, -1
        isw.x iLeft, kVuWalkStateAddr(vi00)
        ibgtz iLeft, lListEntry

    CloseOutputWindowAndKick{ lListLast }

    lDone:

#endvuprog
