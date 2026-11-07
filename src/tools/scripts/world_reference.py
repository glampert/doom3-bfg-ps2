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


def rasterize(triangles: list, eye: tuple, target: tuple, width: int, height: int) -> tuple:
    """The view's pixels (rows of RGB tuples, black where nothing draws) and, per pixel, the index
    of the triangle that won the depth test (None where nothing draws)."""
    forward = _normalize(_sub(eye, target))  # the camera looks down -forward
    right = _normalize(_cross(UP, forward))
    up = _cross(forward, right)
    # tan(fov_x / 2) = 4/3 x tan(fov_y / 2). The tables were made with this rounding order: another one
    # moves pixels on triangle seams and can reorder tied picks.
    tan_y = math.tan(math.radians(FOV_Y_DEGREES / 2))
    focal_y = (height / 2) / tan_y
    focal_x = (width / 2) / (tan_y * DISPLAY_ASPECT[0] / DISPLAY_ASPECT[1])
    image = [[(0, 0, 0)] * width for _ in range(height)]
    depth = [[math.inf] * width for _ in range(height)]
    owner = [[None] * width for _ in range(height)]
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
        if len(clipped) < 3:
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
                    z = 1.0 / (w0 / z0 + w1 / z1 + w2 / z2)
                    if z < depth[y][x]:
                        depth[y][x] = z
                        owner[y][x] = index
                        image[y][x] = tuple(int(round(w0 * c0[q] + w1 * c1[q] + w2 * c2[q])) for q in range(3))
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


def compare(capture: Path, view: str, bits: int, images: Path | None) -> float:
    """The share of the capture's pixels within the profile's tolerance of the reference image. A
    half-height capture is a FRAME buffer: its row y is the reference's row 2y."""
    width, height, pixels = renderer_capture.read_tga(capture)
    lines = {lines // 2: lines for lines in renderer_capture.VISIBLE_LINES.values()}.get(height, height)
    if lines not in renderer_capture.VISIBLE_LINES.values() or width not in renderer_capture.RESOLUTION_WIDTHS:
        raise ValueError(f"{capture} is {width}x{height}, not a world display mode")
    eye, target = reference_views()[view]
    triangles = load_triangles(PROC_PATH.read_text(), json.loads(JOB_PATH.read_text()))
    image, _ = rasterize(triangles, eye, target, width, lines)
    step = lines // height
    slack = renderer_capture.tolerance(bits)
    mismatches = 0
    difference = [[(0, 0, 0)] * width for _ in range(height)]
    for y in range(height):
        for x in range(width):
            offset = (y * width + x) * 3
            actual = tuple(pixels[offset:offset + 3])
            expected = renderer_capture.stored(image[y * step][x], bits)
            if max(abs(a - e) for a, e in zip(actual, expected)) > slack:
                mismatches += 1
                difference[y][x] = (255, 0, 255)
            else:
                difference[y][x] = tuple(c // 3 for c in actual)
    if images is not None:
        write_png(images / f"ref-{view}-{width}x{lines}.png", image)
        write_png(images / f"diff-{capture.stem}.png", difference)
    return 100.0 * (1.0 - mismatches / (width * height))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="fail when scene_pixels.json does not match a fresh run")
    parser.add_argument("--images", type=Path, help="write the reference images (and --compare's differences, mismatches in magenta) here as PNGs")
    parser.add_argument("--compare", type=Path, metavar="TGA", help="compare a whole world capture with its view's reference")
    parser.add_argument("--view", choices=sorted(reference_views()), help="the camera of the --compare capture")
    parser.add_argument("--bits", type=int, choices=renderer_capture.FRAME_BITS, default=32,
                        help="the --compare capture's frame-buffer bits (its capture line's bits=)")
    parser.add_argument("--min-match", type=float, default=99.0, help="the --compare share (percent) below which it fails")
    args = parser.parse_args()
    if args.images is not None:
        args.images.mkdir(parents=True, exist_ok=True)

    if args.compare is not None:
        if args.view is None:
            parser.error("--compare needs --view")
        share = compare(args.compare, args.view, args.bits, args.images)
        print(f"{args.compare.name} {args.view} {args.bits}-bit: {share:.2f}% within {renderer_capture.tolerance(args.bits)}")
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
