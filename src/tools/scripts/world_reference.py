#!/usr/bin/env python3
# ================================================================================================
# File: world_reference.py
# Brief: Rasterize the graybox from the world gates' cameras: reference pixels for the captures, whole-image comparisons.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""A software reference for what world_lm.vcl draws of the authored graybox: the same .proc
triangles, the cook job's vertex colour bytes (base colour times the fixed light, 0x80 as 128),
the cameras of run_world.VIEWS that the GS gate shares, a 90 degree vertical field of view on
the TV's 4:3 picture and the near plane at 4 units, depth-tested with perspective-correct depth
and colour interpolated linearly across the screen, as the GS does.

With no arguments it rasterizes every view at each frame size (512 and 640 wide, NTSC and PAL
lines), picks eight points a view deep inside flat faces and writes them to
src/tests/data/graybox_map/scene_pixels.json, which renderer_capture.py reads as
WORLD_SCENE_TABLES. The file records a digest of its inputs (the .proc, the cook job, the
cameras and this script); --check fails when it is stale, and make test-host checks the digest.

--compare matches a whole capture against the reference image of its view and reports the share
of pixels within the frame-buffer profile's tolerance. The point checks cannot see a missing
strip of geometry away from their eight points, and a whole-image comparison can: it found the
portal scissors collapsed onto the world origin (2026-11-07). --images writes the reference
images as PNGs for inspection."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys
import zlib

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[2]
sys.path.insert(0, str(SCRIPT_DIR))

import renderer_capture  # noqa: E402
import run_world  # noqa: E402

GRAYBOX_ROOT = REPO_ROOT / "src/tests/data/graybox_map"
PROC_PATH = GRAYBOX_ROOT / "maps/graybox.proc"
JOB_PATH = GRAYBOX_ROOT / "graybox_world.json"
OUTPUT_PATH = renderer_capture.WORLD_SCENE_FILE

FOV_Y_DEGREES = 90.0
DISPLAY_ASPECT = (4, 3)  # width, height
NEAR = 4.0
# The world pass's far plane (scene.cpp kFarPlane): world_lm.vcl skips a triangle with a corner past it.
FAR = 65536.0
UP = (0.0, 0.0, 1.0)

# Smooth-shaded materials (the icosphere, the column's sides) change colour from pixel to pixel, so
# no point is picked on them.
SMOOTH_MATERIALS = ("white", "cyan")
POINTS_PER_VIEW = 8
PICK_MARGIN = 6
MAX_RADIUS = 40


# ------------------------------------------------------------------------------------------------
# Inputs
# ------------------------------------------------------------------------------------------------

def reference_views() -> dict:
    """The cameras whose captures the reference pixels apply to (eye, target), by view name."""
    return {view[0]: (tuple(view[1][:3]), tuple(view[1][3:])) for view in run_world.VIEWS if view[6]}


def frame_sizes() -> list:
    """(width, standard, lines) for every table: each resolution width at each standard's lines."""
    return [(width, standard, lines) for width in renderer_capture.RESOLUTION_WIDTHS
            for standard, lines in renderer_capture.VISIBLE_LINES.items()]


def inputs_digest() -> str:
    """What the tables depend on: a change to any of these makes scene_pixels.json stale."""
    digest = hashlib.sha256()
    for path in (PROC_PATH, JOB_PATH, Path(__file__).resolve()):
        digest.update(path.read_bytes())
    digest.update(json.dumps({"views": reference_views(), "sizes": frame_sizes()}, sort_keys=True).encode())
    return digest.hexdigest()


def load_triangles(proc_text: str, job: dict) -> list:
    """Every area model triangle of the .proc as (material, ((position, rgb bytes) x 3))."""
    light = job["shading"]["light"]
    length = math.sqrt(sum(c * c for c in light))
    light = [c / length for c in light]
    ambient = job["shading"]["ambient"]
    tokens = re.findall(r'"[^"]*"|[{}()]|[^\s{}()]+', re.sub(r"//[^\n]*", "", proc_text))
    cursor = 1  # past the file identifier

    def take() -> str:
        nonlocal cursor
        cursor += 1
        return tokens[cursor - 1]

    triangles = []
    while cursor < len(tokens):
        if take() != "model":
            while take() != "}":
                pass
            continue
        take()  # {
        take()  # model name
        for _ in range(int(take())):
            take()  # {
            material = take().strip('"')
            vertex_count, index_count = int(take()), int(take())
            vertices = []
            for _ in range(vertex_count):
                take()  # (
                vertices.append([float(take()) for _ in range(8)])
                take()  # )
            indexes = [int(take()) for _ in range(index_count)]
            take()  # }
            base = job["materials"].get(material, job["default_color"])
            for first in range(0, index_count, 3):
                corners = []
                for index in indexes[first:first + 3]:
                    vertex = vertices[index]
                    normal = vertex[5:8]
                    shade = ambient + (1.0 - ambient) * max(0.0, sum(n * l for n, l in zip(normal, light)))
                    corners.append((vertex[:3], [min(255, max(0, round(c * shade * 128))) for c in base]))
                triangles.append((material.rsplit("/", 1)[-1], tuple(corners)))
        take()  # }
    return triangles


