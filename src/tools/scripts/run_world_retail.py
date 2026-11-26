#!/usr/bin/env python3
# ================================================================================================
# File: run_world_retail.py
# Brief: Draw mars_city1, cooked from the retail .bproc, through id's render world: area 2 from the player start, portal culling, whole-image references, the R2 gate's probes, a frame-logged tour and load cycles.
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
may win: a missing area or misplaced geometry fails it. Each view lit by its lightmaps alone must
match the same reference with the lightmap pass modelled: the cook's pages, decoded back out of the
D3TX, sampled bilinearly at each pixel's perspective-correct lightmap UV and multiplied into the
vertex colour. Closing the doorway's portal must leave only area 2, against a reference of area 2
alone. The R2 gate's probes follow, each held to the .bproc as id's proc loader reads it
(world_probes.py): sampled PointInArea, AreasAreConnected with the doorway open, closed as a door
closes it and reopened, and the sound world's walk from the start to the corridor, which the closed
doorway must lengthen by s_doorDistanceAdd. The first frames of the frame log's camera tour play into
ps2_framelog. In debug builds the free-flight camera then
flies from the start under scripted stick input, to the pose the same steps give here, and its
capture must match the reference from that pose. The world is then unloaded, loaded and unloaded
again four times, the last three leaving the ledger exactly where the first did, and the session
quits, recovering the startup ledger. With --stage --perf it stages a package that plays the whole
tour into framelog.txt by itself, for a console run."""

import argparse
from collections import namedtuple
import json
import math
from pathlib import Path
import re
import shutil

import frame_log
import renderer_capture
import run_headless
import run_pcsx2_test as smoke
import run_world
import world_probes
import world_reference
import world_tour
from run_console import BASELINE, TARGET_CRASH_RE
from stage_headless import REPO_ROOT, stage


MAP = "maps/game/mars_city1"
DEFAULT_WORLD = REPO_ROOT / "build/world/mars_city1.d3wd"
JOB = REPO_ROOT / "config/world_mars_city1.json"
# The frame log's camera tour (world_tour.py), staged beside the map.
TOUR = REPO_ROOT / "config/world_mars_city1_tour.path"
TOUR_PACKAGE_PATH = f"{MAP}_tour.path"
# Seams and depth ties aside, a capture matches its reference at 99.7 % or more; a capture missing
# a visible area (or drawing one the reference does not) scores well below.
MIN_MATCH = 99.0
# The doorway between area 2 and the corridor (area 50): its handle and the areas it joins.
DOORWAY_PORTAL = (55, 2, 50)

# Each view: its camera (eye, target as ps2_world view takes them), the doorway portal's state
# while it draws (None leaves it open), the area the view starts in, the areas portal culling lets
# it draw (their batches come from the cook report, so either topology passes), and the areas its
# reference holds (None for every area).
View = namedtuple("View", "name camera portal area areas reference_areas")
# info_player_start_2 stands at (-3632, -3416, 192); the eye is 68 units above it.
START = (-3632, -3416, 260)
VIEWS = (
    View("east", START + (-3332, -3416, 260), None, 2, (2,), None),
    View("west", START + (-3932, -3416, 260), None, 2, (2,), None),
    View("north", START + (-3632, -3116, 260), None, 2, (2, 50, 51), None),
    View("south", START + (-3632, -3716, 260), None, 2, (2,), None),
    View("doorway", (-3772, -3300, 260, -3772, -2900, 260), None, 2, (2, 50, 51), None),
    View("corridor", (-3772, -3000, 260, -3700, -3500, 240), None, 50, (50, 2), None),
    View("closed", START + (-3632, -3116, 260), "closed", 2, (2,), (2,)),
)

# Traces through the area models' s16 mirror from the player's eye: straight down onto the floor
# grate under the start, and west into the computer rack the cook merged into area 2
# (tim_func_static_2650, a mesh prop; before 2026-11-24 the trace reached the hall's wall,
# textures/mcity/stewall3b at 0.383, as id's Trace would not have: it traces static entity models).
# Each: start, end, hit, fraction, normal and the material. A Python ray cast gives the same
# fractions and materials: over the .bproc's triangles for the grate, over the cooked D3WD's packets
# for the rack (normal within 0.006: the mirror's s16 positions round its small faces apart).
TRACES = (
    (START, (-3632, -3416, -200), 1, 0.148, (0.0, 0.0, 1.0), "textures/base_floor/sflgrate2"),
    (START, (-4632, -3416, 260), 1, 0.049, (0.999, 0.047, 0.001), "models/mapobjects/cpu/comrack2"),
)


# A decal projected at load straight down onto the floor grate under the start, where one flat
# .bproc triangle (z = 192) holds the whole 32-unit square: once a view sees area 2, its model must
# hold the square's area inside the square's bounds.
DECALS = (((-3632, -3416, 192), (0, 0, -1), 32, 8, "textures/base_floor/sflgrate2"),)
DECALS_EXPECTED = {"entities": 1, "area": 32.0 * 32.0, "bounds": (-3648.0, -3432.0, 192.0, -3616.0, -3400.0, 192.0)}


# The free-flight step (debug builds): from the east view, ps2_world fly input holds the sticks
# (left x, left y, right x, right y, -128 to 127), a libpad button mask and a frame count; each
# frame advances 1/30 s whatever it took. Here: 96 units forward while turning 110 degrees left
# and looking a little down, which keeps the eye inside the start room (area 2).
FLY_START = VIEWS[0]
FLY_INPUT = (0, -63, -119, 34, 0, 30)
FLY_AREA = 2
# The presentation's flight constants (presentation.cpp).
FLY_SPEED, FLY_TURN_RATE, FLY_DEAD_ZONE, FLY_MAX_PITCH, FLY_STEP = 256.0, 120.0, 24, 89.0, 1.0 / 30.0
# The pose the target reaches, within its single-precision steps.
FLY_POSITION_TOLERANCE, FLY_ANGLE_TOLERANCE = 0.25, 0.05
FLY_RE = re.compile(r"\[D3BFG\] WORLD fly (\w+) on=([01]) eye=(-?[\d.]+),(-?[\d.]+),(-?[\d.]+) yaw=(-?[\d.]+) pitch=(-?[\d.]+) ")


def fly_axis(value: int) -> float:
    """A stick's deflection past the dead zone, rescaled to [-1, 1] (presentation.cpp's FlyAxis)."""
    if value > FLY_DEAD_ZONE:
        return min(1.0, (value - FLY_DEAD_ZONE) / (128 - FLY_DEAD_ZONE))
    if value < -FLY_DEAD_ZONE:
        return max(-1.0, (value + FLY_DEAD_ZONE) / (128 - FLY_DEAD_ZONE))
    return 0.0


def fly_axes(yaw: float, pitch: float) -> tuple[tuple, tuple]:
    """Forward and left of idAngles(pitch, yaw, 0).ToMat3() (pitch positive looks down)."""
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    return (cp * cy, cp * sy, -sp), (-sy, cy, 0.0)


def fly_pose(start: tuple, script: tuple) -> tuple[tuple, float, float]:
    """The eye, yaw and pitch after the scripted frames, starting level and looking along +x
    (the east view), as UpdateFly steps them: turn, then move along the new axes."""
    left_x, left_y, right_x, right_y, buttons, frames = script
    if buttons != 0:
        raise ValueError("the flight model covers stick input only")
    eye, yaw, pitch = list(start), 0.0, 0.0
    for _ in range(frames):
        yaw = (yaw - fly_axis(right_x) * FLY_TURN_RATE * FLY_STEP) % 360.0
        pitch = max(-FLY_MAX_PITCH, min(FLY_MAX_PITCH, pitch + fly_axis(right_y) * FLY_TURN_RATE * FLY_STEP))
        forward, left = fly_axes(yaw, pitch)
        for lane in range(3):
            eye[lane] += FLY_SPEED * FLY_STEP * (-fly_axis(left_y) * forward[lane] - fly_axis(left_x) * left[lane])
    return tuple(eye), yaw, pitch


def fly_camera() -> tuple[tuple, tuple]:
    """The flight's final camera as an eye and a target 100 units along its view."""
    eye, yaw, pitch = fly_pose(FLY_START.camera[:3], FLY_INPUT)
    forward, _ = fly_axes(yaw, pitch)
    return eye, tuple(eye[lane] + 100.0 * forward[lane] for lane in range(3))


