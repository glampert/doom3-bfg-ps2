#!/usr/bin/env python3
# ================================================================================================
# File: renderer_capture.py
# Brief: Validate bounded native GS captures and compare authored scene pixels without display scaling.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""The native capture writer emits one exact, top-down, uncompressed 24-bit TGA profile."""

from __future__ import annotations

import hashlib
import math
from pathlib import Path
import struct


TGA_HEADER = struct.Struct("<BBBHHBHHHHBB")
MAX_DIMENSION = 512
PIXEL_TOLERANCE = 8
SCENE_PIXELS = (
    ("border", 0, 0, (255, 255, 255)),
    ("background", 12, 12, (16, 24, 40)),
    ("solid-red", 48, 48, (255, 0, 0)),
    ("solid-green", 160, 48, (0, 255, 0)),
    ("solid-blue", 280, 48, (0, 0, 255)),
    ("solid-white", 400, 48, (255, 255, 255)),
    ("npot-top-edge", 26, 154, (255, 0, 0)),
    ("npot-nearest-yellow", 34, 162, (255, 255, 0)),
    ("npot-nearest-green", 50, 162, (0, 255, 0)),
    ("npot-linear-interior", 194, 162, (255, 255, 0)),
    ("npot-linear-blend", 188, 162, (255, 128, 0)),
    ("npot-right-clamp", 48, 256, (0, 0, 255)),
    ("repeat-first", 346, 160, (0, 255, 255)),
    ("repeat-other", 356, 160, (255, 0, 255)),
    ("repeat-wrap", 382, 160, (0, 255, 255)),
    ("clipped-texture-edge", 22, 288, (0, 0, 255)),
    ("textured-tint-alpha", 400, 256, (32, 64, 32)),
    ("textured-white-alpha", 160, 336, (255, 128, 128)),
    ("textured-overlap-order", 224, 340, (128, 64, 64)),
    ("textured-black-alpha", 400, 336, (0, 0, 128)),
    ("dirty-before", 48, 408, (255, 0, 0)),
    ("dirty-after", 104, 408, (0, 255, 0)),
    ("evict-blue", 168, 408, (0, 0, 255)),
    ("evict-yellow", 224, 408, (255, 255, 0)),
    ("textured-add-quarter-alpha", 464, 408, (41, 33, 41)),
    ("textured-add-half-alpha", 352, 408, (74, 49, 49)),
    ("textured-add-black-preserves-destination", 408, 408, (16, 24, 40)),
)

# Virtual 640x480 positions deliberately sit inside the authored glyph strokes or empty regions.
# The A's top, legs, crossbar and hole distinguish readable coverage from opaque text quads.
UI_SCENE_PIXELS = (
    ("ui-background", 12, 12, (16, 24, 40)),
    ("native-big-cyan", 36, 26, (0, 255, 255)),
    ("native-color-red", 34, 82, (255, 0, 0)),
    ("native-color-green", 66.5, 82, (0, 255, 0)),
    ("native-color-blue", 114, 82, (0, 0, 255)),
    ("native-a-top", 35.5, 105.75, (255, 255, 255)),
    ("native-a-left-leg", 32.5, 108.5, (255, 255, 255)),
    ("native-a-right-leg", 39, 108.5, (255, 255, 255)),
    ("native-a-crossbar", 35.5, 112, (255, 255, 255)),
    ("native-a-hole", 35.5, 108.5, (16, 24, 40)),
    ("native-a-top-corner", 32.5, 105.75, (16, 24, 40)),
    ("native-cursor-underline", 35.5, 190, (255, 255, 255)),
    ("native-cursor-underline-hole", 35.5, 182, (16, 24, 40)),
    ("native-cursor-block", 59.5, 182, (255, 255, 255)),
    ("native-cursor-block-margin", 56.5, 182, (16, 24, 40)),
    ("native-replace-zero-alpha", 96, 224, (255, 0, 0)),
    ("native-straight-alpha", 240, 224, (8, 140, 20)),
    ("native-fractional-white", 360, 224, (255, 255, 255)),
    ("native-fractional-left-uncovered", 320, 224, (16, 24, 40)),
    ("native-fractional-right-covered", 400, 224, (255, 255, 255)),
    ("native-fractional-right-uncovered", 401.25, 224, (16, 24, 40)),
    ("native-clipped-blue", 1, 272, (0, 0, 255)),
    ("native-clipped-blue-right", 31.25, 272, (0, 0, 255)),
    ("native-clipped-blue-outside", 33.75, 272, (16, 24, 40)),
    ("native-input-line", 35.5, 306, (255, 255, 255)),
    ("native-input-final-cursor", 235.5, 310, (255, 255, 255)),
    # Half of source 127/63/31 is added to stored 16/24/40, then each pass stores
    # five-bit RGB. Capture expands it back; account for both overlap storage rounds.
    ("native-add-half-alpha", 352, 344, (74, 49, 49)),
    ("native-add-overlap", 400, 360, (132, 74, 57)),
    ("native-add-second-only", 464, 376, (74, 49, 49)),
    ("native-add-outside", 492, 360, (16, 24, 40)),
    ("native-add-zero-alpha", 528, 352, (16, 24, 40)),
    ("native-add-saturates", 528, 400, (255, 255, 255)),
    ("native-add-restores-alpha", 368, 416, (8, 74, 16)),
)

