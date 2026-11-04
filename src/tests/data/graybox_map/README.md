# Graybox map

The first level the PS2 world renderer draws (gate R1 of the
[rendering plan](../../../../docs/plans/RENDERING_ARCHITECTURE.md)): two 1024x768-unit
rooms, 256 units high, split by a wall at x = 0 with a 128x128 doorway, each with basic
shapes in it. It is authored, not retail content, and nothing here is hand-edited:
`src/tests/host/scripts/graybox_fixture.py` writes every file, and
`graybox_fixture.py --check` (also run by `make test-host`) fails when any is stale.

- **Room 0 (area 0, x < 0)**: a red crate, a blue octagonal pillar from floor to ceiling,
  a yellow pyramid, a green ramp and a floating white icosphere (smooth normals).
- **Room 1 (area 1, x > 0)**: four orange stairs against the far wall, a cyan 12-sided
  column (smooth sides, flat top) and a magenta box with a smaller box turned 45 degrees
  on top of it.
- Floors are a 64-unit checker (`floor_light` / `floor_dark`); walls and ceilings are
  64-unit grids, so seams between them share vertices.

`maps/graybox.map` holds the solids as `brushDef3` brushes (worldspawn only, for collision
later). `maps/graybox.proc` is what dmap would make of them, built by the generator since
no dmap runs here: faces in contact with another solid are cut away, polygons are split
into areas at x = 0, T-junctions on unshared edges are removed, and every triangle is
wound clockwise seen from its front, as `idPlane::FromPoints` expects. The one portal is
the doorway, its winding facing area 0 so the view flood can pass it from either side.
Eleven area nodes bound both rooms, so points outside them are solid.

`materials/graybox.mtr` declares the materials collision-only; the world cook draws them
from `graybox_world.json`, which gives each its base colour and the fixed light that shades
the vertex colours until lightmaps exist. `make world-cook` cooks the map into
`build/world/graybox.d3wd` and `graybox.report.json`.

The older `headless_map` fixture stays as it is for `make test-map`: its `.proc` is two floor
quads without walls, and its portal winding faces area 1 instead of area 0, so the view flood
would pass it from neither side (nothing there draws).
