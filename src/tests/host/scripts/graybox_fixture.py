#!/usr/bin/env python3
# ================================================================================================
# File: graybox_fixture.py
# Brief: Author the graybox map, the first level the PS2 world renderer draws: two rooms joined by
#        a doorway portal, with basic shapes in each. Writes the .map brushes, the .proc render
#        geometry dmap would make from them, the materials and the world cook job.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""No dmap runs on this host, so the .proc is built here from the same solids as the .map:
visible faces only (faces in contact with another solid are cut away), split into areas at the
doorway plane, T-junctions removed and every surface wound as id's loader expects."""

import argparse
import json
import math
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
FIXTURE_ROOT = Path("src/tests/data/graybox_map")
BANNER = "// This source code is released under the GNU GPL-3.0-or-later license.\n"

# Interior of the two rooms; the divider wall at x = 0 has a doorway and the area boundary.
ROOM_X = 512
ROOM_Y = 384
ROOM_Z = 256
SHELL = 16
DIVIDER = 8
DOOR_Y = 64
DOOR_Z = 128
GRID = 64
UV_UNITS = 64.0
EPSILON = 1e-4

# Linear base colours the cook shades per vertex until lightmaps exist (gate R2).
MATERIALS = {
    "textures/graybox/floor_light": (0.62, 0.62, 0.60),
    "textures/graybox/floor_dark": (0.38, 0.38, 0.40),
    "textures/graybox/wall": (0.70, 0.68, 0.64),
    "textures/graybox/ceiling": (0.42, 0.44, 0.48),
    "textures/graybox/trim": (0.58, 0.52, 0.44),
    "textures/graybox/red": (0.80, 0.22, 0.18),
    "textures/graybox/green": (0.30, 0.70, 0.30),
    "textures/graybox/blue": (0.25, 0.40, 0.80),
    "textures/graybox/yellow": (0.90, 0.80, 0.25),
    "textures/graybox/orange": (0.90, 0.50, 0.15),
    "textures/graybox/cyan": (0.25, 0.75, 0.80),
    "textures/graybox/magenta": (0.75, 0.30, 0.70),
    "textures/graybox/white": (0.90, 0.90, 0.90),
}
LIGHT_DIRECTION = (0.35, 0.25, 0.90)
AMBIENT = 0.45


# ------------------------------------------------------------------------------------------------
# Vector helpers
# ------------------------------------------------------------------------------------------------

def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def normalize(a):
    length = math.sqrt(dot(a, a))
    return scale(a, 1.0 / length)


def id_face_normal(a, b, c):
    # idPlane::FromPoints, the convention R_DeriveFacePlanes and the traces use.
    return cross(sub(a, b), sub(c, b))


# ------------------------------------------------------------------------------------------------
# Solids: outward planes (n, d) with n.p + d = 0, as brushDef3 writes them
# ------------------------------------------------------------------------------------------------

class Solid:
    def __init__(self, material, planes, mesh=None):
        self.material = material
        self.planes = [(normalize(n), d / math.sqrt(dot(n, n))) for n, d in planes]
        # A smooth solid supplies its own shared-vertex mesh instead of flat faces.
        self.mesh = mesh


def box(material, lo, hi):
    return Solid(material, [((1, 0, 0), -hi[0]), ((-1, 0, 0), lo[0]), ((0, 1, 0), -hi[1]), ((0, -1, 0), lo[1]),
                            ((0, 0, 1), -hi[2]), ((0, 0, -1), lo[2])])


def prism(center, radius, sides, z0, z1, phase=0.0):
    planes = [((0, 0, 1), -z1), ((0, 0, -1), z0)]
    apothem = radius * math.cos(math.pi / sides)
    for index in range(sides):
        angle = phase + (index + 0.5) * 2.0 * math.pi / sides
        normal = (math.cos(angle), math.sin(angle), 0.0)
        planes.append((normal, -(dot(normal, (center[0], center[1], 0.0)) + apothem)))
    return planes


