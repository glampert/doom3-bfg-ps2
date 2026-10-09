#!/usr/bin/env python3
# ================================================================================================
# File: test_resident_link.py
# Brief: Keep failed resident links, missing registrations and stale artifacts from passing the gate.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/scripts"))
import link_resident


class ResidentLinkTests(unittest.TestCase):
    def test_diagnostics_keep_unique_symbols_and_requestors(self):
        missing, duplicate = link_resident.diagnostics(
            "ld: build/a.o: in function `first':\n"
            "first.cpp:1: undefined reference to `renderSystem'\n"
            "ld: build/b.o: undefined reference to `renderSystem'\n"
            "ld: build/a.o: undefined reference to `crc32'\n"
            "ld: build/c.o: multiple definition of `game'; build/a.o: first defined here\n")
        self.assertEqual(duplicate, ["game"])
        self.assertEqual(missing, [
            {"symbol": "crc32", "category": "codecs", "requestors": ["build/a.o"]},
            {"symbol": "renderSystem", "category": "renderer", "requestors": ["build/a.o", "build/b.o"]},
        ])

    def fixture(self, root):
        obj = root / "resident.o"
        obj.write_bytes(b"object")
        return argparse.Namespace(
            compiler="ee-g++", configuration="debug", output=root / "out", linkfile=root / "linkfile",
            sdk_lib=root, objects=[str(obj)], sources=["src/ps2/system/resident_boot.cpp"])

    def link(self, args, *, status=0, log="", registrations=None, mapped=True, executable=True):
        def compiler(command, **kwargs):
            if command[0] == args.compiler:
                candidate = Path(command[command.index("-o") + 1])
                header = struct.pack("<16sHHIIIIIHHHHHH", b"\x7fELF\x01\x01" + b"\0" * 10,
                                     2 if executable else 1, 8, 1, 0x100000, 52, 0, 0, 52, 32, 1, 0, 0, 0)
                candidate.write_bytes(header + struct.pack("<IIIIIIII", 1, 0, 0x100000, 0x100000, 84, 128, 7, 128))
                (args.output / "d3bfg.map").write_text(args.objects[0] if mapped else "unrelated.o")
                return subprocess.CompletedProcess(command, status, stdout=log)
            if command[0] == "ee-strip":
                Path(command[command.index("-o") + 1]).write_bytes(b"stripped")
                return subprocess.CompletedProcess(command, 0)
            self.fail(f"unexpected process: {command}")

        names = link_resident.REGISTRATIONS if registrations is None else registrations
        def output(command, **kwargs):
            if command[0] == "ee-c++filt":
                return kwargs["input"]
            if command[0] == "ee-nm":
                return "\n".join(f"00100000 B {name}" for name in names)
            self.fail(f"unexpected query: {command}")

        with patch.object(link_resident.subprocess, "run", side_effect=compiler), \
             patch.object(link_resident.subprocess, "check_output", side_effect=output), \
             patch.object(link_resident, "source_identity", return_value={}):
            return link_resident.run(args)

    def test_failed_link_removes_stale_images_and_records_exit(self):
        with tempfile.TemporaryDirectory() as temp:
            args = self.fixture(Path(temp))
            args.output.mkdir()
            for name in ("d3bfg.elf", "d3bfg_unstripped.elf"):
                (args.output / name).write_bytes(b"stale")
            self.assertEqual(self.link(args, status=7, log="linker error"), 1)
            report = json.loads((args.output / "report.json").read_text())
            self.assertEqual(report["linker_returncode"], 7)
            self.assertFalse(report["passed"])
            self.assertFalse(list(args.output.glob("*.elf*")))

    def test_zero_exit_cannot_ignore_undefined_diagnostics(self):
        with tempfile.TemporaryDirectory() as temp:
            args = self.fixture(Path(temp))
            self.assertEqual(self.link(args, log="ld: a.o: undefined reference to `unknownService'"), 1)
            report = json.loads((args.output / "report.json").read_text())
            self.assertEqual(report["categories"], {"unclassified": 1})
            self.assertFalse(list(args.output.glob("*.elf*")))

    def test_missing_registration_or_map_input_or_executable_fails(self):
        for options in ({"registrations": ["gameLocal"]}, {"mapped": False}, {"executable": False}):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as temp:
                args = self.fixture(Path(temp))
                self.assertEqual(self.link(args, **options), 1)
                self.assertFalse(list(args.output.glob("*.elf*")))

    def test_registered_whole_object_executable_can_pass(self):
        with tempfile.TemporaryDirectory() as temp:
            args = self.fixture(Path(temp))
            self.assertEqual(self.link(args), 0)
            report = json.loads((args.output / "report.json").read_text())
            self.assertTrue(report["passed"])
            self.assertEqual(report["loaded_memory_bytes"], 128)
            self.assertTrue((args.output / "d3bfg.elf").is_file())
            self.assertNotIn("--gc-sections", " ".join(report["command"]))

    def test_duplicate_and_classic_inputs_are_rejected_before_link(self):
        with tempfile.TemporaryDirectory() as temp:
            args = self.fixture(Path(temp))
            args.objects *= 2
            args.sources *= 2
            with self.assertRaisesRegex(ValueError, "duplicate"):
                link_resident.run(args)
            args = self.fixture(Path(temp))
            args.sources = ["doomclassic/main.cpp"]
            with self.assertRaisesRegex(ValueError, "Classic"):
                link_resident.run(args)


if __name__ == "__main__":
    unittest.main()
