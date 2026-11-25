#!/usr/bin/env python3
# ================================================================================================
# File: world_probes.py
# Brief: The R2 gate's expected answers from a map's .bproc, apart from the cook: PointInArea over the area nodes, AreasAreConnected over the portals and the sound world's walk through them.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""What id's render and sound worlds answer for a map, computed here from the .bproc's own nodes
and portals (world_reference.bproc_world), so the gate holds the cooked D3WD the runtime loads to
the map as id's proc loader reads it: the area a point is in (idRenderWorldLocal::PointInArea),
which areas the portals join for each portal attribute (AreasAreConnected, with some portals
closed), and the distance a sound travels through the portals to a listener, a closed one adding
s_doorDistanceAdd (idSoundWorldLocal::ResolveOrigin). It also picks the gate's sample points."""

import math
import random

# A point whose walk passes this close to a node's plane may fall either side in single precision;
# the samples avoid it, the queries report it as None.
PLANE_MARGIN = 0.05
# idSoundWorldLocal::ResolveOrigin's MAX_PORTAL_TRACE_DEPTH, and s_doorDistanceAdd's default.
MAX_PORTAL_TRACE_DEPTH = 10
DOOR_DISTANCE_ADD = 150.0
# ps2_world area takes this many points a command.
POINTS_PER_COMMAND = 16


def point_in_area(nodes: list, point: tuple, margin: float = PLANE_MARGIN) -> int | None:
    """PointInArea: the area holding 'point', -1 in solid; None when the walk passes within 'margin'
    of a plane (a distance of exactly 0 takes the back child, as id's 'd > 0' test does)."""
    index = 0
    while True:
        a, b, c, d, front, back = nodes[index]
        distance = point[0] * a + point[1] * b + point[2] * c + d
        if abs(distance) < margin:
            return None
        child = front if distance > 0 else back
        if child == 0:
            return -1
        if child < 0:
            return -1 - child
        index = child


def area_bounds(areas: dict, area: int) -> tuple | None:
    """The bounds of an area's model vertices, None for an empty model."""
    positions = [vertex[0] for _, vertices, _ in areas.get(area, ()) for vertex in vertices]
    if not positions:
        return None
    return tuple(min(p[k] for p in positions) for k in range(3)), tuple(max(p[k] for p in positions) for k in range(3))


def portal_plane(points: list) -> tuple:
    """idWinding::GetPlane: the normal (p1 - centre) x (p0 - centre) normalized, through p0."""
    centre = tuple(sum(p[k] for p in points) / len(points) for k in range(3))
    v1 = _sub(points[0], centre)
    v2 = _sub(points[1], centre)
    normal = _normalize(_cross(v2, v1))
    return normal, -_dot(normal, points[0]), centre


def sample_points(areas: dict, portals: list, nodes: list, per_area: int = 6, seed: int = 0x52A7E) -> list:
    """Deterministic probe points, each with the area PointInArea gives it: 'per_area' points inside
    every area's bounds, the two sides of every portal 4 units off its centre, and the corners of the
    map's bounds pushed 512 units out (solid). Coordinates have two decimals, as the commands send
    them, and no point lies within PLANE_MARGIN of a plane its walk tests."""
    rng = random.Random(seed)
    samples = []

    def add(point):
        point = tuple(round(value, 2) for value in point)
        area = point_in_area(nodes, point)
        if area is None:
            return False
        samples.append((point, area))
        return True

    bounds = [area_bounds(areas, area) for area in sorted(areas)]
    for box in bounds:
        if box is None:
            continue
        for _ in range(per_area):
            for _ in range(16):
                if add(tuple(rng.uniform(box[0][k], box[1][k]) for k in range(3))):
                    break
    for _, _, points in portals:
        normal, _, centre = portal_plane(points)
        for side in (4.0, -4.0):
            add(tuple(centre[k] + side * normal[k] for k in range(3)))
    present = [box for box in bounds if box is not None]
    low = [min(box[0][k] for box in present) - 512.0 for k in range(3)]
    high = [max(box[1][k] for box in present) + 512.0 for k in range(3)]
    for corner in range(8):
        add(tuple(high[k] if corner & (1 << k) else low[k] for k in range(3)))
    return samples


