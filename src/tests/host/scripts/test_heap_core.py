#!/usr/bin/env python3
# ================================================================================================
# File: test_heap_core.py
# Brief: Each seeded heap defect must end the tracking build's process in its own fatal.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[4]


class HeapCoreTests(unittest.TestCase):
    def run_mode(self, mode: str):
        return subprocess.run(
            [str(ROOT / "build/tests/heap_core_tests_1"), mode],
            capture_output=True, text=True, timeout=30,
        )

    def test_defects_are_fatal(self):
        cases = (
            ("small-write-after-free", r"64-byte slot 0x[0-9a-f]+ was written after it was freed \(word 10 is 0xdeadbe01"),
            ("large-write-after-free", r"5024-byte block 0x[0-9a-f]+ was written after it was freed \(payload word 25 is 0xdeadbe01\)"),
            ("small-double-free", r"double free of 64-byte slot"),
            ("large-double-free", r"0x[0-9a-f]+ is not a live allocation"),
        )
        for mode, message in cases:
            with self.subTest(mode=mode):
                result = self.run_mode(mode)
                self.assertNotEqual(result.returncode, 0)
                self.assertRegex(result.stdout, r"\[D3BFG\] FATAL heap: " + message)
                self.assertNotIn("returned", result.stdout)

    def test_unknown_mode_is_rejected(self):
        self.assertEqual(self.run_mode("none").returncode, 2)


if __name__ == "__main__":
    unittest.main()
