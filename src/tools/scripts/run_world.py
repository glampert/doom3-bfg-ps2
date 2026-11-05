#!/usr/bin/env python3
# ================================================================================================
# File: run_world.py
# Brief: Draw the cooked graybox through id's render world in one production session per standard: load, portal culling, captures, unload.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""The production ELF boots its visible console on the authored headless data with the graybox map
staged beside it. HostFs commands close the console, load maps/graybox (its cooked D3WD, not a
.proc) into Common's render world with ps2_world, and draw it from fixed cameras through id's
RenderScene: the portal flow decides which areas each view draws. Traces through the area
models check the compact position mirror id's traces read. Every view is captured and
checked: the GS gate's four cameras against the same reference pixels, a camera facing away from
the doorway and the doorway camera with its portal closed against the area counts that portal
culling must give, the closed doorway against the open one. The world is then unloaded and the
session quits, recovering the startup ledger."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
import uuid

import renderer_capture
import run_console
import run_headless
import run_pcsx2_test as smoke
from run_console import BASELINE, TARGET_CRASH_RE, echoed
from stage_headless import REPO_ROOT, boot_data, sha256, stage


GRAYBOX_ROOT = REPO_ROOT / "src/tests/data/graybox_map"
DEFAULT_WORLD = REPO_ROOT / "build/world/graybox.d3wd"
STARTUP_SECONDS = 120
SESSION_SECONDS = 240
CLEAR_RGB = (16, 24, 40)
LOAD_RE = re.compile(r"\[D3BFG\] world maps/graybox\.d3wd: areas=2 surfaces=18 batches=26 portals=1 nodes=11 ")
WORLD_RE = re.compile(r"\[D3BFG\] WORLD (\w+) areas=(\d+) portals=(\d+) viewing=([01]) frame=(\d+) views=(\d+) area=(-?\d+) "
                      r"entities=(\d+) drawn=(\d+) batches=(\d+) arena=(\d+)")
CAPTURE_RE = re.compile(r"\[D3BFG\] CONSOLE capture=(\S+\.tga) frame=(\d+) presents=(\d+) redraws=(\d+)")
TRACE_RE = re.compile(r"\[D3BFG\] WORLD trace hit=([01]) fraction=([\d.]+) normal=(-?[\d.]+),(-?[\d.]+),(-?[\d.]+) material=(\S+)")
WORLD_FIELDS = ("event", "areas", "portals", "viewing", "frame", "views", "area", "entities", "drawn", "batches", "arena")

# Area 0 holds 14 batches and area 1 12. Each view: its camera (eye, target), the doorway portal's
# state, the area the view starts in, the areas portal culling lets it draw and their batches,
# and whether the GS gate's reference pixels apply (its cameras).
VIEWS = (
    ("room0", (-480, -340, 150, -180, 100, 40), "open", 0, 2, 26, True),
    ("room1", (480, 330, 150, 240, -150, 40), "open", 1, 2, 26, True),
    ("doorway", (-440, 0, 64, 0, 0, 64), "open", 0, 2, 26, True),
    ("clip", (-20, -40, 40, -300, 300, 20), "open", 0, 1, 14, True),
    ("away", (-200, 0, 64, -600, 0, 64), "open", 0, 1, 14, False),
    ("closed", (-440, 0, 64, 0, 0, 64), "closed", 0, 1, 14, False),
)


# Traces through the area models' s16 mirror: straight down onto room 0's floor, across both
# areas into the divider's post (x = -8), and through the open doorway, which hits nothing. Each:
# start, end, hit, fraction, normal and the material's prefix.
TRACES = (
    ((-100, 300, 100), (-100, 300, -100), 1, 0.5, (0.0, 0.0, 1.0), "textures/graybox/floor_"),
    ((-100, 200, 64), (100, 200, 64), 1, 0.46, (-1.0, 0.0, 0.0), "textures/graybox/trim"),
    ((-100, 0, 64), (100, 0, 64), 0, 1.0, (0.0, 0.0, 0.0), "none"),
)


