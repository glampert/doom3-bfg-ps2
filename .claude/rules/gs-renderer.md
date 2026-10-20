# GS renderer facts from the Quake II PS2 reference

The Quake sections' frame flow, source paths, cvars, capacities and measured results describe
`quake2-ps2`. Reuse the GS hardware findings, then verify each design against Doom's
renderer before treating it as a rule for this port.

## Doom renderer interface boundary

The logical adapters in `src/ps2/renderer/` bind native `tr` / `renderSystem`, image,
shader, cinematic and vertex-cache interfaces. Default/headless construction creates
metadata without GS setup or pixel/frame arenas. Explicit visible-mode `rs`/`ui`
activation adds the separate bounded 2D capabilities described below; world submission,
general shaders, cinematics and native vertex-cache buffers remain unavailable.
Before explicit 2D activation, display/draw requests fail by method name. General
renderer initialization and resource loading remain unavailable.
Retained frontend cvars preserve native defaults/flags and cannot enable rendering.
The resident map mode now explicitly initializes native CPU model/world ownership,
with stage-free materials. Areas, portals, static model traces and portal-controlled PVS
pass in the authored map; physical initialization, image/vertex payload loading and
submission still fail by method name. The separate CPU-only GS device layer now draws
solid and direct RGBA 2D frames. Explicit native UI activation now supplies a bounded
white/ASCII image and axis-quad/text profile. Production now optionally draws the real
native console through Common. Bounded indexed GUI collection and CPU triangles now
extend the native adapter. The authored native SWF/font path now passes debug/release NTSC/PAL;
general image loading, retail menus and VU output remain subsequent gates.

- Start the logical renderer frame counter above zero and advance it with game ticks.
  Native `R_EntityDefDynamicModel` compares the definition stamp to `tr.frameCount`;
  at zero an uninitialized definition returns its null cached model, hiding static
  surfaces from `ModelTrace`. This was reproduced on the EE in the headless map.
- Stage-free materials are not drawn. CPU `ModelTrace` needs the native `collision`
  material flag to include these surfaces. Doom's three-point plane normal uses
  clockwise triangle indices for the upward-facing authored floor.
- `R_IdentifySilEdges` uses desktop-sized list/hash granularity `MAX_SIL_EDGES=0x7ffff`.
  The PS2 path bounds scratch granularity by `max(1,min(numIndexes,MAX_SIL_EDGES))`:
  at most one edge can be created per index. This preserves native edge matching and
  avoids multi-megabyte temporary allocations for tiny CPU models.

## Retail host page contract

The 2D-5 host cooker preserves source pixels in 256×256 RGBA pages with a 254×254
interior and one-texel gutter. Internal gutters sample neighboring full-atlas pixels;
clamping each tile to its own edge changes bilinear filtering at seams. Clamp only
the original atlas's outer boundary and unused padding. Each page records a source
rectangle and affine global-normalized-UV to page-UV map; split/clip geometry at page
boundaries before applying it. Native SWF RenderShape uses atlasMaterial, atlasOffset,
image size and channelScale for bitmap fills, not dictionary.material. Dictionary
renaming alone cannot implement paging. `ui_page_image.*` now streams compact UIPG
images through fixed texture slots and calls `ui_page_geometry.*` for page/screen
splits. Native authored SWF/font images now share a `PageCache`; the bounded English
retail font resource screen uses a separate six-slot linear profile. General retail
movie admission and active-menu working-set acceptance remain pending.

Page intersections must be reconstructed from the canonical original triangle and
shared boundary constraints. Chaining interpolation through different preceding
page clips produced a real one-sixteenth-pixel shared-edge mismatch. Track original
edge/plane provenance, mark vertices already on a plane, preserve constant components
and coalesce exactly equivalent constraints for rank-one UV mappings. Merely sorting
the traversed edge endpoints is insufficient. A rounded fixed UV at a page border can
also be a nearby interior vertex; compare genuine shared constraints in seam tests.
Keep this math single precision on EE; guards and exact integer significand products
handle degenerate constraints without software-double operations.