# ------------------------------------------------------------------------------------------------
# Retail maps (the binary .bproc in the map's .resources container)
# ------------------------------------------------------------------------------------------------

RETAIL_DATA = REPO_ROOT / "gamedata/d3_bfg"
# What the world view clears to: where no area draws, the capture shows this.
CLEAR_RGB = (16, 24, 40)


def _f32(value: float) -> float:
    """Round to a float32, as each operation of the cook's float arithmetic does."""
    return struct.unpack("<f", struct.pack("<f", value))[0]


def read_container_file(container: Path, name: str) -> bytes:
    """One record of a .resources container (big-endian header and table, little-endian name lengths)."""
    data = container.read_bytes()
    magic, table_offset, table_bytes = struct.unpack(">III", data[:12])
    if magic != 0xD000000D:
        raise ValueError(f"{container} is not a resource container")
    table = data[table_offset:table_offset + table_bytes]
    count, cursor, found = struct.unpack(">I", table[:4])[0], 4, None
    for _ in range(count):
        length = struct.unpack("<I", table[cursor:cursor + 4])[0]
        record = table[cursor + 4:cursor + 4 + length].decode("latin1").lower().replace("\\", "/")
        offset, size = struct.unpack(">II", table[cursor + 4 + length:cursor + 12 + length])
        cursor += 12 + length
        if record == name:
            found = data[offset:offset + size]  # later records win
    if found is None:
        raise ValueError(f"{container} holds no {name}")
    return found


class _Reader:
    """id's binary writers: WriteBig values big-endian, strings and WriteVec3/WriteFloat little-endian."""

    def __init__(self, data: bytes):
        self.data, self.cursor = data, 0

    def take(self, count: int) -> bytes:
        if self.cursor + count > len(self.data):
            raise ValueError("bproc ends early")
        self.cursor += count
        return self.data[self.cursor - count:self.cursor]

    def big(self) -> int:
        return struct.unpack(">i", self.take(4))[0]

    def string(self) -> str:
        return self.take(struct.unpack("<i", self.take(4))[0]).decode("latin1")


def bproc_areas(data: bytes) -> tuple:
    """The area models of a .bproc as {area: [(material, vertices, indices)]}, vertices as (position,
    normal bytes), and the portals as (area a, area b, points). The C++ reader (proc.cpp) is the
    strict one; this one only needs the same geometry."""
    reader = _Reader(data)
    if reader.take(4) != b"PRO\x01":
        raise ValueError("not a version 1 .bproc")
    entries = reader.big()
    reader.string()
    reader.take(8)
    areas, portals = {}, []
    for _ in range(entries):
        kind = reader.string().lower()
        if kind in ("model", "shadowmodel"):
            name = reader.string()
            if reader.take(4) != b"BRM\x6c":
                raise ValueError(f"model {name} is not a version 108 binary model")
            reader.take(8)
            surfaces = []
            for _ in range(reader.big()):
                reader.take(4)
                material = reader.string()
                if not reader.take(1)[0]:
                    continue
                reader.take(24 + 4 + 4)
                vertex_count, in_file = reader.big(), reader.big()
                vertices = []
                for _ in range(vertex_count if in_file > 0 else 0):
                    record = reader.take(32)
                    vertices.append((struct.unpack("<3f", record[:12]), record[16:19]))
                reader.take(16 * reader.big())
                index_count = reader.big()
                indices = struct.unpack(f">{index_count}H", reader.take(2 * index_count))
                if reader.big() > 0:
                    reader.take(2 * index_count)
                reader.take(4 * reader.big())
                reader.take(8 * reader.big())
                reader.take(8 * reader.big())
                if reader.take(1)[0]:
                    reader.take(16 * vertex_count)
                reader.take(12)
                surfaces.append((material, vertices, indices))
            reader.take(24 + 12)
            reader.string()
            reader.take(9)
            if name.startswith("_area"):
                areas[int(name[5:])] = surfaces
        elif kind == "interareaportals":
            reader.big()
            for _ in range(reader.big()):
                count, a, b = reader.big(), reader.big(), reader.big()
                points = [struct.unpack(">3f", reader.take(12)) for _ in range(count)]
                portals.append((a, b, points))
        elif kind == "nodes":
            reader.take(24 * reader.big())
        else:
            raise ValueError(f"unexpected .bproc entry {kind}")
    if reader.cursor != len(data):
        raise ValueError("bytes after the last .bproc entry")
    return areas, portals


