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
    # The pass band. Every pass stores five-bit RGB and blends against that stored value. An additive
    # color term is a fog pass at F = 0, whose (255 * FOGCOL) >> 8 lands on the term with FOGCOL + 1.
    ("passes-replace-color", 40, 405, (99, 49, 24)),
    ("passes-two-pass-add", 100, 405, (99, 49, 24)),
    ("passes-gap", 70, 405, (16, 24, 41)),
    ("passes-half-alpha-add", 160, 405, (165, 140, 148)),
    ("passes-subtract", 270, 405, (123, 189, 255)),
    ("passes-mode8-add", 330, 405, (57, 49, 57)),
    # Screen adds Cd * (channel * alpha) per channel over a replaced base of 63/127/191 stored 56/120/184.
    ("passes-screen-full", 40, 450, (115, 181, 189)),
    ("passes-screen-base", 70, 450, (57, 123, 189)),
    ("passes-screen-half", 100, 450, (82, 148, 189)),
    # A concave L in the depth mask: its notch rejects content, then the cleared mask rejects all.
    ("passes-mask-l-left", 155, 450, (255, 255, 0)),
    ("passes-mask-l-notch", 195, 440, (16, 24, 41)),
    ("passes-mask-l-foot", 195, 458, (255, 255, 0)),
    ("passes-mask-cleared", 245, 450, (16, 24, 41)),
    ("passes-mask-line", 300, 450, (0, 255, 255)),
    ("passes-mask-line-outside", 300, 440, (16, 24, 41)),
    ("passes-line-replace", 380, 405, (255, 0, 0)),
    ("passes-line-replace-outside", 380, 420, (16, 24, 41)),
    ("passes-line-vertical", 410, 420, (0, 255, 0)),
    ("passes-line-half-alpha", 455, 420, (132, 140, 148)),
    ("passes-line-mode8", 455, 435, (148, 90, 41)),
    ("passes-line-gouraud", 545, 450, (127, 0, 127)),
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
