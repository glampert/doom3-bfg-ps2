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
    fall_z = (4.000, 3.861, 3.574, 3.164, 2.589, 2.251, 2.251, 2.251)
    fall_vz = (-8.192, -16.896, -25.600, -33.792, -42.496, -8.662, 0.000, 0.000)
    slide_y = (33.024, 34.112, 35.807, 36.832, 37.921, 39.010, 40.035, 41.124)
    player_x = (20.328, 20.872, 21.614, 21.749, 21.379, 21.183, 21.151, 21.151)
    player_vx = (20.480, 32.040, 43.600, -0.054, -21.760, -11.560, -1.960, 0.000)
    # Rounded native EE traces: the two jumps start at different 60 Hz millisecond phases.
    jump_z = (
        (2.352, 4.195, 6.010, 7.677, 9.110, 10.490, 11.722, 12.746, 13.690, 14.486,
         15.101, 15.610, 15.971, 16.176, 16.250, 16.176, 15.971, 15.610, 15.101,
         14.486, 13.690, 12.746, 11.722, 10.490, 9.110, 7.677, 6.010, 4.195, 2.352),
        (2.352, 4.306, 6.010, 7.677, 9.196, 10.490, 11.722, 12.805, 13.690, 14.486,
         15.135, 15.610, 15.971, 16.184, 16.250, 16.176, 15.954, 15.610, 15.101,
         14.444, 13.690, 12.746, 11.653, 10.490, 9.110, 7.583, 6.010, 4.195, 2.232))
    jump_vz = (
        (119.296, 111.104, 102.400, 93.696, 85.504, 76.800, 68.096, 59.904, 51.200,
         42.496, 34.304, 25.600, 16.896, 8.704, 0.000, -8.704, -16.896, -25.600,
         -34.304, -42.496, -51.200, -59.904, -68.096, -76.800, -85.504, -93.696,
         -102.400, -111.104, -119.296),
        (119.296, 110.592, 102.400, 93.696, 84.992, 76.800, 68.096, 59.392, 51.200,
         42.496, 33.792, 25.600, 16.896, 8.192, 0.000, -8.704, -17.408, -25.600,
         -34.304, -43.008, -51.200, -59.904, -68.608, -76.800, -85.504, -94.208,
         -102.400, -111.104, -119.808))
    for name in sorted(runner.REQUIRED_GAME_CHECKS):
        log += f"[D3BFG] CHECK {name} PASS\n"
    for cycle in range(3):
        log += (f"[D3BFG] GAME_COLLISION cycle={cycle} point=0.49219 point_z=0.250 "
                "box=0.42969 box_z=2.250 inside=1 outside=0 filtered=0\n")
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
            x = 512 * time / 1000 if frame < 3 else 21.75
            log += (f"[D3BFG] GAME_PHYSICS cycle={cycle} frame={frame} x={x:.3f} "
                    f"y=0.000 z=16.000 stopped={int(frame >= 3)}\n")
            grounded = int(frame >= 7)
            log += (f"[D3BFG] GAME_FALL cycle={cycle} frame={frame} x=-32.000 y=-32.000 "
                    f"z={fall_z[frame - 1]:.3f} vz={fall_vz[frame - 1]:.3f} "
                    f"ground={grounded} rest={grounded} floor={grounded}\n")
            vx = 512.000 if frame < 3 else -0.512
            log += (f"[D3BFG] GAME_SLIDE cycle={cycle} frame={frame} x={x:.3f} "
                    f"y={slide_y[frame - 1]:.3f} z=2.250 vx={vx:.3f} vy=64.000 "
                    f"ground=1 floor=1 blocked={int(frame >= 3)} move={int(frame >= 3)}\n")
            cmd = 127 if frame <= 4 else -127 if frame == 5 else 0
            log += (f"[D3BFG] GAME_PLAYER cycle={cycle} frame={frame} cmd={cmd} time={time} "
                    f"read={frame - 1} written={frame} pending=0 x={player_x[frame - 1]:.3f} "
                    f"y=-16.000 z=0.250 vx={player_vx[frame - 1]:.3f} floor=1\n")
        for frame in range(9, 89):
            flight = 0 if frame < 44 else 1
            offset = frame - (9 if flight == 0 else 44)
            airborne = offset < 29
            z = jump_z[flight][offset] if airborne else 0.250
            vz = jump_vz[flight][offset] if airborne else 0.000
            crouched = int(76 <= frame <= 84)
            buttons = 96 if crouched else 32 if frame <= 42 or frame == 44 else 0
            height = 38 if crouched else 74
            log += (f"[D3BFG] GAME_POSTURE cycle={cycle} frame={frame} buttons={buttons} "
                    f"time={frame * 1000 // 60} read={frame - 1} written={frame} pending=0 "
                    f"x=21.151 y=-16.000 z={z:.3f} vz={vz:.3f} height={height:.3f} "
                    f"floor={int(not airborne)} jumped={int(frame in (9, 44))} "
                    f"crouched={crouched} script=8\n")
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

    def test_game_requires_native_traces_movement_wall_stop_and_all_physics_ticks(self):
        result, log = complete()
        for bad in (
            "\n".join(line for line in log.splitlines() if "GAME_COLLISION cycle=1" not in line),
            log.replace("point=0.49219", "point=1.00000"),
            log.replace("box_z=2.250", "box_z=0.250"),
            log.replace("filtered=0", "filtered=1"),
            "\n".join(line for line in log.splitlines() if "GAME_PHYSICS cycle=1 frame=3" not in line),
            log.replace("x=8.192", "x=0.000"),
            log.replace("x=21.750", "x=24.000"),
            log.replace("y=0.000", "y=1.000"),
            log.replace("z=16.000", "z=0.000"),
            log.replace("stopped=1", "stopped=0"),
            log.replace("stopped=0", "stopped=1"),
        ):
            with self.subTest(output=bad):
                self.assertFalse(runner.classify_run("run1", "game", result, bad, None, False)[0])

    def test_game_requires_gravity_floor_rest_and_grounded_wall_sliding(self):
        result, log = complete()
        for bad in (
            "\n".join(line for line in log.splitlines() if "GAME_FALL cycle=1 frame=6" not in line),
            log.replace("z=3.861 vz=-16.896", "z=4.000 vz=0.000"),
            log.replace("vz=-42.496", "vz=-25.600"),
            log.replace("z=2.251", "z=1.900"),
            log.replace("ground=0 rest=0", "ground=1 rest=1"),
            log.replace("rest=1", "rest=0"),
            log.replace("vz=0.000 ground=1", "vz=-8.000 ground=1"),
            log.replace("floor=1", "floor=0"),
            "\n".join(line for line in log.splitlines() if "GAME_SLIDE cycle=2 frame=8" not in line),
            log.replace("y=41.124", "y=40.035"),
            log.replace("y=35.807", "y=45.000"),
            log.replace("z=2.250 vx=", "z=1.900 vx="),
            log.replace("vx=-0.512", "vx=512.000"),
            log.replace("vy=64.000", "vy=0.000"),
            log.replace("ground=1 floor=1 blocked=1", "ground=0 floor=1 blocked=1"),
            log.replace("blocked=1", "blocked=0"),
            log.replace("move=1", "move=0"),
        ):
            with self.subTest(output=bad):
                self.assertFalse(runner.classify_run("run1", "game", result, bad, None, False)[0])

    def test_game_requires_queued_player_input_acceleration_wall_reverse_and_friction(self):
        result, log = complete()
        for bad in (
            "\n".join(line for line in log.splitlines() if "GAME_PLAYER cycle=1 frame=4" not in line),
            log.replace("frame=5 cmd=-127", "frame=5 cmd=127"),
            log.replace("frame=6 cmd=0", "frame=6 cmd=-127"),
            log.replace("cmd=127 time=33", "cmd=127 time=16"),
            log.replace("read=3 written=4", "read=2 written=4"),
            log.replace("written=8", "written=9"),
            log.replace("pending=0 x=", "pending=1 x="),
            log.replace("x=20.328", "x=20.000"),
            log.replace("vx=32.040", "vx=20.480"),
            log.replace("x=21.749", "x=22.000"),
            log.replace("vx=-21.760", "vx=0.000"),
            log.replace("vx=-11.560", "vx=-21.760"),
            log.replace("z=0.250 vx=0.000", "z=0.250 vx=-1.960"),
            log.replace("y=-16.000", "y=-15.000"),
            log.replace("z=0.250 vx=", "z=-0.250 vx="),
            log.replace("vx=-1.960 floor=1", "vx=-1.960 floor=0"),
        ):
            with self.subTest(output=bad):
                self.assertFalse(runner.classify_run("run1", "game", result, bad, None, False)[0])

    def test_game_requires_jump_landing_release_crouch_and_standing_traces(self):
        result, log = complete()
        for bad in (
            "\n".join(line for line in log.splitlines() if "GAME_POSTURE cycle=1 frame=23" not in line),
            log + next(line for line in log.splitlines() if "GAME_POSTURE cycle=1 frame=23" in line) + "\n",
            log.replace("frame=9 buttons=32", "frame=9 buttons=0"),
            log.replace("frame=42 buttons=32", "frame=42 buttons=0"),
            log.replace("frame=43 buttons=0", "frame=43 buttons=32"),
            log.replace("frame=44 buttons=32", "frame=44 buttons=0"),
            log.replace("frame=76 buttons=96", "frame=76 buttons=64"),
            log.replace("frame=85 buttons=0", "frame=85 buttons=96"),
            log.replace("time=150 read=8", "time=149 read=8"),
            log.replace("read=43 written=44", "read=42 written=44"),
            log.replace("read=87 written=88 pending=0", "read=87 written=88 pending=1"),
            log.replace("z=2.352 vz=119.296", "z=0.250 vz=0.000"),
            log.replace("z=16.250", "z=17.000"),
            log.replace("vz=-119.296", "vz=119.296"),
            log.replace("z=0.250 vz=0.000 height=74.000 floor=1", "z=0.000 vz=-8.000 height=74.000 floor=1"),
            log.replace("height=74.000 floor=1", "height=74.000 floor=0"),
            log.replace("floor=1 jumped=0 crouched=0", "floor=1 jumped=1 crouched=0"),
            log.replace("floor=0 jumped=1", "floor=0 jumped=0"),
            log.replace("height=38.000", "height=74.000"),
            log.replace("height=74.000 floor=1 jumped=0 crouched=0", "height=38.000 floor=1 jumped=0 crouched=1"),
            log.replace("floor=1 jumped=0 crouched=1", "floor=1 jumped=1 crouched=1"),
            log.replace("crouched=1", "crouched=0"),
            log.replace("crouched=0 script=8", "crouched=0 script=9"),
            log.replace("x=21.151 y=-16.000 z=2.352", "x=22.000 y=-16.000 z=2.352"),
        ):
            with self.subTest(output=bad):
                self.assertFalse(runner.classify_run("run1", "game", result, bad, None, False)[0])

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
