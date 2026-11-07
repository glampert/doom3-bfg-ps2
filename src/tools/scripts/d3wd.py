#!/usr/bin/env python3
# ================================================================================================
# File: d3wd.py
# Brief: Read the records of a cooked D3WD world (src/tools/assets/world.h) that scripts need: the
# areas' batch ranges and each batch's header and VIF packet. The C++ readers validate the file;
# this one only checks the magic and the section table.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from collections import namedtuple
from pathlib import Path
import struct

MAGIC = 0x44573344
HEADER_BYTES = 32
SECTION_COUNT = 11
# Section order (world.h's D3wdSection).
AREAS, BATCHES, PACKETS = 2, 7, 10
FLAG_INDEXED_STRIPS = 1 << 4

Area = namedtuple("Area", "first_batch batch_count")
Batch = namedtuple("Batch", "flags vertex_count packet_offset packet_qwords index_count")


class World:
    def __init__(self, data: bytes):
        magic, _, header_bytes, sections = struct.unpack_from("<IHHH", data, 0)
        if magic != MAGIC or header_bytes != HEADER_BYTES or sections != SECTION_COUNT:
            raise ValueError("not a D3WD file")
        directory = [struct.unpack_from("<IIII", data, HEADER_BYTES + 16 * index) for index in range(SECTION_COUNT)]
        self.areas = [Area(*struct.unpack_from("<II", data, directory[AREAS][0] + 64 * index + 48)) for index in range(directory[AREAS][2])]
        self.batches = []
        for index in range(directory[BATCHES][2]):
            at = directory[BATCHES][0] + 64 * index
            flags, = struct.unpack_from("<I", data, at + 12)
            vertices, = struct.unpack_from("<I", data, at + 28)
            offset, qwords = struct.unpack_from("<II", data, at + 48)
            indices, = struct.unpack_from("<H", data, at + 60)
            self.batches.append(Batch(flags, vertices, offset, qwords, indices))
        self.packets = data[directory[PACKETS][0]:directory[PACKETS][0] + directory[PACKETS][1]]

    def packet(self, batch: Batch) -> bytes:
        return self.packets[batch.packet_offset:batch.packet_offset + 16 * batch.packet_qwords]

    def area_batches(self, areas) -> int:
        return sum(self.areas[area].batch_count for area in areas)

    def indexed(self) -> bool:
        return all(batch.flags & FLAG_INDEXED_STRIPS for batch in self.batches)


def load(path: Path) -> World:
    return World(Path(path).read_bytes())
