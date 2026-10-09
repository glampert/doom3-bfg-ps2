#!/usr/bin/env python3
# ================================================================================================
# File: test_jpeg_decoder.py
# Brief: Require real-codec image/tables decoding and cleanup before each expected fatal image failure.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[3]


class JpegDecoderTests(unittest.TestCase):
    def run_case(self, case):
        result = subprocess.run([str(ROOT / "build/tests/jpeg_decoder_tests"), case],
                                capture_output=True, text=True, timeout=10)
        self.assertNotIn("Sanitizer", result.stderr)
        return result

    def test_valid_reuse_tables_alpha_and_ledger(self):
        result = self.run_case("valid")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("valid/reuse/tables/alpha/ledger PASS", result.stdout)

    def test_fatal_errors_release_decoder_and_output(self):
        for case, reason in (("invalid", "invalid input"), ("marker", "marker extends past input"),
                             ("oversize", "oversized dimensions"), ("truncated", "truncated input"),
                             ("signature", "Not a JPEG file")):
            with self.subTest(case=case):
                result = self.run_case(case)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn(reason, result.stdout)
                self.assertIn("JPEG cleanup PASS", result.stdout)
