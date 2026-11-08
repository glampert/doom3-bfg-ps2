; =================================================================================================
; world_strips.vcl
;
; One cooked world batch in plain strips (D3WD, layout WA without the index stream: the cook's
; "strips" topology, src/tools/assets/world.h), the other half of gate R1's topology comparison
; with world_lm.vcl. Preprocessed with vclpp; -j adds the VCL boilerplate.
;
; Batch layout at XTOP:
;   +0       position offset xyz, w the index qwords (0)
;   +1       position scale x, UV scale y, UV offset zw
;   +2       N vertices (N from XITOP) of four qwords in strip order: position s16 with the GS
;            ADC flag in w (0x8000 on the first two vertices of each strip), UVs, RGBA bytes, free
;   +297     the batch state, as world_lm.vcl reads it
;
; Phase 1 transforms every vertex in place, as world_lm.vcl does: qword 0 becomes the clip-space
; position, qword 1 the projected XYZ2 (12.4 xy, z) with w the vertex's ADC flag (0x8000) plus
; 0x4000 when the vertex left the guard band, qword 3 the PACKED ST with Q. Phase 2 sends it: the strip path copies the
; vertices in order (w is exactly the ADC flag there, since no vertex left the band); the list
; path walks the same sequence as triangles, sending those wholly inside as they are and the rest
; through the clipper (vu_clip.i) as a fan, all as a triangle list. A strip vertex is transformed
; as often as the strips repeat it, about 1.3 times more than world_lm.vcl's unique vertices, and
; needs no index decode to be sent.
;
; This source code is released under the GNU GPL-3.0-or-later license.
; =================================================================================================

#include "vu_memory.h"
#include "vu_window.i"
#include "vu_clip.i"

; A strip vertex re-sent at the head of a fresh window with ADC set, from 'back' qwords below
; iVert: a new GIF packet restarts the GS vertex queue, so the two vertices the next triangle
; closes over are sent again, drawing nothing.
;
;   out[0] = iVert[back + 3]; out[1] = iVert[back + 2]; out[2].xyz = iVert[back + 1].xyz;
;   out[2].w = 0x8000; out += 3; --vertsLeft;
#macro StripReentry: offSt, offCol, offXyz
    lq     fStq, offSt(iVert)
    lq     fCol, offCol(iVert)
    lq     fXyz, offXyz(iVert)
    sq     fStq, 0(iOutPtr)
    sq     fCol, 1(iOutPtr)
    sq.xyz fXyz, 2(iOutPtr)
    isw.w  iFlagBit, 2(iOutPtr)

    iaddiu iOutPtr,    iOutPtr,     3
    iaddi  iVertsLeft, iVertsLeft, -1
#endmacro

; A list path corner whose triangle is wholly inside: its phase 1 qwords with the flags word
; cleared, so ADC stays clear and the triangle draws.
;
;   out[off] = vertex[3]; out[off + 1] = vertex[2]; out[off + 2].xyz = vertex[1].xyz; out[off + 2].w = 0;
#macro ListCorner: iCorner, off
    lq     fStq, 3(iCorner)
    lq     fCol, 2(iCorner)
    lq     fXyz, 1(iCorner)
    sq     fStq, off(iOutPtr)
    sq     fCol, off + 1(iOutPtr)
    sq.xyz fXyz, off + 2(iOutPtr)
    isw.w  vi00, off + 2(iOutPtr)
#endmacro

; Projects a clipped corner and writes it at 'offSt'/'offCol'/'offXyz', as world_lm.vcl's macro does.
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

