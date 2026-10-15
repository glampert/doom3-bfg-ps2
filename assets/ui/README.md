# Authored native UI data

These resources exercise the engine's native `idSWF`, AVM1 script interpreter and `idFont`
on the PS2 backend. They contain no retail Doom assets. The artwork comes from the port's
procedural ASCII glyphs, solid geometry and authored timeline commands; the files are
released under GPL-3.0-or-later.

Regenerate all tracked outputs from the repository root:

```sh
python3 src/tools/scripts/build_authored_ui.py
python3 src/tools/scripts/build_authored_ui.py --check
python3 -m unittest discover -s src/tests/host -p test_authored_ui.py
```

The generator writes the following package-relative files beneath `base/`:

| Path | Purpose |
| --- | --- |
| `generated/swf/ps2_demo.bswf` | Native BSWF version 16, 640 × 480, six timeline frames per second. |
| `newfonts/ps2_ascii/48.dat` | Native 48-point font metrics for ASCII codepoints 32 through 126. |
| `materials/ps2_ui.mtr` | Four materials referring to backend-owned white, console and padded font images. |

`manifest.json` records the generated resource sizes, hashes and scene inventory.
`src/ps2/ui/authored_ui_data.h` contains the same resource bytes as an explicit admission
profile. Edit the generator rather than either generated copy. `--check` reports missing
or changed outputs without overwriting them.

The movie has one main frame and 16 dictionary entries. It draws solid background and
panel shapes, an affine-transformed bitmap letter, six read-only native text instances,
a focus marker and a child sprite with four color/alpha frames. Its two authored AVM1
functions use native property access and arithmetic:

- `focusNext` toggles `focusIndex` between 0 and 1, moves `focusFrame` between y = 312 and
  y = 360, and writes `FOCUS 0` or `FOCUS 1` to the status text.
- `select` increments `selectCount`, copies `focusIndex` into `selected`, and writes
  `SELECTED 0` or `SELECTED 1` to the status text.

The production adapter binds Tab and Enter to these functions through the native SWF
shortcut table. Loading or reloading the movie resets focus to 0, selection to -1 and the
selection count to 0. The generator includes no loops, imports, sound actions, dynamic
calls, masks, line draws, gradients, morphs or special blend modes. This is an authored
bring-up scene; it does not implement the retail BFG menu.

The BSWF format follows the original engine writer: most scalar fields are big endian,
native `idFile` string lengths are little endian, and PlaceObject2/AVM1 command streams
retain their SWF bit and byte order. The source timestamp is -1 because there is no
separate raw `.swf` file. The font has a big endian header and 95 little endian, padded
10-byte glyph records followed by sorted 32-bit codepoints. Its 512 × 128 procedural
atlas leaves room for the native font renderer's half-texel UV expansion.

BSWF and font loading currently admit only these exact bounded resources. The backend
reads each file once, compares its bytes against the compiled profile and supplies an
owned, read-only memory snapshot to the legacy binary parser. Missing, changed or
unknown resources fail before unchecked allocation counts or strings are parsed.
DeclManager indexes material text at startup; the movie separately checks the current
material file, and every draw validates its native material/state profile. Material
parsing uses DeclManager's cached text, rather than the checked binary snapshots.
This admission path is specific to the authored profile; general conversion and
validation belong to the later asset pipeline. The snapshots are transient; the movie,
font cache and renderer own their parsed objects and image storage through shutdown.

Local retail data remains under the ignored repository-root `base/` folder. Do not copy
it into this authored tree or replace these generated resources with retail binaries.