# Virtual-space samples use barycentric colors or interior points of the authored A's strokes.
# Rotated/skewed quad bounding boxes contain deliberate empty samples to reject sprite substitutes.
INDEXED_SCENE_PIXELS = (
    ("indexed-background", 216, 400, (16, 24, 40)),
    ("indexed-rotated-a-top", 120, 79, (255, 255, 255)),
    ("indexed-rotated-a-leg", 65, 95, (255, 255, 255)),
    ("indexed-rotated-a-hole", 113, 107, (16, 24, 40)),
    ("indexed-rotated-a-crossbar", 108, 127, (255, 255, 255)),
    ("indexed-rotated-a-corner", 73, 63, (16, 24, 40)),
    ("indexed-rotated-outside", 192, 48, (16, 24, 40)),
    ("indexed-clipped-a-leg", 66, 273, (0, 255, 255)),
    ("indexed-clipped-a-hole", 6, 285, (16, 24, 40)),
    ("indexed-clipped-a-crossbar", 11, 305, (0, 255, 255)),
    ("indexed-clipped-outside", 100, 230, (16, 24, 40)),
    ("indexed-gouraud-red", 280, 80, (153, 51, 51)),
    ("indexed-gouraud-green", 344, 64, (76, 153, 25)),
    ("indexed-gouraud-blue", 264, 144, (76, 25, 153)),
    ("indexed-triangle-outside", 396, 128, (16, 24, 40)),
    ("indexed-solid-diamond", 528, 128, (255, 255, 0)),
    ("indexed-solid-diamond-left", 472, 128, (255, 255, 0)),
    ("indexed-solid-diamond-outside", 456, 64, (16, 24, 40)),
    ("indexed-alpha-gradient-middle", 352, 192, (136, 140, 148)),
    ("indexed-alpha-gradient-right", 400, 192, (225, 226, 228)),
    ("indexed-alpha-gradient-outside", 304, 224, (16, 24, 40)),
    ("indexed-deferred-white-on-red", 280, 312, (255, 128, 128)),
    ("indexed-interleaved-black-order", 344, 312, (128, 64, 64)),
    ("indexed-deferred-white-on-blue", 456, 312, (128, 128, 255)),
    ("indexed-deferred-white-on-background", 420, 312, (136, 140, 148)),
    ("indexed-pressure-last-triangle", 632, 424, (0, 255, 0)),
    ("indexed-pressure-triangle-outside", 612, 444, (16, 24, 40)),
)


def _read_capture(path: Path) -> tuple[int, int, bytes, str]:
    with Path(path).open("rb") as stream:
        header = stream.read(TGA_HEADER.size)
        if len(header) != TGA_HEADER.size:
            raise ValueError("renderer capture has a truncated TGA header")
        (id_length, color_map_type, image_type, color_map_first, color_map_length,
         color_map_bits, x_origin, y_origin, width, height, depth, descriptor) = TGA_HEADER.unpack(header)
        if (id_length, color_map_type, image_type, color_map_first, color_map_length,
                color_map_bits, x_origin, y_origin, depth, descriptor) != (0, 0, 2, 0, 0, 0, 0, 0, 24, 0x20):
            raise ValueError("renderer capture is not the native top-down 24-bit TGA profile")
        if not (1 <= width <= MAX_DIMENSION and 1 <= height <= MAX_DIMENSION):
            raise ValueError("renderer capture dimensions exceed the bounded TGA profile")
        expected = width * height * 3
        # One extra byte rejects trailers without reading an unbounded file into memory.
        payload = stream.read(expected + 1)
        if len(payload) != expected:
            raise ValueError("renderer capture TGA payload length does not match its dimensions")
    rgb = bytearray(expected)
    rgb[0::3], rgb[1::3], rgb[2::3] = payload[2::3], payload[1::3], payload[0::3]
    return width, height, bytes(rgb), hashlib.sha256(header + payload).hexdigest()


