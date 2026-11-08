; =================================================================================================
; vu_clip.i
;
; Sutherland-Hodgman clipping on VU1 against the near plane and the four guard band sides,
; adapted from quake2-ps2's vu_clip.i: a corner is three qwords here (the clip-space position, the
; colour as floats and the texture coordinates before the divide), so nothing in it is a bit
; pattern and every lane is interpolated; attributes interpolate linearly in clip space.
;
; A polygon lives in one of two scratch buffers (vu_memory.h) with its first corner repeated at
; the end, so the edge walk needs no wrap test; a pass reads one buffer and writes the other, and
; five passes leave the survivors in B. Buffer A holds 8 entries (7 corners plus the wrap) and B 9,
; three qwords each.
; The corner count travels through kVuClipCountAddr, never a register: openvcl's liveness does
; not carry a value from one loop into a sibling loop and hands the register to a temporary.
;
; Signs come from the float distances, clamped and read through ftoi4 and mtir, rather than from
; the clip flag register, so openvcl cannot reorder them away. A macro that places labels takes a
; prefix, unique to each invocation.
;
; This source code is released under the GNU GPL-3.0-or-later license.
; =================================================================================================

; Seeds buffer A with a triangle's corners, read from the transformed batch
; vertices at iA, iB and iC: the clip position (qword 0, its w rebuilt as 1/Q,
; since phase 1 keeps the lightmap T x Q there), the colour bytes (qword 2)
; widened to floats, and the pass's UV back from the projected STQ (qword 3)
; as S * w, all of which the cut interpolates. The pass (walk state .z, 0 base
; or 1 lightmap) picks the UV: the base S x Q and T x Q (qword 3 .xy) or the
; lightmap's (qword 3 .w and qword 0 .w), blended by it as a float.
;
;   float pass = itof(vuMem[kVuWalkPrevAddr].z);
;   for (corner : a, b, c)
;   {
;       vec2 st = lerp(v[3].xy, { v[3].w, v[0].w }, pass);
;       float w = 1 / v[3].z;
;       pos = { v[0].xyz, w }; col = itof(v[2]); uv = st * w;
;   }
;   bufA = { pos(a), col(a), uv(a), pos(b), ..., uv(c), pos(a), col(a), uv(a) };
;   clipCount = 3;
#macro SeedCorner: iV, vPos, vColI, vCol, vUV, vLm
    lq    vPos,  0(iV)
    lq    vColI, 2(iV)
    lq    vUV,   3(iV)

    add.x  vLm,  vf00, vUV[w]
    add.y  vLm,  vf00, vPos[w]
    sub.xy vLm,  vLm,  vUV
    mul.xy vLm,  vLm,  fPass[z]
    add.xy vUV,  vUV,  vLm

    div    q,    vf00[w], vUV[z]
    mul.w  vPos, vf00,    q
    mul.xy vUV,  vUV,     vPos[w]
    itof0  vCol, vColI
#endmacro

#macro ClipSeedTriangle: iA, iB, iC
    lq    fPass, kVuWalkPrevAddr(vi00)
    itof0.z fPass, fPass

    SeedCorner{ iA, fPos0, fColI0, fCol0, fUV0, fLm0 }
    SeedCorner{ iB, fPos1, fColI1, fCol1, fUV1, fLm1 }
    SeedCorner{ iC, fPos2, fColI2, fCol2, fUV2, fLm2 }

    sq fPos0, kVuClipBufA + 0(vi00)
    sq fCol0, kVuClipBufA + 1(vi00)
    sq fUV0,  kVuClipBufA + 2(vi00)
    sq fPos1, kVuClipBufA + 3(vi00)
    sq fCol1, kVuClipBufA + 4(vi00)
    sq fUV1,  kVuClipBufA + 5(vi00)
    sq fPos2, kVuClipBufA + 6(vi00)
    sq fCol2, kVuClipBufA + 7(vi00)
    sq fUV2,  kVuClipBufA + 8(vi00)
    sq fPos0, kVuClipBufA + 9(vi00)
    sq fCol0, kVuClipBufA + 10(vi00)
    sq fUV0,  kVuClipBufA + 11(vi00)

    iaddiu iCount, vi00, 3
    isw.x  iCount, kVuClipCountAddr(vi00)
#endmacro

