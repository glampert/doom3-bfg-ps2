# Retail UI host inventory and cooking

This records the **2D-5** host pipeline and its scoped native resource/timeline
profiles. Documentation reviewed **2026-10-20**, with runtime evidence through
**2026-10-20**. Original shell/dialog dictionaries, English text and checked native
timelines can load and execute through separate admission profiles, and the original
dialog draws in full through a third one. The shell still shows isolated English
labels; its drawing, event routing and offline menu navigation are not admitted.

The cook keeps `runtime_ready=false` because complete drawing/resource closure and
the active menu working set remain pending. Convex masks and additive blend 8 are
implemented, while complex/stroked masks, screen blend 4 and nonzero additive color
transforms remain feature gaps. See the [renderer roadmap](RENDERER_2D_PLAN.md) and
[agent handover](AGENT_HANDOVER.md) for the next dialog/shell/menu gates.

## Commands and local data

Run from the repository root with Python 3.10 or later. No Python packages are needed.
The default installation is `gamedata/d3_bfg/`, containing `_common.resources`,
`_ordered.resources` and loose `strings/english.lang`. Initial localization and font
bring-up target **English only**. The selected labels, font coverage and native
integration gates in this profile use that assumption.

```sh
make ui-inventory
make ui-cook
make run-retail-ui
make CONSOLE_STANDARD=pal run-retail-ui
make test-retail-ui
make BUILD=release test-retail-ui

# Separate reports and override the local installation location:
make ui-inventory UI_DATA_ROOT=/path/to/bfg/base UI_COOK_OUTPUT=build/ui-inventory
make ui-cook UI_DATA_ROOT=/path/to/bfg/base UI_COOK_OUTPUT=build/ui-retail
```

`UI_RETAIL_CONFIG` selects the checked-in scope profile, normally
[`config/ui_retail.json`](../config/ui_retail.json). Both commands write
`manifest.json` in their output directory. Use separate directories to retain an
inventory report alongside a cooked manifest. Output must be a descendant of this
repository's ignored `build/` tree; retail files and generated assets stay local.
Neither command runs the engine, emulator or AVM1 scripts, or changes the production
package. `make ui` / `make test-ui` still use the authored native SWF.

`make retail-ui` stages the same production ELF under `build/<config>/ui-retail/`;
`run-retail-ui` displays the resource screen until quit. `UI_COOK_OUTPUT` selects the
source cook, and `CONSOLE_STANDARD` selects NTSC/PAL. The package contains only the
compact catalog, modern font files and UIPGs from the local cook. It does not copy
retail SWFs, material source text, legacy font blobs or the full language dictionary.
Use a fresh visible process: authored nearest-filter pages and the retail linear
profile are deliberately separate startup choices.

Append commands to the running package's `console.txt`, for example:

```sh
echo 'ps2_uiRetail font 3' >> build/debug/ui-retail/console.txt
echo 'ps2_uiRetail status' >> build/debug/ui-retail/console.txt
echo 'quit' >> build/debug/ui-retail/console.txt
```

Font indices are Arial Narrow (0), BankGothic (1), Chainlink (2), Microgramma (3)
and Sarasori (4). Each screen uses native `idFont` at 12/24/48-point scales. The
command's unload/reload retains checked font/image caches until full UI shutdown.

For an ordinary directory tree containing the original native files, use the separate
[C++ resource unpacker](../src/tools/resource_unpack/README.md):

```sh
make resource-unpack
build/tools/resource_unpack --output build/unpacked-bfg \
    gamedata/d3_bfg/_common.resources gamedata/d3_bfg/_ordered.resources
```

BFG containers store payload bytes directly. Extraction preserves those bytes;
decoding image compression is part of the UI cook, not resource extraction.

## What is inspected and retained

The scoped readers follow the checked-in native format implementations, with explicit
bounds and synthetic tests. This is static dependency evidence, not proof that every
possible dynamically generated name, callback or localized string has been found.

