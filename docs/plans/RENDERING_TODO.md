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
      The bracket's step exists since 2026-11-05 for the `ps2_world` viewer's fixed camera;
      the game's own views (`idGameLocal::Draw`, the player view) wait for a loaded map.
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
- [x] Cook mars_city1 area 2: a `.bproc` reader for the retail containers (15.2's byte
      order traps), then the same cook (2026-11-07). `assets::ParseBinaryProc` reads
      `generated/maps/game/mars_city1.bproc` to its exact end; the job
      (`config/world_mars_city1.json`, `make world-cook-retail`) names the container and cooks
      the whole map: 56 areas, 50,332 triangles in 2,138 batches, 2.7 MB. `make
      test-world-retail` draws area 2 from the player start and the corridor beyond its doorway,
      each capture matching a rasterization of the `.bproc` at 99.7 to 99.9 %
      (`docs/images/renderer-world-7-*`). Two findings: the world pass's far plane moved from
      8,192 to 65,536 units, since the sky box around area 2 spans 14,000 and `world_lm.vcl`
      skips a triangle with a corner past it (depth goes as 1 / distance, so the plane costs no
      precision); and the retail material names need declarations in a test package, or id's
      declaration manager makes implicit materials whose images the PS2 refuses.
- [x] `ps2::renderer::world`: load `.d3wd` whole into a 64-byte-aligned `TAG_RENDER`
      allocation, validated in every build (`world_format.cpp`, shared with the host tests)
      and written back once; `world::Draw` REFs every batch (2026-11-04).
- [x] The `_areaN` model adapters over the compact CPU mirror; `InitFromMap` takes portals and
      nodes from the D3WD behind a tagged seam (5.2, 5.4): `world_map.cpp` builds the areas,
      portals, nodes and one `idRenderModelStatic` per area whose surfaces reference the
      mirror's indexes and carry its s16 positions on `srfTriangles_t`, which `R_LocalTrace`
      decodes; a map without a `.d3wd` still loads its `.proc` (2026-11-05).
- [x] `ps2::renderer::scene::RenderView` consuming `viewDef_t` after
      `FindViewLightsAndEntities`, drawing the world areas the portal flow listed; frame
      arena initialised by `ps2` with a small tagged size (one 128 KiB arena, 16-byte
      rounding); `RenderScene` without a gui model or post-process pass (5.2 table, 5.3)
      (2026-11-05).
- [x] The removed frontend units leave the campaign list and the kept symbols move (5.2
      table): `tr_frontend_addlights`, `tr_frontend_addmodels`, `Interaction`, the four
      shadow-volume job units, `tr_frontend_deform` and `GuiModel` are PS2 replacements now (271
      campaign units). `R_EntityDefDynamicModel`, `R_IssueEntityDefCallback` and
      `R_ClearEntityDefDynamicModel` moved into `RenderEntity.cpp`; interaction creation and
      freeing are tagged out; the decal and overlay draw-surface emitters are tagged out until
      their PS2 replacements (13.2); `DeviceContext`'s glyph fast path uses
      `renderSystem->AllocTris`. Runnable `PT_LOAD` fell 95,424 bytes debug and 72,768 release
      (2026-11-07).
- [ ] World decals over the compact mirror (`ModelDecal.cpp` reads `idDrawVert`s, which world
      area surfaces no longer have) before the game projects one on a loaded map (5.4).
- [x] Gate (production): `make test-world` loads the cooked graybox into Common's render
      world with `ps2_world` and draws it through `RenderScene` in both standards: the GS
      gate's four cameras at its reference pixels, area counts from portal culling (a view
      facing away and the doorway closed draw one area), the closed doorway changing only
      the opening, three traces through the mirror, unload and the startup ledger
      (2026-11-05).
- [x] `world_lm.vcl` with vertex colour only: in-place transform, the strip walk, and the
      triangle list path through the imported clipper (`vu_clip.i`) for batches leaving the
      guard band; the GS gate's `world-graybox` draws four cameras in both standards and the
      runner checks 32 pixels a standard against a reference rasterizer (2026-11-04).
- [x] A plain-strips program variant and the VU1 cost comparison of the two topologies
      (6.1, 7.4): `world_strips.vcl` (swap region) drawn for a level cooked with `--topology
      strips` (`make world-cook-retail WORLD_TOPOLOGY=strips`), the level validator refusing a
      world that mixes topologies, `make test-world-retail` passing on both cooks (view batches
      now taken from the cook report), and `vu_cost.py` counting strip-path cycles over the
      cooked batches: 93.0 cycles per triangle for the hybrid, 122.5 for plain strips, with 19 %
      more packet bytes. The hybrid stays; cycles on the console remain a hardware item
      (2026-11-07).
