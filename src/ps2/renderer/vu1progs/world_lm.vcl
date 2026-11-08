; =================================================================================================
; world_lm.vcl
;
; One cooked world batch (D3WD, layout WA: src/tools/assets/world.h): the base pass, its material's
; diffuse times the vertex colour (the light's chroma), then, when the batch has a lightmap, the
; lightmap pass in the other GS context, Cd x As with the page's intensity. Preprocessed with
; vclpp; -j adds the VCL boilerplate.
;
; Batch layout at XTOP:
;   +0       position offset xyz, w the index qwords Q
;   +1       position scale x, UV scale y, UV offset zw
;   +2       the strip index stream: four entries per qword, each the vertex's qword offset in
;            this half with the GS ADC flag in bit 15 (entries past the last repeat the first
;            vertex with ADC set, so whole qwords can be walked)
;   +2+Q     N vertices (N from XITOP) of four qwords: position s16, UVs (the base pair, then the
;            lightmap pair in 4.12), RGBA bytes, free
;   +295     the batch state (vu_memory.h): the lightmap page's A+D packet (its second qword's
;            word 3 non-zero when the batch has a lightmap pass), the material's A+D packet,
;            kicked first, then the batch's strip and list drawing tags
;
; Phase 1 transforms every unique vertex once, in place: qword 0 becomes the clip-space
; position with the lightmap T x Q in w, qword 1 the projected XYZ2 (12.4 xy, z) with w 0x8000
; when the vertex left the guard band, qword 2 keeps the colour bytes, already PACKED RGBAQ, and
; qword 3 receives the PACKED ST with Q (the UV times Q = 1/w, and Q) with the lightmap S x Q in
; w. Sixteen lanes hold everything both passes and the clipper need: the clipper rebuilds w as
; 1/Q, and a lightmap vertex assembles its ST from the two w lanes and Q.
;
; Phase 2 sends the batch to the GS. When every vertex is inside the band (nearly every batch)
; the strip path walks the index stream and emits each entry's vertex with its ADC flag as a
; triangle strip: about 1.5 vertices per triangle. Otherwise the list path walks the same
; stream as triangles: those with every corner inside go out as they are, the rest through the
; clipper (vu_clip.i) as a fan, all as a triangle list. A lit batch walks again for the lightmap
; pass after kicking the page's A+D packet: both paths send their usual three qwords with the
; lightmap ST. The RGBAQ qword stays even though DECAL ignores the colour: PACKED ST holds its Q
; until an RGBAQ write stores it, so without one every vertex would divide by a stale Q. The two contexts share one CLUT buffer, so the
; page's TEX0 (which loads its CLUT) goes only once the base pass's windows are out.
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

; The lightmap pass's ST: the lightmap S x Q (qword 3 .w), T x Q (qword 0 .w) and Q.
;
;   st = { vertex[3].w, vertex[0].w, vertex[3].z };
#macro LightmapSt: iV, vSt, vStq, vT
    lq    vStq, 3(iV)
    lq.w  vT,   0(iV)
    add.x vSt,  vf00, vStq[w]
    add.y vSt,  vf00, vT[w]
    move.z vSt, vStq
#endmacro