UIPG v1 stores a checked 32-byte little-endian header and eight-byte offset/FNV1a32
records, followed by fixed 262,144-byte raw RGBA pages. Validate exact file length,
dimensions, inferred grid/count and every offset before reserving cache ownership;
verify each complete page before updating a texture slot. The scratch buffer is
separate from texture pixels. Reuse follows existing Update/Bind/Upload fences, so
queued pieces from this frame retire before their texture contents change. Close
images before their cache and the device. Standalone renderer probes use linear/clamp;
the authored native SWF/font profile shares four nearest/clamp slots. Keep the sampler
fixed per cache and reject repeat/unknown sampling before allocating.

Native `idImage` dimensions remain the full atlas dimensions. `SWF_Render` derives
global UVs from the atlas material, and `idFont::GetScaledGlyph` expands glyph UVs by
half a texel. Preserve those UVs through page splitting; publishing 256×256 as the
logical dimensions scales both incorrectly. The authored font leaves padding for
that expansion. Retail fonts contain edge glyphs: the opt-in `outerGutter` mode admits
only [-0.5/width, 1+0.5/width] and its height equivalent. Extend only the atlas's outer
clip planes; map against the original page origin plus its one-texel gutter. This
preserves native glyph geometry and interior samples. Default page admission remains
strictly 0..1. Do not clamp the glyph UVs or rescale the complete texture to fit.
Validate the entire queued native collection before emitting packets; paged emission
must run under the collector's reentry guard because page Update/Bind also flushes it.
Direct glyph quads flush older indexed bitmap runs before touching the shared cache.
Closing an image flushes/fences queued references and invalidates its keys before its
address can be reused. Cache shutdown rejects attached images with assertions off too.

## Doom CPU-only device acceptance

- `ps2::rs` owns a frame, `cmdbuf` owns two tagged 64-byte-aligned command halves,
  and `gs` owns both PSMCT16 contexts and Z16 with depth writes masked. Default
  halves are 128 KiB each. `EndFrame` fences and immediately presents; the final
  picture requires no following frame. Production initializes this layer only for an
  explicit visible console startup policy.
- Reset/claim VIF1 and GIF along with the GS at device startup, after bounded checks
  for incoming DMA completion. A GS CSR reset alone stalled the first Doom PATH2
  DIRECT in PCSX2: VIF1 waited on GIF, with GIF_STAT `0x00000e00`. Resetting both
  interfaces resolved it. Initial/reload runs and release reproduce the fixed path;
  this does not identify a particular loader-state bit or establish hardware behavior.
- All packet reservations leave space for the final EOP and four-qword FINISH/END
  terminator. SDK environment emission is preflighted for sixteen qwords. DMA,
  FINISH and VSYNC polls use a wrap-safe COP0 Count deadline in both builds.
- Debug/release NTSC 512×448 and PAL 512×512 solid/alpha scenes, context flips,
  128 KiB and 1 KiB pressure, repeated teardown and exact tagged ledger recovery
  pass on 2026-10-14. Captures are visually inspected; see PORT_STATUS. These are
  PCSX2 functional observations, not GS throughput or EE cache-coherency results.

## Doom RGBA and capture acceptance

The accepted Doom 2D-2 path uses RGBA32 with logical dimensions up to 512, POT TEX0
extents, a minimum 64-pixel upload/TBW stride and whole 64×32 pages. Every padded texel
repeats the nearest logical edge; region clamp bounds sampling to the logical image.
Repeat requires a POT logical extent on that axis. Engine alpha converts with
`(a*128+127)/255`; textured vertex RGB also converts to 128 unity. The 16-bit color
buffer rounds blended output, so native pixel comparisons allow eight channel levels.

Queued TEX0 references are hazards even before a kick. Dirty reupload, eviction and
destruction drain/FINISH before overwriting or releasing VRAM. Uploads retire PATH2
before synchronous PATH3 IMAGE/REF chains, preflight the SDK's chunk count, write back
both chain and owned pixels, and wait for DMA plus GS FINISH. A 512 KiB upload crosses
the 32767-qword IMAGE split and is checked by native quadrant readback.