def material_color(job: dict, material: str) -> list:
    """WorldPolicy's base colour (world.cpp MaterialColor) in the cook's float32 arithmetic."""
    if material in job.get("materials", {}):
        return [_f32(c) for c in job["materials"][material]]
    color = [_f32(c) for c in job.get("default_color", (0.5, 0.5, 0.5))]
    tint = _f32(job.get("material_tint", 0.0))
    if tint <= 0.0:
        return color
    digest = 2166136261
    for byte in material.encode("latin1"):
        digest = ((digest ^ byte) * 16777619) & 0xFFFFFFFF
    for channel in range(3):
        unit = _f32(((digest >> (8 * channel)) & 0xFF) / _f32(255.0))
        color[channel] = _f32(color[channel] * _f32(1.0 + _f32(tint * _f32(_f32(2.0 * unit) - 1.0))))
    return color


def load_retail_triangles(data: Path, job: dict, only: set | None = None) -> list:
    """The cooked map's area triangles from its .bproc, with the cook's vertex colour bytes; with
    'only', just those areas' triangles."""
    container = data / job["containers"][-1]
    areas, _ = bproc_areas(read_container_file(container, f"generated/{job['map']}.bproc"))
    light = [_f32(c) for c in job["shading"]["light"]]
    length = _f32(math.sqrt(_f32(_f32(_f32(light[0] * light[0]) + _f32(light[1] * light[1])) + _f32(light[2] * light[2]))))
    light = [_f32(c / length) for c in light]
    ambient = _f32(job["shading"]["ambient"])
    scale = _f32(2.0 / 255.0)
    triangles = []
    for area in sorted(areas):
        if only is not None and area not in only:
            continue
        for material, vertices, indices in areas[area]:
            base = material_color(job, material)
            colours = []
            for _, normal_bytes in vertices:
                normal = [_f32(_f32(b * scale) - 1.0) for b in normal_bytes]
                facing = _f32(_f32(_f32(normal[0] * light[0]) + _f32(normal[1] * light[1])) + _f32(normal[2] * light[2]))
                shade = _f32(ambient + _f32(_f32(1.0 - ambient) * max(0.0, facing)))
                # std::round: halves away from zero.
                colours.append([min(255, max(0, math.floor(_f32(_f32(c * shade) * 128.0) + 0.5))) for c in base])
            for first in range(0, len(indices), 3):
                corners = tuple((vertices[i][0], colours[i]) for i in indices[first:first + 3])
                triangles.append((material, corners))
    return triangles


def retail_portals(data: Path, job: dict) -> list:
    """The map's portals as (handle, area a, area b, centre): handles count from 1 in file order."""
    _, portals = bproc_areas(read_container_file(data / job["containers"][-1], f"generated/{job['map']}.bproc"))
    return [(index + 1, a, b, tuple(sum(p[k] for p in points) / len(points) for k in range(3)))
            for index, (a, b, points) in enumerate(portals)]


# ------------------------------------------------------------------------------------------------
# Rasterizer
# ------------------------------------------------------------------------------------------------

def _sub(a, b):
    return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]


def _cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def _normalize(a):
    length = math.sqrt(sum(c * c for c in a))
    return [c / length for c in a]


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


# Two depths the GS's 16-bit buffer cannot order: its depth is about 65535 x near / distance (math::
# PerspectiveProjection), so surfaces closer than this many levels apart may draw either way.
DEPTH_LEVELS = 2.0
DEPTH_TOLERANCE = DEPTH_LEVELS / (65535.0 * NEAR)


