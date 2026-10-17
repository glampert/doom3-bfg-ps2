#!/usr/bin/env python3
# ================================================================================================
# File: test_renderer_pages.py
# Brief: Reject malformed authored pages, false seamless captures and reordered/stale cache output.
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
import renderer_page_fixture as fixture
from ui_page_package import fnv1a32


def tga(height, rgb):
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 512, height, 24, 0x20)
    bgr = bytearray(len(rgb))
    bgr[0::3], bgr[1::3], bgr[2::3] = rgb[2::3], rgb[1::3], rgb[0::3]
    return header + bgr


def reference_scene(height):
    """Rasterize original atlas triangles, without the verifier's selected probe table."""
    pixels = bytearray(bytes((16, 24, 40)) * (512 * height))
    colors = (((255, 0, 0), (0, 255, 0), (0, 0, 255)),
              ((0, 255, 255), (255, 0, 255), (255, 255, 0)))

    def texel(x, y):
        return colors[int(max(0, min(319, y)) >= 254)][min(2, max(0, min(599, x)) // 254)]

    def filtered(x, y):
        x, y = x - 0.5, y - 0.5
        base_x, base_y = math.floor(x), math.floor(y)
        fx, fy = x - math.floor(x), y - math.floor(y)
        upper_left, upper_right = texel(base_x, base_y), texel(base_x + 1, base_y)
        lower_left, lower_right = texel(base_x, base_y + 1), texel(base_x + 1, base_y + 1)
        return tuple((upper_left[channel] * (1 - fx) + upper_right[channel] * fx) * (1 - fy) +
                     (lower_left[channel] * (1 - fx) + lower_right[channel] * fx) * fy for channel in range(3))

    def quad(points, source, alpha=255):
        screen = [(math.floor(x * 512 / 640 * 16 + 0.5) / 16,
                   math.floor(y * height / 480 * 16 + 0.5) / 16) for x, y in points]
        for ids in ((0, 1, 2), (0, 2, 3)):
            (ax, ay), (bx, by), (cx, cy) = (screen[index] for index in ids)
            denominator = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            for y in range(max(0, math.floor(min(ay, by, cy))), min(height, math.ceil(max(ay, by, cy)))):
                for x in range(max(0, math.floor(min(ax, bx, cx))), min(512, math.ceil(max(ax, bx, cx)))):
                    wa = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / denominator
                    wb = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / denominator
                    wc = 1 - wa - wb
                    if min(wa, wb, wc) < -0.000001:
                        continue
                    uv = [sum(source[index][channel] * weight for index, weight in zip(ids, (wa, wb, wc)))
                          for channel in range(2)]
                    color = filtered(*uv)
                    offset = (y * 512 + x) * 3
                    pixels[offset:offset + 3] = bytes(max(0, min(255, round(value * alpha / 255 +
                        pixels[offset + channel] * (1 - alpha / 255)))) for channel, value in enumerate(color))

    full = ((0, 0), (600, 0), (600, 320), (0, 320))
    quad(((20, 32), (620, 32), (620, 224), (20, 224)), full)
    quad(((64, 256), (576, 256), (576, 304), (64, 304)), ((246, 120), (262, 120), (262, 136), (246, 136)))
    quad(((64, 320), (576, 320), (576, 368), (64, 368)), ((300, 246), (316, 246), (316, 262), (300, 262)))
    quad(((-24, 392), (136, 376), (160, 456), (0, 472)), full)
    quad(((224, 392), (416, 392), (416, 456), (224, 456)), full)
    quad(((320, 408), (512, 408), (512, 472), (320, 472)), ((550, 100),) * 4, 128)
    return pixels


class AuthoredPageFixture(unittest.TestCase):
    def test_real_page_cookers_produce_six_checked_pages_and_isolated_corruptions(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            metadata = fixture.stage(directory)
            self.assertEqual(set(metadata), set(fixture.NAMES))
            good = (directory / "renderer-pages.uip").read_bytes()
            self.assertEqual(struct.unpack_from("<8I", good), (0x47504955, 1, 600, 320, 256, 1, 6, 0))
            self.assertEqual(len(good), 80 + 6 * 262144)
            for index in range(6):
                offset, checksum = struct.unpack_from("<II", good, 32 + index * 8)
                self.assertEqual(offset, 80 + index * 262144)
                self.assertEqual(checksum, fnv1a32(good[offset:offset + 262144]))
            for name, changed_offset in (("renderer-pages-bad-header.uip", 0),
                                         ("renderer-pages-bad-table.uip", 32),
                                         ("renderer-pages-bad-pixel.uip", 80)):
                changed = (directory / name).read_bytes()
                self.assertEqual(len(changed), len(good))
                self.assertEqual([i for i, values in enumerate(zip(good, changed)) if values[0] != values[1]], [changed_offset])
            self.assertEqual((directory / "renderer-pages-truncated.uip").read_bytes(), good[:-1])
            self.assertEqual(metadata["renderer-pages.uip"]["sha256"], hashlib.sha256(good).hexdigest())
            self.assertFalse(list(directory.glob(".renderer-pages-*")))
            self.assertEqual(fixture.stage(directory), metadata)

    def test_global_neighbor_gutters_and_partial_edge_pages_keep_the_authored_pattern(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            fixture.stage(directory)
            good = (directory / "renderer-pages.uip").read_bytes()
            # Red page's right gutter is the next global green texel; the next
            # page's left gutter is global red, rather than private edge clamp.
            def pixel(page, x, y):
                offset = 80 + page * 262144 + (y * 256 + x) * 4
                return tuple(good[offset:offset + 4])
            self.assertEqual(pixel(0, 254, 120), (255, 0, 0, 255))
            self.assertEqual(pixel(0, 255, 120), (0, 255, 0, 255))
            self.assertEqual(pixel(1, 0, 120), (255, 0, 0, 255))
            self.assertEqual(pixel(1, 1, 120), (0, 255, 0, 255))
            self.assertEqual(pixel(1, 120, 255), (255, 0, 255, 255))
            self.assertEqual(pixel(4, 120, 0), (0, 255, 0, 255))
            self.assertEqual(pixel(5, 255, 255), (255, 255, 0, 255))


class PagedCapture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scenes = {448: reference_scene(448), 512: reference_scene(512)}

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / "renderer-paged.tga"

    def verify(self, pixels, standard="NTSC"):
        height = 448 if standard == "NTSC" else 512
        self.path.write_bytes(tga(height, pixels))
        return capture.verify_paged_scene(self.path, standard)

    def test_six_pages_rotated_clip_overlaps_and_bilinear_seams_both_standards(self):
        for standard, height in (("NTSC", 448), ("PAL", 512)):
            result = self.verify(self.scenes[height], standard)
            self.assertEqual(result["height"], height)
            self.assertEqual(len(result["checks"]), 45)
            self.assertTrue(all(item["passed"] for item in result["checks"]))
            seam = [item for item in result["checks"] if "seam" in item["name"]]
            self.assertEqual(len(seam), 22)
            self.assertTrue(any(8 < item["expected_rgb"][0] < 247 for item in seam))

    def test_private_edge_clamp_or_nearest_sampling_cannot_fake_bilinear_seams(self):
        for direction in ("vertical", "horizontal"):
            pixels = bytearray(self.scenes[448])
            for y in range(448):
                for x in range(512):
                    if direction == "vertical" and 51 <= x < 461 and 239 <= y < 284:
                        color = (255, 0, 0) if x < 256 else (0, 255, 0)
                    elif direction == "horizontal" and 51 <= x < 461 and 299 <= y < 344:
                        color = (0, 255, 0) if y < 321 else (255, 0, 255)
                    else:
                        continue
                    offset = (y * 512 + x) * 3
                    pixels[offset:offset + 3] = bytes(color)
            with self.subTest(direction=direction), self.assertRaisesRegex(ValueError, "paged-" + direction + "-seam"):
                self.verify(pixels)

    def test_untextured_or_stale_page_slot_output_is_rejected(self):
        for color in ((255, 255, 255), (0, 255, 255)):
            pixels = bytearray(self.scenes[448])
            for y in range(32, 209):
                for x in range(16, 496):
                    offset = (y * 512 + x) * 3
                    pixels[offset:offset + 3] = bytes(color)
            with self.subTest(color=color), self.assertRaisesRegex(ValueError, "paged-red"):
                self.verify(pixels)

    def test_cache_reuse_or_draw_reordering_must_preserve_the_overlapping_alpha_draw(self):
        pixels = bytearray(self.scenes[448])
        offset = (392 * 512 + 288) * 3
        pixels[offset:offset + 3] = bytes((0, 255, 0))
        with self.assertRaisesRegex(ValueError, "paged-cache-overlap-order"):
            self.verify(pixels)

    def test_seam_tolerance_remains_eight_and_bad_dimensions_are_rejected(self):
        pixels = bytearray(self.scenes[448])
        offset = (int(280 * 448 / 480) * 512 + 256) * 3
        pixels[offset] += 9
        with self.assertRaisesRegex(ValueError, "paged-vertical-seam"):
            self.verify(pixels)
        self.path.write_bytes(tga(448, self.scenes[448]))
        with self.assertRaisesRegex(ValueError, "expected 512x512"):
            capture.verify_paged_scene(self.path, "PAL")
        with self.assertRaisesRegex(ValueError, "standard"):
            capture.verify_paged_scene(self.path, "SECAM")


if __name__ == "__main__":
    unittest.main()
