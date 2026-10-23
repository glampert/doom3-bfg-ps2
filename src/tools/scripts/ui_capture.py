#!/usr/bin/env python3
# ================================================================================================
# File: ui_capture.py
# Brief: Check native authored SWF geometry, readable text, script focus and animated color/alpha.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import hashlib
from pathlib import Path

from renderer_capture import PIXEL_TOLERANCE, read_tga


MOVIE_PATH = "generated/swf/ps2_demo.bswf"
FONT_PATH = "newfonts/ps2_ascii/48.dat"
MOVIE_PAGE_PATH = "textures/ps2_movie.uipg"
FONT_PAGE_PATH = "textures/ps2_ascii.uipg"
NEGATIVE_DIAGNOSTICS = {
    "bswf-missing": "authored UI asset missing: " + MOVIE_PATH,
    "bswf-truncated": "authored UI asset length mismatch: " + MOVIE_PATH,
    "bswf-oversized": "authored UI asset length mismatch: " + MOVIE_PATH,
    "bswf-mutated": "authored UI asset content mismatch: " + MOVIE_PATH,
    "font-missing": "authored UI asset missing: " + FONT_PATH,
    "font-mutated": "authored UI asset content mismatch: " + FONT_PATH,
    "movie-page-missing": "authored UI page profile mismatch: " + MOVIE_PAGE_PATH,
    "movie-page-truncated": "authored UI page profile mismatch: " + MOVIE_PAGE_PATH,
    "movie-page-header": "authored UI page profile mismatch: " + MOVIE_PAGE_PATH,
    "movie-page-table": "authored UI page profile mismatch: " + MOVIE_PAGE_PATH,
    "movie-page-pixels": "native UI paged triangle failed",
    "font-page-missing": "authored UI page profile mismatch: " + FONT_PAGE_PATH,
}
BACKGROUND = (16, 24, 40)
PANEL = (32, 48, 64)
TEXT = (240, 248, 255)
# Expected authored scene data is deliberately independent of its asset generator.
# Samples sit inside solid regions, glyph strokes and holes, away from rasterization edges.
STATIC_PIXELS = (
    ("outside-panel", 16, 16, BACKGROUND),
    ("panel-top", 320, 32, PANEL),
    ("panel-bottom", 320, 448, PANEL),
    ("transformed-a-top", 502.8, 162.4, (255, 255, 255)),
    ("transformed-a-left-leg", 453.2, 181.6, (255, 255, 255)),
    ("transformed-a-right-leg", 539.6, 200.8, (255, 255, 255)),
    ("transformed-a-hole", 496.4, 191.2, PANEL),
    # The crossbar stays white on both sides of the movie atlas's x=254 page seam.
    ("transformed-a-crossbar", 493.2, 205.6, (255, 255, 255)),
    ("transformed-a-page-seam", 496.8, 206.4, (255, 255, 255)),
    ("transformed-a-crossbar-right", 500.4, 207.2, (255, 255, 255)),
    ("transformed-a-corner", 459.6, 152.8, PANEL),
    ("transformed-quad-outside", 438, 140, PANEL),
    ("native-title-d-top", 70, 51, TEXT),
    ("native-title-d-left", 66, 57, TEXT),
    ("native-title-d-right", 81, 57, TEXT),
    ("native-title-d-hole", 73, 57, PANEL),
    ("native-title-o-top", 92, 51, TEXT),
    ("native-title-o-hole", 92, 57, PANEL),
    ("native-subtitle-a-top", 71.5, 90.5, TEXT),
    ("native-subtitle-a-hole", 71.5, 94.5, PANEL),
    # Font glyph quads include one physical pixel for the half-texel gutter. These
    # row centers land inside the two-pixel strokes after either NTSC/PAL scaling.
    ("native-subtitle-a-crossbar", 71.5, 97.5, TEXT),
    # PackColor truncates the source to 127/63/31. GS white modulation preserves those
    # bytes; half alpha is 64/128. The panel stores 24/40/56 in PSMCT16, each blend
    # truncates to five bits again, then capture expands five-bit RGB to eight bits.
    # Model each storage round instead of widening tolerance for accumulated error.
    ("add-zero-alpha", 352, 316, (24, 41, 57)),
    ("add-half-alpha", 352, 344, (82, 66, 66)),
    ("add-full-alpha", 352, 372, (148, 99, 82)),
    ("add-saturates", 352, 400, (255, 255, 255)),
    ("add-overlap", 384, 348, (140, 90, 74)),
    ("add-shared-edge-left", 344, 334, (82, 66, 66)),
    ("add-shared-edge-middle", 352, 336, (82, 66, 66)),
    ("add-shared-edge-right", 360, 338, (82, 66, 66)),
    ("add-paged-a-top", 512, 314, (82, 66, 66)),
    ("add-paged-a-hole", 528, 334, (24, 41, 57)),
    ("add-paged-seam-left", 522, 349, (82, 66, 66)),
    ("add-paged-seam-right", 534, 349, (82, 66, 66)),
    # The upper convex panel has a blue base. Child clip ranges cross (A ends before B),
    # and two later masks expire together before the first white restoration patch.
    ("mask-trapezoid-left-bbox-corner", 66, 164, PANEL),
    ("mask-trapezoid-right-bbox-corner", 382, 164, PANEL),
    ("mask-trapezoid-inside-slant", 96, 160, (0, 0, 123)),
    ("mask-nested-solid", 174, 128, (255, 57, 24)),
    ("mask-nested-only-first", 114, 128, (0, 0, 123)),
    ("mask-nested-only-second", 244, 128, (0, 0, 123)),
    ("mask-nested-half-alpha", 174, 138, (8, 123, 90)),
    ("mask-nested-alpha-outside", 244, 138, (0, 0, 123)),
    ("mask-nested-additive", 174, 148, (57, 24, 132)),
    ("mask-nested-additive-outside", 244, 148, (0, 0, 123)),
    ("mask-non-lifo-first-removed", 244, 157, (57, 255, 57)),
    ("mask-non-lifo-second-retained", 114, 157, (0, 0, 123)),
    ("mask-last-child-removed", 312, 128, (255, 255, 255)),
    ("mask-same-expiry-intersection", 318, 140, (189, 24, 189)),
    ("mask-same-expiry-first-only", 306, 140, (0, 0, 123)),
    ("mask-same-expiry-second-only", 332, 140, (0, 0, 123)),
    ("mask-same-expiry-both-removed", 306, 152, (255, 255, 255)),
    ("mask-same-expiry-right-restored", 340, 152, (255, 255, 255)),
    ("mask-parent-removed", 56, 152, (255, 255, 255)),
    # The lower gray panel retains full-atlas UVs while its child mask clips the A.
    # Its crossbar covers both sides of x=254, then native edittext uses another mask.
    ("mask-page-panel-left-bbox-corner", 302, 286, PANEL),
    ("mask-page-panel-right-bbox-corner", 394, 286, PANEL),
    ("mask-page-panel-inside", 380, 214, (57, 57, 57)),
    ("mask-paged-seam-left", 326, 234, (255, 255, 255)),
    ("mask-paged-seam-right", 338, 234, (255, 255, 255)),
    ("mask-paged-crossbar-left-clipped", 306, 234, (57, 57, 57)),
    ("mask-paged-crossbar-right-clipped", 354, 234, (57, 57, 57)),
    ("mask-paged-a-hole", 328, 224, (57, 57, 57)),
    ("mask-paged-a-left-leg", 310, 226, (255, 255, 255)),
    ("mask-paged-a-right-leg", 349.5, 226, (255, 255, 255)),
    ("mask-text-first-top-clipped", 308.5, 274.5, (57, 57, 57)),
    ("mask-text-first-right-leg", 314.5, 278.5, TEXT),
    ("mask-text-second-a-top", 339.5, 274.5, TEXT),
    ("mask-text-third-a-clipped", 371.5, 274.5, (57, 57, 57)),
    ("mask-text-expired-before-next-child", 384, 280, (255, 123, 0)),
    ("mask-page-parent-removed", 292, 284, (255, 255, 255)),
    # Draw features at (420, 30). Native line mode draws one-pixel outlines; x multiples of 5 and
    # y multiples of 15 are whole pixels in both standards. White-texture modulation turns 40 into
    # 39 and 64/128/192 into 63/127/191 before five-bit storage.
    ("outline-fill", 440, 45, (57, 57, 57)),
    ("outline-top", 440, 30, (255, 255, 255)),
    ("outline-bottom", 440, 60, (255, 255, 255)),
    ("outline-left", 420, 45, (255, 255, 255)),
    ("outline-right", 460, 45, (255, 255, 255)),
    # A color transform adds 63/127/0 (bytes 159/191/127) after the fill, in a fog pass.
    ("additive-solid", 490, 45, (90, 156, 33)),
    # Screen adds Cd * Cs per channel: the PC renderer's DST_COLOR/ONE without its ignored blend op.
    ("screen-solid", 540, 45, (115, 181, 189)),
    # 0.25 white plus 127/63/0, weighted by the glyph's own alpha: holes keep the panel.
    ("additive-bitmap-leg", 426, 93, (181, 115, 57)),
    ("additive-bitmap-crossbar", 450, 105, (181, 115, 57)),
    ("additive-bitmap-hole", 450, 97, PANEL),
    # A concave L in the depth mask, then an outline-only mask that admits just its edges.
    ("depth-mask-l-bar", 528, 82, (255, 255, 0)),
    ("depth-mask-l-column", 502, 100, (255, 255, 0)),
    ("depth-mask-l-notch", 525, 97, PANEL),
    ("depth-mask-outline-top", 565, 75, (255, 0, 255)),
    ("depth-mask-outline-left", 545, 90, (255, 0, 255)),
    ("depth-mask-outline-inside", 565, 90, PANEL),
    ("depth-mask-outline-outside", 542, 72, PANEL),
)
PULSE_COLORS = {1: (0.1, 0.85, 1.0, 1.0), 2: (0.8, 0.25, 1.0, 0.4),
                3: (1.0, 0.5, 0.1, 0.7), 4: (0.2, 1.0, 0.3, 1.0)}


