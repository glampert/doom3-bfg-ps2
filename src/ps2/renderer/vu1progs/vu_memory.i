;--------------------------------------------------------------------
; vu_memory.i
;
; The VU1 data memory map (qword addresses) the microprograms share
; with ps2/renderer/vu.h, which mirrors every value here. Change both
; together; vu.h's comments say what each region holds.
;--------------------------------------------------------------------
#pragma once

; Frame constants: MVP rows, GS scale and offset, clip scale, fog.
#define kFrameConstantsAddr 0

; Per-pass GIF tag and A+D templates. The first is a null GIF tag
; (NLOOP 0, EOP 1), the drain guard passthrough.vcl kicks.
#define kGifTemplatesAddr   8
#define kNullGifTagAddr     8
