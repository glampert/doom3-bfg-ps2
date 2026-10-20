#!/usr/bin/env python3
# ================================================================================================
# File: test_headless_runner.py
# Brief: Reject false production-loop acceptance from stale identities, crashes and incomplete cleanup.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/scripts"))
import run_headless as runner


RETAIL_STARTUP = ("Loaded resource file _common.resources\nLoaded resource file _ordered.resources\n"
                  "[D3BFG] MEMORY startup-foundation requested=100 backing=200 allocations=3 peak_requested=100 peak_backing=200\n"
                  "[D3BFG] MEMORY startup-decls requested=400 backing=500 allocations=6 peak_requested=400 peak_backing=500\n"
                  "Compiled 'script/doom_main.script': 7807.0 ms\n     Strings: 120, 14868 bytes\n"
                  "  Statements: 95386, 1907720 bytes\n   Functions: 3070, 386208 bytes\n   Variables: 206536 bytes\n"
                  " Static data: 3515932 bytes\n   Allocated: 4998796 bytes\n"
                  "[D3BFG] MEMORY startup-game requested=900 backing=1000 allocations=9 peak_requested=900 peak_backing=1000\n"
                  "[D3BFG] TAG startup-game DECL requested=300 backing=350 allocations=4 peak_requested=300\n"
                  "[D3BFG] TAG startup-game DECLTEXT requested=500 backing=600 allocations=5 peak_requested=500\n")


def completed(case="bounded"):
    status = "[D3BFG] HEADLESS frames={frame} game_frames=0 state=console console=1 game_nomap=1 render_device=0 sound_output=0 heap=10/20/3\n"
    log = RETAIL_STARTUP if case == "retail" else ""
    log += "game initialized.\nHEADLESS console ready;\nLOOP ready;\nrun_begin\n"
    log += status.format(frame=0) + status.format(frame=120)
    if case in ("bounded", "retail"):
        log += "HEADLESS frame limit reached frames=120\n"
    else:
        log += ']echo "run_command"\n]history\n]quit\n'
        log += "discarding invalid or oversized headless console line\n" * 2
    log += "------------ Game Shutdown\nHEADLESS shutdown complete frames=120\n"
    log += "COMMON stage=system shutdown\nHEADLESS memory baseline=1/2/3 final=1/2/3 peak=10/20 arena=30\nBOOT production shutdown complete\n"
    return log


class HeadlessAcceptance(unittest.TestCase):
    def test_completed_modes(self):
        for case in ("bounded", "console", "retail"):
            with self.subTest(case=case):
                self.assertTrue(runner.classify(completed(case), case, "run")[0])

    def test_crash_overrides_completion(self):
        for crash in ("[D3BFG] FATAL failure", "TLB Miss, pc=0x100000 addr=0x0 [load]"):
            with self.subTest(crash=crash):
                self.assertFalse(runner.classify(completed() + crash, "bounded", "run")[0])

    def test_headless_completion_cannot_hide_graphics_startup(self):
        self.assertFalse(runner.classify(completed() + "[D3BFG] GS ready NTSC 512x448\n", "bounded", "run")[0])

    def test_wrong_identity_and_state(self):
        for old, new in (("run_begin", "stale_begin"), ("console=1", "console=0"), ("game_nomap=1", "game_nomap=0"), ("sound_output=0", "sound_output=1")):
            with self.subTest(field=old):
                self.assertFalse(runner.classify(completed().replace(old, new), "bounded", "run")[0])

    def test_missing_or_duplicate_shutdown(self):
        for log in (completed().replace("BOOT production shutdown complete", ""), completed() + "HEADLESS shutdown complete frames=120\n"):
            self.assertFalse(runner.classify(log, "bounded", "run")[0])

    def test_frame_count_and_memory_recovery(self):
        for old, new in (("HEADLESS shutdown complete frames=120", "HEADLESS shutdown complete frames=119"), ("final=1/2/3", "final=1/2/4")):
            with self.subTest(field=old):
                self.assertFalse(runner.classify(completed().replace(old, new), "bounded", "run")[0])

    def test_live_input_requires_submission_and_rejection(self):
        for log in (completed("console").replace("]history", ""), completed("console").replace("discarding invalid or oversized headless console line", ""), completed("console") + "]quit xxxxxxxxx\n"):
            self.assertFalse(runner.classify(log, "console", "run")[0])

    def test_retail_requires_containers_script_and_checkpoints(self):
        log = completed("retail")
        for marker in ("Loaded resource file _common.resources", "Loaded resource file _ordered.resources",
                       "Compiled 'script/doom_main.script'", "MEMORY startup-decls"):
            with self.subTest(missing=marker):
                self.assertFalse(runner.classify(log.replace(marker, "missing"), "retail", "run")[0])
        repeated = log.replace("[D3BFG] MEMORY startup-game", "[D3BFG] MEMORY startup-decls requested=1 backing=1 allocations=1\n"
                               "[D3BFG] MEMORY startup-game", 1)
        self.assertFalse(runner.classify(repeated, "retail", "run")[0])
        self.assertFalse(runner.classify(log.replace("HEADLESS frame limit reached frames=120", ""), "retail", "run")[0])
        # An authored boot's checkpoints cannot stand in for the retail containers.
        self.assertFalse(runner.classify(completed("bounded"), "retail", "run")[0])

    def test_startup_memory_record(self):
        record = runner.startup_memory(completed("retail"))
        self.assertEqual(list(record["stages"]), list(runner.STARTUP_STAGES))
        self.assertEqual(record["stages"]["startup-game"], {"requested": 900, "backing": 1000, "allocations": 9})
        self.assertEqual([tag["tag"] for tag in record["tags"]], ["DECLTEXT", "DECL"])
        self.assertEqual(record["script"]["statements"], 95386)
        self.assertEqual(record["script"]["function_bytes"], 386208)
        self.assertEqual(record["script"]["compile_ms"], 7807.0)
        self.assertEqual(runner.startup_memory("no checkpoints"), {"stages": {}, "tags": [], "script": {}})

    def test_mirrored_logs_and_secondary_crash(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            log = "[D3BFG] BOOT production BEGIN\n" + completed()
            (output / "emulog.txt").write_text(log)
            (output / "stdout.txt").write_text(log)
            self.assertEqual(runner.read_log(output), log)
            (output / "stdout.txt").write_text(log + "[D3BFG] FATAL secondary stream\n")
            self.assertFalse(runner.classify(runner.read_log(output), "bounded", "run")[0])


if __name__ == "__main__":
    unittest.main()
