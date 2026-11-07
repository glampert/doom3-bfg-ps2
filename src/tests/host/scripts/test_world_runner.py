#!/usr/bin/env python3
# ================================================================================================
# File: test_world_runner.py
# Brief: Reject false world-session acceptance: a missing cooked load, broken portal culling, stray
# captures, a frame-buffer profile that did not change, an unfinished unload or ledger, and a closed
# doorway that changes the wrong pixels.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/scripts"))
import renderer_capture
import run_world as runner
from test_renderer_capture import tga

IDENTITY = "20261105T000000Z_world_0123456789ab"
WORLD = ("[D3BFG] WORLD {event} areas={areas} portals={portals} viewing={viewing} frame={frame} views={views} area={area} "
         "entities={entities} drawn={drawn} batches={batches} arena=1248\n")


def echo(name):
    return f"]echo \"{IDENTITY}_{name}\"\n{IDENTITY}_{name} \n"


def trace_line(hit, fraction, normal, material):
    return (f"[D3BFG] WORLD trace hit={hit} fraction={fraction:.3f} normal={normal[0]:.3f},{normal[1]:.3f},{normal[2]:.3f} "
            f"material={material}\n")


def decals_line(entities=0, decals=0, vertices=0, triangles=0, area=0.0, bounds=(0.0,) * 6):
    return (f"[D3BFG] WORLD decals entities={entities} decals={decals} vertices={vertices} triangles={triangles} area={area:.2f} "
            f"bounds={','.join(f'{value:.2f}' for value in bounds)}\n")


# The floor decal as a good session reports it: two pieces across a floor tile seam.
FLOOR_DECAL = {"entities": 1, "decals": 2, "vertices": 8, "triangles": 4, "area": 2304.0,
               "bounds": runner.DECALS_EXPECTED["bounds"]}


def switch_line(profile, fixed, texture, width=640, height=448, interlace="FIELD"):
    """A GS display line; profile is a draw/display label, or 16/32 for 16/16 and 32/32."""
    label = {16: "16/16", 32: "32/32"}.get(profile, profile)
    return (f"[D3BFG] GS display {width}x{height} {interlace} bits={label} {runner.PROFILE_NAMES[label]} fixed_vram={fixed} "
            f"texture_vram={texture}\n")


def expected_switch_line(display, standard="ntsc"):
    """The GS display line a step's mode logs on 'standard', from the runner's own expectation."""
    width, height, interlace, profile, _, fixed, texture = runner.expected_switch(standard, display)
    return switch_line(profile, fixed, texture, width, height, interlace)


def session(views=None, unloaded=None, memory="7424/7600/4 final=7424/7600/4", traces=None, switches=None, standard="ntsc",
            decals=(None, FLOOR_DECAL, None)):
    """A world session's log as PCSX2's file log stamps it, with per-view, per-trace and per-switch
    overrides (a switch override replaces its GS line; None drops it)."""
    log = echo("ready") + echo("begin")
    log += "[D3BFG] world maps/graybox.d3wd: areas=2 surfaces=18 batches=26 portals=1 nodes=11 packets=34896 bytes=55056\n"
    log += WORLD.format(event="loaded", areas=2, portals=1, viewing=0, frame=0, views=0, area=-1, entities=0, drawn=0, batches=0)
    for index, (_, _, hit, fraction, normal, material) in enumerate(runner.TRACES):
        found = {"hit": hit, "fraction": fraction, "normal": normal,
                 "material": material + "light" if material.endswith("_") else material}
        found.update((traces or {}).get(index, {}))
        log += trace_line(**found)
    log += decals_line(**(decals[0] or {})) + echo("loaded")
    frame = 70
    for name, display, view in runner.steps(standard):
        bits = renderer_capture.draw_bits(display.profile)
        if view is None:
            line = expected_switch_line(display, standard)
            line = (switches or {}).get(name, line)
            if line:
                log += line
            log += echo(name)
            frame += 3
            continue
        _, _, _, area, drawn, batches, _ = view
        status = {"event": "status", "areas": 2, "portals": 1, "viewing": 1, "frame": frame + 3, "views": frame,
                  "area": area, "entities": drawn, "drawn": drawn, "batches": batches}
        status.update((views or {}).get(name, {}))
        capture = status.pop("capture", f"world-{name}.tga")
        captured_bits = status.pop("bits", bits)
        if capture:
            log += f"[D3BFG] CONSOLE capture={capture} frame={frame} presents={frame} redraws=0 bits={captured_bits}\n"
        if status.pop("status", True):
            log += WORLD.format(**status)
        log += echo(name)
        frame += 10
    unload = {"event": "unloaded", "areas": 1, "portals": 0, "viewing": 0, "frame": frame, "views": frame, "area": 0,
              "entities": 1, "drawn": 1, "batches": 14}
    unload.update(unloaded or {})
    log += decals_line(**(decals[1] or {})) + WORLD.format(**unload) + decals_line(**(decals[2] or {})) + echo("unloaded")
    # The front end returns to 32/16 once the world is gone.
    frontend = (switches or {}).get("frontend", expected_switch_line(runner.front_end_display(standard), standard))
    log += (frontend or "") + echo("frontend")
    log += f"Memory baseline={memory} peak=5795973/5876776 arena=5915604\n"
    log += "======== Shutdown complete ========\n"
    return "".join("[    1.0000] " + line for line in log.splitlines(keepends=True))


