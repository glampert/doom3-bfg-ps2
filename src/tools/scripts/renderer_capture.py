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
# The widest frame buffer (640) and the most lines (PAL's 512) the device offers.
MAX_DIMENSION = 640
PIXEL_TOLERANCE = 8
# The 32-bit profile stores every channel bit, so its captures may differ from an exact model by
# rounding alone; a five-bit store of the same colours misses by up to seven.
PIXEL_TOLERANCE_32 = 2
FRAME_BITS = (16, 32)


def stored(rgb: tuple, bits: int) -> tuple:
    """An 8-bit colour as the frame-buffer profile keeps it and the capture expands it again."""
    if bits not in FRAME_BITS:
        raise ValueError("frame-buffer bits must be 16 or 32")
    if bits == 32:
        return tuple(rgb)
    # PSMCT16 truncates each channel to five bits; the capture repeats the upper bits below them.
    return tuple(((channel >> 3) << 3) | (channel >> 5) for channel in rgb)


def tolerance(bits: int) -> int:
    """The per-channel slack each profile's pixel model allows."""
    if bits not in FRAME_BITS:
        raise ValueError("frame-buffer bits must be 16 or 32")
    return PIXEL_TOLERANCE_32 if bits == 32 else PIXEL_TOLERANCE


# The device's display modes (system::video): the standard's visible lines, its frame-buffer widths
# (index 0 the default) and how each field reads the buffer.
VISIBLE_LINES = {"NTSC": 448, "PAL": 512}
RESOLUTION_WIDTHS = (512, 640)
INTERLACE_MODES = ("FIELD", "FRAME")


def frame_size(standard: str, resolution: int = 0, interlace: str = "FIELD") -> tuple:
    """The frame buffer's pixels in a display mode: FRAME mode holds half the standard's lines."""
    if standard not in VISIBLE_LINES or not 0 <= resolution < len(RESOLUTION_WIDTHS) or interlace not in INTERLACE_MODES:
        raise ValueError("display mode outside the device's table")
    lines = VISIBLE_LINES[standard]
    return RESOLUTION_WIDTHS[resolution], lines // 2 if interlace == "FRAME" else lines