def area_commands(samples: list) -> list[str]:
    """ps2_world area commands asking for every sample, POINTS_PER_COMMAND at a time."""
    commands = []
    for first in range(0, len(samples), POINTS_PER_COMMAND):
        chunk = samples[first:first + POINTS_PER_COMMAND]
        commands.append("ps2_world area " + " ".join(f"{value:.2f}" for point, _ in chunk for value in point))
    return commands


def connected_groups(area_count: int, portals: list, closed: frozenset = frozenset()) -> list[int]:
    """AreasAreConnected as ps2_world connected reports it: for every area, the lowest area the
    portals join it to, the portals whose handles (1 up, in file order) are in 'closed' blocked."""
    parent = list(range(area_count))

    def find(area):
        while parent[area] != area:
            parent[area] = parent[parent[area]]
            area = parent[area]
        return area

    for index, (a, b, _) in enumerate(portals):
        if index + 1 in closed:
            continue
        first, second = find(a), find(b)
        if first != second:
            parent[max(first, second)] = min(first, second)
    lowest = {}
    for area in range(area_count):
        lowest.setdefault(find(area), area)
    return [lowest[find(area)] for area in range(area_count)]


def sound_distance(portals: list, nodes: list, listener: tuple, source: tuple, max_distance: float, closed: frozenset = frozenset(),
                   door_add: float = DOOR_DISTANCE_ADD) -> dict:
    """The distance ps2_world sound reports: the straight line within the listener's area, otherwise
    idSoundWorldLocal::ResolveOrigin's shortest walk from the source's area through the portals (a
    virtual origin on each portal: where the line to the listener crosses its plane, slid inside its
    edges), at most MAX_PORTAL_TRACE_DEPTH portals deep, each closed one adding 'door_add', or
    'max_distance' when no walk is shorter."""
    listener_area = point_in_area(nodes, listener, margin=0.0)
    source_area = point_in_area(nodes, source, margin=0.0)
    direct = _length(_sub(source, listener))
    result = {"listener_area": listener_area, "source_area": source_area, "direct": direct, "spatialized": direct}
    if listener_area in (None, -1) or source_area in (None, -1) or listener_area == source_area:
        return result
    exits = {}
    for index, (a, b, points) in enumerate(portals):
        exits.setdefault(a, []).append((b, points, index + 1))
        exits.setdefault(b, []).append((a, list(reversed(points)), index + 1))
    best = [max_distance]

    def walk(depth, chain, area, distance, origin):
        if distance >= best[0]:
            return
        if area == listener_area:
            best[0] = min(best[0], distance + _length(_sub(origin, listener)))
            return
        if depth == MAX_PORTAL_TRACE_DEPTH:
            return
        for other, points, handle in exits.get(area, ()):
            if other in chain:
                continue
            normal, d, centre = portal_plane(points)
            ray = _sub(listener, origin)
            across = _dot(normal, ray)
            if across == 0.0:
                virtual = centre
            else:
                scale = -(_dot(normal, origin) + d) / across
                virtual = tuple(origin[k] + scale * ray[k] for k in range(3))
                for i in range(len(points)):
                    j = (i + 1) % len(points)
                    edge_normal = _cross(normal, _sub(points[j], points[i]))
                    inside = _dot(edge_normal, _sub(virtual, points[j]))
                    if inside > 0:
                        length = _length(edge_normal)
                        unit = tuple(value / length for value in edge_normal)
                        virtual = tuple(virtual[k] - inside / length * unit[k] for k in range(3))
            step = _length(_sub(virtual, origin))
            walk(depth + 1, chain | {area}, other, distance + step + (door_add if handle in closed else 0.0), virtual)

    walk(0, frozenset(), source_area, 0.0, source)
    result["spatialized"] = best[0]
    return result


def _sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _length(a):
    return math.sqrt(_dot(a, a))


def _normalize(a):
    length = _length(a)
    return tuple(value / length for value in a) if length > 0 else (0.0, 0.0, 0.0)
