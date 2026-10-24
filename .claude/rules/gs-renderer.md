# GS renderer facts from the Quake II PS2 reference

The Quake sections' frame flow, source paths, cvars, capacities and measured results describe
`quake2-ps2`. Reuse the GS hardware findings, then verify each design against Doom's
renderer before treating it as a rule for this port.

## Doom renderer interface boundary

The logical adapters in `src/ps2/renderer/` bind native `tr` / `renderSystem`, image,
shader, cinematic and vertex-cache interfaces. Default/headless construction creates
metadata without GS setup or pixel/frame arenas. Explicit visible-mode `renderer`/`ui`
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

## Authored page contract (UIPG)

The authored native SWF and renderer test stream UIPG pages; the retail cook used them
until the paletted textures below replaced them. The page cooker preserves source
pixels in 256×256 RGBA pages with a 254×254 interior and one-texel gutter. Internal gutters sample neighboring full-atlas pixels;
clamping each tile to its own edge changes bilinear filtering at seams. Clamp only
the original atlas's outer boundary and unused padding. Each page records a source
rectangle and affine global-normalized-UV to page-UV map; split/clip geometry at page
boundaries before applying it. Native SWF RenderShape uses atlasMaterial, atlasOffset,
image size and channelScale for bitmap fills, not dictionary.material. Dictionary
renaming alone cannot implement paging. `ui/page_image.*` now streams compact UIPG
images through fixed texture slots and calls `ui/page_geometry.*` for page/screen
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

- `ps2::renderer` owns a frame, the `gs` command buffer two tagged 64-byte-aligned
  command halves, and `gs` both PSMCT16 contexts and Z16 with depth writes masked. Default
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

