#!/usr/bin/env python3
# ================================================================================================
# File: test_ui_bimage.py
# Brief: Check native image bounds and independent DXT/green-alpha cooking vectors.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import hashlib
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[4]
SPEC = importlib.util.spec_from_file_location("ui_bimage", ROOT / "src/tools/scripts/ui_bimage.py")
image = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(image)


def bimage(width, height, payloads, image_format=7, color_format=0, texture_type=1):
    result = bytearray(struct.pack(">q7i", -1, 0x0A4D4942, texture_type, image_format,
                                   color_format, width, height, len(payloads)))
    for level, payload in enumerate(payloads):
        result.extend(struct.pack(">5i", level, 0, max(1, width >> level), max(1, height >> level), len(payload)))
        result.extend(payload)
    return bytes(result)


def colors(first, second, indices):
    return struct.pack("<HHI", first, second, sum(value << (2 * index) for index, value in enumerate(indices)))


def alpha(first, second, indices):
    return bytes((first, second)) + sum(value << (3 * index) for index, value in enumerate(indices)).to_bytes(6, "little")


def pixels(data, level=0):
    width, height, rgba = image.decode_level(data, level)
    return width, height, [tuple(rgba[offset:offset + 4]) for offset in range(0, len(rgba), 4)]


class BinaryImageTests(unittest.TestCase):
    def test_rgb565_and_four_color_interpolation(self):
        block = colors(0xF800, 0x001F, [0, 1, 2, 3] * 4)
        width, height, result = pixels(bimage(4, 4, [block]))
        self.assertEqual((width, height), (4, 4))
        self.assertEqual(result[:4], [(255, 0, 0, 255), (0, 0, 255, 255),
                                     (170, 0, 85, 255), (85, 0, 170, 255)])
        # A non-extreme endpoint checks bit replication rather than only 0/255.
        result = pixels(bimage(4, 4, [colors(0x8C51, 0, [0] * 16)]))[2]
        self.assertEqual(result[0], (140, 138, 140, 255))

    def test_dxt1_three_color_transparency(self):
        result = pixels(bimage(4, 4, [colors(0, 0xFFFF, [0, 1, 2, 3] * 4)]))[2]
        self.assertEqual(result[:4], [(0, 0, 0, 255), (255, 255, 255, 255),
                                     (127, 127, 127, 255), (0, 0, 0, 0)])

    def test_dxt5_eight_alpha_palette(self):
        indices = list(range(8)) * 2
        block = alpha(250, 10, indices) + colors(0xFFFF, 0, [0] * 16)
        result = pixels(bimage(4, 4, [block], image_format=8))[2]
        self.assertEqual([pixel[3] for pixel in result[:8]], [250, 10, 215, 181, 147, 112, 78, 44])
        self.assertTrue(all(pixel[:3] == (255, 255, 255) for pixel in result))

    def test_dxt5_six_alpha_palette_and_forced_four_colors(self):
        block = alpha(10, 250, list(range(8)) * 2) + colors(0, 0xFFFF, [3] * 16)
        result = pixels(bimage(4, 4, [block], image_format=8))[2]
        self.assertEqual([pixel[3] for pixel in result[:8]], [10, 250, 58, 106, 154, 202, 0, 255])
        self.assertTrue(all(pixel[:3] == (170, 170, 170) for pixel in result))

    def test_npot_block_cropping_and_multiple_rows(self):
        red = colors(0xF800, 0, [0] * 16)
        green = colors(0x07E0, 0, [0] * 16)
        blue = colors(0x001F, 0, [0] * 16)
        white = colors(0xFFFF, 0, [0] * 16)
        width, height, result = pixels(bimage(5, 5, [red + green + blue + white]))
        self.assertEqual((width, height, len(result)), (5, 5, 25))
        self.assertEqual(result[:5], [(255, 0, 0, 255)] * 4 + [(0, 255, 0, 255)])
        self.assertEqual(result[-5:], [(0, 0, 255, 255)] * 4 + [(255, 255, 255, 255)])

    def test_green_alpha_restores_white_rgb(self):
        # Green 0/255/170/85 becomes coverage; DXT alpha is not the coverage source.
        block = colors(0x07E0, 0, [0, 1, 2, 3] * 4)
        result = pixels(bimage(4, 4, [block], color_format=3))[2]
        self.assertEqual(result[:4], [(255, 255, 255, 255), (255, 255, 255, 0),
                                     (255, 255, 255, 170), (255, 255, 255, 85)])
        result5 = pixels(bimage(4, 4, [alpha(0, 0, [0] * 16) + block], 8, 3))[2]
        self.assertEqual(result5, result)

    def test_mips_hashes_and_explicit_decode_selection(self):
        red = colors(0xF800, 0, [0] * 16)
        blue = colors(0x001F, 0, [0] * 16)
        data = bimage(4, 4, [red, blue])
        info = image.inspect_bimage(data)
        self.assertEqual((info["version"], info["format"], info["color_format"]), (10, "FMT_DXT1", "CFM_DEFAULT"))
        self.assertEqual((info["decoded_bytes"], info["decoded_mip_bytes"], info["payload_bytes"]), (64, 80, 16))
        self.assertEqual(info["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual(info["mips"][1]["sha256"], hashlib.sha256(blue).hexdigest())
        self.assertEqual(pixels(data, 1), (2, 2, [(0, 0, 255, 255)] * 4))
        for level in (-1, 2, False, "0"):
            with self.subTest(level=level), self.assertRaises(ValueError):
                image.decode_level(data, level)

    def test_truncation_and_trailing_bytes(self):
        data = bimage(4, 4, [colors(0, 0, [0] * 16)])
        for length in range(len(data)):
            with self.subTest(length=length), self.assertRaises(ValueError):
                image.inspect_bimage(data[:length])
        with self.assertRaisesRegex(ValueError, "trailing"):
            image.inspect_bimage(data + b"\0")

    def test_header_and_mip_bounds(self):
        original = bimage(4, 4, [colors(0, 0, [0] * 16)])
        # Native header fields and per-image fields are signed big-endian ints.
        edits = [(8, 0), (12, 0), (16, 99), (20, 99), (24, -1), (28, 0), (32, 0),
                 (32, 16), (36, -1), (40, 1), (44, 3), (48, 8), (52, 7), (52, -1)]
        for offset, value in edits:
            data = bytearray(original)
            struct.pack_into(">i", data, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                image.inspect_bimage(data)
        data = bytearray(original)
        struct.pack_into(">ii", data, 24, 8192, 8192)
        with self.assertRaisesRegex(ValueError, "64 MiB"):
            image.inspect_bimage(data)
        duplicate = bytearray(bimage(4, 4, [original[-8:], original[-8:]]))
        struct.pack_into(">i", duplicate, 64, 0)
        with self.assertRaisesRegex(ValueError, "duplicate"):
            image.inspect_bimage(duplicate)

    def test_unsupported_decode_is_explicit_after_validation(self):
        block = colors(0, 0, [0] * 16)
        for color_format in (1, 2):
            data = bimage(4, 4, [alpha(0, 0, [0] * 16) + block], 8, color_format)
            self.assertEqual(image.inspect_bimage(data)["color_format_id"], color_format)
            with self.subTest(color_format=color_format), self.assertRaisesRegex(ValueError, "unsupported.*color format"):
                image.decode_level(data)
        data = bimage(1, 1, [b"\1\2\3\4"], image_format=1)
        with self.assertRaisesRegex(ValueError, "unsupported.*format"):
            image.decode_level(data)

    def test_cube_metadata_requires_all_faces_and_retains_padded_tail(self):
        block = colors(0, 0, [0] * 16)
        data = bytearray(struct.pack(">q7i", -1, 0x0A4D4942, 2, 7, 0, 4, 4, 2))
        for level in range(2):
            for face in range(6):
                data.extend(struct.pack(">5i", level, face, 4, 4, 8))
                data.extend(block)
        info = image.inspect_bimage(data)
        self.assertEqual(len(info["mips"]), 12)
        self.assertEqual((info["mips"][-1]["width"], info["mips"][-1]["stored_width"]), (2, 4))
        with self.assertRaisesRegex(ValueError, "cube"):
            image.decode_level(data)


if __name__ == "__main__":
    unittest.main()