def rasterize(triangles: list, eye: tuple, target: tuple, width: int, height: int, background: tuple = (0, 0, 0),
              ties: list | None = None) -> tuple:
    """The view's pixels (rows of RGB tuples, 'background' where nothing draws) and, per pixel, the
    index of the triangle that won the depth test (None where nothing draws). With 'ties' (an empty
    list), it also receives per pixel the colours of every surface the 16-bit depth buffer could
    not tell from the nearest (DEPTH_TOLERANCE in 1 / distance), coplanar decals in particular."""
    forward = _normalize(_sub(eye, target))  # the camera looks down -forward
    right = _normalize(_cross(UP, forward))
    up = _cross(forward, right)
    # tan(fov_x / 2) = 4/3 x tan(fov_y / 2). The tables were made with this rounding order: another one
    # moves pixels on triangle seams and can reorder tied picks.
    tan_y = math.tan(math.radians(FOV_Y_DEGREES / 2))
    focal_y = (height / 2) / tan_y
    focal_x = (width / 2) / (tan_y * DISPLAY_ASPECT[0] / DISPLAY_ASPECT[1])
    image = [[background] * width for _ in range(height)]
    depth = [[math.inf] * width for _ in range(height)]
    owner = [[None] * width for _ in range(height)]
    # Per pixel, the (1 / distance, colour) of every fragment within the tolerance of the nearest.
    near_set = [[None] * width for _ in range(height)] if ties is not None else None
    for index, (_, corners) in enumerate(triangles):
        polygon = []
        for position, colour in corners:
            offset = _sub(position, eye)
            polygon.append(([_dot(offset, right), _dot(offset, up), -_dot(offset, forward)], colour))

        # Clip against the near plane (view depth >= NEAR), interpolating colour along the edges.
        clipped = []
        for k, (a, colour_a) in enumerate(polygon):
            b, colour_b = polygon[(k + 1) % len(polygon)]
            if a[2] >= NEAR:
                clipped.append((a, colour_a))
            if (a[2] >= NEAR) != (b[2] >= NEAR):
                t = (NEAR - a[2]) / (b[2] - a[2])
                clipped.append(([a[q] + (b[q] - a[q]) * t for q in range(3)],
                                [colour_a[q] + (colour_b[q] - colour_a[q]) * t for q in range(3)]))
        if len(clipped) < 3 or any(p[2] > FAR for p, _ in clipped):
            continue

        for k in range(1, len(clipped) - 1):
            screen = [(width / 2 + focal_x * p[0] / p[2], height / 2 - focal_y * p[1] / p[2], p[2], c)
                      for p, c in (clipped[0], clipped[k], clipped[k + 1])]
            (x0, y0, z0, c0), (x1, y1, z1, c1), (x2, y2, z2, c2) = screen
            area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
            if abs(area) < 1e-9:
                continue
            for y in range(max(0, int(min(y0, y1, y2))), min(height, int(max(y0, y1, y2)) + 2)):
                for x in range(max(0, int(min(x0, x1, x2))), min(width, int(max(x0, x1, x2)) + 2)):
                    w0 = ((x1 - x) * (y2 - y) - (x2 - x) * (y1 - y)) / area
                    w1 = ((x2 - x) * (y0 - y) - (x0 - x) * (y2 - y)) / area
                    w2 = 1.0 - w0 - w1
                    if w0 < 0 or w1 < 0 or w2 < 0:
                        continue
                    inverse = w0 / z0 + w1 / z1 + w2 / z2
                    z = 1.0 / inverse
                    colour = None
                    if z < depth[y][x]:
                        depth[y][x] = z
                        owner[y][x] = index
                        colour = tuple(int(round(w0 * c0[q] + w1 * c1[q] + w2 * c2[q])) for q in range(3))
                        image[y][x] = colour
                    if near_set is not None and inverse >= 1.0 / depth[y][x] - DEPTH_TOLERANCE:
                        if colour is None:
                            colour = tuple(int(round(w0 * c0[q] + w1 * c1[q] + w2 * c2[q])) for q in range(3))
                        front = 1.0 / depth[y][x]
                        kept = [entry for entry in near_set[y][x] or () if entry[0] >= front - DEPTH_TOLERANCE]
                        near_set[y][x] = kept + [(inverse, colour)]
    if ties is not None:
        ties[:] = [[tuple({c for _, c in cell}) if cell else () for cell in row] for row in near_set]
    return image, owner


# ------------------------------------------------------------------------------------------------
# Point picking
# ------------------------------------------------------------------------------------------------

