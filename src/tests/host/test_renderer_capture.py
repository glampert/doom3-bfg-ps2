#!/usr/bin/env python3
# ================================================================================================
# File: test_renderer_capture.py
# Brief: Reject malformed native captures and scene pixels that cannot prove the expected GS output.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/scripts"))
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
    rectangle(304, 296, 184, 88, (0, 0, 255))
    rectangle(360, 312, 128, 56, (0, 0, 128))
    for x, color in ((24, (255, 0, 0)), (80, (0, 255, 0)),
                     (144, (0, 0, 255)), (200, (255, 255, 0))):
        rectangle(x, 400, 48, 16, color)
    rectangle(0, 0, 512, 4, (255, 255, 255))
    rectangle(0, height - 4, 512, 4, (255, 255, 255))
    rectangle(0, 0, 4, height, (255, 255, 255))
    rectangle(508, 0, 4, height, (255, 255, 255))
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


if __name__ == "__main__":
    unittest.main()
