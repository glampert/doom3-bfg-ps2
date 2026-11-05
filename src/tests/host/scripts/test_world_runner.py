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


def switch_line(bits, fixed, texture):
    name = "PSMCT32/Z16S" if bits == 32 else "PSMCT16/Z16"
    return f"[D3BFG] GS frame buffer bits={bits} {name} fixed_vram={fixed} texture_vram={texture}\n"


def session(views=None, unloaded=None, memory="7424/7600/4 final=7424/7600/4", traces=None, switches=None):
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
    log += echo("loaded")
    frame = 70
    for name, bits, view in runner.steps():
        if view is None:
            line = switch_line(bits, 2293760 if bits == 32 else 1376256, 1900544 if bits == 32 else 2097152)
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
    log += WORLD.format(**unload) + echo("unloaded")
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
        self.assertEqual(details["switches"]["bits-32"], {"fixed_vram": 2293760, "texture_vram": 1900544})

    def test_pal_profiles_pass(self):
        log = session(switches={"bits-32": switch_line(32, 2621440, 1572864), "bits-16": switch_line(16, 1572864, 2097152)})
        passed, reason, _ = runner.classify(log, IDENTITY)
        self.assertTrue(passed, reason)

    def test_profile_switch_must_happen_once_with_its_buffers(self):
        # No change, a change to the wrong profile, buffers of no standard, an uncapped texture range.
        self.assertFalse(runner.classify(session(switches={"bits-32": None}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(switches={"bits-32": switch_line(16, 1376256, 2097152)}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(switches={"bits-32": switch_line(32, 2293761, 1900543)}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(switches={"bits-32": switch_line(32, 2293760, 2097152)}), IDENTITY)[0])
        self.assertFalse(runner.classify(session(switches={"bits-16": None}), IDENTITY)[0])
        # A second change inside a view's span, or one after the session's steps.
        doubled = switch_line(32, 2293760, 1900544) * 2
        self.assertFalse(runner.classify(session(switches={"bits-32": doubled}), IDENTITY)[0])
        log = session().replace("[D3BFG] WORLD unloaded", switch_line(32, 2293760, 1900544).rstrip("\n") + "\n[    1.0000] [D3BFG] WORLD unloaded")
        self.assertFalse(runner.classify(log, IDENTITY)[0])

    def test_capture_must_carry_its_profile(self):
        self.assertFalse(runner.classify(session({"room0": {"bits": 16}}), IDENTITY)[0])
        self.assertFalse(runner.classify(session({"room0-16": {"bits": 32}}), IDENTITY)[0])
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

    def test_crash_or_fatal_fails(self):
        self.assertFalse(runner.classify(session() + "[    2.0000] TLB Miss, pc=0x100 addr=0x0 [load]\n", IDENTITY)[0])
        self.assertFalse(runner.classify(session() + "[D3BFG] FATAL renderer\n", IDENTITY)[0])

    def test_proc_load_or_refusal_fails(self):
        log = session()
        self.assertFalse(runner.classify(log.replace("graybox.d3wd: areas=2", "graybox.d3wd: areas=3"), IDENTITY)[0])
        self.assertFalse(runner.classify(log + "[D3BFG] WORLD load failed map=maps/graybox\n", IDENTITY)[0])

    def test_portal_culling_must_hold(self):
        # A view facing away from the doorway, or through it closed, still drawing both rooms.
        for name in ("away", "closed", "clip"):
            log = session({name: {"drawn": 2, "batches": 26, "entities": 2}})
            passed, reason, _ = runner.classify(log, IDENTITY)
            self.assertFalse(passed, name)
            self.assertIn(name, reason)
        # A doorway view drawing only its own room, or one starting in the wrong area.
        self.assertFalse(runner.classify(session({"doorway": {"drawn": 1, "batches": 14}}), IDENTITY)[0])
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


if __name__ == "__main__":
    unittest.main()
