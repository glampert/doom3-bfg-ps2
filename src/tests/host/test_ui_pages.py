#!/usr/bin/env python3
# ================================================================================================
# File: test_ui_pages.py
# Brief: Check lossless tile reconstruction, global-neighbor gutters and deterministic UV layout.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("ui_pages", ROOT / "src/tools/scripts/ui_pages.py")
pages = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pages)


def pattern(width, height):
    return bytes(channel for y in range(height) for x in range(width)
                 for channel in (x % 256, y % 256, (x + y) % 256, (3 * x + y) % 256))


def pixel(data, width, x, y):
    start = (y * width + x) * 4
    return tuple(data[start:start + 4])


class UiPageTests(unittest.TestCase):
    def test_npot_interiors_reconstruct_every_source_pixel_once(self):
        width, height = 255, 257
        rgba = pattern(width, height)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            layout = pages.write_pages(width, height, rgba, output)
            self.assertEqual((layout["columns"], layout["rows"], layout["page_count"]), (2, 2, 4))
            restored = bytearray(len(rgba))
            coverage = bytearray(width * height)
            for page in layout["pages"]:
                tile = (output / page["path"]).read_bytes()
                x, y, tile_width, tile_height = page["source_rect"]
                self.assertEqual(page["interior_rect"], [1, 1, tile_width, tile_height])
                self.assertEqual(page["atlas_origin"], [x, y])
                for row in range(tile_height):
                    source = ((row + 1) * 256 + 1) * 4
                    dest = ((y + row) * width + x) * 4
                    restored[dest:dest + tile_width * 4] = tile[source:source + tile_width * 4]
                    for column in range(tile_width):
                        coverage[(y + row) * width + x + column] += 1
            self.assertEqual(restored, rgba)
            self.assertEqual(coverage, b"\1" * (width * height))

    def test_internal_gutters_sample_global_neighbors_and_corners(self):
        width = height = 256
        rgba = pattern(width, height)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            layout = pages.write_pages(width, height, rgba, output)
            tiles = [(output / page["path"]).read_bytes() for page in layout["pages"]]
            # Horizontal boundary is source columns 253/254, intentionally distinct.
            self.assertNotEqual(pixel(rgba, width, 253, 10), pixel(rgba, width, 254, 10))
            self.assertEqual(pixel(tiles[0], 256, 255, 11), pixel(rgba, width, 254, 10))
            self.assertEqual(pixel(tiles[1], 256, 0, 11), pixel(rgba, width, 253, 10))
            # Vertical boundaries and the diagonal neighboring texel are preserved.
            self.assertEqual(pixel(tiles[0], 256, 11, 255), pixel(rgba, width, 10, 254))
            self.assertEqual(pixel(tiles[2], 256, 11, 0), pixel(rgba, width, 10, 253))
            self.assertEqual(pixel(tiles[0], 256, 255, 255), pixel(rgba, width, 254, 254))
            self.assertEqual(pixel(tiles[3], 256, 0, 0), pixel(rgba, width, 253, 253))

    def test_outer_gutters_and_unused_edge_padding_clamp_to_full_atlas(self):
        width, height = 255, 255
        rgba = pattern(width, height)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            layout = pages.write_pages(width, height, rgba, output)
            first = (output / layout["pages"][0]["path"]).read_bytes()
            last = (output / layout["pages"][-1]["path"]).read_bytes()
            self.assertEqual(pixel(first, 256, 0, 0), pixel(rgba, width, 0, 0))
            self.assertEqual(pixel(first, 256, 0, 10), pixel(rgba, width, 0, 9))
            self.assertEqual(pixel(last, 256, 1, 1), pixel(rgba, width, 254, 254))
            self.assertEqual(pixel(last, 256, 255, 255), pixel(rgba, width, 254, 254))
            self.assertEqual(pixel(last, 256, 255, 0), pixel(rgba, width, 254, 253))
            self.assertEqual(pixel(last, 256, 0, 255), pixel(rgba, width, 253, 254))

    def test_uv_affine_preserves_global_pixel_centers_and_boundaries(self):
        width, height = 510, 3
        with tempfile.TemporaryDirectory() as directory:
            layout = pages.write_pages(width, height, pattern(width, height), Path(directory))
            for page in layout["pages"]:
                x, y, content_width, content_height = page["source_rect"]
                scale, bias = page["uv_affine"]["scale"], page["uv_affine"]["bias"]
                for local_x, local_y in ((0, 0), (content_width - 1, content_height - 1)):
                    global_uv = ((x + local_x + 0.5) / width, (y + local_y + 0.5) / height)
                    self.assertAlmostEqual(global_uv[0] * scale[0] + bias[0], (1 + local_x + 0.5) / 256)
                    self.assertAlmostEqual(global_uv[1] * scale[1] + bias[1], (1 + local_y + 0.5) / 256)
                self.assertAlmostEqual((x / width) * scale[0] + bias[0], 1 / 256)
                self.assertAlmostEqual(((x + content_width) / width) * scale[0] + bias[0], (1 + content_width) / 256)

    def test_one_pixel_and_exact_interior_extent(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            layout = pages.write_pages(1, 1, b"\1\2\3\4", output)
            tile = (output / layout["pages"][0]["path"]).read_bytes()
            self.assertEqual(tile, b"\1\2\3\4" * 256 * 256)
            layout = pages.write_pages(254, 254, b"\1\2\3\4" * 254 * 254, output)
            self.assertEqual(layout["page_count"], 1)
            self.assertEqual(layout["pages"][0]["sha256"], hashlib.sha256(tile).hexdigest())

    def test_deterministic_hashes_paths_and_content_deduplication(self):
        rgba = b"\1\2\3\4" * 508 * 254
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            first, second = Path(first), Path(second)
            layout = pages.write_pages(508, 254, rgba, first)
            repeated = pages.write_pages(508, 254, rgba, second)
            self.assertEqual(json.dumps(layout, sort_keys=True), json.dumps(repeated, sort_keys=True))
            self.assertEqual(pages.write_pages(508, 254, rgba, first), layout)
            self.assertEqual(layout["source_sha256"], hashlib.sha256(rgba).hexdigest())
            self.assertEqual(layout["cooked_rgba_bytes"], 2 * 256 * 256 * 4)
            self.assertEqual(layout["unique_rgba_bytes"], 256 * 256 * 4)
            self.assertEqual(len(list((first / "assets").iterdir())), 1)
            cooked_hash = hashlib.sha256()
            for page in layout["pages"]:
                self.assertEqual(page["path"], "assets/" + page["sha256"] + ".rgba")
                tile = (first / page["path"]).read_bytes()
                self.assertEqual(hashlib.sha256(tile).hexdigest(), page["sha256"])
                self.assertEqual(tile, (second / page["path"]).read_bytes())
                cooked_hash.update(tile)
            self.assertEqual(layout["cooked_sha256"], cooked_hash.hexdigest())

    def test_invalid_inputs_fail_before_creating_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "unpublished"
            for width, height, data in ((0, 1, b""), (-1, 1, b""), (True, 1, b"\0" * 4),
                                        (1, 1.0, b"\0" * 4), (1, 1, b"\0" * 3),
                                        (1, 1, b"\0" * 5), (8192, 8192, b"")):
                with self.subTest(width=width, height=height), self.assertRaises(ValueError):
                    pages.write_pages(width, height, data, output)
                self.assertFalse(output.exists())
            # A very thin image can exceed the page limit while remaining below 64 MiB.
            width = 254 * 1024 + 1
            with self.assertRaisesRegex(ValueError, "1024"):
                pages.write_pages(width, 1, b"\0" * (width * 4), output)
            self.assertFalse(output.exists())

    def test_existing_corrupt_content_and_symlinks_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            layout = pages.write_pages(1, 1, b"\1\2\3\4", output)
            path = output / layout["pages"][0]["path"]
            path.write_bytes(b"corrupt")
            with self.assertRaisesRegex(ValueError, "different bytes"):
                pages.write_pages(1, 1, b"\1\2\3\4", output)
            path.unlink()
            target = output / "target"
            target.write_bytes(b"unchanged")
            path.symlink_to(target)
            with self.assertRaisesRegex(ValueError, "symlink"):
                pages.write_pages(1, 1, b"\1\2\3\4", output)
            self.assertEqual(target.read_bytes(), b"unchanged")


if __name__ == "__main__":
    unittest.main()
