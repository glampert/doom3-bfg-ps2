#!/usr/bin/env python3
# ================================================================================================
# File: retail_ui_capture.py
# Brief: Derive conservative font ink/hole probes from local checked retail glyphs and converted pixels.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import hashlib
import math
from pathlib import Path

from renderer_capture import PIXEL_TOLERANCE, read_tga
import ui_gstx


BACKGROUND = (16, 24, 40)
TEXT = (240, 248, 255)
PHRASE = "Campaign"
# The weakest coverage an ink probe may bound: 48/255 still lifts every channel of TEXT far
# beyond PIXEL_TOLERANCE from the background. Halved thin fonts have little solid interior.
INK_MINIMUM = 48


class Atlas:
    """A converted GSTX font atlas, sampled as the target samples it (logical texel centers)."""

    def __init__(self, path: Path):
        self.texture = ui_gstx.read_texture(path)
        self.width, self.height = self.texture.width, self.texture.height

    def alpha(self, x: int, y: int) -> int:
        x, y = max(0, min(self.width - 1, x)), max(0, min(self.height - 1, y))
        return self.texture.alpha(x, y)

    def alpha_bounds(self, x: int, y: int):
        """Alpha sampling can return within one texel of texel (x, y)'s center: the probe's
        projection tolerance, independent of the target's bilinear rounding at stroke edges."""
        return self.texture.alpha_bounds(x - 0.5, y - 0.5, x + 1.5, y + 1.5)


def blend(ink, opacity: float, backdrop=BACKGROUND) -> tuple:
    return tuple(round(channel * opacity + under * (1 - opacity)) for channel, under in zip(ink, backdrop))


def ink_interval(ink, low: float, high: float, backdrop=BACKGROUND) -> tuple[tuple, tuple]:
    """Per-channel framebuffer range for coverage between low and high (0..1)."""
    first, second = blend(ink, low, backdrop), blend(ink, high, backdrop)
    return tuple(map(min, first, second)), tuple(map(max, first, second))


def glyph_candidates(atlas: Atlas, glyph: dict) -> dict:
    """Interior texels of a glyph as ink (strongest lower bound first) or holes (no coverage)."""
    candidates = {"ink": [], "hole": []}
    for y in range(2, glyph["height"] - 2):
        for x in range(2, glyph["width"] - 2):
            bounds = atlas.alpha_bounds(glyph["s"] + x, glyph["t"] + y)
            if bounds is None:
                continue
            if bounds[1] == 0:
                candidates["hole"].append((0, y, x, bounds))
            elif bounds[0] >= INK_MINIMUM:
                candidates["ink"].append((-bounds[0], y, x, bounds))
    return {kind: [entry[1:] for entry in sorted(rows)] for kind, rows in candidates.items()}


def within(actual, probe: dict, tolerance: int = PIXEL_TOLERANCE) -> bool:
    return all(low - tolerance <= value <= high + tolerance for value, low, high in zip(actual, probe["low"], probe["high"]))


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
    probes = [{"name": "background", "x": 8, "y": height - 8, "expected": BACKGROUND, "low": BACKGROUND,
               "high": BACKGROUND}]
    pen = 48
    for ordinal, character in enumerate(PHRASE):
        glyph = glyphs[ord(character)]
        # The full-size row maps each glyph texel to about one pixel. A probe accepts any color
        # the converted texture can produce within one texel of its projected position.
        for kind, candidates in glyph_candidates(atlas, glyph).items():
            selected = []
            for y, x, (low, high) in candidates:
                if len(selected) == 2:
                    break
                virtual_x = pen + glyph["left"] + x + 1
                virtual_y = 350 - glyph["top"] + y + 1
                screen_x = int(virtual_x * 512 / 640)
                screen_y = int(virtual_y * height / 480)
                if any(abs(screen_x - old[0]) < 4 and abs(screen_y - old[1]) < 4 for old in selected):
                    continue
                selected.append((screen_x, screen_y))
                darkest, brightest = ink_interval(TEXT, low / 255, high / 255)
                probes.append({"name": f"glyph-{ordinal}-{character}-{kind}-{len(selected)}",
                               "x": screen_x, "y": screen_y,
                               "expected": blend(TEXT, atlas.alpha(glyph["s"] + x, glyph["t"] + y) / 255),
                               "low": darkest, "high": brightest})
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
        passed = within(actual, probe)
        check = {**probe, "actual": actual, "passed": passed}
        checks.append(check)
        if not passed:
            raise ValueError(f"retail font {font_index} {probe['name']} at {probe['x']},{probe['y']}: "
                             f"expected {probe['low']}..{probe['high']}, got {actual}")
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
