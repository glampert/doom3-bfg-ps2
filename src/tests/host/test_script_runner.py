#!/usr/bin/env python3
# ================================================================================================
# File: test_script_runner.py
# Brief: Reject stale identities, missing cleanup, unexpected errors and watchdogs in script probes.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/scripts"))
from run_script_tests import classify


class ScriptRunnerTests(unittest.TestCase):
    def test_valid_requires_matching_identity_and_no_fatal(self):
        log = "[D3BFG] SCRIPT BEGIN token valid\n[D3BFG] SCRIPT RESULT token PASS\n"
        self.assertTrue(classify("token", "valid", log, None, False))
        self.assertFalse(classify("stale", "valid", log, None, False))
        self.assertFalse(classify("token", "valid", log + "[D3BFG] FATAL error", None, False))

    def test_negative_requires_ordered_cleanup_and_matching_diagnostic(self):
        begin = "[D3BFG] SCRIPT BEGIN token syntax\n"
        cleanup = "[D3BFG] SCRIPT CLEANUP token PASS\n"
        fatal = '[D3BFG] FATAL script fixture.script:2: Unknown value "unknown"\n'
        self.assertTrue(classify("token", "syntax", begin + cleanup + fatal, None, False))
        for log in (begin + fatal, begin + fatal + cleanup, begin + cleanup + fatal.replace("unknown", "other"),
                    begin + cleanup + fatal.replace(":2:", ":0:"), begin + cleanup + fatal.replace("fixture.script", "wrong.script"), begin + cleanup + fatal + "TLB Miss"):
            self.assertFalse(classify("token", "syntax", log, None, False))

    def test_watchdog_and_process_crash_cannot_pass(self):
        log = "[D3BFG] SCRIPT BEGIN token valid\n[D3BFG] SCRIPT RESULT token PASS\n"
        self.assertFalse(classify("token", "valid", log, None, True))
        self.assertFalse(classify("token", "valid", log, -11, False))


if __name__ == "__main__":
    unittest.main()
