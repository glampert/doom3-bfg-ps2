#!/usr/bin/env python3
# ================================================================================================
# File: run_world_retail.py
# Brief: Draw mars_city1, cooked from the retail .bproc, through id's render world: area 2 from the player start, portal culling, whole-image references.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""The production ELF boots its visible console on the authored headless data with mars_city1's
cooked world staged beside it (make world-cook-retail: the retail container's binary .bproc,
cooked into a D3WD). Its materials are declared collision-only until the cook compiles material
passes (R2), so id's declaration manager never loads their images. HostFs commands load the map
into Common's render world with ps2_world and draw it from fixed cameras in area 2, where the
player starts, and from the corridor beyond its doorway: the portal flow decides which areas
each view draws, and the session requires the areas and batches it drew. Traces through the area
models check the compact position mirror id's traces read. Each capture must match, pixel for
pixel within the frame-buffer profile's tolerance, a software rasterization of the same .bproc
from the same camera (world_reference.py), where any surface the 16-bit depth buffer cannot order
may win: a missing area or misplaced geometry fails it. Closing the doorway's portal must leave
only area 2, against a reference of area 2 alone. The world is then unloaded and the session
quits, recovering the startup ledger."""

import argparse
from collections import namedtuple
import json
from pathlib import Path
import re
import shutil

import renderer_capture
import run_headless
import run_pcsx2_test as smoke
import run_world
import world_reference
from run_console import BASELINE, TARGET_CRASH_RE
from stage_headless import REPO_ROOT, stage


MAP = "maps/game/mars_city1"
DEFAULT_WORLD = REPO_ROOT / "build/world/mars_city1.d3wd"
JOB = REPO_ROOT / "config/world_mars_city1.json"
# Seams and depth ties aside, a capture matches its reference at 99.7 % or more; a capture missing
# a visible area (or drawing one the reference does not) scores well below.
MIN_MATCH = 99.0
# The doorway between area 2 and the corridor (area 50): its handle and the areas it joins.
DOORWAY_PORTAL = (55, 2, 50)

# Each view: its camera (eye, target as ps2_world view takes them), the doorway portal's state
# while it draws (None leaves it open), the area the view starts in, the areas portal culling lets
# it draw and their batches, and the areas its reference holds (None for every area).
View = namedtuple("View", "name camera portal area drawn batches reference_areas")
# info_player_start_2 stands at (-3632, -3416, 192); the eye is 68 units above it.
START = (-3632, -3416, 260)
VIEWS = (
    View("east", START + (-3332, -3416, 260), None, 2, 1, 64, None),
    View("west", START + (-3932, -3416, 260), None, 2, 1, 64, None),
    View("north", START + (-3632, -3116, 260), None, 2, 3, 162, None),
    View("south", START + (-3632, -3716, 260), None, 2, 1, 64, None),
    View("doorway", (-3772, -3300, 260, -3772, -2900, 260), None, 2, 3, 162, None),
    View("corridor", (-3772, -3000, 260, -3700, -3500, 240), None, 50, 2, 111, None),
    View("closed", START + (-3632, -3116, 260), "closed", 2, 1, 64, (2,)),
)

# Traces through the area models' s16 mirror from the player's eye: straight down onto the floor
# grate under the start, and west into the hall's wall. Each: start, end, hit, fraction, normal and
# the material (a Python ray cast over the .bproc's triangles gives the same fractions and materials).
TRACES = (
    (START, (-3632, -3416, -200), 1, 0.148, (0.0, 0.0, 1.0), "textures/base_floor/sflgrate2"),
    (START, (-4632, -3416, 260), 1, 0.383, (0.990, 0.0, -0.141), "textures/mcity/stewall3b"),
)


def world_report(world: Path) -> dict:
    """The cook's report beside the D3WD (asset_reexport world)."""
    return json.loads(world.with_name(world.stem + ".report.json").read_text())


def view_commands(identity: str, view: View) -> list[str]:
    # The capture runs two frames after the camera is set, and the status two frames after that.
    handle = DOORWAY_PORTAL[0]
    portal = [f"ps2_world portal {handle} {view.portal}"] if view.portal else []
    reopen = [f"ps2_world portal {handle} open"] if view.portal else []
    return [*portal, "ps2_world view " + " ".join(str(value) for value in view.camera), "wait 2", f'ps2_capture "world-{view.name}.tga"',
            "wait 2", "ps2_world status", *reopen, f'echo "{run_world.barrier(identity, view.name)}"']


