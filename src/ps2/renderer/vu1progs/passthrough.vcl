; =================================================================================================
; passthrough.vcl
;
; The smallest VU1 microprogram: kicks the GIF packet the EE unpacked
; into this batch's double-buffer half, untouched. It carries what has
; to be ordered with PATH1 output but needs no VU work - SIGNAL/LABEL
; fences and A+D register blocks between batches - and the first R0
; frames, which draw the existing 2D scene through the new chain
; before any transform program exists. Preprocessed with vclpp; -j
; adds the VCL boilerplate (.init_*, --enter/--exit).
;
; The second XGKICK is a hazard guard, not a second packet. XGKICK is
; asynchronous and the E bit ends the program as soon as the kick is
; *issued*. MSCAL waits for the program to end, not for its kick to
; drain, so VIF1 proceeds to the next unpacks while the GIF may still
; be reading this half; the unpack after the next targets this half
; again and would overwrite the packet under the GIF. An XGKICK issued
; while the previous PATH1 transfer is in progress stalls the VU until
; that transfer ends (VU User's Manual, XGKICK), and a GIF tag with
; NLOOP 0 and EOP 1 outputs nothing (EE User's Manual, GIFtag: "when
; NLOOP is 0, the GIF does not output anything"). Kicking the null tag
; therefore ends the program only once its packet has left VU memory,
; and the ordinary double buffer is protection enough.
;
;   qword * packet = xtop();
;   xgkick(packet);                    // asynchronous
;   xgkick(&vuMem[kVuNullGifTagAddr]); // stalls until the kick above has drained
;
; This source code is released under the GNU GPL-3.0-or-later license.
; =================================================================================================

#include "vu_memory.h"

#vuprog passthrough
    xtop   iPacket
    xgkick iPacket

    iaddiu iNullTag, vi00, kVuNullGifTagAddr
    xgkick iNullTag
#endvuprog
