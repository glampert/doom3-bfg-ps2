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
- **PACKED ST's Q reaches the GS only with the next PACKED RGBAQ write** (the GS holds it until
  then). A vertex format of ST and XYZ2 alone draws every vertex with a stale Q: textures slide
  with the camera. Send RGBAQ after ST even when the colour is unused (DECAL).
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
- **Frame and depth formats pair by group** (GS manual 2.5.4): `PSMCT32/24/16S` only with
  `PSMZ32/24/16S`, `PSMCT16` only with `PSMZ16`. The 32-bit profile's depth is `PSMZ16S`.
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
  in one GS frame. `EndFrame` fences and presents immediately (`gs::PresentMode::Immediate`); no
  following frame is needed. `dma::EndFrame` itself only kicks: a chain runs while the EE builds the
  next frame, `Kick` waits for the previous chain before sending. Production's Common frames
  pipeline (2026-11-26, `ps2_pipelineFrames`): `renderer::EndFrame(PresentMode::Pipelined)` returns
  with the chain drawing, and the next kick waits for it and presents it first
  (`dma::BeforeNextSend`), flipping at the next vsync of either field. Memory the chain in flight
  reads must stay untouched until then: state blocks are per drawing context, texture copies wait
  for `dma::ChainDone`, and a VU program's chain is rewritten only after `WaitIdle`. Captures,
  redraws between frames and test images present immediately; `LastPresentedContext` asserts no
  present is pending. GS
  vsync paces visible mode (no headless sleep). The vsync wait **sleeps** (2026-11-24): an
  `INTC_VBLANK_S` handler signals a semaphore, then CSR's VSINT confirms the edge (the IMR still masks
  the GS interrupts, so CSR's events stay ours to poll). The background loader (`system/aio.h`) runs
  only while the main thread sleeps; never turn this wait back into a spin. `UpdateScreen` coalesces in-frame redraws, and an
  out-of-frame redraw never advances simulation; guard against recursive Begin/End. A queued
  `ps2_capture` finishes before shutdown.
- **Display modes:** `renderer::DisplayMode` is a resolution index into `system::video`'s
  per-standard table (512 or 640 wide at the standard's lines; production defaults to 640, test
  images to 512), the interlace mode and the frame-buffer profile: `gs::FrameFormat` `Color32`
  (32/32: two `PSMCT32` + `PSMZ16S`, production's in-game default), `Color32Display16` (32/16: one
  `PSMCT32` draw buffer + `PSMZ16S`, presented as a dithered copy into one `PSMCT16S` display buffer
  in the vertical blank; the front end draws a 32/32 preference this way, since two 640-wide 32-bit
  buffers leave too little texture VRAM for the menus) or `Color16` (16/16, `renderer::Config`'s
  default for test images). A loaded map (or `ps2_world` render world) switches to the preference. Live through `ps2_resolution`, `ps2_interlace` and `ps2_frameBufferBits`. A change
  restarts the GS (`renderer::SetDisplayMode`: shutdown, init, `vu::Invalidate`, `tex::MoveVram`);
  never lay the buffers out again under a running GS (PCSX2's hardware renderer then returned stale
  old-format targets to the first readback). FRAME mode (`SMODE2.FFMD` 1) draws a half-height buffer
  shown whole on every field: no flicker filter, both `DISPFB` circuits at line 0. Buffers pad to whole GS page rows (64 lines for 16-bit formats, 32 for
  `PSMCT32`) so a 224-line buffer never shares a page row with the next one. Pixel models use
  `renderer_capture.stored(rgb, bits)`, `frame_size` and `fixed_vram_bytes`.
