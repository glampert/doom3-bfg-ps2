#!/usr/bin/env python3
# ================================================================================================
# File: ui_gstx.py
# Brief: Read and validate GSTX v1 converted textures for the cook, the catalog and capture oracles,
# with the same canonical-layout checks and GS bilinear sampling as the host tools.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""GSTX v1 is written by src/tools/asset_reexport; src/tools/assets/gstx.h documents the layout.

Reading is strict: every count, checksum, padding byte and placement rectangle is checked, so
a file accepted here is exactly what the target admits. Sampling reproduces the GS's straight
bilinear filter with the placement's region clamp, for expected-pixel oracles. build_texture
encodes synthetic fixtures; it is not the production writer.
"""

import math
import struct

MAGIC = 0x58545347
VERSION = 1
HEADER_BYTES = 48
PAGE_RECORD_BYTES = 32
CLUT_RECORD_BYTES = 16
PLACEMENT_RECORD_BYTES = 32
MAX_PAGES = 16
MAX_CLUTS = 128
MAX_PLACEMENTS = 1024
MAX_PAGE_EXTENT = 512
MAX_LOGICAL_EXTENT = 16384
PSMT8 = 0x13
PSMT4 = 0x14
_HEADER = struct.Struct("<IHHHHHHHHIII16x")
_PAGE = struct.Struct("<HHBBHIIII8x")
_CLUT = struct.Struct("<HHIII")
_PLACEMENT = struct.Struct("<14HI")


def fnv1a32(data, seed=2166136261):
    value = seed
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def _align16(value):
    return (value + 15) & ~15


def clut_swizzle(index):
    return (index & ~0x18) | ((index & 0x08) << 1) | ((index & 0x10) >> 1)


def rle_decode(data, size):
    output = bytearray()
    cursor = 0
    while cursor < len(data):
        control = data[cursor]
        cursor += 1
        if control < 128:
            count = control + 1
            if count > len(data) - cursor or len(output) + count > size:
                raise ValueError("GSTX page encoding invalid")
            output += data[cursor:cursor + count]
            cursor += count
        else:
            count = control - 125
            if cursor >= len(data) or len(output) + count > size:
                raise ValueError("GSTX page encoding invalid")
            output += bytes([data[cursor]]) * count
            cursor += 1
    if len(output) != size:
        raise ValueError("GSTX page encoding invalid")
    return bytes(output)


def _index_bytes(psm, width, height):
    return width * height if psm == PSMT8 else (width * height + 1) // 2


def _inside(inner, outer):
    return (inner[0] >= outer[0] and inner[1] >= outer[1] and inner[0] + inner[2] <= outer[0] + outer[2] and
            inner[1] + inner[3] <= outer[1] + outer[3])


def _overlap(first, second):
    return (first[0] < second[0] + second[2] and second[0] < first[0] + first[2] and
            first[1] < second[1] + second[3] and second[1] < first[1] + first[3])


class Texture:
    """A validated GSTX file: pages (decoded GS index data), CLUTs (GS-order RGBA) and placements."""

    def __init__(self, data: bytes):
        if len(data) < HEADER_BYTES:
            raise ValueError("GSTX header invalid")
        (magic, version, header_bytes, self.width, self.height, page_count, clut_count, placement_count, flags,
         table_bytes, file_bytes, table_checksum) = _HEADER.unpack_from(data)
        if (magic, version, header_bytes, flags) != (MAGIC, VERSION, HEADER_BYTES, 0) or any(data[32:48]):
            raise ValueError("GSTX header invalid")
        if (page_count > MAX_PAGES or clut_count > MAX_CLUTS or placement_count > MAX_PLACEMENTS or
                table_bytes != HEADER_BYTES + page_count * PAGE_RECORD_BYTES + clut_count * CLUT_RECORD_BYTES +
                placement_count * PLACEMENT_RECORD_BYTES or file_bytes != len(data) or table_bytes > file_bytes):
            raise ValueError("GSTX counts or sizes invalid")
        if fnv1a32(data[32:table_bytes], fnv1a32(bytes(4), fnv1a32(data[:28]))) != table_checksum:
            raise ValueError("GSTX table checksum mismatch")
        self.table_bytes = table_bytes
        expected, previous_end = _align16(table_bytes), table_bytes

        def block(offset, size):
            nonlocal expected, previous_end
            if offset != expected or size > file_bytes - offset or any(data[previous_end:offset]):
                raise ValueError("GSTX data layout is not canonical")
            previous_end = offset + size
            expected = _align16(previous_end)
            return data[offset:offset + size]

        clut_records = HEADER_BYTES + page_count * PAGE_RECORD_BYTES
        self.cluts = []
        for index in range(clut_count):
            entries, reserved, offset, size, checksum = _CLUT.unpack_from(data, clut_records + index * CLUT_RECORD_BYTES)
            if entries not in (16, 256) or reserved or size != entries * 4:
                raise ValueError("GSTX CLUT record invalid")
            raw = block(offset, size)
            if fnv1a32(raw) != checksum:
                raise ValueError("GSTX CLUT record invalid")
            self.cluts.append({"format": PSMT8 if entries == 256 else PSMT4,
                               "texels": [tuple(raw[i * 4:i * 4 + 4]) for i in range(entries)]})
        self.pages = []
        for index in range(page_count):
            (width, height, psm, encoding, reserved, offset, size, decoded_bytes,
             checksum) = _PAGE.unpack_from(data, HEADER_BYTES + index * PAGE_RECORD_BYTES)
            if (psm not in (PSMT8, PSMT4) or encoding > 1 or reserved or not 1 <= width <= MAX_PAGE_EXTENT or
                    not 1 <= height <= MAX_PAGE_EXTENT or decoded_bytes != _index_bytes(psm, width, height) or
                    (encoding == 0 and size != decoded_bytes)):
                raise ValueError("GSTX page record invalid")
            raw = block(offset, size)
            decoded = rle_decode(raw, decoded_bytes) if encoding else bytes(raw)
            if fnv1a32(decoded) != checksum:
                raise ValueError("GSTX page checksum mismatch")
            self.pages.append({"width": width, "height": height, "format": psm, "data": decoded,
                               "encoded_bytes": size, "encoding": encoding})
        if _align16(previous_end) != file_bytes or any(data[previous_end:]):
            raise ValueError("GSTX file has trailing data")
        placement_records = clut_records + clut_count * CLUT_RECORD_BYTES
        self.placements = []
        for index in range(placement_count):
            values = _PLACEMENT.unpack_from(data, placement_records + index * PLACEMENT_RECORD_BYTES)
            if values[14]:
                raise ValueError("GSTX placement record invalid")
            self.placements.append({"match": values[0:4], "source": values[4:8], "dest": values[8:12],
                                    "page": values[12], "clut": values[13]})
        self._validate()
        self._palettes = [self._palette(clut) for clut in self.cluts]

    def _validate(self):
        if not (1 <= self.width <= MAX_LOGICAL_EXTENT and 1 <= self.height <= MAX_LOGICAL_EXTENT):
            raise ValueError("GSTX logical dimensions out of range")
        if not self.pages or not self.cluts or not self.placements:
            raise ValueError("GSTX page, CLUT or placement count out of range")
        for page in self.pages:
            page_height = 64 if page["format"] == PSMT8 else 128
            if page["width"] % 128 or page["height"] % page_height:
                raise ValueError("GSTX page dimensions or data size invalid")
        for clut in self.cluts:
            if len(clut["texels"]) != (256 if clut["format"] == PSMT8 else 16):
                raise ValueError("GSTX CLUT entry count invalid")
        logical = (0, 0, self.width, self.height)
        for index, placement in enumerate(self.placements):
            if placement["page"] >= len(self.pages) or placement["clut"] >= len(self.cluts):
                raise ValueError("GSTX placement references a missing page or CLUT")
            page = self.pages[placement["page"]]
            if self.cluts[placement["clut"]]["format"] != page["format"]:
                raise ValueError("GSTX placement CLUT does not match its page format")
            match, source, dest = placement["match"], placement["source"], placement["dest"]
            if (match[2] < 1 or match[3] < 1 or not _inside(match, source) or not _inside(source, logical) or
                    dest[2] < 1 or dest[3] < 1 or dest[2] > source[2] or dest[3] > source[3] or
                    not _inside(dest, (0, 0, page["width"], page["height"]))):
                raise ValueError("GSTX placement rectangles invalid")
            for other in self.placements[:index]:
                if _overlap(match, other["match"]):
                    raise ValueError("GSTX placement match rectangles overlap")

    @staticmethod
    def _palette(clut):
        texels = clut["texels"]
        if clut["format"] == PSMT8:
            return [texels[clut_swizzle(index)] for index in range(256)]
        return list(texels)

    def descriptor(self, data: bytes) -> bytes:
        """The header and tables a catalog embeds to admit this exact file."""
        return bytes(data[:self.table_bytes])

    def placement_at(self, x: float, y: float):
        for placement in self.placements:
            match = placement["match"]
            if match[0] <= x < match[0] + match[2] and match[1] <= y < match[1] + match[3]:
                return placement
        return None

    def _index(self, page, x, y):
        offset = y * page["width"] + x
        if page["format"] == PSMT8:
            return page["data"][offset]
        byte = page["data"][offset // 2]
        return byte >> 4 if offset & 1 else byte & 0x0F

    def sample(self, x: float, y: float):
        """Straight RGBA at a logical texel position (centers at i + 0.5), or None outside placements."""
        placement = self.placement_at(x, y)
        if placement is None:
            return None
        page = self.pages[placement["page"]]
        palette = self._palettes[placement["clut"]]
        source, dest = placement["source"], placement["dest"]
        u = dest[0] + (x - source[0]) * dest[2] / source[2] - 0.5
        v = dest[1] + (y - source[1]) * dest[3] / source[3] - 0.5
        x0, y0 = math.floor(u), math.floor(v)
        fx, fy = u - x0, v - y0
        sums = [0.0, 0.0, 0.0, 0.0]
        for corner in range(4):
            tx = min(max(x0 + (corner & 1), dest[0]), dest[0] + dest[2] - 1)
            ty = min(max(y0 + (corner >> 1), dest[1]), dest[1] + dest[3] - 1)
            color = palette[self._index(page, tx, ty)]
            weight = (fx if corner & 1 else 1.0 - fx) * (fy if corner >> 1 else 1.0 - fy)
            for channel in range(4):
                sums[channel] += weight * color[channel]
        return tuple(int(math.floor(value + 0.5)) for value in sums)

    def alpha(self, x: int, y: int) -> int:
        """Alpha of logical texel (x, y) as the target samples it; zero outside every placement."""
        color = self.sample(x + 0.5, y + 0.5)
        return 0 if color is None else color[3]

    def alpha_bounds(self, x0: float, y0: float, x1: float, y1: float):
        """Lowest and highest alpha bilinear sampling can return anywhere in [x0, x1] x [y0, y1].

        The rectangle samples through the placement holding its center, and every texel with a
        nonzero filter weight somewhere inside it counts, so the bound holds wherever the target's
        raster lands inside it. Returns None outside every placement.
        """
        placement = self.placement_at((x0 + x1) / 2, (y0 + y1) / 2)
        if placement is None:
            return None
        page = self.pages[placement["page"]]
        palette = self._palettes[placement["clut"]]
        source, dest = placement["source"], placement["dest"]

        def texels(low, high, axis):
            start, size = dest[axis], dest[axis + 2]
            first = math.floor(start + (low - source[axis]) * size / source[axis + 2] - 0.5)
            last = math.ceil(start + (high - source[axis]) * size / source[axis + 2] - 0.5)
            return range(min(max(first, start), start + size - 1), min(max(last, start), start + size - 1) + 1)

        alphas = [palette[self._index(page, x, y)][3] for y in texels(y0, y1, 1) for x in texels(x0, x1, 0)]
        return min(alphas), max(alphas)


def build_texture(width: int, height: int, pages: list, cluts: list, placements: list) -> bytes:
    """Encode decoded parts as a canonical GSTX file, storing every page raw.

    Pages are dicts with width, height, format (PSMT8/PSMT4) and GS-layout index data; CLUTs are
    lists of 16 or 256 RGBA tuples in GS order; placements are dicts with match, source and dest
    rectangles, page and clut. The converter also chooses RLE; both encodings are canonical.
    The result must pass Texture, so a fixture can never be one the reader would refuse.
    """
    table_bytes = (HEADER_BYTES + len(pages) * PAGE_RECORD_BYTES + len(cluts) * CLUT_RECORD_BYTES +
                   len(placements) * PLACEMENT_RECORD_BYTES)
    offset = _align16(table_bytes)
    blocks = []
    clut_records = bytearray()
    for texels in cluts:
        raw = b"".join(bytes(texel) for texel in texels)
        clut_records += _CLUT.pack(len(texels), 0, offset, len(raw), fnv1a32(raw))
        blocks.append((offset, raw))
        offset = _align16(offset + len(raw))
    page_records = bytearray()
    for page in pages:
        raw = bytes(page["data"])
        page_records += _PAGE.pack(page["width"], page["height"], page["format"], 0, 0, offset, len(raw), len(raw),
                                   fnv1a32(raw))
        blocks.append((offset, raw))
        offset = _align16(offset + len(raw))
    placement_records = b"".join(_PLACEMENT.pack(*placement["match"], *placement["source"], *placement["dest"],
                                                 placement["page"], placement["clut"], 0) for placement in placements)
    data = bytearray(offset)
    data[:table_bytes] = (_HEADER.pack(MAGIC, VERSION, HEADER_BYTES, width, height, len(pages), len(cluts),
                                       len(placements), 0, table_bytes, offset, 0) +
                          page_records + clut_records + placement_records)
    struct.pack_into("<I", data, 28, fnv1a32(data[32:table_bytes], fnv1a32(bytes(4), fnv1a32(data[:28]))))
    for block_offset, raw in blocks:
        data[block_offset:block_offset + len(raw)] = raw
    Texture(bytes(data))
    return bytes(data)


def read_texture(path) -> Texture:
    with open(path, "rb") as stream:
        return Texture(stream.read())


def runtime_stats(texture: Texture) -> dict:
    """The counters ps2::ui::PalettedImage reports for an open texture (paletted_image.cpp).

    Pixel bytes are the CLUT texture (16x16 tiles, four per 64-texel row) plus every decoded page;
    metadata is the tables plus the placement lookup grid (at most 64 cells per axis, 32 texels
    minimum, one 32-bit start per cell and a 16-bit index per covered cell).
    """
    rows = (len(texture.cluts) + 3) // 4 * 16
    pixels = 64 * rows * 4 + sum(len(page["data"]) for page in texture.pages)
    extent = max(texture.width, texture.height)
    cell = max(32, (extent + 63) // 64)
    columns, grid_rows = (texture.width + cell - 1) // cell, (texture.height + cell - 1) // cell
    items = 0
    for row in range(grid_rows):
        for column in range(columns):
            x, y = column * cell, row * cell
            for placement in texture.placements:
                mx, my, mw, mh = placement["match"]
                if mx < x + cell and x < mx + mw and my < y + cell and y < my + mh:
                    items += 1
    return {"pages": len(texture.pages), "cluts": len(texture.cluts), "pixel_bytes": pixels,
            "metadata_bytes": texture.table_bytes + (columns * grid_rows + 1) * 4 + items * 2}
