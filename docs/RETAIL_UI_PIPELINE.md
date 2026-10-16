# Retail UI host inventory and cooking

This is the host foundation for renderer milestone **2D-5**, verified on
**2026-10-16, Australia/Sydney**. It produces inspected asset candidates for native
offline PressStart, Root and Quit-dialog bring-up. The package explicitly records
`runtime_ready=false`: the native authored loader does not admit these retail files.
The [renderer roadmap](RENDERER_2D_PLAN.md) tracks remaining target gates.

## Commands and local data

Run from the repository root with Python 3.10 or later. No Python packages are needed.
The default installation is `gamedata/d3_bfg/`, containing `_common.resources`,
`_ordered.resources` and loose `strings/english.lang`.

```sh
make ui-inventory
make ui-cook

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

The local package has **143 logical pages**, **145 unique asset files** and
**34,336,948 asset bytes**, excluding the manifest. The twelve decoded source level-zero
images total **26,220,672 bytes**. These are host storage totals, not simultaneous EE
or GS residency. An eight-page / 2 MiB CPU pixel cache is only a proposal. Measure
page references, upload pressure, font/SWF metadata and load/transition peaks before
accepting a target budget; do not load the complete decoded package into EE memory.

All inputs are inspected before publication. Cooking uses a private staging tree,
publishes checked immutable assets and replaces the manifest last. A failed inspection
keeps the prior manifest and referenced assets intact. Later publication I/O failure
may leave unreferenced immutable files; old assets are not automatically pruned.
Output symlinks and source/config aliases are refused. Use a tree without concurrent
external mutation. This is atomic manifest replacement, not a durable filesystem
transaction or an adversarial directory sandbox.

## Validation and remaining gates

`make test-host` runs the synthetic format/cook regressions alongside existing host
checks and sanitizer C++ harnesses; it needs no retail installation. Coverage includes
truncation and bounds, alias/lookup semantics, native font metrics, required labels,
DXT interpolation/swizzles, page seams/UVs, immutable output and failed-cook preservation.
Two real cooks are byte-identical; all source assets and page hashes are checked.
Every decoded source image reconstructs exactly from its page interiors. The decoded
shell atlas and font alpha plane were inspected locally; previews stay ignored.
Host tests do not establish EE parser safety, menu correctness or physical PS2 behavior.

The real shell contains 54 mask placements, 29 additive-color placements, blend modes
0/4/8 and fifteen stroked shapes. Dialog adds ten mask placements and blend modes 0/8.
Native page binding/triangle splitting, bounded retail font/material/image admission,
mask and blend rendering, and measured active working sets remain required. Static
AVM1 evidence also needs runtime dependency/callback validation.

Offline native Shell startup follows those gates. It must explicitly gate intro/Mars
cinematics, sound, save-indicator startup and unavailable campaign/online/save/Classic
operations. PC `pcBar` versus controller layout and initial Classic cover behavior
remain product decisions. None is silently selected by this host profile.
