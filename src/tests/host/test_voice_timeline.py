#!/usr/bin/env python3
# ================================================================================================
# File: test_voice_timeline.py
# Brief: Require deterministic shared playback and fatal runtime validation with assertions enabled and disabled.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[3]


class VoiceTimelineTests(unittest.TestCase):
    def run_case(self, assertions, case):
        result = subprocess.run([str(ROOT / f"build/tests/voice_timeline_tests_{assertions}"), case],
                                capture_output=True, text=True, timeout=10)
        self.assertNotIn("Sanitizer", result.stderr)
        return result

    def test_completion_pause_pitch_loops_seek_extreme_clock_and_cadence(self):
        for assertions in (0, 1):
            with self.subTest(assertions=assertions):
                result = self.run_case(assertions, "valid")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("CHECK audio/voice-timeline PASS", result.stdout)

    def test_invalid_inputs_remain_fatal_in_release(self):
        for assertions in (0, 1):
            for case, message in (
                ("unconfigured", "requires configured samples"),
                ("sample", "invalid logical voice sample timing"),
                ("clock", "clock moved backwards"),
                ("offset", "start offset is negative"),
                ("negative-pitch", "pitch must be between 0 and 8"),
                ("large-pitch", "pitch must be between 0 and 8"),
                ("nan-pitch", "pitch must be between 0 and 8"),
                ("inf-pitch", "pitch must be between 0 and 8"),
            ):
                with self.subTest(assertions=assertions, case=case):
                    result = self.run_case(assertions, case)
                    self.assertLess(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn(message, result.stdout)