def fixed_vram_bytes(width: int, height: int, bits: int) -> int:
    """VRAM the two colour buffers and the depth buffer take (gs.cpp LayOutBuffers): each buffer is
    padded to whole GS page rows, 64 lines for 16-bit formats and 32 for PSMCT32, and the depth
    buffer is 16 bits in both profiles."""
    if bits not in FRAME_BITS:
        raise ValueError("frame-buffer bits must be 16 or 32")

    def buffer(bytes_per_pixel):
        page_rows = 32 if bytes_per_pixel == 4 else 64
        return width * -(-height // page_rows) * page_rows * bytes_per_pixel

    return 2 * buffer(bits // 8) + buffer(2)
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

# The graybox world drawn by world_lm.vcl from the GS gate's four cameras. Points sit deep inside
# flat faces (eight materials a view, the clipped floor and divider near the camera included), with
# 8-bit colours from a reference rasterizer of the same .proc, cameras and vertex colour bytes; each
# profile compares what its frame buffer stores of them (stored()). Untextured vertex colour writes
# 0x80 as 128: half brightness until R2.
WORLD_SCENE_PIXELS = {
    "NTSC": {
        "room0": ((40, 180, (58, 56, 53)), (92, 40, (24, 25, 28)), (472, 152, (33, 30, 25)), (326, 340, (46, 13, 10)), (150, 180, (40, 39, 37)), (268, 268, (97, 27, 22)), (380, 422, (46, 46, 48)), (164, 424, (75, 75, 73))),
        "room1": ((40, 72, (40, 39, 37)), (138, 40, (24, 25, 28)), (266, 412, (46, 46, 48)), (318, 182, (48, 43, 36)), (178, 176, (53, 51, 48)), (348, 382, (75, 75, 73)), (148, 252, (68, 38, 11)), (250, 226, (56, 23, 53))),
        "doorway": ((388, 40, (24, 25, 28)), (58, 76, (14, 23, 46)), (152, 216, (33, 30, 25)), (176, 384, (75, 75, 73)), (296, 384, (46, 46, 48)), (430, 262, (60, 54, 17)), (484, 152, (53, 51, 48)), (364, 258, (94, 83, 26))),
        "clip": ((88, 40, (24, 25, 28)), (414, 124, (40, 39, 37)), (338, 350, (46, 46, 48)), (140, 162, (58, 56, 53)), (226, 414, (75, 75, 73)), (482, 208, (33, 30, 25)), (128, 222, (38, 88, 38)), (270, 36, (16, 25, 50))),
    },
    "PAL": {
        "room0": ((56, 40, (24, 25, 28)), (468, 174, (33, 30, 25)), (106, 200, (40, 39, 37)), (342, 378, (46, 13, 10)), (32, 260, (58, 56, 53)), (270, 306, (97, 27, 22)), (398, 482, (46, 46, 48)), (150, 484, (75, 75, 73))),
        "room1": ((40, 136, (40, 39, 37)), (112, 40, (24, 25, 28)), (268, 470, (46, 46, 48)), (328, 208, (48, 43, 36)), (166, 202, (53, 51, 48)), (356, 432, (75, 75, 73)), (132, 288, (68, 38, 11)), (234, 260, (56, 23, 53))),
        "doorway": ((396, 40, (24, 25, 28)), (40, 88, (14, 23, 46)), (132, 228, (33, 30, 25)), (452, 296, (60, 54, 17)), (160, 432, (75, 75, 73)), (296, 432, (46, 46, 48)), (380, 296, (94, 83, 26)), (238, 256, (40, 39, 37))),
        "clip": ((40, 40, (24, 25, 28)), (438, 134, (40, 39, 37)), (120, 178, (58, 56, 53)), (348, 392, (46, 46, 48)), (222, 474, (75, 75, 73)), (108, 252, (38, 88, 38)), (496, 222, (33, 30, 25)), (272, 42, (16, 25, 50))),
    },
}


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


def verify_world_scene(path: Path, standard: str, view: str, bits: int = 16) -> dict:
    """Require the graybox view's depth-tested, clipped world geometry at its reference colours,
    as the frame-buffer profile of 'bits' stores them."""
    if standard not in ("NTSC", "PAL") or view not in WORLD_SCENE_PIXELS["NTSC"]:
        raise ValueError("world capture standard must be NTSC or PAL and the view a known camera")
    slack = tolerance(bits)
    width, height, pixels, digest = _read_capture(path)
    expected_height = 448 if standard == "NTSC" else 512
    if (width, height) != (512, expected_height):
        raise ValueError(f"world capture is {width}x{height}; expected 512x{expected_height} for {standard}")
    checks = []
    for x, y, reference in WORLD_SCENE_PIXELS[standard][view]:
        expected = stored(reference, bits)
        offset = (y * width + x) * 3
        actual = tuple(pixels[offset:offset + 3])
        passed = all(abs(a - e) <= slack for a, e in zip(actual, expected))
        checks.append({"x": x, "y": y, "expected_rgb": list(expected), "actual_rgb": list(actual),
                       "tolerance": slack, "passed": passed})
        if not passed:
            raise ValueError(f"world {view} {bits}-bit capture pixel ({x},{y}) is {actual}; expected {expected} within {slack}")
    return {"schema": 1, "standard": standard, "view": view, "bits": bits, "width": width, "height": height,
            "sha256": digest, "checks": checks}


# The GS gate's profile switch: a fill and a modulated texture whose low channel bits PSMCT16 drops,
# over the black clear (renderer_gs_tests.cpp, FrameFormatSwitch). Each: name, pixel rectangle
# (x0, y0, x1, y1, exclusive) and 8-bit colour; every other pixel is the clear.
FORMAT_SCENE_RECTS = (
    ("format-fill", (64, 64, 192, 160), (93, 47, 205)),
    ("format-texture", (256, 64, 384, 160), (3, 130, 253)),
)
FORMAT_CLEAR = (0, 0, 0)


def verify_format_scene(path: Path, standard: str, bits: int, size: tuple | None = None) -> dict:
    """Require every pixel of the profile-switch scene at the colour the profile of 'bits' stores:
    a pixel of an earlier picture or layout left anywhere fails. 'size' is the display mode's frame
    (frame_size), the default 512-wide FIELD mode unless given."""
    if standard not in ("NTSC", "PAL"):
        raise ValueError("renderer capture standard must be NTSC or PAL")
    slack = tolerance(bits)
    width, height, pixels, digest = _read_capture(path)
    expected = size or frame_size(standard)
    if (width, height) != tuple(expected):
        raise ValueError(f"format capture is {width}x{height}; expected {expected[0]}x{expected[1]} for {standard}")
    counts = {name: 0 for name, *_ in FORMAT_SCENE_RECTS}
    counts["format-clear"] = 0
    for y in range(height):
        for x in range(width):
            name, reference = "format-clear", FORMAT_CLEAR
            for rect_name, (x0, y0, x1, y1), colour in FORMAT_SCENE_RECTS:
                if x0 <= x < x1 and y0 <= y < y1:
                    name, reference = rect_name, colour
            expected = stored(reference, bits)
            offset = (y * width + x) * 3
            actual = tuple(pixels[offset:offset + 3])
            if any(abs(a - e) > slack for a, e in zip(actual, expected)):
                raise ValueError(f"{bits}-bit capture pixel {name} ({x},{y}) is {actual}; expected {expected} within {slack}")
            counts[name] += 1
    return {"schema": 1, "standard": standard, "bits": bits, "width": width, "height": height,
            "sha256": digest, "pixels": counts, "tolerance": slack}

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
