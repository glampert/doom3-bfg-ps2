#!/usr/bin/env python3
# ================================================================================================
# File: test_filesystem.py
# Brief: Run authored filesystem probes in a disposable root, never against retail assets.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]


class FilesystemTests(unittest.TestCase):
    def test_real_filesystem_services(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([str(ROOT / "build/tests/filesystem_tests")], cwd=directory,
                                    capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("Sanitizer", result.stderr)
        for check in ("paths", "stdio", "directories-errors"):
            self.assertIn(f"filesystem/{check} PASS", result.stdout)
