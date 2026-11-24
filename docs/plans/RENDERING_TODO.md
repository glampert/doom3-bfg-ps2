# Rendering implementation TODO

Companion to [RENDERING_ARCHITECTURE.md](RENDERING_ARCHITECTURE.md): the same work as its
section 19 gates, broken into concrete steps in the proposed order. Opened **2026-11-03**;
status as of **2026-11-24**: R0 and R1 done but for the hardware probes, R2 in progress (the
lightmapped, textured world with material passes and the sky is done, declarations read their
text on demand and the world's textures stream; chroma subdivision and the R2 gate remain), R3 to R9
not started.
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
      machine) (20.3). Deferred on 2026-11-24 until the frame is more complete: R2's gate no
      longer compares with the PC, and the first comparison is R7's start-sequence gate, once
      models, lights and particles draw.

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
- [x] Replace vclpp, openvcl and vu-checker with oxide-vcl (`src/external/oxide-vcl`, built
      by cargo, oxide mode with every optimization), CI installing Rust; tune the world
      programs for it: the transform pipelined, the strip loops a window at a time, the walk
      state in registers, no ordering barriers (2026-11-16).
- [x] Write the VU programs in VUSL and build them with oxide-vusl: `passthrough`, `world_lm`
      and `world_strips` from the VCL ones, the clipper a library resident at 1536 that both
      world programs call (`vu::Bind` uploads it once), and `vu.h` taking the data memory map
      from the generated headers; same kicks and vertices over mars_city1, 3-6% more VU1 cycles
      a triangle (2026-11-21).
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
- [x] World decals over the compact mirror (`ModelDecal.cpp` reads `idDrawVert`s, which world
      area surfaces no longer have) before the game projects one on a loaded map (5.4):
      `CreateDecal` culls and clips the s16 positions (`R_CompactPosition`, shared with the traces)
      and reads the borrowed indexes directly; `scene::RenderView` builds a visible entity's pending
      decals, the step of the replaced model pass; `ps2_world decal`/`decals` project and report
      them, and `make test-world` / `test-world-retail` require a floor decal to cover its square
      exactly (2,304 and 1,024 units²) once a view sees its area, and none before or after the
      world. The stand-in materials take `forceOverlays`, since id refuses decals on materials
      without stages (2026-11-07). Drawing them is R7's decal pass.
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

- [x] Cook: light classification A to F per map; lightmap baker for category A (id's light
      materials and projections evaluated in the tool, occlusion by rays against the area models
      and the `func_static` brush models); chart extraction, skyline packing, 1-texel borders;
      Light8 pages as GSTX v2 in the D3TX (8.2, 8.3, 15.1; 2026-11-08: mars_city1's 410 class A
      lights into 8 pages, alpha-tested surfaces lit per vertex).
- [ ] Cook: chroma subdivision to 64 units; the `.bcm` and mesh models (`func_static` `.lwo`/`.ase`)
      as occluders (8.2, 8.3).
- [x] Gate: a pixel reference for the lightmap pass (2026-11-14): every lightmap-only capture of the
      retail gate against the cooked pages sampled at each pixel's lightmap UV; it rejects the
      stale-Q captures and found clipped corners in the eye's plane losing their UVs (fixed).
- [x] Cook: static `func_static` brush models merge into the area models before the bake
      (2026-11-08, `"static_models": true`): mars_city1's 108 whole and 15 partial models, 3,625
      triangles, drawn, lit, shadowed and traced as area geometry.
- [ ] The `func_static` models the cook leaves out: 35 with GUIs (GUI surfaces), 13 bound (R4),
      189 flares, glass and glow decals coloured by their entity (material passes, with the entity's
      parms), and 311 mesh models (`.lwo`/`.ase`, model import). Material passes also need per-surface
      parms for the 93 merged opaque surfaces whose glow stages read them.
- [x] Mesh model import and census (2026-11-24): the cook reads the retail binary render models
      (`generated/rendermodels/*.blwo`, `.base`; `ParseBinaryModel`, the `.bproc` model parser) of
      every map entity naming a `.lwo` or `.ase`, and reports them (`mesh_models`). mars_city1: 118
      files (24,048 triangles, 1.6 MB), none missing; 311 `func_static`s place 46,377 triangles, 42
      doors 2,446, 3 movers 624, 10 lights 116. The merge rules admit 234 `func_static`s with 30,376
      triangles (29,844 inside areas; 1,433 translucent, 288 reading parms). They keep 53 with GUIs
      (11,983 triangles), 17 with skins (2,406), 4 bound and 3 more. Merged, they would raise the world's
      53,957 triangles by 55% and the worst area with its neighbours from 10,898 to 15,991 (area 5);
      the start area 2 from 1,808 to 2,923.