def blended(color, alpha, background=PANEL):
    return tuple(round(channel * alpha + backdrop * (1.0 - alpha)) for channel, backdrop in zip(color, background))


def expected_pixels(pulse: int, focus: int, selected: int):
    if pulse not in PULSE_COLORS or focus not in (0, 1) or selected not in (-1, 0, 1):
        raise ValueError("invalid native SWF capture state")
    red, green, blue, alpha = PULSE_COLORS[pulse]
    animated = blended((red * 255, green * 255, blue * 255), alpha)
    highlight = blended((24, 192, 224), 110 / 255)
    return STATIC_PIXELS + (
        ("pulse-color-alpha-left", 80, 230, animated),
        ("pulse-color-alpha-middle", 174, 240, animated),
        ("pulse-color-alpha-right", 268, 250, animated),
        ("pulse-outside", 300, 236, PANEL),
        ("focus-first-row", 270, 340, highlight if focus == 0 else PANEL),
        ("focus-second-row", 270, 388, highlight if focus == 1 else PANEL),
        ("native-status-t-or-s-top-left", 65.5, 411.5, TEXT if selected == -1 else PANEL),
        ("native-status-t-or-s-left", 65.5, 414.5, PANEL if selected == -1 else TEXT),
        ("native-status-selection-digit", 215.5, 414.5, TEXT if selected == 1 else PANEL),
    )


def verify(path: Path, standard: str, *, pulse: int, focus: int, selected: int) -> dict:
    if standard not in ("ntsc", "pal"):
        raise ValueError("native SWF capture standard must be ntsc or pal")
    width, height, pixels = read_tga(path)
    expected_height = 448 if standard == "ntsc" else 512
    if (width, height) != (512, expected_height):
        raise ValueError(f"native SWF capture expected 512x{expected_height}, got {width}x{height}")
    checks = []
    for name, virtual_x, virtual_y, expected in expected_pixels(pulse, focus, selected):
        x, y = int(virtual_x * width / 640), int(virtual_y * height / 480)
        offset = (y * width + x) * 3
        actual = tuple(pixels[offset:offset + 3])
        passed = all(abs(value - target) <= PIXEL_TOLERANCE for value, target in zip(actual, expected))
        checks.append({"name": name, "x": x, "y": y, "expected": expected, "actual": actual, "passed": passed})
        if not passed:
            raise ValueError(f"native SWF capture {name} at {x},{y}: expected {expected}, got {actual}")
    return {"width": width, "height": height, "pulse": pulse, "focus": focus, "selected": selected,
            "sha256": hashlib.sha256(Path(path).read_bytes()).hexdigest(), "checks": checks}