| Input | Inventory and cook policy |
| --- | --- |
| Resource containers | Validate mixed-endian tables, names, offsets, ranges and overlaps; record whole-container and per-resource SHA-256, duplicate copies and selected source. Later container/record wins after extraction-path normalization. |
| BSWF16 shell/dialog | Validate dictionary, shapes, indices, sprite timelines, placements and bounded AVM1 action structure. Report fonts, text identifiers, image references, paths, masks, transforms, blend modes and strokes. Retain original BSWF bytes; no script execution or rewriting. |
| Modern fonts | Validate native glyph/codepoint records and atlas bounds. Resolve native localization aliases and normalization. Preserve complete font data, including native duplicate-codepoint selection. |
| Legacy font metrics | Retain available old 12/24/48-point files separately. Native layout extrema can differ from the modern glyph maxima; do not recompute them from a glyph subset. |
| English localization | Validate dictionary syntax and required label/glyph coverage; retain the complete source dictionary. Manifest coverage contains hashes/codepoints, not translated text bodies. Missing required strings or glyphs refuse cooking. |
| Material declarations | Inspect stage/map references and dynamic expressions; preserve native canonical identities. Conflicting requested definitions or unresolved resource expressions refuse cooking. This is not semantic native shader validation. |
| BIMAGE v10 | Validate all mip/face records and payload sizes. Decode selected 2D DXT1/DXT5 level zero; green-alpha font swizzle produces white RGB with green as alpha. Unsupported decoding formats fail explicitly. |

The profile hashes five native source files describing menu/dialog contracts and
records thirteen shell paths. Twelve appear in static timeline expansion;
`menuStart/backing` is absent and is optional in native PressStart code. These hashes
and path observations support later integration review; they do not replace runtime
callback/state validation.

The local snapshot resolves two SWFs, five font families, twelve images and all
**31 required localization IDs**, using 128 material files. Font coverage includes
Arial Narrow, BankGothic, Chainlink, Microgramma and Sarasori. Full English data and
full selected fonts are retained until dynamic reachability and subsetting are proven.
No theme changes, downscaling or demo-mode substitutions are made.

## Page layout and ownership

The cook additionally emits a content-addressed **UICT v1** runtime catalog, referenced
by `cooked.runtime_catalog`. Staging renames that checked index to `base/retail_ui.uic`;
the EE never parses the large diagnostic JSON. Its 64-byte little-endian header gives
exact array/string/descriptor offsets and an English-only flag. Records bind images
to complete UIPG header/tables, fonts to immutable modern `.dat` snapshots and three
legacy width/height pairs, aliases to canonical families, and material names to images.
The current index is **5,236 bytes**: twelve images, five fonts, nineteen aliases and
twelve implicit material bindings. Catalog bounds are 128 KiB, 32 images, eight fonts,
128 aliases, 64 materials and 1,024 total pages. Native validation rejects overlapping/
inconsistent ranges, traversal or token-injection paths, duplicate identities, invalid
references and missing canonical self-aliases before publishing a view.

Each font snapshot is length/checksum checked and structurally validated before native
`idFont` allocation: at most 2,048 glyphs, exact idf42 layout, sorted Unicode scalars,
valid signed-byte ASCII lookup indices and checked atlas rectangles. Duplicate indices
remain intact; native lookup chooses the last duplicate. The inspected old 12/24/48
metrics are retained as bounded summaries; Sarasori uses the native modern-extrema
fallback. Checksums provide integrity, not authentication or unchecked parser admission.
Only implicit image-default materials enter this catalog. Native material creation
generates their known constant blend/map/linear/clamp/vertexColor stages; original
retail shader text is not loaded. Explicit unsupported definitions are reported as
omitted bindings, and cannot silently become supported target shaders.