def driver(identity: str):
    """Yield each command batch with the condition, over the log, that ends its wait."""

    def until(name: str):
        return lambda log: run_world.echoed(log, run_world.barrier(identity, name))

    traces = ["ps2_world trace " + " ".join(str(value) for value in start + end) for start, end, *_ in TRACES]
    # The console starts open; closed, it slides away over real time before the first view.
    yield ([f'echo "{run_world.barrier(identity, "begin")}"', "ps2_consoleKey toggle", "wait 60", "r_znear 4", f"ps2_world load {MAP}",
            *traces, f'echo "{run_world.barrier(identity, "loaded")}"'], until("loaded"))
    # Loading the map leaves the front end: two frames later the device draws in the preference.
    yield ["wait 2", f'echo "{run_world.barrier(identity, "ingame")}"'], until("ingame")
    for view in VIEWS:
        yield view_commands(identity, view), until(view.name)
    yield ["ps2_world unload", f'echo "{run_world.barrier(identity, "unloaded")}"'], until("unloaded")
    yield ["wait 2", f'echo "{run_world.barrier(identity, "frontend")}"', "quit"], lambda log: run_headless.SHUTDOWN_COMPLETE in log


def classify(log: str, identity: str, standard: str, report: dict) -> tuple[bool, str, dict]:
    """Check the session log: the cooked load with the report's counts, the traces, every view's
    portal culling and capture, the display changes into and out of the map, the unload and shutdown."""
    if TARGET_CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
        return False, "crash or fatal diagnostic", {}
    load = re.compile(rf"\[D3BFG\] world {re.escape(MAP)}\.d3wd: areas={report['areas']} surfaces={report['surfaces']} "
                      rf"batches={report['batches']} portals={report['portals']} nodes={report['nodes']} ")
    if not load.search(log):
        return False, "the cooked map did not load with the cook report's areas, surfaces, batches, portals and nodes", {}
    if "WORLD load failed" in log:
        return False, "the render world refused the map", {}
    markers = ["begin", "loaded", "ingame"] + [view.name for view in VIEWS] + ["unloaded", "frontend"]
    # Echo output, not the submitted line, is the barrier.
    spans = [re.search(re.escape(run_world.barrier(identity, name)) + r" +\r?\n", log) for name in markers]
    if any(span is None for span in spans):
        return False, "missing session barrier", {}
    positions = [span.start() for span in spans]
    if positions != sorted(positions):
        return False, "session barriers ran out of order", {}
    if run_headless.SHUTDOWN_COMPLETE not in log:
        return False, "the session did not shut down", {}
    loaded = [line for line in run_world.world_lines(log) if line["event"] == "loaded"]
    if len(loaded) != 1 or (loaded[0]["areas"], loaded[0]["portals"]) != (report["areas"], report["portals"]):
        return False, "the render world did not take the cooked areas and portals", {}
    traces = run_world.TRACE_RE.findall(log[positions[0]:positions[1]])
    if len(traces) != len(TRACES):
        return False, "missing world trace", {}
    for (start, end, hit, fraction, normal, material), found in zip(TRACES, traces):
        normal_found = tuple(float(value) for value in found[2:5])
        if (int(found[0]) != hit or abs(float(found[1]) - fraction) > 0.002 or
                any(abs(a - b) > 0.002 for a, b in zip(normal_found, normal)) or found[5] != material):
            return False, f"the trace from {start} to {end} found {found}; expected hit={hit} fraction={fraction} {normal} {material}", {}
    ingame = run_world.SWITCH_RE.findall(log[positions[1]:positions[2]])
    expected = run_world.expected_switch(standard, run_world.default_display(standard))
    if len(ingame) != 1 or run_world.parse_switch(ingame[0]) != expected:
        return False, f"loading the map did not switch the display once to {expected[3]}", {}
    views = {}
    for index, view in enumerate(VIEWS):
        span = log[positions[index + 2]:positions[index + 3]]
        captures = run_world.CAPTURE_RE.findall(span)
        statuses = [line for line in run_world.world_lines(span) if line["event"] == "status"]
        if run_world.SWITCH_RE.search(span) or len(captures) != 1 or captures[0][0] != f"world-{view.name}.tga" or len(statuses) != 1:
            return False, f"{view.name}: missing capture or world status", {}
        bits = renderer_capture.draw_bits(run_world.default_display(standard).profile)
        if int(captures[0][4]) != bits:
            return False, f"{view.name}: captured in the {captures[0][4]}-bit profile; expected {bits}", {}
        status = statuses[0]
        presents = int(captures[0][2])
        if status["viewing"] != 1 or status["frame"] <= presents:
            return False, f"{view.name}: the view was not drawn after its capture", {}
        if (status["area"], status["drawn"], status["batches"]) != (view.area, view.drawn, view.batches) or status["entities"] < view.drawn:
            return False, (f"{view.name}: started in area {status['area']} and drew {status['drawn']} areas, {status['batches']} batches; "
                           f"expected area {view.area}, {view.drawn} areas, {view.batches} batches"), {}
        views[view.name] = {"capture": captures[0][0], "bits": bits, "presents": presents, "status": status}
    if run_world.SWITCH_RE.search(log[:positions[1]] + log[positions[-3]:positions[-2]] + log[positions[-1]:]):
        return False, "the display mode changed outside its steps", {}
    unloads = [line for line in run_world.world_lines(log[positions[-3]:positions[-2]]) if line["event"] == "unloaded"]
    if len(unloads) != 1 or (unloads[0]["areas"], unloads[0]["portals"], unloads[0]["viewing"]) != (1, 0, 0):
        return False, "the world did not unload to the empty single-area world", {}
    back = run_world.SWITCH_RE.findall(log[positions[-2]:positions[-1]])
    expected = run_world.expected_switch(standard, run_world.front_end_display(standard))
    if len(back) != 1 or run_world.parse_switch(back[0]) != expected:
        return False, f"the front end did not return to {expected[3]} once after the unload", {}
    memory = run_headless.MEMORY_RE.findall(log)
    if len(memory) != 1 or tuple(map(int, memory[0][:3])) != BASELINE or memory[0][:3] != memory[0][3:6]:
        return False, "production shutdown did not recover the startup allocation ledger", {}
    arena = max(view["status"]["arena"] for view in views.values())
    return True, ("mars_city1 loaded from its cooked .bproc, drew area 2 and the corridor through the portal flow, closed the "
                  "doorway down to area 2, unloaded and shut down"), {"views": views, "arena_bytes": arena}