def read_tga(path: Path) -> tuple[int, int, bytes]:
    """Return native dimensions and top-down RGB pixels; reject any other TGA profile."""
    width, height, pixels, _ = _read_capture(path)
    return width, height, pixels


def verify_scene(path: Path, standard: str) -> dict:
    """Require the authored scene's dimensions and known pixels, allowing GS 5-bit rounding."""
    if standard not in ("NTSC", "PAL"):
        raise ValueError("renderer capture standard must be NTSC or PAL")
    width, height, pixels, digest = _read_capture(path)
    expected_height = 448 if standard == "NTSC" else 512
    if (width, height) != (512, expected_height):
        raise ValueError(f"renderer capture is {width}x{height}; expected 512x{expected_height} for {standard}")
    checks = []
    for name, x, y, expected in SCENE_PIXELS:
        offset = (y * width + x) * 3
        actual = tuple(pixels[offset:offset + 3])
        passed = all(abs(a - e) <= PIXEL_TOLERANCE for a, e in zip(actual, expected))
        checks.append({"name": name, "x": x, "y": y, "expected_rgb": list(expected),
                       "actual_rgb": list(actual), "tolerance": PIXEL_TOLERANCE, "passed": passed})
        if not passed:
            raise ValueError(f"renderer capture pixel {name} ({x},{y}) is {actual}; expected {expected} within {PIXEL_TOLERANCE}")
    return {"schema": 1, "standard": standard, "width": width, "height": height,
            "sha256": digest, "checks": checks}


def verify_ui_scene(path: Path, standard: str) -> dict:
    """Require native UI glyph coverage, colors, blending and clipped/fractional geometry."""
    return _verify_virtual_scene(path, standard, UI_SCENE_PIXELS)


def verify_indexed_scene(path: Path, standard: str) -> dict:
    """Require genuine indexed triangles, interpolated native attributes and deferred ordering."""
    return _verify_virtual_scene(path, standard, INDEXED_SCENE_PIXELS)


