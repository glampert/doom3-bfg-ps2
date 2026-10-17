#!/usr/bin/env python3
# ================================================================================================
# File: retail_ui_capture.py
# Brief: Derive conservative font ink/hole probes from local checked retail glyphs and cooked pixels.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import hashlib
import math
from pathlib import Path
import struct

from renderer_capture import PIXEL_TOLERANCE, read_tga


BACKGROUND = (16, 24, 40)
TEXT = (240, 248, 255)
PHRASE = "Campaign"


class Atlas:
    def __init__(self, path: Path):
        self.raw = path.read_bytes()
        magic, version, self.width, self.height, extent, gutter, self.count, reserved = struct.unpack_from("<8I", self.raw)
        if (magic, version, extent, gutter, reserved) != (0x47504955, 1, 256, 1, 0):
            raise ValueError("retail capture oracle requires UIPG1")
        self.columns = (self.width + 253) // 254
        if len(self.raw) != 32 + self.count * 8 + self.count * 262144:
            raise ValueError("retail capture oracle image length mismatch")

    def alpha(self, x: int, y: int) -> int:
        x, y = max(0, min(self.width - 1, x)), max(0, min(self.height - 1, y))
        column, row = x // 254, y // 254
        page = row * self.columns + column
        offset, _ = struct.unpack_from("<2I", self.raw, 32 + page * 8)
        return self.raw[offset + (((y - row * 254 + 1) * 256 + x - column * 254 + 1) * 4) + 3]


def _image_for_font(manifest: dict, font_path: str) -> str:
    logical = font_path[:-4] + ".tga"
    names = {entry["selected"] for entry in manifest["image_resolutions"]
             if entry["logical_image"] == logical and entry["status"] == "resolved"}
    if len(names) != 1:
        raise ValueError("retail capture oracle font atlas is ambiguous")
    return names.pop()


def expected_probes(manifest: dict, source: Path, font_index: int, standard: str) -> list[dict]:
    if standard not in ("ntsc", "pal"):
        raise ValueError("retail capture standard must be ntsc or pal")
    fonts = sorted(manifest["fonts"])
    if not 0 <= font_index < len(fonts):
        raise ValueError("retail capture font index is invalid")
    font_path = fonts[font_index]
    details = manifest["fonts"][font_path]
    atlas = Atlas(source / manifest["cooked"]["runtime_images"][_image_for_font(manifest, font_path)]["path"])
    glyphs = {glyph["codepoint"]: glyph for glyph in details["glyphs"]}
    height = 448 if standard == "ntsc" else 512
    probes = [{"name": "background", "x": 8, "y": height - 8, "expected": BACKGROUND}]
    pen = 48
    for ordinal, character in enumerate(PHRASE):
        glyph = glyphs[ord(character)]
        selected = {"ink": [], "hole": []}
        # The full-size row preserves enough source pixels for a conservative 3x3
        # neighborhood, independent of the target's bilinear rounding at edges.
        for y in range(2, glyph["height"] - 2):
            for x in range(2, glyph["width"] - 2):
                alpha = [atlas.alpha(glyph["s"] + x + dx, glyph["t"] + y + dy)
                         for dy in range(-1, 2) for dx in range(-1, 2)]
                low, high = min(alpha), max(alpha)
                kind = "ink" if low >= 192 and high - low <= 8 else "hole" if high == 0 else None
                if kind is None or len(selected[kind]) >= 2:
                    continue
                virtual_x = pen + glyph["left"] + x + 1
                virtual_y = 350 - glyph["top"] + y + 1
                screen_x = int(virtual_x * 512 / 640)
                screen_y = int(virtual_y * height / 480)
                if any(abs(screen_x - old[0]) < 4 and abs(screen_y - old[1]) < 4 for old in selected[kind]):
                    continue
                selected[kind].append((screen_x, screen_y))
                opacity = sum(alpha) / (len(alpha) * 255)
                expected = tuple(round(ink * opacity + backdrop * (1 - opacity))
                                 for ink, backdrop in zip(TEXT, BACKGROUND)) if kind == "ink" else BACKGROUND
                probes.append({"name": f"glyph-{ordinal}-{character}-{kind}-{len(selected[kind])}",
                               "x": screen_x, "y": screen_y, "expected": expected})
        pen += glyph["advance"]
    if sum("-ink-" in entry["name"] for entry in probes) < 4:
        raise ValueError("retail capture has insufficient conservative source ink probes")
    if sum("-hole-" in entry["name"] for entry in probes) < 4:
        raise ValueError("retail capture has insufficient conservative source hole probes")
    return probes


def verify(path: Path, manifest: dict, source: Path, font_index: int, standard: str) -> dict:
    width, height, pixels = read_tga(path)
    if (width, height) != (512, 448 if standard == "ntsc" else 512):
        raise ValueError("retail native font capture has incorrect dimensions")
    checks = []
    for probe in expected_probes(manifest, source, font_index, standard):
        offset = (probe["y"] * width + probe["x"]) * 3
        actual = tuple(pixels[offset:offset + 3])
        passed = all(abs(value - target) <= PIXEL_TOLERANCE for value, target in zip(actual, probe["expected"]))
        check = {**probe, "actual": actual, "passed": passed}
        checks.append(check)
        if not passed:
            raise ValueError(f"retail font {font_index} {probe['name']} at {probe['x']},{probe['y']}: "
                             f"expected {probe['expected']}, got {actual}")
    rows = []
    for scale, baseline in ((0.25, 120), (0.5, 220), (1, 350)):
        top = max(0, int((baseline - 100 * scale) * height / 480))
        bottom = min(height, math.ceil((baseline + 8) * height / 480))
        ink = sum(pixels[(y * width + x) * 3] > 100
                  for y in range(top, bottom) for x in range(32, 480))
        if ink < 12:
            raise ValueError(f"retail font {font_index} scale {scale} did not draw readable source ink")
        rows.append({"scale": scale, "ink_pixels": ink})
    return {"width": width, "height": height, "font_index": font_index,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "checks": checks, "rows": rows}
