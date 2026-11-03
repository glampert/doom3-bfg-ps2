# Rendering implementation TODO

Companion to [RENDERING_ARCHITECTURE.md](RENDERING_ARCHITECTURE.md): the same work as its
section 19 gates, broken into concrete steps in the proposed order. Opened **2026-11-03**.
Tick items as they land; move a step's measurements into the plan's decision log
(section 20) and its acceptance into [PORT_STATUS.md](../PORT_STATUS.md). Every step keeps
the headless and 2D gates green (`make test-headless`, `test-renderer`, `test-ui`) and the
production link free of test inputs. Section numbers in parentheses point into the plan.

## Prerequisites outside the renderer

- [ ] Production `Frame` calls the game's draw path: a `RenderScene` step inside the
      presentation bracket, after the game tick and before the console draw (5.2, 19).
- [ ] Common owns a loaded map in production (M3 work in
      [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)); until then R0 and R1 use the game
      test image's authored map.
- [ ] Decide and record the PC reference capture method (Wine on this Mac, or a Windows
      machine) for R2 (20.3).

## R0: toolchain, display list, frame model

- [x] Import `vclpp` (with nested `parse-utils`) and `vu-checker` as pinned submodules under
      `src/external/`; record both in `REUSE.md` and the pins in
      [vclpp-submodule.md](../../.claude/rules/vclpp-submodule.md); CI checks out submodules
      (2026-11-03).
