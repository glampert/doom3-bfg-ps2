# Host SWF fixture data

These resources stand in for retail movies and fonts in the host tests of the retail UI
pipeline: the BSWF inspector and execution validator (`ui_swf_format_tests`,
`swf_execution_tests`, `test_ui_swf.py`), the inventory/cook (`test_ui_inventory.py`), the
catalog (`test_ui_retail_catalog.py`) and the retail movie runner (`test_retail_movies.py`).
No target image loads them. They contain no retail Doom assets: the glyphs come from the
core test's procedural ASCII charset, the rest is solid geometry and authored timeline
commands. The files are released under GPL-3.0-or-later.

They began as the placeholder native SWF that brought up the PS2 UI; that movie, its
material and its UIPG atlas pages were removed from production on 2026-10-29.

Regenerate the tracked outputs from the repository root:

```sh
python3 src/tests/host/scripts/swf_fixture.py
python3 src/tests/host/scripts/swf_fixture.py --check
python3 -m unittest discover -s src/tests/host/scripts -p test_swf_fixture.py
```

The generator writes these package-relative files beneath `base/`:

| Path | Purpose |
| --- | --- |
| `generated/swf/ps2_demo.bswf` | Native BSWF version 16, 640 × 480, six timeline frames per second. |
| `newfonts/ps2_ascii/48.dat` | Native 48-point font metrics for ASCII codepoints 32 through 126. |

`manifest.json` records the generated resource sizes, hashes and scene inventory. Edit
the generator rather than the generated files. `--check` reports missing or changed
outputs without overwriting them.

The movie has one main frame and 20 dictionary entries. It draws solid background and
panel shapes, an affine-transformed bitmap letter, seven read-only native text instances,
a focus marker and a child sprite with four color/alpha frames. Two additional child
sprites exercise eight convex clip-depth masks, with three active at peak. Its two authored AVM1
functions use native property access and arithmetic:

- `focusNext` toggles `focusIndex` between 0 and 1, moves `focusFrame` between y = 312 and
  y = 360, and writes `FOCUS 0` or `FOCUS 1` to the status text.
- `select` increments `selectCount`, copies `focusIndex` into `selected`, and writes
  `SELECTED 0` or `SELECTED 1` to the status text.

Six original lower-right PlaceObject3 records select native additive blend
mode 8. The lower-right patches check zero/half/full alpha, overlap and saturation;
the second bitmap A checks transparent holes and the same page seam under additive
blending. A seventh additive placement checks a masked strip in the upper panel.
These records reuse existing dictionary shapes and the original atlas.

The generator includes no loops, imports, sound actions, dynamic
calls, line draws, gradients, morphs or nonzero additive color transforms.
Mask probes cover transformed trapezoids, inherited/nested intersections, crossing clip
ranges, simultaneous expiry, alpha/additive strips, paged bitmap UVs and native text. Mode 8
is its only special blend; other modes are rejected by the generator. It is a synthetic
scene; it does not implement the retail BFG menu.

The BSWF format follows the original engine writer: most scalar fields are big endian,
native `idFile` string lengths are little endian, and PlaceObject2/3 and AVM1 command streams
retain their SWF bit and byte order. The source timestamp is -1 because there is no
separate raw `.swf` file. The font has a big endian header and 95 little endian, padded
10-byte glyph records followed by sorted 32-bit codepoints. Its 512 × 128 procedural
atlas leaves room for the native font renderer's half-texel UV expansion. BSWF image
records interleave width, atlas X offset, height and atlas Y offset. The transformed
bitmap retains its screen geometry while sampling the copied A at (246, 64).

Local retail data remains under the ignored repository-root `base/` folder. Do not copy
it into this fixture tree or replace these generated resources with retail binaries.
