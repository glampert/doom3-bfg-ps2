#!/usr/bin/env python3
# ================================================================================================
# File: test_game_runner.py
# Brief: Reject game results without real tick evidence and stale resident image identities.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / "tools/scripts/run_pcsx2_test.py"
SPEC = importlib.util.spec_from_file_location("game_runner", SCRIPT)
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)


def complete():
    result = {"schema": 1, "test_id": "run1", "manifest": "PASS", "platform": "PASS",
              "core": "SKIP", "game": "PASS"}
    log = "[D3BFG] RUN run1 BEGIN\n[D3BFG] STAGE game BEGIN\n"
    for name in sorted(runner.REQUIRED_GAME_CHECKS):
        log += f"[D3BFG] CHECK {name} PASS\n"
    for cycle in range(3):
        log += f"[D3BFG] GAME_EVENTS cycle={cycle} activation_ms=90 canceled_ms=120 posted=2\n"
        for frame in range(1, 9):
            time = frame * 1000 // 60
            log += (f"[D3BFG] GAME_TICK cycle={cycle} frame={frame} time={time} "
                    f"expected={time} think={frame} script={frame}\n")
            command = "fixture-activated" if frame in (4, 6) else "none"
            log += (f"[D3BFG] GAME_COMMAND cycle={cycle} frame={frame} command={command} "
                    f"expected={command} pending=0\n")
            alive = int(frame < 7)
            log += (f"[D3BFG] GAME_LIFETIME cycle={cycle} frame={frame} valid={alive} "
                    f"resolved={alive} named={alive}\n")
    log += "[D3BFG] STAGE game PASS\n[D3BFG] RESULT run1 PASS\n"
    return result, log