- [x] Build the pinned vclpp into `build/tools/vclpp` from the Makefile (the reference's
      out-of-tree `BIN_TARGET` rule, rebuilt when the submodule's sources change)
      (2026-11-03).
- [x] Add the Makefile VU rule (`vclpp -I ... -Wundef -Werror -j`, `openvcl`, `openvcl -c`,
      `dvp-as`, `check_vu_code.py`, delete the object on failure), a `VCL_FILES` list and the
      `.vutext` link into every image; CI installs `g++` for vclpp, and the image's `openvcl`
      and `dvp-as` are proven by quake2-ps2's build on the same digest
      ([ci.md](../../.claude/rules/ci.md)). `passthrough.vcl` (6 instructions, with a
      null-tag drain guard) and `vu.h` are the first program and header; the core gate's
      `renderer/passthrough-microprogram` check reads them (2026-11-03).
- [x] Import `qwords.h`, `math/vec_mat.{h,cpp}` and `PS2_DECLARE_VU_MICROPROGRAM` from
      quake2-ps2 with `REUSE.md` rows; the VU0 asm and the qword copies are proven against
      scalar references by the core gate's `math/*` checks rather than a one-off test ELF
      (2026-11-03).
- [x] Mechanical move of `cmd_buffer.*` to `dma.{h,cpp}` under `ps2::renderer::dma`
      (`dma::Init/BeginFrame/BeginDirect/Kick/WaitIdle/GetStats`) (2026-11-03).
- [ ] `ps2::renderer::dma`: `DisplayList` in the scratchpad (two 8 KiB halves, `fromSPR`
      drains, CNT/REF/CALL tags with TTE VIFcodes, END, one NEXT per ring wrap) and two
      384 KiB chain rings (12.1, 18.1).
- [ ] Asynchronous frame: build chain N+1 while N runs, FINISH fence at the frame boundary,
      flip on the second field for 30/25 fps; `com_engineHz 50` on PAL (4.1, 4.2).
- [ ] `ps2::renderer::vu`: program table, fixed kernel region, 940-slot swap region,
      `Bind` emitting MPG chains only on a change, `SwapsThisFrame`; `kernel.i` with
      quake2-ps2's clipper and window macros, `passthrough.vcl` (7.1, 7.3).
- [ ] Register new units in `config/sources.mk` (`RESIDENT_SUPPORT_CXX_SRC`,
      `CORE_BACKEND_CXX_SRC`, cold units in `SIZE_OPT_CXX_SRC`); `make compiledb`.
- [ ] Gate: `make` builds and checks a VU object; the existing 2D scene draws through the
      new chain with pixel parity in `make test-renderer`; expected-fatal probes for a chain
      ring overflow and a VU window overrun; hardware probes written for `DATE` (also the
      PCSX2 software renderer), large texture coordinates, DMA cache coherence and a
      `PSMCT16S` scratch sharing the `Z16S` buffer, run at the first console session.
- [ ] Docs: `CVARS.md` rows for the profile cvar, `docs/testing/` entry for the new probes,
      `BUILD_INVENTORY.md` for the moved units.

## R1: first world pixels

- [ ] Cook: `asset_reexport world` producing `.d3wd` (areas, portals, nodes copied; WA/WC
      batches with s16 positions, 4.12 UVs, strip index stream, 64-byte headers) for one
      authored room and mars_city1 area 2 (6.1, 6.2, 15.1); host tests for the format.
- [ ] `ps2::renderer::world`: load `.d3wd` into a 64-byte-aligned DMA arena, the compact CPU
      mirror, the `_areaN` model adapters; `InitFromMap` takes portals and nodes from it
      behind a tagged seam (5.2, 5.4).
- [ ] `ps2::renderer::scene::RenderView` consuming `viewDef_t` after
      `FindViewLightsAndEntities`; frame arena initialised by `ps2` with a small tagged size;
      the removed frontend units leave the campaign list and the kept symbols move
      (5.2 table, 5.3).
- [ ] `world_lm.vcl` with vertex colour only; VU1 counter test of the strip index walk
      against plain ADC strips (6.1, 7.4).
- [ ] Both frame-buffer profiles selectable (`PSMCT32 + Z16S`, `PSMCT16 + Z16`); capture
      pixel model records the profile (4.3).
- [ ] Gate: fixed-camera captures of the room in both profiles; area 2 draws with correct
      depth and portal culling; decisions on profile, topology and FIELD versus FRAME mode
      written into section 20.

## R2: lightmapped world and streaming

- [ ] Cook: light classification A to F per map; lightmap baker for category A (occlusion
      against world and `.bcm`, id's light materials evaluated in the tool); chart
      extraction, skyline packing, 1-texel borders; chroma subdivision to 64 units;
      Light8 atlases as GSTX v2 (8.2, 8.3, 15.1).
- [ ] Cook: GSTX v2 (mips, wrap, usage, constant-alpha CLUT variant) with the YCoCg and
      normal-map DXT5 decoders and a retail `(format, colorFormat)` census (10.4, 15.1).
- [ ] Cook: material pass tables for opaque and alpha-tested materials, cavity bake into the
      diffuse, Q2 policy (10.1, 10.2, 15.3).
- [ ] Runtime: lightmap pass (`Cd x As`, GEQUAL, ZMSK 1) through the second context; sky
      program; `ps2::renderer::tex` streaming per area ring with pinning of in-flight
      textures, `FLUSHA; DIRECT` uploads inside the chain, block-granular `gs::VramArena`
      (10.5).
- [ ] Pull the declaration-text lever (4.76 MB on demand) before measuring the level ledger
      (16.1).
- [ ] Gate: mars_city1 camera path; area, portal and node counts and sampled
      `PointInArea`/`AreasAreConnected` match the proc loader; a door portal toggle still
      gates sound; captures against PC `screenshot`s (`r_skipSpecular 1`) within the
      eight-level pixel model and SSIM 0.9; three map load/unload cycles and one load-game
      return the ledger to baseline; per-device read rates recorded; first hardware frame
      log.

## R3: layers, switches, fog and blend lights

- [ ] Cook: layers for categories B, C, D per switch group and waveform (8.2, 8.3).
- [ ] Runtime: layer passes through destination alpha with the light shader registers
      evaluated per frame; `r_lightScale` as a per-frame global or dropped (8.1, 8.2).
- [ ] Fog and blend lights through the projective STQ path (8.6).
- [ ] Gate: scripted switches and a 60-frame fade in mars_city1 match the PC within the
      recorded tolerance; the fade is monotonic in the captures.

## R4: models, skinning, animation

- [ ] Cook: `.d3md` (static and brush entity models with baked vertex colour, CPU mirror),
      `.d3sk` (weight classes, joint-set sort, 24-joint palettes, 40-vertex strips),
      `.d3an` (s16 sparse); re-run the batcher at the 40 cap and record duplication (11.1,
      15.1).
- [ ] Runtime: `model_rigid.vcl`, `model_skin2.vcl`, `model_skin4.vcl`; PS2 SIMD processor
      joint conversion in VU0 macro mode; `jointsInverted` on the EE; palettes written into
      the display list; `idDrawVert` mirrors for traces, decals and overlays (11.2 to 11.4).
- [ ] Existing `make test-game` and `test-map` fixtures draw their entities.
- [ ] Gate: imp idle and walk cycles within one unit of the PC pose on sampled joints;
      `ModelTrace` unchanged against the fixtures; exact ledgers across reloads; display-list
      ring peak measured and the rings resized (12.1).

## R5: dynamic lights

- [ ] Runtime: four-light per-vertex path in `world_lit.vcl` and the model programs; light
      binding per batch by intensity x falloff; the projected two-pass path; flashlight and
      muzzle flash; entity light sets with the baked ambient sample; per-vertex Blinn
      specular through `HIGHLIGHT2`; alpha ownership per pass sequence (8.4, 8.5, 10.3).
- [ ] Cook: smooth light images folded into falloff polynomials; patterned images and
      `flashlight5` as `PSMT8` cookies (8.4).
- [ ] Spike: CLUT-relight normal mapping on one brick quad (10.2); adopt or drop in 20.1.
- [ ] Gate: flashlight capture on the three-plane test room; a moving light over a
      lightmapped floor; VU1 cost per vertex per light measured.

## R6: shadows L0 and L1

- [ ] `shadow_planar.vcl` and the A-bit mask pass for planar and blob shadows (9.3, 9.4).
- [ ] Flashlight projected shadow texture (128x128 target, STQ projection) behind the
      Flashlight Shadows option; `CVARS.md` row (9.3).
- [ ] Gate: overlapping shadow triangles darken once; projected silhouette within one texel
      of the analytic box.

## R7: the rest of the draws

- [ ] `particles.vcl` from 16-byte records (particle and particle2 deforms); sprite, tube,
      flare, expand, move, eyeball and turbulent deforms (13.1).
- [ ] Decals over the world mirror; overlays over the `idDrawVert` mirror with the skinned
      layout (13.2).
- [ ] In-world GUIs through the 2D collector with the surface matrix; one bounded subview;
      `_currentRender` heat haze; portal sky as the cooked skybox; `videomap` still frames
      (13.3, 10.1).
- [ ] Gate: mars_city1 start sequence captures (monitors, particles, hallway heat haze)
      against PC references.

## R8: UI on PATH1

- [ ] `ui_2d.vcl` and run building in the collector; depth-mask rework (Z clear per mask
      rectangle or the A bit with `DATE`) (14).
- [ ] Gate: every `make test-ui` capture matches the DIRECT path within the pixel model.

## R9: optimisation and hardware

- [ ] Batch merging per material and area; program-swap minimisation; PATH3 upload
      interleave if uploads measure as a stall; LOD and mip tuning; PAL profile decision.
- [ ] L2 counting stencil for at most two lights if the hardware frame log leaves room
      (9.2, 9.3); shadow-rung cvar in `CVARS.md`.
- [ ] Gate: hardware frame log on the camera path, p95 at most 33.3 ms NTSC and 40.0 ms
      PAL, no frame above 50 / 60 ms; memory ledgers inside section 16 at every area
      transition.