- [x] Both frame-buffer profiles selectable (`PSMCT32 + Z16S`, `PSMCT16 + Z16`); capture
      pixel model records the profile (4.3); the profile cvar gets its `CVARS.md` row:
      `gs::FrameFormat` in the device config, `renderer::SetFrameFormat` restarting the GS
      between frames (textures move above the new buffers and upload again, the texture range
      capped by what is left), 32-bit capture readback with `bits=` on capture lines, the
      runners' `stored()`/`tolerance()` model per profile, and the live `ps2_frameBufferBits`
      cvar; the GS gate's `world-graybox-32` and `frame-format-switch`, and `make test-world`
      redrawing its six views at 32 bits (2026-11-05).
- [x] Display modes selectable live (4.2): `system::video`'s resolution table (512 and 640
      wide per standard) and interlace mode (FIELD, FRAME with a half-height buffer),
      `renderer::DisplayMode`/`SetDisplayMode` replacing the profile-only setter, captures up
      to 640 wide, `ps2_resolution` and `ps2_interlace` with their `CVARS.md` rows; the GS
      gate's `display-mode-switch` and `make test-world`'s 640 and FRAME doorway views
      (2026-11-05). Defaults stay 512 wide, FIELD.
- [x] The world viewer's and the GS gate's horizontal field of view from the display's 4:3
      aspect instead of the buffer's width over height (`presentation.cpp` `ps2_world view`,
      `renderer_gs_tests.cpp` `WorldGraybox`, `system::video::kDisplayAspect`): every display
      mode now shows the same framing. `WORLD_SCENE_PIXELS` and `WORLD_SCENE_PIXELS_640`
      rasterized again, and `make test-world`'s FRAME doorway is checked against the references
      at half their rows (2026-11-07). Comparing whole captures with the new references found
      a culling defect: the PS2's `InitLogical` never set `tr.identitySpace`'s model matrix (id's
      `Init` does), so every portal's screen rectangle collapsed onto the projected world origin
      and the `clip` view listed room 1 with an empty scissor and skipped it; it now draws both
      areas. The rasterizer is `src/tools/scripts/world_reference.py` (tables in
      `graybox_map/scene_pixels.json`, staleness checked by `make test-host`; `--compare` for
      whole captures).
- [x] 640 wide by default with a third frame-buffer profile (2026-11-06): 32/16 draws in one
      `PSMCT32` buffer and presents a dithered `PSMCT16S` copy made in the vertical blank;
      `ps2_frameBufferBits 16/16|32/16|32/32` (default 32/32) names the in-game preference and the
      front end draws 32/32 as 32/16; a loaded map or `ps2_world` render world switches to the
      preference. Glyph sets re-cooked for 640, world reference pixels at 640, the GS gate's
      32/16 step with a display-buffer capture, `make test-world` switching on load and unload.
- [x] Gate: fixed-camera captures of the room in both profiles (done for the graybox,
      2026-11-05: `docs/images/renderer-world-3-*`); area 2 draws with correct depth and
      portal culling (done 2026-11-07, `make test-world-retail`); decisions on profile, topology and FIELD versus FRAME mode written into
      section 20. The profile is decided: `PSMCT32 + Z16S` by default (2026-11-05); the topology
      is decided: the strip index hybrid (2026-11-07); FIELD is kept for now (FRAME stays live for
      a TV judgement). R1 is complete (2026-11-07).

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
- [ ] Confirm the 640-wide default (`ps2_resolution 1`, 4.2, default since 2026-11-06): measure
      the in-game texture working set per area against the 1,327,104 bytes NTSC (32 bits) and
      2,097,152 PAL (16 bits) the buffers leave, and the fill and VU1 cost on the hardware frame
      log; fall back to 512 (`ps2_resolution 0`, glyph sets re-cooked for it) only where 640 does
      not fit, and record the outcome in section 20. Two 32-bit 640-wide buffers thrash the retail
      menus, which is why the front end draws in 32/16 and a map switches to the preference.
- [ ] Follow-up: 480p progressive output (`GRAPH_MODE_HDTV_480P`, 4.2) as another entry in the
      resolution table, for component cables: 640x480 with no interlace flicker, presented
      every second vsync. 3,072,000 bytes of fixed VRAM at 32 bits (about 1.1 MB left for
      textures), about 1.84 MB at 16 bits; judge it against the same texture working set as
      the 640 experiment.
- [ ] L2 counting stencil for at most two lights if the hardware frame log leaves room
      (9.2, 9.3); shadow-rung cvar in `CVARS.md`.
- [ ] Gate: hardware frame log on the camera path, p95 at most 33.3 ms NTSC and 40.0 ms
      PAL, no frame above 50 / 60 ms; memory ledgers inside section 16 at every area
      transition.