def fly_commands(identity: str) -> list[str]:
    # The capture runs after the scripted frames end (the pad, untouched, then moves nothing).
    return ["ps2_world view " + " ".join(str(value) for value in FLY_START.camera), "ps2_world fly on",
            "ps2_world fly input " + " ".join(str(value) for value in FLY_INPUT), f"wait {FLY_INPUT[5] + 4}", "ps2_world fly status",
            'ps2_capture "world-fly.tga"', "wait 2", "ps2_world status", "ps2_world lightmaps on", "wait 2", 'ps2_capture "world-fly-light.tga"',
            "wait 2", "ps2_world textures on", "wait 2", 'ps2_capture "world-fly-lit.tga"', "wait 2", "ps2_world textures off",
            "ps2_world lightmaps off", "ps2_world fly off",
            f'echo "{run_world.barrier(identity, "fly")}"']


def check_fly(span: str) -> tuple[bool, str, dict]:
    """The flight's log lines: on at the east view's pose, the status at the modelled pose, its
    capture and a world status that saw the view."""
    lines = FLY_RE.findall(span)
    if [line[0] for line in lines] != ["on", "status", "off"] or [line[1] for line in lines] != ["1", "1", "0"]:
        return False, f"the flight did not log on, status and off: {[line[0] for line in lines]}", {}
    expected = (tuple(FLY_START.camera[:3]), 0.0, 0.0), fly_pose(FLY_START.camera[:3], FLY_INPUT)
    for line, (eye, yaw, pitch) in zip(lines, expected):
        found = tuple(float(value) for value in line[2:5])
        yaw_found, pitch_found = float(line[5]), float(line[6])
        yaw_error = abs((yaw_found - yaw + 180.0) % 360.0 - 180.0)
        if (any(abs(a - b) > FLY_POSITION_TOLERANCE for a, b in zip(found, eye)) or yaw_error > FLY_ANGLE_TOLERANCE or
                abs(pitch_found - pitch) > FLY_ANGLE_TOLERANCE):
            return False, f"flight {line[0]} at {found} yaw={yaw_found} pitch={pitch_found}; expected {eye} yaw={yaw:.2f} pitch={pitch:.2f}", {}
    captures = run_world.CAPTURE_RE.findall(span)
    statuses = [line for line in run_world.world_lines(span) if line["event"] == "status"]
    if ([capture[0] for capture in captures] != ["world-fly.tga", "world-fly-light.tga", "world-fly-lit.tga"] or len(statuses) != 1 or
            statuses[0]["viewing"] != 1):
        return False, "the flight's captures or world status are missing", {}
    if statuses[0]["area"] != FLY_AREA:
        return False, f"the flight ended in area {statuses[0]['area']}; expected {FLY_AREA}", {}
    eye, target = fly_camera()
    return True, "", {"eye": [round(value, 2) for value in eye], "target": [round(value, 2) for value in target], "status": statuses[0]}


def world_report(world: Path) -> dict:
    """The cook's report beside the D3WD (asset_reexport world)."""
    return json.loads(world.with_name(world.stem + ".report.json").read_text())


def view_batches(report: dict, view: View) -> int:
    """The batches of the areas the view draws, as the cook laid them out."""
    return sum(report["area_list"][area]["batches"] for area in view.areas)


