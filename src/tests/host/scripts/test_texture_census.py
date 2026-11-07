#!/usr/bin/env python3
# ================================================================================================
# File: test_texture_census.py
# Brief: Check the texture census's usage names, load-order precedence and per-class totals on
# synthetic containers, without game data.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/scripts"))
import texture_census  # noqa: E402
from test_ui_resources import container  # noqa: E402


def bimage(texture_type, image_format, color_format, width, height, levels, padding=8):
    return struct.pack(">q7i", 0, 0x0A4D4942, texture_type, image_format, color_format, width, height, levels) + bytes(padding)


class TextureCensus(unittest.TestCase):
    def test_usage_comes_from_the_generated_name(self):
        self.assertEqual(texture_census.usage_of("generated/images/textures/a_d#__0100.bimage"), "diffuse")
        self.assertEqual(texture_census.usage_of("generated/images/textures/a_local#__0300.bimage"), "bump")
        self.assertEqual(texture_census.usage_of("generated/images/lights/b#__0500.bimage"), "light")
        self.assertEqual(texture_census.usage_of("generated/images/x#__9900.bimage"), "usage99")
        self.assertEqual(texture_census.usage_of("generated/images/plain.bimage"), "unnamed")

    def test_later_containers_win_and_classes_sum(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second = Path(directory) / "first.resources", Path(directory) / "second.resources"
            first.write_bytes(container([
                ("generated/images/a_d#__0100.bimage", bimage(1, 8, 2, 64, 64, 7)),
                ("generated/images/b_d#__0100.bimage", bimage(1, 8, 2, 128, 32, 1)),
                ("generated/images/readme.txt", b"not an image"),
            ]))
            second.write_bytes(container([
                ("generated/images/a_d#__0100.bimage", bimage(1, 8, 2, 256, 256, 9)),
                ("generated/images/sky#__0200.bimage", bimage(2, 8, 0, 32, 32, 1)),
                ("generated/images/c_d#__0100.bimage", bimage(1, 8, 2, 24, 16, 1)),
            ]))
            selected = texture_census.collect([first, second])

        self.assertEqual(len(selected), 4)
        classes = texture_census.census(selected)
        diffuse = classes[("diffuse", "2d", "FMT_DXT5", "CFM_YCOCG_DXT5")]
        self.assertEqual(diffuse["images"], 3)
        self.assertEqual(diffuse["texels"], 256 * 256 + 128 * 32 + 24 * 16)
        self.assertEqual(diffuse["non_pow2"], 1)
        self.assertEqual(diffuse["mipped"], 1)
        self.assertEqual(dict(diffuse["max_extent"]), {256: 1, 128: 1, 24: 1})
        cube = classes[("default", "cube", "FMT_DXT5", "CFM_DEFAULT")]
        self.assertEqual(cube["texels"], 6 * 32 * 32)

    def test_a_record_that_is_not_a_bimage_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.resources"
            path.write_bytes(container([("generated/images/a#__0100.bimage", bytes(36))]))
            with self.assertRaises(ValueError):
                texture_census.collect([path])


if __name__ == "__main__":
    unittest.main()
