; =================================================================================================
; world_lm.vcl
;
; One cooked world batch (D3WD, layout WA: src/tools/assets/world.h), textured with its material's
; diffuse and the vertex colour until lightmaps exist. Preprocessed with vclpp; -j adds the VCL
; boilerplate.
;
; Batch layout at XTOP:
;   +0       position offset xyz, w the index qwords Q
;   +1       position scale x, UV scale y, UV offset zw
;   +2       the strip index stream: four entries per qword, each the vertex's qword offset in
;            this half with the GS ADC flag in bit 15 (entries past the last repeat the first
;            vertex with ADC set, so whole qwords can be walked)
;   +2+Q     N vertices (N from XITOP) of four qwords: position s16, UVs, RGBA bytes, free
;   +297     the batch state (vu_memory.h): an A+D packet with the material's texture registers
;            and TEST, kicked first, then the batch's strip and list drawing tags
;
; Phase 1 transforms every unique vertex once, in place: qword 0 becomes the clip-space
; position, qword 1 the projected XYZ2 (12.4 xy, z) with w 0x8000 when the vertex left the guard
; band, qword 2 keeps the colour bytes, already PACKED RGBAQ, and qword 3 receives the PACKED ST
; with Q: the UV times 1/w, and 1/w.
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

; Emits the vertex at iVertex into the window at 'off' (ST, RGBAQ, then XYZ2) with ADC iAdc. Only
; raw moves: the colour words are integers, which an FMAC operation would flush as denormals. The
; XYZ2 store leaves .w to the separate isw so the two writes cannot be reordered onto each other.
;
;   out[off] = vertex[3]; out[off + 1] = vertex[2]; out[off + 2].xyz = vertex[1].xyz; out[off + 2].w = adc;
#macro EmitStripVertex: off
    lq     fStq, 3(iVertex)
    lq     fCol, 2(iVertex)
    lq     fXyz, 1(iVertex)
    sq     fStq, off(iOutPtr)
    sq     fCol, off + 1(iOutPtr)
    sq.xyz fXyz, off + 2(iOutPtr)
    isw.w  iAdc, off + 2(iOutPtr)
#endmacro

; One strip entry, already in iEntry: its vertex out with its own ADC flag.
;
;   int offset = entry & 0x7FFF; adc = entry - offset; vertex = batch + offset;
;   EmitStripVertex(0); out += 3; --vertsLeft;
#macro StripEntry
    iaddiu iMask,   vi00,   0x7FFF
    iand   iOffset, iEntry, iMask
    isub   iAdc,    iEntry, iOffset
    iadd   iVertex, iBase,  iOffset

    EmitStripVertex{ 0 }

    iaddiu iOutPtr,    iOutPtr,     3
    iaddi  iVertsLeft, iVertsLeft, -1
#endmacro

; A strip entry re-sent at the head of a fresh window with ADC set: a new GIF packet restarts the
; GS vertex queue, so the two vertices the next triangle closes over are sent again, drawing nothing.
;
;   adc = 0x8000; vertex = batch + (entry & 0x7FFF); EmitStripVertex(0); out += 3; --vertsLeft;
#macro StripReentry
    iaddiu iMask,   vi00,   0x7FFF
    iand   iOffset, iEntry, iMask
    iadd   iVertex, iBase,  iOffset
    iaddiu iAdc,    vi00,   0x7FFF
    iaddiu iAdc,    iAdc,   1

    EmitStripVertex{ 0 }

    iaddiu iOutPtr,    iOutPtr,     3
    iaddi  iVertsLeft, iVertsLeft, -1
#endmacro

