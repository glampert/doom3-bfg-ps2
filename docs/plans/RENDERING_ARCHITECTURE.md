# Rendering architecture plan: Doom 3 BFG on the PlayStation 2

Opened **2026-11-02**. This is the design and implementation plan for the 3D renderer: the
frame pipeline, the VU1 microprogram suite, the seam into id's renderer, lighting, shadows,
materials, skinning, the asset re-export and the budgets. It supersedes the renderer rows
of [IMPLEMENTATION_PLAN.md §8](IMPLEMENTATION_PLAN.md#8-renderer-adaptation) and
continues from the accepted 2D work in [RENDERER_2D_PLAN.md](RENDERER_2D_PLAN.md).
Nothing below is implemented yet; every section states what is verified (source, manual,
retail data) and what is a design decision or an estimate. [RENDERING_TODO.md](RENDERING_TODO.md)
breaks the gates of section 19 into ordered implementation steps.

The evidence behind this plan was gathered on 2026-11-02 from id's source in `src/neo`,
the retail data in `gamedata/d3_bfg`, the GS/EE/VU manuals
(`~/Repos/GitHub/PS2Docs/markdown`), the `quake2-ps2` reference and the Black and
TimeSplitters Future Perfect reverse-engineering notes. Retail numbers quoted below were
measured on the extracted `mars_city1` and `alphalabs1` containers and on all 60 map
containers where stated.

Contents:

1. [Goals and constraints](#1-goals-and-constraints)
2. [What the reference games teach](#2-what-the-reference-games-teach)
3. [Hardware facts that shape the design](#3-hardware-facts-that-shape-the-design)
4. [Frame architecture](#4-frame-architecture)
5. [The seam into id's renderer](#5-the-seam-into-ids-renderer)
6. [Scene data on the PS2](#6-scene-data-on-the-ps2)
7. [VU1 microprograms and their manager](#7-vu1-microprograms-and-their-manager)
8. [Lighting](#8-lighting)
9. [Shadows](#9-shadows)
10. [Materials, normal maps and textures](#10-materials-normal-maps-and-textures)
11. [Animation and skinning](#11-animation-and-skinning)
12. [VU0 and the scratchpad](#12-vu0-and-the-scratchpad)
13. [Particles, decals, GUIs, subviews, fog and post-processing](#13-particles-decals-guis-subviews-fog-and-post-processing)
14. [The UI on PATH1](#14-the-ui-on-path1)
15. [Asset re-export pipeline](#15-asset-re-export-pipeline)
16. [Memory budgets](#16-memory-budgets)
17. [Performance budget and measurement](#17-performance-budget-and-measurement)
18. [Code design](#18-code-design)
19. [Milestones and acceptance gates](#19-milestones-and-acceptance-gates)
20. [Decision log, risks and open questions](#20-decision-log-risks-and-open-questions)
21. [References](#21-references)

---

## 1. Goals and constraints

**Goal.** Doom 3 BFG's single-player campaign fully playable on the PS2 at 30 fps NTSC
(two 59.94 Hz fields per frame) and 25 fps PAL (two 50 Hz fields), preserving the
original look as closely as the hardware allows: the dark, hard-lit rooms, the
projected light patterns, the specular sheen on metal, animated monsters with moving
shadows, and the in-game interfaces.

**Hard constraints.**

| Resource | Size | Where it bites |
| --- | --- | --- |
| EE RAM | 32 MiB, first MiB kernel | About 10 to 11 MB remain after the retail declarations and scripts boot ([memory-budget.md](../../.claude/rules/memory-budget.md)). Everything below must stream per area. |
| GS local memory | 4 MiB, 512 pages of 8 KiB | Frame buffers, Z, textures, lightmaps, CLUTs and a shadow scratch share it. |
| VU1 | 16 KiB micro memory (2,048 instructions), 16 KiB data (1,024 qwords) | Bounds vertices per batch and the number of resident programs. |
| VU0 | 4 KiB micro, 4 KiB data | Macro-mode matrix math; micro mode is optional. |
| Scratchpad | 16 KiB, DMA to and from main RAM and VU memory | The display-list build buffer. |
| GS fill | 16 pixels per cycle untextured, 8 textured, at 150 MHz | 40 M textured pixels per 30 fps frame, 174 full 512x448 layers at peak. Fill is not the first limit; passes and page breaks are. |
| GS blend | `(A - B) * C + D` with `C` a scalar alpha | No colour x colour. Lightmap, bump and light-texture modulation need alpha, vertex colour or channel passes. |
| GS depth test | NEVER, ALWAYS, GEQUAL, GREATER | No EQUAL; no stencil buffer; no face culling. |

**Principles.**

1. **The EE never touches a vertex at draw time.** Cooked geometry is DMA'd as the asset
   pipeline wrote it (Black §5, TimeSplitters chapter 06). The EE's per-frame work is per
   batch: visibility, light binding, state, tags. This is the only way the 30 fps target
   survives Doom's triangle counts (section 17).
2. **Every asset is re-exported** into GS/VU-ready formats by the host tools, with
   validation on load. The retail BIMAGE, BMD5MESH, BMD5ANIM and BPROC formats are inputs
   to the cook, never runtime formats.
3. **Keep id's world database and visibility; replace everything from "what to draw" down.**
   Section 5 fixes the seam at the end of portal flow (`FindViewLightsAndEntities`).
4. **Bake what is static, compute what moves.** World lighting and static shadows are baked
   offline into lightmaps with switchable layers; entities, projectiles, the flashlight and
   moving lights are lit per vertex on VU1.
5. **Measure before committing to a fidelity feature.** PCSX2 models neither GS cost nor the
   EE cache ([performance.md](../../.claude/rules/performance.md)). Every stretch rung below
   has a PCSX2 functional gate and a hardware timing gate.

**Out of scope.** Multiplayer, stereo rendering, the BFG resolution scaler and post-process
copy, demo recording, the editor and all developer visualisation in release builds. Classic
Doom is removed. Expansion campaigns inherit whatever works.

---

## 2. What the reference games teach

| Topic | Black (2006, Criterion) | TimeSplitters FP (2005, Free Radical) | quake2-ps2 (this author) | Adopted here |
| --- | --- | --- | --- | --- |
| Display list | Built in **scratchpad**, drained to a triple-buffered main-RAM chain by `fromSPR` DMA in 8 KiB bursts; CNT/CALL/REF tags with VIF codes in the tag (TTE) | Two frame lists in main RAM, 16 chains per frame, gfx thread sends them; GS `SIGNAL` ends each chain | One 768 KiB chain per frame in main RAM, FINISH fence, one frame of latency | Black's SPR builder (section 12), TimeSplitters' per-frame chain split for mid-frame fences, quake2's FINISH fence model |
| Geometry on disc | Ready-to-DMA VIF packets: `STCYCL` + `UNPACK` per attribute stream, `ITOP` count, `MSCNT`; 16-bit positions with a per-batch offset smuggled into `w` lanes; strips with the ADC bit | Chunk packets with the GIF tag unpacked to qword 0; EE adds matrices, lights and flags in qwords 1 to 7 | 32-byte `DrawVertex` REF'd in place from the loader memory | Black's packet shape; TimeSplitters' per-batch header block (section 6) |
| VU1 programs | 24 hand-written programs chosen by a shader table; `MSCAL 0` init then `MSCNT` per batch; constants at the top of VU memory | Six packages, each a complete micro-memory image swapped with one `FLUSH` + REF; eight common loader routines at fixed addresses; a dispatcher jumps to the renderer through `jr` | Two resident programs, 1,912 of 2,048 instructions used | A **resident kernel plus swapped programs** (section 7): TimeSplitters' fixed common routines, Black's per-shader programs, quake2's clipper verbatim |
| Lighting | Per-vertex directional plus ambient (shader 4); lightmap and second UV for the world (shader 1); no bump mapping | Per-vertex, three directional lights plus ambient per matrix slot; prelit colours; no bump mapping | Lightmap intensity through a CLUT with `Cd x As`, chroma per vertex; four VU1 point lights | quake2's lightmap split, extended with switchable layers; per-vertex dynamic lights on VU1 (section 8) |
| Specular and detail | Gloss mask in texture alpha, additive environment pass with `(Cs - 0) x Ad + Cd`; reflective floors with a per-vertex paraboloid env map | None | None | Black's gloss pass for metal and weapons; `HIGHLIGHT2` per-vertex specular (section 10) |
| Shadows | Projected decals (program `3852379027`, `ALPHA 0x44`, no Z write) | Blob shadows as separate chunks | Planar squash shadow through the same stream | Baked static shadows, planar and blob entity shadows, a counting stencil as a stretch (section 9) |
| Skinning | Bone palette in VU memory, bone index in position `w` | 45 matrix slots per skinned package, slot index times 8 in `w` | Two-keyframe lerp on VU1 | Per-batch joint palette of 24 joints, 40 vertices, three weight kernels (section 11) |
| Textures | 4- and 8-bit, pre-swizzled, uploaded as `PSMCT32`, PATH3 interleaved with `MSKPATH3` | Uploaded inside the VIF1 chain with `FLUSHA; DIRECT` | PATH3 after `WaitIdle`, synchronous | Uploads inside the chain first (safe), PATH3 interleave as an optimisation (section 10) |
| Frame buffer | `PSMCT32` 640x448 interlaced, `PSMZ24` | not recorded | `PSMCT16` + `Z16`, DIMX dither | `PSMCT32` + `Z16S` at 512x448 proposed, `PSMCT16` + `Z16` kept as a profile (section 4) |

Black is the closest visual target: dark interiors, glossy metal, environment passes and a
world shader that carries two UV sets. Its disc format, where the EE only `CALL`s packets,
is the model for our cook. TimeSplitters contributes the package manager and the dispatcher
discipline. quake2-ps2 contributes code: the clipper, the window macros, the lightmap
scheme, the VU0 matrix math, the VU build rule and the checker, all listed in
[REUSE.md](../REUSE.md) once imported.

---

## 3. Hardware facts that shape the design

Verified in the manuals (GS User's Manual, GS Supplement, EE User's Manual, VU User's
Manual); section references are to those documents.

1. **Blend** `Cv = (A - B) * C >> 7 + D`; `A`, `B`, `D` are `Cs`, `Cd` or 0; `C` is `As`,
   `Ad` or `FIX` (GS §3.8.1). Blending computes RGB only: the destination alpha receives
   the source alpha as written, or OR'd with `FBA`. There is no alpha arithmetic.
2. **Destination alpha** is 8 bits in `PSMCT32`, 1 bit in `PSMCT16` (0 or 0x80 as a
   factor), constant 0x80 in `PSMCT24`. `DATE`/`DATM` test only its MSB (GS §3.7.3).
3. **Depth test** `ZTST` is NEVER, ALWAYS, GEQUAL or GREATER; `ZTE` must be 1 (GS §3.7.4).
   Z-only writes use `ATE=1, ATST=NEVER, AFAIL=ZB_ONLY`. Colour-only writes that leave
   alpha and Z use `AFAIL=RGB_ONLY` (`PSMCT32` only) or `FBMSK`.
4. **Frame and Z format groups** (GS §2.5.4): `PSMCT32/24/16S` pair with `PSMZ32/24/16S`;
   `PSMCT16` pairs only with `PSMZ16`. Any colour target sharing a Z buffer must be in its
   group, and shares its `FBW`.
5. **Texture function** MODULATE multiplies texel by Gouraud colour with 0x80 as 1.0 and
   up to 2x headroom; HIGHLIGHT and HIGHLIGHT2 add the fragment alpha as white to RGB
   (GS §3.4.9). TEXA expands `PSMCT16`/`PSMCT24` alpha; with `AEM=1` a black `PSMCT24` texel, or a
   black `PSMCT16` texel whose A bit is 0, gets alpha 0 (a black texel with A set gets `TA1`).
6. **CLUTs**: a 1 KiB on-chip buffer holds 16 `PSMCT32` palettes of 16 entries (`CSA`), or
   one of 256; `CLD` 4/5 skip reloading an unchanged `CBP`; CSM1 stores each group of 32
   entries with its two middle 8-entry blocks swapped (GS §2.7.3, §3.4.7).
7. **Page geometry**: `PSMCT32` 64x32, `PSMCT16` 64x64, `PSMT8` 128x64, `PSMT4` 128x128
   texels per 8 KiB page; the texture cache is one page (Supplement §1.4). Mip levels and
   the textures of one batch should share pages.
8. **Fill and setup** (GS §1.3, Supplement §1.3): 16 or 8 pixels per cycle; textured
   Gouraud triangle setup 4 cycles; fog +1; AA1 6 to 9 cycles; read-modify-write at least
   7 cycles, with a heavy penalty for stacking tiny sprites on the same pixels.
9. **PATH arbitration** PATH1 > PATH2 > PATH3, one idle cycle per switch; a second
   XGKICK while PATH1 is busy stalls the VU; PATH3 IMAGE can run in intermittent mode and
   is masked by `MSKPATH3` (EE §7.1).
10. **VIF UNPACK** widens S/V2/V3/V4 x 32/16/8-bit and V4-5 data for free, with sign or
    zero extension, skipping writes (`STCYCL CL > WL`), fill writes from Row/Col with the
    mask, and offset or difference addition (EE §6.3.4 to §6.3.7). The VIF is our vertex
    decompressor.
11. **Double buffering**: `BASE`/`OFFSET`, `TOPS` flips on every `MSCAL`/`MSCNT`, the
    program reads its buffer with `XTOP` and its count with `XITOP` (EE §6.3.8).
12. **VU1 timing**: FMAC 4-cycle latency, DIV and SQRT 7, RSQRT 13, EFU 11 to 54, one
    branch delay slot; the EFU is not pipelined across operations. `MPG` uploads at most
    256 instructions per tag and waits for the running program (VU §3.4, EE §6.3).
13. **DMAC**: no bus snooping (flush the D-cache or write through the scratchpad); source
    chain tags CNT, NEXT, REF, REFS, CALL (stack depth 2), RET, END; `TTE` sends the
    tag's upper 64 bits to VIF as two VIFcodes; `fromSPR` and `toSPR` move 16 KiB
    scratchpad data in bursts, also to and from VU memory (EE §5).
14. **Interlace**: `SMODE2.FFMD` selects FIELD (every other line from a full-height
    buffer) or FRAME (a half-height buffer shown on both fields); `CSR.FIELD` gives the
    parity (GS §5.2.2).
15. **No NaN or Inf** on the EE FPU or the VUs; division by zero saturates; rounding
    truncates toward zero (VU §2).

Consequences used repeatedly below: no colour x colour (lightmaps and light textures go
through alpha or vertex colour); no counting in alpha (stencil counters live in RGB of a
scratch target); no EQUAL (second passes use GEQUAL with Z writes off after a complete
first pass with bit-identical VU transforms); no culling (the VU splits front and back
faces when a pass needs them).

---

## 4. Frame architecture

### 4.1 The pipeline

```
   EE (294.9 MHz)                 DMAC / VIF1                     VU1 (16K/16K)             GS
   ────────────────────────────   ────────────────────────────    ──────────────────────    ───────────────
   game tick (frame N+1)          ┌─ chain N (built last frame) ─┐
   portal flow, light binding     │ CNT  FLUSH MSCAL ...          │  xtop/xitop
   per batch: state, lights,      │ REF  cooked vertex packet ────┼─▶ UNPACK into TOPS half
     tags into the SCRATCHPAD     │ CALL program MPG (if swap)    │  transform, light,
   8 KiB full → fromSPR DMA ──▶   │ CNT  DIRECT  GS registers ────┼────────── PATH2 ───────▶ A+D state
     main-RAM chain ring N+1      │ CNT  DIRECT  IMAGE texture ───┼────────── PATH2 ───────▶ VRAM upload
                                  │ ...                           │  clip, build GIF packet
   end of frame: END tag,         │ CNT  FLUSH DIRECT FINISH ─────┼─▶ xgkick ── PATH1 ─────▶ triangles
     kick chain N+1,              └───────────────────────────────┘                          FINISH → CSR
     wait FINISH of N,
     flip DISPFB at the next even vsync (30/25 fps), present N
```

- One frame of latency: while the EE builds chain N+1 it never waits for the GS except at
  the frame boundary, where it waits for chain N's FINISH before flipping and before
  reusing N's chain ring and dynamic buffers. Implemented at the dma/gs level on 2026-11-03:
  `dma::EndFrame` kicks without waiting, `dma::Kick` waits for the previous chain before
  sending, and the frame loop waits for N, presents N (`gs::PresentMode::Pipelined`), then
  kicks N+1, so N+1 never draws into the buffer still on display. The 2D path keeps
  `PresentMode::Immediate`.
- Two chain rings (N, N+1) of 384 KiB each in main RAM, two frame buffers in VRAM.
- **Order inside a chain:** VU program and constant uploads, texture uploads for this frame
  (section 10.5), the depth and base pass of the world, the lit passes, entities,
  translucent surfaces back to front, particles and decals, fog and blend lights, the
  in-game GUIs, then the 2D menu, HUD and console. Textures referenced by a draw are
  uploaded earlier in the same chain or are resident from earlier frames.
- **Ordering rule.** VIF1 decodes a `DIRECT` block as soon as it arrives, without waiting
  for the running microprogram, so a `DIRECT` register write is not ordered against
  geometry the previous batch has yet to `XGKICK`. Per-batch pass state therefore travels
  in the VU output packet (the 7-qword A+D block every window carries, ordered by
  `XGKICK`); texture uploads are preceded by `FLUSHA`; and a bare `DIRECT` state block is
  used only for frame-start state or after an explicit `FLUSH`, whose lost overlap section
  17.1 charges.
- **Fences.** The whole chain ends with `FLUSH` and a `DIRECT` write of `FINISH`. Mid-frame
  fences are `SIGNAL` writes through a PATH1 pass-through program (TimeSplitters) when a
  later stage needs the GS to have finished (shadow scratch resolve, framebuffer-as-texture).
  Both are polled; interrupts stay masked as today (`gs.cpp`).

### 4.2 Pacing and display

- Present every second field: the flip waits for a vsync whose `CSR.FIELD` makes the
  frame display for two fields. A late frame slips to the next pair (30 fps falls to 20 fps on NTSC),
  never to a single field, so animation timing stays even. NTSC keeps `com_engineHz` 60
  and draws every second tick; PAL runs `com_engineHz` 50 so a 40 ms frame is also two
  ticks (id's game time is derived from the latched engine Hz; gate R1 verifies that no
  gameplay code assumes 60). The even-field flip (`CSR.FIELD`) and the PAL 50 Hz policy
  (`presentation.cpp`) exist since 2026-11-03.
- Display mode stays interlaced FIELD mode with a full-height 512x448 (NTSC) or 512x512
  (PAL) buffer, as the 2D path uses today. FRAME mode with a half-height buffer is the
  fallback if flicker on thin lit edges proves worse than the resolution loss; gate R1
  captures both.
- 512 wide is the default. 640 wide costs 25% more frame-buffer VRAM and fill and is a
  stretch after texture residency is measured (section 16).

### 4.3 Frame buffer format

Proposed default: **`PSMCT32` colour x2 + `PSMZ16S`** at 512x448.

| Format pair | Fixed VRAM NTSC | Left for textures, lightmaps, CLUTs, scratch | Gains | Costs |
| --- | --- | --- | --- | --- |
| `PSMCT32` + `Z16S` | 2,293,760 B (280 pages) | 1,900,544 B | 8-bit destination alpha: projected light textures and the flashlight in two passes, gloss masks, 8-bit shadow masks; no 5-bit banding in dark rooms | 917,504 B more VRAM than today; 2x frame-buffer bandwidth |
| `PSMCT16` + `Z16` (today) | 1,376,256 B (168 pages) | 2,818,048 B | More texture room; dither available | 1-bit alpha only: light textures and gloss become binary masks; every additive pass truncates to 5 bits |

The decision is taken at gate R1 with captures of a lit room in both formats. The device
already parameterises the split (`renderer::Config`, `gs::InitVram(begin, budget)`); both
profiles stay selectable, and the capture pixel model records which one a run used.

---

## 5. The seam into id's renderer

### 5.1 What the game depends on

Measured over `src/neo/d3xp`, `framework`, `ui`, `swf`, `sound` (2026-11-02):

- `idRenderWorld` is the real contract: entity and light def lifecycle (about 117 calls),
  area and portal queries (`PointInArea`, `BoundsInAreas`, `NumPortalsInArea`, `GetPortal`,
  `SetPortalState`, `AreasAreConnected`; the sound system uses them too), traces
  (`ModelTrace`, `FastWorldTrace`, `GuiTrace`), decals and overlays, `RenderScene` (three
  game call sites plus `RenderWindow`).
- `idRenderModel`: joints and bounds (`NumJoints`, `GetJointHandle`, `Bounds`,
  `InstantiateDynamicModel`) and runtime surface building (`InitEmpty`, `AddSurface`,
  `AllocSurfaceTriangles`) for brittle fracture and similar.
- `idMaterial`: surface flags, contents, type, stage images for PDA video and the shell,
  `GetEntityGui`, sort.
- `idRenderSystem`: the 2D draw family (already served by `ps2/ui/draw.cpp`),
  `CaptureRenderToImage` (five game sites: heat haze and full-screen effects), `CropRenderSize`,
  `GetFrameCount`, the frame loop calls from `framework`; `Common_load.cpp` calls
  `GenerateAllInteractions` at every level load.
- The player view renders the **portal sky** as a second scene (`PlayerView.cpp`: the
  `portalSkyEnt` view, then `CaptureRenderToImage("_currentRender")`, then the main
  view), so `RenderScene` is re-entered once per frame on sky maps.
- `renderEntity_t.callback` (seven game sites) must still fire when an entity is visible,
  shadow-relevant or traced; decals and overlays are created lazily inside add-models.

### 5.2 The cut

```
                 id's code kept                              │          ps2::renderer (new)
  ─────────────────────────────────────────────────────────  │  ─────────────────────────────────────────
  idRenderWorldLocal: defs, areas, portals, nodes, refs,     │
    InitFromMap (areas/portals/nodes from the cooked world), │
    traces (CPU mirror geometry), decals, overlays,          │
    FindViewLightsAndEntities → viewEntity_t/viewLight_t     │
    lists with portal scissor rects                          │
  idRenderModel classes for CPU queries (bounds, joints,     │
    InstantiateDynamicModel for traces and callbacks)        │
  idMaterial parsing and register evaluation                 │
  R_RenderView:                                              │
    view/projection setup                                    │
    FindViewLightsAndEntities ──────────────────────────────▶│  scene::RenderView(viewDef)
    [R_AddLights, R_AddModels, Interaction, shadow jobs,     │    bind lights to batches, cull batches,
     R_SortDrawSurfs, vertexCache, drawSurf_t: REMOVED]      │    instantiate dynamic models (callbacks,
    R_AddInGameGuis → ps2 GUI surfaces                        │    decals, overlays on the same triggers),
    R_GenerateSubViews → bounded ps2 subviews                │    sort, build the display list
  idRenderSystemLocal: 2D calls → ui::draw (today)           │  world::, model::, light::, shadow::,
  RenderScene → FrameBegin/End → scene::RenderView            │  material::, vu::, dma::, tex::, gs::
```

- **Kept**: `RenderWorld*.cpp` (defs, areas, portals, load of portals and nodes, traces,
  decals, overlays), the model classes as CPU-query objects, `Material.cpp`, `tr_trace.cpp`,
  `tr_frontend_main.cpp` down to `FindViewLightsAndEntities`. `tr_frontend_guisurf.cpp`
  and `tr_frontend_subview.cpp` take `drawSurf_t` arrays today, so they are rewritten over
  a PS2 surface descriptor (material, `viewEntity_t`, geometry) iterating the PS2 batch
  list; their GUI redraw, precise cull and mirror/remote view math is kept.
- **Replaced**: `tr_frontend_addlights.cpp`, `tr_frontend_addmodels.cpp`, `Interaction.cpp`,
  the three shadow-volume jobs, `tr_frontend_deform.cpp` (its nine deforms become VU or EE
  generators in section 13), `GuiModel.cpp` (its surfaces are produced for the PS2 2D
  collector), `drawSurf_t`, `vertCacheHandle_t`, `idVertexCache`, the 2 x 64 MiB frame
  arena. Their units leave the campaign source list; each engine caller gets a tagged seam.

Symbols the kept units still need from the removed ones, and where they go:

| Symbol | Used by | Disposition |
| --- | --- | --- |
| `R_EntityDefDynamicModel`, `R_ClearEntityDefDynamicModel`, `R_IssueEntityDefCallback` | `RenderWorld.cpp` (traces, def updates) | move into a kept unit (`RenderEntity.cpp`), unchanged |
| `tr.guiModel->EmitFullScreen/Clear` in `RenderScene` | `RenderWorld.cpp` | tagged seam: the PS2 2D collector flushes instead |
| `GenerateAllInteractions`, `FreeInteractions`, `idInteraction` | `Common_load.cpp`, `RenderWorld.cpp` | tagged no-ops; interactions are cooked offline |
| `vertexCache.Alloc*`, `R_CreateStaticBuffersForTri` | `tr_trisurf.cpp`, `ModelDecal.cpp`, `ModelOverlay.cpp` | PS2 bodies that build batches or keep CPU geometry; no GPU buffers |
| `R_FrameAlloc`, `R_ClearedFrameAlloc` | `RenderWorld.cpp`, `RenderWorld_portals.cpp` | kept; `ps2` calls `R_InitFrameData` with a tagged, small size (see below) and `R_ToggleSmpFrame` once per renderer frame |
| `drawSurf_t` producers in decals/overlays | `ModelDecal.cpp`, `ModelOverlay.cpp` | keep the CPU generators, replace the surface emission |
- **`RenderScene` on the PS2**: `idRenderWorldLocal::RenderScene` keeps its entry (it
  flushes pending 2D, builds the `viewDef_t`, sets viewport and scissor) but calls
  `ps2::renderer::scene::RenderView` instead of `R_AddDrawViewCmd`. `CropRenderSize` and
  `PerformResolutionScaling` become no-ops that report full size.

Why this cut and not a deeper cut at `RenderScene`: the portal flow is cheap, already compiled
and tested, and it produces exactly the per-view entity and light lists with scissor
rectangles a PS2 renderer wants. A deeper cut would reimplement area linking that the sound
system and `SetPortalState` depend on, for a saving of a few hundred small frame
allocations. `R_InitFrameData` is called by `ps2` (today nothing calls it, so the first
`RenderScene` would use an uninitialised arena) with a frame size of
`sizeof(viewDef_t) + N x sizeof(viewEntity_t) + M x sizeof(viewLight_t)` plus the
`connectedAreas` array at the p99 counts of section 8.1, and with 16-byte rounding instead
of the desktop's 128-byte cache-line rounding.

### 5.3 What `scene::RenderView` does

```cpp
void scene::RenderView(const viewDef_t & view)
{
    // 1. Static world: for every visible area (view.renderWorld areas with a scissor),
    //    walk its cooked batches, frustum-test batch bounds, bind the dynamic lights that
    //    touch the batch (section 8.4), emit base + lightmap (+ layer) passes.
    // 2. Entities: for every viewEntity_t, instantiate the dynamic model if needed
    //    (callbacks run here as id does), pick the entity's light set, emit the model's
    //    batches through the model program (rigid or skinned).
    // 3. Shadows: planar/blob (default) or the counter stencil (stretch) per light.
    // 4. Translucent, particles, decals, fog and blend lights, in id's sort order.
    // 5. In-game GUI surfaces (R_AddInGameGuis output) and subviews, bounded.
    // Every step writes DMA tags and VIF/GIF payload into dma::DisplayList (scratchpad).
}
```

`FindViewLightsAndEntities` only creates the `viewEntity_t`/`viewLight_t` records with their
portal scissors. `scene::RenderView` re-creates what `R_AddSingleLight` and
`R_AddSingleModel` did on top of them: light shader register evaluation and the dark-light
skip, the light scissor and depth range, `suppressLightInViewID`/`allowLightInViewID`,
the per-entity model and MVP matrices, `weaponDepthHack`/`modelDepthHack`,
`suppressSurfaceInViewID`/`allowSurfaceInViewID`, `noSelfShadow`, `noShadow`, `xrayIndex`,
`timeGroup`, skin and `customShader` remapping, and the dynamic-model cache
(`dynamicModelFrameCount == tr.frameCount`). Each is tested by the existing fixtures
before any retail map draws.

### 5.4 Model objects

Each cooked model has two faces:

- an `idRenderModel` for id's code (bounds, joints, surfaces with a CPU mirror for
  `ModelTrace`, decals, overlays and the collision fallback). The kept consumers read
  `srfTriangles_t` with `idDrawVert` (positions, UVs, joint indices and weights), so
  characters and props keep that mirror (32 B per vertex, 2 B per index); world areas keep
  a compact `s16` position and `u16` index mirror and the world decal code is adapted to it;
- a `ps2::renderer::model::Mesh`: the DMA-ready batch list (section 6), material pass
  table references and, for skinned models, the per-batch joint palettes.

The world's `_areaN` models are the same class; `FastWorldTrace` needs only the cooked
area nodes, while world decals and `ModelTrace` on world models use the compact mirror.

---

## 6. Scene data on the PS2

### 6.1 Batches

A batch is the unit of VU1 work: one material, one program, at most `N` unique vertices
whose input half fits the VU1 double buffer (88, 68 or 40 by layout, section 7.2). The
cooked file holds, per batch, a raw VIF packet (VIFcodes and UNPACK data, qword padded
with NOP) exactly as the VIF consumes it, REF'd from the display list:

```
batch packet (REF'd from the display list, never copied by the EE)
  STCYCL cl=1 wl=1
  UNPACK V4-32 num=2  addr=0 +TOPS        ; qw0: pos offset xyz | index count ; qw1: pos scale, uv scale, uv offset st
  UNPACK V4-8  num=I/4 addr=2 +TOPS       ; strip index stream, four u8 indices per qword (bit 7: ADC)
  STCYCL cl=S wl=1                        ; vertex stride S qwords (3 for lightmapped world)
  UNPACK V4-16 num=N  addr=2+I/4   +TOPS  ; position xyz (s16) + w: ADC flag 0x8000 (plain strips only)
  UNPACK V4-16 num=N  addr=3+I/4   +TOPS  ; uv0 (4.12) and lightmap uv (4.12)
  UNPACK V4-8  num=N  addr=4+I/4   +TOPS  ; rgba chroma (0x80 = 1.0)
  [UNPACK V3-8 num=N addr=5+I/4 +TOPS]    ; normal (lit variants)
  [UNPACK V4-8 ... joint indices, weights] ; skinned variants (no index stream, plain strips)
  ITOP N ; MSCNT                           ; count for xitop, run the resident program
```

Vertex layouts (bytes per vertex in the packet, qwords in VU memory):

| Layout | Streams | Bytes | VU qw | Used by |
| --- | --- | --- | --- | --- |
| **WA** world lightmapped | pos s16x4, uv0+uvlm 4.12, rgba | 20 | 3 | static world |
| **WB** world dynamic-lit | WA + normal s8x3 | 24 | 4 | world batches a dynamic light touches |
| **WC** prelit | pos, uv, rgba | 16 | 3 | static props with baked vertex colour, GUIs, decals |
| **SK-R** skinned rigid | pos, uv, normal, joint index | 20 | 4 | 57% of skinned vertices (one weight) |
| **SK-A** skinned blended | pos, uv, normal, palette offsets x4, weights x4 | 28 | 5 | two to four weights |
| **SK-B** skinned with tangent | SK-A + tangent s8x4 | 32 | 6 | only if a CLUT-relight stretch needs it |

Positions are `s16` with a per-batch offset and scale (a 4,096-unit batch box gives 1/16
unit precision; the worst retail surface, 14,272 units, gives 0.22 unit). 98.9% of retail
world vertices have UV magnitudes within ±8, so 4.12 fixed point covers them; the rest get
a per-batch UV offset in the header, as Black hides its position offset.

**Topology.** The GS has no index buffer. Four encodings were simulated on the mars_city1
world (50,332 triangles, 1,791 surfaces):

| Encoding | Vertices emitted per triangle | Whole-world bytes (WA) |
| --- | --- | --- |
| Triangle list | 3.00 | 3.02 MB |
| ADC strips (greedy stripper, 2.98 triangles per strip) | 1.67 | 1.76 MB |
| Unique vertices + 8-bit local index list (80 per batch) | 1.28 | 1.55 MB |
| **Unique vertices + 8-bit strip index stream** | 1.28 in, about 1.67 out | **1.52 MB** (64 B headers) |

The hybrid is the default: VU1 transforms each unique vertex once, then emits the GS packet
by walking a byte index stream of strips (`lq` by index), so output traffic is the strip
cost and input traffic the unique cost (about 1.3 emitted vertices per unique vertex).
Each index byte names the vertex in its low 7 bits; bit 7 is the GS ADC flag, set on the
first two entries of every strip, which close no triangle. Strip triangle `k` is
`(k, k+1, k+2)` in id's winding for even `k` and `(k+1, k, k+2)` for odd `k`, so a later
culling path can recover each triangle's facing.
A better stripper (the greedy one averages three triangles per strip) improves it further.
The simulations ran at caps of 48 to 96 vertices (2,626 to 2,094 batches); the 88 cap of
section 7.2 lands near 2,150 batches for the whole level. Gate R1 measures the VU1 cycle cost of the index
walk against plain ADC strips before the cook commits.

### 6.2 Batch header and draw record

```cpp
// Cooked, 64 bytes; the EE reads only what it culls and binds with.
struct BatchHeader
{
    float boundsMin[3];
    u32   flags;              // layout, two-sided, has lightmap, has index stream, ...
    float boundsMax[3];
    u32   vertexCount;
    float posOffset[3];
    float posScale;           // the same values the VU header qwords carry
    u32   packetOffset;       // the REF target inside the level's DMA arena
    u32   packetQwords;
    u16   material;           // pass table index
    u16   lightmapPage;       // lightmap atlas page
    u16   indexCount;         // strip index bytes in the packet
    u16   reserved;
};

static_assert(sizeof(BatchHeader) == 64);
```

The level loader places every batch packet in a 64-byte-aligned DMA arena that is written
once (then `SyncDCache`d) and never touched by the EE again. The display list only `REF`s it.

Implemented in the host cook on 2026-11-04 (`src/tools/assets/world.{h,cpp}`, the `world`
command of `asset_reexport`): D3WD v1 holds this header per batch, the packets in one
64-byte-aligned section, the areas, surfaces, portals and nodes, and the compact CPU mirror
(5.4); `world.h` documents every field. Positions take a per-batch power-of-two step with
the offset snapped to it, so map-grid coordinates stay exact; UVs are 4.12 around a
whole-number offset and coarsen only when a batch's range needs it. The packet ends with
`ITOP N; MSCNT`, so it is independent of where the program sits in micro memory; the first
batch after a `vu::Bind` needs the program's `MSCAL` entry once. The graybox map (two
rooms, 1,516 triangles) cooks into 26 batches: 3.8 triangles per strip, 0.94 vertices
transformed and 1.53 emitted per triangle, 33 KB of packets in a 53 KB file.

### 6.3 Material pass tables

The cook compiles each material into a fixed list of GS passes (section 10.1): for each
pass the texture, CLUT, `TEX1`, `CLAMP`, `ALPHA`, `TEST`, `FBMSK`, `PRIM` flags and the VU
program variant. At run time the pass's A+D register block (6 qwords: `TEX0`, `TEX1`, `CLAMP`, `ALPHA`,
`TEST`, `FRAME` mask) travels in the batch header qwords and is copied by the VU program
into every output window ahead of the drawing tag, so it is ordered with the triangles by
`XGKICK` (section 4.1); nothing per batch goes through `DIRECT`. Materials with time-driven registers
(1,601 of 6,878 non-light materials use `time`, tables, parms or `sound`) keep
`EvaluateRegisters` on the EE and patch the affected words (UV matrix, colour, alpha test
reference, stage enable) per frame.

---

## 7. VU1 microprograms and their manager

### 7.1 Program suite

| Program | Input layout | Per-vertex work | Output | Est. instructions |
| --- | --- | --- | --- | --- |
| `kernel` (resident) | all | clipper (near + guard band, Sutherland-Hodgman, from quake2), output windows, GIF tag copy, ADC judgement, fan emit | | 750 |
| `world_lm` | WA | transform, two UV sets scaled by Q, chroma colour, fog F; emits base pass and lightmap pass packets (two contexts) | ST/RGBAQ/XYZF2 x2 | 350 |
| `world_lit` | WB | `world_lm` + up to 4 point lights per vertex (section 8.4), optional projected STQ for one textured light | + light pass | 450 |
| `model_rigid` | SK-R / WC | one matrix, up to 4 lights, per-vertex Blinn specular into alpha (HIGHLIGHT2), optional env ST | base (+ env pass) | 400 |
| `model_skin2` / `model_skin4` | SK-A | 2- or 4-joint blend of position and normal from the batch palette, then as `model_rigid` | | 450 / 520 |
| `shadow_volume` | positions + sil index pairs | extrude along the light, split front and back by screen winding, emit two ADC-patterned kicks | flat XYZ2 | 300 |
| `shadow_planar` | SK-R / WA positions | squash matrix, A-bit mask emit | flat | 150 |
| `particles` | centre, colour, size, rotation | billboard to a SPRITE or two triangles, soft fade by depth | 5 qw per particle | 120 (quake2's 90) |
| `ui_2d` | 16-byte packed 2D vertex | 640x480 to 1/16 px, UV page mapping, per-run tags | ST/RGBAQ/XYZ2 | 80 (Black's 47) |
| `passthrough` | GIF packet | `xgkick` of the input half, then `xgkick` of the null GIF tag at qword 8 so the program ends only once its packet has drained (an XGKICK stalls while the previous PATH1 transfer runs); for SIGNAL/LABEL fences and A+D blocks that must be ordered with PATH1 | | 6 (built) |

The kernel, `world_lm` and `passthrough` (about 1,110 instructions) stay resident during
gameplay. The remaining 940 slots are one swap region that holds one or two programs at a
time (`world_lit` + `model_rigid` = 850 fit together, as do `model_skin4` +
`shadow_volume` = 820); `vu::Init` reports the region size and a gate checks that every
program fits it alone. Draw lists are sorted by program so the region swaps a few times per
frame (world, then rigid models, then skinned, then effects), never per batch. Estimates come from quake2-ps2's measured 1,822-instruction program
(656 of them the clipper) and Black's program sizes (0x148 to 0x2aa0 bytes).

### 7.2 VU1 data memory

```
qword     world_lm (S = 3, N = 88, I = 1.3 N indices)       model_skin4 (K = 24 joints, N = 40, strips)
0..7      frame constants: MVP rows, GS scale/offset,       same
          clip scale, fog params
8..23     per-pass GIF tag and A+D templates (2 passes x 8); same
          qword 8 is a null GIF tag (NLOOP 0, EOP 1)
24..39    per-draw constants: lightmap and layer scales,    same
          material UV matrix, specular parameters,
          the projected light's four planes (4 qw)
40..345   input half A (BASE = 40, 306 qw): 2 header +       input half A: 96 palette + 2 header +
          29 index qw (1.3 x 88 / 4) + 88 x 3 = 295          40 x 5 = 298
346..651  input half B (OFFSET = 306): same                  same
652..802  output window A: 7 tags + 3 x 48 emitted = 151    same
803..953  output window B: 151                               same
954..1009 clipper scratch and spill qwords (quake2's 54)    same
1010..1023 light block: 4 lights, one per lane              same
```

The output windows hold a fixed 48 emitted vertices each, not `N`: the program kicks a
window when it fills and continues in the other (quake2-ps2's `OpenOutputWindow`), so the
strip walk (about 1.3 emitted vertices per unique vertex) and the second context pass only
add kicks. The input halves are what bound `N`: 1,024 less 40 constants, 54 scratch, 14
light block and 302 of windows leaves 614, or 307 per half. With the index stream
(`1.3 N / 4` qwords) that gives `N` 88 for 3-qword vertices (WA, WC), 68 for 4-qword
vertices (WB, SK-R) and, for the skinned layout with its 96-qword palette and plain strips
instead of an index stream, 40. Every layout is pinned by `static_assert`s as `vu1.h` does
in quake2-ps2, and sized from the program's **last write**, not its last input: VIF1 unpacks
the next batch into the other half while the program runs, so an overrun corrupts the next
batch's header and hangs the console while PCSX2 shows nothing (quake2-ps2 rule).

### 7.3 Program manager

`ps2::renderer::vu` is a namespace-scoped system (section 18) that owns micro memory:

```cpp
namespace ps2::renderer::vu
{
    enum class Program : u8
    {
        Kernel,
        WorldLightmapped,
        WorldLit,
        ModelRigid,
        ModelSkin2,
        ModelSkin4,
        ShadowVolume,
        ShadowPlanar,
        Particles,
        Ui2D,
        Passthrough,
        Count
    };

    PS2_COLD_FUNC void Init();          // measure every program, lay out the fixed kernel

    // Emits an MPG upload chain into the display list only when 'program' is not resident,
    // evicting the least recently used swappable slot. Returns the MSCAL address.
    MicroAddr Bind(dma::DisplayList & list, Program program);

    // Draw lists are sorted by program before emission so swaps happen a few times per
    // frame, never per batch.
    u32 SwapsThisFrame();
}
```

Each program's assembled code is one pre-built MPG chain in main memory (REF tags of up to
256 instructions each, as Black's `.vutext` chains), so a swap costs one `CALL` into that
chain (MPG itself waits for the running program to end, so no FLUSH precedes it). The
kernel's entry addresses are fixed; programs call it with `bal`, and the checker's
`branches` check confirms every target. Implemented 2026-11-03 (`vu.{h,cpp}`): residents
upload on their first `Bind` after `Init` or `Invalidate`, swappable programs bump-allocate
the swap region and evict it whole when they do not fit, and `EmitFrameConstants` uploads
the shared data memory constants once per frame.

Build: the quake2-ps2 rule (`vclpp -I vu1progs -Wundef -Werror -j`, then `openvcl`, then
`openvcl -c` for the source names, then `dvp-as`, then `check_vu_code.py`) is adopted verbatim into the Makefile (done 2026-11-03), with vclpp and
vu-checker imported under `src/external/` and vclpp built into `build/tools/vclpp`.
`PS2_DECLARE_VU_MICROPROGRAM` and the instruction-count helper come from `vu1.h` into
`ps2/renderer/vu.h`; the code lands in `.vutext`, and `vu1progs/vu_memory.h` holds the
data memory map above, included by both the programs and `vu.h`.

### 7.4 Program skeleton

Every program follows one shape (Black §7.2, TimeSplitters' loaders), written in VCL with
the pseudo-code comment blocks the Quake rule requires:

```vcl
    ; Lightmapped world batch: transform the unique vertices once, then walk the strip
    ; index stream and emit both GS passes (base texture, lightmap) through the two
    ; output windows.
    ;
    ;   qword * batch = xtop(); int count = xitop();
    ;   vec4 offset = batch[0], scale = batch[1];
    ;   int i = 0;
    ;   do
    ;   {
    ;       vec4 p    = itof0(batch[2 + indexQwords + 3 * i]) * scale + offset;
    ;       vec4 clip = mvp * p;                          // clip flags from clipw
    ;       vec4 uv   = itof12(batch[... + 1]) * (1 / clip.w);
    ;       emitBothContexts(clip, uv, rgba(batch[... + 2]), fog(clip.z));
    ;   } while (++i < count);
    ;   walkStripIndices();                               // ADC on restarts, kicks on full windows
#vuprog world_lm
    ; init block, run once by MSCAL after the upload: window pointers, constants
    ...
    nop[E]
    nop
lMain:                                 ; MSCNT lands here for every batch
    xtop   iBase                       ; this batch's input half
    xitop  iCount                      ; vertex count from ITOP
    LoadFrameConstants{ }
    lq     fPosOffset, 0(iBase)        ; batch header
    lq     fPosScale,  1(iBase)
    OpenOutputWindow{ }
    iaddiu iVert, iBase, 2
lVertex:                               ; software pipelined 2 vertices per iteration
    lq.xyz fPos, 0(iVert)              ; s16 from V4-16, widened by the VIF
    itof0.xyz fPos, fPos
    mul.xyz   fPos, fPos, fPosScale
    add.xyz   fPos, fPos, fPosOffset
    Transform{ fClip, fPos }           ; mvp0*x + mvp1*y + mvp2*z + mvp3
    clipw.xyz fClip, fClip[w]          ; guard-band judgement (kernel macro)
    ...                                ; uv0, uvlm * Q, chroma, fog
    EmitVertexTwoContexts{ }           ; base pass qwords and lightmap pass qwords
    iaddiu iVert, iVert, 3
    ibne   iVert, iEnd, lVertex
    ; strip index walk and ADC patterning follow (section 6.1)
    CloseOutputWindowAndKick{ }
    nop[E]
    nop
    b lMain
    nop                                ; the branch delay slot executes
#endvuprog
```

The two-context trick (Black shaders 4/7, TimeSplitters' second pass) lets one program
emit the base pass under `CTXT=0` and the lightmap or environment pass under `CTXT=1` with
different `TEX0`/`ALPHA`/`TEST`, so the vertices are transformed once for two passes.

### 7.5 Clipping policy

- The projection scales NDC so the visible half-extent is 256/2048 = 0.125 horizontally
  and 224/2048 (NTSC) or 256/2048 (PAL) vertically, and the GS 12.4 window (4,096 px) is
  the guard band; the kernel cuts only triangles beyond quake2-ps2's
  `kGuardBandNdcLimit` of 0.8 in clip space, so nearly every on-screen triangle needs no
  clipping.
- The kernel clips against the near plane and the guard band in clip space (about 2% of
  triangles), carrying every interpolated attribute (two UVs, colour, light sum) as three
  qwords per corner. The far plane is judged and rejected whole, never cut (never straddled
  in 606,931 clipped triangles).
- Shadow volumes extruded to the light's bounds (never to infinity, section 9) stay finite
  and clip with the same kernel.

---

## 8. Lighting

### 8.1 Doom's model and what survives

Per light and surface, id's interaction shader computes per pixel
`(diffuse * diffMod + spec * pow(N·H, 10) * specMod) * (N·L * lightProjection(s/q, t/q) * falloff(r)) * vertexColor`,
additively over lights, with `r_lightScale` (the player's brightness slider, 2.0 to 4.0,
default 3; the PS2 applies it as a per-frame global or drops the option) and specular
colour x2. `N` is the tangent-
space normal map; the light projection is a 2D texture; the falloff is a 1D texture; the
specular map is an RGB colour with a constant exponent.

Retail lighting in two maps (measured):

| | mars_city1 | alphalabs1 |
| --- | --- | --- |
| Lights | 486 (480 point, 6 projected) | 587 |
| Static, always on (category A) | 409 | 389 |
| Scalar-animated by table or `sin` (B) | 13 | 99 |
| Switchable, scripted, targeted or breakable (C) | 29 | 35 |
| Sound-driven (D) | 2 | 39 |
| Moving, projected, rotating texture (E) | 30 | 25 |
| Fog (F) | 3 | 0 |
| Shadow casters | 330 | 371 |
| Lights per lightable surface, all lights: mean / p99 / max | 5.0 / 20 / 26 | 6.1 / 25 / 52 |
| Runtime-dynamic lights (E+F) per surface: mean / p99 / max | 0.22 / 2 / 4 | 0.69 / 5 / 6 |

Five projection images (`biground1`, `squarelight1`, `squareishlight`, `spot01`,
`blamplight_cv`) cover 453 of mars_city1's 486 lights; the flashlight (`lights/flashlight5`,
about 512x512 judged from its file size, a 53° spot) is the only projected light the player carries, and it casts no
shadows in the retail defaults (`g_weaponShadows 0`).

### 8.2 World: baked lightmaps with layers

```
offline (cook)                                        runtime (per world batch)
  classify lights A..F per map                          pass 1  diffuse x chroma (vertex RGB), Z write
  A  → base intensity atlas + per-vertex chroma         pass 2  Light8 atlas through ramp CLUT,
  B,C,D → one Light8 layer per switch group / waveform           ALPHA 0x89 = (Cd - 0) x As, ZTST GEQUAL, ZMSK 1
  E,F → not baked                                       per active layer k (rare):
  occlusion for the 330 casters by ray casting                  pass a  layer_k through CLUT, vertex alpha = s_k(t),
    against the world and .bcm collision                                FBMSK = RGB masked (writes alpha only)
  charts: planar unions of surface triangles,                   pass b  diffuse x chroma_k, ALPHA 0x58 = (Cs - 0) x Ad + Cd
    16 units per luxel, 1-texel border, skyline packed
```

- The split follows quake2-ps2: luxel **intensity** in an 8-bit atlas sampled through a
  CLUT with `Cd x As`, luxel **chroma** per vertex. GS blending cannot multiply an RGB
  lightmap into the diffuse; this is the expressible form. Strongly coloured lights (24% of
  mars_city1's lights have saturation above 0.5) need the cook to subdivide large faces to
  about 64 units for chroma (about +7 k vertices on mars_city1).
- Layers use the destination alpha (`PSMCT32`): the layer's intensity, scaled per frame by
  the light shader's evaluated register through the vertex alpha (`Av = At x Af`), is
  written to alpha, then the diffuse is added weighted by it. Layers are rare per surface
  (mean 0.38 in mars_city1, 1.39 in alphalabs1). In the `PSMCT16` profile layers degrade
  to their vertex-colour average.
- Switch groups: the runtime evaluates the light shader's registers each frame (RGB from
  `shaderParms`, which `SetLightLevel`, fades and `target_entity_*` write; `MODE` and
  `TIMEOFFSET`, which `BecomeBroken` and `On` reset; tables and `sound`) and derives the
  layer scale from them, 0 skipping the layer. Lights whose scripts call `setShader`,
  `setLightParm(s)` or `setRadius` are cooked as runtime lights (class E) instead.
- Exposure: `r_lightScale 3` exceeds the 2x headroom of `Cd x As`. The cook brightens the
  diffuse CLUT by a per-level exposure and scales the intensity atlas by its inverse, as
  quake2-ps2's lit palette does.
- Size: mars_city1's lightable area is 27.9 M unit². Charts of at least 1,024 unit² hold
  94% of it and need 301 k texels at 16 units per luxel (about 300 KiB `PSMT8`, 150 KiB
  `PSMT4`), so about five 256x256 atlases per map, with only the visible areas' atlases
  resident in VRAM. The retail prelight shadow volumes (4.35 MB in memory for mars_city1)
  disappear.

### 8.3 Lightmap baker

A new host stage in `asset_reexport` (section 15). Per luxel: for each light in classes A
to D that touches the chart, `N_geo·L x P(s/q, t/q) x F(r)` with id's own projection and
falloff images decoded from the retail BIMAGEs, occluded by rays against the area models
and the `.bcm` collision model when the light casts shadows. Light materials are evaluated
with id's `idMaterial` register code compiled into the tool so tables, `colored` and
`zeroClamp` behave exactly as in the engine. Determinism follows the existing tool rules
(`-ffp-contract=off`, byte-identical output across hosts).

### 8.4 Dynamic lights on VU1

The runtime light set per batch is small: categories E and F plus weapon, projectile and
`fx` lights, a p99 of 2 to 5 per surface. The budget is **4 lights per batch**, chosen by
the EE from the lights whose box touches the batch bounds, ranked by intensity x falloff
at the batch centre.

Per vertex and light on VU1, with the light block laid out one light per SIMD lane as
quake2-ps2 does:

```
d²      = Σ (L - P)²                          ; 3 lanes x 4 lights, 3 FMAC
falloff = max(1 - d²/r², 0)                    ; or the cooked polynomial for the light's image class
ndotl   = max(dot(N, L - P) x rsqrt(d²), 0)    ; one RSQRT per light, interleaved across vertices
colour += lightColour x falloff x ndotl        ; 4 FMAC
```

Smooth light images (`biground1`, `squarelight1`, `squareishlight`: 403 of mars_city1's
lights) are folded into the falloff polynomial, so the pass is one additive draw:
`Cs = diffuse x vertexLight` (MODULATE), `ALPHA 0x48 = (Cs - 0) x As + Cd`, ZTST GEQUAL,
ZMSK 1. Patterned or projected lights (`spot01`, grates, the flashlight) use the
destination alpha in two passes: pass a draws the light image with projective STQ from the
light's four planes (`lightProject[0..3]` is a 4x4 matrix: four FMAC per vertex give S, T,
Q and the falloff coordinate) into alpha only; pass b adds `diffuse x vertexColour x Ad`
(`ALPHA 0x58`). The flashlight is this path with the cone's `flashlight5` image reduced to
256x256 `PSMT8`; in the `PSMCT16` profile it becomes an analytic soft cone.

Lit and additive passes bind the diffuse through a CLUT variant whose alpha is a constant
0x80 (MODULATE gives `Av = At x Af`), so a gloss or coverage alpha in the base CLUT never
scales them; only the base, alpha-test and environment passes use the gloss CLUT.

**Destination alpha ownership.** One alpha channel serves several passes, so each batch's
pass sequence states what alpha holds:

| Pass | Alpha before | Alpha after |
| --- | --- | --- |
| base diffuse | anything | 0 (or the gloss mask for environment-pass materials) |
| lightmap `Cd x As` | gloss or 0 | unchanged (`FBMSK` alpha) |
| layer / projected light pass a | 0 | the layer or light image intensity |
| layer / projected light pass b (`Cs x Ad + Cd`) | intensity | unchanged; a following pass a overwrites it |
| environment pass (`Cs x Ad + Cd`) | gloss mask | unchanged |
| shadow resolve (L2) | anything | 0x80 where shadowed, 0 elsewhere; light passes test it with `DATE` and mask their alpha writes |

A shadow mask and a projected light image cannot share alpha in one sequence, so in L2 a
shadow-masked light is either a smooth (per-vertex) light or draws without the mask; the
environment pass runs before any shadow resolve.

The estimated VU1 cost is 12 to 16 cycles per vertex per light. 20,000 lit vertices at four
lights are about 1.2 M cycles, 12% of a 30 fps frame at 294.9 MHz; this is an estimate to
be measured at gate R5.

### 8.5 Entities

- **Static props and brush entities** (673 `func_static` in the mars_city1 map, 564 brush
  surfaces in its proc): cooked into `.d3md` with baked per-vertex colour from the same
  baker (with occlusion), drawn with layout WC; dynamic lights as above. Movers (doors,
  `func_mover`) take dynamic light sets like characters.
- **Characters and movers**: ambient from the baked light at the entity's position (the
  lightmap sample under the entity, as Quake does, or an optional cooked probe grid), plus
  the four strongest lights (static and dynamic) per entity evaluated per vertex, plus
  per-vertex Blinn specular for armour and metal (section 10.2). Static lights lighting a
  monster are therefore dynamic per vertex, which is what makes monsters read correctly
  against lightmapped rooms.
- **First-person weapons**: per-vertex lights, the gloss environment pass, the flashlight
  and muzzle flash through the projected path; `weaponDepthHack` scales Z in the VU.

### 8.6 Fog and blend lights

Fog lights (3 in mars_city1) draw as id does: the surfaces inside the fog volume
re-emitted with the fog texture coordinate from the fog plane and `ALPHA 0x44`. Blend lights
(6 materials in retail) are a projected texture pass with the light's blend. Both reuse the
`world_lit` projective STQ path. GS per-vertex fog (`XYZF2.F`, `FOGCOL`) handles distance
darkening in large rooms for one extra setup cycle per fogged triangle and is enabled per
level by the cook.

---

## 9. Shadows

### 9.1 What BFG does

BFG draws stencil volumes per light: precomputed `_prelight_*` volumes for static lights
(267 in mars_city1, 4.35 MB in memory), static per-interaction volumes, and per-frame
dynamic volumes for animated models from silhouette edges (an imp has 946 vertices, 1,346
triangles and 2,031 silhouette edge records; its worst-case volume is 6,754 triangles). The
default backend algorithm is "preload plus Z-pass", which is Z-fail written only with
depth-pass and always-pass stencil operations, and plain Z-pass when the CPU proves the
viewer is outside the volume. Interactions then draw with stencil EQUAL 128.

### 9.2 What the GS can do

- No stencil, no face culling, no depth bounds, no polygon offset, no EQUAL.
- The destination alpha bit can be **set** and tested (`DATE`) but never incremented, so a
  1-bit stencil is wrong for overlapping or concave volumes (a character's arm in front of
  its body) and order-dependent.
- A **counting stencil** works in the red channel of a scratch colour target that shares the
  scene Z buffer: increment `ALPHA 0x80_00000068` (`Cs x FIX + Cd`), decrement
  `0x80_000000A1` (`(Cd - Cs) x FIX`), saturating. In the `PSMCT16S` scratch the channel
  holds 5 bits, so the draws use `Cs.R = 0x08` (one fragment moves the counter by one), the
  count saturates at 31, dither stays off, and the clear writes A = 0. A `PSMCT24` scratch
  (688,128 B at 512x448) counts in 8 bits if 31 overlaps ever prove too few. BFG's preload
  scheme maps exactly onto the two available depth tests:

| BFG draw | Stencil op | GS pass |
| --- | --- | --- |
| preload, front faces | DECR always | DEC at `ZTST ALWAYS` |
| preload, back faces | INCR always | INC at `ZTST ALWAYS` |
| Z-pass, front faces | INCR on pass | INC at `ZTST GEQUAL` |
| Z-pass, back faces | DECR on pass | DEC at `ZTST GEQUAL` |

  All INC draws run before all DEC draws so the saturating counter never clips at zero. The
  VU splits front and back faces by screen winding (two kicks with complementary ADC
  patterns). The counter is converted to the scene's alpha by one sprite that reads the
  scratch as a `PSMCT24`/`PSMCT16S` texture with `TEXA.AEM=1`, `TA0 = 0x80`, `TA1 = 0`
  (black with A clear gives alpha 0, any other colour gives 0x80), `TFX DECAL`, writing alpha
  only; the light passes then use `DATE=1, DATM=0`
  and mask their own alpha writes. With the `PSMCT32 + Z16S` scene the scratch is `PSMCT16S`
  (same format group, 458,752 B at 512x448).

### 9.3 The ladder

| Rung | Content | GS cost per frame (estimate) | VRAM | Fidelity |
| --- | --- | --- | --- | --- |
| **L0 default** | Static shadows baked into the lightmaps (section 8.2); planar and blob shadows for monsters and props from the strongest nearby light, drawn through the A-bit mask so overlaps darken once | 0.05 to 0.3 ms | none | no shadows from dynamic lights; matches BFG's weapon-light default |
| L1 | Projected shadow texture for the flashlight when "Flashlight Shadows" is on: caster silhouettes into a 128x128 target, projected onto receivers with STQ, `ALPHA 0x44`; no self-shadowing | +0.1 to 0.4 ms | +32 KiB | the player's shadow play in the beam |
| L2 stretch | Counter stencil for at most two dynamic lights per frame, chosen by coverage x intensity, reusing BFG's silhouette and Z-fail decision code | +0.1 to 1.3 ms, +0.1 to 0.2 ms VU1 | +458,752 B | BFG-equal for those lights |
| L3 reference | Every light's volume, prelight volumes retained | 1.6 to 3.9 ms for six lights, plus 1.2 to 4.9 MB of volume data | | parity; the validation reference for the baker |

Costs assume 50% of the manual's peak fill and are not measurements. L0 ships first; L2 is
scheduled after the frame budget is measured on hardware (section 19).

### 9.4 Counter stencil in code

```cpp
void shadow::Counter::RenderLight(dma::DisplayList & list, const LightView & light,
                                  const VolumeList & volumes)
{
    list.Direct(ClearScratch(light.scissor));                       // R := 0, DTHE off
    list.Direct(SetAlpha(kAlphaIncrement));
    for (const Volume & v : volumes)                                 // all increments first
    {
        if (v.renderZFail)
        {
            Draw(list, v, Faces::Back, ZTest::Always);
        }

        Draw(list, v, Faces::Front, ZTest::GEqual);
    }

    list.Direct(SetAlpha(kAlphaDecrement));

    for (const Volume & v : volumes)
    {
        if (v.renderZFail)
        {
            Draw(list, v, Faces::Front, ZTest::Always);
        }

        Draw(list, v, Faces::Back, ZTest::GEqual);
    }

    list.Direct(TexFlush());
    list.Direct(ResolveToSceneAlpha(light.scissor));                 // A := (R != 0) ? 0x80 : 0
    // the caller now draws this light's passes with DATE=1, DATM=0, alpha masked
}
```

`Draw` binds `vu::Program::ShadowVolume`, whose per-vertex work is the light-space
extrusion to the light bounds (finite, so the kernel can clip it), the screen winding sign
and the two ADC-patterned kicks. Volumes are clipped to the light bounds on the VU, which
replaces BFG's depth-bounds test; the one-unit bias away from the viewer moves into the
VU's Z.

---

## 10. Materials, normal maps and textures

### 10.1 Compiling materials to passes

The cook evaluates every material's stage list with id's parser (built into the tool) and
emits a pass table:

| Material feature | Retail count | PS2 pass |
| --- | --- | --- |
| bump + diffuse + specular interaction | about 4,600 materials | base pass with the cavity-baked diffuse (10.2); specular per class (10.3) |
| `alphatest` | 743 | `ATE=1, ATST GEQUAL, AREF` from the register, `AFAIL KEEP`; the coverage image is the diffuse CLUT alpha |
| blend stages (`add`, `blend`, `filter`, `gl_*` pairs) | about 1,100 stages | one pass each with the equivalent `ALPHA` row; `gl_dst_color, gl_zero` (modulate by colour) becomes `Cd x As` with the stage image's luminance in alpha |
| texture matrix (`scroll`, `rotate`, `scale`, ...) | 840 | 2x3 UV matrix in the pass constants, patched per frame when register-driven |
| `if` conditions, `parm`-driven colour | 329 / 1,601 dynamic | `EvaluateRegisters` on the EE, pass enable and colour patched |
| `texgen reflect` | 121 materials, 52 cube maps | per-vertex sphere-map ST on VU1 from a cooked 2D map (10.3) |
| `texgen skybox` / `wobblesky` | 17 / 9 | sky program: six quads with cooked faces, drawn first with Z writes off |
| `videomap`, `cameracubemap`, mirrors, `_currentRender` | 119 dynamic-image materials | mirrors and remote cameras are bounded subviews (13.3); `_currentRender` reads the frame buffer as a texture; cube cameras degrade to a static sphere map; `videomap` (PDA videos, screens, intro) has no Bink or `idCinematic` on the PS2 and shows a cooked still frame until a converted-video path exists (decision in 20.1) |
| `portalSky` surfaces | portal-sky boxes (areas 0, 3 and 4 of mars_city1) | the sky program draws the sky area's cooked skybox in place of the nested scene; no second `RenderScene` |
| custom `vertexProgram`/`fragmentProgram` (heat haze, blood orb, enviro suit) | 61 | heat haze: frame-buffer texture with a per-vertex UV wobble; the rest degrade to their fixed stages |
| deforms (sprite, tube, eyeball, particle, particle2, flare, expand, move, turbulent) | 275 | section 13 |

### 10.2 Normal maps

The GS cannot evaluate `N·L` against a normal map per pixel, and emboss (two shifted
height passes) cannot be multiplied by the albedo afterwards because the GS has no colour x
colour blend. Measured against the retail data: 1,963 bump images, usually the same size as
their diffuse, with model textures holding 63% of the bump texels.

**Default: bake the normal map into the diffuse.** For each material the cook computes a
light-independent term per texel from the decoded normal (`sqrt(1 - x² - y²)` from the
DXT5 `wy` channels):

```cpp
// cook, per texel; L0 is the material's key light in tangent space, a cook policy
// (default normalize(0.3, 0.3, 0.9): mostly along the normal, a little from above)
float shade  = Clamp(Dot(n, L0) / L0.z, 0.5f, 1.5f);
float cavity = 1.0f + amplitude * (shade - 1.0f);      // amplitude per material class
diffuse      = Clamp(diffuse * cavity);                 // before palettisation
```

This keeps the surface detail Doom's look depends on under the baked key light, costs
nothing at run time, and is identical to a separate modulate pass unless the amplitude must
change at run time. Three orientation variants (wall, floor, ceiling) are available per
material where the key direction matters.

**Stretch: palette-indexed normal maps relit per draw** (TimeSplitters 2's unfinished bump
mapper): a `PSMT8` normal texture whose 256-entry CLUT is rebuilt per draw with
`alpha = N_i·L`, written into the destination alpha, followed by `diffuse x Ad`. It is
exact per pixel for one light direction per patch, so it fits rigid weapons, the flashlight
("headlight" case) and world patches under 128 units; it needs `PSMCT32`. It is a spike,
not a default (section 19, R5).

### 10.3 Specular and gloss

- **Per-vertex Blinn** for characters, weapons and props: `c² = (N·S)² / (S·S)` with
  `S = normalize(L) + normalize(V)` needs one DIV, then `c^10 = (c²)^5` in three
  multiplies. Folded into the base pass as white through `TFX HIGHLIGHT2` with the specular
  scalar in the vertex alpha: zero extra passes. Tinted specular (`specularmap` colour) is
  one additive pass with the specular map as texture.
- **Gloss and environment** for metal classes (Black shaders 4/7/8): the cook derives a
  gloss mask from the specular map's luminance into the diffuse CLUT alpha; context 1 draws
  the base and writes that alpha, context 2 adds a view-space sphere-mapped environment
  (`ALPHA 0x58`, no Z write). The 52 retail cube maps convert to 64x64 or 128x128 `PSMT8`
  sphere maps (under 1 MB in total). In the `PSMCT16` profile the mask is binary.
- World surfaces get no specular by default; glossy world materials (specular luminance
  above a threshold) may opt into the environment pass.

### 10.4 Texture format: GSTX version 2

GSTX v1 (`src/tools/assets/gstx.h`) serves atlases with placements and region clamp. World
and model textures need:

- power-of-two sizes up to 512, `REPEAT` wrap, one texture = one `TEX0`;
- mip chains (`TEX1.MXL` up to 3 or 4 levels in practice, each level a page set with its own
  `TBP/TBW`, levels packed into the same pages where possible);
- `PSMT8` with a 256-entry CLUT or `PSMT4` with a 16-entry CLUT, chosen per texture by
  measured error (the UI tool already does this), with CLUT alpha carrying coverage or
  gloss;
- a usage tag (diffuse, light cookie, lookup ramp, sphere map, lightmap atlas), a
  constant-alpha CLUT variant for lit passes (section 8.4), and the decoders the current
  BIMAGE reader lacks: YCoCg and normal-map DXT5 plus the non-DXT formats a retail
  `(format, colorFormat)` census finds (`RGB565` light images at least; `L8A8`, `LUM8`,
  `INT8`, `ALPHA`, `RGBA8` if present).

```cpp
struct GstxHeaderV2            // 64 bytes (static_assert), little-endian, canonical like v1
{
    u32 magic;                 // 'GSTX'
    u16 version;               // 2
    u16 headerBytes;           // 64
    u16 width, height;         // level 0, powers of two
    u8  psm;                   // 0x13 PSMT8, 0x14 PSMT4
    u8  mipLevels;             // 1..7
    u8  usage;                 // diffuse, cookie, ramp, sphere, lightmap, ui-atlas
    u8  wrap;                  // REPEAT/CLAMP per axis
    u16 clutEntries;           // 16 or 256
    u16 levelCount;            // table rows follow
    u32 clutOffset, clutBytes, clutFnv;
    u32 levelTableOffset;      // rows: {u32 dataOffset, dataBytes, decodedBytes, fnv; u16 w, h, tbw, flags}
    u32 fileBytes, headerFnv;
    u8  reserved[20];
};

static_assert(sizeof(GstxHeaderV2) == 64);
```

Loading validates every table and FNV before publication (as the UI path does) and keeps
the decoded bytes in the EE copy for re-upload after eviction.

### 10.5 Residency and streaming

- VRAM texture arena: whatever the frame-buffer profile leaves after lightmap atlases,
  CLUT pages and the optional shadow scratch (1.18 to 1.64 MB in the `PSMCT32` profile at
  512x448, section 16.2), shared with the UI images and glyph sets. The current allocator (`vram.*`,
  page-granular first-fit with generations) grows a block-granular footprint
  (`TextureFootprintWords`, quake2-ps2) and LRU eviction with pinning of textures referenced
  by the chain in flight.
- The EE keeps decoded copies only for the **area ring** (the visible area and its portal
  neighbours): mars_city1's worst 1-ring is 0.88 MiB of colour textures at the balanced
  policy (halved, half of them `PSMT4`, with mips), 0.37 MiB on average. Textures outside
  the ring are loaded from the level package when an area becomes visible, prefetched one
  portal ahead. The budget per portal crossing is that worst ring against the measured
  read rate of each device (`host:`, USB, HDD; gate R2 records them); a miss draws the
  stale or white texture and never stalls the frame.
- Uploads ride inside the VIF1 chain as `FLUSHA; DIRECT` blocks (`BITBLTBUF`, `TRXPOS`,
  `TRXREG`, `TRXDIR`, then IMAGE data by REF), followed by `TEXFLUSH`, in the order the
  frame needs them (TimeSplitters). This keeps PATH2 and PATH1 ordered with no arbitration
  hazards. Moving uploads to PATH3 with `MSKPATH3` interleaving (Black) is an optimisation
  for gate R9 if upload volume ever stalls the chain.
- One texture and its CLUT bind together (`BindPair`); every textured `TEX0` writes
  `CLD 1`, as the GSTX path does today, until CBP caching is measured worth its hazards.

---

## 11. Animation and skinning

### 11.1 Data

Retail: 257 skinned meshes (mars_city1 ships 50 with up to 62 joints per model and 47 per
mesh; the campaign maximum is 131 joints and 115 per mesh), four weights per vertex with
byte joint indices, bind-pose model-space positions, and 2,681 animations at 24 Hz storing
only animated components (1.4 of 6 per joint and frame on average) as float32.

Cooked formats:

- **Skinned mesh** (`.d3sk`): batches of at most 40 vertices with a **joint palette** of at
  most 24 model-global joints (4 qwords each, column-major 3x4 for `mulax/madday/maddaz/maddw`),
  vertices re-indexed to palette slots. Triangles are sorted by joint set before batching:
  duplication is 1.41 to 1.51x at caps of 64 and 48 against 1.61x without the sort; palette
  size between 16 and 32 changes the result by under 3%, the vertex cap is the binding limit
  (the simulation at a 40 cap and a 12-joint palette gave 1.58x). Vertices are split into
  weight classes (one weight 57%, two 26.5%, three or four 16.7%) so the rigid kernel skips
  blending. The whole mars_city1 plus common set is about 1.7 MiB at 24 to 28 bytes per
  vertex, unverified at the 40 cap with the class split.
- **Animation** (`.d3an`): id's sparse structure with `s16` components (quaternion xyz over
  32767, translation through a per-channel range), `w` reconstructed as `CalcW` does. A
  62-joint monster costs about 174 bytes per frame; the mars_city1 manifest set is 8.24 MiB
  against 16.47 MiB of retail floats, of which 3.69 MiB are cinematics longer than 300
  frames that stream.

### 11.2 Joint pipeline per frame

```
EE (game, idAnimator)            VU0 macro mode                        VU1 (per batch)
 blend quats/translations  ───▶   quat → 3x4 matrix per joint        palette[k] x bindInverse
 (idAnimBlend, as today)          parent chain multiply               arrives in the batch's input half
                                  x inverse bind pose                 (double-buffered with the vertices)
                                  write palette qwords into the
                                  scratchpad display list (CNT payload)
```

The joint array stays game-owned (`renderEntity_t.joints`). The quaternion conversion and
parent chain are `SIMDProcessor->ConvertJointQuatsToJointMats` and `TransformJoints` in
the game's animator, so the PS2 SIMD processor implements them in VU0 macro-mode asm
(`vec_mat.h` patterns: `lqc2`, `vmulax/vmadday/vmaddaz/vmaddw`, `sqc2`). The renderer's
`joints x invertedDefaultPose` product stays on the EE as `jointsInverted`, which the CPU
traces read, and the batch palettes are gathered from that array. A 62-joint model is about
2 to 4 k FMAC operations per frame, cheap. Per-batch palettes are
gathered from the joint array by palette index and written into the display list, so the
VIF unpacks them into VU memory ahead of the vertices with no extra DMA tag.

### 11.3 Skinning kernels on VU1

```
model_skin4, per vertex (software pipelined, 2 vertices in flight):
  lq      fPos, 0(iVert)   ; s16 xyz widened, w = ADC
  lq      fNrm, 2(iVert)   ; s8 normal
  ilw.x   iJ0, 3(iVert) ... iJ3           ; palette offsets, cooked as slot x 4 (the VU has no shift)
  lq      fW,  4(iVert)                    ; weights as 0..256 integers, cooked to sum to 256: itof8 gives the fractions
  iaddiu  iM0, iBase, kPalette ; iadd iM0, iM0, iJ0   (4 qw per joint)
  mulax   ACC, m0c0, fPos[x] ; madday ACC, m0c1, fPos[y] ; maddaz ACC, m0c2, fPos[z] ; maddw fP0, m0c3, vf00[w]
  ... same for joints 1..3, each scaled by its weight: mulx.xyz fP0, fP0, fW[x] ... add
  normal: 3x3 part only, renormalised with one RSQRT per vertex (or per two vertices)
  then as model_rigid: transform, lights, specular, emit
```

The rigid kernel is one matrix and no weights; the two-weight kernel skips joints 2 and 3.
Kernel selection is per batch, so a monster is three programs' worth of batches sorted by
program (section 7.3). Skinned shadow volumes (L2) skin positions with the same palette in
`shadow_volume`.

### 11.4 Traces and gameplay

`ModelTrace` and hit-location joints keep id's CPU path over each mesh's `idDrawVert`
mirror, skinned with `jointsInverted` (game joints x inverted default pose) through
`staticModelWithJoints`, as `tr_trace.cpp` does today.

---

## 12. VU0 and the scratchpad

### 12.1 The display list lives in the scratchpad

Black builds every DMA tag and VIF/GIF payload in the 16 KiB scratchpad and drains 8 KiB
bursts to main RAM with `fromSPR` DMA. Adopted:

```cpp
namespace ps2::renderer::dma
{

class DisplayList                 // one per frame ring, reusable component
{
public:
    // Reserve 'qwords' of contiguous payload in the scratchpad half being filled.
    // When the half is full, start a fromSPR burst to the main-RAM ring and switch halves.
    qword_t * Reserve(u32 qwords);

    // Tag helpers: CNT (payload follows), REF (data elsewhere), CALL (sub-chain, depth <= 2),
    // each carrying two VIFcodes in the tag's upper half (TTE).
    void Cnt(u32 qwords, u32 vif0, u32 vif1);
    void Ref(const void * data, u32 qwords, u32 vif0, u32 vif1);
    void Call(const void * chain, u32 vif0, u32 vif1);
    void Direct(const GsRegisterBlock & block);        // DIRECT with an A+D GIF tag
    void End();                                        // END tag, final drain

private:
    qword_t * m_spr[2];        // 0x70000000 halves, 8 KiB each
    u32       m_ring;          // main-RAM ring write offset, uncached view
    ...
};

}
```

- CNT, REF and CALL carry the content, so the drained list is position-independent and
  needs no address patching; the chain ends with END and program sub-chains end with RET.
  Implemented 2026-11-03 (`dma_list.{h,cpp}`, `dma.cpp`): each frame's chain starts at
  offset 0 of its own ring and a ring that fills mid-frame kicks, waits and restarts at 0
  (the `drains` statistic) instead of wrapping through a NEXT tag, so the two rings need no
  wrap tag and an overflow is a counted restart rather than silent corruption.
- The ring is written by DMA, never by the EE, so no D-cache flush is needed per frame; the
  EE writes only to the scratchpad (uncached). Dynamic per-frame data (joint palettes,
  light blocks, particle vertices) is written into CNT payload in the scratchpad rather than
  into cached main memory, which removes the `FlushCache(0)` per kick the 2D path pays now.
- Two chain rings of 384 KiB each: quake2-ps2's heaviest frames reached 500 to 660 KiB at
  about 60 batches per frame because its vertices rode in the chain; here vertices are
  REF'd, so a world batch costs about 9 qwords (tags plus header) and a skinned batch adds
  its palette (up to 96 qwords). At about 900 batches with 250 skinned the list is roughly
  250 to 400 KiB; the rings are sized from the measured peak at R4 and palettes move to a
  separate REF'd buffer if they dominate.
- The `fromSPR` DMA alternates bus cycles with the EE; a burst of 8 KiB takes a few
  microseconds and the EE keeps filling the other half.

### 12.2 VU0 macro mode

Used for exactly the work it is good at and where quake2-ps2 has measured asm: 4x4 matrix
products (view, projection, per-entity MVP), frustum tests of batch bounds (eight corners
against the frustum matrix with one `Transform` each and a mask AND), joint matrix
composition (11.2), and the light-set ranking's dot products. Rules from
[vu-microprograms.md](../../.claude/rules/vu-microprograms.md) apply: `$vf` register
prefixes, `"f"` constraints for floats, fused blocks to hide FMAC latency, no host fallback,
proven with the scratchpad test-ELF recipe before replacing scalar code.

### 12.3 VU0 micro mode, optional

VU0's 4 KiB data memory holds 256 qwords. Candidates for a micro program started with
`VCALLMS` from the game thread: particle simulation for the heaviest effects, or blending
animation channels for several entities. Both are experiments after R7; the EE must use the
`.ni` COP2 forms around a running micro program or it stalls, so the win is not guaranteed.

---

## 13. Particles, decals, GUIs, subviews, fog and post-processing

### 13.1 Particles and deforms

id regenerates particle quads on the CPU every frame (`Model_prt.cpp`), four `idDrawVert`
per quad. On the PS2 the particle stage runs its RNG and age logic on the EE as today, but
emits **one 16-byte record per particle** (position, colour, size, rotation, UV frame) into
the display list; `vu::Program::Particles` billboards it into a SPRITE or two triangles. The
mars_city1 manifest's particles average 21 quads per declaration, 81 at most; the whole
pass is small. The particle and particle2 deforms feed the same program; sprite and tube
deforms are the same program with an axis; flare, expand,
move and eyeball deforms stay EE-side (few vertices); the four `turbulent` materials get a
VU UV wobble.

### 13.2 Decals and overlays

Impact decals (world) keep id's generation over the world mirror (`MAX_DECALS 32`,
deferred 4, as the non-PC constants say) and draw as WC batches written into the display
list per frame: at most 18 vertices each. Blood overlays (`MAX_OVERLAYS 8` per model) copy
the base model's vertices, including the skinned path, so they are generated over the
`idDrawVert` mirror and drawn with the model's skinned layout and palette.

### 13.3 In-game GUIs and subviews

- In-world GUIs (101 entities in mars_city1 carry a `gui` key) run `idUserInterface::Redraw`
  on the EE as id does and draw through the 2D collector with the surface's model matrix,
  depth tested against the world. Recursion stays capped at 4.
- Mirrors (6 materials), remote cameras (3) and X-ray views render into a texture-sized
  region of the frame buffer first, then the main view samples it. One subview per frame,
  bounded in resolution (256x256), is the budget; more are skipped with a logged reason.
- The portal sky is not a nested scene on the PS2: the sky program draws the cooked skybox
  of the sky area behind the portal surfaces, and the `_currentRender` capture that follows
  it in `PlayerView.cpp` becomes a tagged no-op on sky maps.
- `CaptureRenderToImage` for heat haze and full-screen effects binds the frame buffer's
  own pages as a `TEX0` texture after a FINISH fence (Black and many PS2 titles do this);
  the effect draws a screen-aligned pass reading it.

### 13.4 Fog, blend lights and post-processing

Fog and blend lights draw as in 8.6. BFG's post-process pass is only a resolution-scale
copy and is not needed. The "bloom"-style `_accum` effects degrade to a vertex-alpha
screen quad.

---

## 14. The UI on PATH1

Today every UI triangle is an A+D block of 15 to 17 qwords written by the EE through
`DIRECT` (about 300 bytes per triangle), with CPU clipping and per-triangle `TEX0`
writes. The 3D renderer's PATH1 path makes a 2D program natural:

- `vu::Program::Ui2D` takes packed 16-byte vertices (x, y as s16 1/16 px; u, v as 4.12;
  rgba) in runs that share a page, CLUT, blend and mask state, with the run's A+D block
  in the batch header; it scales 640x480 to GS space, and applies the placement's UV map.
- The EE still owns SWF execution, mask classification and pass expansion (Screen = three
  channel passes, colour terms, replace-colour), placement lookup and residency; the
  collector groups triangles into runs and writes them into the display list.
- Convex CPU masks remain CPU clipping; the depth-mask scheme must change because the Z
  buffer now holds the world: UI depth masks run after the world with a Z clear of the mask
  rectangle (one sprite) or move to the destination-alpha bit with `DATE`.
- This unification is gate R8, after the world draws; its benefit is one submission path,
  no per-triangle EE register traffic, and the 2D test gates confirm pixel parity against
  today's captures. The current DIRECT path stays as the reference until then.

---

## 15. Asset re-export pipeline

### 15.1 Tools

`asset_reexport` grows from its `ui` subcommand into the level cooker; the shared
`src/tools/assets/` library keeps the resource reader, BIMAGE decoder (plus YCoCg and
normal-map DXT5), palettes, packing, RLE and GSTX. New stages:

```
retail .resources ──▶ resource catalog (later containers win)
     │
     ├─ materials (.mtr) ──▶ material compiler ──▶ pass tables (.d3mt) + texture job list
     ├─ textures (.bimage) ──▶ decode, bake cavity/gloss, palettise, mip, pack ──▶ .gstx v2
     ├─ world (.bproc or text .proc, .map, .bcm) ──▶ areas/portals/nodes copy, surface → chart → batch,
     │        light classification, lightmap bake, chroma subdivision ──▶ .d3wd (+ .gstx lightmaps)
     ├─ static models (.blwo/.base/.bma) ──▶ batches + CPU mirror + baked vertex colour ──▶ .d3md
     ├─ skinned models (.bmd5mesh) ──▶ weight classes, joint-set sort, palettes ──▶ .d3sk
     ├─ animations (.bmd5anim) ──▶ s16 sparse ──▶ .d3an
     ├─ particles, decls ──▶ unchanged text (engine parsers), referenced textures cooked
     └─ level manifest (.preload) ──▶ package index: per-area texture sets, prefetch order
```

Every format has magic, version, explicit little-endian integer widths, checked counts and
offsets, FNV-1a over tables and data, and the converter version. Output is byte-identical
across hosts (`-ffp-contract=off`, no `double` paths; id's `R_Dropsample` uses doubles and
is re-implemented in float with the same rounding rules where the cook needs it). Retail
inputs and cooked outputs stay ignored under `build/`.

### 15.2 Byte-order and layout traps already found

- id's binary writers mix endianness: `WriteVec3`, `WriteFloat`, string lengths and
  `idDrawVert.xyz` are little-endian; everything through `WriteBig` (counts, `st` halves,
  indices, portals, nodes, `.bmd5anim` counts, bounds, joint info and base-pose
  quaternions) is big-endian, while `.bmd5anim` base translations, component floats and
  `totaldelta` are little-endian. The scratch decoders used
  for this plan parse every retail model, animation and `.bproc` to the exact EOF.
- `.bproc` writes the type string `shadowmodel` for every model entry (`ParseModel` and
  `ParseShadowModel` both write it; portals and nodes keep their own strings); `_areaN` and
  `_prelight_*` are told apart by name.
- The `.bproc` `_area` vertices carry garbage in `color`/`color2`; honour only the
  material's vertex-colour flag.
- Diffuse BIMAGEs are YCoCg-scaled DXT5 (the Co/Cg scale is in `z`); bump maps store X in
  alpha and Y in green with Z reconstructed; `heightmap()` and `addnormals()` results are
  already evaluated in the shipped files.

### 15.3 Quality policies

Three cook policies are defined so gates can compare them (measured on mars_city1's
colour set of 518 images, the manifest's 630 animations and the world):

| | Q1 fidelity | **Q2 balanced (default)** | Q3 tight |
| --- | --- | --- | --- |
| Colour textures | retail size, `PSMT8`, mips | halved, `PSMT4` where the error allows (assume half), mips | halved, all `PSMT4`, mips |
| Whole-level textures | 28.8 MiB | 5.6 MiB | 3.6 MiB |
| Worst area ring (1-ring) textures | 4.49 MiB | 0.88 MiB | 0.55 MiB |
| World vertex | 28 B float | 20 B (WA) | 20 B |
| World geometry (batches, 64 B headers) | 1.97 MiB | 1.45 MiB | 1.45 MiB |
| Static and brush models | 1.42 MiB | 1.02 MiB | 0.92 MiB |
| Skinned meshes (level + common) | 2.46 MiB | 1.67 MiB | 1.54 MiB |
| Lightmap luxel | 16 u | 16 u (`PSMT8`) | 32 u (`PSMT4`) |
| Lightmap atlases (charts of at least 1,024 u², with borders) | 0.29 MiB | 0.29 MiB | 0.12 MiB |
| Animations (manifest) | 16.5 MiB float | 8.2 MiB s16, cinematics streamed | 8.2 MiB |

Q1 does not fit VRAM even per ring. Q2 is the default and the baseline for the visual pass;
the user's review of `UI_PREVIEWS`-style side-by-side PNGs decides per-class exceptions
(characters and weapons at retail size, for instance).

---

## 16. Memory budgets

### 16.1 EE RAM (32 MiB)

Measured starting point (release, 2026-10-31): `PT_LOAD` 4.06 MB; the retail
`startup-game` ledger (declarations, scripts, containers, particles) 16.5 MB of heap
backing in 74,564 allocations; about 11 to 12 MB remain before stacks, arena slack and any
map. Backing adds up to 25% to the payload figures below. Proposed renderer envelope for one loaded level, to be rebalanced at
each gate:

| Owner | Budget | Notes |
| --- | --- | --- |
| World and brush batches | 2.5 MiB | Q2: 1.45 + 1.02 MiB |
| World CPU mirror (`s16` positions, `u16` indices) | 0.7 MiB | 62,934 + 6,387 vertices, 168 k indices |
| Skinned meshes resident (level + common) | 1.7 MiB | all of mars_city1's; larger levels stream by manifest |
| `idDrawVert` mirrors for traces, decals and overlays (characters, props) | 1.1 MiB | 34,654 model vertices at 32 B plus indices |
| Map-resident game data (collision, AAS, entities, sound world) | measure at R2 | mars_city1's container holds 5.4 MB of binary collision and 2 MB of AAS as id stores them |
| Animations resident | 2.5 MiB | non-cinematic set of the level (4.55 MiB Q2 for mars_city1 needs pruning by use or streaming) |
| Textures, EE copies for the area ring | 1.5 MiB | Q2 worst 1-ring 0.88 MiB plus characters and weapons |
| Lightmap atlases, EE copies | 0.5 MiB | 301 KiB at 16 u for mars_city1 |
| Display-list rings (2 x 384 KiB), frame arena, light blocks | 1.0 MiB | replaces the 2 x 64 MiB frame arena and 2 x 128 KiB DIRECT halves |
| VU program chains, CLUT images, material pass tables, manifest | 0.3 MiB | |
| Streaming buffers (two area loads in flight) | 0.5 MiB | |
| **Total without map game data** | **12.3 MiB** | already above the 11 to 12 MB headroom before collision, AAS and entities |

The envelope does not close as a whole-level residency: per-area streaming of textures,
animations and model batches is required, not optional, and the declaration text lever
(4.76 MB on demand, the next item in [memory-budget.md](../../.claude/rules/memory-budget.md))
is pulled before R2. Level transitions unload the previous level first (`ExecuteMapChange`
ordering). Load and unload follow the owners in reverse `Init` order (movies, fonts, UI
images, then models, world, textures, lightmaps, the VRAM allocator, the VU programs, the
display list, the device); R2 runs three map load/unload cycles and one load-game and
requires the tagged ledger back at baseline each time.

### 16.2 VRAM (4 MiB)

```
PSMCT32 + Z16S profile, 512x448 NTSC            pages   bytes
  frame buffer 0                                 112    917,504
  frame buffer 1                                 112    917,504
  Z16S                                            56    458,752
  shadow counter scratch PSMCT16S (L2 only)       56    458,752
  lightmap atlases, visible areas (2-3 x 64 KiB)  24    196,608
  CLUT pages (256-entry CLUTs, 8 per page)         8     65,536
  texture arena, shared with UI images, glyph     144  1,179,648   (1,638,400 without the scratch;
    sets (136 KiB) and the L1 target (32 KiB)                       the UI's 2 MiB cap shrinks to fit)
                                                 512  4,194,304
PAL 512x512 loses another 48 pages to the taller buffers (16 per colour buffer, 8 for Z, 8 for the
scratch); the texture arena is then 96 pages (0.79 MB) with the scratch, 160 pages (1.31 MB)
without. The PSMCT16 + Z16 profile adds 112 pages to the arena.
```

PAL at `PSMCT32` with the L2 scratch is tight: the scratch is allocated only while L2 is
active, and PAL may run the `PSMCT16` profile if R1's captures show the difference is
acceptable.

### 16.3 VU1 memory

Section 7.2. The binding limits are 88 unique vertices for 3-qword layouts, 68 for 4, and
40 for skinned batches with a 24-joint palette and plain strips.

---

## 17. Performance budget and measurement

### 17.1 Frame budget (30 fps NTSC = 33.3 ms, 25 fps PAL = 40 ms)

| Stage | Budget | Basis |
| --- | --- | --- |
| Game simulation (two ticks: 60 Hz NTSC, 50 Hz PAL) | 12 ms NTSC, 14 ms PAL | unmeasured on the PS2; the headless retail boot runs but no map ticks yet |
| Renderer EE: portal flow, light binding, culling, display list | 6 ms | about 900 batches per frame (650 world at 23 triangles each, 250 model) at 3 to 5 µs each, plus flow and sort |
| VU1: transform, light, clip, emit | 20 ms in parallel | 15 k world triangles (the 1-ring max is 10.5 k; deeper portal views reach 15 k) plus 10 k model triangles in two passes. **This row does not close at quake2-ps2's static estimate of 1.2 µs per triangle pass** (30 ms per pass, 60 ms for two); it needs about 0.4 µs per triangle pass, which the two-context kernel (one transform for both passes) and the unique-vertex walk aim at. Gate R1's VU1 counter test decides whether counts, passes or the target must give. |
| GS: fill, setup, uploads | in parallel | 3 to 6 passes over about 1.5x overdraw of 229 k pixels is under 10% of peak fill; setup at 30 M fogged textured triangles per second is 1.7 ms for 50 k emitted triangles |
| Fences and present | 1 ms | FINISH wait at the frame boundary only |

The EE budget is the reason for principle 1: quake2-ps2 spends about 1 µs of EE time per
triangle because its vertices travel through the chain; Doom's 25 k triangles would cost 25
ms that way. REF'd cooked batches make the EE cost per batch, not per vertex.

### 17.2 Measurement plan

- **PCSX2** gates function and regressions only: captures with pixel probes, the `[D3BFG] CHECK`
  markers, submitted qword, triangle, pass and program-swap counters printed per frame, and
  the frame log format from quake2-ps2 (`FLOG` rows with per-stage microseconds and
  counters, `summarize_flog.py`, `frame_budget.py`). PCSX2 charges roughly per EE
  instruction and nothing for the GS or the cache.
- **Hardware** decides timing: a fixed camera path through mars_city1 (`setviewpos`
  sequence) with the frame log written at the end of the run, in release, on a real console
  via USB or HDD. The first hardware capture is gate R2's exit criterion, before any
  stretch rung is scheduled.
- **VU1 cost** is measured with a counter test ELF in the scratchpad (performance.md recipe)
  before the batch layout is frozen, because no per-triangle VU1 measurement exists in
  quake2-ps2 either.
- **GS cost** is approximated by submitted pixel and texture-page estimates in PCSX2 and
  confirmed only on hardware.

---

## 18. Code design

### 18.1 Namespaces and systems

Following [ps2-backend-cpp.md](../../.claude/rules/ps2-backend-cpp.md): `ps2::module::subsystem`,
one namespace per subsystem spread over its files, namespace-scoped functions for global
singleton systems, classes for reusable components.

| Namespace | Kind | Owns |
| --- | --- | --- |
| `ps2::renderer::gs` | system (exists; owns the VRAM allocator and capture today) | device, present, register builders (grows `MakeTex0/Tex1/MipTbp/Alpha/Test/Zbuf`), VRAM pages and block footprints (`gs::VramArena`), capture |
| `ps2::renderer::dma` | system + `DisplayList` class | scratchpad builder, chain rings, fences, kick; `cmd_buffer.*` moved into it as `dma.{h,cpp}` on 2026-11-03 |
| `ps2::renderer::vu` | system | program table, residency, `Bind`, MPG chains |
| `ps2::renderer::tex` | system (exists) | textures, CLUTs, residency, streaming, GSTX v2 loading |
| `ps2::renderer::scene` | system | `RenderView`, draw list build, sort, per-frame bump allocator |
| `ps2::renderer::world` | system + `Area`, `Batch` classes | cooked world load, area ring, lightmap atlases, layers |
| `ps2::renderer::model` | `Mesh`, `SkinnedMesh`, `Palette` classes | cooked models, CPU mirrors, `idRenderModel` adapters |
| `ps2::renderer::light` | system + `LightSet` class | light classification at load, per-batch binding, light blocks, projected STQ setup |
| `ps2::renderer::shadow` | `Planar`, `Counter` classes | the ladder rungs |
| `ps2::renderer::material` | system | pass tables, register patching, state blocks |
| `ps2::renderer::anim` | system | `.d3an` decode, joint composition on VU0 |
| `ps2::ui` | exists | the 2D collector; later the `Ui2D` run builder |

### 18.2 Reusable components as classes

`dma::DisplayList`, `dma::ChainRing`, `gs::VramArena` (block-granular), `tex::Streamer`,
`world::Batch`, `model::Mesh`, `model::Palette`, `light::LightSet`, `shadow::Counter`,
`material::PassTable`, and in the host tools' existing `assets` namespace `GstxWriterV2`,
`LightmapBaker`, `Stripper`, `JointBatcher`. Each is a class with `m_` members,
constructed by its owning system, testable on the host where it has no hardware dependency
(the baker, stripper, batcher, GSTX v2, pass tables).

### 18.3 Files

```
src/ps2/renderer/
  dma.{h,cpp} dma_list.{h,cpp}  vu.{h,cpp}             scene.{h,cpp}
  world.{h,cpp}            world_format.h             lightmaps.{h,cpp}
  model.{h,cpp}            model_format.h             skinning.{h,cpp}
  lights.{h,cpp}           shadows.{h,cpp}            materials.{h,cpp}
  anim.{h,cpp}             texture_stream.{h,cpp}     gstx2_format.h
  vu1progs/ kernel.i vu_common.i vu_clip.i world_lm.vcl world_lit.vcl model_rigid.vcl
            model_skin2.vcl model_skin4.vcl shadow_volume.vcl shadow_planar.vcl
            particles.vcl ui_2d.vcl passthrough.vcl
src/ps2/math/ math.h vec_mat.{h,cpp}; src/ps2/qwords.h  (imported from quake2-ps2, 2026-11-03)
src/tools/asset_reexport/ main.cpp (ui, world) model.cpp anim.cpp lightmap.cpp materials.cpp
src/tools/assets/ proc.{h,cpp} world.{h,cpp} (D3WD, strips; 2026-11-04) bimage_ycocg.cpp bimage_normal.cpp gstx2.{h,cpp}
                  joint_batcher.{h,cpp} lightmap_baker.{h,cpp} material_compiler.{h,cpp}
src/external/vclpp/ (submodule, nested parse-utils)   src/external/vu-checker/ (submodule)
src/tests/smoketests/ renderer_world_tests.cpp ...   src/tests/host/ world_tests.cpp cook_*_tests.cpp
src/tests/data/graybox_map/ (generated by src/tests/host/scripts/graybox_fixture.py)
```

New units register in `config/sources.mk` (`RESIDENT_SUPPORT_CXX_SRC` and
`CORE_BACKEND_CXX_SRC` for shared units, `SIZE_OPT_CXX_SRC` for load-time units) and the
VU objects in a `VCL_FILES` list with the imported rule; `make compiledb` after each list
change. Engine edits keep the `// [PS2_D3BFG]:` tag; the removed frontend units leave the
campaign list with their replacement named in `BUILD_INVENTORY.md`. R0's bookkeeping
checklist: retire the superseded stubs (`null_render.cpp`, `image_stubs.cpp`,
`vertex_stubs.cpp`, `model_import.cpp`) as their replacements land, with the production
link still rejecting test inputs; `REUSE.md` rows for every imported quake2-ps2 file with
its commit; `CVARS.md` rows for the new cvars (frame-buffer profile, shadow rung, Flashlight
Shadows) with debug and release defaults; a check that the pinned CI ps2sdk image provides
`openvcl` and `dvp-as` and builds vclpp, or `make` breaks in CI; a `make test-<area>`
target and a `docs/testing/` entry per gate; debug builds draw the render world's debug
primitives as GS lines, release builds draw nothing.

### 18.4 Validation policy

As the backend rule states: load and init time validate fully in every build and fail by
name (cooked file tables, FNVs, batch bounds, palette indices, VU program sizes against the
micro memory map); render time asserts (packet reservations, VU memory bounds, state
transitions, light counts, window overruns) and release trusts it. Fatal branches are
`[[unlikely]]`, load-only functions `PS2_COLD_FUNC`.

---

## 19. Milestones and acceptance gates

Each gate is a PCSX2 run with captures and counters, debug first, release when asked; the
hardware gates need a console. Milestones are ordered so every step draws something the
previous one did not, and the budget decisions (frame-buffer format, batch topology, L2)
are taken on measurements.

| Gate | Work | Acceptance |
| --- | --- | --- |
| **R0 toolchain** | Import vclpp, parse-utils and vu-checker as submodules under `src/external/`; the Makefile VU rule; `vu::Init` uploads `kernel` + `passthrough`; `qwords.h`, `vec_mat.h` imported; `dma::DisplayList` in the scratchpad with `fromSPR` drains and chain rings; asynchronous frame (build N+1 while N runs) with the FINISH fence; the section 18.3 checklist | `make` builds and checks a VU object; a core test draws the existing 2D scene through the new chain with pixel parity; a VU0 asm test ELF passes in PCSX2; a chain-ring overflow and a VU window overrun are expected-fatal probes; hardware probes for `DATE` (PCSX2 software renderer too), large texture coordinates, DMA cache coherence and a `PSMCT16S` scratch sharing the `Z16S` buffer are written and run on the first console session |
| **R1 first world pixels** | `world_lm` with vertex colour only; cooked `.d3wd` of one authored room and of mars_city1 area 2; both frame-buffer profiles; strip index hybrid versus ADC strips measured by VU1 counter | Fixed-camera captures of the room in both profiles; mars_city1 area 2 draws with correct depth and portal culling; the format and topology decisions are recorded in section 20 |
| **R2 lightmapped world** | Lightmap baker (category A), chroma subdivision, Light8 atlases, `Cd x As` pass, area-ring texture streaming with GSTX v2 (Q2 policy), material pass tables for opaque and alpha-tested materials, sky | Walkthrough of mars_city1 on a fixed camera path; area, portal and node counts and sampled `PointInArea`/`AreasAreConnected` results match the proc loader, and a door portal toggle still gates sound; captures compared against PC `screenshot`s at the same `setviewpos` with `r_skipSpecular 1` within the eight-level pixel model and an SSIM floor of 0.9 per view; three map load/unload cycles and one load-game return the tagged ledger to baseline; the per-device read rates (`host:`, USB, HDD) against the worst area ring are recorded; the first **hardware frame log** |
| **R3 layers and switches** | Categories B, C, D layers through destination alpha; `idLight` on/off/fade drive them; fog and blend lights | Scripted light switches in mars_city1 match the PC within the recorded tolerance; a 60-frame fade is monotonic in the captures |
| **R4 models and skinning** | `.d3md`, `.d3sk`, `.d3an`; `model_rigid`, `model_skin2/4`; VU0 joint pipeline; CPU mirrors for traces; static props with baked vertex colour; the existing `test-game`/`test-map` fixtures draw their entities | An imp idle and walk cycle matches the PC pose within one unit on sampled joints; `ModelTrace` results unchanged against the fixtures; ledgers exact across reloads |
| **R5 dynamic lights** | Four-light per-vertex path, projected two-pass path, the flashlight and muzzle flash, entity light sets, per-vertex specular; the CLUT-relight spike on one quad | Flashlight capture on the three-plane test room; a moving light over a lightmapped floor; the spike's result recorded as adopt or drop |
| **R6 shadows L0 and L1** | Planar and blob shadows through the A-bit mask; flashlight projected shadow texture behind the option | Overlapping shadow triangles darken once; the projected silhouette matches the analytic box within one texel |
| **R7 the rest of the draws** | Particles program, decals and overlays, in-world GUIs, one bounded subview, `_currentRender` heat haze, fog | mars_city1 start sequence captures (monitors, particles, the hallway heat haze) against PC references |
| **R8 UI on PATH1** | `ui_2d` runs, the collector writing into the display list, depth-mask rework | Every `make test-ui` capture matches the current DIRECT path within the pixel model |
| **R9 optimisation and hardware** | Batch merging per material and area, program-swap minimisation, PATH3 upload interleave if measured, LOD/mips tuning, PAL profile decision, L2 counter stencil if the budget allows | Hardware frame log on the camera path: p95 frame time at most 33.3 ms NTSC and 40.0 ms PAL, no frame above 50 / 60 ms; the memory ledgers stay inside section 16 at every area transition |

Before R1 can draw, the production frame must call the game's draw path: the existing
headless `Frame` gains a `RenderScene` step inside the presentation bracket, and `Common`
must own a loaded map, which is M3 work in [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md).
R0 and R1 use the test executable's authored map until then: the graybox map
(`src/tests/data/graybox_map/`, two rooms with basic shapes joined by a doorway portal).

---

## 20. Decision log, risks and open questions

### 20.1 Decisions taken in this plan

| Decision | Choice | Alternative kept | Why |
| --- | --- | --- | --- |
| Seam altitude | After `FindViewLightsAndEntities`; replace add-lights/add-models/interactions/shadow jobs/vertex cache | Cut at `RenderScene` | Portal flow is cheap and gives the lists we want; area and portal state is shared with sound and doors |
| Geometry transport | Cooked REF'd packets, VIF-decompressed `s16`/4.12/`s8` streams, strip index hybrid | ADC strips | EE cost per batch, not per vertex; 1.48 MB world |
| Frame-buffer profile | `PSMCT32 + Z16S` proposed | `PSMCT16 + Z16` (today) | Destination alpha for projected lights, layers, gloss; no 5-bit banding; decided at R1 |
| World lighting | Baked intensity atlas + per-vertex chroma, layers per switch group | Dense per-vertex baking; three-channel RGB lightmap passes | Expressible on the GS; Quake-proven; layers keep scripted lights |
| Dynamic lights | Four per batch on VU1, additive; projected ones through destination alpha | Per-pixel through CLUT relight everywhere | Budget; CLUT relight is a spike for weapons and flashlight |
| Normal maps | Cavity bake into the diffuse | Emboss; CLUT relight | Zero run-time cost; keeps detail under the baked key light |
| Shadows | L0 baked + planar/blob; L1 flashlight texture; L2 counter stencil as stretch | Full parity | BFG's own default has no weapon-light shadows; counter cost unmeasured |
| Chain rings (2026-11-03) | Each frame's chain starts at offset 0 of its ring; a full ring kicks, waits and restarts at 0 | A continuous ring wrapped by one NEXT tag | Two full-size rings make the wrap pointless; a restart is a counted, debuggable event and the DIRECT path keeps its drain statistic |
| First level (2026-11-04) | An authored graybox map generated with its `.proc` (`graybox_fixture.py`) | The `headless_map` fixture's `.proc` | That one is two floor quads without walls or shapes, and its portal winding faces area 1, so the view flood would pass it from neither side; it stays as `make test-map`'s fixture |
| Strip restarts (2026-11-04) | Bit 7 of each index byte is the GS ADC flag (first two entries of each strip); 7-bit vertex indices | A restart marker byte | The VU writes the flag straight into XYZ2, and 88 vertices fit 7 bits |
| Batch packet tail (2026-11-04) | `ITOP N; MSCNT` in the cooked packet | `MSCAL` with a cooked address | Packets stay valid whatever the micro memory layout `vu::Init` measures |
| Skinning | VU1 palettes of 24 joints, 40 vertices, three weight kernels | VU0 skinning streamed through SPR | Double-buffered palettes ride with the vertices; simulated duplication 1.4 to 1.6x, to be re-run at the 40 cap |
| Lightmap density | 16 units per luxel for Q1 and Q2 (lighting report's chart measurement), 32 for Q3 | the asset report's 16/32/64 | the measured charts give 0.29 MiB at 16 u, cheap enough to keep |
| Video materials | cooked still frames until a converted-video path exists | Bink or RoQ decode | no cinematic decoder is linked; the PDA and screens must not block the campaign |
| Portal sky | cooked skybox drawn by the sky program | nested second scene | removes a full second frontend pass and a frame-buffer capture |
| Texture uploads | Inside the VIF1 chain (`FLUSHA; DIRECT`) | PATH3 with `MSKPATH3` | Ordering safety first; interleave as an R9 optimisation |
| Display list | Scratchpad builder, `fromSPR` drains, CNT/REF/CALL only | Main-RAM builder with cache flushes | No per-frame flush, position-independent list (Black) |
| Resolution | 512x448 / 512x512 FIELD mode | 640 wide; FRAME mode half height | VRAM; decided finally at R1 and R9 |

### 20.2 Risks

1. **Frame budget is unmeasured.** Every per-frame figure in sections 9, 17 and 8.4 is an
   estimate from manual peaks and quake2-ps2 ratios. R2's hardware frame log is the first
   real number; stretch rungs wait for it.
2. **EE RAM.** The envelope (16.1) closes only with per-area streaming and animation
   pruning; the 16.5 MB declaration and script footprint is the fallback lever.
3. **VU1 micro memory.** The suite estimate (about 3,600 instructions) exceeds 2,048, so
   the 940-slot swap region is structural; if swap counts per frame grow, programs merge by
   feature as quake2-ps2 did, and the largest program must stay under the region size.
   The VU1 time row of 17.1 does not close at quake2-ps2's per-triangle estimate, which
   makes R1's counter test the first budget decision.
4. **Z16S precision** in Doom's large rooms and the GEQUAL second passes; Z24 costs another
   458,752 B.
5. **Baked lighting loses bump response** on static light; the cavity bake is the mitigation
   and the visual pass decides amplitude.
6. **Hardware-only bugs**: VU window overruns, large-ST precision loss, DMA cache coherence,
   `DATE` differences between PCSX2 renderers. Each has a probe in R0 to R2.
7. **Cook correctness**: mixed endianness, `shadowmodel` naming, YCoCg decode. The scratch
   decoders parse every retail file to EOF; the tool tests fix that behaviour.

### 20.3 Open questions

- Whether the shared Z buffer across `PSMCT32` scene and `PSMCT16S` scratch behaves on
  hardware exactly as the group rule implies (R0 hardware probe).
- Whether a half-height FRAME-mode buffer is acceptable if interlace flicker on lit edges is
  visible at 30 fps.
- How many lights per view the real portal flow yields in the busiest rooms (areas 15 and 7
  of mars_city1 have 51 to 52 lights in reach), which sizes R5's light binding.
- Whether cinematics' animations (3.7 MiB) stream from disc fast enough on USB and HDD.
- The retail PC reference: `Doom3BFG.exe` under Wine on this Mac is unverified; a Windows
  machine may be needed for R2's reference captures.

---

## 21. References

- id source: `src/neo/renderer/` (`tr_frontend_*.cpp`, `RenderWorld*.cpp`, `Interaction.cpp`,
  `jobs/`, `Model_md5.cpp`, `Material.cpp`, `tr_backend_draw.cpp`), `base/renderprogs/`.
- Current backend: `src/ps2/renderer/`, `src/ps2/ui/`, `src/tools/assets/`,
  [RENDERER_2D_PLAN.md](RENDERER_2D_PLAN.md), [RETAIL_UI_PIPELINE.md](../RETAIL_UI_PIPELINE.md),
  [gs-renderer.md](../../.claude/rules/gs-renderer.md), [memory-budget.md](../../.claude/rules/memory-budget.md).
- Reference port: `~/Repos/GitHub/quake2-ps2` (`src/ps2/renderer/vu1.h`, `vu1progs/`,
  `lightmap.cpp`, `vram.cpp`, `math/vec_mat.h`, `qwords.h`, `Makefile` VU rule,
  `src/tools/vu-checker`), its rules `vu-microprograms.md`, `performance.md`.
- Black: `~/Experiments/black-re/docs/rendering.md`, `docs/vu/README.md`,
  `docs/vu/world-shader.md`.
- TimeSplitters Future Perfect: `~/Experiments/timesplitters-re/docs/16-rendering.md`,
  `docs/10-vu-microcode.md`.
- Manuals: `~/Repos/GitHub/PS2Docs/markdown/` GS User's Manual (§1.3, §2.5, §2.6, §2.7,
  §3.4, §3.7, §3.8, §3.9, §5.2, §8), GS Supplement (§1.2 to §1.4), EE User's Manual (§5
  DMAC, §6.3 VIF, §7 GIF), VU User's Manual (§1, §2, §3.4, §5).
