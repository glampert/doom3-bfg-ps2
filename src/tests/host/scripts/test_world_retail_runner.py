#!/usr/bin/env python3
# ================================================================================================
# File: test_world_retail_runner.py
# Brief: Reject false retail-world acceptance: a load that differs from the cook report or the .bproc, broken
# portal culling, wrong traces or probes, a short frame log, a missing display change or capture, an
# unfinished unload, a drifting cycle ledger; check the probe models and the reference's depth-tie and
# far-plane rules.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/scripts"))
import frame_log
import run_world
import run_world_retail as runner
import world_probes
import world_reference
import world_tour
from test_renderer_capture import tga
from test_world_runner import WORLD, decals_line, expected_switch_line, trace_line

IDENTITY = "20261107T000000Z_world_retail_0123456789ab"
# Area 2 holds 64 batches, areas 50 and 51 another 47 and 51 (the hybrid cook).
AREA_BATCHES = {2: 64, 50: 47, 51: 51}
REPORT = {"areas": 56, "surfaces": 1791, "batches": 2138, "portals": 55, "nodes": 9783, "static_models": {"merged_entities": 357},
          "material_names": ["textures/a", "textures/skies/b"],
          "area_list": [{"batches": AREA_BATCHES.get(area, 1)} for area in range(56)]}


# The probes' expected answers (expected_probes reads them from the .bproc): three samples, the
# doorway alone joining area 2 to the rest, the sound 150 units further with it closed.
OPEN_GROUPS = [0] * 56
CLOSED_GROUPS = [2 if area == 2 else 0 for area in range(56)]
SOUNDS = [{"listener_area": 2, "source_area": 50, "direct": 438.93, "spatialized": spatialized} for spatialized in (438.93, 588.93, 438.93)]
PROBES = {"areas": 56, "portals": 55, "nodes": 9783, "samples": [((1.0, 2.0, 3.0), 2), ((9000.0, 0.0, 0.0), -1), ((4.0, 5.0, 6.0), 50)],
          "groups": [OPEN_GROUPS, CLOSED_GROUPS, OPEN_GROUPS], "sounds": SOUNDS}


def probe_step(areas=None, groups=None, portals=None, sounds=None, loader=True):
    """The probe step as a good session logs it, with overrides."""
    found = areas or [area for _, area in PROBES["samples"]]
    log = f"[D3BFG] WORLD area points={len(found)} areas={','.join(str(area) for area in found)}\n"
    states = groups or PROBES["groups"]
    blocking = portals or [7, 0]
    walks = sounds or [sound["spatialized"] for sound in SOUNDS]
    for state in range(3):
        for kind in runner.CONNECTED_KINDS:
            log += f"[D3BFG] WORLD connected kind={kind} first=0 groups={','.join(str(group) for group in states[state])}\n"
        log += (f"[D3BFG] WORLD sound listener_area=2 source_area=50 direct=438.93 spatialized={walks[state]:.2f} "
                f"origin=-3772.00,-3000.00,260.00 door_add=150.00\n")
        if state < 2:
            log += f"[D3BFG] WORLD portal handle={runner.DOORWAY_PORTAL[0]} blocking={blocking[state]}\n"
    if loader:
        log += "[D3BFG] LOADER bytes=3000000 chunks=120 seeks=40 io_us=500000 kib_per_s=5859 queue=0 device=host:\n"
    return log + echo("probes")


def tour_step(frames=runner.TOUR_FRAMES, rows=runner.TOUR_FRAMES, textures=None):
    log = f"[D3BFG] WORLD path started file={runner.TOUR_PACKAGE_PATH} keys=441 frames=7244 playing={frames}\n"
    log += f"[D3BFG] FRAMELOG started frames={runner.TOUR_FRAMES + 1} bytes=41452 label=map=maps/game/mars_city1 load_ms=900 standard=NTSC\n"
    log += f"[D3BFG] WORLD path done keys=441 frames={frames} eye=1.00,2.00,3.00\n"
    log += f"[D3BFG] FRAMELOG written path=host:framelog.txt rows={rows} bytes=40000 frame_us_mean=40000 frame_us_max=90000\n"
    log += "[D3BFG] LOADER bytes=3200000 chunks=130 seeks=44 io_us=520000 kib_per_s=6000 queue=0 device=host:\n"
    return log + textures_line(**(textures or {})) + echo("tour")


