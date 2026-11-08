# Asset re-export

`asset_reexport` is a standalone C++20 host tool that converts Doom 3 BFG assets into
forms the PS2 consumes directly. Its `world` command cooks a map's render geometry into a
**D3WD** world file ([below](#world-cook)). Its `ui` command turns the interface images a job
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
directory and publishes its output; its `conversion.texture_policies` map per-image
`policy` overrides onto the job (the console charset keeps `scale` 1.0). Run the tool
directly to try a different policy.
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
keeps the glyph sharpest. The bitmaps of every glyph with ink, its outline in the right
half when the size has one, are packed onto PSMT4 pages on one 16-level alpha CLUT. The
logical image stacks the pages, and each page is one band: a single placement mapping it
1:1, so the texture holds one placement per page however many glyphs it has. Every glyph
records its own rectangle in the logical image instead. `assets/glyph_set.h` describes
the results.

## Output

Each texture becomes one GSTX file, checked by re-reading it before it is written.
`report.json` records, per texture, its logical size, source/file/table/decoded/VRAM
bytes, conversion time, pages, CLUT sizes and every placement's rectangles, format and
errors; a glyph set adds `glyph_set`, each size's stroke weight with every glyph's
codepoint, rectangle `[x, y, width, height]` in the logical image (null without ink),
outline flag and ink origin (its left edge on the baseline, in 1/16 pixel from the
bitmap's corner). `--previews` adds
`<stem>.original.png` (the source inside the match rectangles; not for a glyph set) and
`<stem>.converted.png` (the converted texture as the target samples it) for review.
Existing output files are replaced.

Floating-point contraction is off (`/fp:precise` under MSVC), so output is byte-identical
on every host. Keep converted retail textures and previews under ignored `build/`; do
not commit them.

`make test-host` runs the library's sanitizer tests (`build/tests/asset_tests`), checks
the target's GSTX admission against this tool's output (`build/tests/gstx_format_tests`)
and runs Python tests that invoke the tool on synthetic containers.

## World cook

```sh
make world-cook          # the authored graybox map into build/world/
make world-cook-retail   # mars_city1 from the retail installation (RETAIL_DATA_ROOT)
build/tools/asset_reexport world --data src/tests/data/graybox_map \
    --job src/tests/data/graybox_map/graybox_world.json --output build/world
build/tools/asset_reexport world --data gamedata/d3_bfg \
    --job config/world_mars_city1.json --output build/world
# the same map in plain strips, for world_strips.vcl and the R1 topology comparison
make world-cook-retail WORLD_TOPOLOGY=strips WORLD_COOK_OUTPUT=build/world-strips
```

`--topology hybrid|strips` replaces the job's `topology`.

`world` reads `<data>/<map>.proc`, the text render geometry dmap writes (`mapProcFile003`),
or, when the job lists `containers`, `generated/<map>.bproc` from those `.resources`
containers: the binary cache id's loader reads first, the form retail maps ship. Both are
parsed by [`assets/proc.h`](../assets/proc.h) as strictly as id's loader would need, the
binary one to its exact end. It writes `<name>.d3wd` and `<name>.report.json` to `--output`.
The format is documented field by field in [`assets/world.h`](../assets/world.h); the design
is section 6 of the [rendering plan](../../../docs/plans/RENDERING_ARCHITECTURE.md#6-scene-data-on-the-ps2).

The `.bproc` layout (`idRenderWorldLocal::InitFromMap`, `idRenderModelStatic::WriteBinaryModel`)
mixes byte orders: `WriteBig` values (counts, flags, indexes, half texture coordinates,
portal points, node planes) are big-endian, while string lengths, `WriteVec3`/`WriteFloat`
values and `idDrawVert` positions are little-endian. Every model entry, area or prelight
shadow volume, has the type `shadowmodel`, then its name, then the `BRM` binary model; the
name tells them apart. Retail area vertices carry garbage in `color`/`color2`, which the
reader skips with tangents and the precomputed shadow and silhouette data.

```json
{
  "version": 1,
  "map": "maps/graybox",
  "topology": "hybrid",
  "shading": { "light": [0.35, 0.25, 0.9], "ambient": 0.45 },
  "default_color": [0.5, 0.5, 0.5],
  "materials": { "textures/graybox/red": [0.8, 0.22, 0.18] }
}
```

- `map` is the path below `--data` without `.proc`; with `containers` (paths below `--data`,
  later ones winning), the name of the `.bproc` inside them without `generated/` and `.bproc`.
- `topology` is `hybrid` (unique vertices plus a strip index stream, the default, drawn by
  `world_lm.vcl`) or `strips` (vertices repeated in strip order, drawn by `world_strips.vcl`).
  Gate R1 measured both on mars_city1: the hybrid takes 93 VU1 cycles a triangle and 1.61 MB of
  packets, plain strips 122.5 cycles and 1.92 MB (`src/tools/scripts/vu_cost.py`), so the hybrid
  is the default. A world is one topology; both readers refuse a mix.
- Without a `lighting` section, each vertex colour is the material's base colour (`materials`,
  else `default_color`, each channel 0–2 with 1.0 written as the GS's 0x80) times
  `ambient + (1 - ambient) * max(0, N.L)` for the `shading` light direction.
  `material_tint` (0–1, default 0) varies the materials without a colour around the default:
  each channel scales by `1 + tint * (2u - 1)`, `u` a byte of the name's FNV-1a hash over 255,
  so a retail map's hundreds of materials read apart (`config/world_mars_city1.json` uses 0.35).
- `textures` (optional) also cooks the world's diffuse textures into `<name>.d3tx` and
  `<name>.textures.json` ([below](#world-textures)): `material_containers` (below `--data`) hold
  id's `materials/*.mtr`; `scale` (default 0.5), `min_extent` (64), `max_extent` (256) and
  `min_mip_extent` (8) set the policy.
- `lighting` (optional, needs `textures` and the map's containers) bakes the map's static lights
  ([below](#world-lighting)): `script` names the map script inside the containers (light classes
  read it); `light_scale` (r_lightScale, default 3), `luxel_size` (16 units a texel),
  `shadow_samples` (1 or 4), `chart_angle` (30 degrees), `ray_offset` (0.5 units), `min_exposure`
  (0.25), `flat_steps` (1) and `shadows` (true) set the bake.

### World textures

With a `textures` section the world command scans every `materials/*.mtr` in the material
containers (file-name order, the first definition of a name winning; `assets/material.cpp`
reads the stages, not id's whole grammar) and, for each material of the cooked world, takes its
first diffuse stage (`diffusemap X`, or a stage with `blend diffusemap` and `map X`). It reads the
BIMAGE id generated for that image (`generated/images/<name>#__0100.bimage`, the extension
stripped, image programs spelled as paths) from all the containers and decodes level 0; when
the stage alpha-tests, the coverage image (`#__1000`, DXT1 green-alpha) supplies the alpha,
otherwise alpha is opaque. Retail diffuse images are YCoCg DXT5, decoded as
`idColorSpace::ConvertCoCgSYToRGB` does; normal maps and RGB565 light images decode too.

Each image becomes a GSTX v2 file (`assets/gstx_v2.h`, plan 10.4): level 0 is the source times
`scale` rounded down to a power of two, at least `min(source, min_extent)` and at most
`max_extent` a side; every mip down to `min_mip_extent` is box-filtered from the source; one
palette serves every level, `PSMT4` when a 16-colour palette keeps RMSE within 3 and every
channel within 48 over all levels, else `PSMT8`. Materials sharing an image share the file.
`<name>.d3tx` (`assets/texture_pack.h`) holds, per D3WD material in its order, a texture index and
log2 of the material's UV units per world unit (its UV area over its world area across the
`.bproc`'s area surfaces, from which the target sets the mip LOD), bound to the world by the FNV-1a
of the material names, then the files 64-byte aligned.
`--previews DIR` writes each texture's decoded source and cooked level 0 as PNGs.

mars_city1 at the defaults: 258 textures for 261 of 287 materials (the other 26 have no
diffuse stage: shadow and stain overlays, the entity GUI; the 25 of them that are translucent are
written as not drawn textured), 186 `PSMT4` and 72 `PSMT8`, 35 alpha-tested, 1,976,832 bytes of
VRAM for the whole map; median RMSE 1.3 against each level's resampled source, at most 3.04.

### World lighting

With a `lighting` section the world command reads `<map>.map` from the containers
(`assets/map_entities.cpp`), parses each `light` entity as `idGameEdit::ParseSpawnArgsToRenderLight`
does and derives its projection and bounds as `R_DeriveLightData` does (`assets/light.cpp`). The
material scan keeps every stage's image, clamp mode, condition and colour registers, parsed with
id's expression grammar and its tables, and decides `lit` (an interaction stage), `castsShadow`
and `translucent` as `idMaterial` does. Each light falls in class A to F (plan 8.1):
- F: fog and blend lights.
- E: bound to a mover or joint, a texture matrix, a cinematic, a script that changes its shader,
  parms or radius, or an undeclared material.
- D: an expression reading `sound`.
- C: `start_off`, health, `broken`, `break` or levels, targeted by an entity, or named in the script.
- B: an expression reading time.
- A: the rest.

`<name>.lights.json` lists every light with its class, the rule that decided it, and the bake's
counts.

The class A lights bake into Light8 pages (`assets/lightmap.cpp`, plan 8.2 and 8.3):
- Each lit area surface splits into charts: edge-connected triangles within `chart_angle` of the
  first one's normal, projected onto it at `luxel_size`.
- Every texel bilinear filtering reads, plus one more around, takes the light at the nearest point
  of the chart's triangles: per stage colour x projection x falloff x N.L.
- Shadows come from `shadow_samples` rays toward the light origin through a BVH. It holds the area
  triangles whose materials cast shadows (alpha-tested ones whole, as id's shadow volumes do) and
  the `func_static` inline models placed at their entity. Only lights without `noshadows` cast them.
- Lights reach the areas flooded through portals their bounds overlap.
- Each chart vertex gets the chroma of the light around it, normalised to its brightest channel.
  It is multiplied by the chart's exposure E (its peak intensity over 255/128, at least
  `min_exposure`) into the vertex colour.
- Texels hold the intensity against that chroma over E (128 = 1.0).
- Flat or sub-texel charts collapse to a 3x3 block.
- Charts pack area by area by skyline into 256x256 pages, each a `PSMT8` GSTX v2 file whose CLUT
  alpha is the index. They go into the D3TX's page table, and every batch names its page.
- Alpha-tested surfaces take their light per vertex, without a page.
- `--previews DIR` also writes `lightmap-<page>.png`.

With `"static_models": true` the brush models of static `func_static` entities join the area
models before the bake (`assets/static_models.cpp`), so they draw, light, shadow and trace as area
geometry. Each model is placed at its entity's `origin` and `rotation` (or `angle`), and each
triangle goes to the area holding its centroid (else a corner, else the origin), appended to an
area surface of its material while that has room. An entity keeps its model when it is bound, hidden,
carries a GUI or a skin, is targeted by another entity or named in the map script. A translucent
surface whose material reads parms the entity sets (`_color`, `shaderParmN`: flares, glass, glow
decals) stays in the model too; opaque ones merge, since the world pass draws only their diffuse.
The report's `static_models` counts them. Models left out shadow from their entity in the bake.

mars_city1: A 410, B 13, C 31, D 2, E 27, F 3. The 410 A lights (284 cast shadows) cover 1,716 lit
surfaces (204 alpha-tested, lit per vertex) with 10,683 charts in 8 pages: 532,480 bytes of VRAM
and 1.83 M rays, in about 1.4 s single threaded. The output is byte-identical across runs. Of its 362
inline `func_static` models, 108 merge whole and 15 in part (3,625 triangles; 93 opaque surfaces read
their entity's parms in a glow stage); 189 are only flares, glass or glow decals, 35 carry GUIs,
13 are bound and 2 targeted. Its 311 mesh models (`.lwo`, `.ase`) wait for model import.

Every `_areaN` model becomes an area of surfaces (in `.proc` order) and each surface a
run of batches, each the VIF packet one VU1 input half receives: at most 88 unique
vertices and 306 qwords including its two header qwords and the index stream. Batches
follow a breadth-first walk over shared edges, so each is a compact piece of the surface,
and greedy strips (the longest of the start triangle's three rotations) order the
triangles. Positions are `s16` with a per-batch power-of-two step and an offset snapped
to it, so map-grid coordinates stay exact; UVs are 4.12 around a whole-number offset,
coarser only when a batch's range needs it. The CPU mirror keeps every source triangle on
`s16` positions per area. Portals and area nodes are copied unchanged; other models
(entity brush models) are counted in the report and skipped.

The report records the source (`<map>.proc` or `generated/<map>.bproc`), counts (areas,
surfaces, batches, strips, triangles), vertices transformed and emitted per triangle, packet
and file bytes, the largest position and UV quantization errors, each area's bounds and the
material names in file order (test packages declare them until the cook compiles material
passes). mars_city1: 56 areas, 1,791 surfaces, 287 materials, 50,332 triangles in 2,572
batches (2,262 lit across 8 pages), 3,055,840 bytes, position error at most 0.094 units. The file is re-read with the full validation
(`ReadD3wd`, packet VIFcodes included) before it is written. `build/tests/world_tests`
checks both parsers (the graybox written as id would cache it reads back as its text form;
every truncation of a small `.bproc` and targeted corruptions are refused), the strips, byte-exact round trips, corruption and, through an
independent VIF model, that every graybox batch unpacks inside one input half and draws
exactly its source triangles.