class GameRunnerTests(unittest.TestCase):
    def test_game_requires_every_native_check_and_all_reload_ticks(self):
        result, log = complete()
        self.assertTrue(runner.classify_run("run1", "game", result, log, None, False)[0])
        self.assertTrue(runner.classify_run("run1", "game", result, log + log, None, False)[0])
        for name in runner.REQUIRED_GAME_CHECKS:
            with self.subTest(name=name):
                missing = log.replace(f"[D3BFG] CHECK {name} PASS\n", "")
                self.assertFalse(runner.classify_run("run1", "game", result, missing, None, False)[0])
        missing = "\n".join(line for line in log.splitlines() if "cycle=2 frame=8" not in line)
        self.assertFalse(runner.classify_run("run1", "game", result, missing, None, False)[0])
        self.assertFalse(runner.classify_run("run1", "game", result, log.replace("script=8", "script=7"), None, False)[0])

    def test_game_rejects_missing_wrong_frame_repeated_and_unconsumed_commands(self):
        result, log = complete()
        for bad in (
            "\n".join(line for line in log.splitlines() if "GAME_COMMAND cycle=1 frame=4" not in line),
            log.replace("frame=4 command=fixture-activated", "frame=3 command=fixture-activated"),
            log.replace("frame=4 command=fixture-activated", "frame=5 command=fixture-activated"),
            log.replace("command=fixture-activated", "command=none"),
            log.replace("frame=5 command=none", "frame=5 command=fixture-activated"),
            log.replace("frame=6 command=fixture-activated", "frame=5 command=fixture-activated"),
            log.replace("frame=6 command=fixture-activated", "frame=7 command=fixture-activated"),
            log.replace("frame=8 command=none", "frame=8 command=fixture-activated"),
            log.replace("pending=0", "pending=1"),
        ):
            with self.subTest(output=bad):
                self.assertFalse(runner.classify_run("run1", "game", result, bad, None, False)[0])

    def test_game_requires_posted_deadlines_removal_and_invalidated_entity_handle(self):
        result, log = complete()
        for bad in (
            "\n".join(line for line in log.splitlines() if "GAME_EVENTS cycle=1" not in line),
            "\n".join(line for line in log.splitlines() if "GAME_LIFETIME cycle=1 frame=7" not in line),
            log.replace("activation_ms=90", "activation_ms=100"),
            log.replace("canceled_ms=120", "canceled_ms=140"),
            log.replace("posted=2", "posted=1"),
            log.replace("frame=6 valid=1 resolved=1 named=1", "frame=6 valid=0 resolved=0 named=0"),
            log.replace("frame=7 valid=0", "frame=7 valid=1"),
            log.replace("frame=8 valid=0 resolved=0", "frame=8 valid=0 resolved=1"),
            log.replace("frame=8 valid=0 resolved=0 named=0", "frame=8 valid=0 resolved=0 named=1"),
        ):
            with self.subTest(output=bad):
                self.assertFalse(runner.classify_run("run1", "game", result, bad, None, False)[0])

    def test_game_cannot_pass_stale_partial_crashed_or_timed_out(self):
        result, log = complete()
        for document, output, code, timeout in (
            (None, log, None, False), (dict(result, test_id="old"), log, None, False),
            (dict(result, game="FAIL"), log, None, False), (result, log, 1, False),
            (result, log, None, True), (result, log + "TLB Miss", None, False),
            (result, log.replace("STAGE game PASS", "STAGE game BEGIN"), None, False),
            (result, log.replace("[D3BFG] STAGE game BEGIN\n", ""), None, False),
        ):
            self.assertFalse(runner.classify_run("run1", "game", document, output, code, timeout)[0])

    def test_negative_game_requires_native_init_and_first_fatal_source(self):
        for mode, message in runner.GAME_FAILURES.items():
            begin = "[D3BFG] RUN run1 BEGIN\n[D3BFG] STAGE game BEGIN\n[D3BFG] CHECK game/native-init PASS\n"
            log = begin + f"[D3BFG] FATAL {message}1: invalid input\n"
            self.assertTrue(runner.classify_run("run1", mode, None, log, None, False)[0])
            for bad in (log.replace("run1", "old"), log.replace("CHECK game/native-init PASS", "init pending"),
                        begin + "[D3BFG] FATAL unrelated failure\n" + log,
                        log.replace(message, "script script/doom_main.script:"), log + "Bus error"):
                self.assertFalse(runner.classify_run("run1", mode, None, bad, None, False)[0])

    def test_resident_archive_rejects_changed_images_map_flags_and_incomplete_link(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "resident"
            source.mkdir()
            elf = source / "d3bfg.elf"
            symbols = source / "d3bfg_unstripped.elf"
            elf.write_bytes(b"runnable")
            symbols.write_bytes(b"symbols")
            for name in ("d3bfg.map", "objects.rsp", "report.txt"):
                (source / name).write_text(name)
            stamp = root / "flags.json"
            stamp.write_text('{"flags": "original"}')
            report = {"passed": True, "garbage_collection": False, "registrations": {"gameLocal": True},
                      "object_count": 344, "unresolved": [], "duplicate_definitions": [], "missing_map_objects": [],
                      "elf_sha256": runner.sha256(symbols), "runnable_elf_sha256": runner.sha256(elf),
                      "link_map_sha256": runner.sha256(source / "d3bfg.map"),
                      "flag_stamps": {str(stamp): {"flags": "original"}}}
            destination = root / "archive"
            destination.mkdir()
            (source / "report.json").write_text(json.dumps(report))
            self.assertIn("d3bfg.map", runner.archive_resident_artifacts(elf, elf, symbols, destination))
            for name in ("elf_sha256", "runnable_elf_sha256", "link_map_sha256", "registrations", "passed"):
                changed = dict(report, **{name: False if name == "passed" else {} if name == "registrations" else "stale"})
                (source / "report.json").write_text(json.dumps(changed))
                with self.assertRaises(ValueError):
                    runner.archive_resident_artifacts(elf, elf, symbols, destination)
            (source / "report.json").write_text(json.dumps(report))
            stamp.write_text('{"flags": "changed"}')
            with self.assertRaises(ValueError):
                runner.archive_resident_artifacts(elf, elf, symbols, destination)
