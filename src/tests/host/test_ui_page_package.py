#!/usr/bin/env python3
# ================================================================================================
# File: test_ui_page_package.py
# Brief: Check streamed native UIPG headers, row-major page ownership and malformed layout rejection.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "src/tools/scripts"))
from ui_pages import write_pages
from ui_page_package import fnv1a32, write_page_package


def pixels(width, height):
    return bytes(value for y in range(height) for x in range(width)
                 for value in (x % 256, y % 256, (x ^ y) % 256, 255))


class RuntimePagePackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.pages = self.root / "pages"
        self.output = self.root / "output"

    def test_native_header_offsets_payloads_and_fnv_are_exact(self):
        layout = write_pages(1, 1, bytes(4), self.pages)
        record = write_page_package(layout, self.pages, self.output)
        data = (self.output / record["path"]).read_bytes()
        self.assertEqual(data[:4], b"UIPG")
        self.assertEqual(struct.unpack_from("<8I", data), (0x47504955, 1, 1, 1, 256, 1, 1, 0))
        offset, checksum = struct.unpack_from("<II", data, 32)
        self.assertEqual(offset, 40)
        # For all-zero bytes FNV1a is independently offset_basis * prime**N mod 2**32.
        self.assertEqual(checksum, 2166136261 * pow(16777619, 262144, 2**32) % (2**32))
        self.assertEqual(data[offset:], bytes(262144))
        self.assertEqual(len(data), 32 + 8 + 262144)
        self.assertEqual(record["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual(record["path"], "assets/" + record["sha256"] + ".uip")
        self.assertEqual((record["page_count"], record["page_bytes"], record["data_offset"]), (1, 262144, 40))

    def test_fnv_known_vectors_and_chunk_continuation(self):
        self.assertEqual(fnv1a32(b""), 0x811C9DC5)
        self.assertEqual(fnv1a32(b"a"), 0xE40C292C)
        self.assertEqual(fnv1a32(b"foobar"), 0xBF9CF968)
        self.assertEqual(fnv1a32(b"bar", fnv1a32(b"foo")), 0xBF9CF968)

    def test_npot_row_major_pages_are_copied_without_resampling(self):
        layout = write_pages(255, 257, pixels(255, 257), self.pages)
        record = write_page_package(layout, self.pages, self.output)
        data = (self.output / record["path"]).read_bytes()
        self.assertEqual(struct.unpack_from("<8I", data), (0x47504955, 1, 255, 257, 256, 1, 4, 0))
        self.assertEqual(len(data), 32 + 8 * 4 + 262144 * 4)
        for index, page in enumerate(layout["pages"]):
            offset, checksum = struct.unpack_from("<II", data, 32 + index * 8)
            self.assertEqual(offset, 64 + index * 262144)
            original = (self.pages / page["path"]).read_bytes()
            self.assertEqual(data[offset:offset + 262144], original)
            self.assertEqual(checksum, fnv1a32(original))

    def test_deterministic_reruns_stream_without_whole_file_reads(self):
        layout = write_pages(255, 3, pixels(255, 3), self.pages)
        with patch.object(Path, "read_bytes", side_effect=AssertionError("whole-file allocation")):
            first = write_page_package(layout, self.pages, self.output)
            second = write_page_package(layout, self.pages, self.output)
        self.assertEqual(first, second)
        self.assertEqual(len(list((self.output / "assets").iterdir())), 1)
        separate = write_page_package(layout, self.pages, self.root / "second")
        self.assertEqual(first, separate)
        self.assertEqual((self.output / first["path"]).read_bytes(), (self.root / "second" / separate["path"]).read_bytes())

    def test_truncated_oversized_and_hash_bad_pages_publish_no_package(self):
        layout = write_pages(1, 1, bytes(4), self.pages)
        source = self.pages / layout["pages"][0]["path"]
        for data in (bytes(262143), bytes(262145), b"\1" + bytes(262143)):
            source.write_bytes(data)
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                write_page_package(layout, self.pages, self.output)
            self.assertEqual(list(self.output.glob("assets/*.uip")), [])
            self.assertEqual(list(self.output.glob("assets/.uip-*")), [])

    def test_shape_counts_rectangles_and_affine_contamination_are_rejected(self):
        original = write_pages(255, 3, pixels(255, 3), self.pages)
        changes = [("source_width", 0), ("source_height", 16385), ("page_width", 128), ("gutter", 0),
                   ("page_count", 1), ("version", 2), ("columns", True), ("source_rgba_bytes", 1),
                   ("cooked_rgba_bytes", 1), ("unique_rgba_bytes", 1), ("source_sha256", "bad")]
        for name, value in changes:
            layout = copy.deepcopy(original)
            layout[name] = value
            with self.subTest(name=name), self.assertRaises(ValueError):
                write_page_package(layout, self.pages, self.output)
        for name, value in (("index", 1), ("source_rect", [0, 0, 253, 3]), ("interior_rect", [0, 0, 254, 3]),
                            ("atlas_origin", [1, 0]), ("path", "../escape.rgba"), ("bytes", 1),
                            ("uv_affine", {"scale": [1, 1], "bias": [0, 0]})):
            layout = copy.deepcopy(original)
            layout["pages"][0][name] = value
            with self.subTest(page_field=name), self.assertRaises(ValueError):
                write_page_package(layout, self.pages, self.output)
        self.assertFalse(self.output.exists())

    def test_page_count_and_decoded_size_caps_are_checked_before_files(self):
        original = write_pages(1, 1, bytes(4), self.pages)
        for width, height in ((8192, 8192), (16384, 1024), (1, 16385)):
            layout = copy.deepcopy(original)
            layout["source_width"], layout["source_height"] = width, height
            with self.subTest(width=width, height=height), self.assertRaises(ValueError):
                write_page_package(layout, self.pages, self.output)
        self.assertFalse(self.output.exists())

    def test_cross_atlas_page_mix_cannot_hide_behind_individual_hashes(self):
        layout = write_pages(255, 3, pixels(255, 3), self.pages)
        first, second = layout["pages"]
        self.assertNotEqual(first["sha256"], second["sha256"])
        # The substituted file has a valid individual SHA/path but changes the
        # ordered layout hash, so a partial valid package must never be published.
        first["path"], first["sha256"] = second["path"], second["sha256"]
        layout["unique_rgba_bytes"] = 262144
        with self.assertRaisesRegex(ValueError, "ordered page SHA256"):
            write_page_package(layout, self.pages, self.output)
        self.assertEqual(list(self.output.glob("assets/*.uip")), [])
        self.assertEqual(list(self.output.glob("assets/.uip-*")), [])

    def test_symlink_source_and_corrupt_existing_package_are_rejected(self):
        layout = write_pages(1, 1, bytes(4), self.pages)
        record = write_page_package(layout, self.pages, self.output)
        destination = self.output / record["path"]
        destination.write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "existing content-addressed"):
            write_page_package(layout, self.pages, self.output)
        self.assertEqual(destination.read_bytes(), b"corrupt")
        source = self.pages / layout["pages"][0]["path"]
        target = self.root / "same-pixels"
        source.rename(target)
        source.symlink_to(target)
        with self.assertRaisesRegex(ValueError, "escapes"):
            write_page_package(layout, self.pages, self.output)

    def test_pipeline_configuration_rejects_non_english_sources(self):
        from inventory_retail_ui import _configuration
        config = {"schema_version": 1, "roots": ["generated/swf/shell.bswf"],
                  "language_ids": ["#str_test"], "material_names": ["_white"],
                  "source_language": "strings/french.lang", "page_config": {
                      "extent": 256, "gutter": 1, "pixel_cache_budget_bytes": 2097152, "resampling": "none"}}
        path = self.root / "profile.json"
        path.write_text(json.dumps(config))
        with self.assertRaisesRegex(ValueError, "English strings/english.lang only"):
            _configuration(path)
        config["source_language"] = "strings/english.lang"
        path.write_text(json.dumps(config))
        self.assertEqual(_configuration(path)[0]["source_language"], "strings/english.lang")


if __name__ == "__main__":
    unittest.main()
