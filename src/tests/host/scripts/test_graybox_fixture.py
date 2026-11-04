#!/usr/bin/env python3
# ================================================================================================
# File: test_graybox_fixture.py
# Brief: Check the graybox map fixture: generated files current, id's triangle winding, areas on
#        their side of the portal, the portal facing the way the view flood needs, the area BSP and
#        no T-junctions.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import importlib.util
import json
import math
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[4]
FIXTURE = ROOT / "src/tests/data/graybox_map"

SPEC = importlib.util.spec_from_file_location("graybox_fixture", ROOT / "src/tests/host/scripts/graybox_fixture.py")
builder = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(builder)

SMOOTH_MATERIALS = {"textures/graybox/white", "textures/graybox/cyan"}


def parse_proc(text):
    text = re.sub(r"//[^\n]*", "", text)
    tokens = re.findall(r'"[^"]*"|[{}()]|[^\s{}()]+', text)
    position = 0

    def take():
        nonlocal position
        position += 1
        return tokens[position - 1]

    def expect(value):
        token = take()
        if token != value:
            raise ValueError(f"expected {value}, found {token}")

    def matrix(count):
        expect("(")
        values = [float(take()) for _ in range(count)]
        expect(")")
        return values

    expect("mapProcFile003")
    areas = {}
    portals = []
    nodes = []
    while position < len(tokens):
        token = take()
        if token == "model":
            expect("{")
            name = take().strip('"')
            surfaces = []
            for _ in range(int(take())):
                expect("{")
                material = take().strip('"')
                vertex_count = int(take())
                index_count = int(take())
                vertices = [matrix(8) for _ in range(vertex_count)]
                indices = [int(take()) for _ in range(index_count)]
                expect("}")
                surfaces.append((material, vertices, indices))
            expect("}")
            areas[int(name[len("_area"):])] = surfaces
        elif token == "interAreaPortals":
            expect("{")
            area_count = int(take())
            for _ in range(int(take())):
                point_count = int(take())
                a, b = int(take()), int(take())
                portals.append((a, b, [matrix(3) for _ in range(point_count)]))
            expect("}")
        elif token == "nodes":
            expect("{")
            for _ in range(int(take())):
                plane = matrix(4)
                nodes.append((plane, int(take()), int(take())))
            expect("}")
        else:
            raise ValueError(f"unexpected {token}")
    return area_count, areas, portals, nodes


def sub(a, b):
    return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def point_in_area(nodes, point):
    # idRenderWorldLocal::PointInArea.
    node = 0
    while True:
        plane, front, back = nodes[node]
        child = front if dot(plane[:3], point) + plane[3] > 0 else back
        if child == 0:
            return -1
        if child < 0:
            return -1 - child
        node = child


class GrayboxFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.outputs = builder.outputs()
        cls.area_count, cls.areas, cls.portals, cls.nodes = parse_proc((FIXTURE / "maps/graybox.proc").read_text())

    def test_outputs_are_current(self):
        for relative, data in self.outputs.items():
            self.assertEqual((ROOT / relative).read_bytes(), data, f"{relative} is stale: run graybox_fixture.py")

    def test_triangles_face_their_normals(self):
        for area, surfaces in self.areas.items():
            for material, vertices, indices in surfaces:
                self.assertEqual(len(indices) % 3, 0)
                for index in range(0, len(indices), 3):
                    a, b, c = (vertices[indices[index + corner]] for corner in range(3))
                    # idPlane::FromPoints: (a - b) x (c - b).
                    normal = cross(sub(a[:3], b[:3]), sub(c[:3], b[:3]))
                    length = math.sqrt(dot(normal, normal))
                    self.assertGreater(length, 1e-3, f"degenerate triangle in {material}")
                    facing = dot(normal, a[5:8]) / length
                    self.assertGreater(facing, 0.5 if material in SMOOTH_MATERIALS else 0.999, f"{material} in area {area} is wound backwards")

    def test_areas_stay_on_their_side(self):
        self.assertEqual(self.area_count, 2)
        for area, surfaces in self.areas.items():
            for material, vertices, indices in surfaces:
                for vertex in vertices:
                    self.assertTrue(vertex[0] <= 1e-3 if area == 0 else vertex[0] >= -1e-3, f"{material} vertex in the wrong area")
                # Centroids may lie on the room's bounding planes, which classify inside.
                for index in range(0, len(indices), 3):
                    corners = [vertices[indices[index + corner]] for corner in range(3)]
                    centroid = [sum(c[axis] for c in corners) / 3 for axis in range(3)]
                    self.assertEqual(point_in_area(self.nodes, centroid), area, f"{material} triangle at {centroid}")

    def test_portal_faces_its_first_area(self):
        self.assertEqual(len(self.portals), 1)
        first, second, points = self.portals[0]
        self.assertEqual((first, second), (0, 1))
        center = [sum(p[axis] for p in points) / len(points) for axis in range(3)]
        # idWinding::GetPlane, then the view flood's distance test from inside area 0.
        normal = cross(sub(points[1], center), sub(points[0], center))
        self.assertLess(normal[0], 0)
        self.assertEqual(sorted(tuple(p) for p in points), [(0, -64, 0), (0, -64, 128), (0, 64, 0), (0, 64, 128)])

    def test_nodes_bound_the_rooms(self):
        self.assertEqual(point_in_area(self.nodes, [-256, 0, 64]), 0)
        self.assertEqual(point_in_area(self.nodes, [256, 0, 64]), 1)
        for outside in ([600, 0, 64], [-600, 0, 64], [100, 500, 10], [100, -500, 10], [-100, 0, -5], [100, 0, 300]):
            self.assertEqual(point_in_area(self.nodes, outside), -1, outside)

    def test_no_t_junctions(self):
        # A crack needs a vertex inside the edge of a triangle no other triangle shares that edge
        # with, and an edge of its own along that line; a corner resting on a watertight surface
        # (a box on the floor) is not one.
        points = set()
        edges = []
        neighbours = {}
        uses = {}
        for surfaces in self.areas.values():
            for _, vertices, indices in surfaces:
                for vertex in vertices:
                    points.add(tuple(round(c, 3) for c in vertex[:3]))
                for index in range(0, len(indices), 3):
                    for corner in range(3):
                        a = vertices[indices[index + corner]][:3]
                        b = vertices[indices[index + (corner + 1) % 3]][:3]
                        edges.append((a, b))
                        key = tuple(sorted((tuple(round(c, 3) for c in a), tuple(round(c, 3) for c in b))))
                        uses[key] = uses.get(key, 0) + 1
                        neighbours.setdefault(tuple(round(c, 3) for c in a), []).append(b)
                        neighbours.setdefault(tuple(round(c, 3) for c in b), []).append(a)
        cells = {}
        for point in points:
            cells.setdefault(tuple(int(math.floor(c / 64)) for c in point), []).append(point)
        for a, b in edges:
            if uses[tuple(sorted((tuple(round(c, 3) for c in a), tuple(round(c, 3) for c in b))))] > 1:
                continue
            lo = [math.floor(min(a[k], b[k]) / 64) for k in range(3)]
            hi = [math.floor(max(a[k], b[k]) / 64) for k in range(3)]
            edge = sub(b, a)
            length2 = dot(edge, edge)
            for x in range(lo[0], hi[0] + 1):
                for y in range(lo[1], hi[1] + 1):
                    for z in range(lo[2], hi[2] + 1):
                        for point in cells.get((x, y, z), []):
                            t = dot(sub(point, a), edge) / length2
                            if t <= 1e-3 or t >= 1 - 1e-3:
                                continue
                            offset = sub(point, [a[k] + edge[k] * t for k in range(3)])
                            if dot(offset, offset) > 1e-4:
                                continue
                            for other in neighbours[point]:
                                along = cross(sub(other, a), edge)
                                self.assertGreater(dot(along, along) / length2, 1e-4, f"T-junction at {point} on edge {a} {b}")

    def test_map_materials_and_job_agree(self):
        used = {material for surfaces in self.areas.values() for material, _, _ in surfaces}
        declared = set(re.findall(r"^(textures/graybox/\w+) \{", (FIXTURE / "materials/graybox.mtr").read_text(), re.M))
        job = json.loads((FIXTURE / "graybox_world.json").read_text())
        self.assertLessEqual(used, declared)
        self.assertLessEqual(used, set(job["materials"]))
        self.assertEqual(job["version"], 1)
        map_text = (FIXTURE / "maps/graybox.map").read_text()
        self.assertEqual(map_text.count("brushDef3"), 21)
        self.assertLessEqual(set(re.findall(r'"(textures/graybox/\w+)"', map_text)), declared)


if __name__ == "__main__":
    unittest.main()