def cycle_steps(ledgers=None):
    log = ""
    for index in range(runner.CYCLES + 1):
        # Each cycle loads and unloads the map again, with its world lines.
        log += WORLD.format(event="loaded", areas=56, portals=55, viewing=0, frame=0, views=0, area=-1, entities=0, drawn=0, batches=0)
        log += expected_switch_line(run_world.default_display("ntsc"))
        log += WORLD.format(event="unloaded", areas=1, portals=0, viewing=0, frame=0, views=0, area=0, entities=1, drawn=1, batches=64)
        log += expected_switch_line(run_world.front_end_display("ntsc"))
        requested, backing, allocations = (ledgers or {}).get(index, (5000000, 5200000, 22000))
        log += (f"[D3BFG] MEMORY cycle{index} requested={requested} backing={backing} allocations={allocations} "
                f"peak_requested=1 peak_backing=1\n")
        log += echo(f"cycle{index}")
    return log


def echo(name):
    return f"]echo \"{IDENTITY}_{name}\"\n{IDENTITY}_{name} \n"


# The grate decal as a good session reports it.
GRATE_DECAL = {"entities": 1, "decals": 1, "vertices": 4, "triangles": 2, "area": 1024.0, "bounds": runner.DECALS_EXPECTED["bounds"]}


# A lit pass that held every texture and lightmap page it needed.
TEXTURES = {"enabled": 1, "arena": 1064960, "resident": 42, "resident_bytes": 500480, "uploads": 42, "evictions": 0, "fallbacks": 0,
            "lightmaps": 1, "pages": 2, "unlit": 0, "passes": 12, "pass_fallbacks": 0, "sky_faces": 20, "sky_fallbacks": 0, "repacks": 0,
            "stream_loads": 60, "stream_bytes": 700000, "copies": 60, "copy_bytes": 702464, "copy_peak": 702464, "stream_failures": 0,
            "stream_misses": 0, "stream_waits": 3, "stream_wait_ms": 40, "overflows": 0, "pass_evictions": 0, "moved_states": 0,
            "moved_fallbacks": 0, "upload_bytes": 600000, "vram_yields": 0, "yielded_bytes": 0, "regrows": 0}


def textures_line(**fields):
    values = dict(TEXTURES, **fields)
    return ("[D3BFG] WORLD textures " + " ".join(f"{name}={values[name]}" for name in runner.TEXTURE_FIELDS) + "\n")


def fly_line(event, on, eye, yaw, pitch):
    return (f"[D3BFG] WORLD fly {event} on={on} eye={eye[0]:.2f},{eye[1]:.2f},{eye[2]:.2f} yaw={yaw:.2f} pitch={pitch:.2f} "
            f"view=0 0 0 1 0 0\n")


def flight(frame, pose=None, area=runner.FLY_AREA):
    """The flight step as a debug session logs it, ending at 'pose' (the model's by default)."""
    start = tuple(runner.FLY_START.camera[:3])
    eye, yaw, pitch = pose or runner.fly_pose(start, runner.FLY_INPUT)
    log = fly_line("on", 1, start, 0.0, 0.0) + fly_line("status", 1, eye, yaw, pitch)
    log += f"[D3BFG] CONSOLE capture=world-fly.tga frame={frame} presents={frame} redraws=0 bits=32\n"
    log += WORLD.format(event="status", areas=56, portals=55, viewing=1, frame=frame + 3, views=frame, area=area, entities=2, drawn=2,
                        batches=123)
    log += f"[D3BFG] CONSOLE capture=world-fly-light.tga frame={frame + 5} presents={frame + 5} redraws=0 bits=32\n"
    log += f"[D3BFG] CONSOLE capture=world-fly-lit.tga frame={frame + 7} presents={frame + 7} redraws=0 bits=32\n"
    return log + fly_line("off", 0, eye, yaw, pitch) + echo("fly")


def share_step(frame, shown=None, closed=None):
    """The console step: the arena gives a page back while the console shows, then grows back."""
    log = f"[D3BFG] CONSOLE capture=world-console.tga frame={frame} presents={frame} redraws=0 bits=32\n"
    log += textures_line(**dict({"arena": TEXTURES["arena"] - 40960, "vram_yields": 1, "yielded_bytes": 40960}, **(shown or {})))
    log += textures_line(**dict({"vram_yields": 1, "yielded_bytes": 40960, "regrows": 1}, **(closed or {})))
    return log + echo("share")


