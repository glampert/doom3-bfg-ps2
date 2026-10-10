#!/usr/bin/env python3
# ================================================================================================
# File: test_pcm_wave.py
# Brief: Verify bounded PCM parsing and the independently authored emulator fixtures under sanitizers.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("pcm_runner", ROOT / "src/tools/scripts/run_pcsx2_test.py")
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)


class PcmWaveTests(unittest.TestCase):
    def run_case(self, *args):
        result = subprocess.run([str(ROOT / "build/tests/pcm_wave_tests"), *map(str, args)],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("Sanitizer", result.stderr)
        return result.stdout

    def test_pcm_layout_timing_signed_peak_and_chunk_order(self):
        self.run_case("valid")

    def test_corrupt_headers_lengths_frames_and_short_reads(self):
        self.run_case("malformed")

    def test_deterministic_corruption_stays_inside_reader_and_payload(self):
        self.run_case("mutations")

    def test_staged_fixtures_match_real_parser(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / "audio"
            runner.stage_audio_fixtures(directory)
            for name, expected in (
                ("mono", "rate=11025 channels=1 frames=11025 bytes=22050 duration=1000"),
                ("stereo", "rate=44100 channels=2 frames=22050 bytes=88200 duration=500"),
                ("unsupported", "rejected: unsupported PCM format"),
                ("truncated", "rejected: RIFF length mismatch"),
                ("chunk", "rejected: chunk exceeds RIFF"),
                ("budget", "rejected: PCM data exceeds fixture budget"),
            ):
                with self.subTest(name=name):
                    self.assertIn(expected, self.run_case("fixture", directory / f"{name}.wav"))
