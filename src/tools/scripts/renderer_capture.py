#!/usr/bin/env python3
# ================================================================================================
# File: renderer_capture.py
# Brief: Validate bounded native GS captures and compare authored scene pixels without display scaling.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""The native capture writer emits one exact, top-down, uncompressed 24-bit TGA profile."""

from __future__ import annotations

import hashlib
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