def _row_runs(row: list) -> tuple:
    """For each pixel of a row, where its run of equal colours starts and ends."""
    starts = [0] * len(row)
    ends = [0] * len(row)
    for x in range(1, len(row)):
        starts[x] = starts[x - 1] if row[x] == row[x - 1] else x
    ends[-1] = len(row) - 1
    for x in range(len(row) - 2, -1, -1):
        ends[x] = ends[x + 1] if row[x] == row[x + 1] else x
    return starts, ends


def pick_points(image: list, owner: list, triangles: list) -> tuple:
    """Up to eight (x, y, rgb) points, one per (material, colour), each the even pixel with the
    widest square of its own colour around it (capped at MAX_RADIUS), widest first."""
    height, width = len(image), len(image[0])
    runs = [_row_runs(row) for row in image]
    best = {}
    for y in range(PICK_MARGIN, height - PICK_MARGIN, 2):
        for x in range(PICK_MARGIN, width - PICK_MARGIN, 2):
            index = owner[y][x]
            if index is None or triangles[index][0] in SMOOTH_MATERIALS:
                continue
            colour = image[y][x]

            def reach(row: int) -> int:
                # How far the centre's colour extends left and right of x along this row.
                if image[row][x] != colour:
                    return -1
                starts, ends = runs[row]
                return min(x - starts[x], ends[x] - x)

            # The widest square of radius 'clear' all in this colour; the radius recorded is the first
            # ring that fails (MAX_RADIUS at most).
            narrowest = reach(y)
            clear = 0
            while clear < MAX_RADIUS:
                ring = clear + 1
                if y - ring < 0 or y + ring >= height:
                    break
                narrowest = min(narrowest, reach(y - ring), reach(y + ring))
                if narrowest < ring:
                    break
                clear = ring
            radius = min(clear + 1, MAX_RADIUS)
            key = (triangles[index][0], colour)
            if radius > best.get(key, (0,))[0]:
                best[key] = (radius, x, y)
    ranked = sorted(best.items(), key=lambda item: -item[1][0])[:POINTS_PER_VIEW]
    return tuple((x, y, colour) for (_, colour), (_, x, y) in ranked)


# ------------------------------------------------------------------------------------------------
# Outputs
# ------------------------------------------------------------------------------------------------

def write_png(path: Path, image: list) -> None:
    height, width = len(image), len(image[0])
    raw = b"".join(b"\0" + bytes(c for pixel in row for c in pixel) for row in image)

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def format_tables(tables: dict, digest: str) -> str:
    """scene_pixels.json, one view's points to a line."""
    lines = ["{", '  "schema": 1,', '  "generator": "src/tools/scripts/world_reference.py",',
             f'  "inputs_sha256": "{digest}",', '  "tables": {']
    for w, (width, standards) in enumerate(tables.items()):
        lines.append(f'    "{width}": {{')
        for s, (standard, views) in enumerate(standards.items()):
            lines.append(f'      "{standard}": {{')
            for v, (view, points) in enumerate(views.items()):
                body = ", ".join(f"[{x}, {y}, [{r}, {g}, {b}]]" for x, y, (r, g, b) in points)
                lines.append(f'        "{view}": [{body}]' + ("," if v + 1 < len(views) else ""))
            lines.append("      }" + ("," if s + 1 < len(standards) else ""))
        lines.append("    }" + ("," if w + 1 < len(tables) else ""))
    lines += ["  }", "}"]
    return "\n".join(lines) + "\n"


def generate(images: Path | None) -> str:
    triangles = load_triangles(PROC_PATH.read_text(), json.loads(JOB_PATH.read_text()))
    tables = {}
    for width, standard, lines in frame_sizes():
        for view, (eye, target) in reference_views().items():
            image, owner = rasterize(triangles, eye, target, width, lines)
            tables.setdefault(width, {}).setdefault(standard, {})[view] = pick_points(image, owner, triangles)
            if images is not None:
                write_png(images / f"ref-{view}-{width}x{lines}.png", image)
            print(f"{view} {width}x{lines}", file=sys.stderr)
    return format_tables(tables, inputs_digest())


def scene_views(scene: str) -> dict:
    """A scene's named cameras (eye, target)."""
    if scene == "graybox":
        return reference_views()
    import run_world_retail  # the retail session's cameras

    return {view.name: (tuple(view.camera[:3]), tuple(view.camera[3:])) for view in run_world_retail.VIEWS}


def scene_triangles(scene: str, data: Path) -> tuple:
    """A scene's triangles and the colour where nothing draws."""
    if scene == "graybox":
        return load_triangles(PROC_PATH.read_text(), json.loads(JOB_PATH.read_text())), (0, 0, 0)
    job = json.loads((REPO_ROOT / f"config/world_{scene}.json").read_text())
    return load_retail_triangles(data, job), CLEAR_RGB