# Cumulative since the map loaded; passes counts material passes drawn, pass_fallbacks those whose
# texture the arena could not hold (they are left out, the frame keeps its diffuse textures); the
# sky's faces likewise (sky_faces, sky_fallbacks), and repacks the passes that emptied the arena to
# place their textures without holes.
# Streaming (cumulative too): textures read in and their bytes; the EE copies held at the status, their
# bytes and the most they held; refused reads; draws left out because a read had not landed; and the
# passes that waited for their reads after a jump (each view's camera move is one), with the time;
# then the passes whose planned textures exceeded the arena even empty, the textures given up mid-pass
# for one a batch needed, the state blocks re-placed textures took and the batches that found none
# left, and the bytes uploaded; the times the arena gave pages back to UI textures, the bytes it gave,
# and the times it grew back over pages they freed.
TEXTURES_RE = re.compile(r"\[D3BFG\] WORLD textures enabled=([01]) arena=(\d+) resident=(\d+) resident_bytes=(\d+) uploads=(\d+) "
                         r"evictions=(\d+) fallbacks=(\d+) lightmaps=([01]) pages=(\d+) unlit=(\d+) passes=(\d+) pass_fallbacks=(\d+) "
                         r"sky_faces=(\d+) sky_fallbacks=(\d+) repacks=(\d+) stream_loads=(\d+) stream_bytes=(\d+) copies=(\d+) "
                         r"copy_bytes=(\d+) copy_peak=(\d+) stream_failures=(\d+) stream_misses=(\d+) stream_waits=(\d+) "
                         r"stream_wait_ms=(\d+) overflows=(\d+) pass_evictions=(\d+) moved_states=(\d+) moved_fallbacks=(\d+) "
                         r"upload_bytes=(\d+) vram_yields=(\d+) yielded_bytes=(\d+) regrows=(\d+) stream_reads=(\d+)")
TEXTURE_FIELDS = ("enabled", "arena", "resident", "resident_bytes", "uploads", "evictions", "fallbacks", "lightmaps", "pages", "unlit",
                  "passes", "pass_fallbacks", "sky_faces", "sky_fallbacks", "repacks", "stream_loads", "stream_bytes", "copies",
                  "copy_bytes", "copy_peak", "stream_failures", "stream_misses", "stream_waits", "stream_wait_ms", "overflows",
                  "pass_evictions", "moved_states", "moved_fallbacks", "upload_bytes", "vram_yields", "yielded_bytes", "regrows",
                  "stream_reads")
TEXTURE_LOAD_RE = re.compile(rf"\[D3BFG\] world textures {re.escape(MAP)}\.d3tx: textures=(\d+) materials=(\d+) pages=(\d+) "
                             r"passes=(\d+) areas=(\d+) bytes=(\d+) tables=(\d+) streamed")


def view_commands(identity: str, view: View) -> list[str]:
    # The untextured, unlit capture (compared with the vertex-colour reference) runs two frames after
    # the camera is set and the status two frames after that; then the same view lit by its
    # lightmaps alone, then textured and lit, and the residency of that pass.
    handle = DOORWAY_PORTAL[0]
    portal = [f"ps2_world portal {handle} {view.portal}"] if view.portal else []
    reopen = [f"ps2_world portal {handle} open"] if view.portal else []
    return [*portal, "ps2_world view " + " ".join(str(value) for value in view.camera), "wait 2", f'ps2_capture "world-{view.name}.tga"',
            "wait 2", "ps2_world status", "ps2_world lightmaps on", "wait 2", f'ps2_capture "world-{view.name}-light.tga"', "wait 2",
            "ps2_world textures on", "wait 2", f'ps2_capture "world-{view.name}-lit.tga"', "wait 2",
            "ps2_world textures status", "ps2_world textures off", "ps2_world lightmaps off", *reopen,
            f'echo "{run_world.barrier(identity, view.name)}"']


# The UI's textures share the texture range with the world's arena: opening the console in game makes
# the arena give back its end for the console's character page, and once the console has been closed
# past the idle time (world_textures.cpp's kUiIdleFrames, 30) the arena grows back over it.
SHARE_IDLE_WAIT = 45


def share_commands(identity: str) -> list[str]:
    return ["ps2_consoleKey toggle", "wait 4", 'ps2_capture "world-console.tga"', "wait 2", "ps2_world textures status",
            "ps2_consoleKey toggle", f"wait {SHARE_IDLE_WAIT}", "ps2_world textures status", f'echo "{run_world.barrier(identity, "share")}"']


def check_share(span: str, before: dict, full_arena: int) -> tuple[bool, str, dict]:
    """The console step: its capture, the arena giving pages back while the console shows, and the
    arena whole again after."""
    textures = [dict(zip(TEXTURE_FIELDS, map(int, match))) for match in TEXTURES_RE.findall(span)]
    captures = run_world.CAPTURE_RE.findall(span)
    if len(textures) != 2 or [capture[0] for capture in captures] != ["world-console.tga"]:
        return False, "the console step's capture or texture statuses are missing", {}
    shown, closed = textures
    if shown["vram_yields"] <= before["vram_yields"] or shown["arena"] >= full_arena:
        return False, f"the world's arena did not give pages back for the console: {shown}", {}
    if closed["regrows"] <= shown["regrows"] or closed["arena"] != full_arena:
        return False, f"the world's arena did not grow back after the console closed: {closed}", {}
    return True, "", {"shown": shown, "closed": closed}


# The R2 gate's probes, held to the .bproc as id's proc loader reads it (world_probes): sampled
# PointInArea, AreasAreConnected for each portal attribute with the doorway open, closed (as a door
# closes it: every attribute blocked) and open again, and the sound world's walk from the player's
# eye to a sound in the corridor, which the closed doorway must lengthen by s_doorDistanceAdd.
SOUND_SOURCE = (-3772, -3000, 260)
SOUND_MAX = 4096.0
# The walk's virtual origins and LengthFast round a little apart from the model's doubles.
SOUND_TOLERANCE = 1.0
CONNECTED_KINDS = ("view", "location", "air")
AREA_RE = re.compile(r"\[D3BFG\] WORLD area points=(\d+) areas=(-?\d+(?:,-?\d+)*)")
CONNECTED_RE = re.compile(r"\[D3BFG\] WORLD connected kind=(\w+) first=(\d+) groups=(\d+(?:,\d+)*)")
SOUND_RE = re.compile(r"\[D3BFG\] WORLD sound listener_area=(-?\d+) source_area=(-?\d+) direct=([\d.]+) spatialized=([\d.]+) "
                      r"origin=(-?[\d.]+),(-?[\d.]+),(-?[\d.]+) door_add=([\d.]+)")