- **Device:** reset/claim VIF1 and GIF along with the GS (CSR reset alone stalled PATH2 DIRECT).
  The display list (`dma::DisplayList`, `dma_list.h`) is the one command interface (DIRECT
  blocks, `FlushPaths`, VU1 frame constants, programs and batches; no client writes tags). It is built in the 16 KiB scratchpad and
  drained by channel 8 (fromSPR) into a per-frame chain ring the EE never writes, so a kick
  needs no `FlushCache`; a full ring kicks, waits and restarts at offset 0 (a `drains` stat).
  All packet reservations leave room for the final EOP and four-qword FINISH/END terminator;
  DMA, FINISH and VSYNC polls use wrap-safe COP0 Count deadlines in assertion builds; release
  spins. Packet bounds and state preflight are assertions too ([ps2-backend-cpp.md](ps2-backend-cpp.md#validation-and-error-handling)).
- **World pass:** `gs::BeginDepthPass`/`EndDepthPass` wrap the VU1 world draw with a VIF FLUSH and
  a DIRECT block (TEST GEQUAL with depth written, then depth writes masked again), so the state
  lands between whole PATH1 packets. ClearFrame leaves depth 1 (far); world depth is 0xFFFF at
  the near plane. A `GifWriter::SetRegister` costs two qwords (its own tag and the A+D data):
  size `BeginDirect` accordingly. Untextured vertex colour writes 0x80 as 128, half brightness,
  until a textured MODULATE pass makes 0x80 one.
- **Views:** id's `RenderScene` reaches `scene::RenderView` (`ps2/renderer/scene.cpp`) after the
  portal flow; it draws the areas whose `_areaN` entity is on the view entity list with a
  non-empty scissor (empty means shadows only), in one `world::BeginPass`/`DrawArea`/`EndPass`.
  The scissor is the portal winding's screen rectangle, projected through `tr.identitySpace`,
  which `InitLogical` must set as id's `Init` does (left zero, every rectangle collapsed onto
  the world origin and a portal at the screen edge culled its area). Cameras use a 4:3
  horizontal field of view (`system::video::kDisplayAspect`), never the buffer's width/height. The world pass's far
  plane is 65,536 units (`scene.cpp`): `world_lm.vusl` skips a triangle with a corner past it, and
  retail sky boxes reach 14,000 units from inside a level. `scene::BeginFrame` resets id's frame arena once
  per renderer frame (presentation does it). A cooked world's packets are freed only between
  renderer frames (`FreeCookedMap` waits for the DMA).
- **World textures (GSTX v2):** a file is the texture's GS memory image (levels and CLUT) plus the
  `PSMCT32` rectangles that write it; `DisplayList::UploadImage` sends it at any block (FLUSH,
  `BITBLTBUF` with `DBW 1`, REF under `DIRECT`, `TEXFLUSH`) and `RelocateTex0`/`RelocateMiptbp`
  move the cooked block addresses. Never reorder or copy its texels on the EE. Fixed-LOD draws:
  `TEX1.LCM` 1 with `K` = level << 4. `renderer::DrawingContext()` asserts an open frame.
- **Streamed world textures (2026-11-24):** a map's D3TX keeps only its tables in EE memory; each
  texture's file streams into its own copy on the loader thread. A pass requests its drawn areas'
  textures first, then the view area's portal neighbours'. It waits for them only after a jump
  (first pass, teleport, a switch turned on); otherwise a missing copy draws as a VRAM fallback
  (`stream_misses`). Copies leave over a 2 MiB budget only once `dma::ChainDone` says no chain still
  uploads from them. Details: RENDERING_ARCHITECTURE.md 10.5, "Streaming as built".
- **Textured world pass:** a map's `.d3tx` stays resident in one VRAM arena (`world_textures.cpp`:
  best fit, LRU, first evicting what this pass neither drew nor planned). The arena shares the whole
  texture range with the UI's textures (2026-11-25): a UI texture that finds no room fences, then the
  arena gives back its end (`YieldVram`, never while a pass is built). UI textures idle 30 frames
  release their pages (`tex::ReleaseIdle`) and the arena grows back over them (`GrowArena`). Each batch's state block is per material and per drawing context: a frame
  rewrites only its own context's blocks, never one the chain in flight reads.