Capture accepts only the most recent completed context. Aligned static targets survive
failure; finite request/receive/restore polls restore forward BUSDIR/FDR, IMR and PATH3.
Wait for trailing VIF restore NOPs to leave the FIFO before starting the next strip.
On timeout, suspend DMAC while clearing owned VIF1 STR, then reset owned VIF/GIF and
restore saved idle state. The injected deadline proves state restoration in PCSX2,
not recovery from a genuinely wedged physical GS. No hardware cache claim follows.

## Doom native drawing adapters

Native `ui::Init` is separate from low-level `rs` readiness. Only native activation
sets `R_IsInitialized`; `IsOpenGLRunning` remains false. Shutdown images/UI before
the caller-owned device, without replacing logical-world ownership. The initial
image profile owns only `_white` and `textures/bigchars`; the latter requires clamp
and nearest/default sampling. Its authored ASCII atlas has sixteen 16×16 cells per
axis, 15×14 glyph coverage, native cursor bytes 10/11 and explicit fallback boxes.

Map all console coordinates once from virtual 640×480 to GS 1/16 fixed point. Clip
positions and interpolate UV before converting; normalized UV addresses logical image
dimensions. Reject NaN/Inf/subnormal representations before EE arithmetic and preserve
fractional endpoints. Indexed geometry additionally clips triangles with either winding,
interpolating UV/RGBA before rounding; transformed native quads use the same path.

Explicit UI activation owns bounded CPU spans: 4,096 vertices, 6,144 indices and 128
ordered runs. Fill each returned span before the next allocation/draw. Pressure flushes
completed draws and recycles storage; oversized requests and invalid local indices fail
before flushing. Frame end and sprite emission flush queued triangles first. These are
write-only CPU spans, not native vertex-cache handles; static/index/joint buffer APIs
remain unavailable. Shutdown releases these three allocations before intrinsic images.
Texture update/destruction flushes queued UI references before mutating/releasing the
underlying texture, then preserves the existing command/FINISH residency fence.

Decode native half UVs from their bits and reject exponent 31 before EE arithmetic;
the retained `F16toF32` implementation does not preserve Inf/NaN classification. The
initial indexed profile requires zero depth and exact `color2` bytes of 128. Only a
scoped native SWF draw additionally accepts 127, produced by native `PackColor(0.5)`,
as its zero-additive encoding. Nonzero additive transforms remain unsupported.

Textured triangles select Gouraud RGB/alpha and affine fixed UV. Each triangle reserves
30 qwords and explicitly writes TEX1/TEX0/CLAMP/ALPHA/TEST/PRIM plus all three vertices,
so command-half drains cannot inherit stale primitive state. Validate binding, context,
blend and all fixed XY/UV fields before reserving. Emit exact fixed endpoints plus
XYOFFSET; there is no sprite-specific half-pixel adjustment. GS modulation maps native
255 to unity 128 for both vertex RGB and alpha. Degenerate triangles are skipped;
all-zero-alpha triangles skip only with alpha-weighted blending, preserving replacement
RGB writes at alpha zero. Debug/release NTSC/PAL indexed captures pass twenty-seven
pixel checks, identical reload hashes and 120/four frames on 2026-10-15. All three CPU
pool limits and 1 KiB command-half drains preserve ordering and exact ledger recovery.
A 3×3 shared-diagonal sample on a translucent quad in both debug modes shows single
blending without gaps. These PCSX2 observations do not establish cache coherence or
GS cost on hardware; run identities and images are recorded in PORT_STATUS.

`GLS_DEFAULT` is ONE/ZERO replacement. Ordinary intrinsic console helpers select a
declared straight-alpha profile; `GLS_OVERRIDE` can explicitly select replacement.
Disable GS environment cutout for replacement, since alpha-zero fragments still write
RGB. Reject unsupported blend, mask, depth or shader state instead of normalizing it.
Color packing uses native byte truncation and compile-time EE little endian. Core GS
dispatch runs before idlib initialization, so runtime `LittleLong` callbacks are not
available there. Bounded text is preflighted before glyph emission.

