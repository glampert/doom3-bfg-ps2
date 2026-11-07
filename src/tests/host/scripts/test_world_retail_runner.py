#!/usr/bin/env python3
# ================================================================================================
# File: test_world_retail_runner.py
# Brief: Reject false retail-world acceptance: a load that differs from the cook report, broken portal
# culling, wrong traces, a missing display change or capture, an unfinished unload or ledger; and
# check the reference's depth-tie and far-plane rules.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/scripts"))
import run_world
import run_world_retail as runner
import world_reference
from test_renderer_capture import tga
from test_world_runner import WORLD, expected_switch_line, trace_line

IDENTITY = "20261107T000000Z_world_retail_0123456789ab"
REPORT = {"areas": 56, "surfaces": 1791, "batches": 2138, "portals": 55, "nodes": 9783,
          "material_names": ["textures/a", "textures/skies/b"]}


def echo(name):
    return f"]echo \"{IDENTITY}_{name}\"\n{IDENTITY}_{name} \n"


def session(views=None, traces=None, load=None, ingame=True, memory="7424/7600/4 final=7424/7600/4"):
    """A retail world session's log as PCSX2's file log stamps it, with per-view and per-trace overrides."""
    counts = dict(REPORT, **(load or {}))
    log = echo("ready") + echo("begin")
    log += (f"[D3BFG] world {runner.MAP}.d3wd: areas={counts['areas']} surfaces={counts['surfaces']} batches={counts['batches']} "
            f"portals={counts['portals']} nodes={counts['nodes']} packets=1610352 bytes=2704880\n")
    log += WORLD.format(event="loaded", areas=56, portals=55, viewing=0, frame=0, views=0, area=-1, entities=0, drawn=0, batches=0)
    for index, (_, _, hit, fraction, normal, material) in enumerate(runner.TRACES):
        found = {"hit": hit, "fraction": fraction, "normal": normal, "material": material}
        found.update((traces or {}).get(index, {}))
        log += trace_line(**found)
    log += echo("loaded")
    if ingame:
        log += expected_switch_line(run_world.default_display("ntsc"))
    log += echo("ingame")
    frame = 70
    for view in runner.VIEWS:
        status = {"event": "status", "areas": 56, "portals": 55, "viewing": 1, "frame": frame + 3, "views": frame, "area": view.area,
                  "entities": view.drawn, "drawn": view.drawn, "batches": view.batches}
        status.update((views or {}).get(view.name, {}))
        capture = status.pop("capture", f"world-{view.name}.tga")
        if capture:
            log += f"[D3BFG] CONSOLE capture={capture} frame={frame} presents={frame} redraws=0 bits=32\n"
        log += WORLD.format(**status) + echo(view.name)
        frame += 10
    log += WORLD.format(event="unloaded", areas=1, portals=0, viewing=0, frame=frame, views=frame, area=0, entities=1, drawn=1,
                        batches=64) + echo("unloaded")
    log += expected_switch_line(run_world.front_end_display("ntsc")) + echo("frontend")
    log += f"Memory baseline={memory} peak=5795973/5876776 arena=5915604\n"
    log += "======== Shutdown complete ========\n"
    return "".join("[    1.0000] " + line for line in log.splitlines(keepends=True))


def classify(log):
    return runner.classify(log, IDENTITY, "ntsc", REPORT)