PORTAL_RE = re.compile(r"\[D3BFG\] WORLD portal handle=(\d+) blocking=(\d+)")
LOADER_RE = re.compile(r"\[D3BFG\] LOADER bytes=(\d+) chunks=(\d+) seeks=(\d+) io_us=(\d+) kib_per_s=(\d+) queue=(\d+) device=(\S+)")
LOADER_FIELDS = ("bytes", "chunks", "seeks", "io_us", "kib_per_s", "queue")
# PS_BLOCK_ALL: a closed door blocks the view, locations and air.
BLOCK_ALL = 7


def expected_probes(data: Path) -> dict:
    """The probes' answers from the retail .bproc: its counts, the samples with their areas, each
    portal state's groups and the sound walk."""
    bproc = world_reference.retail_bproc(data, json.loads(JOB.read_text()))
    samples = world_probes.sample_points(bproc.areas, bproc.portals, bproc.nodes)
    closed = frozenset({DOORWAY_PORTAL[0]})
    states = (frozenset(), closed, frozenset())
    return {"areas": bproc.area_count, "portals": len(bproc.portals), "nodes": len(bproc.nodes), "samples": samples,
            "groups": [world_probes.connected_groups(bproc.area_count, bproc.portals, state) for state in states],
            "sounds": [world_probes.sound_distance(bproc.portals, bproc.nodes, START[:3], SOUND_SOURCE, SOUND_MAX, state)
                       for state in states]}


def probe_commands(identity: str, probes: dict) -> list[str]:
    sound = "ps2_world sound " + " ".join(str(value) for value in START[:3] + SOUND_SOURCE) + f" {SOUND_MAX:g}"
    handle = DOORWAY_PORTAL[0]
    return [*world_probes.area_commands(probes["samples"]), "ps2_world connected", sound, f"ps2_world portal {handle} closed",
            "ps2_world connected", sound, f"ps2_world portal {handle} open", "ps2_world connected", sound, "ps2_world loader",
            f'echo "{run_world.barrier(identity, "probes")}"']


def check_probes(span: str, probes: dict) -> tuple[bool, str, dict]:
    """Every sample in its area, each state's groups for every attribute, the doorway blocking
    everything once closed and nothing reopened, and the sound walks."""
    found = []
    for count, areas in AREA_RE.findall(span):
        values = [int(value) for value in areas.split(",")]
        if len(values) != int(count):
            return False, "a ps2_world area line does not list its points", {}
        found += values
    expected = [area for _, area in probes["samples"]]
    if len(found) != len(expected):
        return False, f"PointInArea answered {len(found)} of {len(expected)} samples", {}
    wrong = [(point, area, got) for (point, area), got in zip(probes["samples"], found) if area != got]
    if wrong:
        return False, f"PointInArea differs from the .bproc's nodes at {len(wrong)} samples, first {wrong[0]}", {}
    groups = {}
    for kind, first, values in CONNECTED_RE.findall(span):
        groups.setdefault(kind, []).append((int(first), [int(value) for value in values.split(",")]))
    states = []
    for state in range(3):
        for kind in CONNECTED_KINDS:
            lines = groups.get(kind, [])
            areas_found, first = [], 0
            while lines and lines[0][0] == first and (not areas_found or first == len(areas_found)):
                areas_found += lines.pop(0)[1]
                first = len(areas_found)
                if first == len(probes["groups"][state]):
                    break
            if areas_found != probes["groups"][state]:
                return False, f"AreasAreConnected ({kind}, portal state {state}) differs from the .bproc's portals: {areas_found}", {}
        states.append(probes["groups"][state])
    portals = [(int(handle), int(blocking)) for handle, blocking in PORTAL_RE.findall(span)]
    if portals != [(DOORWAY_PORTAL[0], BLOCK_ALL), (DOORWAY_PORTAL[0], 0)]:
        return False, f"the doorway did not close for everything and reopen: {portals}", {}
    sounds = SOUND_RE.findall(span)
    if len(sounds) != 3:
        return False, "missing sound walk", {}
    walks = []
    for line, model in zip(sounds, probes["sounds"]):
        areas_found = (int(line[0]), int(line[1]))
        spatialized = float(line[3])
        if areas_found != (model["listener_area"], model["source_area"]) or abs(spatialized - model["spatialized"]) > SOUND_TOLERANCE:
            return False, f"the sound walk found areas {areas_found} and {spatialized}; the model {model}", {}
        walks.append(spatialized)
    # The doorway is the corridor's only way to the start: closed, the sound goes s_doorDistanceAdd further.
    door = float(sounds[1][7])
    if not walks[1] > walks[0] or abs(walks[1] - walks[0] - door) > SOUND_TOLERANCE or abs(walks[2] - walks[0]) > 0.01:
        return False, f"the closed doorway did not gate the sound by s_doorDistanceAdd ({door}): {walks}", {}
    loader = LOADER_RE.findall(span)
    if len(loader) != 1:
        return False, "missing loader totals", {}
    return True, "", {"samples": len(expected), "solid": expected.count(-1), "areas_hit": len(set(expected) - {-1}),
                      "groups": len(set(states[0])), "sound": walks, "door_add": door,
                      "loader": dict(zip(LOADER_FIELDS, map(int, loader[0][:6])), device=loader[0][6])}


# The frame log over the first TOUR_FRAMES frames of the tour, textured and lit: the path must play
# them and the log hold a row for each (PCSX2's timings are not the console's; the rows and their
# columns are what the gate checks).
TOUR_FRAMES = 240
PATH_RE = re.compile(r"\[D3BFG\] WORLD path (started|done) (?:file=\S+ )?keys=(\d+) frames=(\d+)")
FRAMELOG_RE = re.compile(r"\[D3BFG\] FRAMELOG written path=(\S+) rows=(\d+) bytes=(\d+) frame_us_mean=(\d+) frame_us_max=(\d+)")


