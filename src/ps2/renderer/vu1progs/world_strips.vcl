; =================================================================================================
; world_strips.vcl
;
; One cooked world batch in plain strips (D3WD, layout WA without the index stream: the cook's
; "strips" topology, src/tools/assets/world.h), the other half of gate R1's topology comparison
; with world_lm.vcl. Preprocessed with vclpp; -j adds the VCL boilerplate.
;
; Batch layout at XTOP:
;   +0       position offset xyz, w the index qwords (0)
;   +1       position scale x (UV scale and offset, unused until textures)
;   +2       N vertices (N from XITOP) of three qwords in strip order: position s16 with the GS
;            ADC flag in w (0x8000 on the first two vertices of each strip), UVs, RGBA bytes
;
; Phase 1 transforms every vertex in place, as world_lm.vcl does: qword 0 becomes the clip-space
; position, qword 1 the projected XYZ2 (12.4 xy, z) with w the vertex's ADC flag (0x8000) plus
; 0x4000 when the vertex left the guard band. Phase 2 sends it: the strip path copies the
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
;   out[0] = iVert[back + 2]; out[1].xyz = iVert[back + 1].xyz; out[1].w = 0x8000; out += 2; --vertsLeft;
#macro StripReentry: offCol, offXyz
    lq     fCol, offCol(iVert)
    lq     fXyz, offXyz(iVert)
    sq     fCol, 0(iOutPtr)
    sq.xyz fXyz, 1(iOutPtr)
    isw.w  iFlagBit, 1(iOutPtr)

    iaddiu iOutPtr,    iOutPtr,     2
    iaddi  iVertsLeft, iVertsLeft, -1
#endmacro

; A list path corner whose triangle is wholly inside: its phase 1 qwords with the flags word
; cleared, so ADC stays clear and the triangle draws.
;
;   out[off] = vertex[2]; out[off + 1].xyz = vertex[1].xyz; out[off + 1].w = 0;
#macro ListCorner: iCorner, off
    lq     fCol, 2(iCorner)
    lq     fXyz, 1(iCorner)
    sq     fCol, off(iOutPtr)
    sq.xyz fXyz, off + 1(iOutPtr)
    isw.w  vi00, off + 1(iOutPtr)
#endmacro

; Projects a clipped corner and writes it at 'offCol'/'offXyz', as world_lm.vcl's macro does.
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

; C-like pseudo-code of the program ('vuMem' is VU1 data memory as qwords):
;
;   void world_strips()
;   {
;       qword * batch = xtop(); int n = xitop();
;       qword * v = batch + 2; bool anyOut = false;
;       do // Phase 1: every strip vertex.
;       {
;           int adc   = v[0].w & 0x8000;
;           vec4 p    = itof0(v[0]).xyz * batch[1].x + batch[0].xyz;
;           vec4 clip = p * mvp;                                    // row vectors, w = 1
;           bool out  = clipw(clip.xyz * clipScale.xyz, clip.w);     // left the guard band
;           v[0] = clip;
;           v[1].xyz = ftoi4(gsOffset.xyz + clip.xyz / clip.w * gsScale.xyz);
;           v[1].w = adc | (out ? 0x4000 : 0); anyOut |= out;
;           v += 3;
;       } while (--n > 0);
;
;       if (!anyOut) // Phase 2, strip path.
;       {
;           OpenWindow(stripTag);
;           for (v = batch + 2, n = xitop(); n > 0; v += 3, --n)
;           {
;               if (vertsLeft == 0) { CloseAndKick(); OpenWindow(stripTag); Reentry(v - 6); Reentry(v - 3); }
;               out[0] = v[2]; out[1] = v[1]; out += 2; --vertsLeft;
;           }
;           CloseAndKick();
;       }
;       else // Phase 2, list path: the same sequence as triangles.
;       {
;           OpenWindow(listTag);
;           for (int left = xitop(), v = batch + 2; left > 0; --left, v += 3) // state kept in vuMem
;           {
;               if (vertsLeft < 18) { CloseAndKick(); OpenWindow(listTag); }
;               (a, b) = (prev2, prev1); (prev2, prev1) = (prev1, v);
;               if (v[1].w & 0x8000) continue;                      // opens a strip
;               if (!((a[1].w | b[1].w | v[1].w) & 0x4000)) Emit(a, b, v);
;               else for (tri : Fan(Clip(a, b, v))) Emit(tri, adc = judged again);
;           }
;           CloseAndKick();
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
        ilw.w  iAdc, 0(iVert)

        --barrier

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

        iaddiu iVert,      iVert,       3
        iaddi  iVertsToGo, iVertsToGo, -1
        ibgtz  iVertsToGo, lTransform

    ibne iAnyOut, vi00, lListPath

    ; ---------------------------------------------------------------------
    ; Phase 2, strip path: every vertex inside the guard band.
    ; ---------------------------------------------------------------------
    OpenOutputWindow{ kVuStripTagAddr }

    iaddiu iVert,  iBase, 2
    xitop  iToGo

    lStripVertex:

        ; A full window: kick it and start the next one with the two
        ; previous vertices again, so a strip spanning the kick keeps its
        ; pending pair. The first window holds more than two vertices.
        ibne iVertsLeft, vi00, lStripRoom
        CloseOutputWindowAndKick{ lStripFull }
        OpenOutputWindow{ kVuStripTagAddr }
        StripReentry{ -4, -5 }
        StripReentry{ -1, -2 }
        lStripRoom:

        lq fCol, 2(iVert)
        lq fXyz, 1(iVert)
        sq fCol, 0(iOutPtr)
        sq fXyz, 1(iOutPtr)

        iaddiu iOutPtr,    iOutPtr,     2
        iaddi  iVertsLeft, iVertsLeft, -1
        iaddiu iVert,      iVert,       3
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

    OpenOutputWindow{ kVuListTagAddr }

    xitop  iLeft
    iaddiu iNext, iBase, 2
    isw.x  iLeft, kVuWalkStateAddr(vi00)
    isw.y  iNext, kVuWalkStateAddr(vi00)

    --barrier

    lListEntry:

        ; Room for the most one clipped triangle can fan out to: five planes
        ; leave at most eight corners, six triangles.
        iaddiu iRoom, vi00,       18
        isub   iRoom, iVertsLeft, iRoom
        ibgez  iRoom, lListRoom
        CloseOutputWindowAndKick{ lListFull }
        OpenOutputWindow{ kVuListTagAddr }
        lListRoom:

        ilw.y  iVertex, kVuWalkStateAddr(vi00)
        ilw.w  iPrev1,  kVuWalkStateAddr(vi00)
        ilw.x  iPrev2,  kVuWalkPrevAddr(vi00)
        iaddiu iNext,   iVertex, 3

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