def pyramid(material, center, half, height):
    apex = (center[0], center[1], height)
    corners = [(center[0] - half, center[1] - half, 0), (center[0] + half, center[1] - half, 0),
               (center[0] + half, center[1] + half, 0), (center[0] - half, center[1] + half, 0)]
    planes = [((0, 0, -1), 0.0)]
    for index in range(4):
        a = corners[index]
        b = corners[(index + 1) % 4]
        normal = normalize(cross(sub(b, a), sub(apex, a)))
        planes.append((normal, -dot(normal, a)))
    return Solid(material, planes)


def wedge(material, x_low, x_high, y0, y1, height):
    # Rises from the floor at x_high to 'height' at x_low; the slope meets the floor at x_high, so
    # that end has no face of its own.
    slope = normalize((height, 0.0, x_high - x_low))
    return Solid(material, [((-1, 0, 0), x_low), ((0, 1, 0), -y1), ((0, -1, 0), y0), ((0, 0, -1), 0.0),
                            (slope, -dot(slope, (x_high, 0.0, 0.0)))])


def rotated_box(material, center, half, z0, z1, angle):
    planes = [((0, 0, 1), -z1), ((0, 0, -1), z0)]
    for index in range(4):
        theta = angle + index * math.pi / 2.0
        normal = (math.cos(theta), math.sin(theta), 0.0)
        planes.append((normal, -(dot(normal, (center[0], center[1], 0.0)) + half)))
    return Solid(material, planes)