def tour_commands() -> list[str]:
    # One row more than the path plays: its end, not a full log, writes the file.
    return ["ps2_world textures on", "ps2_world lightmaps on", f"ps2_framelog start {TOUR_FRAMES + 1}",
            f"ps2_world path {TOUR_PACKAGE_PATH} {TOUR_FRAMES}"]


def tour_ended(log: str) -> bool:
    return "[D3BFG] WORLD path done" in log and ("[D3BFG] FRAMELOG written" in log or "[D3BFG] FRAMELOG write failed" in log)


def check_tour(span: str) -> tuple[bool, str, dict]:
    """The path started, played TOUR_FRAMES frames and ended; the frame log wrote that many rows (the
    frame it started in opens the first), and the streaming refused nothing."""
    paths = PATH_RE.findall(span)
    if [path[0] for path in paths] != ["started", "done"] or int(paths[1][2]) != TOUR_FRAMES:
        return False, f"the tour did not play {TOUR_FRAMES} frames: {paths}", {}
    logs = FRAMELOG_RE.findall(span)
    if len(logs) != 1 or int(logs[0][1]) != TOUR_FRAMES:
        return False, f"the frame log did not write {TOUR_FRAMES} rows: {logs}", {}
    textures = [dict(zip(TEXTURE_FIELDS, map(int, match))) for match in TEXTURES_RE.findall(span)]
    if len(textures) != 1 or textures[0]["stream_failures"] != 0 or textures[0]["moved_fallbacks"] != 0:
        return False, f"the tour's textures failed to stream: {textures}", {}
    loader = LOADER_RE.findall(span)
    return True, "", {"keys": int(paths[0][1]), "frames": TOUR_FRAMES, "rows": int(logs[0][1]), "file_bytes": int(logs[0][2]),
                      "frame_us_mean": int(logs[0][3]), "frame_us_max": int(logs[0][4]), "textures": textures[0],
                      "loader": dict(zip(LOADER_FIELDS, map(int, loader[0][:6]))) if loader else None}


# Map load/unload cycles: one warms what a first load leaves resident (declarations, pools), then
# CYCLES more must each return the allocation ledger to exactly where the warm one left it. Each draws
# the start's view textured and lit for a few frames, so the texture copies stream in and leave.
CYCLES = 3
# id's tokenizer splits a label at a dash: "cycle0", not "cycle-0".
CYCLE_MEMORY_RE = re.compile(r"\[D3BFG\] MEMORY cycle(\d+) requested=(\d+) backing=(\d+) allocations=(\d+) ")


def cycle_commands(identity: str, index: int) -> list[str]:
    # The console history holds every HostFs line, and its slots keep the longest one's buffer: clear
    # it before the checkpoint, as the UI sessions do.
    return [f"ps2_world load {MAP}", "ps2_world view " + " ".join(str(value) for value in VIEWS[0].camera), "ps2_world textures on",
            "ps2_world lightmaps on", "wait 6", "ps2_world unload", "wait 2", "clearHistory", f"ps2_memory cycle{index}",
            f'echo "{run_world.barrier(identity, f"cycle{index}")}"']


def check_cycles(span: str) -> tuple[bool, str, dict]:
    ledgers = [(int(index), tuple(map(int, rest))) for index, *rest in CYCLE_MEMORY_RE.findall(span)]
    if [index for index, _ in ledgers] != list(range(CYCLES + 1)):
        return False, f"missing cycle ledgers: {[index for index, _ in ledgers]}", {}
    warm = ledgers[0][1]
    for index, ledger in ledgers[1:]:
        if ledger != warm:
            return False, f"cycle {index} left the ledger at {ledger}; the warm cycle left {warm}", {}
    switches = run_world.SWITCH_RE.findall(span)
    if len(switches) != 2 * (CYCLES + 1):
        return False, f"the cycles switched the display {len(switches)} times; expected {2 * (CYCLES + 1)}", {}
    return True, "", {"cycles": CYCLES, "ledger": dict(zip(("requested", "backing", "allocations"), warm))}


def driver(identity: str, fly: bool, probes: dict):
    """Yield each command batch with the condition, over the log, that ends its wait; 'fly' adds
    the free-flight step (debug builds)."""

    def until(name: str):
        return lambda log: run_world.echoed(log, run_world.barrier(identity, name))

    traces = ["ps2_world trace " + " ".join(str(value) for value in start + end) for start, end, *_ in TRACES]
    # The console starts open; closed, it slides away over real time before the first view.
    yield ([f'echo "{run_world.barrier(identity, "begin")}"', "ps2_consoleKey toggle", "wait 60", "r_znear 4", f"ps2_world load {MAP}",
            "ps2_world textures off", "ps2_world lightmaps off", *traces, *run_world.decal_commands(DECALS), "ps2_world decals",
            f'echo "{run_world.barrier(identity, "loaded")}"'],
           until("loaded"))
    # Loading the map leaves the front end: two frames later the device draws in the preference.
    yield ["wait 2", f'echo "{run_world.barrier(identity, "ingame")}"'], until("ingame")
    for view in VIEWS:
        yield view_commands(identity, view), until(view.name)
    yield share_commands(identity), until("share")
    yield probe_commands(identity, probes), until("probes")
    yield tour_commands(), tour_ended
    yield ["ps2_world loader", "ps2_world textures status", "ps2_world textures off", "ps2_world lightmaps off", "ps2_world off",
           f'echo "{run_world.barrier(identity, "tour")}"'], until("tour")
    if fly:
        yield fly_commands(identity), until("fly")
    yield ["ps2_world decals", "ps2_world unload", "ps2_world decals", f'echo "{run_world.barrier(identity, "unloaded")}"'], until("unloaded")
    yield ["wait 2", f'echo "{run_world.barrier(identity, "frontend")}"'], until("frontend")
    for index in range(CYCLES + 1):
        yield cycle_commands(identity, index), until(f"cycle{index}")
    yield ["quit"], lambda log: run_headless.SHUTDOWN_COMPLETE in log