def doorway_images(height, changed):
    """The open doorway view and the same view with the pixels at 'changed' turned to the clear colour."""
    opened = bytearray(bytes((90, 90, 90)) * (512 * height))
    closed = bytearray(opened)
    for x, y in changed:
        offset = (y * 512 + x) * 3
        closed[offset:offset + 3] = bytes(runner.CLEAR_RGB)
    return tga(512, height, opened), tga(512, height, closed)


class WorldRunnerTests(unittest.TestCase):
    def test_complete_session_passes(self):
        passed, reason, details = runner.classify(session(), IDENTITY)
        self.assertTrue(passed, reason)
        self.assertEqual(details["arena_bytes"], 1248)
        self.assertEqual(details["views"]["away"]["status"]["drawn"], 1)
        self.assertEqual(details["views"]["clip"]["bits"], 32)
        self.assertEqual(details["views"]["clip-16"]["bits"], 16)
        # 32/16 captures its 32-bit draw buffer.
        self.assertEqual(details["views"]["doorway-32-16"]["bits"], 32)
        # Loading the world leaves the front end: 32/32, 640 wide, which leaves 1,327,104 bytes for textures.
        self.assertEqual(details["switches"]["ingame"], {"width": 640, "height": 448, "interlace": "FIELD", "profile": "32/32",
                                                         "fixed_vram": 2867200, "texture_vram": 1327104})
        # 32/16 draws in one 32-bit buffer and shows a 16-bit one: two thirds of 32/32's VRAM.
        self.assertEqual(details["switches"]["bits-32-16"], {"width": 640, "height": 448, "interlace": "FIELD", "profile": "32/16",
                                                             "fixed_vram": 2293760, "texture_vram": 1900544})
        self.assertEqual(details["switches"]["frontend"], details["switches"]["bits-32-16"])
        self.assertEqual(details["switches"]["res-512"], {"width": 512, "height": 448, "interlace": "FIELD", "profile": "32/32",
                                                          "fixed_vram": 2293760, "texture_vram": 1900544})
        # FRAME mode halves the lines; 224-line 32-bit buffers need no padding (32-line page rows).
        self.assertEqual(details["switches"]["frame"], {"width": 640, "height": 224, "interlace": "FRAME", "profile": "32/32",
                                                        "fixed_vram": 1474560, "texture_vram": 2097152})
        self.assertEqual(details["views"]["doorway-frame"]["status"]["drawn"], 2)

    def test_pal_profiles_pass(self):
        passed, reason, details = runner.classify(session(standard="pal"), IDENTITY, "pal")
        self.assertTrue(passed, reason)
        self.assertEqual(details["switches"]["ingame"]["fixed_vram"], 3276800)
        self.assertEqual(details["switches"]["ingame"]["texture_vram"], 917504)
        self.assertEqual(details["switches"]["bits-16"]["fixed_vram"], 1966080)
        self.assertEqual(details["switches"]["frontend"]["fixed_vram"], 2621440)
        self.assertEqual(details["switches"]["frontend"]["texture_vram"], 1572864)
        self.assertEqual(details["switches"]["frame"]["height"], 256)
        # A PAL log judged as NTSC names the wrong buffers.
        self.assertFalse(runner.classify(session(standard="pal"), IDENTITY, "ntsc")[0])

    def test_each_switch_step_changes_one_cvar(self):
        for standard in ("ntsc", "pal"):
            steps = runner.steps(standard)
            self.assertEqual(steps[0][:2], ("ingame", runner.default_display(standard)))
            modes = [step[1] for step in steps[1:]]
            previous = [runner.default_display(standard)] + modes
            commands = [runner.switch_command(before, after) for before, after in zip(previous, modes) if before != after]
            self.assertEqual(commands, ["ps2_frameBufferBits 16/16", "ps2_frameBufferBits 32/16", "ps2_frameBufferBits 32/32",
                                        "ps2_resolution 0", "ps2_resolution 1", "ps2_interlace frame", "ps2_interlace field"])
        self.assertEqual(runner.default_display("ntsc"), runner.Display("32/32", 1, "FIELD"))
        self.assertEqual(runner.front_end_display("PAL"), runner.Display("32/16", 1, "FIELD"))
        # The load switches by itself: its step sends no cvar.
        self.assertEqual(runner.switch_commands("run", "ingame", runner.front_end_display("ntsc"), runner.default_display("ntsc")),
                         ["wait 2", 'echo "run_ingame"'])
        with self.assertRaisesRegex(ValueError, "one cvar"):
            runner.switch_command(runner.default_display("ntsc"), runner.Display("16/16", 0, "FIELD"))

    def test_profile_switch_must_happen_once_with_its_buffers(self):
        # The load's switch missing or to the front end's profile; the return to 32/16 missing or doubled.
        self.assertFalse(runner.classify(session(switches={"ingame": None}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(switches={"ingame": switch_line("32/16", 2293760, 1900544)}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(switches={"frontend": None}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(switches={"frontend": switch_line("32/16", 2293760, 1900544) * 2}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(switches={"frontend": switch_line(32, 2867200, 1327104)}), IDENTITY)[0])
        # No change, a change to the wrong profile, buffers of no standard, an uncapped texture range.
        self.assertFalse(runner.classify(session(switches={"bits-32": None}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(switches={"bits-32": switch_line(16, 1720320, 2097152)}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(switches={"bits-32": switch_line(32, 2867201, 1327103)}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(switches={"bits-32": switch_line(32, 2867200, 2097152)}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(switches={"bits-32": switch_line(32, 2293760, 1900544, 512)}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(switches={"bits-16": None}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(switches={"bits-32-16": switch_line(32, 2867200, 1327104)}), IDENTITY)[0])
        # The resolution or interlace change missing, at the wrong size, or with unpadded buffers.
        self.assertFalse(runner.classify(session(switches={"res-512": None}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(switches={"res-512": switch_line(32, 2867200, 1327104)}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(switches={"frame": switch_line(32, 1433600, 2097152, 640, 224, "FRAME")}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(switches={"frame": switch_line(32, 1474560, 2097152, 640, 224, "FIELD")}), IDENTITY)[0])
        # A second change inside a view's span, or one after the session's steps.
        doubled = switch_line(32, 2867200, 1327104) * 2
        self.assertFalse(runner.classify(session(switches={"bits-32": doubled}), IDENTITY)[0])
        log = session().replace("[D3BFG] WORLD unloaded", switch_line(32, 2867200, 1327104).rstrip("\n") + "\n[    1.0000] [D3BFG] WORLD unloaded")
        self.assertFalse(runner.classify(log, IDENTITY)[0])

    def test_capture_must_carry_its_profile(self):
        self.assertFalse(runner.classify(session({"room0": {"bits": 16}}), IDENTITY)[0])
        self.assertFalse(runner.classify(session({"room0-16": {"bits": 32}}), IDENTITY)[0])
        self.assertFalse(runner.classify(session({"doorway-32-16": {"bits": 16}}), IDENTITY)[0])
        self.assertFalse(runner.classify(session({"doorway-32": {"bits": 16}}), IDENTITY)[0])
        self.assertFalse(runner.classify(session().replace(" bits=32\n", "\n", 1), IDENTITY)[0])

    def test_traces_must_hit_the_mirror_geometry(self):
        # The floor missed, the divider hit at the wrong place or facing, the doorway blocked.
        self.assertFalse(runner.classify(session(traces={0: {"hit": 0, "fraction": 1.0, "normal": (0, 0, 0), "material": "none"}}),
                                         IDENTITY)[0])
        self.assertFalse(runner.classify(session(traces={1: {"fraction": 0.5}}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(traces={1: {"normal": (1.0, 0.0, 0.0)}}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(traces={1: {"material": "textures/graybox/wall"}}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(traces={2: {"hit": 1, "fraction": 0.46}}), IDENTITY)[0])
        self.assertFalse(runner.classify(session().replace("[D3BFG] WORLD trace", "[D3BFG] WORLD skipped", 1), IDENTITY)[0])

    def test_floor_decal_must_cover_its_square(self):
        self.assertEqual(runner.classify(session(), IDENTITY)[2]["decals"]["area"], 2304.0)
        shifted = runner.DECALS_EXPECTED["bounds"][:2] + (0.5,) + runner.DECALS_EXPECTED["bounds"][3:5] + (0.5,)
        for floor in ({"area": 2280.0}, {"bounds": shifted}, {"entities": 2}, {"triangles": 0}, None):
            with self.subTest(floor=floor):
                report = None if floor is None else dict(FLOOR_DECAL, **floor)
                passed, reason, _ = runner.classify(session(decals=(None, report, None)), IDENTITY)
                self.assertFalse(passed)
                self.assertIn("floor decal", reason)

    def test_decals_wait_for_a_view_and_go_with_the_world(self):
        passed, reason, _ = runner.classify(session(decals=(FLOOR_DECAL, FLOOR_DECAL, None)), IDENTITY)
        self.assertFalse(passed)
        self.assertIn("before any view", reason)
        passed, reason, _ = runner.classify(session(decals=(None, FLOOR_DECAL, FLOOR_DECAL)), IDENTITY)
        self.assertFalse(passed)
        self.assertIn("outlived", reason)

    def test_decal_commands_project_into_the_surface(self):
        self.assertEqual(runner.decal_commands(runner.DECALS[:1]), ["ps2_world decal -100 300 0 0 0 -1 48 8 textures/graybox/wall"])

    def test_crash_or_fatal_fails(self):
        self.assertFalse(runner.classify(session() + "[    2.0000] TLB Miss, pc=0x100 addr=0x0 [load]\n", IDENTITY)[0])
        self.assertFalse(runner.classify(session() + "[D3BFG] FATAL renderer\n", IDENTITY)[0])

    def test_proc_load_or_refusal_fails(self):
        log = session()
        self.assertFalse(runner.classify(log.replace("graybox.d3wd: areas=2", "graybox.d3wd: areas=3"), IDENTITY)[0])
        self.assertFalse(runner.classify(log + "[D3BFG] WORLD load failed map=maps/graybox\n", IDENTITY)[0])

    def test_portal_culling_must_hold(self):
        # A view facing away from the doorway, or through it closed, still drawing both rooms.
        for name in ("away", "closed"):
            log = session({name: {"drawn": 2, "batches": 26, "entities": 2}})
            passed, reason, _ = runner.classify(log, IDENTITY)
            self.assertFalse(passed, name)
            self.assertIn(name, reason)
        # A doorway view drawing only its own room, or one starting in the wrong area. The clip
        # camera sees the doorway at the screen's right edge: listing room 1 without drawing it
        # (an empty portal scissor) fails too.
        self.assertFalse(runner.classify(session({"doorway": {"drawn": 1, "batches": 14}}), IDENTITY)[0])
        passed, reason, _ = runner.classify(session({"clip": {"drawn": 1, "batches": 14}}), IDENTITY)
        self.assertFalse(passed)
        self.assertIn("clip", reason)
        self.assertFalse(runner.classify(session({"room1": {"area": 0}}), IDENTITY)[0])

    def test_missing_or_stale_capture_and_status_fail(self):
        self.assertFalse(runner.classify(session({"room0": {"capture": None}}), IDENTITY)[0])
        self.assertFalse(runner.classify(session({"room0": {"capture": "world-room1.tga"}}), IDENTITY)[0])
        self.assertFalse(runner.classify(session({"room1": {"status": False}}), IDENTITY)[0])
        # A status from before the capture, or with the viewer off.
        self.assertFalse(runner.classify(session({"doorway": {"frame": 80}}), IDENTITY)[0])
        self.assertFalse(runner.classify(session({"doorway": {"viewing": 0}}), IDENTITY)[0])

    def test_barriers_must_be_complete_and_ordered(self):
        log = session()
        self.assertFalse(runner.classify(log.replace(f"{IDENTITY}_clip \n", ""), IDENTITY)[0])
        swapped = log.replace(f"{IDENTITY}_room0 \n", "@@").replace(f"{IDENTITY}_room1 \n", f"{IDENTITY}_room0 \n")
        self.assertFalse(runner.classify(swapped.replace("@@", f"{IDENTITY}_room1 \n"), IDENTITY)[0])

    def test_unload_and_ledger_must_complete(self):
        self.assertFalse(runner.classify(session(unloaded={"areas": 2, "portals": 1}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(memory="7424/7600/4 final=7488/7664/5"), IDENTITY)[0])
        self.assertFalse(runner.classify(session().replace("======== Shutdown complete ========", ""), IDENTITY)[0])

    def test_closed_doorway_in_32_bits_needs_the_exact_clear_colour(self):
        x0, y0, x1, y1 = runner.doorway_box("NTSC")
        inside = [(x, y) for y in range(y0, y1) for x in range(x0, x1)]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            opened, closed = doorway_images(448, inside)
            (root / "open.tga").write_bytes(opened)
            (root / "closed.tga").write_bytes(closed)
            self.assertEqual(runner.verify_closed(root / "open.tga", root / "closed.tga", "NTSC", 32)["centre_rgb"], list(runner.CLEAR_RGB))
            # The five-bit store of the clear colour (16, 24, 41) passes only the 16-bit model.
            stored = bytearray(closed)
            for x, y in inside:
                offset = 18 + (y * 512 + x) * 3
                stored[offset:offset + 3] = bytes((41, 24, 16))
            (root / "closed.tga").write_bytes(stored)
            runner.verify_closed(root / "open.tga", root / "closed.tga", "NTSC", 16)
            # Five levels off in blue would pass PSMCT16's slack; the 32-bit model refuses it.
            for x, y in inside:
                offset = 18 + (y * 512 + x) * 3
                stored[offset:offset + 3] = bytes((45, 24, 16))
            (root / "closed.tga").write_bytes(stored)
            with self.assertRaisesRegex(ValueError, "centre"):
                runner.verify_closed(root / "open.tga", root / "closed.tga", "NTSC", 32)

    def test_closed_doorway_changes_only_the_opening(self):
        x0, y0, x1, y1 = runner.doorway_box("NTSC")
        inside = [(x, y) for y in range(y0, y1) for x in range(x0, x1)]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            opened, closed = doorway_images(448, inside)
            (root / "open.tga").write_bytes(opened)
            (root / "closed.tga").write_bytes(closed)
            result = runner.verify_closed(root / "open.tga", root / "closed.tga", "NTSC")
            self.assertEqual(result["changed_pixels"], len(inside))

            # A pixel outside the opening changed: area 1 was not the only thing culled.
            opened, closed = doorway_images(448, inside + [(10, 10)])
            (root / "closed.tga").write_bytes(closed)
            with self.assertRaisesRegex(ValueError, "outside the doorway"):
                runner.verify_closed(root / "open.tga", root / "closed.tga", "NTSC")

            # Nothing changed: the closed portal still let area 1 draw.
            opened, closed = doorway_images(448, [])
            (root / "closed.tga").write_bytes(closed)
            with self.assertRaisesRegex(ValueError, "centre"):
                runner.verify_closed(root / "open.tga", root / "closed.tga", "NTSC")

    def test_pal_doorway_box_is_centred(self):
        x0, y0, x1, y1 = runner.doorway_box("PAL")
        self.assertEqual((x0 + x1) // 2, 256)
        self.assertEqual((y0 + y1) // 2, 256)

    def test_doorway_box_follows_the_4_3_picture(self):
        # The opening is square on the TV: at 512x448 its width spans 3/8 of the buffer's width per
        # half-width over a vertical half of 224 lines, so it is wider than tall in buffer pixels.
        x0, y0, x1, y1 = runner.doorway_box("NTSC", 512)
        self.assertEqual((x1 - x0, y1 - y0), (2 * (int(64.0 / 436.0 * 192.0) + 3), 2 * (int(64.0 / 436.0 * 224.0) + 3)))
        x0, _, x1, _ = runner.doorway_box("NTSC", 640)
        self.assertEqual(x1 - x0, 2 * (int(64.0 / 436.0 * 240.0) + 3))


if __name__ == "__main__":
    unittest.main()
