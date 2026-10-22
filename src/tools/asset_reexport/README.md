# Texture re-export

`asset_reexport` is a standalone C++20 host tool that converts Doom 3 BFG textures into
forms the PS2 GS samples directly. Its `ui` command turns the interface images a job
file lists into paletted **GSTX** textures: PSMT8/PSMT4 index pages, shared CLUTs, RLE
on disc, and placements that map each image's logical texels onto the pages. The format
is documented field by field in [`assets/gstx.h`](../assets/gstx.h); the retail UI cook
and its policy are described in the [retail pipeline guide](../../../docs/RETAIL_UI_PIPELINE.md#converted-textures-gstx).

The tool is built on the shared [`src/tools/assets`](../assets/) library (resource
containers, BIMAGE/DXT decoding, area resampling, palette building and sharing, GS index
and CLUT layouts, rectangle packing, RLE, GSTX, JSON and PNG), meant for every later
texture re-export as well. `resource_unpack` uses its container reader.

Build and run from the repository root:

```sh
make asset-reexport
build/tools/asset_reexport ui --data gamedata/d3_bfg --job job.json \
    --output build/reexport --previews build/reexport-previews
```

`make ui-cook` writes the job from `config/ui_retail.json`, runs the tool in a private
directory and publishes its output; run the tool directly to try a different policy.
Like `resource_unpack`, it needs no SDK or engine, and any host with CMake 3.24 and a
C++20 compiler, Windows/MSVC included, can build it from
[`src/tools/CMakeLists.txt`](../CMakeLists.txt).

## Job file

```json
{
  "version": 1,
  "containers": ["_common.resources", "_ordered.resources"],
  "policy": { "scale": 0.5, "alpha_levels": 16 },
  "textures": [
    {
      "name": "generated/images/generated/swf/dialog#__0200.bimage",
      "source": "generated/images/generated/swf/dialog#__0200.bimage",
      "output": "generated_swf_dialog.gstx",
      "width": 1152, "height": 596,
      "placements": [{ "match": [17, 9, 120, 40], "source": [16, 8, 124, 44] }]
    }
  ]
}
```

- `containers` lists the `.resources` files under `--data`, later entries winning; it
  defaults to the two shown.
- `policy` overrides library defaults for every texture; a texture's own `policy`
  overrides that again. Unknown fields are refused.
- `source` names a BIMAGE resource; level zero is decoded (DXT1, DXT5, green-alpha font
  swizzle). `width`/`height`, when given, must match it.
- `output` is a plain file name in `--output`.
- `placements` are optional: without them the whole image is one placement. A `match`
  rectangle owns the triangles whose UV centroid falls inside it, and matches may not
  overlap; `source` (containing `match`, inside the image) is what the placement maps
  onto its page, including the border bilinear filtering reads. A placement may give its
  own `scale`.

| Policy field | Default | Meaning |
| --- | --- | --- |
| `scale` | 0.5 | Destination over source size, 0.01–1; never enlarges. |
| `max_page_extent` | 512 | Largest page side, a multiple of 128. |
| `t4_max_rmse`, `t4_max_error` | 3.0, 48 | A placement takes 4-bit indices when its own 16-color palette stays within both. |
| `share_palettes` | true | Merge palettes between placements of one format. |
| `group8_max_rmse`, `group8_max_increase` | 2.5, 0.5 | A shared 256-color palette must keep each member within this RMSE, or within its own palette's RMSE plus the increase. |
| `group4_max_rmse`, `group4_max_increase` | 3.5, 0.75 | The same for 16-color palettes. |
| `group_max_error` | 48 | Largest error a shared palette may give any member. |
| `alpha_levels` | 16 | Levels of the shared alpha ramp for white-RGB (font) images, 2–16. |

Errors are measured in 8-bit units on premultiplied RGB plus alpha, against the
resampled source: RMSE, the maximum, and the share of texels more than 8 off. Integer
fields accept integers only.

## Glyph sets

A texture with `glyphs` renders a font's glyphs at exact on-screen sizes instead of
converting its atlas, so the target can copy them texel for texel (see the
[retail pipeline guide](../../../docs/RETAIL_UI_PIPELINE.md#pre-rendered-glyphs)):

```json
{
  "name": "newfonts/sarasori_rg/48.dat#glyphs",
  "source": "generated/images/newfonts/sarasori_rg/48#__0400.bimage",
  "output": "newfonts_sarasori_rg_ps2_glyphs.gstx",
  "glyphs": {
    "glyphs": [{ "codepoint": 65, "s": 12, "t": 4, "width": 30, "height": 35, "left": 1, "top": 35 }],
    "sizes": [{ "scale": [0.175, 0.2042], "stroke_weight": 1.75 }]
  }
}
```

- `glyphs` lists 1–1,024 glyphs of the 48-point font in codepoint order: the atlas
  rectangle, the left bearing and the height above the baseline, in atlas texels.
- `sizes` lists 1–16 sizes: GS pixels per atlas texel on each axis (at most 4) and the
  outline's stroke weight in atlas texels (0–16; zero renders no outline).
- The texture takes no `width`, `height` or `placements`; of the policy only
  `max_page_extent` applies.

Each pixel is area-filtered from the full-resolution atlas, at the sub-pixel phase that
keeps the glyph sharpest. Every glyph with ink becomes one placement mapping 1:1 onto its
page, its outline in the right half when the size has one, all on one 16-level alpha CLUT.
`assets/glyph_set.h` describes the results.

## Output

Each texture becomes one GSTX file, checked by re-reading it before it is written.
`report.json` records, per texture, its logical size, source/file/table/decoded/VRAM
bytes, conversion time, pages, CLUT sizes and every placement's rectangles, format and
errors; a glyph set adds `glyph_set`, each size's stroke weight with every glyph's
codepoint, placement (null without ink), outline flag and ink origin (its left edge on
the baseline, in 1/16 pixel from the bitmap's corner). `--previews` adds
`<stem>.original.png` (the source inside the match rectangles; not for a glyph set) and
`<stem>.converted.png` (the converted texture as the target samples it) for review.
Existing output files are replaced.

Floating-point contraction is off (`/fp:precise` under MSVC), so output is byte-identical
on every host. Keep converted retail textures and previews under ignored `build/`; do
not commit them.

`make test-host` runs the library's sanitizer tests (`build/tests/asset_tests`), checks
the target's GSTX admission against this tool's output (`build/tests/gstx_format_tests`)
and runs Python tests that invoke the tool on synthetic containers.