; Emits the vertex at iVertex into a lightmap strip window at 'off': its lightmap ST, the colour
; qword (whose write stores the ST's Q), then XYZ2 with ADC iAdc.
;
;   out[off] = LightmapSt(vertex); out[off + 1] = vertex[2]; out[off + 2].xyz = vertex[1].xyz; out[off + 2].w = adc;
#macro EmitLightmapVertex: off
    LightmapSt{ iVertex, fLmSt, fStq, fLmT }
    lq     fCol,  2(iVertex)
    lq     fXyz,  1(iVertex)
    sq.xyz fLmSt, off(iOutPtr)
    sq     fCol,  off + 1(iOutPtr)
    sq.xyz fXyz,  off + 2(iOutPtr)
    isw.w  iAdc,  off + 2(iOutPtr)
#endmacro

; StripEntry and StripReentry for the lightmap pass.
#macro LightmapEntry
    iaddiu iMask,   vi00,   0x7FFF
    iand   iOffset, iEntry, iMask
    isub   iAdc,    iEntry, iOffset
    iadd   iVertex, iBase,  iOffset

    EmitLightmapVertex{ 0 }

    iaddiu iOutPtr,    iOutPtr,     3
    iaddi  iVertsLeft, iVertsLeft, -1
#endmacro

#macro LightmapReentry
    iaddiu iMask,   vi00,   0x7FFF
    iand   iOffset, iEntry, iMask
    iadd   iVertex, iBase,  iOffset
    iaddiu iAdc,    vi00,   0x7FFF
    iaddiu iAdc,    iAdc,   1

    EmitLightmapVertex{ 0 }

    iaddiu iOutPtr,    iOutPtr,     3
    iaddi  iVertsLeft, iVertsLeft, -1
#endmacro

; A list path corner of the lightmap pass, inside the band: ListCorner with the lightmap ST.
;
;   out[off] = LightmapSt(corner); out[off + 1] = corner[2]; out[off + 2] = corner[1];
#macro ListLightmapCorner: iCorner, off
    LightmapSt{ iCorner, fLmSt, fStq, fLmT }
    lq     fCol,  2(iCorner)
    lq     fXyz,  1(iCorner)
    sq.xyz fLmSt, off(iOutPtr)
    sq     fCol,  off + 1(iOutPtr)
    sq     fXyz,  off + 2(iOutPtr)
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
;           vec2 lm   = itof12(v[1]).zw;
;           vec4 clip = p * mvp;                                    // row vectors, w = 1
;           bool out  = clipw(clip.xyz * clipScale.xyz, clip.w);     // left the guard band
;           float q   = 1 / clip.w;
;           v[0] = { clip.xyz, lm.y * q };
;           v[1].xyz = ftoi4(gsOffset.xyz + clip.xyz * q * gsScale.xyz);
;           v[1].w = out ? 0x8000 : 0; anyOut |= out;
;           v[3] = { uv * q, q, lm.x * q };
;           v += 4;
;       } while (--n > 0);
;
;       bool lit = batch[kVuBatchLightmapFlagOffset].w != 0;
;       if (!anyOut) // Phase 2, strip path.
;       {
;           for (pass : base, lit ? lightmap : none)
;           {
;               if (pass == lightmap) xgkick(batch + kVuBatchLightmapOffset);   // its TEX0
;               OpenWindow(pass == base ? batch[kVuBatchStripTagOffset] : vuMem[kVuLightmapStripTagAddr]);
;               for (qword * e = batch + 2; q > 0; ++e, --q)
;               {
;                   if (vertsLeft < 4) { CloseAndKick(); OpenWindow(...); Reentry(e[-1].z); Reentry(e[-1].w); }
;                   for (lane : x, y, z, w) Entry(e[0].lane);       // ST, RGBAQ, XYZ2 or ST, XYZ2
;               }
;               CloseAndKick();
;           }
;       }
;       else // Phase 2, list path: the same stream as triangles, once per pass.
;       {
;           for (pass : base, lit ? lightmap : none)                // pass and tag kept in vuMem
;           {
;               if (pass == lightmap) xgkick(batch + kVuBatchLightmapOffset);
;               OpenWindow(pass == base ? batch[kVuBatchListTagOffset] : vuMem[kVuLightmapListTagAddr]);
;               for (int left = 4 * q; left > 0; --left)             // state kept in vuMem
;               {
;                   if (vertsLeft < 18) { CloseAndKick(); OpenWindow(...); }
;                   entry = NextEntry(); vertex = batch + (entry & 0x7FFF);
;                   (a, b) = (prev2, prev1); (prev2, prev1) = (prev1, vertex);
;                   if (entry & 0x8000) continue;                   // opens a strip
;                   if (!(a[1].w | b[1].w | vertex[1].w)) Emit(pass, a, b, vertex);
;                   else for (tri : Fan(Clip(pass, a, b, vertex))) Emit(tri, adc = judged again);
;               }
;               CloseAndKick();
;           }
;           xgkick(nullTag);                         // the state kicks drain before the program ends
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
        lq        fUV,  1(iVert)
        itof0.xyz fPos, fPos
        itof0.xy  fUV,  fUV
        itof12.zw fUV,  fUV
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
        mul.zw    fLmQ,  fUV,       q
        mul.w     fSTQ,  vf00,      fLmQ[z]
        mula.xyz  acc,   fGSOffset, vf00[w]
        madd.xyz  fProj, fProj,     fGSScale
        ftoi4.xyz fProj, fProj

        ; This vertex's six flags, in the block of its clipw.
        fcand  vi01,     0x00003F
        iaddiu iOutside, vi01,     0x7FFF
        iand   iOutside, iOutside, iFlagBit
        ior    iAnyOut,  iAnyOut,  iOutside

        sq.xyz fClip,    0(iVert)
        sq.w   fLmQ,     0(iVert)
        sq.xyz fProj,    1(iVert)
        isw.w  iOutside, 1(iVert)
        sq     fSTQ,     3(iVert)

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

    ; ---------------------------------------------------------------------
    ; The strip path's lightmap pass: the same stream again under the other
    ; context, after the page's TEX0.
    ; ---------------------------------------------------------------------
    ilw.w  iLit, kVuBatchLightmapFlagOffset(iBase)
    ibeq   iLit, vi00, lDone

    iaddiu iKickAt, iBase, kVuBatchLightmapOffset
    xgkick iKickAt

    OpenLightmapWindow{ }

    iaddiu iIdx,    iBase, 2
    ilw.w  iQwords, 0(iBase)

    lLightmapQword:

        iaddi iRoom, iVertsLeft, -4
        ibgez iRoom, lLightmapRoom
        CloseOutputWindowAndKick{ lLightmapFull }
        OpenLightmapWindow{ }
        ilw.z iEntry, -1(iIdx)
        LightmapReentry{ }
        ilw.w iEntry, -1(iIdx)
        LightmapReentry{ }
        lLightmapRoom:

        ilw.x iEntry, 0(iIdx)
        LightmapEntry{ }
        ilw.y iEntry, 0(iIdx)
        LightmapEntry{ }
        ilw.z iEntry, 0(iIdx)
        LightmapEntry{ }
        ilw.w iEntry, 0(iIdx)
        LightmapEntry{ }

        iaddiu iIdx,    iIdx,     1
        iaddi  iQwords, iQwords, -1
        ibgtz  iQwords, lLightmapQword

    CloseOutputWindowAndKick{ lLightmapLast }
    b lDone

    ; ---------------------------------------------------------------------
    ; Phase 2, list path: some vertex left the guard band.
    ;
    ; The walk's state lives in VU memory, reloaded every entry, so nothing
    ; but the window cursor is carried in registers across the clipper:
    ;   kVuWalkStateAddr   x entries left, y next stream qword, z lanes left
    ;                      in the rotating qword, w the previous vertex
    ;   kVuWalkPrevAddr    x the vertex before that, y the address of the
    ;                      pass's drawing tag, z the pass (0 base, 1 lightmap)
    ;   kVuWalkIndicesAddr the current stream qword, rotated one lane per entry
    ; ---------------------------------------------------------------------
    lListPath:

    iaddiu iTagAt, iBase, kVuBatchListTagOffset
    isw.y  iTagAt, kVuWalkPrevAddr(vi00)
    isw.z  vi00,   kVuWalkPrevAddr(vi00)

    --barrier

    lListPass:

    OpenWalkWindow{ }

    xtop   iBase
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
        OpenWalkWindow{ }
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

        ; Every corner inside: the three phase 1 vertices as they are, with
        ; the lightmap ST in the lightmap pass.
        ilw.z iPass, kVuWalkPrevAddr(vi00)
        ibne  iPass, vi00, lListLightmapCorners

        ListCorner{ iPrev2,  0 }
        ListCorner{ iPrev1,  3 }
        ListCorner{ iVertex, 6 }
        b lListCornersDone

        lListLightmapCorners:

        ListLightmapCorner{ iPrev2,  0 }
        ListLightmapCorner{ iPrev1,  3 }
        ListLightmapCorner{ iVertex, 6 }

        lListCornersDone:

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

    ; After the base pass, the lightmap pass when the batch has one.
    ilw.z  iPass, kVuWalkPrevAddr(vi00)
    ibne   iPass, vi00, lListEnd
    xtop   iBase
    ilw.w  iLit,  kVuBatchLightmapFlagOffset(iBase)
    ibeq   iLit,  vi00, lListEnd

    iaddiu iKickAt, iBase, kVuBatchLightmapOffset
    xgkick iKickAt

    iaddiu iTagAt, vi00, kVuLightmapListTagAddr
    iaddiu iPass,  vi00, 1
    isw.y  iTagAt, kVuWalkPrevAddr(vi00)
    isw.z  iPass,  kVuWalkPrevAddr(vi00)

    --barrier

    b lListPass

    lListEnd:

    ; A list batch may kick no window at all: the null tag waits for the
    ; state packets, which must leave the half before the program ends.
    iaddiu iKickAt, vi00, kVuNullGifTagAddr
    xgkick iKickAt

    lDone:

#endvuprog