def session(views=None, traces=None, load=None, ingame=True, memory="0/0/0 final=0/0/0", report=REPORT,
            decals=(None, GRATE_DECAL, None), textures=None, fly=None, share=None, probes=None, tour=None, cycles=None):
    """A retail world session's log as PCSX2's file log stamps it, with per-view and per-trace overrides."""
    counts = dict(report, **(load or {}))
    log = echo("ready") + echo("begin")
    log += (f"[D3BFG] world {runner.MAP}.d3wd: areas={counts['areas']} surfaces={counts['surfaces']} batches={counts['batches']} "
            f"portals={counts['portals']} nodes={counts['nodes']} packets=1610352 bytes=2704880 "
            f"merged_entities={counts.get('merged_entities', 357)}\n")
    log += f"[D3BFG] world textures {runner.MAP}.d3tx: textures=289 materials=313 pages=8 passes=48 areas=56 bytes=2761280 tables=6720 streamed\n"
    log += WORLD.format(event="loaded", areas=56, portals=55, viewing=0, frame=0, views=0, area=-1, entities=0, drawn=0, batches=0)
    for index, (_, _, hit, fraction, normal, material) in enumerate(runner.TRACES):
        found = {"hit": hit, "fraction": fraction, "normal": normal, "material": material}
        found.update((traces or {}).get(index, {}))
        log += trace_line(**found)
    log += decals_line(**(decals[0] or {})) + echo("loaded")
    if ingame:
        log += expected_switch_line(run_world.default_display("ntsc"))
    log += echo("ingame")
    frame = 70
    for view in runner.VIEWS:
        drawn = len(view.areas)
        status = {"event": "status", "areas": 56, "portals": 55, "viewing": 1, "frame": frame + 3, "views": frame, "area": view.area,
                  "entities": drawn, "drawn": drawn, "batches": runner.view_batches(report, view)}
        status.update((views or {}).get(view.name, {}))
        capture = status.pop("capture", f"world-{view.name}.tga")
        if capture:
            log += f"[D3BFG] CONSOLE capture={capture} frame={frame} presents={frame} redraws=0 bits=32\n"
        log += WORLD.format(**status)
        log += f"[D3BFG] CONSOLE capture=world-{view.name}-light.tga frame={frame + 5} presents={frame + 5} redraws=0 bits=32\n"
        log += f"[D3BFG] CONSOLE capture=world-{view.name}-lit.tga frame={frame + 7} presents={frame + 7} redraws=0 bits=32\n"
        log += textures_line(**(textures or {}).get(view.name, {})) + echo(view.name)
        frame += 10
    log += share_step(frame, **(share or {}))
    log += probe_step(**(probes or {}))
    log += tour_step(**(tour or {}))
    if fly is not None:
        log += flight(frame, **fly)
    log += decals_line(**(decals[1] or {}))
    log += WORLD.format(event="unloaded", areas=1, portals=0, viewing=0, frame=frame, views=frame, area=0, entities=1, drawn=1,
                        batches=64)
    log += decals_line(**(decals[2] or {})) + echo("unloaded")
    log += expected_switch_line(run_world.front_end_display("ntsc")) + echo("frontend")
    log += cycle_steps(**(cycles or {}))
    log += f"Memory baseline={memory} peak=5795973/5876776 arena=5915604\n"
    log += "======== Shutdown complete ========\n"
    return "".join("[    1.0000] " + line for line in log.splitlines(keepends=True))


def classify(log, fly=False, probes=PROBES):
    return runner.classify(log, IDENTITY, "ntsc", REPORT, probes, fly)