The retail resource profile reserves **six linear/clamp slots (1,572,864 bytes)** and
one **262,144-byte** scratch page within the existing global texture budget. Five font
images add **384 bytes** of page records; other catalog images stay unopened until
requested. Glyphs touching atlas edges retain their original UVs: optional outer
half-texel clipping uses the existing replicated gutters, without shrinking geometry
or rescaling interior samples. Default authored page admission remains strictly 0..1.
Font resources, images and the immutable catalog remain warm across preview unloads;
full shutdown releases fonts, then images/cache, then the catalog and device.

`--cook` publishes immutable, content-addressed files under `assets/` and references
them from the manifest. Source SWF/font/language files retain their original bytes.
Decoded artwork uses raw RGBA8 pages of **256 × 256 pixels**, with a one-pixel gutter
and at most **254 × 254 source pixels** per interior. Interiors cover every source
texel exactly once. Internal gutters copy adjacent pixels from the original atlas;
only the full atlas boundary and unused padding clamp to the nearest outer edge.

Each page records its source rectangle, atlas origin, SHA-256 and affine map from
full-atlas normalized UV to page UV. Geometry must first be split/clipped to page
boundaries before applying that map. The current native SWF renderer binds its shared
atlas for bitmap fills; changing dictionary image names alone cannot implement paging.

The cook also writes one indexed **UIPG v1** file for each logical atlas. These
compact files let the native page-image provider seek directly to a page; the target
does not parse JSON or open hundreds of content-addressed page files during a draw.
All header and record words are unsigned 32-bit little-endian integers:

| Section | Layout |
| --- | --- |
| 32-byte header | ASCII `UIPG`, version `1`, logical atlas width/height, page extent `256`, gutter `1`, page count, reserved `0`. |
| 8-byte record per page | Absolute payload offset followed by FNV-1a32 of that page's raw RGBA8 bytes. Records and payloads are in row-major page order. |
| Payload | Exactly `256 × 256 × 4` bytes per page. The file must end immediately after its last payload. |

Native admission checks dimensions, inferred rows/columns, page counts, offsets and
exact file length before allocating its bounded metadata table. Atlas axes are at
most 16,384, logical base RGBA is at most 64 MiB, and page count is at most 1,024.
Each streamed page is checksum-checked before replacing a cache slot. FNV checks
catch changed/corrupt payloads; manifest SHA-256 records retain source/package
identity. This format is a bounded page transport, not retail SWF/font admission.

The current host package has **143 logical pages**, **12 UIPG files**,
**158 unique asset files** and **71,830,304 asset bytes**, excluding its diagnostic
manifest. This includes the **5,236-byte UICT** resource catalog, whose SHA-256 is
`f87cf2962e9c92f431fd125d26fcf3e0312976b686cc2f7ccfe5af9e137b6f4e`.
Original content-addressed RGBA pages and retained source assets remain available
alongside the UIPG files; the new transport therefore duplicates page payloads on
disk. The twelve decoded source level-zero images total **26,220,672 bytes**.
These are host storage totals, not simultaneous EE or GS residency.

The authored renderer scene accepted on **2026-10-16** reserves **two cache textures plus one scratch page**:
524,288 bytes of cache pixels and 262,144 scratch bytes, plus its small page-record
table. It streams into those existing slots without allocating during drawing,
uses linear/clamp sampling, and fences queued texture references before replacement.
Close page images before the device/texture owner shuts down. The CPU geometry helper
clips triangles against page interiors and the screen, preserving winding, RGBA and
shared edges. Constant UVs exactly on an internal page boundary have one page owner,
so alpha blending is not submitted twice.

That fixed cache exercises the authored scene; it does not establish an actual
retail working set. The host profile's eight-page / 2 MiB cache remains a proposal.
Measure page references, upload pressure, font/SWF metadata and load/transition peaks
before accepting a retail target budget; do not load the complete decoded package
into EE memory.

