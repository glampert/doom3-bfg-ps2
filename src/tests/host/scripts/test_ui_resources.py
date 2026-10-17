#!/usr/bin/env python3
# ================================================================================================
# File: test_ui_resources.py
# Brief: Reject malformed resource tables and verify native duplicate precedence without game data.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/scripts"))
from ui_resources import Container, ResourceCatalog, ResourceError, logical_name


def container(records):
    payload = bytearray()
    table = bytearray(struct.pack(">i", len(records)))
    for name, data in records:
        encoded = name.encode("ascii")
        table.extend(struct.pack("<i", len(encoded)) + encoded + struct.pack(">ii", 12 + len(payload), len(data)))
        payload.extend(data)
    return struct.pack(">Iii", 0xD000000D, 12 + len(payload), len(table)) + payload + table


class NativeResourceTables(unittest.TestCase):
    def test_load_order_and_same_container_duplicates_match_native_lookup(self):
        with tempfile.TemporaryDirectory() as directory:
            common, ordered = (Path(directory) / name for name in ("common.resources", "ordered.resources"))
            common.write_bytes(container([("Generated\\SWF/A.bswf", b"old"), ("same", b"identical")]))
            ordered.write_bytes(container([("generated/swf/a.bswf", b"first"), ("generated/swf/a.bswf", b"last"), ("same", b"identical")]))
            catalog = ResourceCatalog([common, ordered])
            self.assertEqual(catalog.names(), ["generated/swf/a.bswf", "same"])
            self.assertEqual(catalog.read("GENERATED\\SWF/A.BSWF"), b"last")
            info = catalog.describe("generated/swf/a.bswf")
            self.assertEqual(info["selected"]["ordinal"], 1)
            self.assertEqual(len(info["copies"]), 3)
            self.assertFalse(info["duplicate_content_identical"])
            self.assertTrue(catalog.describe("same")["duplicate_content_identical"])
            with self.assertRaises(ResourceError):
                catalog.read("missing")
            with self.assertRaises(ResourceError):
                catalog.read("same", max_bytes=1)
            with self.assertRaises(ResourceError):
                ResourceCatalog([common, common])

    def test_every_truncated_prefix_is_rejected(self):
        complete = container([("a/b", b"payload"), ("c/d", b"second")])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "table.resources"
            for end in range(len(complete)):
                path.write_bytes(complete[:end])
                with self.subTest(end=end), self.assertRaises(ResourceError):
                    Container(path)

    def test_header_counts_lengths_offsets_and_names_are_checked(self):
        source = container([("a", b"1234")])
        table = 16
        corruptions = [(0, ">I", 0), (4, ">i", -1), (8, ">i", 33 * 1024 * 1024),
                       (table, ">i", -1), (table, ">i", 2), (table + 4, "<i", -1),
                       (table + 4, "<i", 256), (table + 9, ">i", 0),
                       (table + 9, ">i", table), (table + 13, ">i", -1),
                       (table + 13, ">i", 999)]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "table.resources"
            for offset, encoding, value in corruptions:
                changed = bytearray(source)
                struct.pack_into(encoding, changed, offset, value)
                path.write_bytes(changed)
                with self.subTest(offset=offset, value=value), self.assertRaises(ResourceError):
                    Container(path)
            path.write_bytes(container([("../outside", b"bad")]))
            with self.assertRaises(ResourceError):
                Container(path)

    def test_payload_overlap_and_container_changes_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "table.resources"
            raw = bytearray(container([("a", b"1234"), ("b", b"5678")]))
            struct.pack_into(">i", raw, 20 + 4 + 13 + 5, 14)
            path.write_bytes(raw)
            with self.assertRaisesRegex(ResourceError, "partially overlap"):
                Container(path)
            path.write_bytes(container([("a", b"data")]))
            parsed = Container(path)
            path.write_bytes(b"changed")
            with self.assertRaisesRegex(ResourceError, "changed"):
                parsed.read(parsed.entries[0])

    def test_paths_cannot_escape_the_logical_root(self):
        self.assertEqual(logical_name("GENERATED//sound/a"), "generated/sound/a")
        for name in ("", "/absolute", "//server/file", "a/", "C:/absolute", "a/./b", "a/../b", "a\x00b", "a\nb", "é", "a" * 256):
            with self.subTest(name=name), self.assertRaises(ResourceError):
                logical_name(name)


if __name__ == "__main__":
    unittest.main()