; C-like pseudo-code of the program ('vuMem' is VU1 data memory as qwords):
;
;   void world_strips()
;   {
;       qword * batch = xtop(); int n = xitop();
;       xgkick(batch + kVuBatchStateOffset);                         // the material's registers
;       qword * v = batch + 2; bool anyOut = false;
;       do // Phase 1: every strip vertex.
;       {
;           int adc   = v[0].w & 0x8000;
;           vec4 p    = itof0(v[0]).xyz * batch[1].x + batch[0].xyz;
;           vec2 uv   = itof0(v[1]).xy * batch[1].y + batch[1].zw;
;           vec4 clip = p * mvp;                                    // row vectors, w = 1
;           bool out  = clipw(clip.xyz * clipScale.xyz, clip.w);     // left the guard band
;           v[0] = clip;
;           v[1].xyz = ftoi4(gsOffset.xyz + clip.xyz / clip.w * gsScale.xyz);
;           v[1].w = adc | (out ? 0x4000 : 0); anyOut |= out;
;           v[3].xyz = { uv / clip.w, 1 / clip.w };
;           v += 4;
;       } while (--n > 0);
;
;       if (!anyOut) // Phase 2, strip path.
;       {
;           OpenWindow(batch[kVuBatchStripTagOffset]);
;           for (v = batch + 2, n = xitop(); n > 0; v += 4, --n)
;           {
;               if (vertsLeft == 0) { CloseAndKick(); OpenWindow(...); Reentry(v - 8); Reentry(v - 4); }
;               out[0] = v[3]; out[1] = v[2]; out[2] = v[1]; out += 3; --vertsLeft;
;           }
;           CloseAndKick();
;       }
;       else // Phase 2, list path: the same sequence as triangles.
;       {
;           OpenWindow(batch[kVuBatchListTagOffset]);
;           for (int left = xitop(), v = batch + 2; left > 0; --left, v += 4) // state kept in vuMem
;           {
;               if (vertsLeft < 18) { CloseAndKick(); OpenWindow(...); }
;               (a, b) = (prev2, prev1); (prev2, prev1) = (prev1, v);
;               if (v[1].w & 0x8000) continue;                      // opens a strip
;               if (!((a[1].w | b[1].w | v[1].w) & 0x4000)) Emit(a, b, v);
;               else for (tri : Fan(Clip(a, b, v))) Emit(tri, adc = judged again);
;           }
;           CloseAndKick();
;           xgkick(nullTag);                         // the state kick drains before the program ends
;       }
;   }
#vuprog world_strips

    LoadFrameConstants{ }

    ; -1.0, the lower end of the clipper's distance clamp (vf00.w is the upper one).
    sub.x fMinusOne, vf00, vf00[w]

    xtop   iBase
    xitop  iVertsToGo
    lq     fPosOffset, 0(iBase)
    lq     fPosScale,  1(iBase)
    iaddiu iVert,      iBase, 2

    ; The material's A+D packet goes first: every window this batch kicks
    ; waits behind it, so its registers reach the GS before any vertex.
    iaddiu iKickAt, iBase, kVuBatchStateOffset
    xgkick iKickAt

    ; The UV offset (header .zw) moved to .xy, where the UVs are.
    mr32 fUVOffset, fPosScale
    mr32 fUVOffset, fUVOffset

    ; 0x8000, the ADC bit; iaddiu's immediate is 15 bits.
    iaddiu iFlagBit, vi00,     0x7FFF
    iaddiu iFlagBit, iFlagBit, 1
    iaddiu iAnyOut,  vi00,     0

    ; ---------------------------------------------------------------------
    ; Phase 1: transform, judge and project every strip vertex in place.
    ; ---------------------------------------------------------------------
    lTransform:

        ; The ADC flag is read before qword 0 is overwritten; the barrier
        ; keeps openvcl from moving the store above the load.
        lq.xyz fPos, 0(iVert)
        lq.xy  fUV,  1(iVert)
        ilw.w  iAdc, 0(iVert)

        --barrier

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

        ; This vertex's six flags, in the block of its clipw: 0x4000 when any is set.
        fcand  vi01,     0x00003F
        iaddiu iOutside, vi01,     0x3FFF
        iaddiu iOutBit,  vi00,     0x4000
        iand   iOutside, iOutside, iOutBit
        ior    iAnyOut,  iAnyOut,  iOutside
        iand   iAdc,     iAdc,     iFlagBit
        ior    iAdc,     iAdc,     iOutside

        sq     fClip, 0(iVert)
        sq.xyz fProj, 1(iVert)
        isw.w  iAdc,  1(iVert)
        sq.xyz fSTQ,  3(iVert)

        iaddiu iVert,      iVert,       4
        iaddi  iVertsToGo, iVertsToGo, -1
        ibgtz  iVertsToGo, lTransform

    ibne iAnyOut, vi00, lListPath

    ; ---------------------------------------------------------------------
    ; Phase 2, strip path: every vertex inside the guard band.
    ; ---------------------------------------------------------------------
    OpenBatchWindow{ kVuBatchStripTagOffset }

    iaddiu iVert,  iBase, 2
    xitop  iToGo

    lStripVertex:

        ; A full window: kick it and start the next one with the two
        ; previous vertices again, so a strip spanning the kick keeps its
        ; pending pair. The first window holds more than two vertices.
        ibne iVertsLeft, vi00, lStripRoom
        CloseOutputWindowAndKick{ lStripFull }
        OpenBatchWindow{ kVuBatchStripTagOffset }
        StripReentry{ -5, -6, -7 }
        StripReentry{ -1, -2, -3 }
        lStripRoom:

        lq fStq, 3(iVert)
        lq fCol, 2(iVert)
        lq fXyz, 1(iVert)
        sq fStq, 0(iOutPtr)
        sq fCol, 1(iOutPtr)
        sq fXyz, 2(iOutPtr)

        iaddiu iOutPtr,    iOutPtr,     3
        iaddi  iVertsLeft, iVertsLeft, -1
        iaddiu iVert,      iVert,       4
        iaddi  iToGo,      iToGo,      -1
        ibgtz  iToGo,      lStripVertex

    CloseOutputWindowAndKick{ lStripLast }
    b lDone

    ; ---------------------------------------------------------------------
    ; Phase 2, list path: some vertex left the guard band.
    ;
    ; The walk's state lives in VU memory, reloaded every entry, so nothing
    ; but the window cursor is carried in registers across the clipper:
    ;   kVuWalkStateAddr   x vertices left, y the next vertex, w the previous one
    ;   kVuWalkPrevAddr    x the vertex before that
    ; ---------------------------------------------------------------------
    lListPath:

    OpenBatchWindow{ kVuBatchListTagOffset }

    xitop  iLeft
    iaddiu iNext, iBase, 2
    isw.x  iLeft, kVuWalkStateAddr(vi00)
    isw.y  iNext, kVuWalkStateAddr(vi00)

    ; The clipper's seed (vu_clip.i) picks the UV by the pass: this program
    ; draws only the base pass, which needs Q in qword 3 .z, as phase 1 keeps.
    isw.z  vi00,  kVuWalkPrevAddr(vi00)

    --barrier

    lListEntry:

        ; Room for the most one clipped triangle can fan out to: five planes
        ; leave at most eight corners, six triangles.
        iaddiu iRoom, vi00,       18
        isub   iRoom, iVertsLeft, iRoom
        ibgez  iRoom, lListRoom
        CloseOutputWindowAndKick{ lListFull }
        OpenBatchWindow{ kVuBatchListTagOffset }
        lListRoom:

        ilw.y  iVertex, kVuWalkStateAddr(vi00)
        ilw.w  iPrev1,  kVuWalkStateAddr(vi00)
        ilw.x  iPrev2,  kVuWalkPrevAddr(vi00)
        iaddiu iNext,   iVertex, 4

        --barrier

        isw.y  iNext,   kVuWalkStateAddr(vi00)
        isw.x  iPrev1,  kVuWalkPrevAddr(vi00)
        isw.w  iVertex, kVuWalkStateAddr(vi00)

        ; An opening vertex closes no triangle.
        ilw.w  iF2,   1(iVertex)
        iaddiu iMask, vi00,  0x7FFF
        iaddiu iMask, iMask, 1
        iand   iAdc,  iF2,   iMask
        ibne   iAdc,  vi00,  lListNext

        ilw.w  iF0,   1(iPrev2)
        ilw.w  iF1,   1(iPrev1)
        ior    iF0,   iF0,   iF1
        ior    iF0,   iF0,   iF2
        iaddiu iMask, vi00,  0x4000
        iand   iF0,   iF0,   iMask
        ibne   iF0,   vi00,  lListClip

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