- [ ] Merge the static mesh props into the areas, lit per vertex rather than in lightmap charts;
      the GUI props but for their GUI surfaces, and the skinned ones with the skin's materials.
- [ ] Rigid entity models for doors, movers, bound models, moveables and items (R4's rigid half):
      cooked batches per area streamed with the area ring, drawn by the world program with a
      per-entity matrix, at the cook's rest pose until the game loads retail maps (M3).
- [x] Debug free-flight camera (2026-11-08): `ps2_world fly` steers the viewer with the pad,
      `make run-world-retail` stages mars_city1 flying from the player start, and the retail gate
      flies a scripted path to a modelled pose and matches its capture (debug builds).
- [x] Retail `(format, colorFormat)` census (`texture_census.py`, 2026-11-08): five encodings
      plus RGB565 light images; the plan's 10.4 records it and the memory-image format.
- [x] Cook: GSTX v2 memory images (GS local memory emulation, level and CLUT placement, mips,
      wrap, usage) with their reader and a sampler through the cooked registers, and the
      target's validator (`texture_format.cpp`), held to each other by `texture_format_tests`
      (10.4, 2026-11-08).
- [x] Cook: YCoCg and normal-map DXT5 and RGB565 decoders; coverage merged into the CLUT
      alpha; each world material's diffuse texture cooked into `<map>.d3tx` (`asset_reexport
      world` with a `textures` section; mars_city1: 258 textures, 1.98 MB of VRAM; 2026-11-08)
      (10.4, 15.1).
- [x] Runtime: GSTX v2 validation, the in-chain upload (`DisplayList::UploadImage`: `BITBLTBUF`,
      REF of the stream, `TEXFLUSH`) and register relocation; the renderer gate's `gstx-images`
      draws every level of a `PSMT8` and a `PSMT4` fixture with fixed LOD at unaligned blocks
      and the runner matches every pixel with the fixture's formulas (2026-11-08).
- [x] Cook: material pass tables for opaque and alpha-tested materials, cavity bake into the
      diffuse, Q2 policy (10.1, 10.2, 15.3; 2026-11-21). D3TX v3: each material's alpha test
      reference from its register, its diffuse stage colour folded into the vertex colours, and
      its additive stages as passes whose colours and conditions fold the surface's parms and keep
      time as cooked expression nodes the EE evaluates once a frame; merged `func_static` surfaces
      keep their entity's parms as material variants (26 on mars_city1); bump maps baked into the
      diffuse as cavity shading at the format the unbaked texture qualifies for. Q2's halving and
      PSMT4 choice were in the texture cook since 2026-11-08.
- [x] Runtime: material passes (2026-11-21): after the world pass's areas, each batch of a material
      with passes is sent again per pass with the pass's texture, additive tags (ALPHA
      `(Cs - 0) x FIX + Cd` in the base context) and a constant colour every vertex takes; their
      textures take the arena's top end and are evicted first, so a frame's diffuse textures never
      give way to them (PAL's 640 KB arena drops the north view's glows instead).
- [ ] Material passes still to come: blend, filter and modulate stages of translucent materials
      (25 on mars_city1, `kPackHidden` today; `Cd x As` with the stage's luminance in alpha, 10.1),
      texture matrices (constant ones foldable into the UVs; mars_city1 has two diffuse stages
      with `scale`), cube-map and texgen stages (the gloss and environment pass, 10.3), and stage
      `clamp`.
- [x] Runtime: textured world pass (2026-11-08): `world_lm.vcl`/`world_strips.vcl` send STQ, each
      batch carries its material's texture state, and `world_textures.cpp` keeps the map's D3TX
      resident in a block-granular VRAM arena with LRU eviction and in-chain uploads; the
      retail gate captures every view textured too (6.3, 10.5).
- [x] Runtime: lightmap pass (`Cd x As`, GEQUAL, ZMSK 1) through the second context
      (2026-11-08): `world_lm.vcl` walks a lit batch a second time after kicking the page's `TEX0`,
      the pages share the texture arena, `ps2_world lightmaps on|off`; the retail gate captures each
      view lit by its lightmaps alone and textured (6.1, 6.3, 8.2).
- [x] Sky (2026-11-23, D3TX v4): the cook turns the first sky material's cube map (`texgen skybox`
      or `wobblesky`) into six one-level faces with the stage's colour; the textured pass draws
      them as a box around the eye under everything it draws (DIRECT, STQ, clipped on the EE to
      the guard band), and every sky surface into depth alone before the other batches, so the box
      shows through them and hides what lies behind. The faces give way to the diffuse textures
      (PAL's north view leaves the sky out). mars_city1 has no portal sky: its windows use
      `textures/skies/commoutside`, a camera cube map (10.1, 13.3).
- [x] Runtime: residency planned per pass (2026-11-23): the scene names the areas a pass draws first,
      the pass makes their textures and pages resident before drawing and never evicts them, and a
      view whose new textures do not fit around the resident ones empties the arena and takes them
      in draw order (a repack); the PAL north view fits only with that packing (10.5).
- [x] Runtime: streaming the EE copies per area ring from the package (10.5, 2026-11-24). Measured
      first: per-segment dictionaries lose to single textures on mars_city1, whose wall set repeats
      across the whole portal tree (at least 2.2 MB for the worst area and its neighbours against
      1.65 MB plus the sky). The D3TX keeps only its tables resident (12,504 bytes); each texture's
      file streams into its own copy on the background loader thread (`system/aio.h`, one priority
      below the main thread, which the present's vsync wait now lets run: a vblank semaphore). The
      areas a pass draws are read first, then the view area's portal neighbours; a miss draws as a
      VRAM fallback, and only a jump (first pass, teleport, a switch turned on) waits. The cook lays
      the files out in a portal walk and the runtime requests each area's in file order (43% of
      reads skip their seek, 9% before). Copies beyond 2 MiB leave least recently wanted first,
      once no chain uploads from them. The retail gate's copies peaked at 1,213,888 bytes against
      the 3,047,744-byte package, with the same captures byte for byte.
- [x] Declaration text on demand (2026-11-24, 16.1): a file's declarations keep only where their
      text is and its checksum, and read it again from the container when they parse
      (`fileSystem->ReadFileRange`, the range alone: opening a container entry reads it whole).
      Retail `startup-game` backing 9,810,624 to 5,389,092 bytes (debug), 3.6 s less boot in
      PCSX2; the debug retail gate reads all 11,947 declarations back (1.1 s in order, 3.4 s
      scattered).
- [ ] Gate: mars_city1 camera path; area, portal and node counts and sampled
      `PointInArea`/`AreasAreConnected` match the proc loader; a door portal toggle still
      gates sound; three map load/unload cycles and one load-game
      return the ledger to baseline; per-device read rates recorded (the loader's `aio::GetStats`
      counts bytes, chunks and the cycles inside the reads), and the streaming's misses on a
      console's USB and HDD; first hardware frame log.

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
- [x] Shared resident VU library first (7.3): the clipper is a VUSL library resident at micro
      address 1536 (474 instructions), uploaded once by `vu::Bind` and called with `jalr` from both
      world programs (2026-11-21). Fan emit and the window kicks are still inlined per program;
      move them into the library if the model programs repeat them.
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
- [ ] Decals drawn as WC batches (generated over the world mirror since 2026-11-07); overlays
      over the `idDrawVert` mirror with the skinned layout (13.2).
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
- [ ] Devirtualise the hot renderer interfaces: the PS2 links exactly one renderer backend, so
      id's `virtual` on per-frame `idRenderSystem`/`idRenderWorld` methods (2D draws such as
      `DrawStretchPic`/`SetColor`/`AllocTris`, entity and light updates, `RenderScene`) only
      buys an indirect call and blocks inlining. Spell those `ID_RENDERER_VIRTUAL` in id's
      headers (`virtual` off the PS2, empty under `ID_PS2`), keeping the classes and callers
      unchanged. Callers see only the interface header, so the interface's own declarations
      must be the ones the backend defines: the pure `= 0` needs a companion macro (empty under
      `ID_PS2`), and the PS2 backend then defines `idRenderSystem::DrawStretchPic` and friends
      (or the override in `tr_local.h` drops out). Measure call counts and the static
      instruction delta first ([performance.md](../../.claude/rules/performance.md)); PCSX2
      charges per instruction, so the win shows only as a net instruction cut.
- [ ] L2 counting stencil for at most two lights if the hardware frame log leaves room
      (9.2, 9.3); shadow-rung cvar in `CVARS.md`.
- [ ] Gate: hardware frame log on the camera path, p95 at most 33.3 ms NTSC and 40.0 ms
      PAL, no frame above 50 / 60 ms; memory ledgers inside section 16 at every area
      transition.