def barrier(identity: str, name: str) -> str:
    return f"{identity}_{name}"


def view_commands(identity: str, name: str, camera: tuple, portal: str) -> list[str]:
    # The capture runs two frames after the camera is set, and the status two frames after that.
    return [f"ps2_world portal 1 {portal}", "ps2_world view " + " ".join(str(value) for value in camera), "wait 2",
            f'ps2_capture "world-{name}.tga"', "wait 2", "ps2_world status", f'echo "{barrier(identity, name)}"']


def driver(identity: str):
    """Yield each command batch with the condition, over the log, that ends its wait."""
    # The console starts open; closed, it slides away over real time before the first view.
    traces = ["ps2_world trace " + " ".join(str(value) for value in start + end) for start, end, *_ in TRACES]
    yield ([f'echo "{barrier(identity, "begin")}"', "ps2_consoleKey toggle", "wait 60", "r_znear 4", "ps2_world load maps/graybox",
            *traces, f'echo "{barrier(identity, "loaded")}"'], lambda log: echoed(log, barrier(identity, "loaded")))
    for name, camera, portal, *_ in VIEWS:
        yield view_commands(identity, name, camera, portal), lambda log, name=name: echoed(log, barrier(identity, name))
    yield ["ps2_world portal 1 open", "ps2_world unload", f'echo "{barrier(identity, "unloaded")}"', "quit"], \
        lambda log: run_headless.SHUTDOWN_COMPLETE in log


def world_lines(log: str) -> list[dict]:
    return [dict(zip(WORLD_FIELDS, (match[0],) + tuple(int(value) for value in match[1:]))) for match in WORLD_RE.findall(log)]


def classify(log: str, identity: str) -> tuple[bool, str, dict]:
    """Check the session log: the cooked load, every view's portal culling and capture, the unload and shutdown."""
    if TARGET_CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
        return False, "crash or fatal diagnostic", {}
    if not LOAD_RE.search(log):
        return False, "the cooked graybox did not load with its areas, portals and nodes", {}
    if "WORLD load failed" in log:
        return False, "the render world refused the map", {}
    markers = ["begin", "loaded"] + [name for name, *_ in VIEWS] + ["unloaded"]
    # Echo output, not the submitted line, is the barrier.
    spans = [re.search(re.escape(barrier(identity, name)) + r" +\r?\n", log) for name in markers]
    if any(span is None for span in spans):
        return False, "missing session barrier", {}
    positions = [span.start() for span in spans]
    if positions != sorted(positions):
        return False, "session barriers ran out of order", {}
    if run_headless.SHUTDOWN_COMPLETE not in log:
        return False, "the session did not shut down", {}
    loaded = [line for line in world_lines(log) if line["event"] == "loaded"]
    if len(loaded) != 1 or (loaded[0]["areas"], loaded[0]["portals"]) != (2, 1):
        return False, "the render world did not take the cooked areas and portal", {}
    traces = TRACE_RE.findall(log[positions[0]:positions[1]])
    if len(traces) != len(TRACES):
        return False, "missing world trace", {}
    for (start, end, hit, fraction, normal, material), found in zip(TRACES, traces):
        normal_found = tuple(float(value) for value in found[2:5])
        if (int(found[0]) != hit or abs(float(found[1]) - fraction) > 0.002 or
                any(abs(a - b) > 0.002 for a, b in zip(normal_found, normal)) or not found[5].startswith(material)):
            return False, f"the trace from {start} to {end} found {found}; expected hit={hit} fraction={fraction} {normal} {material}", {}
    views = {}
    for index, (name, _, _, area, drawn, batches, _) in enumerate(VIEWS):
        span = log[positions[index + 1]:positions[index + 2]]
        captures = CAPTURE_RE.findall(span)
        statuses = [line for line in world_lines(span) if line["event"] == "status"]
        if len(captures) != 1 or captures[0][0] != f"world-{name}.tga" or len(statuses) != 1:
            return False, f"{name}: missing capture or world status", {}
        status = statuses[0]
        presents = int(captures[0][2])
        if status["viewing"] != 1 or status["frame"] <= presents:
            return False, f"{name}: the view was not drawn after its capture", {}
        if (status["area"], status["drawn"], status["batches"]) != (area, drawn, batches) or status["entities"] < drawn:
            return False, (f"{name}: started in area {status['area']} and drew {status['drawn']} areas, {status['batches']} batches; "
                           f"expected area {area}, {drawn} areas, {batches} batches"), {}
        views[name] = {"capture": captures[0][0], "presents": presents, "status": status}
    unloads = [line for line in world_lines(log[positions[-2]:positions[-1]]) if line["event"] == "unloaded"]
    if len(unloads) != 1 or (unloads[0]["areas"], unloads[0]["portals"], unloads[0]["viewing"]) != (1, 0, 0):
        return False, "the world did not unload to the empty single-area world", {}
    memory = run_headless.MEMORY_RE.findall(log)
    if len(memory) != 1 or tuple(map(int, memory[0][:3])) != BASELINE or memory[0][:3] != memory[0][3:6]:
        return False, "production shutdown did not recover the startup allocation ledger", {}
    arena = max(view["status"]["arena"] for view in views.values())
    return True, "the cooked graybox loaded, drew through the portal flow in every view, unloaded and shut down", {
        "views": views, "arena_bytes": arena}


