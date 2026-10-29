# GS renderer facts (Doom 2D path, plus Quake II hardware findings)

Digest only. Dated acceptance details, measurements, the dialog/shell drawing history, the
format survey, console charset details, and the Quake II frame model and mipmap notes are in
[docs/reference/gs-renderer.md](../../docs/reference/gs-renderer.md). Hardware facts below
come from `quake2-ps2` and still need checking against Doom's renderer for each design.
Section index of the reference: Doom renderer interface boundary; Native atlas pages; CPU-only
device acceptance; RGBA and capture acceptance; native drawing adapters; production console
frame ownership; SWF draw scope; additive blending; convex masks; full retail dialog drawing;
paletted UI textures (GSTX); console charset; pre-rendered glyphs; outlines/color terms/screen/
depth masks; retail UI page format survey; Quake frame model; GS and libdraw; texture
coordinates; CLUTs; VRAM and mipmaps.

## GS hardware traps

- **Alpha 0x80 = 1.0.** 0xFF is ~2x overbright. Engine alpha converts with `(a*128+127)/255`
  (or `a >> 1`); vertex RGB 0x80 is identity in MODULATE.
- ALPHA computes `(A - B) * C + D` with **C a scalar alpha** (As/Ad/FIX), never a colour.
  Colour x colour is not expressible; check before promising any two-texture multiply.
- libdraw `draw_enable_blending()` is a static read at primitive emission (PRIM.ABE), not a
  register write. Set it per emission.
- `draw_setup_environment()` defaults to CLAMP and alpha test NOTEQUAL 0 (discards A==0 texels).
- **SCISSOR doesn't clip HOST->LOCAL uploads**; writes past the destination width wrap in VRAM.
- **One XGKICK with several GIF tags: only the last tag may set EOP** (PRE=1 on each tag).
- **TEST.ZTE must be 1** (ZTST ALWAYS, ZMSK 1 when not using depth); ZTE 0 is prohibited by the
  manual though PCSX2 tolerates it. Every primitive writes TEST.
- **Normalized ST spans the TEX0 TW/TH extent**, the image rounded up to a power of two, not the
  image. The 2D path uses texel (`PRIM_MAP_UV`) coordinates.
- **CLUT CSM1 reorder:** in each 32-entry group the two middle 8-entry blocks swap:
  `(i & ~0x18) | ((i & 0x08) << 1) | ((i & 0x10) >> 1)`. A CLUT uploads as 16x16 PSMCT32,
  4 blocks at CBP (blocks = word address >> 6), DBW=1. Scrambled-but-stable colours mean a missed reorder.
- **CLD:** the Quake backend binds indexed TEX0 with CLD=4 (compare CBP0) and never changes CLUT
  contents after Init. The Doom GSTX path instead writes **CLD 1** on every textured TEX0 (below).
- **TBW must be even for PSMT8/PSMT4** (128-px stride). Page sizes: PSMT8 128x64, PSMT4 128x128
  (64 rows of PSMT8 / 128 of PSMT4), PSMCT32 64x32, PSMCT16 64x64; `dest_width >= 64` for transfers.
- **VRAM is addressed in 256-byte blocks**, not pages. Size a texture by its far-corner block
  (`vram::TextureFootprintWords`), not whole pages.
- GS pixel centers sit at integer window coordinates, texel centers at +0.5.
- Framebuffer expected pixels: model **five-bit storage after every blend** (PSMCT16 truncates;
  capture expands 5 bits to 8) and keep the **eight-channel tolerance**; ideal float RGB needs a
  growing tolerance. FBMSK bits for the 32-bit pixel apply to PSMCT16 too: R 7:3, G 15:11, B 23:19, A 31.
- GS LINE draws its start pixel but not its end. COLCLAMP is on, so sums saturate.
- Do not trust PCSX2 for GS cost, cache coherence or hardware timing; captures gate regressions only.

## Doom 2D path invariants

- **Layers:** logical adapters in `src/ps2/renderer/` bind native `tr`/`renderSystem`; headless
  creates no GS setup or pixel arenas. Explicit visible `renderer`/`ui` activation adds the
  bounded 2D capabilities; world submission, general shaders, cinematics and vertex-cache buffers
  fail by method name. Only native `ui::Init` sets `R_IsInitialized`; OpenGL stays unavailable.
- Start the logical renderer frame counter above zero (`R_EntityDefDynamicModel` compares to
  `tr.frameCount`). Stage-free materials are not drawn; CPU `ModelTrace` needs the `collision` flag.
- **Frame ownership:** package-root `presentation.cfg` (missing = headless) is read before Common
  startup. One Common frame = events/commands, clocks and NOMAP work, then movie/menu/console draw
  in one GS frame. `EndFrame` fences and presents immediately; no following frame is needed. GS
  vsync paces visible mode (no headless sleep). `UpdateScreen` coalesces in-frame redraws, and an
  out-of-frame redraw never advances simulation; guard against recursive Begin/End. A queued
  `ps2_capture` finishes before shutdown.