; Projects a clipped corner and writes it at 'offSt'/'offCol'/'offXyz': its UV and 1/w as PACKED
; ST, its colour back to bytes through ftoi0 (PACKED RGBAQ), its position through the same mapping
; phase 1 applies.
;
;   float q = 1 / pos.w;
;   out[offSt]      = { uv.x * q, uv.y * q, q };
;   out[offXyz].xyz = ftoi4(gsOffset.xyz + pos.xyz * q * gsScale.xyz);
;   out[offCol]     = ftoi0(col);
#macro EmitClipVertex: vPos, vCol, vUV, offSt, offCol, offXyz
    div       q,     vf00[w],   vPos[w]
    mul.xyz   fProj, vPos,      q
    mul.xy    fSTQ,  vUV,       q
    addq.z    fSTQ,  vf00,      q
    mula.xyz  acc,   fGSOffset, vf00[w]
    madd.xyz  fProj, fProj,     fGSScale
    ftoi4.xyz fProj, fProj
    ftoi0     fRGBA, vCol

    sq.xyz fSTQ,  offSt(iOutPtr)
    sq     fRGBA, offCol(iOutPtr)
    sq.xyz fProj, offXyz(iOutPtr)
#endmacro

; A list path corner whose triangle is wholly inside: its phase 1 qwords copied as they are. The
; XYZ2 .w is the vertex's outside flag, zero here, so ADC stays clear and the triangle draws.
;
;   out[off] = vertex[3]; out[off + 1] = vertex[2]; out[off + 2] = vertex[1];
#macro ListCorner: iCorner, off
    lq fStq, 3(iCorner)
    lq fCol, 2(iCorner)
    lq fXyz, 1(iCorner)
    sq fStq, off(iOutPtr)
    sq fCol, off + 1(iOutPtr)
    sq fXyz, off + 2(iOutPtr)
#endmacro