def verify_captures(output: Path, data: Path, standard: str) -> dict:
    """Every capture against the reference rasterization of its view, written beside it as
    reference-<view>.png and difference-<view>.png (mismatches in magenta)."""
    job = json.loads(JOB.read_text())
    handle, first, second = DOORWAY_PORTAL
    portals = {entry[0]: entry[1:3] for entry in world_reference.retail_portals(data, job)}
    if portals.get(handle) != (first, second):
        raise ValueError(f"portal {handle} joins areas {portals.get(handle)}; expected {first} and {second}")
    bits = renderer_capture.draw_bits(run_world.default_display(standard).profile)
    everything = world_reference.load_retail_triangles(data, job)
    images = output / "references"
    images.mkdir(exist_ok=True)
    results = {}
    for view in VIEWS:
        triangles = everything if view.reference_areas is None else world_reference.load_retail_triangles(data, job, set(view.reference_areas))
        camera = (view.camera[:3], view.camera[3:])
        share = world_reference.compare_image(output / f"world-{view.name}.tga", triangles, camera, bits, world_reference.CLEAR_RGB,
                                              images, view.name)
        results[view.name] = {"match_percent": round(share, 2), "minimum_percent": MIN_MATCH}
        if share < MIN_MATCH:
            raise ValueError(f"{view.name}: {share:.2f}% of the capture matches its reference; expected at least {MIN_MATCH}%")
    return results


def stage_retail_world(build: str, output: Path, standard: str, catalog: Path, world: Path, report: dict) -> Path:
    """A visible console package on the authored headless data, with the cooked map and its
    materials declared collision-only (stand-ins until the cook compiles material passes)."""
    package = stage(build, output, standard, catalog=catalog)
    materials = package / "base/materials"
    materials.mkdir(parents=True, exist_ok=True)
    (materials / "world_stand_in.mtr").write_text("".join(f"{name} {{ solid collision }}\n" for name in report["material_names"]))
    maps = package / "base" / Path(MAP).parent
    maps.mkdir(parents=True, exist_ok=True)
    shutil.copy2(world, maps / world.name)
    return package


def run(args: argparse.Namespace, standard: str, version: str) -> bool:
    if not args.world.is_file():
        raise ValueError(f"missing cooked world {args.world}; run make world-cook-retail")
    report = world_report(args.world)
    return run_world.run_session(args, standard, version, "world_retail",
                                 lambda output: stage_retail_world(args.build, output, standard, args.catalog, args.world, report),
                                 driver, lambda log, identity: classify(log, identity, standard, report),
                                 lambda output: verify_captures(output, args.data, standard), args.world)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", choices=("debug", "release"), default="debug")
    parser.add_argument("--catalog", type=Path, default=REPO_ROOT / "build/ui-retail",
                        help="the cooked retail UI (make ui-cook) the visible console draws its charset from")
    parser.add_argument("--world", type=Path, default=DEFAULT_WORLD, help="the cooked map (make world-cook-retail)")
    parser.add_argument("--data", type=Path, default=world_reference.RETAIL_DATA,
                        help="the retail installation the references read the .bproc from")
    parser.add_argument("--standard", choices=("ntsc", "pal"), help="default: both, one session each")
    parser.add_argument("--emulator", type=Path, default=smoke.DEFAULT_EMULATOR)
    parser.add_argument("--config", type=Path, default=smoke.DEFAULT_CONFIG)
    parser.add_argument("--timeout-scale", type=float, default=1.0, help="multiplies the watchdogs")
    args = parser.parse_args()
    if not 0 < args.timeout_scale <= 10:
        parser.error("timeout scale must be positive and at most 10")
    try:
        if smoke.emulator_is_running(args.emulator):
            raise ValueError("an emulator is already running; close it before starting the world session")
        smoke.check_config(args.config)
        version = smoke.emulator_version(args.emulator)
        for standard in (args.standard,) if args.standard else ("ntsc", "pal"):
            if not run(args, standard, version):
                return 1
    except (OSError, ValueError) as error:
        print(f"FAIL world-retail: {error}", flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