- **Device:** reset/claim VIF1 and GIF along with the GS (CSR reset alone stalled PATH2 DIRECT).
  All packet reservations leave room for the final EOP and four-qword FINISH/END terminator;
  DMA, FINISH and VSYNC polls use wrap-safe COP0 Count deadlines in assertion builds; release
  spins. Packet bounds and state preflight are assertions too ([ps2-backend-cpp.md](ps2-backend-cpp.md#validation-and-error-handling)).
- **Texture hazards:** queued TEX0 references are hazards even before a kick. Dirty reupload,
  eviction, destruction and image close drain/FINISH (flush queued UI references) before
  overwriting or releasing VRAM. Uploads retire PATH2 before synchronous PATH3 chains.
- **Shutdown order:** movies, then fonts, then UI images/geometry, then the caller-owned device.
- **UI collection** owns 4,096 vertices, 6,144 indices and 128 runs; fill each returned span before
  the next allocation; invalid requests are asserted before flushing. Half UVs are decoded from bits
  (assert exponent below 31). `color2` must be exactly 128 outside the SWF scope. Assertion builds
  reject unsupported blend/mask/depth state instead of normalizing it. Console coordinates map once from 640x480 to GS 1/16.
- Textured triangles reserve 30 qwords and write TEX1/TEX0/CLAMP/ALPHA/TEST/PRIM every time, so
  command-half drains cannot inherit state. Degenerate triangles are skipped.
- **Native atlas pages / `idImage`:** native `idImage` keeps the full logical atlas dimensions;
  `SWF_Render` derives UVs from the atlas material and `GetScaledGlyph` expands UVs by half a
  texel. Preserve UVs; never clamp them or rescale the texture. Canonical page-intersection
  rules are in the reference.

## Native atlas pages

Retail UI draws only resident GSTX images (`PalettedImage`); the streamed UIPG page cache was
removed. The surviving rules: split geometry at page boundaries from the canonical original
triangle and shared constraints (chaining clips gave a 1/16-pixel seam mismatch), keep that math
single precision on the EE, and (assertion builds) validate the whole queued collection before emitting packets under
the collector's reentry guard. Full text: reference, "Native atlas pages".

## Paletted GSTX images

- PSMT8/PSMT4 pages without padding (widths multiples of 128, heights whole GS pages, max
  512x512); PSMT4 stores the even texel in the low nibble. CLUTs share a 64-wide PSMCT32 texture
  of 16x16 tiles; its alpha goes through the RGBA `(a*128+127)/255` path.
- A page and its CLUT bind together (`tex::BindPair`): neither may evict the other, and both
  addresses are final before either upload. `gs::Upload` checks only layout/address; draws add
  the CLUT block and clamp region.
- Every textured primitive writes TEX0 with CPSM PSMCT32, CSM1, CSA 0 and **CLD 1**.
- A triangle picks its placement by **UV centroid** (nearest within one texel as fallback;
  empty glyph quads included) and samples with **region clamp** on the placement rectangle.
- Load checks tables and every CLUT/page FNV before publication; the owner keeps the EE copy for
  re-upload after eviction.

## SWF draw scope

- Native SWF draws inside a bounded scope: flush before entering/leaving, restore saved native
  state/color, never cross a frame or shutdown boundary. Inside it, zero additive color may be 127
  or 128 (treated as zero); do not premultiply vertex RGB twice.
- **Additive (mode 8):** ALPHA `(Cs - 0) * As + Cd`, ABE on; straight RGBA input.
- **Color terms:** second pass with PRIM.FGE, fog coefficient 0, FOGCOL = term + 1 adds
  `add x alpha`; negative terms use `(0 - Cs) * As + Cd`. Additive alpha terms stay refused.
- **Screen (mode 4):** the PC result is `Cd + Cd x S`; one channel per pass with FBMSK and channel
  CLUTs; RGBA pages cannot do it.
- **Masks:** convex masks (<= 64 triangles, 16 boundary points, four active, 64 planes) clip on
  the CPU; every other mask goes to the Z16 depth mask (depth 0 written, masked content tests
  GEQUAL, removal rewrites 1). Only one depth mask at a time; two are refused. Flush before every
  mask mutation; key masks by display entry; empty masks clip everything.
- **Outlines:** line records draw in both directions as GS lines, one pixel wide.

## Pre-rendered glyphs

Menu text draws baked glyph bitmaps at the exact on-screen size. Texel-exact copies under bilinear
need rectangles spanning whole texels, starting half a pixel above/left of a whole pixel (1/16 fixed
point makes that exact); sample texel centers so region clamp never reaches a neighbor. Glyph and
outline share a rectangle in equal halves, drawn as two rectangles. The console instead draws the
catalog's `bigchars` at full resolution through the paged triangle path.

## Quake II reference (labeled)

The Quake frame model (2D->3D->2D interleave, deferred present, TEST register layout), mipmap/LOD
and VRAM-heap notes are Quake-only; see the reference file before reusing them.