def classify(log: str, identity: str, standard: str, report: dict, probes: dict, fly: bool = False) -> tuple[bool, str, dict]:
    """Check the session log: the cooked load with the report's counts and the .bproc's ('probes'),
    the traces, every view's portal culling and capture, the probes, the tour's frame log, the flight
    when 'fly' asks for it, the display changes into and out of the map, the unload, the ledger
    cycles and shutdown."""
    if TARGET_CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
        return False, "crash or fatal diagnostic", {}
    load = re.compile(rf"\[D3BFG\] world {re.escape(MAP)}\.d3wd: areas={report['areas']} surfaces={report['surfaces']} "
                      rf"batches={report['batches']} portals={report['portals']} nodes={report['nodes']} ")
    package = TEXTURE_LOAD_RE.search(log)
    if not load.search(log) or not package:
        return False, "the cooked map did not load with the cook report's areas, surfaces, batches, portals and nodes", {}
    # id's proc loader would read the same counts from the .bproc.
    if (report["areas"], report["portals"], report["nodes"]) != (probes["areas"], probes["portals"], probes["nodes"]):
        return False, (f"the cooked map's areas, portals and nodes {(report['areas'], report['portals'], report['nodes'])} differ from "
                       f"the .bproc's {(probes['areas'], probes['portals'], probes['nodes'])}"), {}
    # The entities whose models the cook merged travel with the world, for the game's spawn to skip.
    merged = report.get("static_models", {}).get("merged_entities")
    if merged is not None and not re.search(rf"\[D3BFG\] world {re.escape(MAP)}\.d3wd: .* merged_entities={merged}\b", log):
        return False, f"the cooked map did not load the cook report's {merged} merged entities", {}
    if "WORLD load failed" in log:
        return False, "the render world refused the map", {}
    cycles = [f"cycle{index}" for index in range(CYCLES + 1)]
    markers = (["begin", "loaded", "ingame"] + [view.name for view in VIEWS] + ["share", "probes", "tour"] + (["fly"] if fly else []) +
               ["unloaded", "frontend"] + cycles)
    # Echo output, not the submitted line, is the barrier.
    spans = [re.search(re.escape(run_world.barrier(identity, name)) + r" +\r?\n", log) for name in markers]
    if any(span is None for span in spans):
        return False, "missing session barrier", {}
    positions = [span.start() for span in spans]
    if positions != sorted(positions):
        return False, "session barriers ran out of order", {}
    at = dict(zip(markers, positions))
    before_unload = "fly" if fly else "tour"
    if run_headless.SHUTDOWN_COMPLETE not in log:
        return False, "the session did not shut down", {}
    loaded = [line for line in run_world.world_lines(log[:at["loaded"]]) if line["event"] == "loaded"]
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
    queued = run_world.decal_reports(log[positions[0]:positions[1]])
    if len(queued) != 1 or not run_world.decals_match(queued[0], None):
        return False, "decals existed before any view saw their entity", {}
    ingame = run_world.SWITCH_RE.findall(log[positions[1]:positions[2]])
    expected = run_world.expected_switch(standard, run_world.default_display(standard))
    if len(ingame) != 1 or run_world.parse_switch(ingame[0]) != expected:
        return False, f"loading the map did not switch the display once to {expected[3]}", {}
    views = {}
    for index, view in enumerate(VIEWS):
        span = log[positions[index + 2]:positions[index + 3]]
        captures = run_world.CAPTURE_RE.findall(span)
        statuses = [line for line in run_world.world_lines(span) if line["event"] == "status"]
        textures = [dict(zip(TEXTURE_FIELDS, map(int, match))) for match in TEXTURES_RE.findall(span)]
        if (run_world.SWITCH_RE.search(span) or len(captures) != 3 or captures[0][0] != f"world-{view.name}.tga" or
                captures[1][0] != f"world-{view.name}-light.tga" or captures[2][0] != f"world-{view.name}-lit.tga" or
                len(statuses) != 1 or len(textures) != 1):
            return False, f"{view.name}: missing capture, world status or texture status", {}
        # The lit pass drew every texture and lightmap page it needed: none fell back. Each view's
        # camera move and switch is a jump, so its passes waited for their reads: none was missing.
        if (textures[0]["enabled"] != 1 or textures[0]["uploads"] == 0 or textures[0]["fallbacks"] != 0 or textures[0]["resident"] == 0 or
                textures[0]["lightmaps"] != 1 or textures[0]["pages"] == 0 or textures[0]["unlit"] != 0 or
                textures[0]["stream_failures"] != 0 or textures[0]["stream_misses"] != 0 or textures[0]["stream_waits"] == 0 or
                textures[0]["moved_fallbacks"] != 0):
            return False, f"{view.name}: the lit pass did not hold its textures and pages: {textures[0]}", {}
        # Neighbouring files gather into one loader request: the loads took fewer requests than textures.
        if not 0 < textures[0]["stream_reads"] < textures[0]["stream_loads"]:
            return False, f"{view.name}: {textures[0]['stream_loads']} textures took {textures[0]['stream_reads']} reads; none gathered", {}
        bits = renderer_capture.draw_bits(run_world.default_display(standard).profile)
        if int(captures[0][4]) != bits:
            return False, f"{view.name}: captured in the {captures[0][4]}-bit profile; expected {bits}", {}
        status = statuses[0]
        presents = int(captures[0][2])
        if status["viewing"] != 1 or status["frame"] <= presents:
            return False, f"{view.name}: the view was not drawn after its capture", {}
        drawn, batches = len(view.areas), view_batches(report, view)
        if (status["area"], status["drawn"], status["batches"]) != (view.area, drawn, batches) or status["entities"] < drawn:
            return False, (f"{view.name}: started in area {status['area']} and drew {status['drawn']} areas, {status['batches']} batches; "
                           f"expected area {view.area}, {drawn} areas, {batches} batches"), {}
        views[view.name] = {"capture": captures[0][0], "light": captures[1][0], "lit": captures[2][0], "bits": bits, "presents": presents,
                            "status": status, "textures": textures[0]}
    # mars_city1's windows show the sky: the textured pass drew its box (PAL's north view leaves it out
    # for the diffuse textures, the others have room).
    if max(view["textures"]["sky_faces"] for view in views.values()) == 0:
        return False, "the textured pass never drew the sky box", {}
    share_span = log[at[VIEWS[-1].name]:at["share"]]
    passed, failure, share = check_share(share_span, views[VIEWS[-1].name]["textures"], max(view["textures"]["arena"] for view in views.values()))
    if not passed:
        return False, failure, {}
    passed, failure, probed = check_probes(log[at["share"]:at["probes"]], probes)
    if not passed:
        return False, failure, {}
    passed, failure, tour = check_tour(log[at["probes"]:at["tour"]])
    if not passed:
        return False, failure, {}
    # The package streamed: its EE copies never held the whole file.
    file_bytes = int(package.group(6))
    copy_peak = max(view["textures"]["copy_peak"] for view in views.values())
    if copy_peak >= file_bytes:
        return False, f"the texture copies held {copy_peak} bytes, the whole {file_bytes}-byte package", {}
    unload_span = log[at[before_unload]:at["unloaded"]]
    if run_world.SWITCH_RE.search(log[:at["loaded"]] + log[at["share"]:at[before_unload]] + unload_span + log[at[cycles[-1]]:]):
        return False, "the display mode changed outside its steps", {}
    unloads = [line for line in run_world.world_lines(unload_span) if line["event"] == "unloaded"]
    if len(unloads) != 1 or (unloads[0]["areas"], unloads[0]["portals"], unloads[0]["viewing"]) != (1, 0, 0):
        return False, "the world did not unload to the empty single-area world", {}
    decals = run_world.decal_reports(unload_span)
    if len(decals) != 2 or not run_world.decals_match(decals[0], DECALS_EXPECTED):
        return False, f"the floor decal does not cover its square on the grate: {decals[:1]}", {}
    if not run_world.decals_match(decals[1], None):
        return False, "decals outlived the unloaded world", {}
    back = run_world.SWITCH_RE.findall(log[at["unloaded"]:at["frontend"]])
    expected = run_world.expected_switch(standard, run_world.front_end_display(standard))
    if len(back) != 1 or run_world.parse_switch(back[0]) != expected:
        return False, f"the front end did not return to {expected[3]} once after the unload", {}
    passed, failure, cycled = check_cycles(log[at["frontend"]:at[cycles[-1]]])
    if not passed:
        return False, failure, {}
    memory = run_headless.MEMORY_RE.findall(log)
    if len(memory) != 1 or tuple(map(int, memory[0][:3])) != BASELINE or memory[0][:3] != memory[0][3:6]:
        return False, "production shutdown did not recover the startup allocation ledger", {}
    flight = None
    if fly:
        passed, failure, flight = check_fly(log[at["tour"]:at["fly"]])
        if not passed:
            return False, failure, {}
    arena = max(view["status"]["arena"] for view in views.values())
    return True, ("mars_city1 loaded from its cooked .bproc, drew area 2 and the corridor through the portal flow, closed the "
                  "doorway down to area 2, answered the area, connection and sound probes as the .bproc does, played the tour into "
                  "the frame log, built its floor decal over the mirror, unloaded, cycled the map with an exact ledger and shut down"), \
        {"views": views, "arena_bytes": arena, "decals": decals[0], "flight": flight, "texture_file_bytes": file_bytes, "copy_peak": copy_peak,
         "share": share, "probes": probed, "tour": tour, "cycles": cycled}