`Console.cpp`, `EditField.cpp` and material/decl implementation are campaign-only.
The core adapter scene calls native draw methods with authored content; it does not
establish real console or resident material acceptance. Production console acceptance
uses the full resident ELF and `run_console.py`, without importing these units into core.

## Doom production console frame ownership

- Read package-root `presentation.cfg` before Common startup: missing means headless;
  otherwise only bounded `headless`/`ntsc`/`pal` content is accepted. Repackaging always
  replaces the policy. `d3bfg.cfg` executes later and cannot select startup allocation.
- One Common frame processes events/commands, advances clocks/logical and native NOMAP
  work, then calls `console->Draw(false)` inside one GS frame. GS vsync paces visible
  mode; do not add the headless idle sleep afterward.
- Native Enter calls `UpdateScreen` synchronously. Coalesce an in-frame request into
  its final presentation; an out-of-frame redraw must not advance simulation. Guard
  drawing to prevent recursive Begin/End calls, including refresh-on-print requests.
- A queued `ps2_capture` reads the newest completed context after the final draw. A
  quit command must finish a queued capture before shutdown, even when both arrive in
  one HostFs batch. Release intrinsic UI images before the caller-owned GS device.
- Native ASCII/key commands drive the retained edit field and RAM history. Persistent
  history writes and physical device polling remain unavailable; production console
  close tolerates the absence of mouse capture. Test capability failures stay explicit.

## Doom authored native SWF profile

The image-allocation details in this section describe the initial 2026-10-15
direct-texture slice. Current authored bitmap/font binding uses shared UIPG pages,
described under Retail host page contract above; additive/convex-mask additions
follow this section. Keep the initial captures as historical acceptance evidence.

- Native SWF uses a bounded normal-blend scope around its existing GUI calls. The
  native GUI fragment shader multiplies texture/vertex RGBA, adds the color transform,
  then premultiplies RGB by alpha for ONE/ONE_MINUS_SRC_ALPHA blending. With the authored
  zero-additive transform, GS straight-alpha modulation produces the same intended
  equation. Do not premultiply the CPU vertex RGB a second time. Scoped native mode 8
  and convex masks are now supported as documented below; other special blends,
  nonzero additive color and complex/stroked masks remain unsupported.
- Native packed zero additive color can be 127, while cleared GUI vertices use 128.
  Accept either only inside the SWF scope and treat it as zero; this does not implement
  general additive transforms or preserve the desktop shader's one-byte quantization
  bias. Preserve the initial strict 128-only contract outside the scope. Flush before
  entering/leaving the scope and restore saved native state/color so console drawing
  cannot inherit SWF state. A scope must not cross a frame or shutdown boundary.
- Keep `_white` and the 256×256 console charset as the two initial images. Only an
  explicit `textures/ps2_ascii` request creates the third owned font image lazily.
  It is 512×128 RGBA8, nearest/default filtering and clamp, with TD_FONT/TD_DEFAULT
  usage. Each printable ASCII glyph copies the existing 15×14 coverage to a padded
  32×16 cell at `s=(code&15)*32+1`, `t=(code>>4)*16+1`. Other pixels stay transparent.
  The image manager owns purge, explicit `ImageFromFile` regeneration and shutdown.
- Native `GetScaledGlyph` expands UVs by half a texel on every side. Console cells
  beginning at x=0 would yield a negative UV for glyphs in column zero; the padded
  font atlas keeps all expanded printable glyph UVs strictly inside 512×128. Preserve
  this gutter instead of clamping transformed UVs and changing their interpolation.
  Font records stay 10 bytes each and the authored font file stays 1,342 bytes.
- Movie rendering precedes the real console in the same production frame. Shutdown
  destroys the movie, then cached fonts, then UI geometry/images and the caller-owned
  device. Font/image warm caches may survive a movie unload; repeated warm unloads
  and full Common shutdown are separate ownership checks.
