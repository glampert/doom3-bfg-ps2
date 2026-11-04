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
      test image with the authored graybox map (`src/tests/data/graybox_map/`).
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
- [x] `ps2::renderer::dma`: `DisplayList` in the scratchpad (two 8 KiB halves, `fromSPR`
      drains, CNT/REF/CALL tags with TTE VIFcodes, END) and two per-frame chain rings
      (`renderer::Config::chainRingBytes`, 128 KiB today; a full ring restarts at 0 rather
      than wrapping through NEXT, see 20.1); the 2D DIRECT path runs on it with the GS scene
      captures unchanged and no `FlushCache` per kick (2026-11-03).
- [x] Asynchronous frame: `dma::EndFrame` kicks without waiting and `Kick` fences on the
      previous chain, `gs::Present(..., Pipelined)` flips at an EVEN field for 30/25 fps, and a
      PAL presentation sets `com_engineHz` 50; the GS gate's `pipelined-present` check drives
      the raw frame model (4.1, 4.2) (2026-11-03). The 2D path keeps its immediate present.
- [x] `DisplayList` is the one command interface: DIRECT blocks, `FlushPaths`,
      `SetFrameConstants` (with the VIF1 double buffer), `SetProgram`, `DrawBatch`,
      `UnpackBatch` and `EndBatches`, each reserving its own ring room; tag writers are
      private and `vu::Bind` only resolves residency. The `gs` draw functions take the list
      as a parameter, `dma.h` holds the DMA tag and address encoders and `vif.h` the VIFcodes
      (host-safe, shared with the D3WD validator), and `gs`
      supplies the window transform and `DrawingTag` (`Primitive`, `VertexFormat`) the
      world's frame constants use (2026-11-04).
- [x] `ps2::renderer::vu`: program table, resident region and swap region laid out from the
      measured programs, prebuilt MPG chains (REF pieces of 256 instructions plus RET), `Bind`
      emitting one CALL only on a change, `Invalidate`, frame constants, upload and
      eviction statistics; the GS gate's `vu-passthrough` draws the first PATH1 sprite
      (2026-11-03). `kernel.i` (quake2-ps2's clipper and window macros) is imported with its
      first consumer, `world_lm.vcl`, at R1, so nothing unverified sits in the tree.
- [x] Register new units in `config/sources.mk` (`RESIDENT_SUPPORT_CXX_SRC`,
      `CORE_BACKEND_CXX_SRC`, cold units in `SIZE_OPT_CXX_SRC`); `make compiledb`
      (2026-11-03).
- [x] Gate, emulator part: `make` builds and checks a VU object; the existing 2D scene draws
      through the new chain with pixel parity in `make test-renderer`; the chain ring
      overflow is an expected-fatal probe (`renderer-direct-overflow`) (2026-11-03).
- [x] VU window overrun: a batch past its input half is refused at load in every build
      (`world_format.cpp`, VIFcodes included), and the host tests unpack every graybox packet
      through an independent VIF model that fails any write past the half (2026-11-04).
- [ ] Gate, remaining: hardware probes written for `DATE` (also the PCSX2 software renderer),
      large texture coordinates, DMA cache coherence and a `PSMCT16S` scratch sharing the
      `Z16S` buffer, run at the first console session.
- [x] Docs: `docs/testing/core.md` lists the new markers and probe, `BUILD_INVENTORY.md`
      the moved and added units (2026-11-03). The profile cvar's `CVARS.md` row moves to R1,
      where the cvar is added.

## R1: first world pixels

- [x] Cook: `asset_reexport world` producing `.d3wd` (areas, portals, nodes copied; WA
      batches with s16 positions, 4.12 UVs, strip index stream or plain strips, 64-byte
      headers; the compact CPU mirror) from a text `.proc`, `make world-cook`; host tests
      (`world_tests`: parser, strips, round trips, corruption, every packet through an
      independent VIF model) (6.1, 6.2, 15.1) (2026-11-04).
- [x] The authored graybox map: two rooms with basic shapes and a doorway portal,
      `.map`/`.proc`/materials/cook job generated by `graybox_fixture.py` and checked by
      `test_graybox_fixture.py` (winding, areas, portal facing, nodes, T-junctions)
      (2026-11-04).
- [ ] Cook mars_city1 area 2: a `.bproc` reader for the retail containers (15.2's byte
      order traps), then the same cook.
- [x] `ps2::renderer::world`: load `.d3wd` whole into a 64-byte-aligned `TAG_RENDER`
      allocation, validated in every build (`world_format.cpp`, shared with the host tests)
      and written back once; `world::Draw` REFs every batch (2026-11-04).
- [ ] The `_areaN` model adapters over the compact CPU mirror; `InitFromMap` takes portals and
      nodes from the D3WD behind a tagged seam (5.2, 5.4).
- [ ] `ps2::renderer::scene::RenderView` consuming `viewDef_t` after
      `FindViewLightsAndEntities`; frame arena initialised by `ps2` with a small tagged size;
      the removed frontend units leave the campaign list and the kept symbols move
      (5.2 table, 5.3).
- [x] `world_lm.vcl` with vertex colour only: in-place transform, the strip walk, and the
      triangle list path through the imported clipper (`vu_clip.i`) for batches leaving the
      guard band; the GS gate's `world-graybox` draws four cameras in both standards and the
      runner checks 32 pixels a standard against a reference rasterizer (2026-11-04).
- [ ] A plain-strips program variant and the VU1 cost comparison of the two topologies
      (instruction counts in PCSX2, cycles on the console) (6.1, 7.4).
- [ ] Both frame-buffer profiles selectable (`PSMCT32 + Z16S`, `PSMCT16 + Z16`); capture
      pixel model records the profile (4.3); the profile cvar gets its `CVARS.md` row.
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
