#!/usr/bin/env python3
# ================================================================================================
# File: test_renderer_capture.py
# Brief: Reject malformed native captures and scene pixels that cannot prove the expected GS output.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import hashlib
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/scripts"))
import renderer_capture as capture


def tga(width, height, rgb):
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, width, height, 24, 0x20)
    bgr = bytearray(len(rgb))
    bgr[0::3], bgr[1::3], bgr[2::3] = rgb[2::3], rgb[1::3], rgb[0::3]
    return header + bgr


def scene(height=448):
    # Build the authored rectangles independently of the verifier's sampled pixel table.
    rgb = bytearray(bytes((16, 24, 40)) * (512 * height))

    def rectangle(x, y, width, rows, color):
        row = bytes(color) * width
        for line in range(y, y + rows):
            offset = (line * 512 + x) * 3
            rgb[offset:offset + len(row)] = row

    grid = (
        ((255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)),
        ((255, 255, 0), (0, 255, 255), (255, 0, 255), (128, 128, 128)),
        ((32, 32, 32), (64, 64, 64), (128, 128, 128), (192, 192, 192)),
        ((255, 128, 0), (0, 128, 255), (128, 0, 255), (0, 0, 0)),
    )
    for row, colors in enumerate(grid):
        for column, color in enumerate(colors):
            rectangle(24 + column * 120, 24 + row * 64, 104, 48, color)

    def npot_color(x, y):
        if x == 0 or y == 0:
            return 255, 0, 0
        if x == 36 or y == 18:
            return 0, 0, 255
        return (0, 255, 0) if (x // 4 + y // 4) % 2 else (255, 255, 0)

    for y in range(19):
        for x in range(37):
            rectangle(24 + x * 4, 152 + y * 4, 4, 4, npot_color(x, y))
    # Linear coordinates straddle texel centers; calculate the four authored source contributions.
    for y in range(76):
        for x in range(148):
            left, top = (x - 2) // 4, (y - 2) // 4
            fraction_x, fraction_y = ((x - 2) % 4) / 4, ((y - 2) % 4) / 4
            source = [npot_color(max(0, min(36, left + dx)), max(0, min(18, top + dy)))
                      for dy in range(2) for dx in range(2)]
            weights = ((1 - fraction_x) * (1 - fraction_y), fraction_x * (1 - fraction_y),
                       (1 - fraction_x) * fraction_y, fraction_x * fraction_y)
            color = tuple(round(sum(pixel[channel] * weight for pixel, weight in zip(source, weights)))
                          for channel in range(3))
            rectangle(184 + x, 152 + y, 1, 1, color)
    for y in range(76):
        for x in range(144):
            u, v = x * 32 // 144, y * 16 // 76
            color = (255, 0, 255) if (u // 2 + v // 2) % 2 else (0, 255, 255)
            rectangle(344 + x, 152 + y, 1, 1, color)
    rectangle(24, 240, 148, 32, (0, 0, 255))
    for y in range(12):
        for x in range(24):
            rectangle(x, 280 + y, 1, 1, npot_color((x + 124) // 4, y * 19 // 12))
    # Combined texture/vertex alpha quarters the modulated white over the black grid cell.
    rectangle(384, 240, 104, 24, (32, 64, 32))
    rectangle(24, 296, 224, 88, (255, 0, 0))
    rectangle(136, 312, 112, 56, (255, 128, 128))
    rectangle(200, 328, 48, 24, (128, 64, 64))
    rectangle(304, 296, 184, 88, (0, 0, 255))
    rectangle(360, 312, 128, 56, (0, 0, 128))
    for x, color in ((24, (255, 0, 0)), (80, (0, 255, 0)),
                     (144, (0, 0, 255)), (200, (255, 255, 0))):
        rectangle(x, 400, 48, 16, color)
    rectangle(440, 400, 48, 16, (41, 33, 41))
    rectangle(328, 400, 48, 16, (74, 49, 49))
    rectangle(0, 0, 512, 4, (255, 255, 255))
    rectangle(0, height - 4, 512, 4, (255, 255, 255))
    rectangle(0, 0, 4, height, (255, 255, 255))
    rectangle(508, 0, 4, height, (255, 255, 255))
    return rgb


def ui_rectangle(rgb, height, x, y, width, rows, color):
    """Fill a virtual-space region independently of the verifier's sampled positions."""
    left, right = max(0, math.ceil(x * 512 / 640)), min(512, math.ceil((x + width) * 512 / 640))
    top, bottom = max(0, math.ceil(y * height / 480)), min(height, math.ceil((y + rows) * height / 480))
    row = bytes(color) * max(0, right - left)
    for line in range(top, bottom):
        offset = (line * 512 + left) * 3
        rgb[offset:offset + len(row)] = row


def ui_scene(height=448):
    # Draw only simple authored strokes needed by this scene contract, not the complete atlas.
    # These geometric bars describe an A, cursor shapes, colored glyph bars and the input prompt.
    rgb = bytearray(bytes((16, 24, 40)) * (512 * height))

    def rectangle(x, y, width, rows, color):
        ui_rectangle(rgb, height, x, y, width, rows, color)

    white = (255, 255, 255)
    rectangle(32, 25, 12, 2, (0, 255, 255))
    rectangle(32, 81, 6, 2, (255, 0, 0))
    rectangle(65.5, 81, 4.5, 2, (0, 255, 0))
    rectangle(112, 81, 6, 2, (0, 0, 255))
    rectangle(33.5, 105, 4.5, 2, white)
    rectangle(32, 107, 1.5, 12, white)
    rectangle(38, 107, 1.5, 12, white)
    rectangle(32, 111, 7.5, 2, white)
    rectangle(33, 189, 6, 2, white)
    rectangle(57, 177, 6, 14, white)
    rectangle(32, 208, 128, 32, (255, 0, 0))
    rectangle(176, 208, 128, 32, (8, 140, 20))
    rectangle(320.25, 208.125, 80.5, 31.875, white)
    rectangle(-16.25, 256, 48.5, 32, (0, 0, 255))
    rectangle(33.5, 305, 4.5, 2, white)
    rectangle(233, 305, 6, 14, white)
    rectangle(336, 336, 96, 32, (74, 49, 49))
    rectangle(384, 352, 96, 32, (74, 49, 49))
    rectangle(384, 352, 48, 16, (132, 74, 57))
    rectangle(496, 384, 96, 32, white)
    rectangle(336, 400, 96, 32, (8, 74, 16))
    return rgb


def indexed_scene(height=448):
    """Independent affine/barycentric raster model, including glyph transparency and draw order."""
    rgb = bytearray(bytes((16, 24, 40)) * (512 * height))

    def triangle(points, colors, uv=None):
        (ax, ay), (bx, by), (cx, cy) = points
        denominator = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        left = max(0, math.floor(min(ax, bx, cx) * 512 / 640))
        right = min(512, math.ceil(max(ax, bx, cx) * 512 / 640))
        top = max(0, math.floor(min(ay, by, cy) * height / 480))
        bottom = min(height, math.ceil(max(ay, by, cy) * height / 480))
        for y in range(top, bottom):
            for x in range(left, right):
                px, py = x * 640 / 512, y * 480 / height
                wa = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / denominator
                wb = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / denominator
                wc = 1 - wa - wb
                if min(wa, wb, wc) < -0.00001:
                    continue
                weights = wa, wb, wc
                if uv is not None:
                    u, v = (sum(point[channel] * weight for point, weight in zip(uv, weights))
                            for channel in range(2))
                    cell_x, cell_y = min(15, max(0, int(u * 16))), min(15, max(0, int(v * 16)))
                    # Authored A is five columns scaled three pixels wide and seven two-pixel rows.
                    rows = (14, 17, 17, 31, 17, 17, 17)
                    if (cell_x >= 15 or not 1 <= cell_y < 15 or
                            not rows[(cell_y - 1) // 2] & (1 << (4 - cell_x // 3))):
                        continue
                color = [sum(point[channel] * weight for point, weight in zip(colors, weights))
                         for channel in range(4)]
                alpha = color[3] / 255
                offset = (y * 512 + x) * 3
                rgb[offset:offset + 3] = bytes(max(0, min(255, round(color[channel] * alpha +
                    rgb[offset + channel] * (1 - alpha)))) for channel in range(3))

    def quad(points, color, glyph=False):
        texcoords = ((0, 0), (1, 0), (1, 1), (0, 1))
        for indices in ((0, 1, 2), (0, 2, 3)):
            triangle(tuple(points[index] for index in indices), (color,) * 3,
                     tuple(texcoords[index] for index in indices) if glyph else None)

    quad(((64, 48), (192, 80), (160, 208), (32, 176)), (255, 255, 255, 255), True)
    quad(((-80, 256), (80, 224), (112, 352), (-48, 384)), (0, 255, 255, 255), True)
    triangle(((248, 48), (408, 48), (248, 208)),
             ((255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255)))
    quad(((528, 48), (608, 128), (528, 208), (448, 128)), (255, 255, 0, 255))
    triangle(((288, 176), (416, 176), (416, 240)),
             ((255, 255, 255, 0), (255, 255, 255, 255), (255, 255, 255, 255)))
    ui_rectangle(rgb, height, 192, 256, 160, 112, (255, 0, 0))
    ui_rectangle(rgb, height, 448, 256, 160, 112, (0, 0, 255))
    quad(((256, 272), (480, 272), (480, 352), (256, 352)), (255, 255, 255, 128))
    ui_rectangle(rgb, height, 320, 288, 64, 48, (128, 64, 64))
    triangle(((608, 416), (640, 416), (640, 448)), ((0, 255, 0, 255),) * 3)
    return rgb


class RendererCapture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / "renderer.tga"

    def write(self, content):
        self.path.write_bytes(content)
        return self.path

    def test_tga_channel_order_and_top_down_rows(self):
        pixels = bytes((1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12))
        self.assertEqual(capture.read_tga(self.write(tga(2, 2, pixels))), (2, 2, pixels))

    def test_valid_scene_dimensions_and_hash(self):
        for standard, height in (("NTSC", 448), ("PAL", 512)):
            with self.subTest(standard=standard):
                content = tga(512, height, scene(height))
                result = capture.verify_scene(self.write(content), standard)
                self.assertEqual(result["width"], 512)
                self.assertEqual(result["height"], height)
                self.assertEqual(result["sha256"], hashlib.sha256(content).hexdigest())
                self.assertTrue(all(item["passed"] for item in result["checks"]))

    def test_truncated_header_and_payload_and_trailer(self):
        content = tga(2, 2, bytes(12))
        for malformed in (content[:17], content[:-1], content + b"trailer"):
            with self.subTest(length=len(malformed)):
                with self.assertRaises(ValueError):
                    capture.read_tga(self.write(malformed))

    def test_non_native_tga_profiles(self):
        content = tga(2, 2, bytes(12))
        # Reject IDs, color maps, RLE, palette metadata, nonzero origins, depth and descriptor variants.
        for index, value in ((0, 1), (1, 1), (2, 10), (3, 1), (5, 1), (7, 24),
                             (8, 1), (10, 1), (16, 32), (17, 0), (17, 0x30), (17, 0x28)):
            with self.subTest(index=index, value=value):
                malformed = bytearray(content)
                malformed[index] = value
                with self.assertRaises(ValueError):
                    capture.read_tga(self.write(malformed))

    def test_dimension_bounds_before_payload_read(self):
        for width, height in ((0, 1), (1, 0), (513, 1), (1, 513), (65535, 65535)):
            with self.subTest(width=width, height=height):
                with self.assertRaisesRegex(ValueError, "dimensions"):
                    capture.read_tga(self.write(tga(width, height, b"")))
        self.assertEqual(capture.read_tga(self.write(tga(1, 1, b"\x12\x34\x56"))), (1, 1, b"\x12\x34\x56"))

    def test_wrong_scene_shape_or_standard(self):
        self.write(tga(512, 448, scene()))
        with self.assertRaisesRegex(ValueError, "expected 512x512"):
            capture.verify_scene(self.path, "PAL")
        with self.assertRaisesRegex(ValueError, "standard"):
            capture.verify_scene(self.path, "SECAM")
        with self.assertRaisesRegex(ValueError, "expected 512x448"):
            capture.verify_scene(self.write(tga(2, 2, bytes(12))), "NTSC")

    def test_scene_pixel_failures_are_named(self):
        for name, x, y, expected in capture.SCENE_PIXELS:
            with self.subTest(pixel=name):
                pixels = scene()
                offset = (y * 512 + x) * 3
                pixels[offset] = 0 if expected[0] > 128 else 255
                with self.assertRaisesRegex(ValueError, name):
                    capture.verify_scene(self.write(tga(512, 448, pixels)), "NTSC")

    def test_quantization_tolerance_is_bounded(self):
        pixels = scene()
        offset = (12 * 512 + 12) * 3
        pixels[offset] += capture.PIXEL_TOLERANCE
        capture.verify_scene(self.write(tga(512, 448, pixels)), "NTSC")
        pixels[offset] += 1
        with self.assertRaisesRegex(ValueError, "background"):
            capture.verify_scene(self.write(tga(512, 448, pixels)), "NTSC")

    def test_linear_filter_cannot_be_replaced_by_nearest(self):
        pixels = scene()
        for y in range(152, 228):
            source = (y * 512 + 24) * 3
            destination = (y * 512 + 184) * 3
            pixels[destination:destination + 148 * 3] = pixels[source:source + 148 * 3]
        with self.assertRaisesRegex(ValueError, "npot-linear-blend"):
            capture.verify_scene(self.write(tga(512, 448, pixels)), "NTSC")

    def test_translucent_texture_order_is_observable(self):
        pixels = scene()
        offset = (340 * 512 + 224) * 3
        # Black followed by white produces a lighter composite than white followed by black.
        pixels[offset:offset + 3] = bytes((192, 128, 128))
        with self.assertRaisesRegex(ValueError, "textured-overlap-order"):
            capture.verify_scene(self.write(tga(512, 448, pixels)), "NTSC")

    def test_valid_ui_scene_dimensions_hash_and_scaled_pixels(self):
        for standard, height in (("NTSC", 448), ("PAL", 512)):
            with self.subTest(standard=standard):
                content = tga(512, height, ui_scene(height))
                result = capture.verify_ui_scene(self.write(content), standard)
                self.assertEqual((result["width"], result["height"]), (512, height))
                self.assertEqual(result["sha256"], hashlib.sha256(content).hexdigest())
                self.assertEqual(len(result["checks"]), 33)
                self.assertTrue(all(item["passed"] for item in result["checks"]))
                underline = next(item for item in result["checks"] if item["name"] == "native-cursor-underline")
                self.assertEqual(underline["y"], int(190 * height / 480))

    def test_ui_scene_requires_capture_and_correct_standard(self):
        with self.assertRaises(FileNotFoundError):
            capture.verify_ui_scene(self.path, "NTSC")
        self.write(tga(512, 448, ui_scene()))
        with self.assertRaisesRegex(ValueError, "expected 512x512"):
            capture.verify_ui_scene(self.path, "PAL")
        with self.assertRaisesRegex(ValueError, "standard"):
            capture.verify_ui_scene(self.path, "SECAM")
        with self.assertRaisesRegex(ValueError, "expected 512x448"):
            capture.verify_ui_scene(self.write(tga(2, 2, bytes(12))), "NTSC")

    def test_ui_scene_rejects_missing_text_and_opaque_glyph_cells(self):
        for color, failure in (((16, 24, 40), "native-a-top"), ((255, 255, 255), "native-a-hole")):
            with self.subTest(color=color):
                pixels = ui_scene()
                ui_rectangle(pixels, 448, 32, 104, 8, 16, color)
                with self.assertRaisesRegex(ValueError, failure):
                    capture.verify_ui_scene(self.write(tga(512, 448, pixels)), "NTSC")

    def test_ui_scene_rejects_zero_alpha_replace_being_skipped(self):
        pixels = ui_scene()
        ui_rectangle(pixels, 448, 32, 208, 128, 32, (16, 24, 40))
        with self.assertRaisesRegex(ValueError, "native-replace-zero-alpha"):
            capture.verify_ui_scene(self.write(tga(512, 448, pixels)), "NTSC")

    def test_ui_scene_rejects_alpha_replace_and_color_loss(self):
        for bounds, color, failure in (
            ((176, 208, 128, 32), (0, 255, 0), "native-straight-alpha"),
            ((65.5, 81, 4.5, 2), (255, 255, 255), "native-color-green"),
        ):
            with self.subTest(failure=failure):
                pixels = ui_scene()
                ui_rectangle(pixels, 448, *bounds, color)
                with self.assertRaisesRegex(ValueError, failure):
                    capture.verify_ui_scene(self.write(tga(512, 448, pixels)), "NTSC")

    def test_ui_scene_rejects_wrong_cursor_shape_and_missing_final_cursor(self):
        for bounds, color, failure in (
            ((33, 177, 6, 14), (255, 255, 255), "native-cursor-underline-hole"),
            ((233, 305, 6, 14), (16, 24, 40), "native-input-final-cursor"),
        ):
            with self.subTest(failure=failure):
                pixels = ui_scene()
                ui_rectangle(pixels, 448, *bounds, color)
                with self.assertRaisesRegex(ValueError, failure):
                    capture.verify_ui_scene(self.write(tga(512, 448, pixels)), "NTSC")

    def test_ui_scene_rejects_truncated_fractional_positions(self):
        pixels = ui_scene()
        ui_rectangle(pixels, 448, 320.25, 208.125, 80.5, 31.875, (16, 24, 40))
        ui_rectangle(pixels, 448, 320, 208, 80, 32, (255, 255, 255))
        with self.assertRaisesRegex(ValueError, "native-fractional-left-uncovered"):
            capture.verify_ui_scene(self.write(tga(512, 448, pixels)), "NTSC")

    def test_valid_indexed_scene_both_standards(self):
        for standard, height in (("NTSC", 448), ("PAL", 512)):
            with self.subTest(standard=standard):
                content = tga(512, height, indexed_scene(height))
                result = capture.verify_indexed_scene(self.write(content), standard)
                self.assertEqual((result["width"], result["height"]), (512, height))
                self.assertEqual(result["sha256"], hashlib.sha256(content).hexdigest())
                self.assertEqual(len(result["checks"]), 27)
                self.assertTrue(all(item["passed"] for item in result["checks"]))

    def test_indexed_scene_requires_capture_and_native_dimensions(self):
        with self.assertRaises(FileNotFoundError):
            capture.verify_indexed_scene(self.path, "NTSC")
        self.write(tga(512, 448, indexed_scene()))
        with self.assertRaisesRegex(ValueError, "expected 512x512"):
            capture.verify_indexed_scene(self.path, "PAL")
        with self.assertRaisesRegex(ValueError, "standard"):
            capture.verify_indexed_scene(self.path, "SECAM")
        with self.assertRaisesRegex(ValueError, "expected 512x448"):
            capture.verify_indexed_scene(self.write(tga(2, 2, bytes(12))), "NTSC")

    def test_indexed_scene_rejects_flat_colors_missing_alpha_and_wrong_draw_order(self):
        original = indexed_scene()
        for bounds, color, failure in (
            ((248, 48, 160, 160), (255, 0, 0), "indexed-gouraud-red"),
            ((288, 176, 128, 64), (255, 255, 255), "indexed-alpha-gradient-middle"),
            ((320, 288, 64, 48), (192, 128, 128), "indexed-interleaved-black-order"),
            ((608, 416, 32, 32), (16, 24, 40), "indexed-pressure-last-triangle"),
        ):
            with self.subTest(failure=failure):
                pixels = bytearray(original)
                ui_rectangle(pixels, 448, *bounds, color)
                with self.assertRaisesRegex(ValueError, failure):
                    capture.verify_indexed_scene(self.write(tga(512, 448, pixels)), "NTSC")

    def test_indexed_scene_rejects_sprite_replacement_and_lost_clipped_uvs(self):
        original = indexed_scene()
        for bounds, color, failure in (
            ((32, 48, 160, 160), (255, 255, 255), "indexed-rotated-a-hole"),
            ((0, 224, 112, 160), (0, 255, 255), "indexed-clipped-a-hole"),
            ((448, 48, 160, 160), (255, 255, 0), "indexed-solid-diamond-outside"),
        ):
            with self.subTest(failure=failure):
                pixels = bytearray(original)
                ui_rectangle(pixels, 448, *bounds, color)
                with self.assertRaisesRegex(ValueError, failure):
                    capture.verify_indexed_scene(self.write(tga(512, 448, pixels)), "NTSC")


if __name__ == "__main__":
    unittest.main()
