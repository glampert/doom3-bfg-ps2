#!/usr/bin/env python3
# ================================================================================================
# File: test_codecs.py
# Brief: Require the shared codec fixtures and allocation-failure cleanup to pass under sanitizers.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[3]


class CodecTests(unittest.TestCase):
    def test_streams_crc_errors_jpeg_and_ledgers(self):
        result = subprocess.run([str(ROOT / "build/tests/codec_tests")], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("Sanitizer", result.stderr)
        for check in ("crc-known-vector", "zlib-streaming-ledger", "zlib-error-cleanup", "jpeg-rgba-tables-ledger"):
            self.assertIn(f"[D3BFG] CHECK codecs/{check} PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