class RetailWorldRunnerTests(unittest.TestCase):
    def test_complete_session_passes(self):
        passed, reason, details = classify(session())
        self.assertTrue(passed, reason)
        self.assertEqual(details["views"]["north"]["status"]["drawn"], 3)
        self.assertEqual(details["views"]["corridor"]["status"]["area"], 50)
        self.assertEqual(details["views"]["closed"]["status"]["drawn"], 1)

    def test_load_must_match_the_cook_report(self):
        passed, reason, _ = classify(session(load={"batches": 2137}))
        self.assertFalse(passed)
        self.assertIn("cook report", reason)

    def test_portal_culling_must_hold(self):
        # The closed doorway must leave area 2 alone; the open one must reach past it.
        for name, change in (("closed", {"drawn": 3, "entities": 3, "batches": 162}), ("north", {"drawn": 1, "entities": 1, "batches": 64}),
                             ("corridor", {"area": 2})):
            with self.subTest(view=name):
                passed, reason, _ = classify(session(views={name: change}))
                self.assertFalse(passed)
                self.assertIn(name, reason)

    def test_traces_must_hit_the_mirror_geometry(self):
        for change in ({"hit": 0}, {"fraction": 0.2}, {"normal": (0.0, 0.0, -1.0)}, {"material": "textures/base_floor/sflgrate"}):
            with self.subTest(change=change):
                self.assertFalse(classify(session(traces={0: change}))[0])

    def test_display_must_enter_the_map_once(self):
        passed, reason, _ = classify(session(ingame=False))
        self.assertFalse(passed)
        self.assertIn("switch the display", reason)

    def test_missing_capture_and_ledger_fail(self):
        self.assertFalse(classify(session(views={"west": {"capture": ""}}))[0])
        self.assertFalse(classify(session(memory="7424/7600/5 final=7424/7600/5"))[0])
        self.assertFalse(classify(session() + "[    1.0000] [D3BFG] FATAL boom\n")[0])

    def test_closed_view_brackets_its_portal(self):
        commands = {view.name: runner.view_commands(IDENTITY, view) for view in runner.VIEWS}
        self.assertEqual(commands["closed"][0], f"ps2_world portal {runner.DOORWAY_PORTAL[0]} closed")
        self.assertIn(f"ps2_world portal {runner.DOORWAY_PORTAL[0]} open", commands["closed"])
        self.assertFalse(any("portal" in command for command in commands["north"]))
        # Every view is a reference camera of the retail scene.
        self.assertEqual(set(world_reference.scene_views("mars_city1")), set(commands))

    def test_package_declares_every_cooked_material(self):
        with tempfile.TemporaryDirectory() as directory:
            materials = Path(directory) / "base/materials"
            materials.mkdir(parents=True)
            original = runner.stage
            try:
                runner.stage = lambda build, output, standard, catalog: output
                world = Path(directory) / "mars_city1.d3wd"
                world.write_bytes(b"D3WD")
                package = runner.stage_retail_world("debug", Path(directory), "ntsc", Path(directory), world, REPORT)
            finally:
                runner.stage = original
            text = (package / "base/materials/world_stand_in.mtr").read_text()
            self.assertEqual(text, "textures/a { solid collision }\ntextures/skies/b { solid collision }\n")
            self.assertTrue((package / "base/maps/game/mars_city1.d3wd").is_file())


class ReferenceRuleTests(unittest.TestCase):
    """The rasterizer's rules for what the GS cannot decide or draw."""

    @staticmethod
    def quad(colour, size=2000):
        # A square across the view 400 units ahead.
        corners = [((x, 400, zz), colour) for x, zz in ((-size, -size), (size, -size), (size, size))], \
                  [((x, 400, zz), colour) for x, zz in ((-size, -size), (size, size), (-size, size))]
        return [("m", tuple(corner)) for corner in corners]

    def test_coplanar_surfaces_accept_either_colour(self):
        triangles = self.quad([100, 0, 0]) + self.quad([0, 100, 0])
        camera = ((0, 0, 0), (0, 400, 0))
        with tempfile.TemporaryDirectory() as directory:
            for colour in ((100, 0, 0), (0, 100, 0)):
                path = Path(directory) / "capture.tga"
                path.write_bytes(tga(640, 448, bytes(colour) * (640 * 448)))
                self.assertEqual(world_reference.compare_image(path, triangles, camera, 32, (0, 0, 0)), 100.0)
            # A third colour is no tie.
            path.write_bytes(tga(640, 448, bytes((0, 0, 100)) * (640 * 448)))
            self.assertEqual(world_reference.compare_image(path, triangles, camera, 32, (0, 0, 0)), 0.0)

    def test_separated_surfaces_are_ordered(self):
        # 8 units apart at 400 is far more than two depth levels: only the nearer one may show.
        near = [("m", tuple(((x, 392, z), [100, 0, 0]) for x, z in corners)) for corners in
                (((-2000, -2000), (2000, -2000), (2000, 2000)), ((-2000, -2000), (2000, 2000), (-2000, 2000)))]
        triangles = near + self.quad([0, 100, 0])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.tga"
            path.write_bytes(tga(640, 448, bytes((0, 100, 0)) * (640 * 448)))
            self.assertEqual(world_reference.compare_image(path, triangles, ((0, 0, 0), (0, 400, 0)), 32, (0, 0, 0)), 0.0)

    def test_triangles_past_the_far_plane_are_skipped(self):
        far = world_reference.FAR + 100
        triangle = ("m", (((-100, far, -100), [100, 0, 0]), ((100, far, -100), [100, 0, 0]), ((0, far, 100), [100, 0, 0])))
        image, owner = world_reference.rasterize([triangle], (0, 0, 0), (0, 1, 0), 64, 48, (7, 7, 7))
        self.assertTrue(all(cell is None for row in owner for cell in row))
        self.assertEqual(image[24][32], (7, 7, 7))


if __name__ == "__main__":
    unittest.main()
