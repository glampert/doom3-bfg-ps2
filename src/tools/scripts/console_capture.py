#!/usr/bin/env python3
# ================================================================================================
# File: console_capture.py
# Brief: Verify the native half-console, translucent background and edited input in GS captures.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import hashlib
from pathlib import Path

from renderer_capture import PIXEL_TOLERANCE, read_tga


CLEAR = (16, 24, 40)
DARK = (4, 6, 10)
WHITE = (255, 255, 255)
CYAN = (0, 255, 255)
# Coordinates are virtual 640x480; input text is the native edit field's "echo pending".
# Strokes and holes distinguish readable text from missing or opaque glyph rectangles.
EDIT_PIXELS = (
    ("half-console-background", 8, 200, DARK),
    ("uncovered-background", 8, 300, CLEAR),
    ("native-console-border", 16, 239, CYAN),
    ("native-prompt-top", 43.75, 210, CYAN),
    ("native-prompt-hole", 40.75, 216, DARK),
    ("edit-e-bar", 51.75, 218, WHITE),
    ("edit-e-hole", 51.75, 210, DARK),
    ("edit-c-top", 59.75, 214, WHITE),
    ("edit-c-left", 56.75, 218, WHITE),
    ("edit-c-hole", 59.75, 218, DARK),
    ("edit-h-left", 65, 210, WHITE),
    ("edit-h-right", 71.5, 218, WHITE),
    ("edit-h-hole", 70.75, 210, DARK),
    ("edit-o-top", 75.75, 214, WHITE),
    ("edit-o-hole", 75.75, 216, DARK),
    ("edit-word-space", 83.75, 218, DARK),
    ("edit-p-stem", 88.75, 220, WHITE),
    ("edit-final-g-tail", 142.75, 220, WHITE),
    ("edit-final-g-hole", 139.75, 220, DARK),
)


def verify_edit(path: Path, standard: str) -> dict:
    if standard not in ("ntsc", "pal"):
        raise ValueError("console capture standard must be ntsc or pal")
    width, height, pixels = read_tga(path)
    expected_height = 448 if standard == "ntsc" else 512
    if (width, height) != (512, expected_height):
        raise ValueError(f"console capture expected 512x{expected_height}, got {width}x{height}")
    checks = []
    for name, virtual_x, virtual_y, expected in EDIT_PIXELS:
        x, y = int(virtual_x * width / 640), int(virtual_y * height / 480)
        offset = (y * width + x) * 3
        actual = tuple(pixels[offset:offset + 3])
        passed = all(abs(value - target) <= PIXEL_TOLERANCE for value, target in zip(actual, expected))
        checks.append({"name": name, "x": x, "y": y, "expected": expected, "actual": actual, "passed": passed})
        if not passed:
            raise ValueError(f"console capture {name} at {x},{y}: expected {expected}, got {actual}")
    return {"width": width, "height": height, "sha256": hashlib.sha256(Path(path).read_bytes()).hexdigest(), "checks": checks}