def icosphere(center, radius, subdivisions):
    t = (1.0 + math.sqrt(5.0)) / 2.0
    points = [normalize(p) for p in [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t),
                                     (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]]
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2),
             (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5),
             (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    for _ in range(subdivisions):
        midpoints = {}

        def midpoint(a, b):
            key = (min(a, b), max(a, b))
            if key not in midpoints:
                midpoints[key] = len(points)
                points.append(normalize(scale(add(points[a], points[b]), 0.5)))
            return midpoints[key]

        refined = []
        for a, b, c in faces:
            ab = midpoint(a, b)
            bc = midpoint(b, c)
            ca = midpoint(c, a)
            refined += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        faces = refined
    vertices = [(add(center, scale(p, radius)), p) for p in points]
    triangles = []
    for a, b, c in faces:
        # Outward faces, wound for id's convention.
        if dot(id_face_normal(vertices[a][0], vertices[b][0], vertices[c][0]), points[a]) < 0:
            b, c = c, b
        triangles.append((a, b, c))
    planes = []
    for a, b, c in triangles:
        normal = normalize(id_face_normal(vertices[a][0], vertices[b][0], vertices[c][0]))
        planes.append((normal, -dot(normal, vertices[a][0])))
    return planes, (vertices, triangles)


def column(material, center, radius, sides, height):
    # Smooth sides (radial normals) and a flat top; the bottom rests on the floor.
    vertices = []
    triangles = []
    for index in range(sides):
        angle = index * 2.0 * math.pi / sides
        normal = (math.cos(angle), math.sin(angle), 0.0)
        base = (center[0] + radius * normal[0], center[1] + radius * normal[1])
        vertices.append(((base[0], base[1], 0.0), normal))
        vertices.append(((base[0], base[1], height), normal))
    for index in range(sides):
        a = 2 * index
        b = 2 * ((index + 1) % sides)
        triangles += [(a, a + 1, b), (b, a + 1, b + 1)]
    top = len(vertices)
    for index in range(sides):
        angle = index * 2.0 * math.pi / sides
        vertices.append(((center[0] + radius * math.cos(angle), center[1] + radius * math.sin(angle), height), (0.0, 0.0, 1.0)))
    for index in range(1, sides - 1):
        triangles.append((top, top + index, top + index + 1))
    fixed = []
    for a, b, c in triangles:
        outward = vertices[a][1] if vertices[a][1][2] == 0.0 else (0.0, 0.0, 1.0)
        if dot(id_face_normal(vertices[a][0], vertices[b][0], vertices[c][0]), outward) < 0:
            b, c = c, b
        fixed.append((a, b, c))
    # The brush is the prism through the vertex ring (its sides through the ring's chords).
    planes = [((0, 0, 1), -height), ((0, 0, -1), 0.0)]
    for index in range(sides):
        p = vertices[2 * index][0]
        q = vertices[2 * ((index + 1) % sides)][0]
        normal = normalize(cross(sub(q, p), (0.0, 0.0, 1.0)))
        planes.append((normal, -dot(normal, p)))
    return Solid(material, planes, (vertices, fixed))


def solids():
    half = DIVIDER
    shell = [
        box("textures/graybox/floor_light", (-ROOM_X - SHELL, -ROOM_Y - SHELL, -SHELL), (ROOM_X + SHELL, ROOM_Y + SHELL, 0)),
        box("textures/graybox/ceiling", (-ROOM_X - SHELL, -ROOM_Y - SHELL, ROOM_Z), (ROOM_X + SHELL, ROOM_Y + SHELL, ROOM_Z + SHELL)),
        box("textures/graybox/wall", (-ROOM_X - SHELL, -ROOM_Y, 0), (-ROOM_X, ROOM_Y, ROOM_Z)),
        box("textures/graybox/wall", (ROOM_X, -ROOM_Y, 0), (ROOM_X + SHELL, ROOM_Y, ROOM_Z)),
        box("textures/graybox/wall", (-ROOM_X - SHELL, -ROOM_Y - SHELL, 0), (ROOM_X + SHELL, -ROOM_Y, ROOM_Z)),
        box("textures/graybox/wall", (-ROOM_X - SHELL, ROOM_Y, 0), (ROOM_X + SHELL, ROOM_Y + SHELL, ROOM_Z)),
    ]
    sphere_planes, sphere_mesh = icosphere((-320.0, 0.0, 120.0), 48.0, 1)
    objects = [
        # Divider: two posts and the lintel over the doorway.
        box("textures/graybox/trim", (-half, -ROOM_Y, 0), (half, -DOOR_Y, ROOM_Z)),
        box("textures/graybox/trim", (-half, DOOR_Y, 0), (half, ROOM_Y, ROOM_Z)),
        box("textures/graybox/trim", (-half, -DOOR_Y, DOOR_Z), (half, DOOR_Y, ROOM_Z)),
        # Room 0.
        box("textures/graybox/red", (-432, -272, 0), (-336, -176, 96)),
        Solid("textures/graybox/blue", prism((-200, 200), 40, 8, 0, ROOM_Z, math.pi / 8)),
        pyramid("textures/graybox/yellow", (-176, -176), 72, 128),
        wedge("textures/graybox/green", -480, -352, 64, 320, 64),
        Solid("textures/graybox/white", sphere_planes, sphere_mesh),
        # Room 1: four stairs against the far wall, a smooth column, stacked boxes.
        box("textures/graybox/orange", (384, -160, 0), (416, -32, 24)),
        box("textures/graybox/orange", (416, -160, 0), (448, -32, 48)),
        box("textures/graybox/orange", (448, -160, 0), (480, -32, 72)),
        box("textures/graybox/orange", (480, -160, 0), (512, -32, 96)),
        column("textures/graybox/cyan", (200, 200), 48, 12, 160),
        box("textures/graybox/magenta", (192, -288, 0), (320, -160, 64)),
        rotated_box("textures/graybox/magenta", (256, -224), 32, 64, 128, math.pi / 4),
    ]
    return shell, objects


# ------------------------------------------------------------------------------------------------
# Polygons
# ------------------------------------------------------------------------------------------------

def side(plane, point):
    return dot(plane[0], point) + plane[1]


def split(polygon, plane):
    # Returns (front, back): front is outside the plane (n.p + d > 0).
    distances = [side(plane, p) for p in polygon]
    if all(d <= EPSILON for d in distances):
        return None, polygon
    if all(d >= -EPSILON for d in distances):
        return polygon, None
    front = []
    back = []
    for index, point in enumerate(polygon):
        nxt = polygon[(index + 1) % len(polygon)]
        da = distances[index]
        db = distances[(index + 1) % len(polygon)]
        if da >= -EPSILON:
            front.append(point)
        if da <= EPSILON:
            back.append(point)
        if (da > EPSILON and db < -EPSILON) or (da < -EPSILON and db > EPSILON):
            t = da / (da - db)
            cut = add(point, scale(sub(nxt, point), t))
            front.append(cut)
            back.append(cut)
    return front, back


def face_polygon(solid, plane):
    normal = plane[0]
    origin = scale(normal, -plane[1])
    helper = (0.0, 0.0, 1.0) if abs(normal[2]) < 0.9 else (1.0, 0.0, 0.0)
    u = normalize(cross(helper, normal))
    v = cross(normal, u)
    size = 8192.0
    polygon = [add(origin, add(scale(u, a * size), scale(v, b * size))) for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    for other in solid.planes:
        if other is plane or polygon is None:
            continue
        _, polygon = split(polygon, other)
    return polygon


def subtract(polygon, normal, solid):
    # The parts of 'polygon' outside 'solid'. A face lying on one of the solid's planes counts as
    # inside when the two face each other (contact) and outside when they face the same way.
    # A polygon outside any one plane misses the solid: keep it whole rather than cut it into
    # fragments whose vertices would only add T-junctions.
    for plane in solid.planes:
        distances = [side(plane, p) for p in polygon]
        if all(d >= -EPSILON for d in distances):
            if not all(abs(d) <= EPSILON for d in distances) or dot(plane[0], normal) > 0:
                return [polygon]

    kept = []
    remaining = polygon
    for plane in solid.planes:
        if all(abs(side(plane, p)) <= EPSILON for p in remaining):
            if dot(plane[0], normal) > 0:
                kept.append(remaining)
                return kept
            continue
        front, back = split(remaining, plane)
        if front is not None:
            kept.append(front)
        if back is None:
            return kept
        remaining = back
    return kept


def grid_quads(origin, axis_u, axis_v, count_u, count_v):
    quads = []
    for i in range(count_u):
        for j in range(count_v):
            a = add(origin, add(scale(axis_u, i), scale(axis_v, j)))
            quads.append(((i, j), [a, add(a, axis_u), add(add(a, axis_u), axis_v), add(a, axis_v)]))
    return quads


def shell_faces():
    # The rooms' inner faces as 64-unit grids, so seams between them share their vertices.
    faces = []
    x0, x1, y0, y1 = -ROOM_X, ROOM_X, -ROOM_Y, ROOM_Y
    for (i, j), quad in grid_quads((x0, y0, 0), (GRID, 0, 0), (0, GRID, 0), (x1 - x0) // GRID, (y1 - y0) // GRID):
        material = "textures/graybox/floor_light" if (i + j) % 2 == 0 else "textures/graybox/floor_dark"
        faces.append((material, quad, (0.0, 0.0, 1.0)))
    for _, quad in grid_quads((x0, y0, ROOM_Z), (0, GRID, 0), (GRID, 0, 0), (y1 - y0) // GRID, (x1 - x0) // GRID):
        faces.append(("textures/graybox/ceiling", quad, (0.0, 0.0, -1.0)))
    walls = [((x0, y0, 0), (0, GRID, 0), (1.0, 0.0, 0.0), (y1 - y0) // GRID),
             ((x1, y1, 0), (0, -GRID, 0), (-1.0, 0.0, 0.0), (y1 - y0) // GRID),
             ((x1, y0, 0), (-GRID, 0, 0), (0.0, 1.0, 0.0), (x1 - x0) // GRID),
             ((x0, y1, 0), (GRID, 0, 0), (0.0, -1.0, 0.0), (x1 - x0) // GRID)]
    for origin, axis, normal, count in walls:
        for _, quad in grid_quads(origin, axis, (0, 0, GRID), count, ROOM_Z // GRID):
            faces.append(("textures/graybox/wall", quad, normal))
    return faces


def object_faces(shell, objects):
    faces = []
    for solid in objects:
        if solid.mesh is not None:
            continue
        for plane in solid.planes:
            polygon = face_polygon(solid, plane)
            if polygon is None or len(polygon) < 3:
                continue
            pieces = [polygon]
            for other in shell + objects:
                if other is solid:
                    continue
                pieces = [kept for piece in pieces for kept in subtract(piece, plane[0], other)]
            for piece in pieces:
                faces.append((solid.material, piece, plane[0]))
    return faces


def edge_key(a, b):
    ka = tuple(round(c, 3) for c in a)
    kb = tuple(round(c, 3) for c in b)
    return (ka, kb) if ka < kb else (kb, ka)


def remove_t_junctions(faces):
    # Only an edge no other polygon shares can open a crack: a vertex resting on an edge that two
    # polygons share exactly (a box on a floor seam) leaves the surface beneath it watertight.
    points = []
    seen = set()
    edge_uses = {}
    for _, polygon, _ in faces:
        for index, p in enumerate(polygon):
            key = tuple(round(c, 3) for c in p)
            if key not in seen:
                seen.add(key)
                points.append(p)
            edge = edge_key(p, polygon[(index + 1) % len(polygon)])
            edge_uses[edge] = edge_uses.get(edge, 0) + 1
    fixed = []
    for material, polygon, normal in faces:
        output = []
        for index, a in enumerate(polygon):
            b = polygon[(index + 1) % len(polygon)]
            output.append(a)
            if edge_uses[edge_key(a, b)] > 1:
                continue
            edge = sub(b, a)
            length2 = dot(edge, edge)
            lo = [min(a[k], b[k]) - 0.01 for k in range(3)]
            hi = [max(a[k], b[k]) + 0.01 for k in range(3)]
            inserted = []
            for p in points:
                if not all(lo[k] <= p[k] <= hi[k] for k in range(3)):
                    continue
                t = dot(sub(p, a), edge) / length2
                if t <= 1e-4 or t >= 1.0 - 1e-4:
                    continue
                offset = sub(p, add(a, scale(edge, t)))
                if dot(offset, offset) < 1e-6:
                    inserted.append((t, p))
            output += [p for _, p in sorted(inserted)]
        fixed.append((material, output, normal))
    return fixed


def triangulate(polygon, normal):
    # Convex polygons. A fan from a vertex would make zero-area triangles across collinear
    # T-junction vertices, so such polygons fan from their centroid instead.
    def collinear(index):
        a = polygon[index - 1]
        b = polygon[index]
        c = polygon[(index + 1) % len(polygon)]
        n = cross(sub(b, a), sub(c, b))
        return dot(n, n) < 1e-6

    if any(collinear(index) for index in range(len(polygon))):
        center = scale(polygon[0], 0.0)
        for p in polygon:
            center = add(center, p)
        center = scale(center, 1.0 / len(polygon))
        corners = [center] + polygon
        triangles = [(0, 1 + index, 1 + (index + 1) % len(polygon)) for index in range(len(polygon))]
    else:
        corners = list(polygon)
        triangles = [(0, index, index + 1) for index in range(1, len(polygon) - 1)]
    wound = []
    for a, b, c in triangles:
        if dot(id_face_normal(corners[a], corners[b], corners[c]), normal) < 0:
            b, c = c, b
        wound.append((a, b, c))
    return corners, wound


def planar_uv(point, normal):
    # Project along the dominant axis, 64 units per texture repeat.
    axis = max(range(3), key=lambda k: abs(normal[k]))
    u_axis, v_axis = [(1, 2), (0, 2), (0, 1)][axis]
    return (point[u_axis] / UV_UNITS, -point[v_axis] / UV_UNITS)


# ------------------------------------------------------------------------------------------------
# Areas and output
# ------------------------------------------------------------------------------------------------

AREA_PLANE = ((1.0, 0.0, 0.0), 0.0)


class Surface:
    def __init__(self, material):
        self.material = material
        self.vertices = []
        self.lookup = {}
        self.indices = []

    def vertex(self, position, uv, normal):
        record = tuple(round(c, 4) + 0.0 for c in position + uv + normal)
        if record not in self.lookup:
            self.lookup[record] = len(self.vertices)
            self.vertices.append(record)
        return self.lookup[record]

    def triangle(self, a, b, c):
        if len({a, b, c}) == 3:
            self.indices += [a, b, c]


def area_of(point):
    return 1 if side(AREA_PLANE, point) > 0 else 0


def build_areas():
    shell, objects = solids()
    faces = shell_faces() + object_faces(shell, objects)
    split_faces = []
    for material, polygon, normal in faces:
        front, back = split(polygon, AREA_PLANE)
        for area, piece in ((1, front), (0, back)):
            if piece is not None and len(piece) >= 3:
                split_faces.append((area, material, piece, normal))
    fixed = remove_t_junctions([(m, p, n) for _, m, p, n in split_faces])
    areas = [dict(), dict()]

    def surface(area, material):
        if material not in areas[area]:
            areas[area][material] = Surface(material)
        return areas[area][material]

    for (area, _, _, _), (material, polygon, normal) in zip(split_faces, fixed):
        corners, triangles = triangulate(polygon, normal)
        target = surface(area, material)
        indices = [target.vertex(p, planar_uv(p, normal), normal) for p in corners]
        for a, b, c in triangles:
            target.triangle(indices[a], indices[b], indices[c])
    for solid in objects:
        if solid.mesh is None:
            continue
        vertices, triangles = solid.mesh
        areas_touched = {area_of(p) for p, _ in vertices}
        assert len(areas_touched) == 1, "a smooth solid straddles the area plane"
        target = surface(areas_touched.pop(), solid.material)
        # Planar UVs by the vertex normal's dominant axis would tear a smooth mesh; it maps by the
        # mesh's own cylindrical angle and height instead.
        indices = []
        for position, normal in vertices:
            angle = math.atan2(normal[1], normal[0])
            uv = (angle / (2.0 * math.pi) * 4.0, -position[2] / UV_UNITS)
            indices.append(target.vertex(position, uv, normal))
        for a, b, c in triangles:
            target.triangle(indices[a], indices[b], indices[c])
    return shell, objects, [sorted(area.values(), key=lambda s: s.material) for area in areas]


def number(value):
    text = f"{value:.4f}".rstrip("0").rstrip(".")
    return "0" if text in ("-0", "") else text


def portal_winding():
    # idWinding::GetPlane takes normal = (p1 - c) x (p0 - c); the portal listed for area 0 must
    # face area 0 (x < 0), where the view flood requires a nonnegative distance.
    points = [(0.0, -DOOR_Y, 0.0), (0.0, DOOR_Y, 0.0), (0.0, DOOR_Y, DOOR_Z), (0.0, -DOOR_Y, DOOR_Z)]
    center = scale(add(add(points[0], points[1]), add(points[2], points[3])), 0.25)
    normal = cross(sub(points[1], center), sub(points[0], center))
    if normal[0] > 0:
        points.reverse()
    return points


def nodes():
    # Node children: > 0 a node index, 0 solid, < 0 area -1 - child. children[0] is the front.
    records = [((1, 0, 0, 0), 1, 6)]

    def room(first, area, x_plane):
        bounds = [x_plane, (0, 1, 0, -ROOM_Y), (0, -1, 0, -ROOM_Y), (0, 0, 1, -ROOM_Z), (0, 0, -1, 0)]
        for index, plane in enumerate(bounds):
            back = first + index + 1 if index + 1 < len(bounds) else -1 - area
            records.append((plane, 0, back))

    room(1, 1, (1, 0, 0, -ROOM_X))
    room(6, 0, (-1, 0, 0, -ROOM_X))
    return records


def proc_text(areas):
    lines = [BANNER, "mapProcFile003\n\n"]
    for index, surfaces in enumerate(areas):
        lines.append(f'model {{ "_area{index}" {len(surfaces)}\n')
        for surface in surfaces:
            lines.append(f'{{ "{surface.material}" {len(surface.vertices)} {len(surface.indices)}\n')
            for record in surface.vertices:
                lines.append("( " + " ".join(number(v) for v in record) + " )\n")
            for start in range(0, len(surface.indices), 18):
                lines.append(" ".join(str(i) for i in surface.indices[start:start + 18]) + "\n")
            lines.append("}\n")
        lines.append("}\n\n")
    winding = portal_winding()
    lines.append("interAreaPortals { 2 1\n")
    lines.append(f"{len(winding)} 0 1 " + " ".join("( " + " ".join(number(c) for c in p) + " )" for p in winding) + "\n}\n\n")
    records = nodes()
    lines.append(f"nodes {{ {len(records)}\n")
    for plane, front, back in records:
        lines.append("( " + " ".join(number(float(c)) for c in plane) + f" ) {front} {back}\n")
    lines.append("}\n")
    return "".join(lines)


def map_text(shell, objects):
    lines = [BANNER, "Version 2\n{\n", '"classname" "worldspawn"\n"name" "worldMap"\n']
    for solid in shell + objects:
        lines.append("{\nbrushDef3\n{\n")
        for normal, d in solid.planes:
            plane = " ".join(number(c) for c in normal + (d,))
            lines.append(f'( {plane} ) ( ( 0.015625 0 0 ) ( 0 0.015625 0 ) ) "{solid.material}" 0 0 0\n')
        lines.append("}\n}\n")
    lines.append("}\n")
    return "".join(lines)


def material_text():
    lines = [BANNER, "// Graybox materials: collision only until the world cook compiles material passes (gate R2);\n",
             "// the cook job gives each one its base colour.\n"]
    for name in MATERIALS:
        lines.append(f"{name} {{ solid collision }}\n")
    return "".join(lines)


def job_text():
    job = {
        "version": 1,
        "map": "maps/graybox",
        "topology": "hybrid",
        "shading": {"light": list(LIGHT_DIRECTION), "ambient": AMBIENT},
        "default_color": [0.5, 0.5, 0.5],
        "materials": {name: list(color) for name, color in MATERIALS.items()},
    }
    return json.dumps(job, indent=2) + "\n"


def outputs():
    shell, objects, areas = build_areas()
    return {
        FIXTURE_ROOT / "maps/graybox.proc": proc_text(areas).encode("ascii"),
        FIXTURE_ROOT / "maps/graybox.map": map_text(shell, objects).encode("ascii"),
        FIXTURE_ROOT / "materials/graybox.mtr": material_text().encode("ascii"),
        FIXTURE_ROOT / "graybox_world.json": job_text().encode("ascii"),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="check tracked outputs without modifying them")
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help=argparse.SUPPRESS)
    args = parser.parse_args()
    changed = []
    for relative, data in outputs().items():
        path = args.root / relative
        if path.exists() and path.read_bytes() == data:
            continue
        changed.append(str(relative))
        if not args.check:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    if args.check and changed:
        parser.exit(1, "graybox fixture outputs are stale: " + ", ".join(changed) + "\n")
    print("Graybox fixture outputs verified." if args.check else "Graybox fixture outputs generated.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