; C-like pseudo-code of the program ('vuMem' is VU1 data memory as qwords):
;
;   void world_lm()
;   {
;       qword * batch = xtop(); int n = xitop(); int q = batch[0].w;
;       xgkick(batch + kVuBatchStateOffset);                         // the material's registers
;       qword * v = batch + 2 + q; bool anyOut = false;
;       do // Phase 1: every unique vertex once.
;       {
;           vec4 p    = itof0(v[0]).xyz * batch[1].x + batch[0].xyz;
;           vec2 uv   = itof0(v[1]).xy * batch[1].y + batch[1].zw;
;           vec4 clip = p * mvp;                                    // row vectors, w = 1
;           bool out  = clipw(clip.xyz * clipScale.xyz, clip.w);     // left the guard band
;           v[0] = clip;
;           v[1].xyz = ftoi4(gsOffset.xyz + clip.xyz / clip.w * gsScale.xyz);
;           v[1].w = out ? 0x8000 : 0; anyOut |= out;
;           v[3].xyz = { uv / clip.w, 1 / clip.w };
;           v += 4;
;       } while (--n > 0);
;
;       if (!anyOut) // Phase 2, strip path.
;       {
;           OpenWindow(batch[kVuBatchStripTagOffset]);
;           for (qword * e = batch + 2; q > 0; ++e, --q)
;           {
;               if (vertsLeft < 4) { CloseAndKick(); OpenWindow(...); Reentry(e[-1].z); Reentry(e[-1].w); }
;               for (lane : x, y, z, w) StripEntry(e[0].lane);
;           }
;           CloseAndKick();
;       }
;       else // Phase 2, list path: the same stream as triangles.
;       {
;           OpenWindow(batch[kVuBatchListTagOffset]);
;           for (int left = 4 * q; left > 0; --left)                 // state kept in vuMem
;           {
;               if (vertsLeft < 18) { CloseAndKick(); OpenWindow(...); }
;               entry = NextEntry(); vertex = batch + (entry & 0x7FFF);
;               (a, b) = (prev2, prev1); (prev2, prev1) = (prev1, vertex);
;               if (entry & 0x8000) continue;                       // opens a strip
;               if (!(a[1].w | b[1].w | vertex[1].w)) Emit(a, b, vertex);
;               else for (tri : Fan(Clip(a, b, vertex))) Emit(tri, adc = judged again);
;           }
;           CloseAndKick();
;           xgkick(nullTag);                         // the state kick drains before the program ends
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

    ; The material's A+D packet goes first: every window this batch kicks
    ; waits behind it, so its registers reach the GS before any vertex.
    iaddiu iKickAt, iBase, kVuBatchStateOffset
    xgkick iKickAt

    ; The UV offset (header .zw) moved to .xy, where the UVs are.
    mr32 fUVOffset, fPosScale
    mr32 fUVOffset, fUVOffset

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
        lq.xy     fUV,  1(iVert)
        itof0.xyz fPos, fPos
        itof0.xy  fUV,  fUV
        mul.xyz   fPos, fPos, fPosScale[x]
        mul.xy    fUV,  fUV,  fPosScale[y]
        add.xyz   fPos, fPos, fPosOffset
        add.xy    fUV,  fUV,  fUVOffset

        mul  acc,   fMVP0, fPos[x]
        madd acc,   fMVP1, fPos[y]
        madd acc,   fMVP2, fPos[z]
        madd fClip, fMVP3, vf00[w]

        mul.xyz   fJudge, fClip, fClipScale
        clipw.xyz fJudge, fClip[w]

        div       q,     vf00[w],   fClip[w]
        mul.xyz   fProj, fClip,     q
        mul.xy    fSTQ,  fUV,       q
        addq.z    fSTQ,  vf00,      q
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
        sq.xyz fSTQ,     3(iVert)

        iaddiu iVert,      iVert,       4
        iaddi  iVertsToGo, iVertsToGo, -1
        ibgtz  iVertsToGo, lTransform

    ibne iAnyOut, vi00, lListPath

    ; ---------------------------------------------------------------------
    ; Phase 2, strip path: every vertex inside the guard band.
    ; ---------------------------------------------------------------------
    OpenBatchWindow{ kVuBatchStripTagOffset }

    iaddiu iIdx,    iBase, 2
    ilw.w  iQwords, 0(iBase)

    lStripQword:

        ; Room for this qword's four entries. A fresh window starts with the
        ; previous qword's last two entries again, so a strip spanning the
        ; kick keeps its two pending vertices; the first qword always fits.
        iaddi iRoom, iVertsLeft, -4
        ibgez iRoom, lStripRoom
        CloseOutputWindowAndKick{ lStripFull }
        OpenBatchWindow{ kVuBatchStripTagOffset }
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

    OpenBatchWindow{ kVuBatchListTagOffset }

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
        OpenBatchWindow{ kVuBatchListTagOffset }
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
        ListCorner{ iPrev1,  3 }
        ListCorner{ iVertex, 6 }

        iaddiu iOutPtr,    iOutPtr,     9
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
        lq fUV0,  kVuClipBufB + 2(vi00)
        iaddiu iFan, vi00, kVuClipBufB + 3

        lFanLoop:

            lq fPos1, 0(iFan)
            lq fCol1, 1(iFan)
            lq fUV1,  2(iFan)
            lq fPos2, 3(iFan)
            lq fCol2, 4(iFan)
            lq fUV2,  5(iFan)

            ClipJudge{ fPos0 }
            ClipJudge{ fPos1 }
            ClipJudge{ fPos2 }

            EmitClipVertex{ fPos0, fCol0, fUV0, 0, 1, 2 }
            EmitClipVertex{ fPos1, fCol1, fUV1, 3, 4, 5 }
            EmitClipVertex{ fPos2, fCol2, fUV2, 6, 7, 8 }
            WholeTriangleReject{ }

            iaddiu iOutPtr,    iOutPtr,     9
            iaddi  iVertsLeft, iVertsLeft, -3

            iaddiu iFan,     iFan,     3
            iaddi  iFanLeft, iFanLeft, -1
            ibgtz  iFanLeft, lFanLoop

        lListNext:

        ilw.x iLeft, kVuWalkStateAddr(vi00)
        iaddi iLeft, iLeft, -1
        isw.x iLeft, kVuWalkStateAddr(vi00)
        ibgtz iLeft, lListEntry

    CloseOutputWindowAndKick{ lListLast }

    ; A list batch may kick no window at all: the null tag waits for the
    ; state packet, which must leave the half before the program ends.
    iaddiu iKickAt, vi00, kVuNullGifTagAddr
    xgkick iKickAt

    lDone:

#endvuprog
