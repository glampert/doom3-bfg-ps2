# Retail UI host inventory and cooking

This records the **2D-5** host pipeline and its scoped native resource/timeline
profiles. Documentation reviewed **2026-10-23**, with runtime evidence through
**2026-10-23**. Original shell/dialog dictionaries, English text and checked native
timelines can load and execute through separate admission profiles. The original dialog
and the shell's start screen draw in full through a third one, their text from
[pre-rendered glyphs](#pre-rendered-glyphs), and the native shell handler drives the
[start and root screens](#native-shell-menu) from key events, with Common's quit
confirmation. The other menu screens are not admitted.

Retail artwork and fonts are converted to paletted, RLE-compressed **GSTX** textures by
the host tool `asset_reexport`; the target keeps each open image's pages resident instead
of streaming RGBA pages. The cook keeps `runtime_ready=false` because complete
drawing/resource closure and the active menu working set remain pending. Masks (convex
ones on the CPU, any other one at a time in the GS depth buffer), additive blend 8, screen
blend 4, outlines and RGB color terms are implemented; additive alpha terms and a second
simultaneous non-convex mask remain refused. See the [renderer roadmap](RENDERER_2D_PLAN.md) and
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
make ui-cook UI_PREVIEWS=build/ui-previews
make run-retail-ui
make CONSOLE_STANDARD=pal run-retail-ui
make test-ui
make BUILD=release test-ui

# Every visible package stages the same cook for the console charset:
make ui-cook && make console

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
package. `make ui-cook` builds `build/tools/asset_reexport` first; `UI_PREVIEWS` (under
`build/`) also writes an original and a converted PNG of every texture for visual
review.

`make retail-ui` stages the same production ELF under `build/<config>/ui-retail/`;
`run-retail-ui` displays the resource screen until quit. `UI_COOK_OUTPUT` selects the
source cook, and `CONSOLE_STANDARD` selects NTSC/PAL. The package contains only the
compact catalog, modern font files and GSTX textures from the local cook; staging
removes any other file from its asset directory. It does not copy retail SWFs,
material source text, legacy font blobs or the full language dictionary. Use a fresh
visible process.

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
complete font files are retained; only the converted font textures keep just the glyphs
English text can reach (see below). No theme changes or demo-mode substitutions are
made; the conversion policy halves every texture for the first pass.

## Runtime catalog

The cook additionally emits a content-addressed **UICT v4** runtime catalog, referenced
by `cooked.runtime_catalog`. Staging renames that checked index to `base/retail_ui.uic`;
the EE never parses the large diagnostic JSON. Its 64-byte little-endian header gives
exact array/string/descriptor offsets and an English-only flag. Records bind images
to the complete header and tables of their GSTX texture, fonts to immutable modern
`.dat` snapshots, three legacy width/height pairs and an optional
[glyph table](#pre-rendered-glyphs), aliases to canonical families, and material names
to images. The current index is **55,009 bytes**: fifteen images (21,696 descriptor
bytes, 19 pages; each glyph set's descriptor is 128 bytes), five fonts, two glyph
tables (29,152 bytes, 1,442 entries), nineteen aliases and twelve implicit material
bindings. Catalog bounds are 128 KiB, 32 images, eight fonts, 128 aliases, 64
materials, 64 KiB of descriptors and 256 total pages; a glyph table holds at most 32
sizes and 4,096 entries, over an image of at most 16 band placements. Native validation rejects
overlapping/inconsistent ranges, traversal or token-injection paths, duplicate
identities, invalid references and missing canonical self-aliases before publishing a
view; every descriptor must pass the same strict GSTX table check as the texture file.

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

## Converted textures (GSTX)

`--cook` describes every texture as a job for the C++ converter
(`src/tools/asset_reexport`, built on the shared `src/tools/assets` library) and runs it
in a private directory. `config/ui_retail.json` holds the conversion policy; its
`conversion_only` list adds the HUD, PDA, pause and loading movies, converted for review
and budgets but never admitted at runtime. Each texture is split into **placements**,
the logical texels one draw may own:

- A SWF atlas image owns its content rectangle and maps its 4-aligned block from id's
  packer (`WriteSwfImageAtlas`), which holds at least one clamped border texel per side.
- A font atlas keeps only the glyphs English text can reach: the dictionary, SWF literal
  text and printable ASCII (103 codepoints, 101 glyph rectangles per font). Each glyph
  owns its rectangle and maps it plus a one-texel ring, clamped to the atlas. Native
  text still draws a one-texel quad for the empty space and no-break space glyphs, so
  those own their whole ring (102–103 placements per font).
- Any other image is one placement, the console charset included.

The converter decodes BIMAGE DXT1/DXT5 level zero (green-alpha fonts become white RGB
with green as alpha), resamples each placement with premultiplied area filtering by the
policy `scale` (0.5: the first pass halves everything, fonts included;
`conversion.texture_policies` overrides policy fields per image, keyed by the requested
image, material or BIMAGE name), then quantizes:

- A placement gets its own palette first: variance-split boxes refined by weighted
  Lloyd iterations over a weighted color histogram. RGB weight is the squared maximum
  alpha of each texel's 3×3 neighborhood, so bilinear edges keep their color.
- It becomes **PSMT4** when its 16-color palette stays within RMSE 3.0 and maximum error
  48 (premultiplied 8-bit units), otherwise **PSMT8**. White-RGB images (fonts) share a
  single 16-level alpha ramp and are all PSMT4.
- Placements of one format then share palettes greedily while every member's estimated
  RMSE stays within its limit (or its own palette's error plus a small increase) and its
  maximum error within 48; members of a rebuilt shared palette that miss either limit
  are split out again.
- Palette alpha is snapped so the GS conversion `(a × 128 + 127) / 255` lands exactly
  on the intended level.
- Placements are packed (skyline) into pages of at most 512 × 512, widths in multiples
  of 128 and heights in multiples of 64 (PSMT8) or 128 (PSMT4); 8-bit pages come first.
  A page is stored RLE-compressed when that is smaller.

Floating-point contraction is off in the host build, so a cook is byte-identical on any
host; two consecutive full cooks (7.3 s each, conversion included) produce identical
manifests. Every output is re-read by the strict C++ and Python readers before
publication, and `report.json` keeps per-placement error statistics.

GSTX v1 is documented field by field in `src/tools/assets/gstx.h`. A 48-byte header
(magic `GSTX`, logical size, page/CLUT/placement counts, table and file bytes, FNV-1a32
of the header and tables) is followed by page records (size, PSM, encoding, offset,
encoded/decoded bytes, FNV-1a32 of the decoded indices), CLUT records (16 or 256 RGBA8
entries in GS CSM1 order, straight alpha) and placement records (match, source and
page rectangles, page, CLUT). Data blocks follow in table order at 16-byte boundaries
with zero padding, so the header and tables identify the whole file. RLE control byte
`n < 128` copies `n + 1` literals; `n ≥ 128` repeats the next byte `n − 125` times.
Limits are 16 pages, 128 CLUTs, 1,024 placements, 512-texel pages, 16,384-texel logical
extents and 16 MiB files. Placements are checked against their page, source and
logical rectangles, and their match rectangles may not overlap.

The local cook converts 16 textures from 9,095,516 bytes of source BIMAGE resources to
**762,608 bytes** of GSTX files (**1,980,032 bytes** decoded). The admitted twelve take
562,496 bytes on disc and 1,397,760 decoded:

| Texture | Logical | GSTX bytes | Decoded bytes | Pages | CLUTs | 8-bit / 4-bit placements | Worst RMSE / error |
| --- | --- | ---: | ---: | --- | ---: | --- | --- |
| Dialog atlas | 1152 × 596 | 59,568 | 198,784 | 512 × 320 T8, 512 × 128 T4 | 4 | 6 / 4 | 2.65 / 37 |
| Shell atlas | 1408 × 2320 | 309,552 | 849,472 | 3 × 512 × 512 T8, 384 × 256 T4 | 22 | 58 / 27 | 2.85 / 43 |
| Each cover (3) | 472 × 564 | 39,328–41,824 | 82,944 | 256 × 320 T8 | 1 | 1 / 0 | 1.56–3.11 / 28–50 |
| Each font (5) | 512 × 640 or 640 × 768 | 11,952–16,544 | 16,448 | 128 × 256 T4 | 1 | 0 / 102–103 | 4.89–5.79 / 11–12 |
| Each cursor (2) | 24 × 24 | 1,376 | 9,216 | 128 × 64 T8 | 1 | 1 / 0 | 0.41 / 1 |
| HUD (inspection only) | 512 × 420 | 17,904 | 52,096 | 128 × 192 T8, 384 × 128 T4 | 16 | 15 / 25 | 3.36 / 40 |
| Pause (inspection only) | 1152 × 1172 | 134,880 | 386,496 | 512 × 512 T8, 512 × 192 T8, 256 × 128 T4 | 16 | 48 / 12 | 3.11 / 37 |
| PDA (inspection only) | 640 × 656 | 46,992 | 135,424 | 256 × 384 T8, 256 × 256 T4 | 8 | 25 / 4 | 3.00 / 44 |
| Loading (inspection only) | 128 × 20 | 336 | 8,256 | 128 × 128 T4 | 1 | 0 / 1 | 0 / 0 |
| Console charset (full resolution) | 256 × 256 | 19,184 | 32,832 | 256 × 256 T4 | 1 | 0 / 1 | 4.55 / 11 |

Errors are premultiplied 8-bit units against the resampled source, so they measure
quantization, not the halving. Font errors come from the 16-level ramp; halving also
costs thin faces their solid stroke interior (Sarasori's converted alpha never exceeds
243, Arial Narrow's 253). Whether art or fonts need their full resolution back is the
visual pass's decision; `UI_PREVIEWS` writes the original and converted image of each
texture side by side for it.

On the target, each catalog image opens as a `PalettedImage`. It admits exactly the
file whose length and tables equal the catalog descriptor, then reads every CLUT and
page and checks its FNV-1a32 while loading, so a corrupt texture fails before the first
presentation. The CLUTs become one 64-texel-wide PSMCT32 texture of 16 × 16 tiles (a
16-entry CLUT uses its tile's first 8 × 2 texels), each a CSM1 CLUT at its own block;
the pages become PSMT8/PSMT4 textures in the texture owner. The owner keeps their EE
copy for re-upload after VRAM eviction and uploads on first use; nothing streams per
frame. A triangle selects its placement by its UV centroid through a lookup grid (at
most 64 × 64 cells of at least 32 texels), maps its UVs from source to page rectangle,
and samples with the GS region clamp on that rectangle and the placement's CLUT;
every TEX0 write reloads the CLUT. A triangle outside every placement, such as a glyph
English text cannot reach, fails its draw with the offending UVs logged.
`ps2_uiRetail status` prints `RETAIL textures images/pages/cluts/pixels/metadata/
triangles/uploads/evictions` for the open set. Within the texture owner's 2 MiB pixel
cap, the font screen holds 122,880 bytes of paletted texels, the drawn dialog 327,680
and shell, dialog, cover and fonts together 1,277,952, besides the console's 278,528.
The debug emulator gates accepted this path on 2026-10-21 in NTSC and PAL: the font
screen, the inactive and executing movies and the full dialog draw, with every
`RETAIL textures` counter equal to the cooked files and no eviction. The dialog draws a
frame in about 60 ms in debug PCSX2, where streamed RGBA pages took about 1.6 s; see the
[acceptance record](PORT_STATUS.md#paletted-retail-ui-textures-2026-10-21).

All inputs are inspected before publication. Cooking uses a private staging tree,
publishes checked immutable assets and replaces the manifest last. A failed inspection
keeps the prior manifest and referenced assets intact. Later publication I/O failure
may leave unreferenced immutable files; old assets are not automatically pruned.
Output symlinks and source/config aliases are refused. Use a tree without concurrent
external mutation. This is atomic manifest replacement, not a durable filesystem
transaction or an adversarial directory sandbox.

## Console charset

The native console draws `textures/bigchars`, a 256×256 atlas of sixteen 16×16-texel
cells per axis indexed by the character byte, white with an antialiased alpha. The profile
requests it as a material name; its retail definition in `gfx.mtr` is explicit
(`colored`, like `guiSolid`), so the catalog binds no material and the backend opens the
image by its catalog name (`generated/images/textures/bigchars#__0200.bimage`) when the
presentation starts, before the native console draws. `texture_policies` keeps it at
full resolution: a small console character maps one cell onto 8×16 virtual units, 2.5×
minified horizontally on the 512-wide screen, which the bold 3–4 texel strokes survive
under bilinear sampling; halving would have magnified its rows instead. It converts to
one PSMT4 page on the fonts' 16-level alpha ramp (32 KiB of VRAM) and replaces the
procedural 5×7 charset the console drew with before 2026-10-22, which survives only as
the core renderer test's fixture (`src/tests/smoketests/console_charset.*`), since that
image has no retail data. Every visible package therefore stages the catalog; see
[the GS notes](../.claude/rules/gs-renderer.md#doom-console-charset-2026-10-22).

## Pre-rendered glyphs

Menu text draws far below its atlas size. For a SWF edit text of font height H (in
1280 × 960 stage units), one texel of the 48-point atlas covers H / 120 GS pixels
horizontally in both standards and H × lines / 46,080 vertically (448 or 512 lines), so
the dialog's text (H = 21, 22 and 34) minifies the original atlas 3.5–5.7× horizontally. The PC
samples the fonts' mip chains with trilinear and anisotropic filtering; the GS path
binds one level, bilinear, so thin stems alias. Native `RenderEditText` also grows each
glyph quad by one screen unit but its UVs by one texel, stretching small glyphs, and
draws the outline as four black copies of the glyph offset diagonally by the stroke
weight in atlas texels. Together they closed letters (ACCEPT read ADDEPT) whether the
font page was RGBA or the halved PSMT4 conversion.

Fonts listed under `conversion.glyph_sets` in the profile draw such text from bitmaps
rendered at the exact on-screen size. Each entry names a font and its sizes: `scale`
is GS pixels per atlas texel (x, y) and `stroke_weight` the outline weight in atlas
texels, zero for none. The dialog and the shell's start screen are covered, each size in
NTSC and PAL. Sarasori has the dialog's heights 21 and 22 with weight 1.75 and 34 with
weight 2.0, plus the start screen's prompts and carousel labels at heights 10 and 13
without an outline: ten sizes. Arial Narrow has the start screen's small background text
at heights 2.5 and 3: four sizes. The runtime supplies the values: a retail font drawn at
a size its set lacks logs
`[D3BFG] UI glyph size font=NAME scale=X,Y stroke=W prerendered=no` once per size.

`asset_reexport` renders a glyph-set job (`src/tools/assets/glyph_set.*`) from the
font's full-resolution atlas, decoded from its BIMAGE, and its glyph records:

- Each pixel averages 8 × 8 samples of the atlas texels, which already hold the coverage
  of their squares, so this is the exact area filter. The glyph's own texels and the
  one-texel ring its native quad reaches count; atlas neighbors never leak in.
- Each size picks one of eight baseline phases (eighths of a pixel), and each glyph one
  of eight horizontal phases, whichever concentrates coverage most (largest sum of
  squared coverage), so stems land on whole pixels.
- The outline is baked as the four diagonal copies native draws, `1 − Π(1 − c)` over the
  coverage shifted by ±weight texels on each axis.
- Bitmaps are trimmed to their ink. A glyph and its outline share one rectangle, side
  by side in equal halves; a glyph without ink (the space and no-break space) gets none.
  The converter's page packer places the rectangles on PSMT4 pages sharing one 16-level
  alpha CLUT. The logical image stacks the pages, and each page is one band: a single
  placement mapping it 1:1 (match equals source, page rectangle of the same size). The
  texture therefore holds one placement per page, and each glyph records its own
  rectangle instead.

The Sarasori set is a 256 × 512 logical image, one band on one PSMT4 page: 51,712 bytes
on disc, 128 of them tables, and 64 KiB of VRAM. The Arial Narrow set is 128 × 128:
992 bytes and 8 KiB of VRAM. Each glyph's rectangle, outline flag and ink origin, its
left edge on its baseline in 1/16 pixel from the bitmap's corner, go to
`cooked.glyph_sets` and the catalog.

UICT4 places one glyph table per font with a set between the material records and the
descriptors; font record words 12 and 13 give its offset and length (zero without a
set). A table holds a 16-byte header (image index, size count, entry count, reserved),
20-byte size records (scale x/y and outline weight in 16.16 fixed point, first entry,
entry count) and 20-byte entries: codepoint; bitmap x, y, width and height in the image's
logical texels, all zero without ink; flags with bit 0 for an outline; a reserved zero;
ink origin x/y. Native validation requires exact lengths, contiguous entry runs,
codepoints strictly increasing within a size, a set image of 1–16 placements that each
map 1:1, every rectangle inside one of them, outline pairs of even width, outlines only
on outlined sizes, scales in (0, 16], weights up to 16 and origins within ±1,024 pixels.
Unknown flags and a nonzero reserved half fail.

A retail font's glyph set image opens when the font registers, like its atlas. A tagged
hook in `idSWF::RenderEditText` converts each glyph's scale to GS pixels per texel and
takes a size within 1% on each axis and 0.01 of the outline weight. It draws the outline
in the stroke color, then the glyph in the text color, each as one rectangle copied
texel for texel. The bitmap goes where its ink origin meets the native layout's glyph
edge (pen plus left bearing, on the baseline), rounded to whole GS pixels, so each glyph
lands within half a pixel of its native place at its sharpest phase. The rectangle
starts half a pixel up and left of that pixel, so every pixel samples a texel center
and bilinear filtering returns the stored values. Region clamp spans the whole band, so
that sampling alone keeps out the neighbors packed flush against a bitmap, as it already
kept the two halves of an outline pair apart.
Layout, alignment, colors and masks stay native. Drop shadows, rotated or sheared text,
and sizes or glyphs a set lacks keep the native quads.

Limits: only the dialog's and the start screen's sizes are cooked, so the shell's other
screens still draw the filtered quads and log their sizes as they are reached. A set
holds up to sixteen 512 × 512 pages, far more than the English glyphs of any listed size
need. Entries cost 20 catalog bytes per glyph and size: the 128 KiB catalog bound leaves
room for about 3,800 more, roughly 37 sizes of 103 glyphs.

## Removed authored page transport (UIPG)

Until 2026-10-29 a placeholder authored SWF/font and the renderer test streamed lossless
**UIPG v1** RGBA pages (256 × 256 with a one-pixel gutter) through a shared slot cache;
the retail cook moved to the resident paletted GSTX textures above first. The format,
its cache and the authored movie are gone; the surviving lessons on page splitting and
atlas UVs are in [gs-renderer.md](../.claude/rules/gs-renderer.md#native-atlas-pages).
The authored BSWF and font remain host-test stand-ins (`src/tests/data/ui/`).

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
34-point). Only the three label instances are drawn. At this slice's 2026-10-17
acceptance, two movie atlas metadata tables added 600 bytes to the five font tables and
all seven images shared six streamed RGBA page slots; neither movie atlas streamed
pixels in the text-only screen. Opening a converted texture now loads its pages,
so the movie atlases become resident while only the fonts draw. These are storage/text
measurements, not the active menu's working set.

The `inactive` step of `make test-ui` (and `BUILD=release`) covers both standards, four
120-frame phases, source-derived text widths and pixels, stable warm
reloads, full shutdown recovery at the session's end and the malformed-resource cases. Captures and all
retail files remain under ignored `build/`.

## Validation and remaining gates

`make test-host` runs the synthetic format/cook regressions alongside existing host
checks and sanitizer C++ harnesses; it needs no retail installation. Coverage includes
truncation and bounds, alias/lookup semantics, native font metrics, required labels,
DXT interpolation/swizzles, page seams/UVs, immutable
output and failed-cook preservation. The converter's library tests cover RLE, CLUT
order, index packing, palettes, resampling, packing, GSTX round trips and glyph sets
(exact area coverage, phases, outline pairs, determinism and rejected requests); the
target's GSTX admission is checked against converter output, refusing every corrupted
table byte, and its catalog reader against valid and mutated glyph tables; and the
Python reader, its fixture encoder and sampling bounds have their own tests. The cook tests run the real converter on synthetic containers. Standalone sanitizer geometry checks exercise
UV/screen clipping, reversed shared edges, color interpolation and invalid-input
preservation. Two real cooks are byte-identical, and every converted texture is re-read and checked
before publication. Previews stay under ignored `build/`.
Host tests do not establish EE parser safety, menu correctness or physical PS2 behavior.

The console step of `make test-ui` derives the input line's glyph probes from the cooked
charset, so it needs `make ui-cook` first.

The retail font gate (now the `fonts` step of `make test-ui`) passed debug/release NTSC/PAL: five native
fonts at three scales, seven 120-frame phases, preserved legacy layout metrics,
source-derived glyph probes, warm reloads and full shutdown recovery. Five fresh-process
invalid-resource cases pass in each build. See the [resource acceptance record](PORT_STATUS.md#native-english-retail-font-resources-2d-5-slice)
for matched archives, pixel/cache budgets and measured heap peaks. This gate does not
admit a retail BSWF or activate Shell.

The real shell contains 54 mask placements, 29 additive-color placements, blend modes
0/4/8 and fifteen stroked shapes. Dialog adds ten mask placements and blend modes 0/8.
Converted paletted textures and the native SWF/font binding provide the integration
foundation. Selected English font/image admission and legacy layout metrics now pass
through the opt-in native resource path. Inactive BSWF dictionaries and isolated
English text now have a bounded native path.
The scoped execution path below now adds native timeline and callback ownership for
initial states. Native blend mode 8 now has an alpha-weighted additive GS path for
the zero-additive-color profile. Mode 4, nonzero
additive color transforms, complex masks and strokes are still explicit feature gaps.
A bounded native convex-mask path now exists for simple geometry; it does not
admit the shell's holed/disconnected meshes, 185-point outline or stroked masks. Full
drawing is admitted only for a movie without outlines, screen blends or additive color,
which today means the dialog. Shell drawing, general dynamic resource closure and a
measured active-menu working set remain pending. This does not complete milestone
2D-5 or activate the complete retail menu.

Retail images keep their converted pages resident; the device's texture budget (2 MiB of owned texels and of texture VRAM)
remains the global cap, and the measured screens above do not establish the complete
menu's working set.

Offline native Shell startup follows those gates. It must explicitly gate intro/Mars
cinematics, sound, save-indicator startup and unavailable campaign/online/save/Classic
operations. The minimal owner below selects Doom 3 only. PC `pcBar` versus controller
layout remains a later input/presentation decision; the current execution proof keeps
the existing native PC platform value.

## Scoped retail timeline execution

The opt-in execution path uses the original shell/dialog bytes, native sprite hierarchy
and native AVM1 interpreter. It retains the resource-only rendering boundary: full
movie drawing, movie event routing and the complete Shell handler are separate admissions
(see the drawn movies and the [native shell menu](#native-shell-menu) below).
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
families reviewed for the selected retail movies (and the former authored demo) are admitted.
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

The `execution` step of `make test-ui` runs executable shell/dialog reloads in NTSC/PAL;
`run_retail_movies.py` runs the corrupt-resource and runtime-limit failures. Select
`BUILD=release` to verify checks with assertions off.
Archives retain native execution/owner counters, source-derived text probes, texture/heap
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
residency and masks/blends remained later gates; the native shell menu below now
drives PressStart → Root → Quit.

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
(mode 4) placements, color transforms with any nonzero additive term and those whose
alpha term is nonzero. Only an additive alpha term refuses drawing: outlines draw as GS
lines, screen blending in per-channel passes and RGB color terms in a fog-colored second
pass ([GS notes](../.claude/rules/gs-renderer.md#doom-swf-outlines-color-terms-screen-and-depth-masks-2026-10-23)).
Convex masks clip on the CPU at draw time; any other mask uses the GS depth buffer, one
at a time. The dialog draws with ten convex masks; the shell has 54 masks, 15 outlines,
five screen-blend and 29 additive placements. The target counters match the host
inventory for both movies.

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
The `dialog` step of `make test-ui` draws it; see [PORT_STATUS](PORT_STATUS.md#original-dialog-drawn-in-full-2d-6-third-slice)
for its acceptance record, the page-streaming cost of that first slice and memory. With
converted textures the dialog's pages stay resident, so the case now requires its
upload count to stay fixed from the first status line through both warm reloads.

## Full original shell drawing

`ps2_uiRetail draw shell` loads the original shell with execution and full drawing,
attached to the same minimal owner as `execute shell` (the five-slot carousel holding
only the Doom 3 cover, and the Credits screen's callback). Before its first frame the
loader gives the shell atlas three channel CLUT textures, since five placements screen
blend over colored art; fonts and the white image need none.

The owner does not run the native screens, so the frame is the shell's start screen in
its authored state: the DOOM 3 BFG Edition logo over the animated background, with
placeholder prompts (`PROMPT` beside the button icons) and carousel labels that the
native start screen would fill in. `ps2_uiRetail logo on|off` plays what
`idMenuHandler_Shell::ShowLogo` plays on leaving the start screen (the logo's `rollOn`,
the background's `rollOff`); that transition draws the logo's twelve outlined shapes and
its screen-blended glow. `ps2_uiRetail clock fixed` advances an executing movie 1/60 s
per Common frame from its activation time, so captures at a frame count repeat whatever
the frame time.

```sh
printf 'ps2_uiRetail clock fixed\nps2_uiRetail draw shell\n' >> build/debug/ui-retail-movies/console.txt
printf 'ps2_uiRetail logo on\n' >> build/debug/ui-retail-movies/console.txt
```

A debug PCSX2 frame takes 100–140 ms with one timeline step per frame (the fixed clock);
the real-time clock, which runs up to ten timeline steps per frame to keep up, takes about
330 ms. The EE builds about 1,070 triangles and a dozen masks per frame, plus the outlines
during the logo transition. The `shell` step of `make test-ui` draws it; see [PORT_STATUS](PORT_STATUS.md) for its acceptance record. The
start screen's text draws from pre-rendered glyphs; a later screen's sizes without them
draw as native quads, each size logged once.

## Native shell menu

`ps2_uiRetail menu` opens the menu the way `idCommonLocal::CreateMainMenu` does: the
game's `Shell_CreateMenu(false)`, `Shell_Show(true)` and `Shell_SyncWithSession()`,
after native `PerformGameSwitch`'s move to press-start. A package whose
`presentation.cfg` reads `ntsc menu` or `pal menu` (`make retail-menu`) opens it the
same way at startup, before the first frame and the startup commands, on real time. The real `idMenuHandler_Shell`
owns the movie, its widgets and its screens; the harness only advances and draws it,
on the fixed clock when `ps2_uiRetail clock fixed` came first. Each frame syncs the
handler with the session and runs its update before the timeline advances.

- **Screens.** The handler binds the start, root, campaign, settings and credits screens.
  The start screen's carousel holds the Doom 3 cover alone; the root screen lists
  Campaign, Settings, Credits and Quit (no development map list or multiplayer). Campaign
  offers New Game and Load Game (the offline session has no save game manager, so
  nothing can be continued) and Settings offers Controls, Game Options and System (the
  PS2 renderer reports no stereoscopic mode); their back returns to the root, and the
  screens below them fail by name.
- **Platform.** Handlers and movies report platform 1, the PS3 face-button layout: the
  command bar shows the PlayStation cross and circle with SELECT and BACK.
- **Input.** Common routes each event the console leaves to the menu,
  as `MenuEvent` does, inside the shell's execution scope. `ps2_uiRetail key NAME` posts a
  press and release through the event loop: keyboard `enter`, `escape`, arrows, or pad
  `cross`, `circle`, `dpad_*`. Accept on the start screen signs in the first local user and
  the next session sync shows the root screen; the arrows move its focus (the help line
  follows) and back returns to press-start, signing out. Campaign and Settings fail by
  name (their screens are not bound).
- **Quit.** The menu initializes Common's dialog manager, which opens the admitted dialog
  movie inactive beside the shell. The root's Quit shows the native confirmation over the
  menu; while shown, the dialog takes every event first. Its options take focus left to
  right (dpad or arrows) and accept selects the focused one: Cancel closes the dialog,
  Change Game returns to press-start and Accept quits the game through Common. The
  manager's update runs before the GS frame; the presentation advances and draws the
  dialog after the shell. Its `<JOY1>` prompt draws as the PS3 cross art.
- **Prompt art.** The profile admits the eight PS3 pad prompt images
  (`guis/assets/hud/controller/ps3/*`), each a small PSMT8 texture. Native text replaces
  button tags with them; a drawn movie loads all eight when admitted, before any frame.
- **Admission.** The movie is the admitted shell with execution and drawing, as for
  `draw shell`, plus key events, a fourth admission. Its native callbacks
  (`updateMenuDisplay`, `activateMenus`, `closeMenu`, `refreshCarousel`, `updateCredits`)
  are pinned by identity before execution starts. Events admit keyboard and pad keys only,
  and every native handler a key reaches must be a widget event of one of the handler's
  live widgets. Admission is per movie, so the dialog's runs alongside: execution, drawing
  and events, admitted while it is inactive. The choices each shown dialog installs are
  pinned by identity, and the movie calls them through copies in its button properties.
- **Text.** The root screen adds Sarasori at heights 18 and 10 with a 2.0 outline (its
  options and help line), and the Campaign and Settings screens their headings (0.35 per
  texel, 1.75 outline) and options (0.2, 2.0 outline): 18 sizes on two PSMT4 pages. With
  them, no `UI glyph size` line appears on any bound screen.
- **Border slivers.** A native fill may address the packer's one-texel clamped border
  around an atlas image, and the Campaign and Settings lists sit under a mask: a quad
  clipped to a sliver along that edge has its UV centroid on the border row, outside
  every placement. The paletted lookup then takes the nearest placement within one
  texel, whose region clamp samples that edge row anyway.

The `menu` step of `make test-ui` opens the menu three times: a 120-frame load and two 30-frame warm reloads, each captured on the start screen,
on the root screen 90 frames after accept, with Credits focused 30 frames after two downs
and on the start screen 90 frames after back. Keyboard keys drive the first load and pad
buttons the warm reloads. It requires the handler's screen, session, root focus,
platform, callbacks and event counts at each capture, no unrendered text size, the
shell's draw admission, textures and channel CLUTs, exact warm releases, full shutdown
and pixel-identical warm reload captures. The session's first step, `boot`, drives the
menu its `menu` package opened at startup with keys, with no load or menu command. The
`screens` step enters Campaign from the root, scrolls it, returns, enters Settings,
scrolls it and returns, capturing each state on the fixed clock. The last step, `quit`,
opens the quit confirmation from the root, cancels it, opens it again and accepts it,
which ends the session through Common. See
[PORT_STATUS](PORT_STATUS.md) for the acceptance records.

## Page format survey and the first conversion pass

A host survey on 2026-10-20 measured the format alternatives on the original atlases,
while the cook still kept every retail page lossless RGBA32 and the dialog streamed
about 51 pages per frame. One CLUT per atlas image with PSMT8 indices is near-lossless.
Halving images never drawn above about 1.25× their authored size is free at the
1280 × 960 stage's on-screen scale. English-only fonts in 16-level alpha display
identically. Together they bring each screen to 0.1–1.2 MB, which fits GS memory beside
the framebuffers. Results and method:
[PORT_STATUS](PORT_STATUS.md#memory-budget-investigation-ui-formats-and-the-retail-boot-2026-10-20)
and the [GS rule](../.claude/rules/gs-renderer.md#retail-ui-page-format-survey-2026-10-20).

On 2026-10-20 the user chose that direction as a first pass that accepts some quality
loss: halve all UI, including the PDA and pause menus; English-only fonts; palettes
shared aggressively; PSMT4 wherever it holds; RLE on disc. The cook above implements
it and halves the fonts too, as part of all UI. A later visual pass decides which textures need their resolution or
color depth back, using the previews; only the conversion policy changes for that.