The **2026-10-17 authored binding implementation** connects cooked pages to native
`idImage`, SWF bitmap fills and `idFont` drawing, accepted in debug/release NTSC/PAL.
Images retain their logical atlas dimensions while the draw adapter splits their
triangles into page coordinates. Two authored images share one `PageCache`: four
nearest/clamp texture slots (**1,048,576 bytes**), one **262,144-byte** scratch page
and **56 bytes** of page records. Cache slots are keyed by image and page; drawing
allocates no new storage. The movie's **272 × 256** atlas places its transformed
bitmap across the x = 254 interior boundary. The ASCII font keeps its **512 × 128**
atlas and native half-texel glyph UV expansion.

BSWF/font/material admission still uses the compiled exact-byte authored profile.
Page admission checks the header and every table checksum against compiled authored
descriptors, then verifies streamed pixels before replacing a cache slot. Images and
cache contents remain warm across movie unload/reload; UI shutdown closes every image
before releasing the shared cache and device. This establishes authored English-only
movie binding; the separate retail resource path above now admits selected fonts and
their legacy metrics, while retail movie binding remains pending.

All inputs are inspected before publication. Cooking uses a private staging tree,
publishes checked immutable assets and replaces the manifest last. A failed inspection
keeps the prior manifest and referenced assets intact. Later publication I/O failure
may leave unreferenced immutable files; old assets are not automatically pruned.
Output symlinks and source/config aliases are refused. Use a tree without concurrent
external mutation. This is atomic manifest replacement, not a durable filesystem
transaction or an adversarial directory sandbox.

## Inactive retail dictionaries and English text

`make retail-movies` stages the original shell/dialog BSWF files and complete English
language source beside the same checked font/image catalog. `make run-retail-movies`
launches the production ELF with the shell text resource screen; select PAL with
`CONSOLE_STANDARD=pal`, or release with `BUILD=release`. The separate package is
`build/<config>/ui-retail-movies/`. Its controls are:

```sh
echo 'ps2_uiRetail movie dialog' >> build/debug/ui-retail-movies/console.txt
echo 'ps2_uiRetail movie shell' >> build/debug/ui-retail-movies/console.txt
echo 'ps2_uiRetail movie none' >> build/debug/ui-retail-movies/console.txt
echo 'ps2_uiRetail status' >> build/debug/ui-retail-movies/console.txt
echo 'quit' >> build/debug/ui-retail-movies/console.txt
```

`movie none` returns to the font screen. `unload` closes the active dictionary and
text instances; checked fonts, image metadata and English data remain warm until UI
shutdown. `reload` returns to the font screen; select `movie shell` or `dialog` again
for dictionary inspection. English is the only admitted locale.

This is explicitly **resource-only native SWF loading**, not a partial movie rendered
with missing features. Native `LoadBinary` reads original geometry, fonts, timelines and
text records from one immutable checked snapshot. Storage validation bounds file size
(1 MiB), dictionary (1,024), sprites (512), total commands (32,768), total vertices
(32,768), indices (65,536), font records (eight) and text font references before native
allocation. Unknown record kinds, unsupported gradient/morph/static-text records and
any `doInitActions` refuse admission. Command buffers remain opaque and inactive;
this does not validate placement or AVM1 execution semantics.

