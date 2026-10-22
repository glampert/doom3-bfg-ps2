#!/usr/bin/env python3
# ================================================================================================
# File: console_capture.py
# Brief: Verify the native half-console, translucent background and edited input in GS captures,
# with the input line's glyph probes derived from the cooked retail console charset.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import hashlib
from pathlib import Path

from renderer_capture import PIXEL_TOLERANCE, read_tga
from retail_ui_capture import INK_MINIMUM, Atlas, blend, ink_interval, within


CLEAR = (16, 24, 40)
DARK = (4, 6, 10)
WHITE = (255, 255, 255)
CYAN = (0, 255, 255)
# Native console geometry in virtual 640x480 units: the input line sits two small rows above
# the half console's bottom edge, the prompt one cell in from the left safe margin.
INPUT_LINE = (40, 208)
INPUT_TEXT = "]echo pending"
CELL = (8, 16)
CHARSET_CELL = 16
# The half console, the uncovered clear color below it and its cyan bottom border.
FIXED_PIXELS = (
    ("half-console-background", 8, 200, DARK),
    ("uncovered-background", 8, 300, CLEAR),
    ("native-console-border", 16, 239, CYAN),
)
# The native quad lands within 1/16 pixel of its projection and pixel centers sit on integer
# coordinates (the texel-exact glyph captures confirmed both), so a probe only needs half a
# charset texel of slack around its sample; the eight-level channel tolerance covers rounding.
PROBE_SLACK = (0.5, 0.5)


def charset_image(manifest: dict):
    """The original console charset the cook converts for textures/bigchars, when the profile requests it."""
    names = {entry["selected"] for entry in manifest["image_resolutions"]
             if entry["logical_image"] == "textures/bigchars" and entry["status"] == "resolved"}
    if len(names) > 1:
        raise ValueError("retail UI console charset binding is ambiguous")
    return names.pop() if names else None


def charset_atlas(manifest: dict, source: Path) -> Atlas:
    name = charset_image(manifest)
    if name is None:
        raise ValueError("the cooked retail UI has no console charset (textures/bigchars)")
    atlas = Atlas(source / manifest["cooked"]["runtime_images"][name]["path"])
    if (atlas.width, atlas.height) != (16 * CHARSET_CELL, 16 * CHARSET_CELL):
        raise ValueError("the cooked console charset is not a 256x256 cell grid")
    return atlas


def cell_pixels(index: int, standard: str):
    """The GS pixels one input-line cell covers and each pixel's charset texel coordinates.

    The native quad maps the cell's 16x16 texels onto 8x16 virtual units, 6.4 x 14.9 (NTSC)
    or 6.4 x 17.1 (PAL) pixels. Pixel centers sit on integer coordinates; the texture
    coordinate interpolates linearly across the quad. Edge pixels are left out: a pixel on the
    quad's boundary belongs to whichever cell the fixed-point rounding favors."""
    lines = 448 if standard == "ntsc" else 512
    x0 = (INPUT_LINE[0] + index * CELL[0]) * 512 / 640
    x1 = x0 + CELL[0] * 512 / 640
    y0 = INPUT_LINE[1] * lines / 480
    y1 = y0 + CELL[1] * lines / 480
    pixels = []
    for row in range(int(y0) + 1, int(y1) - 1):
        for column in range(int(x0) + 1, int(x1) - 1):
            if not (x0 <= column < x1 and y0 <= row < y1):
                continue
            pixels.append((column, row, (column - x0) / (x1 - x0) * CHARSET_CELL, (row - y0) / (y1 - y0) * CHARSET_CELL))
    return pixels


def glyph_probes(atlas: Atlas, index: int, character: str, color, standard: str) -> list[dict]:
    """One strongest ink probe and one hole probe inside a character cell, when the charset has them.

    A probe accepts any color the converted charset can produce within the raster slack of its
    projection, so it bounds the target's bilinear rounding instead of a single predicted value."""
    code = ord(character)
    cell_x, cell_y = (code & 15) * CHARSET_CELL, (code >> 4) * CHARSET_CELL
    candidates = {"ink": [], "hole": []}
    for column, row, u, v in cell_pixels(index, standard):
        texel_u, texel_v = cell_x + u, cell_y + v
        bounds = atlas.texture.alpha_bounds(texel_u - PROBE_SLACK[0], texel_v - PROBE_SLACK[1],
                                            texel_u + PROBE_SLACK[0], texel_v + PROBE_SLACK[1])
        if bounds is None:
            continue
        sampled = atlas.texture.sample(texel_u, texel_v)
        expected = blend(color, (0 if sampled is None else sampled[3]) / 255, DARK)
        if bounds[1] == 0:
            candidates["hole"].append((0, row, column, bounds, expected))
        elif bounds[0] >= INK_MINIMUM:
            candidates["ink"].append((-bounds[0], row, column, bounds, expected))
    probes = []
    for kind, rows in candidates.items():
        if not rows:
            continue
        _, row, column, (low, high), expected = sorted(rows)[0]
        darkest, brightest = ink_interval(color, low / 255, high / 255, DARK)
        probes.append({"name": f"edit-{index}-{character}-{kind}", "x": column, "y": row, "expected": expected,
                       "low": darkest, "high": brightest})
    return probes


def expected_probes(manifest: dict, source: Path, standard: str) -> list[dict]:
    if standard not in ("ntsc", "pal"):
        raise ValueError("console capture standard must be ntsc or pal")
    atlas = charset_atlas(manifest, source)
    lines = 448 if standard == "ntsc" else 512
    probes = []
    for name, virtual_x, virtual_y, expected in FIXED_PIXELS:
        probes.append({"name": name, "x": int(virtual_x * 512 / 640), "y": int(virtual_y * lines / 480),
                       "expected": expected, "low": expected, "high": expected})
    for index, character in enumerate(INPUT_TEXT):
        if character == " ":
            # The gap between words shows the half console alone.
            column, row = cell_pixels(index, standard)[len(cell_pixels(index, standard)) // 2][:2]
            probes.append({"name": f"edit-{index}-space", "x": column, "y": row, "expected": DARK, "low": DARK, "high": DARK})
            continue
        probes.extend(glyph_probes(atlas, index, character, CYAN if index == 0 else WHITE, standard))
    if sum(probe["name"].endswith("-ink") for probe in probes) < 6:
        raise ValueError("console capture has insufficient conservative charset ink probes")
    if sum(probe["name"].endswith("-hole") for probe in probes) < 4:
        raise ValueError("console capture has insufficient conservative charset hole probes")
    return probes


def verify_edit(path: Path, standard: str, manifest: dict, source: Path) -> dict:
    if standard not in ("ntsc", "pal"):
        raise ValueError("console capture standard must be ntsc or pal")
    width, height, pixels = read_tga(path)
    expected_height = 448 if standard == "ntsc" else 512
    if (width, height) != (512, expected_height):
        raise ValueError(f"console capture expected 512x{expected_height}, got {width}x{height}")
    checks = []
    for probe in expected_probes(manifest, source, standard):
        offset = (probe["y"] * width + probe["x"]) * 3
        actual = tuple(pixels[offset:offset + 3])
        passed = within(actual, probe)
        checks.append({**probe, "actual": actual, "passed": passed})
        if not passed:
            raise ValueError(f"console capture {probe['name']} at {probe['x']},{probe['y']}: "
                             f"expected {probe['low']}..{probe['high']}, got {actual}")
    return {"width": width, "height": height, "sha256": hashlib.sha256(Path(path).read_bytes()).hexdigest(), "checks": checks}
