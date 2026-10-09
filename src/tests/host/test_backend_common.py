#!/usr/bin/env python3
# ================================================================================================
# File: test_backend_common.py
# Brief: Check stdout completeness and assertion enable/disable semantics, including fatal termination.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[3]


class BackendCommonTests(unittest.TestCase):
    def run_mode(self, assertions: int, mode: str):
        return subprocess.run(
            [str(ROOT / f"build/tests/common_tests_{assertions}"), mode],
            capture_output=True, text=True, timeout=10,
        )

    def test_channels_and_forwarding_do_not_truncate(self):
        result = self.run_mode(1, "log")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertEqual(result.stdout,
            "fragment:100% 7\n[D3BFG] WARNING warning 100%\n[D3BFG] ERROR error 9\n"
            + "x" * 4096 + "\nreturned\n")

    def test_conditions_run_once_and_success_skips_message(self):
        result = self.run_mode(1, "evaluation")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "2 0\nreturned\n")

    def test_disabled_assertions_skip_conditions_and_messages(self):
        result = self.run_mode(0, "evaluation")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "0 0\nreturned\n")
        for mode in ("assert", "message"):
            with self.subTest(mode=mode):
                result = self.run_mode(0, mode)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "returned\n")

    def test_assertions_use_fatal_handler_with_source_location(self):
        for mode, message in (("assert", "false"), ("message", "false: message 100%")):
            with self.subTest(mode=mode):
                result = self.run_mode(1, mode)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f"[D3BFG] FATAL Assert Failed: {message}", result.stdout)
                self.assertRegex(result.stdout, r"common_tests\.cpp:\d+\)\n$")
                self.assertNotIn("returned", result.stdout)

    def test_fatal_and_heap_fail_flush_stdout_with_assertions_disabled(self):
        for mode, output in (("fatal", "fatal 100% 7"), ("heap-fail", "heap: allocation 100%")):
            with self.subTest(mode=mode):
                result = self.run_mode(0, mode)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, f"[D3BFG] FATAL {output}\n")
                self.assertNotIn("returned", result.stdout)

    def test_class_allocation_failures_are_fatal_with_assertions_disabled(self):
        for mode, output in (("class-fail", "idClass allocation failed: size="),
                             ("class-underflow", "idClass allocation counters underflow")):
            with self.subTest(mode=mode):
                result = self.run_mode(0, mode)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f"[D3BFG] FATAL {output}", result.stdout)
                self.assertNotIn("returned", result.stdout)

    def test_script_cleanup_precedes_fatal_with_location(self):
        result = self.run_mode(0, "script-cleanup")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "parser\nprogram\n[D3BFG] FATAL script fixture%name.script:17: unexpected 100% token\n")

    def test_script_diagnostics_are_bounded(self):
        result = self.run_mode(0, "script-long")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "[D3BFG] FATAL script long.script:3: " + "x" * 1023 + "\n")

    def test_script_normal_scope_does_not_run_fatal_cleanup(self):
        result = self.run_mode(1, "script-normal")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "returned\n")


if __name__ == "__main__":
    unittest.main()
