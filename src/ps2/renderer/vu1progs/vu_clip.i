; =================================================================================================
; vu_clip.i
;
; Sutherland-Hodgman clipping on VU1 against the near plane and the four guard band sides,
; adapted from quake2-ps2's vu_clip.i: a corner is two qwords here (the clip-space position and
; the colour as floats), so nothing in it is a bit pattern and every lane is interpolated.
;
; A polygon lives in one of two scratch buffers (vu_memory.h) with its first corner repeated at
; the end, so the edge walk needs no wrap test; a pass reads one buffer and writes the other, and
; five passes leave the survivors in B. Buffer A holds 8 entries (7 corners plus the wrap) and B 9.
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
; vertices at iA, iB and iC: the clip position (qword 0) and the colour bytes
; (qword 2) widened to floats, which the cut interpolates.
;
;   bufA = { pos(a), col(a), pos(b), col(b), pos(c), col(c), pos(a), col(a) };
;   clipCount = 3;
#macro ClipSeedTriangle: iA, iB, iC
    lq    fPos0,  0(iA)
    lq    fColI0, 2(iA)
    lq    fPos1,  0(iB)
    lq    fColI1, 2(iB)
    lq    fPos2,  0(iC)
    lq    fColI2, 2(iC)

    itof0 fCol0, fColI0
    itof0 fCol1, fColI1
    itof0 fCol2, fColI2

    sq fPos0, kVuClipBufA + 0(vi00)
    sq fCol0, kVuClipBufA + 1(vi00)
    sq fPos1, kVuClipBufA + 2(vi00)
    sq fCol1, kVuClipBufA + 3(vi00)
    sq fPos2, kVuClipBufA + 4(vi00)
    sq fCol2, kVuClipBufA + 5(vi00)
    sq fPos0, kVuClipBufA + 6(vi00)
    sq fCol0, kVuClipBufA + 7(vi00)

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
;           float dCur = dist(in[0]), dNxt = dist(in[2]);
;           if (dCur >= 0) { out[0..1] = in[0..1]; out += 2; ++count; }
;           if (dCur * dNxt < 0) { t = dCur / (dCur - dNxt); out[0..1] = lerp(in[0..1], in[2..3], t); out += 2; ++count; }
;           in += 2;
;       } while (--left > 0);
;       if (count >= 3) { out[0..1] = dst[0..1]; clipCount = count; }
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
        lq fNxtPos, 2(iWalk)
        lq fNxtCol, 3(iWalk)

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
        iaddiu iOut,   iOut,   2
        iaddiu iCount, iCount, 1
        lbl##Kept:

        ibgez iSignProd, lbl##NoCut

        ; The cut: t = dCur / (dCur - dNxt), held in a register for both lerps.
        sub.x  fClipT, fDCur,    fDNxt
        div    q,      fDCur[x], fClipT[x]
        addq.x fClipQ, vf00,     q

        sub fCutPos, fNxtPos, fCurPos
        sub fCutCol, fNxtCol, fCurCol
        mul fCutPos, fCutPos, fClipQ[x]
        mul fCutCol, fCutCol, fClipQ[x]
        add fCutPos, fCurPos, fCutPos
        add fCutCol, fCurCol, fCutCol

        sq fCutPos, 0(iOut)
        sq fCutCol, 1(iOut)
        iaddiu iOut,   iOut,   2
        iaddiu iCount, iCount, 1
        lbl##NoCut:

        iaddiu iWalk,    iWalk,    2
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
    sq fCurPos, 0(iOut)
    sq fCurCol, 1(iOut)
    isw.x iCount, kVuClipCountAddr(vi00)
    lbl##Out:
#endmacro

; Guard band judgement for a clipped corner before it is emitted, as an
; unclipped vertex is judged. Leaves its flags newest in the clip flag register.
#macro ClipJudge: vPos
    mul.xyz   fJudge, vPos, fClipScale
    clipw.xyz fJudge, vPos[w]
#endmacro