- Debug/release NTSC/PAL production runs on 2026-10-15 each completed three authored
  movie load/unload cycles, 28 native pixel checks per capture, console overlays, native
  Tab/Enter actions, stable warm ledgers and exact full pre-boot ledger recovery. Six
  fresh malformed/missing asset probes pass in both builds. See PORT_STATUS for matched
  archives; this does not establish retail SWFs, world rendering or hardware behavior.

## Doom native SWF additive blending

Native SWF mode 8 selects ONE/ONE after `gui.pixel` premultiplies its straight texture
and color by output alpha. With the admitted zero-additive-color transform, the GS can
compute the same RGB equation in one pass: `(Cs - 0) * As + Cd`. Select ALPHA A=Cs,
B=zero (2), C=As, D=Cd; enable PRIM.ABE. Texture and interpolated vertex alpha still
multiply through MODULATE, so half texture alpha and half vertex alpha contribute a
quarter source. Zero-alpha sources and black RGB must preserve destination RGB.

`BlendMode::AdditiveAlpha` takes straight RGBA, not already-premultiplied input. Only
the native SWF draw scope maps GLS ONE/ONE to this equation; ordinary ONE/ONE state
remains unavailable. Sprites and triangles write the complete ALPHA/PRIM state on
every draw, including after command-half drains. Page splitting retains the blend
without multiplying alpha again. Existing packet sizes remain 22/30 qwords, and no
extra surfaces, passes or pixel allocations are introduced. COLCLAMP is enabled by
the SDK environment, so overflowing sums saturate.

Model framebuffer storage after every blend when deriving expected pixels. With
dithering disabled, PSMCT16 truncates each RGB result to five bits; capture then
expands those five bits to eight. For example, the authored panel stores 24/40/56.
Native PackColor(.5/.25/.125) yields 127/63/31, and half source alpha contributes
63/31/15. The first sum stores 80/64/64 (captured 82/66/66); adding it again stores
136/88/72 (captured 140/90/74). Comparing repeated sums with ideal floating-point
RGB instead would require a growing tolerance. Preserve the eight-channel threshold
and compute the intermediate storage rounds instead.

These RGB-only presentation profiles do not promise destination-alpha accumulation
in a one-bit-alpha framebuffer. Complex masks, native screen blend 4 and nonzero additive
color transforms remain unadmitted; a movie that needs them is refused full drawing.

## Doom native convex SWF masks

The GS has no native eight-bit stencil counter. The checked CPU bridge captures the
transformed fill triangles from native `RenderMask`; it never emits mask geometry or
clips a new mask against its parent. Reverse interior edges cancel, one closed
boundary remains, and all boundary points must lie inside every edge. This positive
triangle chain proves one convex coverage region rather than merely matching an area
or bounding box. Degenerate triangles contribute no coverage; an empty mask clips
everything. Concave, disconnected, holed, overlapping, nonconforming and stroked
geometry remains unavailable. The profile bounds each capture at 64 triangles and
16 simplified boundary points, with four active masks and 64 total clip planes.

Flush completed write-only spans before every capture/add/remove. Key masks by the
native display entry, since crossing clip-depth ranges can expire out of stack order.
After `RemoveIndexFast`, revisit the swapped entry when several masks expire together.
Compare native inherited/local mask counts against the backend set before translating
SWF state; arbitrary stencil GLS bits remain unsupported. Reject outstanding mask
ownership at scope, frame and shutdown boundaries in release too.

Clip indexed runs and direct rectangle/font paths alike. Preserve original triangle
edges and mask-plane constraints, reconstruct intersections canonically, keep UV/RGBA
interpolation in float, and round colors only after all mask planes. An exact mask
corner on an original edge retains both constraints. Validate original geometry and
UVs even if an empty/offscreen mask hides it. Then dispatch the clipped geometry
through existing screen/page clipping. Native fill-style alpha does not weight mask
coverage: the material override uses placement/inherited color-transform alpha only
for the native early skip. Do not treat a faint admitted mask as partial coverage.