def verify_frame_log(output: Path) -> dict:
    """The tour's framelog.txt: complete, a row for each frame, every row's columns; its summary."""
    log = frame_log.parse((output / "framelog.txt").read_text())
    if log["end"] != TOUR_FRAMES or len(log["rows"]) != TOUR_FRAMES:
        raise ValueError(f"framelog.txt holds {len(log['rows'])} rows and ends at {log['end']}; expected {TOUR_FRAMES}")
    if [row["frame"] for row in log["rows"]] != list(range(TOUR_FRAMES)) or any(row["frame_us"] <= 0 for row in log["rows"]):
        raise ValueError("framelog.txt's rows are out of order or hold empty frames")
    summary = frame_log.summarize(log)
    (output / "framelog-summary.txt").write_text(frame_log.format_summary(summary) + "\n")
    return summary


def verify_captures(output: Path, data: Path, world: Path, standard: str, fly: bool = False) -> dict:
    """Every unlit capture against the reference rasterization of its view, written beside it as
    reference-<view>.png and difference-<view>.png (mismatches in magenta). The reference draws
    the cooked world's own triangles and vertex colours: the baked chroma is the cook's, and the
    cook's geometry is held to the .bproc by its own checks (max_position_error in the report).
    Every capture lit by the lightmaps alone against the same reference with the lightmap pass:
    each lit batch's page (the cook's <stem>.lightmaps.raw, read back out of the D3TX) sampled at
    the pixel's lightmap UV and multiplied into its colour."""
    job = json.loads(JOB.read_text())
    handle, first, second = DOORWAY_PORTAL
    portals = {entry[0]: entry[1:3] for entry in world_reference.retail_portals(data, job)}
    if portals.get(handle) != (first, second):
        raise ValueError(f"portal {handle} joins areas {portals.get(handle)}; expected {first} and {second}")
    bits = renderer_capture.draw_bits(run_world.default_display(standard).profile)
    pages_path = world.with_name(world.stem + ".lightmaps.raw")
    if not pages_path.is_file():
        raise ValueError(f"missing {pages_path}; run make world-cook-retail")
    pages = world_reference.load_lightmap_pages(pages_path)
    images = output / "references"
    images.mkdir(exist_ok=True)
    results = {}

    def check(name: str, capture: str, triangles: list, camera: tuple, lit: bool):
        share = world_reference.compare_image(output / capture, triangles, camera, bits, world_reference.CLEAR_RGB, images,
                                              name + ("-light" if lit else ""), pages if lit else None)
        results[name + ("-light" if lit else "")] = {"match_percent": round(share, 2), "minimum_percent": MIN_MATCH}
        if share < MIN_MATCH:
            raise ValueError(f"{capture}: {share:.2f}% of the capture matches its reference; expected at least {MIN_MATCH}%")

    for lit in (False, True):
        everything = world_reference.load_cooked_triangles(world, lightmaps=lit)
        for view in VIEWS:
            triangles = (everything if view.reference_areas is None else
                         world_reference.load_cooked_triangles(world, set(view.reference_areas), lightmaps=lit))
            check(view.name, f"world-{view.name}{'-light' if lit else ''}.tga", triangles, (view.camera[:3], view.camera[3:]), lit)
        if fly:
            check("fly", f"world-fly{'-light' if lit else ''}.tga", everything, fly_camera(), lit)
    return results