- **Mid-pass eviction (2026-11-25):** when a view's textures exceed the arena, a batch's diffuse or
  page may evict a texture the pass drew or still needs. The victim is the one whose next use in the
  pass's plan is furthest (`AdvanceBatch` walks the plan, pinning the current batch's two). This is
  safe because the GS runs the chain in order: an upload lands after every batch that drew from those
  blocks (`UploadImage` FLUSHes first). The pass's first victim links each planned use to its
  texture's next one (`LinkUses`), and `AdvanceBatch` keeps every texture's next use from there:
  never walk the plan per victim (it cost about 285 µs of EE each on a console, 2026-11-26). **Never rewrite a state block the pass already REF'd**: the
  DMA reads it later, so earlier batches would draw from the new address. A texture that moves
  within a pass takes a moved block from a per-context pool (`CurrentBlock`). The pass draws its
  batches grouped by texture (`BatchOrderKey`, fixed order every frame so coplanar surfaces never
  trade places), then their material passes, and the plan covers both; the sky box evicts through it
  too. A texture this pass needs no more ranks by its first use in the plan, as if it repeats. `world::PlanArea` names a pass's areas first; the pass makes their textures resident
  before drawing and repacks the arena when a view's new set will not fit around the old (PAL's
  north view needs it: first fit packed it by luck).
  `TEX1.K` = log2(UV per unit) + log2(texture size) - log2(GS pixels per unit at depth 1).
  `ps2_world textures on|off|status`; the untextured captures and the lightmap-only ones (below)
  are held to references, the textured ones are checked by eye.
- **Lightmap pass:** the frame's *other* GS context (contexts alternate per frame) is set for it
  by `gs::BeginDepthPass` (this frame's buffer with alpha masked, ZBUF masked, GEQUAL, ALPHA
  `(Cd - 0) x As + 0`, bilinear LOD 0, clamp) and handed back its own frame by `EndDepthPass`.
  Both contexts share **one CLUT buffer**: a page's `TEX0` (CLD 1) loads its CLUT, so the program
  kicks it only after the base pass's windows; never put it in the material's A+D packet. Pages
  are `PSMT8` ramps (alpha = index, 0x80 = 1.0, up to 0xFF), drawn with `TFX` DECAL OR'd in at
  run time. Alpha-tested surfaces get no lightmap pass (it cannot test the diffuse's alpha and the
  GS has no EQUAL depth test): the cook lights them per vertex. `ps2_world lightmaps on|off`.
  The retail gate holds every lightmap-only capture to `world_reference.py`'s model of the pass
  (`--lightmaps`): the cook's `<stem>.lightmaps.raw` (the pages read back out of the D3TX) sampled
  bilinearly at the perspective-correct lightmap UV, times the vertex colour, `>> 7`, saturated.
- **Material passes** (D3TX v3, 2026-11-21): a material's base pass takes its alpha test
  reference from the cook (`TEST.AREF`, 0x80 = 1.0) and its stage colour in the vertex colours;
  its additive stages (glow maps) are passes the world pass draws after its areas, each batch
  again with the pass's texture, ABE set (the base context's ALPHA is `(Cs - 0) x FIX + Cd`, FIX
  1.0, from `gs::BeginDepthPass`) and a constant colour every vertex takes. The same vertices give
  the same depth, so GEQUAL lands a pass only where its surface is nearest, in any order. Colours
  and conditions that time drives are cooked expression nodes the EE evaluates once a frame. Pass
  textures sit at the arena's top and are evicted first: a frame's diffuse textures never give
  way to them (`pass_fallbacks` counts the passes left out). Passes draw only textured.
- **Sky** (D3TX v4, 2026-11-23): the textured pass draws the map's sky box first (six cooked faces,
  DIRECT fans with STQ over w, cut on the EE to the guard band, depth untouched), then every sky
  surface into depth alone (`ATST NEVER`, `AFAIL ZB_ONLY`) before the other batches. The faces give
  way first, even after they drew: their `TEX0` is in the chain's DIRECT data, no state block. Any
  pixel no surface covers shows the sky (a doorway without its door model).
- **Translucent materials** (`kPackHidden` in the D3TX, 2026-11-25) have no base pass: their stages
  are passes drawn after the opaque glows, phase by phase (`PassPhase`: lit diffuse, multiply, add,
  alpha). Each phase opens with `gs::SetPassBlend` (it starts the pending batch, FLUSHes, then writes
  ALPHA and ZBUF): depth stays tested GEQUAL and is no longer written. Multiply is `Cd x As` with the
  image's luminance cooked into the texture's alpha (TCC 1, MODULATE with vertex alpha 0x80); the lit
  diffuse takes the batch's vertex colours (the unlit page block). Untextured passes draw them as any
  surface. The retail gate's
  reference rasterizes the cooked D3WD's own triangles (`world_reference.load_cooked_triangles`),
  since a lit cook's vertex colours are the baked chroma.
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
