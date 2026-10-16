#!/usr/bin/env python3
# ================================================================================================
# File: test_resource_unpack.py
# Brief: Exercise the standalone C++ resource extractor with synthetic containers and unsafe paths.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
EXECUTABLE = Path(os.environ.get("RESOURCE_UNPACK", ROOT / "build/tests/resource_unpack"))


def container(records):
    payload = bytearray()
    table = bytearray(struct.pack(">i", len(records)))
    for name, data in records:
        encoded = name.encode("ascii") if isinstance(name, str) else name
        table.extend(struct.pack("<i", len(encoded)) + encoded + struct.pack(">ii", 12 + len(payload), len(data)))
        payload.extend(data)
    return bytearray(struct.pack(">Iii", 0xD000000D, 12 + len(payload), len(table)) + payload + table)


def entry_positions(raw):
    cursor = struct.unpack_from(">i", raw, 4)[0] + 4
    result = []
    count = struct.unpack_from(">i", raw, cursor - 4)[0]
    for _ in range(count):
        length = struct.unpack_from("<i", raw, cursor)[0]
        result.append((cursor, cursor + 4 + length))
        cursor += 12 + length
    return result


class ResourceUnpack(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not EXECUTABLE.is_file():
            raise RuntimeError(f"Build the C++ resource unpacker first: missing {EXECUTABLE}")

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.source = self.directory / "input.resources"
        self.output = self.directory / "extracted"

    def test_failed_listing_output_is_not_success(self):
        self.source.write_bytes(container([("example/data", b"payload")]))
        with open(os.devnull, "rb") as invalid_output:
            result = subprocess.run([str(EXECUTABLE), "--list", str(self.source)],
                                    stdout=invalid_output, stderr=subprocess.PIPE, text=True, timeout=20)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("cannot write standard output", result.stderr)
        self.assertFalse(self.output.exists())

    def run_tool(self, *args, success=True):
        result = subprocess.run([str(EXECUTABLE), *(str(arg) for arg in args)], capture_output=True, text=True, timeout=20)
        self.assertNotIn("AddressSanitizer", result.stderr)
        self.assertNotIn("runtime error:", result.stderr)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result

    def test_extracts_all_bytes_with_native_case_and_slash_normalization(self):
        large = bytes(range(256)) * 769 + b"end"
        self.source.write_bytes(container([("Generated\\SWF\\Menu.BSWF", b"synthetic"),
                                           ("empty", b""), ("images/large.dat", large)]))
        self.run_tool("--output", self.output, self.source)
        self.assertEqual((self.output / "generated/swf/menu.bswf").read_bytes(), b"synthetic")
        self.assertEqual((self.output / "images/large.dat").read_bytes(), large)
        self.assertEqual((self.output / "empty").read_bytes(), b"")
        self.assertFalse(list(self.output.rglob("*.tmp")))

    def test_later_record_and_later_input_container_win_deterministically(self):
        self.source.write_bytes(container([("A", b"old"), ("a", b"same-container-last"), ("unique", b"x")]))
        second = self.directory / "second.resources"
        second.write_bytes(container([("A", b"later-container"), ("another", b"y")]))
        listing = self.run_tool("--list", self.source, second).stdout.splitlines()
        self.assertEqual([line.split("\t")[0] for line in listing], ["a", "another", "unique"])
        self.assertTrue(listing[0].endswith("second.resources"))
        self.assertFalse(self.output.exists())
        self.run_tool("--output", self.output, self.source, second)
        self.assertEqual((self.output / "a").read_bytes(), b"later-container")
        self.assertEqual((self.output / "unique").read_bytes(), b"x")
        other = self.directory / "other"
        self.run_tool("--output", other, self.source)
        self.assertEqual((other / "a").read_bytes(), b"same-container-last")

    def test_internal_repeated_separators_alias_same_output_and_keep_later_wins(self):
        self.source.write_bytes(container([("generated//sound///loop.dat", b"earlier"),
                                           ("generated/sound/loop.dat", b"later"),
                                           ("double\\\\inner/file", b"backslash")]))
        listing = self.run_tool("--list", self.source).stdout.splitlines()
        self.assertEqual([row.split("\t")[0] for row in listing], ["double/inner/file", "generated/sound/loop.dat"])
        self.run_tool("--output", self.output, self.source)
        self.assertEqual((self.output / "generated/sound/loop.dat").read_bytes(), b"later")
        self.assertEqual((self.output / "double/inner/file").read_bytes(), b"backslash")
        for name in ("//leading", "a/b/", "a//./b", "a///../b"):
            self.source.write_bytes(container([(name, b"refuse")]))
            with self.subTest(name=name):
                self.run_tool("--list", self.source, success=False)

    def test_every_truncated_prefix_is_rejected_without_output_creation(self):
        complete = container([("one", b"payload"), ("two", b"more")])
        for end in range(len(complete)):
            self.source.write_bytes(complete[:end])
            with self.subTest(end=end):
                self.run_tool("--output", self.output, self.source, success=False)
                self.assertFalse(self.output.exists())

    def test_header_table_counts_lengths_and_bounds_are_checked(self):
        complete = container([("name", b"1234")])
        table = struct.unpack_from(">i", complete, 4)[0]
        name, span = entry_positions(complete)[0]
        cases = [(0, ">I", 0), (4, ">i", -1), (8, ">i", 33 * 1024 * 1024),
                 (table, ">i", -1), (table, ">i", 1_000_001), (table, ">i", 2),
                 (name, "<i", 0), (name, "<i", -1), (name, "<i", 256),
                 (span, ">i", 0), (span, ">i", -1), (span, ">i", table),
                 (span + 4, ">i", -1), (span + 4, ">i", 999)]
        for offset, encoding, value in cases:
            changed = bytearray(complete)
            struct.pack_into(encoding, changed, offset, value)
            self.source.write_bytes(changed)
            with self.subTest(offset=offset, value=value):
                self.run_tool("--output", self.output, self.source, success=False)
                self.assertFalse(self.output.exists())
        changed = complete + b"x"
        struct.pack_into(">i", changed, 8, len(changed) - table)
        self.source.write_bytes(changed)
        self.assertIn("trailing", self.run_tool("--list", self.source, success=False).stderr)

    def test_payload_partial_overlap_is_rejected_and_identical_alias_is_allowed(self):
        original = container([("one", b"1234"), ("two", b"5678")])
        second_span = entry_positions(original)[1][1]
        partial = bytearray(original)
        struct.pack_into(">i", partial, second_span, 14)
        self.source.write_bytes(partial)
        self.assertIn("partially overlap", self.run_tool("--output", self.output, self.source, success=False).stderr)
        self.assertFalse(self.output.exists())
        identical = bytearray(original)
        struct.pack_into(">i", identical, second_span, 12)
        self.source.write_bytes(identical)
        self.run_tool("--output", self.output, self.source)
        self.assertEqual((self.output / "one").read_bytes(), b"1234")
        self.assertEqual((self.output / "two").read_bytes(), b"1234")

    def test_unsafe_archive_names_are_rejected_before_any_write(self):
        names = ["../outside", "/absolute", "C:/drive", "a/./b", "a/../b", "a\\..\\b", "a\x00b",
                 "a\nb", "name.", "name ", "con", "COM1.dat", b"\xff", "a" * 256, ""]
        for name in names:
            self.source.write_bytes(container([("valid/first", b"safe"), (name, b"unsafe")]))
            with self.subTest(name=name):
                self.run_tool("--output", self.output, self.source, success=False)
                self.assertFalse(self.output.exists())
            self.assertFalse((self.directory / "outside").exists())

    def test_all_inputs_are_parsed_before_any_payload_is_published(self):
        self.source.write_bytes(container([("first", b"valid")]))
        bad = self.directory / "bad.resources"
        bad.write_bytes(b"malformed")
        self.run_tool("--output", self.output, self.source, bad, success=False)
        self.assertFalse(self.output.exists())
        self.run_tool("--list", self.source, self.source, success=False)

    def test_preexisting_output_refuses_whole_operation_until_overwrite_is_explicit(self):
        self.source.write_bytes(container([("a-first", b"new"), ("z-existing", b"replacement")]))
        self.output.mkdir()
        existing = self.output / "z-existing"
        existing.write_bytes(b"original")
        self.run_tool("--output", self.output, self.source, success=False)
        self.assertEqual(existing.read_bytes(), b"original")
        self.assertFalse((self.output / "a-first").exists())
        self.run_tool("--overwrite", "--output", self.output, self.source)
        self.assertEqual(existing.read_bytes(), b"replacement")
        self.assertFalse(list(self.output.rglob("*.tmp")))

    def test_file_directory_collisions_are_rejected_before_creating_output(self):
        self.source.write_bytes(container([("a", b"file"), ("a/b", b"nested")]))
        self.run_tool("--output", self.output, self.source, success=False)
        self.assertFalse(self.output.exists())
        self.source.write_bytes(container([("a-first", b"first"), ("z/b", b"nested")]))
        self.output.mkdir()
        (self.output / "z").write_bytes(b"not a directory")
        self.run_tool("--output", self.output, self.source, success=False)
        self.assertFalse((self.output / "a-first").exists())

    def test_symlink_root_parent_and_destination_are_rejected(self):
        outside = self.directory / "outside"
        outside.mkdir()
        self.source.write_bytes(container([("sub/file", b"data")]))
        self.output.symlink_to(outside, target_is_directory=True)
        self.run_tool("--output", self.output, self.source, success=False)
        self.output.unlink()
        self.output.mkdir()
        (self.output / "sub").symlink_to(outside, target_is_directory=True)
        self.run_tool("--output", self.output, self.source, success=False)
        self.assertFalse((outside / "file").exists())
        (self.output / "sub").unlink()
        (self.output / "sub").mkdir()
        (outside / "file").write_bytes(b"keep")
        (self.output / "sub/file").symlink_to(outside / "file")
        self.run_tool("--overwrite", "--output", self.output, self.source, success=False)
        self.assertEqual((outside / "file").read_bytes(), b"keep")

    def test_input_directory_ancestor_and_hardlink_alias_cannot_overwrite_source(self):
        raw = container([("alias", b"replacement")])
        self.source.write_bytes(raw)
        self.run_tool("--overwrite", "--output", self.directory, self.source, success=False)
        self.assertEqual(self.source.read_bytes(), raw)
        self.output.mkdir()
        os.link(self.source, self.output / "alias")
        self.run_tool("--overwrite", "--output", self.output, self.source, success=False)
        self.assertEqual(self.source.read_bytes(), raw)

    def test_command_line_requires_explicit_output_and_rejects_incomplete_options(self):
        self.source.write_bytes(container([("valid", b"data")]))
        for options in ([], [self.source], ["--output"], ["--unknown", self.source],
                        ["--list", "--overwrite", self.source], ["--list", "--output", self.output, self.source]):
            with self.subTest(options=options):
                self.run_tool(*options, success=False)
        self.assertIn("Later input containers", self.run_tool("--help").stdout)


if __name__ == "__main__":
    unittest.main()