Native `ui::Init` is separate from low-level `renderer` readiness. Only native activation
sets `R_IsInitialized`; `IsOpenGLRunning` remains false. Shutdown images/UI before
the caller-owned device, without replacing logical-world ownership. The image manager
creates only `_white`. The console charset is the catalog's `textures/bigchars`,
opened at presentation startup (see [the charset notes](#doom-console-charset-2026-10-22));
the core test image, which has no catalog, installs the authored fixture atlas from
`tests/smoketests/console_charset.*`: sixteen 16×16 cells per axis, 15×14 glyph coverage,
native cursor bytes 10/11, explicit fallback boxes, nearest/clamp sampling.

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
  otherwise only bounded `headless`/`ntsc`/`pal` content is accepted, the visible modes
  optionally followed by `menu`, which opens the native menu at the end of startup. Repackaging always
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
described under Authored page contract above; additive/convex-mask additions
follow this section. Keep the initial captures as historical acceptance evidence.

- Native SWF uses a bounded normal-blend scope around its existing GUI calls. The
  native GUI fragment shader multiplies texture/vertex RGBA, adds the color transform,
  then premultiplies RGB by alpha for ONE/ONE_MINUS_SRC_ALPHA blending. With the authored
  zero-additive transform, GS straight-alpha modulation produces the same intended
  equation. Do not premultiply the CPU vertex RGB a second time. Scoped native mode 8,
  masks, screen blending, additive color terms and outlines are supported as documented
  below; other special blends and additive alpha terms remain unsupported.
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
in a one-bit-alpha framebuffer. Screen blend 4, additive color terms and non-convex masks
now have their own paths ([below](#doom-swf-outlines-color-terms-screen-and-depth-masks-2026-10-23)).

## Doom native convex SWF masks

The GS has no native eight-bit stencil counter. The checked CPU bridge captures the
transformed fill triangles from native `RenderMask`; it never emits mask geometry or
clips a new mask against its parent. Reverse interior edges cancel, one closed
boundary remains, and all boundary points must lie inside every edge. This positive
triangle chain proves one convex coverage region rather than merely matching an area
or bounding box. Degenerate triangles contribute no coverage; an empty mask clips
everything. Concave, disconnected, holed, overlapping, nonconforming, stroked or larger
geometry goes to the GS depth mask instead
([below](#doom-swf-outlines-color-terms-screen-and-depth-masks-2026-10-23)). The convex
profile bounds each capture at 64 triangles and 16 simplified boundary points, with four
active masks and 64 total clip planes.

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
a 185-point outline, disconnected/holed meshes and two stroked masks. Its largest fill
vertex count is 198; the largest triangle count is 183. The dialog's ten rectangular
masks stay on this bridge; the shell's other masks use the depth mask. PCSX2 captures
establish bounded functional clipping, not hardware fill cost or frame-rate acceptance.

## Doom full retail dialog drawing

The original dialog draws through native `idSWF::Render` with the existing adapters:
bitmap fills via the shared page cache, `guiSolid` fills over the owned white image,
normal and scoped additive blends, convex masks and native glyph quads with text
strokes. No new GS path was needed. With RGBA32 pages its cost was page streaming: the
1152×596 dialog atlas spans 15 RGBA32 pages (5×3), and two fonts add theirs, while the
retail profile shared six linear slots. Drawing order interleaves images and glyphs, so
LRU missed about 51 pages per frame (about 890 triangles, 736 page hits). Each miss read
and checked 256 KiB from HostFs, about 31 ms in PCSX2, so a frame took about 1.6 s.

More slots cannot fix this. GS memory left after the NTSC/PAL framebuffers holds about
ten 256 KiB pages, below the dialog's working set of roughly 20. Shrinking the pages
(paletted CLUT formats, or downscaling art the 512-wide screen cannot resolve) is the
real fix; the shell atlas alone (60 of the profile's 143 pages) makes it mandatory.
These are PCSX2 HostFs costs, not hardware storage or GS fill measurements. The
paletted textures below implement that fix: about 750 triangles and 60 ms per debug
PCSX2 frame, with no file reads or uploads after the first frames.

## Doom paletted UI textures (GSTX)

Retail UI images now load as converted GSTX textures (`ui/paletted_image.*`,
`ui/gstx_format.*`; layout in `src/tools/assets/gstx.h`). The debug emulator gates accepted
them on 2026-10-21 (PORT_STATUS); native PCSX2 captures confirm the layouts below as far
as noted.

- **Pages** are PSMT8 or PSMT4 textures with no padding: widths in multiples of 128
  (TBW = width/64 must be even for both), heights in whole GS pages (64 rows for PSMT8,
  128 for PSMT4), at most 512×512. The index data is exactly the VRAM footprint and
  uploads as one `draw_texture_transfer` rectangle; the SDK sizes PSMT8/PSMT4 transfers
  by bits per pixel, matching the chain preflight. PSMT4 stores the even texel in the
  low nibble, as the converter packs it: font and dialog captures confirm it.
- **CLUTs** of one image share a 64-texel-wide PSMCT32 texture of 16×16 tiles, four per
  row. Tile k starts at block `(k / 8) × 32 + {0, 4, 16, 20, 8, 12, 24, 28}[k % 8]`: within
  a 64×32 PSMCT32 page the four blocks of each 16×16 tile are consecutive in the
  `[0 1; 2 3]` order a CSM1 load reads. A 16-entry CLUT uses its tile's first 8×2 texels;
  256-entry CLUTs are stored in CSM1 order (middle 8-entry blocks of every 32 swapped).
  The CLUT texture goes through the RGBA path, so its alpha gets the same
  `(a × 128 + 127) / 255` conversion; the converter snaps palette alpha so that lands
  exactly on the intended GS level. Captures confirm tiles 0–3 (blocks 0, 4, 16, 20;
  the dialog's two 256- and two 16-entry CLUTs); the second tile row and later CLUT
  pages are first drawn by the shell (22 CLUTs).
- Every textured primitive writes TEX0 with the CLUT block, CPSM PSMCT32, CSM1, CSA 0
  and **CLD 1**, so each write reloads the CLUT; the CLUT texture can move with
  residency. Per-triangle CLUT loads are a GS cost PCSX2 does not charge; a CBP-compare
  mode can replace them once residency is stable.
- A page and its CLUT texture bind together (`tex::BindPair`): neither may evict the
  other, and both addresses are final before either upload.
- `gs::Upload` checks only the texels' layout and address; draws add the CLUT block and
  clamp region. A page uploads before any draw names its CLUT, so applying the draw
  checks to uploads rejected the first paletted page on the target. Host harnesses
  stub GS submission and never run either check.
- A triangle picks its placement by the UV centroid, maps source to page texels and
  samples with region clamp (CLAMP mode 2) on the placement's page rectangle, so
  bilinear taps never reach a neighbor packed with another palette. When no placement
  holds the centroid, the nearest within one texel is taken: a fill may address the
  packer's one-texel clamped border, and a mask can clip such a quad to a sliver whose
  centroid sits on that border row (the Campaign screen's masked list did this on
  2026-10-24); the region clamp samples the image's edge there anyway. Native half-texel
  glyph expansion stays inside the converter's one-texel glyph ring.
- Native edit text draws a quad for **empty glyphs** too (`glyph.width + 1.0f`): a space
  or no-break space covers half a texel around its 0×0 glyph, and its triangle centroids
  fall a sixth of a texel either side. Their placements match the whole ring, and the
  lookup accepts centroids up to half a texel outside the image (BankGothic's space is at
  s=0). Skipping empty glyphs stopped the first dialog frame at its first space.
- Load checks the catalog's tables and every CLUT/page FNV before publication; nothing
  streams afterwards. The owner keeps the EE copy for re-upload after VRAM eviction.

## Doom console charset (2026-10-22)

The console draws the retail `textures/bigchars` (256×256, 16×16 cells, white with an
antialiased alpha, one DXT5 level) as a catalog GSTX image: one placement, one 256×256
PSMT4 page (32 KiB of VRAM), kept at full resolution by `texture_policies`.

- The retail material (`gfx.mtr`) is explicit (`colored`, like `guiSolid`), so the catalog
  binds no material for it; the backend finds the image by its catalog name
  (`generated/images/textures/bigchars#__0200.bimage`). The PS2 text path never reads
  `tr.charSetMaterial`: `DrawSmallChar`/`DrawBigChar` emit the charset image directly
  through the paged triangle path, so each glyph is two triangles whose UV centroid
  selects the single placement.
- A small character maps a 16×16-texel cell onto 8×16 virtual units: 6.4 × 14.9 GS px in
  NTSC, a 2.5× horizontal minification through one bilinear level. The charset's strokes
  are 3–4 texels wide, so a host simulation of the GS sampling found the result within
  2% (mean) of an exact area filter and never a quarter level apart; unlike the menu
  fonts, the console needs no pre-rendered sizes. Halving it (the global policy) would
  have magnified rows 1.9× instead.
- `test-console` derives its input-line probes from the cooked texture: pixel centers sit
  on integer coordinates and the quad is exact to 1/16 px, so half a texel of slack around
  each sample is enough; the eight-level channel tolerance covers rounding.

## Doom pre-rendered glyphs (2026-10-22)

Menu text draws from glyph bitmaps rendered at their exact on-screen size
(`ui/font_backend.cpp`; cook and catalog in
[RETAIL_UI_PIPELINE](../../docs/RETAIL_UI_PIPELINE.md#pre-rendered-glyphs)).

- **Texel-exact copies under bilinear filtering.** GS pixel centers sit at integer window
  coordinates and texel centers at +0.5 (GS manual 2.4.4, Figure 2-6). A rectangle that
  spans whole texels and starts half a pixel above and left of a whole pixel samples
  every texel at its center, where bilinear (`iu0 = int(u − 0.5)`) takes that texel alone.
  Vertices are 1/16-pixel fixed point, so the half pixel is exact. The glyph draw needs
  no nearest-filtering state; PCSX2 captures show crisp, unblurred glyphs in NTSC and PAL.
- **Why not filter the atlas.** The dialog's text maps one atlas texel to 0.175–0.283 GS
  pixels horizontally (font height / 120) and minifies the 48-point atlas 3.5–5.7× with
  one bilinear level, so thin stems alias; mipmaps with a trilinear per-glyph LOD
  simulated soft. Area-filtering the full atlas at the exact size, with a per-glyph
  sub-pixel phase, gave the sharpest result in simulation and on target.
- A glyph and its outline share one rectangle, in equal halves. Each half draws as its
  own rectangle. The set image has one 1:1 placement per page (a band); the UV centroid
  selects it and region clamp bounds the whole band, so only sampling at texel centers
  keeps out the other half and the neighbors packed flush against the bitmap. A
  per-glyph placement (2026-10-22) cost 32 bytes of descriptor per bitmap and capped a
  texture at 1,024; the catalog's glyph entries now carry the rectangles (2026-10-23).
- Cost: ten Sarasori sizes of 103 glyphs (six outlined) fit one 256×512 PSMT4 page (64 KiB
  of VRAM). Each glyph draws two rectangles (outline, then glyph) instead of the native
  five quads (four stroke copies and the glyph): the full dialog fell from about 750
  to about 334 triangles a frame.

## Doom SWF outlines, color terms, screen and depth masks (2026-10-23)

The shell's remaining draw features, each verified by the core renderer test's pass band
and the authored SWF's draw-feature sprite in NTSC and PAL captures:

- **Depth test enable.** The GS manual prohibits TEST.ZTE = 0 ("may cause a malfunction";
  use ZTE 1, ZTST ALWAYS and ZBUF.ZMSK 1). The UI packets used to write ZTE 0, which
  PCSX2 tolerates; every primitive now writes ZTE 1, and FillRect writes its TEST too.
- **Depth-buffer mask.** The Z16 buffer, otherwise unused, holds one SWF mask.
  `ClearFrame` writes depth 1 with the frame's clear; a mask writes depth 0 over its
  native fill triangles and outline lines with every FBMSK bit set and ZMSK 0; masked
  content draws at depth 0 with ZTST GEQUAL, which passes only where depth is 0; removal
  writes depth 1 over the pixel bounds the mask wrote. The GS has only GEQUAL/GREATER
  depth tests, so a counter (native stencil INCR/DECR with an EQUAL test) cannot be
  emulated: two non-convex masks at once are refused. Convex masks keep the exact CPU
  bridge and intersect with the depth mask. Coverage follows GS rasterization of the
  native triangles, as the PC stencil follows GL's.
- **Additive color terms.** Native GUI shading is `(texture × color + add) × alpha`. A
  second pass with PRIM.FGE and fog coefficient 0 (XYZF2) replaces the texture
  function's RGB by FOGCOL while keeping its alpha (At × Av), so ALPHA
  `(Cs − 0) × As + Cd` adds `add × alpha`; the two passes sum to the premultiplied
  result. A negative term subtracts with `(0 − Cs) × As + Cd`. The fog blend is
  `(F × C + (255 − F) × FOGCOL) >> 8`: at F = 0, FOGCOL = term + 1 lands exactly. When
  every vertex RGB is zero (a zero color multiply), one fog pass replaces both. Decode
  color2 as native does, `2 × byte − 255` per unit; 127 and 128 stay zero. Additive alpha
  terms have no path and are refused.
- **Screen (mode 4).** Native SWF asks for DST_COLOR/ONE with `GLS_BLENDOP_SUB`, but the
  shipped GL backend's `GL_State` never applies blend ops, so the PC result is
  `Cd + Cd × S` (S the premultiplied source), a brightening rather than Flash's screen.
  The GS computes it one channel per pass: ALPHA `(Cd − 0) × As + Cd` with FBMSK writing
  that channel, and As that channel of S. The vertex channel × alpha goes into the vertex
  alpha; the texel's comes from a channel CLUT (paletted images, derived from the file's
  CLUTs when a drawn movie's preflight counts screen placements) or needs no change for
  white texels (fonts, the white intrinsic). RGBA pages cannot route color into alpha
  and are refused. FBMSK bits follow the 32-bit pixel for PSMCT16 too (R 7:3, G 15:11,
  B 23:19, A 31); PCSX2 honors per-channel masks on the 16-bit frame.
- **Outlines.** Native line records are degenerate `(a, a, b)` triangles drawn in GL line
  mode, which rasterizes all three edges: each segment draws in both directions. The GS
  LINE primitive draws its start pixel but not its end, so drawing both directions
  covers the segment inclusively, and translucent outlines compound alpha as on PC.
  Lines are untextured: RGB is the pixel color itself and only alpha takes 0x80 = 1.0.
  Widths are ignored, as on PC (one pixel).
- **Empty records.** Five shell shapes hold empty fill records; native `AllocTris` of
  nothing draws nothing, and every SWF caller skips a null span.

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