; One pass against one plane, over a polygon of any size. 'vSelCur'/'vSelNxt'
; name the lane of the judgement vector, or of its negation, carrying this
; plane's term (G is the guard band scale in fClipScale):
;
;   near   fJn?[z]   w - z
;   x-     fJn?[x]   w - G*x        x+   fJp?[x]   w + G*x
;   y-     fJn?[y]   w - G*y        y+   fJp?[y]   w + G*y
;
; w is shrunk by fGSScale[w] first so a cut lands strictly inside the plane,
; and the distance is scaled by fGSOffset[w] so the ftoi4 sign survives the
; product of two distances (quake2-ps2's kVuClipShrink and kVuClipDistScale).
; The two endpoints' chains are interleaved by hand: openvcl will not overlap
; them on its own. There is no far plane; a triangle past it is rejected whole.
;
;   int left = clipCount; clipCount = 0;
;   if (left >= 3)
;   {
;       qword * in = src, * out = dst; int count = 0;
;       do
;       {
;           float dCur = dist(in[0]), dNxt = dist(in[3]);
;           if (dCur >= 0) { out[0..2] = in[0..2]; out += 3; ++count; }
;           if (dCur * dNxt < 0) { t = dCur / (dCur - dNxt); out[0..2] = lerp(in[0..2], in[3..5], t); out += 3; ++count; }
;           in += 3;
;       } while (--left > 0);
;       if (count >= 3) { out[0..2] = dst[0..2]; clipCount = count; }
;   }
#macro ClipPlanePass: vSelCur, vSelNxt, srcBuf, dstBuf, lbl
    ilw.x  iCorners, kVuClipCountAddr(vi00)

    ; Dead unless proven otherwise: only the surviving path overwrites it.
    iaddiu iCount, vi00, 0
    isw.x  iCount, kVuClipCountAddr(vi00)

    iaddi iSignCur, iCorners, -3
    ibltz iSignCur, lbl##Out

    iaddiu iWalk, vi00, srcBuf
    iaddiu iOut,  vi00, dstBuf

    lbl##Loop:

        lq fCurPos, 0(iWalk)
        lq fCurCol, 1(iWalk)
        lq fCurUV,  2(iWalk)
        lq fNxtPos, 3(iWalk)
        lq fNxtCol, 4(iWalk)
        lq fNxtUV,  5(iWalk)

        mul.xyz fJpC,  fCurPos, fClipScale
        mul.xyz fJpN,  fNxtPos, fClipScale
        addw.x  fDCur, vf00,    fCurPos
        addw.x  fDNxt, vf00,    fNxtPos
        sub.xyz fJnC,  vf00,    fJpC
        sub.xyz fJnN,  vf00,    fJpN
        mul.x   fDCur, fDCur,   fGSScale[w]
        mul.x   fDNxt, fDNxt,   fGSScale[w]
        add.x   fDCur, fDCur,   vSelCur
        add.x   fDNxt, fDNxt,   vSelNxt
        mul.x   fDCur, fDCur,   fGSOffset[w]
        mul.x   fDNxt, fDNxt,   fGSOffset[w]

        ; This corner's sign and the product's (negative exactly when the edge
        ; crosses), clamped to [-1, +1] so ftoi4 fits the 16 bits mtir moves.
        mul.x   fClipP,    fDCur,   fDNxt
        mini.x  fClipT,    fDCur,   vf00[w]
        mini.x  fClipP,    fClipP,  vf00[w]
        max.x   fClipT,    fClipT,  fMinusOne[x]
        max.x   fClipP,    fClipP,  fMinusOne[x]
        ftoi4.x fClipT,    fClipT
        ftoi4.x fClipP,    fClipP
        mtir    iSignCur,  fClipT[x]
        mtir    iSignProd, fClipP[x]

        ibltz iSignCur, lbl##Kept
        sq fCurPos, 0(iOut)
        sq fCurCol, 1(iOut)
        sq fCurUV,  2(iOut)
        iaddiu iOut,   iOut,   3
        iaddiu iCount, iCount, 1
        lbl##Kept:

        ibgez iSignProd, lbl##NoCut

        ; The cut: t = dCur / (dCur - dNxt), held in a register for both lerps.
        sub.x  fClipT, fDCur,    fDNxt
        div    q,      fDCur[x], fClipT[x]
        addq.x fClipQ, vf00,     q

        sub fCutPos, fNxtPos, fCurPos
        sub fCutCol, fNxtCol, fCurCol
        sub fCutUV,  fNxtUV,  fCurUV
        mul fCutPos, fCutPos, fClipQ[x]
        mul fCutCol, fCutCol, fClipQ[x]
        mul fCutUV,  fCutUV,  fClipQ[x]
        add fCutPos, fCurPos, fCutPos
        add fCutCol, fCurCol, fCutCol
        add fCutUV,  fCurUV,  fCutUV

        sq fCutPos, 0(iOut)
        sq fCutCol, 1(iOut)
        sq fCutUV,  2(iOut)
        iaddiu iOut,   iOut,   3
        iaddiu iCount, iCount, 1
        lbl##NoCut:

        iaddiu iWalk,    iWalk,    3
        iaddi  iCorners, iCorners, -1
        ibgtz  iCorners, lbl##Loop

    ; Fewer than three survivors (a corner exactly on the plane can leave two)
    ; is nothing; otherwise repeat the first corner at the end.
    iaddi  iSignCur, iCount, -3
    ibgez  iSignCur, lbl##Wrap
    iaddiu iCount,   vi00, 0
    lbl##Wrap:

    ibltz iSignCur, lbl##Out
    lq fCurPos, dstBuf + 0(vi00)
    lq fCurCol, dstBuf + 1(vi00)
    lq fCurUV,  dstBuf + 2(vi00)
    sq fCurPos, 0(iOut)
    sq fCurCol, 1(iOut)
    sq fCurUV,  2(iOut)
    isw.x iCount, kVuClipCountAddr(vi00)
    lbl##Out:
#endmacro

; Guard band judgement for a clipped corner before it is emitted, as an
; unclipped vertex is judged. Leaves its flags newest in the clip flag register.
#macro ClipJudge: vPos
    mul.xyz   fJudge, vPos, fClipScale
    clipw.xyz fJudge, vPos[w]
#endmacro