def stage_retail_world(build: str, output: Path, standard: str, catalog: Path, world: Path, report: dict) -> Path:
    """A visible console package on the authored headless data, with the cooked map and its
    materials declared collision-only (stand-ins until the cook compiles material passes)."""
    package = stage(build, output, standard, catalog=catalog)
    materials = package / "base/materials"
    materials.mkdir(parents=True, exist_ok=True)
    # Without stages id refuses the stand-ins decals, which the opaque retail materials accept.
    # The report lists a material once per parm variant; id names declarations case-insensitively.
    seen = set()
    names = [name for name in report["material_names"] if not (name.lower() in seen or seen.add(name.lower()))]
    (materials / "world_stand_in.mtr").write_text("".join(f"{name} {{ solid collision forceOverlays }}\n" for name in names))
    maps = package / "base" / Path(MAP).parent
    maps.mkdir(parents=True, exist_ok=True)
    shutil.copy2(world, maps / world.name)
    # The map's textures (asset_reexport world with the job's textures section).
    textures = world.with_suffix(".d3tx")
    if not textures.is_file():
        raise ValueError(f"missing cooked textures {textures}; run make world-cook-retail")
    shutil.copy2(textures, maps / textures.name)
    shutil.copy2(TOUR, package / "base" / TOUR_PACKAGE_PATH)
    return package


def stage_flight(args: argparse.Namespace, standard: str) -> Path:
    """A package for flying through the map by hand (make run-world-retail, debug builds): its
    console.txt closes the console, loads the map, sets the player start's east view and turns
    the free-flight camera on."""
    if not args.world.is_file():
        raise ValueError(f"missing cooked world {args.world}; run make world-cook-retail")
    if args.build != "debug":
        raise ValueError("the free-flight camera exists in debug builds only")
    package = stage_retail_world(args.build, args.stage, standard, args.catalog, args.world, world_report(args.world))
    commands = ["ps2_consoleKey toggle", "r_znear 4", f"ps2_world load {MAP}", "ps2_world view " + " ".join(str(value) for value in FLY_START.camera),
                "ps2_world fly on"]
    (package / "console.txt").write_text("".join(command + "\n" for command in commands))
    return package


# The console run (make stage-perf-world-retail, a release build on a USB drive or the hard disk):
# the package's startup config loads the map and plays the whole tour into the frame log, then opens
# the console on how it ended. framelog.txt lands in the package folder.
PERF_CONSOLE_WAIT = 60


def perf_commands() -> list[str]:
    frames = world_tour.path_frames(world_tour.read_path(TOUR))
    return ["ps2_consoleKey toggle", f"wait {PERF_CONSOLE_WAIT}", "r_znear 4", f"ps2_world load {MAP}", "ps2_world textures on",
            "ps2_world lightmaps on", f"ps2_framelog start {frames + 1}", f"ps2_world path {TOUR_PACKAGE_PATH}", f"wait {frames + 30}",
            "ps2_world loader", "ps2_world textures status", "ps2_consoleKey toggle"]


def stage_perf(args: argparse.Namespace, standard: str) -> Path:
    """A package that plays the tour into the frame log by itself: its startup config, which a
    console run reads from the drive, holds the commands HostFs would otherwise send."""
    if not args.world.is_file():
        raise ValueError(f"missing cooked world {args.world}; run make world-cook-retail")
    package = stage_retail_world(args.build, args.stage, standard, args.catalog, args.world, world_report(args.world))
    (package / "base/d3bfg.cfg").write_text("".join(command + "\n" for command in perf_commands()))
    (package / "console.txt").write_bytes(b"")
    return package


def run(args: argparse.Namespace, standard: str, version: str) -> bool:
    if not args.world.is_file():
        raise ValueError(f"missing cooked world {args.world}; run make world-cook-retail")
    report = world_report(args.world)
    probes = expected_probes(args.data)
    # The free-flight camera exists in debug builds only.
    fly = args.build == "debug"

    def verify(output: Path) -> dict:
        captures = verify_captures(output, args.data, args.world, standard, fly)
        captures["frame_log"] = verify_frame_log(output)
        return captures

    return run_world.run_session(args, standard, version, "world_retail",
                                 lambda output: stage_retail_world(args.build, output, standard, args.catalog, args.world, report),
                                 lambda identity: driver(identity, fly, probes),
                                 lambda log, identity: classify(log, identity, standard, report, probes, fly), verify, args.world)


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
    parser.add_argument("--stage", type=Path, help="only stage a package here that starts flying from the player start (debug builds)")
    parser.add_argument("--perf", action="store_true", help="with --stage: a package that plays the camera tour into framelog.txt")
    args = parser.parse_args()
    if not 0 < args.timeout_scale <= 10:
        parser.error("timeout scale must be positive and at most 10")
    try:
        if args.stage:
            package = (stage_perf if args.perf else stage_flight)(args, args.standard or "ntsc")
            print(f"staged {package}", flush=True)
            return 0
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