The authored native SWF exercises three simultaneous masks, crossing ranges, joint
expiry, oblique/transformed boundaries, alpha/additive content, bitmap page seams and text.
The retail inventory has ten rectangular dialog placements, but the shell also needs
a 185-point outline, disconnected/holed meshes and two stroked masks. Its largest fill vertex count is 198; the largest triangle count is 183. Shell drawing remains gated; the dialog's ten rectangular masks draw. PCSX2
captures establish bounded functional clipping, not hardware fill cost or frame-rate
acceptance.

## Doom full retail dialog drawing

The original dialog draws through native `idSWF::Render` with the existing adapters:
bitmap fills via the shared page cache, `guiSolid` fills over the owned white image,
normal and scoped additive blends, convex masks and native glyph quads with text
strokes. No new GS path was needed. Its cost is page streaming: the 1152×596 dialog
atlas spans 15 RGBA32 pages (5×3), and two fonts add theirs, while the retail profile
shares six linear slots. Drawing order interleaves images and glyphs, so LRU misses
about 51 pages per frame (about 890 triangles, 736 page hits). Each miss reads and
checks 256 KiB from HostFs, about 31 ms in PCSX2, so a frame takes about 1.6 s.

More slots cannot fix this. GS memory left after the NTSC/PAL framebuffers holds about
ten 256 KiB pages, below the dialog's working set of roughly 20. Shrinking the pages
(paletted CLUT formats, or downscaling art the 512-wide screen cannot resolve) is the
real fix; the shell atlas alone (60 of the profile's 143 pages) makes it mandatory.
These are PCSX2 HostFs costs, not hardware storage or GS fill measurements.

## Retail UI page format survey (2026-10-20)

A host survey compared GS formats for the original atlases (byte totals are in
[memory-budget.md](memory-budget.md)). Facts to build the paletted pages on:

- **Every retail movie's stage is 1280×960.** At authored size, art appears at 0.4×
  horizontally and 448/960 (NTSC) or 512/960 (PAL) vertically. Many fills are
  stretched far more along one axis (bars, glows); the PDA and pause menu enlarge
  pictures 2–4×. Bound enlargement per image from its fill matrix (scale 20 per texel)
  times the accumulated placement scales.
- **id's packer (`WriteSwfImageAtlas`) aligns each image to a 4-texel block** with at
  least one clamped border texel on every side, normalizes each channel to full range
  (the draw multiplies back by `channelScale`) and fills unused atlas space with solid
  red. A 2×2 premultiplied downsample therefore never mixes images, and bilinear
  footprints at a halved image's edge stay inside its block; quartering would not.
  Exclude the red filler from palettes.
- **One CLUT per atlas image, not per page.** Per-page 256-color palettes score
  43–51 dB and band smooth gradients (rings in the dialog's glow); per-image palettes
  score 47.5–55.6 dB with no visible banding. The GS looks up each texel through the
  bound CLUT before filtering, so a draw may only sample texels quantized with its own
  palette. Block borders keep the footprint inside the image's texels.
- **Halve only images never drawn above about 1.25× their authored size.** That covers
  85–96% of menu and HUD texels and lowers the NTSC display error (less minification
  aliasing); enlarged pictures keep full resolution.
- **PSMT4 is for alpha-only fonts.** Retail font atlases are white RGB with DXT1-green
  alpha. Sixteen alpha levels display identically to 8-bit alpha. For art, 16 colors
  leave 4–17% of texels more than 8/255 off.
- **Method.** The simulation samples each atlas image at its stage scale with GS
  bilinear, straight alpha and PSMCT16 truncation, over black, gray and white. It
  compares candidates with an area-filtered ideal in 5-bit levels. The current RGBA32
  path is not the ideal: at 2.5× minification bilinear already aliases (1.3–15% of
  pixels two or more levels off, depending on the movie). Judge a candidate against
  that baseline, then confirm with target captures.

## Quake frame model: 2D and 3D interleave

- The engine draws **2D → 3D → 2D** per frame (`SCR_TileClear`, then `V_RenderView`, then
  HUD/console in `SCR_UpdateScreen`), with no hook at the boundaries.
- `BeginFrame` clears color+depth. 2D primitives accumulate in a lazily opened pending batch
  (ALLPASS z-test). `FlushPending2D` sends it and is a no-op when empty. **Every 3D emitter
  calls it first**, and `EndFrame` calls it last. Never defer all 2D to frame end: the 2D
  packet bakes texture VRAM addresses, and mid-frame 3D uploads can evict them. Don't
  reintroduce a frame-wide 2D bracket. Each 2D→3D switch is a GS sync point (~2 per frame).
- **With `ps2_gs_latency` on (the default), a frame reaches the screen only when the next
  `BeginFrame` runs.** A frame that nothing follows never shows. The loading plaque was one:
  `SCR_BeginLoadingPlaque` draws it once, then `cls.disable_screen` stops all drawing until
  the level is up, so a slow load sat on a black screen. Draw such frames with
  `rs::EndFrame(/*deferPresent=*/false)`, as `PS2_DrawLoadingScreen` does. `rs::FrameStarted()`
  tells you whether a frame is already open.
- **No frame may begin while a `.bsp` is parsed.** Both command buffer halves live in the world
  loader's lump scratch, lent from `cmdbuf::DrainBeforeWorldLoad` until `BspFileReader::Close`
  (`cmdbuf::LentToWorldLoad()`). `LoadTexInfo` loads every wall texture inside that window, so
  anything that draws from a file hook must check it. A frame built there writes its chain over
  the pinned vertex/edge lumps. That surfaced as "Surface lightmap too large for the
  accumulator!" and is now an assert in `cmdbuf::BeginFrame`. To reproduce: set the loading
  screen's `kRedrawIntervalMsec` to 0, so every file read draws.
- **TEST is not in the VU1 batch register block.** MIPTBP1 took its slot, and the 7-qword tag
  block can't grow: 8 qwords would need 2 more per double-buffer half, and there is 1. 3D
  relies on TEST holding the 3D pixel tests. The clear leaves it so, and `gs::EmitEnd2D`
  re-arms it whenever a 2D section closes. Any new path that writes TEST must restore it the
  same way.

## GS and libdraw

- **Alpha 0x80 = 1.0.** 0xFF is about 2× overbright under `(Cs-Cd)*As/128+Cd`. Scale
  engine-facing 0..255 alpha with `a >> 1`. In MODULATE, vertex colour 0x80 is identity.
- The ALPHA register computes `(A - B) * C + D` where **C is a scalar alpha** (As/Ad/FIX), not
  a colour. Colour × colour (GL's `GL_ZERO, GL_SRC_COLOR`) is not expressible. That is why
  lightmaps are alpha-only intensity atlases, with chroma per vertex. Check this before
  promising any two-texture multiply.
- libdraw's `draw_enable_blending()`/`draw_disable_blending()` toggle a **static read at
  primitive emission** (PRIM.ABE), not a register write. Set it per emission. A global enable
  makes opaque fills blend about 2× bright.
- `draw_setup_environment()` defaults to CLAMP wrap (program REPEAT afterwards where tiling
  is needed) and an alpha test of NOTEQUAL 0 that discards A==0 texels. That is how conchars
  transparency works (palette index 255 has alpha 0). `ATEST_KEEP_*` names what is
  *preserved*: `ATEST_KEEP_FRAMEBUFFER` == GS `ZB_ONLY`. Any value where 0 is meaningful,
  such as a black lightmap luxel, must clamp to 1, or it draws nothing.
- **SCISSOR doesn't clip HOST→LOCAL uploads.** Writes past the destination width wrap in VRAM
  (for a 640-wide PSMCT32 buffer, to `(x-640, y+32)`).
- **One XGKICK with many GIF tags: only the last tag may set EOP.** The GIF stops at the
  first EOP, so EOP on every fan's tag drew only the first fan. Intermediate tags get EOP=0.
  On the VU, remember the last tag's address and OR in `0x8000` after the loop. Keep PRE=1 on
  each tag so the GS starts a new fan.
- `draw_rect_textured` = 4 qwords per sprite. A full console of text is about 9K qwords.

## Texture coordinates and sizes

- **Normalized ST spans the TEX0 TW/TH extent, the image rounded *up* to a power of two**, not
  the image. Every MD2 skin is non-power-of-two (276x194 samples as 512x256). On screen this
  looks like a UV-flip bug, but it isn't one: GS and GL both have T=0 at the first row. Scale
  coordinates in [0,1] with `tex::StScaleFor()`. A *tiling* non-POT texture needs resampling
  on load (33 of 2118 `.wal` files), not a coordinate scale. The 2D path uses `PRIM_MAP_UV`
  texel coordinates and is unaffected.

## CLUTs and palettized textures

- Most textures are PSMT8, sampling Quake's palette. A second CLUT is an alpha ramp for
  coverage-only images (particles, lightmap atlases).
- **CSM1 reorder:** in every 32-entry group the two middle 8-entry blocks swap:
  `(i & ~0x18) | ((i & 0x08) << 1) | ((i & 0x10) >> 1)`, which is its own inverse. If you
  forget it, colours come out scrambled but stable.
- A CLUT uploads as a 16x16 PSMCT32 image: exactly 4 blocks at CBP, dest width 64 (DBW=1).
  CBP is in blocks (word address >> 6).
- **CLD:** `gs::MakeTex0` binds indexed textures with `CLUT_COMPARE_CBP0` (CLD=4), and
  `gs::Init`'s `SeedClutBuffer` writes TEX2 with CLD=2 so CBP0 starts known. Invariants: every
  indexed TEX0 uses CLD=4 (one CLD=1 load leaves CBP0 stale), and CLUT contents never change
  after Init (the compare is on the address only). CLD is ignored for non-indexed formats.
- **TBW must be even for PSMT8/PSMT4** (128-px stride). Narrow 8-bit textures round their
  stride up for TEX0 and the upload (`TextureStridePixels()`).
- Page sizes: PSMT8 128x64, PSMCT32 64x32, PSMCT16/16S 64x64. libdraw's
  `draw_texture_transfer` takes `dest_width` separately, and DBW = dest_width >> 6, so a
  16-px transfer needs `dest_width >= 64`.

## VRAM and mipmaps

- **The GS addresses textures in 256-byte blocks, not pages.** Any block-aligned TBP is
  valid. PSMT8's block order equals PSMCT32's (PCSX2 `_blockTable8 == _blockTable32`), so a
  texture occupies exactly `[TBP, TBP + far-corner block + 1)`. Size by
  `vram::TextureFootprintWords`, never by whole pages. Block granularity cut city3's walls
  from 2024 KB to 1391 KB.
- Mip levels share the base allocation, first-fit by block (`vram::MipLayout`, one per POT
  size). Each level has its own TBP/TBW via MIPTBP1, and PSMT8 TBW stays even. TEX1.MTBA
  auto-layout only handles square sizes.
- Bilinear needs every level ≥ 8 texels per side: `MXL = min(3, log2(min(w,h)) - 3)`.
- LOD: LCM=0 gives `LOD = log2(1/Q) + K`. With Q = 1/view depth, `K = -log2(f)`,
  `f = (screenH/2)·cot(fovY/2)` = 320 at 640x448, fov 90. LOD is depth-only, so grazing
  floors under-filter (`ps2_mip_bias` tunes it).
- Cvars: `ps2_mipmaps` (load-time, 0 = old behaviour exactly), `ps2_mip_filter`
  nearest/bilinear/trilinear (live), `ps2_mip_bias` (live).
- Lightmap atlases are `ImageType::Wall` (Alpha8). Exclude them by format when a rule means
  "walls".
- Before changing the Quake II reference's heap or residency policy, read that project's README
  VRAM heap notes: LRU, never evict textures bound this frame, recoverable OOM, defrag on level change.