def compare_image(capture: Path, triangles: list, camera: tuple, bits: int, background: tuple,
                  images: Path | None = None, label: str = "view") -> float:
    """The share (percent) of the capture's pixels within the profile's tolerance of the reference
    image from 'camera' (eye, target), or of any colour the 16-bit depth buffer could have kept there
    instead. A half-height capture is a FRAME buffer: its row y is the reference's row 2y."""
    width, height, pixels = renderer_capture.read_tga(capture)
    lines = {lines // 2: lines for lines in renderer_capture.VISIBLE_LINES.values()}.get(height, height)
    if lines not in renderer_capture.VISIBLE_LINES.values() or width not in renderer_capture.RESOLUTION_WIDTHS:
        raise ValueError(f"{capture} is {width}x{height}, not a world display mode")
    ties = []
    image, _ = rasterize(triangles, camera[0], camera[1], width, lines, background, ties)
    step = lines // height
    slack = renderer_capture.tolerance(bits)
    mismatches = 0
    difference = [[(0, 0, 0)] * width for _ in range(height)]
    for y in range(height):
        for x in range(width):
            offset = (y * width + x) * 3
            actual = tuple(pixels[offset:offset + 3])
            # Any colour the depth buffer could have kept is a match.
            candidates = ties[y * step][x] or (image[y * step][x],)
            if all(max(abs(a - e) for a, e in zip(actual, renderer_capture.stored(c, bits))) > slack for c in candidates):
                mismatches += 1
                difference[y][x] = (255, 0, 255)
            else:
                difference[y][x] = tuple(c // 3 for c in actual)
    if images is not None:
        write_png(images / f"ref-{label}-{width}x{lines}.png", image)
        write_png(images / f"diff-{capture.stem}.png", difference)
    return 100.0 * (1.0 - mismatches / (width * height))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="fail when scene_pixels.json does not match a fresh run")
    parser.add_argument("--images", type=Path,
                        help="write the reference images (and --compare's differences, mismatches in magenta) here as PNGs")
    parser.add_argument("--compare", type=Path, metavar="TGA", help="compare a whole world capture with its view's reference")
    parser.add_argument("--scene", choices=("graybox", "mars_city1"), default="graybox",
                        help="the --compare capture's map: the authored graybox or a cooked retail map (config/world_<scene>.json)")
    parser.add_argument("--data", type=Path, default=RETAIL_DATA, help="the retail installation a retail scene reads")
    parser.add_argument("--view", help="the camera of the --compare capture, by name")
    parser.add_argument("--camera", type=float, nargs=6, metavar=("EX", "EY", "EZ", "TX", "TY", "TZ"),
                        help="the --compare capture's camera given directly, as ps2_world view takes it")
    parser.add_argument("--bits", type=int, choices=renderer_capture.FRAME_BITS, default=32,
                        help="the --compare capture's frame-buffer bits (its capture line's bits=)")
    parser.add_argument("--min-match", type=float, default=99.0, help="the --compare share (percent) below which it fails")
    args = parser.parse_args()
    if args.images is not None:
        args.images.mkdir(parents=True, exist_ok=True)

    if args.compare is not None:
        if (args.view is None) == (args.camera is None):
            parser.error("--compare needs --view or --camera")
        if args.camera is not None:
            camera, label = (tuple(args.camera[:3]), tuple(args.camera[3:])), args.compare.stem
        else:
            views = scene_views(args.scene)
            if args.view not in views:
                parser.error(f"--view must be one of {', '.join(sorted(views))}")
            camera, label = views[args.view], args.view
        triangles, background = scene_triangles(args.scene, args.data)
        share = compare_image(args.compare, triangles, camera, args.bits, background, args.images, label)
        print(f"{args.compare.name} {label} {args.bits}-bit: {share:.2f}% within {renderer_capture.tolerance(args.bits)}")
        return 0 if share >= args.min_match else 1

    text = generate(args.images)
    relative = OUTPUT_PATH.relative_to(REPO_ROOT)
    if args.check:
        if OUTPUT_PATH.read_text() != text:
            print(f"{relative} is stale: run src/tools/scripts/world_reference.py", file=sys.stderr)
            return 1
        print(f"{relative} is current")
        return 0
    OUTPUT_PATH.write_text(text)
    print(f"wrote {relative}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