def doorway_box(standard: str) -> tuple[int, int, int, int]:
    """The doorway's screen rectangle from the doorway camera, with a margin for the divider's thickness."""
    height = 448 if standard == "NTSC" else 512
    # The camera is 440 units from the doorway's centre, looking straight at it with a 90-degree
    # vertical field of view; the opening is 128 units square.
    half = int(64.0 / 436.0 * height / 2.0) + 3
    return 256 - half, height // 2 - half, 256 + half, height // 2 + half


def verify_closed(open_path: Path, closed_path: Path, standard: str) -> dict:
    """The closed doorway shows the clear colour where area 1 was and matches the open view everywhere else."""
    width, height, opened = renderer_capture.read_tga(open_path)
    closed_size = renderer_capture.read_tga(closed_path)
    if (width, height) != closed_size[:2]:
        raise ValueError("the open and closed doorway captures differ in size")
    closed = closed_size[2]
    x0, y0, x1, y1 = doorway_box(standard)
    changed = 0
    for y in range(height):
        for x in range(width):
            offset = (y * width + x) * 3
            if opened[offset:offset + 3] == closed[offset:offset + 3]:
                continue
            if not (x0 <= x <= x1 and y0 <= y <= y1):
                raise ValueError(f"closing the doorway portal changed pixel ({x},{y}) outside the doorway")
            changed += 1
    centre = (height // 2 * width + width // 2) * 3
    actual = tuple(closed[centre:centre + 3])
    if any(abs(a - e) > renderer_capture.PIXEL_TOLERANCE for a, e in zip(actual, CLEAR_RGB)):
        raise ValueError(f"the closed doorway's centre is {actual}; expected the clear colour {CLEAR_RGB}")
    if changed < (x1 - x0) * (y1 - y0) // 2:
        raise ValueError(f"closing the doorway portal changed only {changed} pixels")
    return {"changed_pixels": changed, "doorway_box": [x0, y0, x1, y1], "centre_rgb": list(actual)}


def verify_captures(output: Path, standard: str) -> dict:
    results = {}
    for name, *_, reference in VIEWS:
        if reference:
            results[name] = renderer_capture.verify_world_scene(output / f"world-{name}.tga", standard, name)
    results["closed"] = verify_closed(output / "world-doorway.tga", output / "world-closed.tga", standard)
    return results


def stage_world(build: str, output: Path, standard: str, catalog: Path, world: Path) -> Path:
    """A visible console package on the authored headless data, with the graybox materials and cooked world."""
    package = stage(build, output, standard, catalog=catalog)
    materials = package / "base/materials"
    materials.mkdir(parents=True, exist_ok=True)
    shutil.copy2(GRAYBOX_ROOT / "materials/graybox.mtr", materials / "graybox.mtr")
    maps = package / "base/maps"
    maps.mkdir(parents=True, exist_ok=True)
    shutil.copy2(world, maps / "graybox.d3wd")
    return package


def run(args: argparse.Namespace, standard: str, version: str) -> bool:
    if smoke.emulator_is_running(args.emulator):
        raise ValueError("an emulator is already running; the world session requires an isolated emulator")
    smoke.check_config(args.config)
    if not args.world.is_file():
        raise ValueError(f"missing cooked world {args.world}; run make world-cook")
    identity = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_world_" + uuid.uuid4().hex[:12]
    output = stage_world(args.build, REPO_ROOT / "build/test-results" / identity, standard, args.catalog, args.world)
    report = json.loads((output / "report.json").read_text())
    for name in report["flag_stamps"]:
        shutil.copy2(REPO_ROOT / name, output / Path(name).name)
    shutil.copy2(args.config, output / "PCSX2.ini.snapshot")
    (output / "base/d3bfg.cfg").write_text(f'echo "{identity}_ready"\nps2_maxFrames 0\n')
    (output / "console.txt").write_bytes(b"")
    (output / "boot-data.json").write_text(json.dumps(boot_data(output), indent=2) + "\n")
    command = [str(args.emulator), "-batch", "-elf", str(output / "d3bfg.elf"), "-logfile", str(output / "emulog.txt")]
    steps = driver(identity)
    until = None
    started = time.monotonic()
    deadline = started + STARTUP_SECONDS * args.timeout_scale
    completed, failure = False, "watchdog timeout during startup"
    with (output / "stdout.txt").open("wb") as stdout:
        process = subprocess.Popen(command, cwd=output, stdout=stdout, stderr=subprocess.STDOUT)
        try:
            while True:
                log = run_headless.read_log(output)
                if TARGET_CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
                    failure = "crash or fatal diagnostic"
                    break
                if run_headless.SHUTDOWN_COMPLETE in log:
                    completed = True
                    break
                if until is None:
                    if echoed(log, identity + "_ready"):
                        commands, until = next(steps)
                        run_console.append_commands(output, *commands)
                        deadline = time.monotonic() + SESSION_SECONDS * args.timeout_scale
                elif until(log):
                    try:
                        commands, until = next(steps)
                        run_console.append_commands(output, *commands)
                    except StopIteration:
                        until = lambda _: False
                if time.monotonic() > deadline:
                    failure = "watchdog timeout"
                    break
                if process.poll() is not None:
                    failure = f"emulator exited with status {process.returncode} after {time.monotonic() - started:.1f} s"
                    break
                time.sleep(0.1)
        finally:
            smoke.stop_owned_process(process)
    log = run_headless.read_log(output)
    passed, reason, details = classify(log, identity) if completed else (False, failure, {})
    captures = {}
    if passed:
        try:
            captures = verify_captures(output, standard.upper())
        except (OSError, ValueError) as error:
            passed, reason = False, str(error)
    if process.returncode != 0:
        passed, reason = False, f"emulator exited with status {process.returncode}"
    summary = {"passed": passed, "reason": reason, "configuration": args.build, "standard": standard, "command": command,
               "emulator_version": version, "elapsed_seconds": round(time.monotonic() - started, 1),
               "emulator_returncode": process.returncode, "boot_data": boot_data(output), "world_sha256": sha256(args.world),
               "console_sha256": sha256(output / "console.txt"), "details": details, "captures": captures,
               "pixel_inspection_required": True}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"{'PASS' if passed else 'FAIL'} world-{standard}: {reason}\nArtifacts: {output}", flush=True)
    if not passed:
        print(log[-6000:], flush=True)
    return passed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", choices=("debug", "release"), default="debug")
    parser.add_argument("--catalog", type=Path, default=REPO_ROOT / "build/ui-retail",
                        help="the cooked retail UI (make ui-cook) the visible console draws its charset from")
    parser.add_argument("--world", type=Path, default=DEFAULT_WORLD, help="the cooked graybox (make world-cook)")
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
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        parser.exit(2, "error: " + str(error) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
