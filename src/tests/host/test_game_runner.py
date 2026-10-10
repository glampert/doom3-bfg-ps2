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
    native_x = (-31.595, -30.960, -30.156, -29.072, -28.161, -27.457, -26.882, -26.481, -26.257, -26.192, -26.192)
    native_vx = (23.800, 37.400, 50.200, 63.800, 53.600, 44.000, 33.800, 23.600, 14.000, 3.800, 0.000)
    # Native idPlayer traces, including early contact and the following overclip tick.
    native_jump_z = (
        5.232, 10.226, 14.912, 19.042, 23.130, 26.910, 30.186, 33.368, 36.242, 38.666,
        40.942, 42.910, 44.481, 45.851, 46.912, 47.630, 48.094, 48.250, 48.115, 47.673,
        46.922, 45.935, 44.586, 42.930, 41.090, 38.835, 36.272, 33.579, 30.419, 26.950,
        23.404, 19.337, 14.962, 10.564, 5.591, 0.310)
    native_jump_vz = (
        302.844, 284.722, 266.600, 249.544, 231.422, 213.300, 196.244, 178.122, 160.000,
        142.944, 124.822, 106.700, 89.644, 71.522, 53.400, 36.344, 18.222, 0.100,
        -16.956, -35.078, -53.200, -70.256, -88.378, -106.500, -123.556, -141.678,
        -159.800, -176.856, -194.978, -213.100, -230.156, -248.278, -266.400,
        -283.456, -301.578, -319.700)
    crouch_x = (-25.987, -25.712, -25.379, -25.014, -24.800, -24.759, -24.759,
                -24.759, -24.759, -24.759, -24.759, -24.759, -24.759, -24.759, -24.759)
    crouch_vx = (12.800, 16.200, 19.600, 22.800, 12.600, 2.400, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    crouch_eye = (63.320, 59.248, 55.706, 52.624, 49.943, 47.611, 45.581, 43.816,
                  42.280, 45.623, 48.532, 51.063, 53.265, 55.180, 56.847)
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
        for frame in range(89, 100):
            tick = frame - 88
            log += (f"[D3BFG] GAME_NATIVE_PLAYER cycle={cycle} frame={frame} cmd={127 if frame <= 92 else 0} "
                    f"time={frame * 1000 // 60} read={tick - 1} written={tick} pending=0 "
                    f"x={native_x[tick - 1]:.3f} y=0.000 z=0.250 vx={native_vx[tick - 1]:.3f} "
                    f"floor=1 health=100 script={tick} constructs=1\n")
        for frame in range(100, 196):
            tick = frame - 88
            airborne = frame < 135 or 142 <= frame < 177
            crouched = 181 <= frame <= 189
            if frame <= 135 or 142 <= frame <= 177:
                offset = frame - (100 if frame < 142 else 142)
                z = native_jump_z[offset] + (0 if frame < 142 else 0.060)
                vz = native_jump_vz[offset]
            else:
                z = 0.310 if frame < 142 else 0.370 if frame < 181 else 0.250
                vz = 0.289 if frame in (136, 178) else 0
            x = crouch_x[frame - 181] if frame >= 181 else -26.192
            vx = crouch_vx[frame - 181] if frame >= 181 else 0
            eye = crouch_eye[frame - 181] if frame >= 181 else 68
            state = "FixtureAir" if airborne else "FixtureCrouch" if crouched else "FixtureIdle"
            transitions = (2 if frame < 135 else 3 if frame < 142 else 4 if frame < 177
                           else 5 if frame < 181 else 6 if frame < 190 else 7)
            soft_ticks = 0 if frame < 135 else 1 if frame == 135 else 2 if frame < 177 else 3 if frame == 177 else 4
            buttons = 96 if crouched else 32 if frame <= 140 or frame == 142 else 0
            log += (f"[D3BFG] GAME_PLAYER_STATE cycle={cycle} frame={frame} buttons={buttons} "
                    f"cmd={127 if 181 <= frame <= 184 else 0} time={frame * 1000 // 60} "
                    f"read={tick - 1} written={tick} pending=0 x={x:.3f} y=0.000 z={z:.3f} "
                    f"vx={vx:.3f} vz={vz:.3f} height={38 if crouched else 74:.3f} eye={eye:.3f} "
                    f"floor={int(not airborne)} jumped={int(frame in (100, 142))} crouched={int(crouched)} "
                    f"soft={int(frame in (135, 136, 177, 178))} health=100 script={tick} "
                    f"state={2 if airborne else 3 if crouched else 1} transitions={transitions} "
                    f"starts={1 if frame < 142 else 2} landings={0 if frame < 135 else 1 if frame < 177 else 2} "
                    f"soft_ticks={soft_ticks} constructs=1 native=fixture_player::{state}\n")
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

    def test_game_requires_native_player_commands_script_lifetime_and_movement(self):
        result, log = complete()
        trace = next(line for line in log.splitlines() if "GAME_NATIVE_PLAYER cycle=1 frame=89" in line)
        bad_traces = (
            trace.replace("frame=89", "frame=90"), trace.replace("cmd=127", "cmd=0"),
            trace.replace("time=1483", "time=1482"), trace.replace("read=0", "read=-1"),
            trace.replace("written=1", "written=2"), trace.replace("pending=0", "pending=1"),
            trace.replace("x=-31.595", "x=-32.000"), trace.replace("y=0.000", "y=1.000"),
            trace.replace("z=0.250", "z=0.000"), trace.replace("vx=23.800", "vx=0.000"),
            trace.replace("floor=1", "floor=0"), trace.replace("health=100", "health=0"),
            trace.replace("script=1", "script=0"), trace.replace("constructs=1", "constructs=2"),
        )
        for bad in (log.replace(trace + "\n", ""), log + trace + "\n",
                    *(log.replace(trace, changed) for changed in bad_traces),
                    log.replace("frame=93 cmd=0", "frame=93 cmd=127"),
                    log.replace("x=-26.192 y=0.000 z=0.250 vx=0.000", "x=-26.192 y=0.000 z=0.250 vx=3.800"),
                    log.replace("health=100 script=11", "health=100 script=10")):
            with self.subTest(output=bad):
                self.assertFalse(runner.classify_run("run1", "game", result, bad, None, False)[0])

    def test_game_requires_native_player_posture_landing_and_actor_state_transitions(self):
        result, log = complete()
        traces = [line for line in log.splitlines() if "GAME_PLAYER_STATE cycle=1" in line]
        # Exercise each column at takeoff, contact, release, crouch and standing phases.
        changes = {
            100: (("frame=100", "frame=101"), ("buttons=32", "buttons=0"), ("cmd=0", "cmd=127"),
                  ("time=1666", "time=1665"), ("read=11", "read=10"), ("written=12", "written=13"),
                  ("pending=0", "pending=1"), ("x=-26.192", "x=-25.000"), ("y=0.000", "y=1.000"),
                  ("z=5.232", "z=0.250"), ("vx=0.000", "vx=1.000"), ("vz=302.844", "vz=0.000"),
                  ("height=74.000", "height=38.000"), ("eye=68.000", "eye=32.000"),
                  ("floor=0", "floor=1"), ("jumped=1", "jumped=0"), ("crouched=0", "crouched=1"),
                  ("soft=0", "soft=1"), ("health=100", "health=99"), ("script=12", "script=11"),
                  ("state=2", "state=1"), ("transitions=2", "transitions=1"), ("starts=1", "starts=2"),
                  ("landings=0", "landings=1"), ("soft_ticks=0", "soft_ticks=1"),
                  ("constructs=1", "constructs=2"), ("FixtureAir", "FixtureIdle")),
            135: (("floor=1", "floor=0"), ("soft=1", "soft=0"), ("landings=1", "landings=0")),
            136: (("vz=0.289", "vz=0.000"), ("soft_ticks=2", "soft_ticks=1")),
            140: (("buttons=32", "buttons=0"), ("jumped=0", "jumped=1")),
            141: (("buttons=0", "buttons=32"),),
            142: (("starts=2", "starts=1"), ("transitions=4", "transitions=2")),
            177: (("soft_ticks=3", "soft_ticks=4"), ("landings=2", "landings=1")),
            181: (("buttons=96", "buttons=64"), ("vx=12.800", "vx=23.800"),
                  ("eye=63.320", "eye=32.000"), ("FixtureCrouch", "FixtureIdle")),
            190: (("buttons=0", "buttons=96"), ("eye=45.623", "eye=68.000"),
                  ("height=74.000", "height=38.000"), ("transitions=7", "transitions=6")),
            195: (("script=107", "script=106"), ("x=-24.759", "x=-26.192")),
        }
        bad_logs = [log.replace(traces[0] + "\n", ""), log + traces[0] + "\n",
                    log.replace(traces[0] + "\n" + traces[1], traces[1] + "\n" + traces[0])]
        for frame, replacements in changes.items():
            trace = next(line for line in traces if f"frame={frame} " in line)
            for old, new in replacements:
                self.assertIn(old, trace)
                bad_logs.append(log.replace(trace, trace.replace(old, new)))
        for index, bad in enumerate(bad_logs):
            with self.subTest(case=index):
                self.assertFalse(runner.classify_run("run1", "game", result, bad, None, False)[0])

    def test_negative_game_requires_native_init_and_first_fatal_source(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            runner.stage_game_fixtures(directory, "game-player-script")
            self.assertNotIn("AI_ONGROUND", (directory / "script/doom_main.script").read_text())
            runner.stage_game_fixtures(directory, "game-player-state")
            script = (directory / "script/doom_main.script").read_text()
            self.assertIn('setState("FixtureAir")', script)
            self.assertNotIn("void FixtureAir();", script)
            self.assertNotIn("void fixture_player::FixtureAir()", script)
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
