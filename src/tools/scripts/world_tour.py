#!/usr/bin/env python3
# ================================================================================================
# File: world_tour.py
# Brief: Generate a map's camera tour for the frame log: a walk from the player start through every area the portals reach, at eye height, in ps2_world path keyframes.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""The frame log's fixed camera path (ps2_world path, presentation.cpp). From the map's .bproc
alone: the walk starts at the gate's player-start eye, visits every area the open portals reach in
depth-first order and walks back the way it came, so each doorway is crossed both ways. Inside an
area it stands at eye height over the area's floors (68 units over an upward-facing surface with
room above) and goes from the doorway it entered by to a central standing point and on to the
next doorway, by the shortest chain of straight steps that PointInArea keeps inside the area and
the two areas a doorway joins (sampled every 8 units over the .bproc's nodes). It walks at
WALK_SPEED units a frame and turns where it stands at TURN_RATE degrees a frame, looking along
its way (pitched with the slope). Areas the portals do not reach from the start are left out.

The output is a committed file (config/world_<map>_tour.path); regenerate it with this script,
never by hand: python3 src/tools/scripts/world_tour.py --data gamedata/d3_bfg"""

import argparse
import heapq
import json
import math
from pathlib import Path

import world_probes
import world_reference

REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_JOB = REPO_ROOT / "config/world_mars_city1.json"
# The gate's player-start eye (run_world_retail.START): info_player_start_2 with a 68-unit eye.
DEFAULT_START = (-3632.0, -3416.0, 260.0)
DEFAULT_YAW = 0.0

EYE_HEIGHT = 68.0
# Room the eye needs above its floor, and the floor's least upward slope (the normal's z).
HEADROOM = 80.0
FLOOR_NORMAL_Z = 0.7
# The standing points tried in an area: a grid of this pitch over its bounds.
GRID_STEP = 96.0
# Steps longer than this are not tried, and every step is sampled at this spacing.
MAX_STEP = 1024.0
SAMPLE_SPACING = 8.0
# Units and degrees a frame: about a run at 30 frames a second, and a quick turn.
WALK_SPEED = 7.0
TURN_RATE = 6.0
MAX_PITCH = 25.0
# ps2_world path's limits (presentation.cpp kMaxPathKeys, kMaxPathSegmentFrames).
MAX_KEYS = 4096
MAX_SEGMENT_FRAMES = 3600


def area_triangles(areas: dict, area: int) -> list:
    """The area model's triangles as (corners, upward), 'upward' when their vertex normals average
    above FLOOR_NORMAL_Z (the .bproc's normal bytes, 0 to 255 for -1 to 1)."""
    triangles = []
    for _, vertices, indices in areas.get(area, ()):
        for first in range(0, len(indices) - 2, 3):
            corners = [vertices[indices[first + k]] for k in range(3)]
            normal_z = sum(corner[1][2] / 255.0 * 2.0 - 1.0 for corner in corners) / 3.0
            triangles.append((tuple(corner[0] for corner in corners), normal_z > FLOOR_NORMAL_Z))
    return triangles


def vertical_hits(triangles: list, x: float, y: float) -> list:
    """Every triangle the vertical line through (x, y) crosses: (z, upward)."""
    hits = []
    for corners, upward in triangles:
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = corners
        if x < min(ax, bx, cx) or x > max(ax, bx, cx) or y < min(ay, by, cy) or y > max(ay, by, cy):
            continue
        det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(det) < 1e-9:
            continue
        u = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / det
        v = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / det
        if u < 0.0 or v < 0.0 or u + v > 1.0:
            continue
        hits.append((u * az + v * bz + (1.0 - u - v) * cz, upward))
    return hits


def standing_points(areas: dict, nodes: list, area: int) -> list:
    """Eye points over the area's floors, on a GRID_STEP grid: EYE_HEIGHT over an upward-facing
    surface with HEADROOM clear above it, PointInArea in the area."""
    box = world_probes.area_bounds(areas, area)
    if box is None:
        return []
    triangles = area_triangles(areas, area)
    points = []
    columns = max(1, int((box[1][0] - box[0][0]) // GRID_STEP))
    rows = max(1, int((box[1][1] - box[0][1]) // GRID_STEP))
    for column in range(columns):
        for row in range(rows):
            x = box[0][0] + (column + 0.5) * (box[1][0] - box[0][0]) / columns
            y = box[0][1] + (row + 0.5) * (box[1][1] - box[0][1]) / rows
            hits = vertical_hits(triangles, x, y)
            for floor, upward in hits:
                if not upward:
                    continue
                if any(floor + 1.0 < z < floor + HEADROOM for z, _ in hits):
                    continue
                eye = (round(x, 2), round(y, 2), round(floor + EYE_HEIGHT, 2))
                if world_probes.point_in_area(nodes, eye, margin=0.0) == area:
                    points.append(eye)
    return points


def step_clear(nodes: list, start: tuple, end: tuple, allowed: set) -> bool:
    """Whether every sample of the step lies in an allowed area (a sample on a plane passes)."""
    length = math.dist(start, end)
    count = max(1, int(math.ceil(length / SAMPLE_SPACING)))
    for index in range(count + 1):
        t = index / count
        point = tuple(start[k] + t * (end[k] - start[k]) for k in range(3))
        area = world_probes.point_in_area(nodes, point)
        if area is not None and area not in allowed:
            return False
    return True


def route(nodes: list, points: list, start: tuple, goal: tuple, allowed: set) -> list | None:
    """The shortest chain of clear steps from 'start' to 'goal' through the standing points, both
    ends included; None when there is none."""
    if step_clear(nodes, start, goal, allowed):
        return [start, goal]
    stops = [start, goal] + points
    distance = {0: 0.0}
    previous = {}
    queue = [(0.0, 0)]
    clear = {}
    while queue:
        cost, index = heapq.heappop(queue)
        if index == 1:
            chain = [1]
            while chain[-1] != 0:
                chain.append(previous[chain[-1]])
            return [stops[i] for i in reversed(chain)]
        if cost > distance.get(index, math.inf):
            continue
        for other in range(1, len(stops)):
            if other == index:
                continue
            length = math.dist(stops[index], stops[other])
            if length > MAX_STEP or cost + length >= distance.get(other, math.inf):
                continue
            key = (min(index, other), max(index, other))
            if key not in clear:
                clear[key] = step_clear(nodes, stops[index], stops[other], allowed)
            if clear[key]:
                distance[other] = cost + length
                previous[other] = index
                heapq.heappush(queue, (cost + length, other))
    return None


def tour(areas: dict, portals: list, nodes: list, start: tuple) -> tuple[list, list]:
    """The tour's waypoints from 'start', and the areas it left out (no standing point, or no route
    in or out). Depth first over the open portals; each area's walk goes doorway, centre, doorway."""
    start_area = world_probes.point_in_area(nodes, start, margin=0.0)
    if start_area in (None, -1):
        raise ValueError(f"the start {start} is not inside an area")
    neighbours = {}
    for index, (a, b, points) in enumerate(portals):
        _, _, centre = world_probes.portal_plane(points)
        neighbours.setdefault(a, []).append((b, centre, index + 1))
        neighbours.setdefault(b, []).append((a, centre, index + 1))
    standing = {}
    centres = {}

    def centre_of(area):
        if area not in centres:
            standing[area] = standing_points(areas, nodes, area)
            points = standing[area]
            if points:
                middle = tuple(sum(p[k] for p in points) / len(points) for k in range(3))
                centres[area] = min(points, key=lambda p: math.dist(p, middle))
            else:
                centres[area] = None
        return centres[area]

    waypoints = [start]
    left_out = []
    visited = {start_area}

    def walk(area, here):
        # here: the point the walk stands at in 'area' (its centre, or the start).
        for other, doorway, _ in sorted(neighbours.get(area, ()), key=lambda entry: (entry[2], entry[0])):
            if other in visited:
                continue
            visited.add(other)
            target = centre_of(other)
            if target is None:
                left_out.append(other)
                continue
            inward = route(nodes, standing[area], here, doorway, {area, other})
            onward = route(nodes, standing[other], doorway, target, {area, other})
            if inward is None or onward is None:
                left_out.append(other)
                continue
            waypoints.extend(inward[1:] + onward[1:])
            walk(other, target)
            back = route(nodes, standing[other], target, doorway, {area, other})
            home = route(nodes, standing[area], doorway, here, {area, other})
            if back is None or home is None:
                raise ValueError(f"no way back from area {other} to area {area}")
            waypoints.extend(back[1:] + home[1:])

    centre_of(start_area)
    walk(start_area, start)
    return waypoints, sorted(left_out)


def heading(start: tuple, end: tuple) -> tuple[float, float]:
    """The yaw and pitch (positive down) looking from 'start' to 'end'."""
    dx, dy, dz = (end[k] - start[k] for k in range(3))
    yaw = math.degrees(math.atan2(dy, dx))
    pitch = max(-MAX_PITCH, min(MAX_PITCH, -math.degrees(math.atan2(dz, math.hypot(dx, dy)))))
    return yaw, pitch


def keyframes(waypoints: list, yaw: float = DEFAULT_YAW) -> list:
    """ps2_world path keyframes (frames, x, y, z, yaw, pitch): at each waypoint a turn toward the
    next one where it stands, then the walk there."""
    keys = [(0, *waypoints[0], round(yaw, 2), 0.0)]
    pitch = 0.0
    for start, end in zip(waypoints, waypoints[1:]):
        if math.dist(start, end) < 0.5:
            continue
        new_yaw, new_pitch = heading(start, end)
        turn = abs((new_yaw - yaw + 180.0) % 360.0 - 180.0)
        frames = int(math.ceil(max(turn, abs(new_pitch - pitch)) / TURN_RATE))
        if frames > 0:
            keys.append((frames, *start, round(new_yaw, 2), round(new_pitch, 2)))
        yaw, pitch = new_yaw, new_pitch
        length = math.dist(start, end)
        while length > 0.0:
            frames = int(math.ceil(min(length, WALK_SPEED * MAX_SEGMENT_FRAMES) / WALK_SPEED))
            portion = min(1.0, frames * WALK_SPEED / length)
            stop = end if portion >= 1.0 else tuple(start[k] + portion * (end[k] - start[k]) for k in range(3))
            keys.append((frames, *(round(v, 2) for v in stop), round(yaw, 2), round(pitch, 2)))
            length -= math.dist(start, stop)
            start = stop
    if len(keys) > MAX_KEYS:
        raise ValueError(f"the tour has {len(keys)} keyframes; ps2_world path takes {MAX_KEYS}")
    return keys


def path_frames(keys: list) -> int:
    """The frames ps2_world path plays: the first keyframe, then max(FRAMES, 1) for each after it."""
    return 1 + sum(max(key[0], 1) for key in keys[1:]) if keys else 0


def read_path(path: Path) -> list:
    """The keyframes of a path file (comments after //)."""
    keys = []
    for line in path.read_text().splitlines():
        line = line.split("//", 1)[0].strip()
        if line:
            fields = line.split()
            keys.append((int(fields[0]), *(float(value) for value in fields[1:6])))
    return keys


def format_path(keys: list, map_name: str, left_out: list) -> str:
    lines = [f"// {map_name}: the frame log's camera tour, generated by src/tools/scripts/world_tour.py; do not edit.",
             f"// {len(keys)} keyframes, {path_frames(keys)} frames; FRAMES X Y Z YAW PITCH (ps2_world path).",
             f"// Areas the walk leaves out: {' '.join(str(area) for area in left_out) or 'none'}."]
    lines += [f"{key[0]} {key[1]:.2f} {key[2]:.2f} {key[3]:.2f} {key[4]:.2f} {key[5]:.2f}" for key in keys]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data", type=Path, default=world_reference.RETAIL_DATA, help="the retail installation")
    parser.add_argument("--job", type=Path, default=DEFAULT_JOB, help="the map's world job (its map and containers)")
    parser.add_argument("--output", type=Path, help="default: config/world_<map>_tour.path")
    parser.add_argument("--check", action="store_true", help="only check that the output is current")
    args = parser.parse_args()
    job = json.loads(args.job.read_text())
    name = Path(job["map"]).name
    output = args.output or REPO_ROOT / f"config/world_{name}_tour.path"
    areas, portals, nodes, _ = world_reference.retail_bproc(args.data, job)
    waypoints, left_out = tour(areas, portals, nodes, DEFAULT_START)
    text = format_path(keyframes(waypoints), job["map"], left_out)
    if args.check:
        current = output.is_file() and output.read_text() == text
        print(f"{output}: {'current' if current else 'stale; regenerate it'}")
        return 0 if current else 1
    output.write_text(text)
    keys = read_path(output)
    print(f"wrote {output}: {len(keys)} keyframes, {path_frames(keys)} frames, {len(waypoints)} waypoints; left out {left_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