def _paged_color(x, y):
    colors = (((255, 0, 0), (0, 255, 0), (0, 0, 255)),
              ((0, 255, 255), (255, 0, 255), (255, 255, 0)))
    return colors[int(y >= 254)][min(2, x // 254)]


def _paged_bilinear(source_x, source_y):
    # Native UV addresses pixel boundaries. GS linear filtering is centered half
    # a texel inward; keep this established phase rather than widening tolerance.
    x, y = source_x - 0.5, source_y - 0.5
    left, top = math.floor(x), math.floor(y)
    fraction_x, fraction_y = x - left, y - top
    colors = [_paged_color(max(0, min(599, left + dx)), max(0, min(319, top + dy)))
              for dy in range(2) for dx in range(2)]
    weights = ((1 - fraction_x) * (1 - fraction_y), fraction_x * (1 - fraction_y),
               (1 - fraction_x) * fraction_y, fraction_x * fraction_y)
    return tuple(round(sum(color[channel] * weight for color, weight in zip(colors, weights)))
                 for channel in range(3))


def _paged_strip_sample(x, y, height, rectangle, source):
    def fixed(value):
        return math.floor(value * 16 + 0.5) / 16
    left, top, right, bottom = rectangle
    left, right = fixed(left * 512 / 640), fixed(right * 512 / 640)
    top, bottom = fixed(top * height / 480), fixed(bottom * height / 480)
    source_left, source_top, source_right, source_bottom = source
    return _paged_bilinear(source_left + (x - left) * (source_right - source_left) / (right - left),
                           source_top + (y - top) * (source_bottom - source_top) / (bottom - top))


def paged_scene_probes(standard: str) -> tuple:
    """Return physical pixel probes, including adjacent seam samples against CPU filtering."""
    if standard not in ("NTSC", "PAL"):
        raise ValueError("renderer capture standard must be NTSC or PAL")
    height = 448 if standard == "NTSC" else 512
    background = (16, 24, 40)
    virtual = (
        ("paged-red", 100, 90, (255, 0, 0)),
        ("paged-green", 400, 90, (0, 255, 0)),
        ("paged-blue", 575, 90, (0, 0, 255)),
        ("paged-cyan", 100, 204, (0, 255, 255)),
        ("paged-magenta", 400, 204, (255, 0, 255)),
        ("paged-yellow", 575, 204, (255, 255, 0)),
        ("paged-outside-left", 12, 100, background),
        ("paged-outside-right", 632, 100, background),
        ("paged-between-strips", 320, 312, background),
        ("paged-rotated-red", 22, 408, (255, 0, 0)),
        ("paged-rotated-green", 94, 401, (0, 255, 0)),
        ("paged-rotated-blue", 134, 397, (0, 0, 255)),
        ("paged-rotated-cyan", 14, 462, (0, 255, 255)),
        ("paged-rotated-magenta", 94, 454, (255, 0, 255)),
        ("paged-rotated-yellow", 150, 449, (255, 255, 0)),
        ("paged-clipped-left", 1, 410, (255, 0, 0)),
        ("paged-rotated-outside", 155, 392, background),
        ("paged-opaque-red", 248, 410, (255, 0, 0)),
        ("paged-opaque-green", 352, 400, (0, 255, 0)),
        ("paged-cache-overlap-order", 360, 420, (0, 127, 128)),
        ("paged-cache-overlap-magenta", 352, 452, (127, 0, 255)),
        ("paged-overlay-background", 480, 440, (8, 12, 148)),
        ("paged-overlay-outside", 480, 476, background),
    )
    probes = [(name, int(x * 512 / 640), int(y * height / 480), color) for name, x, y, color in virtual]
    for delta in (-16, -12, -8, -4, -2, -1, 0, 1, 2, 4, 8, 12, 16):
        x, y = 256 + delta, int(280 * height / 480)
        expected = _paged_strip_sample(x, y, height, (64, 256, 576, 304), (246, 120, 262, 136))
        probes.append((f"paged-vertical-seam-{delta:+d}", x, y, expected))
    center_y = round(344 * height / 480)
    for delta in (-4, -3, -2, -1, 0, 1, 2, 3, 4):
        x, y = 256, center_y + delta
        expected = _paged_strip_sample(x, y, height, (64, 320, 576, 368), (300, 246, 316, 262))
        probes.append((f"paged-horizontal-seam-{delta:+d}", x, y, expected))
    return tuple(probes)


def verify_paged_scene(path: Path, standard: str) -> dict:
    """Require all six pages, gutter filtering, clipped triangles and cache-safe draw order."""
    probes = paged_scene_probes(standard)
    width, height, pixels, digest = _read_capture(path)
    expected_height = 448 if standard == "NTSC" else 512
    if (width, height) != (512, expected_height):
        raise ValueError(f"renderer capture is {width}x{height}; expected 512x{expected_height} for {standard}")
    checks = []
    for name, x, y, expected in probes:
        offset = (y * width + x) * 3
        actual = tuple(pixels[offset:offset + 3])
        passed = all(abs(value - target) <= PIXEL_TOLERANCE for value, target in zip(actual, expected))
        checks.append({"name": name, "x": x, "y": y, "expected_rgb": list(expected),
                       "actual_rgb": list(actual), "tolerance": PIXEL_TOLERANCE, "passed": passed})
        if not passed:
            raise ValueError(f"renderer capture pixel {name} ({x},{y}) is {actual}; expected {expected} within {PIXEL_TOLERANCE}")
    return {"schema": 1, "standard": standard, "width": width, "height": height, "sha256": digest, "checks": checks}


def _verify_virtual_scene(path: Path, standard: str, scene_pixels: tuple) -> dict:
    if standard not in ("NTSC", "PAL"):
        raise ValueError("renderer capture standard must be NTSC or PAL")
    width, height, pixels, digest = _read_capture(path)
    expected_height = 448 if standard == "NTSC" else 512
    if (width, height) != (512, expected_height):
        raise ValueError(f"renderer capture is {width}x{height}; expected 512x{expected_height} for {standard}")
    checks = []
    for name, virtual_x, virtual_y, expected in scene_pixels:
        x, y = int(virtual_x * width / 640), int(virtual_y * height / 480)
        offset = (y * width + x) * 3
        actual = tuple(pixels[offset:offset + 3])
        passed = all(abs(a - e) <= PIXEL_TOLERANCE for a, e in zip(actual, expected))
        checks.append({"name": name, "x": x, "y": y, "expected_rgb": list(expected),
                       "actual_rgb": list(actual), "tolerance": PIXEL_TOLERANCE, "passed": passed})
        if not passed:
            raise ValueError(f"renderer capture pixel {name} ({x},{y}) is {actual}; expected {expected} within {PIXEL_TOLERANCE}")
    return {"schema": 1, "standard": standard, "width": width, "height": height,
            "sha256": digest, "checks": checks}
