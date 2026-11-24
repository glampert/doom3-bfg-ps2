#!/usr/bin/env python3
# ================================================================================================
# File: d3wd.py
# Brief: Read the records of a cooked D3WD world (src/tools/assets/world.h) that scripts need: the
# areas' batch ranges, each batch's header and VIF packet, and the vertices and triangles a packet
# draws. The C++ readers validate the file; this one only checks the magic and the section table.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from collections import namedtuple
from pathlib import Path
import struct

MAGIC = 0x44573344
HEADER_BYTES = 32
SECTION_COUNT = 12
# Section order (world.h's D3wdSection).
STRINGS, MATERIALS, AREAS, BATCHES, PACKETS, ENTITIES = 0, 1, 2, 7, 10, 11
FLAG_INDEXED_STRIPS = 1 << 4
NO_LIGHTMAP = 0xFFFF
VERTEX_QWORDS = 4

Area = namedtuple("Area", "first_batch batch_count")
Batch = namedtuple("Batch", "flags vertex_count packet_offset packet_qwords index_count material lightmap_page")
# A decoded vertex: world position, texture UV, lightmap UV (0..1 of its page), RGBA bytes.
Vertex = namedtuple("Vertex", "position uv lightmap color")


class World:
    def __init__(self, data: bytes):
        magic, _, header_bytes, sections = struct.unpack_from("<IHHH", data, 0)
        if magic != MAGIC or header_bytes != HEADER_BYTES or sections != SECTION_COUNT:
            raise ValueError("not a D3WD file")
        directory = [struct.unpack_from("<IIII", data, HEADER_BYTES + 16 * index) for index in range(SECTION_COUNT)]
        self.lightmap_pages, = struct.unpack_from("<I", data, 24)
        strings = data[directory[STRINGS][0]:directory[STRINGS][0] + directory[STRINGS][1]]
        self.materials = []
        for index in range(directory[MATERIALS][2]):
            offset, length = struct.unpack_from("<II", data, directory[MATERIALS][0] + 8 * index)
            self.materials.append(strings[offset:offset + length].decode("latin1"))
        self.areas = [Area(*struct.unpack_from("<II", data, directory[AREAS][0] + 64 * index + 48)) for index in range(directory[AREAS][2])]
        self.batches = []
        for index in range(directory[BATCHES][2]):
            at = directory[BATCHES][0] + 64 * index
            flags, = struct.unpack_from("<I", data, at + 12)
            vertices, = struct.unpack_from("<I", data, at + 28)
            offset, qwords = struct.unpack_from("<II", data, at + 48)
            material, page, indices = struct.unpack_from("<HHH", data, at + 56)
            self.batches.append(Batch(flags, vertices, offset, qwords, indices, material, page))
        self.packets = data[directory[PACKETS][0]:directory[PACKETS][0] + directory[PACKETS][1]]

    def packet(self, batch: Batch) -> bytes:
        return self.packets[batch.packet_offset:batch.packet_offset + 16 * batch.packet_qwords]

    def vertices(self, batch: Batch) -> list:
        """The batch's vertices as its packet unpacks them (world.h's layout), in stream order."""
        packet = self.packet(batch)
        words = struct.unpack_from(f"<{len(packet) // 4}I", packet)
        offset = struct.unpack_from("<3f", packet, 8)
        scale, uv_scale, uv_s, uv_t = struct.unpack_from("<4f", packet, 24)
        at = 10
        if batch.flags & FLAG_INDEXED_STRIPS:
            # Four u16 entries a qword: two words each.
            at += 1 + 2 * int(words[5])
        at += 2
        count = batch.vertex_count
        positions = struct.unpack_from(f"<{count * 4}h", packet, at * 4)
        at += 1 + count * 2
        uvs = struct.unpack_from(f"<{count * 4}h", packet, at * 4)
        at += 1 + count * 2
        colors = packet[at * 4:at * 4 + count * 4]
        out = []
        for index in range(count):
            p = positions[index * 4:index * 4 + 3]
            q = uvs[index * 4:index * 4 + 4]
            out.append(Vertex(tuple(p[axis] * scale + offset[axis] for axis in range(3)), (q[0] * uv_scale + uv_s, q[1] * uv_scale + uv_t),
                              (q[2] / 4096.0, q[3] / 4096.0), tuple(colors[index * 4:index * 4 + 4])))
        return out

    def triangles(self, batch: Batch) -> list:
        """The batch's triangles as vertex index triples in id's winding (world.h's strip rule)."""
        packet = self.packet(batch)
        if not batch.flags & FLAG_INDEXED_STRIPS:
            # Plain strips: the ADC flag rides in position w.
            count = batch.vertex_count
            # STCYCL, the header unpack and its 8 words, STCYCL 4,1 and the position unpack: word 12.
            positions = struct.unpack_from(f"<{count * 4}h", packet, 12 * 4)
            entries = [index | (0x80 if positions[index * 4 + 3] == -32768 else 0) for index in range(count)]
        else:
            qwords, = struct.unpack_from("<I", packet, 20)
            base = 2 + qwords
            raw = struct.unpack_from(f"<{batch.index_count}H", packet, 11 * 4)
            entries = [((entry & 0x7FFF) - base) // VERTEX_QWORDS | (0x80 if entry & 0x8000 else 0) for entry in raw]
        # world.cpp's WalkStrips: ADC opens a strip (its first two entries); triangle k of a strip
        # is (k, k+1, k+2) for even k, (k+1, k, k+2) for odd k.
        triangles = []
        window = [0, 0]
        run = 0
        for entry in entries:
            vertex = entry & 0x7F
            if entry & 0x80:
                run = 2 if run == 1 else 1
            elif run >= 2:
                triangles.append((window[0], window[1], vertex) if run % 2 == 0 else (window[1], window[0], vertex))
                run += 1
            window = [window[1], vertex]
        return triangles

    def area_batches(self, areas) -> int:
        return sum(self.areas[area].batch_count for area in areas)

    def indexed(self) -> bool:
        return all(batch.flags & FLAG_INDEXED_STRIPS for batch in self.batches)


def load(path: Path) -> World:
    return World(Path(path).read_bytes())
