; =================================================================================================
; world_lm.vcl
;
; One cooked world batch (D3WD, layout WA: src/tools/assets/world.h): the base pass, its material's
; diffuse times the vertex colour (the light's chroma), then, when the batch has a lightmap, the
; lightmap pass in the other GS context, Cd x As with the page's intensity. Built by oxide-vcl,
; whose preprocessor (-G) runs first; --vcl-boilerplate adds the VCL boilerplate.
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
    iand   iOffset, iEntry, iMask
    iadd   iVertex, iBase,  iOffset
    iaddiu iAdc,    iFlagBit, 0

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
    iand   iOffset, iEntry, iMask
    isub   iAdc,    iEntry, iOffset
    iadd   iVertex, iBase,  iOffset

    EmitLightmapVertex{ 0 }

    iaddiu iOutPtr,    iOutPtr,     3
    iaddi  iVertsLeft, iVertsLeft, -1
#endmacro

#macro LightmapReentry
    iand   iOffset, iEntry, iMask
    iadd   iVertex, iBase,  iOffset
    iaddiu iAdc,    iFlagBit, 0

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
;           for (pass : base, lit ? lightmap : none)                // the tag and pass also in vuMem
;           {
;               if (pass == lightmap) xgkick(batch + kVuBatchLightmapOffset);
;               OpenWindow(pass == base ? batch[kVuBatchListTagOffset] : vuMem[kVuLightmapListTagAddr]);
;               for (int left = 4 * q; left > 0; --left)             // state in registers
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

    ; 1e-20 in w, added to every clip position after the matrix: a w of any real size absorbs it,
    ; but one that cancels to exactly 0 (a vertex in the eye's plane, as on grid-aligned cameras)
    ; keeps Q = 1 / w finite. With Q at the largest float the clipper's rebuilt w (1 / Q) flushes
    ; to 0 and takes the corner's UV (S x Q x w) with it.
    sub    fWBias, vf00, vf00
    loi    1.0e-20
    muli.w fWBias, vf00, i

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
        madda acc,  fMVP3,  vf00[w]
        madd  fClip, fWBias, vf00[w]

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
    ; The entries' offset mask, for every walk below.
    iaddiu iMask, vi00, 0x7FFF

    OpenBatchWindow{ kVuBatchStripTagOffset }

    iaddiu iIdx,    iBase, 2
    ilw.w  iQwords, 0(iBase)

    lStripWindow:

        ; A window holds kVuWindowStripQwords stream qwords: the first one 48 of
        ; its 50 vertices, every later one the two re-sent entries and 48 more.
        ; This is where a room test per qword would close it.
        iaddiu iRun,  vi00,    kVuWindowStripQwords
        isub   iDiff, iQwords, iRun
        ibgez  iDiff, lStripRun
        iaddiu iRun,  iQwords, 0
        lStripRun:
        isub   iQwords, iQwords, iRun

        lStripQword:

            ; Not software-pipelined: the loop is bound by its 11 lower-pipe
            ; instructions an entry, and pipelining gains under 1% of the strip
            ; path's cycles for about a second more of every build.
            --LoopCS 0,0

            ilw.x iEntry, 0(iIdx)
            StripEntry{ }
            ilw.y iEntry, 0(iIdx)
            StripEntry{ }
            ilw.z iEntry, 0(iIdx)
            StripEntry{ }
            ilw.w iEntry, 0(iIdx)
            StripEntry{ }

            iaddiu iIdx, iIdx,  1
            iaddi  iRun, iRun, -1
            ibgtz  iRun, lStripQword

        ; Qwords left: kick the full window and start the next one with the
        ; previous qword's last two entries again, so a strip spanning the kick
        ; keeps its two pending vertices.
        ibeq iQwords, vi00, lStripEnd
        CloseOutputWindowAndKick{ lStripFull }
        OpenBatchWindow{ kVuBatchStripTagOffset }
        ilw.z iEntry, -1(iIdx)
        StripReentry{ }
        ilw.w iEntry, -1(iIdx)
        StripReentry{ }
        b lStripWindow

    lStripEnd:

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

    lLightmapWindow:

        iaddiu iRun,  vi00,    kVuWindowStripQwords
        isub   iDiff, iQwords, iRun
        ibgez  iDiff, lLightmapRun
        iaddiu iRun,  iQwords, 0
        lLightmapRun:
        isub   iQwords, iQwords, iRun

        lLightmapQword:

            ; Not software-pipelined, as the base pass's loop.
            --LoopCS 0,0

            ilw.x iEntry, 0(iIdx)
            LightmapEntry{ }
            ilw.y iEntry, 0(iIdx)
            LightmapEntry{ }
            ilw.z iEntry, 0(iIdx)
            LightmapEntry{ }
            ilw.w iEntry, 0(iIdx)
            LightmapEntry{ }

            iaddiu iIdx, iIdx,  1
            iaddi  iRun, iRun, -1
            ibgtz  iRun, lLightmapQword

        ibeq iQwords, vi00, lLightmapEnd
        CloseOutputWindowAndKick{ lLightmapFull }
        OpenLightmapWindow{ }
        ilw.z iEntry, -1(iIdx)
        LightmapReentry{ }
        ilw.w iEntry, -1(iIdx)
        LightmapReentry{ }
        b lLightmapWindow

    lLightmapEnd:

    CloseOutputWindowAndKick{ lLightmapLast }
    b lDone

    ; ---------------------------------------------------------------------
    ; Phase 2, list path: some vertex left the guard band.
    ;
    ; The walk's state is in registers: the entries left (iLeft, whose low two
    ; bits also count the lanes left in the rotating qword fIndices), the next
    ; stream qword (iNext), the two vertices before this entry's (iPrev1,
    ; iVertex) and the pass (iPass, never written by the walk, so unswitch
    ; gives each pass its own copy of it). Two values are in VU memory, where
    ; they are read: kVuWalkPrevAddr .y the pass's drawing tag (a window open)
    ; and .z the pass (the clipper's seed). The batch base is read again with
    ; xtop rather than held, which keeps the walk within the VI registers the
    ; clipper leaves.
    ; ---------------------------------------------------------------------
    lListPath:

    iaddiu iTagAt, iBase, kVuBatchListTagOffset
    iaddiu iPass,  vi00,  0

    lListPass:

    ; The pass's drawing tag, for every window the walk opens, and the pass for
    ; the clipper's seed (vu_clip.i): both read where they are used, from memory.
    isw.y  iTagAt, kVuWalkPrevAddr(vi00)
    isw.z  iPass,  kVuWalkPrevAddr(vi00)

    OpenWalkWindow{ }

    ilw.w  iQwords, 0(iBase)
    iadd   iLeft,   iQwords, iQwords
    iadd   iLeft,   iLeft,   iLeft
    iaddiu iNext,   iBase,   2

    ; The first entry opens a strip, so these are never read before the walk
    ; sets them; they only need a value.
    iaddiu iPrev1,  iBase, 0
    iaddiu iVertex, iBase, 0
    lq     fIndices, 2(iBase)

    lListEntry:

        ; Room for the most one clipped triangle can fan out to: five planes
        ; leave at most eight corners, six triangles. Asked once, before the
        ; corners load.
        isubiu iRoom, iVertsLeft, 18
        ibgez  iRoom, lListRoom
        CloseOutputWindowAndKick{ lListFull }
        OpenWalkWindow{ }
        lListRoom:

        ; The next entry: lane x of the rotating qword, refilled every four
        ; entries, when the count left is a multiple of four.
        iaddiu iLanes,   vi00,   3
        iand   iLanes,   iLeft,  iLanes
        ibne   iLanes,   vi00,   lListHaveLanes
        lq     fIndices, 0(iNext)
        iaddiu iNext,    iNext,  1
        lListHaveLanes:

        mtir   iEntry,   fIndices[x]
        mr32   fIndices, fIndices

        ; The triangle this entry closes: the two vertices before it and its own.
        xtop   iBase
        iaddiu iMask,   vi00,    0x7FFF
        iaddiu iPrev2,  iPrev1,  0
        iaddiu iPrev1,  iVertex, 0
        iand   iOffset, iEntry,  iMask
        isub   iAdc,    iEntry,  iOffset
        iadd   iVertex, iBase,   iOffset

        ; An opening entry closes no triangle.
        ibne iAdc, vi00, lListNext

        ilw.w iF0, 1(iPrev2)
        ilw.w iF1, 1(iPrev1)
        ilw.w iF2, 1(iVertex)
        ior   iF0, iF0, iF1
        ior   iF0, iF0, iF2
        ibne  iF0, vi00, lListClip

        ; Every corner inside: the three phase 1 vertices as they are, with
        ; the lightmap ST in the lightmap pass. The walk never writes iPass,
        ; so oxide-vcl's unswitch gives each pass its own copy of the walk.
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

        iaddi iLeft, iLeft, -1
        ibgtz iLeft, lListEntry

    CloseOutputWindowAndKick{ lListLast }

    ; After the base pass, the lightmap pass when the batch has one.
    ibne   iPass, vi00, lListEnd
    xtop   iBase
    ilw.w  iLit,  kVuBatchLightmapFlagOffset(iBase)
    ibeq   iLit,  vi00, lListEnd

    iaddiu iKickAt, iBase, kVuBatchLightmapOffset
    xgkick iKickAt

    iaddiu iTagAt, vi00, kVuLightmapListTagAddr
    iaddiu iPass,  vi00, 1
    b lListPass

    lListEnd:

    ; A list batch may kick no window at all: the null tag waits for the
    ; state packets, which must leave the half before the program ends.
    iaddiu iKickAt, vi00, kVuNullGifTagAddr
    xgkick iKickAt

    lDone:

#endvuprog