class RetailWorldRunnerTests(unittest.TestCase):
    def test_complete_session_passes(self):
        passed, reason, details = classify(session())
        self.assertTrue(passed, reason)
        self.assertEqual(details["views"]["north"]["status"]["drawn"], 3)
        self.assertEqual(details["views"]["corridor"]["status"]["area"], 50)
        self.assertEqual(details["views"]["closed"]["status"]["drawn"], 1)

    def test_flight_model_follows_the_target(self):
        # Full forward for 30 frames of 1/30 s covers the flight speed along the starting view (+x).
        eye, yaw, pitch = runner.fly_pose((0.0, 0.0, 0.0), (0, -128, 0, 0, 0, 30))
        self.assertEqual((round(eye[0], 6), round(eye[1], 6), round(eye[2], 6), yaw, pitch), (runner.FLY_SPEED, 0.0, 0.0, 0.0, 0.0))
        # The scripted flight as the debug target logged it (20261108T090034Z_world_retail_038a5b17cb0c).
        eye, yaw, pitch = runner.fly_pose(tuple(runner.FLY_START.camera[:3]), runner.FLY_INPUT)
        for found, logged in zip((*eye, yaw, pitch), (-3586.95, -3348.12, 250.05, 109.62, 11.54)):
            self.assertAlmostEqual(found, logged, delta=0.01)

    def test_flight_session_passes(self):
        passed, reason, details = classify(session(fly={}), fly=True)
        self.assertTrue(passed, reason)
        self.assertEqual(details["flight"]["status"]["area"], runner.FLY_AREA)
        # A debug session must fly.
        self.assertFalse(classify(session(), fly=True)[0])

    def test_flight_must_reach_the_modelled_pose_in_its_area(self):
        eye, yaw, pitch = runner.fly_pose(tuple(runner.FLY_START.camera[:3]), runner.FLY_INPUT)
        moved = (eye[0] + 1.0, eye[1], eye[2])
        self.assertIn("flight status", classify(session(fly={"pose": (moved, yaw, pitch)}), fly=True)[1])
        self.assertIn("flight status", classify(session(fly={"pose": (eye, yaw + 1.0, pitch)}), fly=True)[1])
        self.assertIn("ended in area", classify(session(fly={"area": -1}), fly=True)[1])

    def test_view_batches_follow_the_cook_report(self):
        # The strips cook lays the same areas out in more batches: the session must draw those.
        strips = dict(REPORT, batches=2286, area_list=[{"batches": batches + 5} for batches in
                                                       (entry["batches"] for entry in REPORT["area_list"])])
        self.assertEqual(runner.view_batches(REPORT, runner.VIEWS[2]), 162)
        self.assertTrue(runner.classify(session(report=strips), IDENTITY, "ntsc", strips, PROBES)[0])
        self.assertFalse(classify(session(report=strips))[0])

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

    def test_grate_decal_must_cover_its_square(self):
        self.assertEqual(classify(session())[2]["decals"]["area"], 1024.0)
        for grate in ({"area": 1000.0}, {"entities": 0, "decals": 0, "triangles": 0}, None):
            with self.subTest(grate=grate):
                passed, reason, _ = classify(session(decals=(None, None if grate is None else dict(GRATE_DECAL, **grate), None)))
                self.assertFalse(passed)
                self.assertIn("floor decal", reason)
        self.assertFalse(classify(session(decals=(GRATE_DECAL, GRATE_DECAL, None)))[0])
        self.assertFalse(classify(session(decals=(None, GRATE_DECAL, GRATE_DECAL)))[0])

    def test_display_must_enter_the_map_once(self):
        passed, reason, _ = classify(session(ingame=False))
        self.assertFalse(passed)
        self.assertIn("switch the display", reason)

    def test_missing_capture_and_ledger_fail(self):
        self.assertFalse(classify(session(views={"west": {"capture": ""}}))[0])
        self.assertFalse(classify(session(memory="0/0/1 final=0/0/1"))[0])

    def test_textured_pass_must_hold_its_textures(self):
        for change in ({"fallbacks": 1}, {"uploads": 0}, {"enabled": 0}, {"resident": 0}, {"lightmaps": 0}, {"pages": 0}, {"unlit": 1},
                       {"stream_failures": 1}, {"stream_misses": 2}, {"stream_waits": 0}, {"moved_fallbacks": 1}):
            with self.subTest(change=change):
                passed, reason, _ = classify(session(textures={"north": change}))
                self.assertFalse(passed)
                self.assertIn("north", reason)
        self.assertFalse(classify(session() + "[    1.0000] [D3BFG] FATAL boom\n")[0])

    def test_merged_entities_must_load(self):
        passed, reason, _ = classify(session(load={"merged_entities": 300}))
        self.assertFalse(passed)
        self.assertIn("merged entities", reason)

    def test_console_must_share_the_arena(self):
        # The arena must give pages back while the console shows, and be whole again after.
        for share in ({"shown": {"vram_yields": 0}}, {"shown": {"arena": TEXTURES["arena"]}}, {"closed": {"regrows": 0}},
                      {"closed": {"arena": TEXTURES["arena"] - 8192}}):
            with self.subTest(share=share):
                passed, reason, _ = classify(session(share=share))
                self.assertFalse(passed)
                self.assertIn("arena", reason)
        passed, _, details = classify(session())
        self.assertTrue(passed)
        self.assertEqual(details["share"]["closed"]["regrows"], 1)

    def test_textures_must_stream(self):
        # Copies that held the whole package did not stream it.
        passed, reason, _ = classify(session(textures={"north": {"copy_peak": 2761280}}))
        self.assertFalse(passed)
        self.assertIn("whole", reason)
        passed, _, details = classify(session())
        self.assertTrue(passed)
        self.assertEqual((details["texture_file_bytes"], details["copy_peak"]), (2761280, 702464))

    def test_textured_pass_must_draw_the_sky(self):
        passed, reason, _ = classify(session(textures={view.name: {"sky_faces": 0} for view in runner.VIEWS}))
        self.assertFalse(passed)
        self.assertIn("sky", reason)
        # A view that leaves the sky out for its diffuse textures is fine.
        self.assertTrue(classify(session(textures={"north": {"sky_faces": 0, "sky_fallbacks": 5}}))[0])

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
                world.with_suffix(".d3tx").write_bytes(b"D3TX")
                package = runner.stage_retail_world("debug", Path(directory), "ntsc", Path(directory), world, REPORT)
            finally:
                runner.stage = original
            text = (package / "base/materials/world_stand_in.mtr").read_text()
            self.assertEqual(text, "textures/a { solid collision forceOverlays }\ntextures/skies/b { solid collision forceOverlays }\n")
            self.assertTrue((package / "base/maps/game/mars_city1.d3wd").is_file())
            self.assertTrue((package / "base/maps/game/mars_city1.d3tx").is_file())