The resource constructor never runs first-frame placements/actions. Its full-movie
`Render`, `Activate`, `HandleEvent` and timeline advancement paths fail explicitly.
Only a separate native `RenderResourceText` entry draws isolated edit-text instances;
wrapping, alignment, margins, font size, source color and glyph sampling remain native.
Three English label IDs bind through the original dictionary. No original movie bytes
are rewritten and no retail mask, additive transform or special blend is silently
ignored. Execution and full drawing are later, separate admissions; see
[full original dialog drawing](#full-original-dialog-drawing).

Staging creates a fixed **64-byte UIMV v1** descriptor at `base/retail/movies.uim`.
All sixteen words are little endian: magic `UIMV`, version 1, length 64, English flag
1, movie count 2, language byte count/FNV1a32/entry count, then four words each for
shell and dialog (byte count, FNV1a32, dictionary count, preview edit-text index).
Resource paths are fixed `retail/{shell,dialog}.bswf` and `retail/english.lang`; the
profile contains no dynamic paths. Host staging rechecks source SHA-256 and the full
structural inventory. Target storage preflight still runs after length/checksum
validation; checksums do not replace parser checks or authenticate resource files.

The target admits at most 1 MiB and 16,384 English entries. It validates the native
one-pair-per-line grammar and bounded escapes before the legacy loader, and verifies
UTF-8 in release too. The local dictionary contains **9,097 entries / 563,876 bytes**.
Native `ClearDictionary` intentionally retains keys and allocator pools for language
reloads. The PS2 full-release operation invalidates borrowed strings/`idStrId` values
and frees keys, indices and pools after movies/fonts have shut down.

The local shell retains **618 dictionary records / 117 edit texts / 256 sprites /
15,600 commands**; dialog retains **56 / 5 / 26 / 280**. Their selected original text
prototypes are shell 121 (centered white, 36-point) and dialog 29 (left-aligned orange,
34-point). Only the three label instances are drawn. Two movie atlas metadata tables
add 600 bytes to the five font tables; all seven images share the same six linear
slots and scratch page. Neither movie bitmap atlas streams pixels in this text-only
screen. These are storage/text measurements, not the active menu's working set.

Run `make test-retail-movies` and `make BUILD=release test-retail-movies` for both
standards, four 120-frame phases, source-derived text widths and pixels, stable warm
reloads, full shutdown recovery and six malformed-resource cases. Captures and all
retail files remain under ignored `build/`.

## Validation and remaining gates

`make test-host` runs the synthetic format/cook regressions alongside existing host
checks and sanitizer C++ harnesses; it needs no retail installation. Coverage includes
truncation and bounds, alias/lookup semantics, native font metrics, required labels,
DXT interpolation/swizzles, page seams/UVs, UIPG headers/records/checksums, immutable
output and failed-cook preservation. Standalone sanitizer geometry checks exercise
UV/screen clipping, reversed shared edges, color interpolation and invalid-input
preservation. A passing sanitizer harness exercises the real shared cache with
`PS2_D3BFG_ASSERTS=0` and `1`, using stubbed GS submission. The existing renderer scene
extends this with authored paged triangles, cache eviction, reload/cleanup and failure
paths. The passing production `make test-ui` gate checks shared movie/font residency,
native page splits, three warm reload cycles and twelve invalid-resource cases,
including a deferred pixel checksum failure. Thirty pixel probes per capture pass;
four page loads stay warm with zero evictions. Target measurements and inspected
NTSC/PAL captures are in [PORT_STATUS](PORT_STATUS.md#native-swf-and-font-page-binding-2d-5-slice).
Two real cooks are byte-identical; all source assets and page hashes are checked.
Every decoded source image reconstructs exactly from its page interiors. The decoded
shell atlas and font alpha plane were inspected locally; previews stay ignored.
Host tests do not establish EE parser safety, menu correctness or physical PS2 behavior.

The separate `make test-retail-ui` gate now passes debug/release NTSC/PAL: five native
fonts at three scales, seven 120-frame phases, preserved legacy layout metrics,
source-derived glyph probes, warm reloads and full shutdown recovery. Five fresh-process
invalid-resource cases pass in each build. See the [resource acceptance record](PORT_STATUS.md#native-english-retail-font-resources-2d-5-slice)
for matched archives, pixel/cache budgets and measured heap peaks. This gate does not
admit a retail BSWF or activate Shell.

The real shell contains 54 mask placements, 29 additive-color placements, blend modes
0/4/8 and fifteen stroked shapes. Dialog adds ten mask placements and blend modes 0/8.
The shared native page cache and authored SWF/font binding provide the integration
foundation. Selected English font/image admission and legacy layout metrics now pass
through the opt-in native resource path. Inactive BSWF dictionaries and isolated English text now have a bounded native path.
The scoped execution path below now adds native timeline and callback ownership for
initial states. Native blend mode 8 now has an alpha-weighted additive GS path for
the zero-additive-color profile, exercised by the authored native SWF. Mode 4, nonzero
additive color transforms, complex masks and strokes are still explicit feature gaps.
A bounded native convex-mask path now exists for authored geometry; it does not
admit the shell's holed/disconnected meshes, 185-point outline or stroked masks. Full
drawing is admitted only for a movie without outlines, screen blends or additive color,
which today means the dialog. Shell drawing, general dynamic resource closure and a
measured active-menu working set remain pending. This does not complete milestone
2D-5 or activate the complete retail menu.

Standalone `PageImage` callers can still own a private window. Simultaneous native
SWF/font images use the shared cache so opening an atlas does not multiply pixel or
scratch residency. The device's texture budget remains a global cap; the authored
four-slot budget does not establish a retail working set.

Offline native Shell startup follows those gates. It must explicitly gate intro/Mars
cinematics, sound, save-indicator startup and unavailable campaign/online/save/Classic
operations. The minimal owner below selects Doom 3 only. PC `pcBar` versus controller
layout remains a later input/presentation decision; the current execution proof keeps
the existing native PC platform value.

## Scoped retail timeline execution

The opt-in execution path uses the original shell/dialog bytes, native sprite hierarchy
and native AVM1 interpreter. It retains the resource-only rendering boundary: full
movie drawing, movie event routing and the complete Shell handler remain unavailable.
English is the only locale. The inspection screen still draws the three isolated native
edit-text records, rather than pretending that an incomplete movie is fully rendered.

After staging with `make retail-movies` and launching `make run-retail-movies`, append
commands to the package's HostFs input file:

```sh
printf 'ps2_uiRetail execute shell\n' >> build/debug/ui-retail-movies/console.txt
printf 'ps2_uiRetail status\n' >> build/debug/ui-retail-movies/console.txt
printf 'ps2_uiRetail execute dialog\n' >> build/debug/ui-retail-movies/console.txt
printf 'ps2_uiRetail movie shell\n' >> build/debug/ui-retail-movies/console.txt
printf 'ps2_uiRetail unload\nquit\n' >> build/debug/ui-retail-movies/console.txt
```

`movie` preserves inactive inspection; `execute` requests an additional target-side
placement/action preflight before activation. That pass checks command operands,
dictionary references, display mutations, sprite cycles/depth, constant pools, function
headers/registers and branches to actual instruction boundaries. Only the 32 action
families observed across the selected retail movies and authored demo are admitted.
Unused interpreter APIs, including construction/inheritance, are not enabled by this
profile. Source checksums do not replace structural and runtime checks.

The shell receives a minimal owner built from the original carousel and Credits
implementations. The carousel retains five visible slots with **one Doom 3 option** and
an admitted Doom 3 cover material. The original native callbacks are installed before
frame one, pinned by their owner and checked by resolved function identity and signature.
They cannot be replaced by an alias to another native service. The Credits callback
uses the original screen's active-state gate; this slice does not enter a Credits page.
Widgets detach from their borrowed movie before its destruction. An unprovided callback
or resource demand fails explicitly and is logged before dispatch.

Timeline advancement happens once per Common frame, before GS frame submission. Native
movie rate and bounded catch-up are retained; synchronous redraws never execute scripts.
Each execution scope admits at most 65,536 instructions, 32,768 timeline commands,
4,096 script calls, 32 nested script calls and 64 nested traversal/property operations.
Operand stacks are capped at 4,096 entries and native argument lists at 16. Live bounds
include 2,048 sprites, 512 texts and 16 hierarchy levels; the original shell frame-one
placement graph contains 1,855 sprites, 485 texts and nine levels. Strings, objects,
functions and properties also retain bounded lifetime/work accounting between scopes.
Native seeks and widget callbacks reject destructive ancestor reentry against every
active script source and receiver, including outer callers hidden by a nested call.
Their bounded ancestor snapshots remain live until each call returns.

Run `make test-retail-timelines` for executable shell/dialog reloads in NTSC/PAL, or
`make test-retail-movies` for the inactive and executable gates plus corrupt-resource
and runtime-limit failures. Select `BUILD=release` to verify checks with assertions off.
The optional runner case `--case dialog-execution` isolates three dialog reloads.
Archives retain native execution/owner counters, source-derived text probes, page/heap
ledgers, capture comparisons and matched build evidence. This extends the existing
production acceptance mode and adds no native test fixture.

Loaded shell allocations vary with clock-driven animation frames. Active captures keep
the 16 MiB heap ceiling and live-instance limits; stable ownership is checked after
each movie/widget teardown, with exact warm release ledgers and full shutdown recovery.
The runner clears console history before the final unload so its queued echo identity
does not add a transient history string to that checkpoint. Shell reload images and
warm dialog reloads must match. The existing NTSC dialog cold-edge allowance is limited
to four pixels, channel delta at most 25, on the source-derived final-T bottom row;
all 32 source probes remain required.

These checks cover the selected initial states and owners. They do not establish
general AVM1 lifetime safety: copied text aliases and raw function/default-sprite
references retain upstream limitations. General dynamic path closure, active artwork
residency, masks/blends and PressStart → Root → Quit navigation remain later gates.

## Full original dialog drawing

`ps2_uiRetail dialog quit` loads the original dialog with execution and a third,
separate admission: full drawing through native `idSWF::Render`. With the retail-movies
package running:

```sh
printf 'ps2_uiRetail dialog quit\n' >> build/debug/ui-retail-movies/console.txt
printf 'ps2_uiRetail status\n' >> build/debug/ui-retail-movies/console.txt
printf 'ps2_uiRetail unload\nquit\n' >> build/debug/ui-retail-movies/console.txt
```

Drawing is admitted per movie from the target preflight, not by name. Storage
validation counts shape outline records; execution preflight counts screen-blend
(mode 4) placements and color transforms with any nonzero additive term. A movie is
drawable only when all three are zero, since the 2D backend has no stroke, screen or
additive-color path yet. Masks are admitted and each one still passes the bounded
convex CPU bridge at draw time. The dialog qualifies with ten masks; the shell's 15
outlines, five screen-blend and 29 additive placements keep it blocked. The target
counters match the host inventory for both movies.

The dialog owner mirrors `idCommonDialog::Init` (the `DIALOG_*` constants) and
`ShowDialog` for the root menu's quit request: `GDM_QUIT_GAME` as `DIALOG_DYNAMIC`, with
title/text keys from native `GetDialogMsg` and ACCEPT / CANCEL / CHANGE GAME options
from the English dictionary. Common's own dialog manager stays uninitialized. The three
choices are native callback objects that fail explicitly when called; dialog event
routing and Common's deferred clear belong to the offline menu gate. Retail `guiSolid`
and `_white` resolve to one constant vertex-colored stage over the owned white image,
which is how native GUI surfaces evaluate them.

The timeline still advances once per Common frame before the GS frame; `Render` only
draws, inside the SWF draw scope and an execution scope. The visible frame shows the
dialog alone over the dark clear color, without the isolated label screen.
`make test-retail-dialog` (also part of `make test-retail-movies`) runs the
`dialog-render` case; see [PORT_STATUS](PORT_STATUS.md#original-dialog-drawn-in-full-2d-6-third-slice)
for its acceptance record, page-streaming cost and memory.