class R2GateTests(unittest.TestCase):
    """The probes, the tour's frame log and the load cycles."""

    def test_complete_session_reports_the_gate(self):
        passed, reason, details = classify(session())
        self.assertTrue(passed, reason)
        self.assertEqual(details["probes"]["samples"], 3)
        self.assertEqual(details["probes"]["sound"], [438.93, 588.93, 438.93])
        self.assertEqual(details["tour"]["rows"], runner.TOUR_FRAMES)
        self.assertEqual(details["cycles"]["ledger"], {"requested": 5000000, "backing": 5200000, "allocations": 22000})

    def test_counts_must_match_the_bproc(self):
        passed, reason, _ = classify(session(), probes=dict(PROBES, nodes=9782))
        self.assertFalse(passed)
        self.assertIn(".bproc", reason)

    def test_probes_must_answer_as_the_bproc(self):
        for change, words in (({"areas": [2, -1, 51]}, "PointInArea"), ({"areas": [2, -1]}, "PointInArea"),
                              ({"groups": [OPEN_GROUPS, OPEN_GROUPS, OPEN_GROUPS]}, "AreasAreConnected"),
                              ({"portals": [1, 0]}, "doorway"), ({"sounds": [438.93, 438.93, 438.93]}, "sound"),
                              ({"loader": False}, "loader")):
            with self.subTest(change=change):
                passed, reason, _ = classify(session(probes=change))
                self.assertFalse(passed)
                self.assertIn(words, reason)

    def test_tour_must_fill_the_frame_log(self):
        for change in ({"frames": runner.TOUR_FRAMES - 1}, {"rows": runner.TOUR_FRAMES - 1}, {"textures": {"stream_failures": 1}}):
            with self.subTest(change=change):
                self.assertFalse(classify(session(tour=change))[0])

    def test_cycles_must_return_to_the_warm_ledger(self):
        # The warm cycle may differ from startup; every cycle after it must equal it exactly.
        self.assertTrue(classify(session(cycles={"ledgers": {0: (1, 2, 3), 1: (1, 2, 3), 2: (1, 2, 3), 3: (1, 2, 3)}}))[0])
        passed, reason, _ = classify(session(cycles={"ledgers": {2: (5000064, 5200064, 22001)}}))
        self.assertFalse(passed)
        self.assertIn("cycle 2", reason)

    def test_point_in_area_walks_the_nodes(self):
        # One node splitting x = 0: area 0 in front (x > 0), solid behind; a point on the plane is
        # neither side's for certain.
        nodes = [(1.0, 0.0, 0.0, 0.0, -1, 0)]
        self.assertEqual(world_probes.point_in_area(nodes, (5.0, 0.0, 0.0)), 0)
        self.assertEqual(world_probes.point_in_area(nodes, (-5.0, 0.0, 0.0)), -1)
        self.assertIsNone(world_probes.point_in_area(nodes, (0.01, 0.0, 0.0)))
        self.assertEqual(world_probes.point_in_area(nodes, (0.0, 0.0, 0.0), margin=0.0), -1)

    def test_connected_groups_follow_the_open_portals(self):
        square = [(0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 1.0, 1.0), (0.0, 0.0, 1.0)]
        portals = [(0, 1, square), (1, 2, square), (3, 4, square)]
        self.assertEqual(world_probes.connected_groups(5, portals), [0, 0, 0, 3, 3])
        self.assertEqual(world_probes.connected_groups(5, portals, frozenset({2})), [0, 0, 2, 3, 3])

    def test_sound_walk_crosses_the_doorway(self):
        # Two areas split by x = 0 (area 0 at x > 0), joined by a doorway square in that plane: a sound
        # straight through it travels the direct line, and the closed doorway adds its distance.
        nodes = [(1.0, 0.0, 0.0, 0.0, -1, -2)]
        doorway = [(0.0, -32.0, -32.0), (0.0, 32.0, -32.0), (0.0, 32.0, 32.0), (0.0, -32.0, 32.0)]
        portals = [(0, 1, doorway)]
        open_walk = world_probes.sound_distance(portals, nodes, (100.0, 0.0, 0.0), (-100.0, 0.0, 0.0), 4096.0)
        self.assertEqual((open_walk["listener_area"], open_walk["source_area"]), (0, 1))
        self.assertAlmostEqual(open_walk["spatialized"], 200.0)
        closed = world_probes.sound_distance(portals, nodes, (100.0, 0.0, 0.0), (-100.0, 0.0, 0.0), 4096.0, frozenset({1}))
        self.assertAlmostEqual(closed["spatialized"], 350.0)
        # Off to the side, the walk bends through the doorway's edge: longer than the straight line.
        bent = world_probes.sound_distance(portals, nodes, (100.0, 200.0, 0.0), (-100.0, 200.0, 0.0), 4096.0)
        self.assertGreater(bent["spatialized"], bent["direct"] + 1.0)
        # Out of reach: the maximum.
        self.assertEqual(world_probes.sound_distance(portals, nodes, (100.0, 0.0, 0.0), (-100.0, 0.0, 0.0), 150.0)["spatialized"], 150.0)

    def test_tour_keyframes_play_as_the_target_steps_them(self):
        keys = world_tour.keyframes([(0.0, 0.0, 0.0), (70.0, 0.0, 0.0), (70.0, 70.0, 0.0)])
        # Walk 10 frames east, turn 90 degrees in 15, walk 10 frames north.
        self.assertEqual([key[0] for key in keys], [0, 10, 15, 10])
        self.assertEqual(world_tour.path_frames(keys), 36)
        self.assertEqual(keys[2][4], 90.0)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tour.path"
            path.write_text(world_tour.format_path(keys, "maps/test", []))
            self.assertEqual(world_tour.read_path(path), [tuple(float(v) if i else int(v) for i, v in enumerate(key)) for key in keys])
        # The committed tour reads back within ps2_world path's limits.
        tour = world_tour.read_path(runner.TOUR)
        self.assertEqual(tour[0][0], 0)
        self.assertLessEqual(len(tour), world_tour.MAX_KEYS)
        self.assertGreater(world_tour.path_frames(tour), runner.TOUR_FRAMES)

    def test_frame_log_parses_and_summarises(self):
        header = "FLOG#hdr,frame," + ",".join(frame_log.TIME_COLUMNS) + ",stream_wait_ms,area,areas,entities," + \
            ",".join(frame_log.COUNT_COLUMNS) + ",x,y,z,yaw"
        columns = header.split(",")[1:]
        rows = []
        for frame in range(3):
            values = {name: 0 for name in columns}
            values.update(frame=frame, frame_us=33000 + frame * 10000, gpu_us=1000, batches=100 + frame)
            rows.append("FLOG," + ",".join(str(values[name]) for name in columns))
        text = "\n".join(["FLOG#build,release", header] + rows + ["FLOG#loader,bytes=10,chunks=1,seeks=1,io_us=5,kib_per_s=1", "FLOG#end,3"])
        summary = frame_log.summarize(frame_log.parse(text))
        self.assertTrue(summary["complete"])
        self.assertEqual(summary["stages"]["frame_us"]["max"], 53000)
        self.assertEqual(summary["over_budget"]["33367"], 2)
        self.assertEqual(summary["worst"][0]["frame"], 2)
        self.assertIn("53000", frame_log.format_summary(summary, summary))
        # A file without its end row was cut short.
        self.assertFalse(frame_log.summarize(frame_log.parse(text.replace("FLOG#end,3", "")))["complete"])
        with self.assertRaises(ValueError):
            frame_log.parse(text.replace("FLOG,0,", "FLOG,0,1,"))


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
