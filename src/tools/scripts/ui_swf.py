#!/usr/bin/env python3
# ================================================================================================
# File: ui_swf.py
# Brief: Bounded, read-only structural inventory of the engine's native BSWF16 container.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""Inspect native BSWF16 without executing AVM1 or exporting text/script payloads.

The byte layout follows SWF_Load.cpp and SWF_Sprites.cpp. Container numbers are
big endian, idFile strings have little endian signed lengths, and embedded SWF
commands retain SWF little endian/bit-packed encoding. This is an inventory tool,
not a converter, runtime safety validator, or proof of dynamic resource closure.
"""

from collections import Counter
import hashlib
import math
import re
import struct

MAX_BYTES = 64 * 1024 * 1024
MAX_ITEMS = 1_000_000
MAX_DICTIONARY = 65_536
MAX_ACTIONS = 1_000_000
MAX_DEPTH = 64
MAX_PATHS = 20_000
DICT_NAMES = ("null", "image", "shape", "morph", "sprite", "font", "text", "edittext")
TAG_NAMES = {12: "DoAction", 26: "PlaceObject2", 28: "RemoveObject2", 70: "PlaceObject3"}
ACTION_NAMES = {
    0: "End", 4: "NextFrame", 5: "PrevFrame", 6: "Play", 7: "Stop", 8: "ToggleQuality",
    9: "StopSounds", 10: "Add", 11: "Subtract", 12: "Multiply", 13: "Divide", 14: "Equals",
    15: "Less", 16: "And", 17: "Or", 18: "Not", 19: "StringEquals", 20: "StringLength",
    21: "StringExtract", 23: "Pop", 24: "ToInteger", 28: "GetVariable", 29: "SetVariable",
    32: "SetTarget2", 33: "StringAdd", 34: "GetProperty", 35: "SetProperty", 36: "CloneSprite",
    37: "RemoveSprite", 38: "Trace", 39: "StartDrag", 40: "EndDrag", 41: "StringLess",
    42: "Throw", 43: "CastOp", 44: "ImplementsOp", 48: "RandomNumber", 49: "MBStringLength",
    50: "CharToAscii", 51: "AsciiToChar", 52: "GetTime", 53: "MBStringExtract",
    54: "MBCharToAscii", 55: "MBAsciiToChar", 58: "Delete", 59: "Delete2", 60: "DefineLocal",
    61: "CallFunction", 62: "Return", 63: "Modulo", 64: "NewObject", 65: "DefineLocal2",
    66: "InitArray", 67: "InitObject", 68: "TypeOf", 69: "TargetPath", 70: "Enumerate",
    71: "Add2", 72: "Less2", 73: "Equals2", 74: "ToNumber", 75: "ToString", 76: "PushDuplicate",
    77: "StackSwap", 78: "GetMember", 79: "SetMember", 80: "Increment", 81: "Decrement",
    82: "CallMethod", 83: "NewMethod", 84: "InstanceOf", 85: "Enumerate2", 96: "BitAnd",
    97: "BitOr", 98: "BitXor", 99: "BitLShift", 100: "BitRShift", 101: "BitURShift",
    102: "StrictEquals", 103: "Greater", 104: "StringGreater", 105: "Extends",
    129: "GotoFrame", 131: "GetURL", 135: "StoreRegister", 136: "ConstantPool",
    138: "WaitForFrame", 139: "SetTarget", 140: "GoToLabel", 141: "WaitForFrame2",
    142: "DefineFunction2", 143: "Try", 148: "With", 150: "Push", 153: "Jump",
    154: "GetURL2", 155: "DefineFunction", 157: "If", 158: "Call", 159: "GotoFrame2",
}
LOCALIZATION = re.compile(r"(?<![A-Za-z0-9_])#str_[A-Za-z0-9_]+(?![A-Za-z0-9_])", re.I)
IDENTIFIER = re.compile(r"[A-Za-z_$][A-Za-z0-9_$.:]*\Z")
RESOURCE = re.compile(r"(?:[A-Za-z0-9_./\\-]+\.(?:tga|dds|bimage|bswf|swf|dat|mtr|wav|ogg))\Z", re.I)


class SwfError(ValueError):
    """Malformed input or an explicit inspection bound was exceeded."""


class Reader:
    def __init__(self, data, base=0):
        self.data = memoryview(data)
        self.offset = 0
        self.base = base

    @property
    def left(self):
        return len(self.data) - self.offset

    def fail(self, reason):
        raise SwfError(f"{reason} at byte {self.base + self.offset}")

    def take(self, size):
        if not 0 <= size <= self.left:
            self.fail("truncated BSWF field")
        start = self.offset
        self.offset += size
        return self.data[start:self.offset]

    def read(self, fmt):
        result = struct.unpack(fmt, self.take(struct.calcsize(fmt)))
        return result[0] if len(result) == 1 else result

    def count(self, minimum=0, limit=MAX_ITEMS, stride=1):
        count = self.read(">i")
        if not minimum <= count <= limit or count > self.left // stride:
            self.fail("invalid BSWF count")
        return count

    def string(self):
        size = self.read("<i")
        if not 0 <= size <= min(self.left, 1_048_576):
            self.fail("invalid idFile string length")
        raw = bytes(self.take(size))
        if b"\0" in raw:
            self.fail("embedded null in idFile string")
        return raw.decode("utf-8", errors="replace")

    def cstring(self):
        end = 0
        while end < min(self.left, 1_048_577) and self.data[self.offset + end]:
            end += 1
        if end == self.left or end > 1_048_576:
            self.fail("unterminated SWF string")
        result = bytes(self.take(end)).decode("utf-8", errors="replace")
        self.take(1)
        return result

    def floats(self, count, endian=">"):
        values = struct.unpack(endian + str(count) + "f", self.take(count * 4))
        if not all(math.isfinite(value) for value in values):
            self.fail("non-finite BSWF float")
        return list(values)

    def end(self):
        if self.left:
            self.fail("unconsumed BSWF field bytes")


class Bits:
    def __init__(self, reader):
        self.reader = reader
        self.bits = 0
        self.value = 0

    def unsigned(self, count):
        if not 0 <= count <= 32:
            self.reader.fail("invalid bit width")
        while self.bits < count:
            self.value = (self.value << 8) | self.reader.read("B")
            self.bits += 8
        self.bits -= count
        return (self.value >> self.bits) & ((1 << count) - 1)

    def signed(self, count):
        value = self.unsigned(count)
        return value - (1 << count) if count and value & (1 << (count - 1)) else value


def _matrix(reader):
    bits = Bits(reader)
    xx, yy, yx, xy = 1.0, 1.0, 0.0, 0.0
    if bits.unsigned(1):
        width = bits.unsigned(5)
        xx, yy = (bits.signed(width) / 65536 for _ in range(2))
    if bits.unsigned(1):
        width = bits.unsigned(5)
        yx, xy = (bits.signed(width) / 65536 for _ in range(2))
    width = bits.unsigned(5)
    tx, ty = (bits.signed(width) / 20 for _ in range(2))
    return [xx, yy, xy, yx, tx, ty]


def _color(reader):
    bits = Bits(reader)
    add, mul, width = bits.unsigned(1), bits.unsigned(1), bits.unsigned(4)
    multiplier = [bits.signed(width) / 256 for _ in range(4)] if mul else [1.0] * 4
    additive = [bits.signed(width) / 256 for _ in range(4)] if add else [0.0] * 4
    return multiplier, additive


def _bounds(reader):
    bounds = reader.floats(4)
    if bounds[2] < bounds[0] or bounds[3] < bounds[1]:
        reader.fail("inverted BSWF bounds")
    return bounds


def _vertices(reader):
    count = reader.count(limit=65_536, stride=8)
    bounds = None
    for _ in range(count):
        x, y = reader.floats(2)
        if bounds is None:
            bounds = [x, y, x, y]
        else:
            bounds = [min(bounds[0], x), min(bounds[1], y), max(bounds[2], x), max(bounds[3], y)]
    return count, bounds


def _indices(reader, vertices):
    count = reader.count(stride=2)
    if count % 3:
        reader.fail("triangle index count is not divisible by three")
    for _ in range(count):
        if reader.read(">H") >= vertices:
            reader.fail("triangle vertex index outside vertex list")
    return count


class Inspector:
    def __init__(self):
        self.counts = Counter()
        self.features = Counter()
        self.dependencies = {"images": set(), "fonts": set(), "localization_ids": set(), "script_resource_candidates": set()}
        self.opcodes = Counter()
        self.string_hashes = set()
        self.functions = set()
        self.call_candidates = set()
        self.limitations = {"AVM1 is inventoried without execution; dynamic names, control flow, callbacks and engine-injected resources are not closed."}
        self.entries = []
        self.references = []
        self.sprites = []
        self.blends = set()
        self.filter_types = set()
        self.action_count = 0
        self.literal_codepoints = set()

    def literal(self, value):
        self.counts["script_strings"] += 1
        self.literal_codepoints.update(ord(char) for char in value if char.isprintable())
        self.string_hashes.add(hashlib.sha256(value.encode("utf-8")).hexdigest())
        self.dependencies["localization_ids"].update(LOCALIZATION.findall(value))
        if RESOURCE.fullmatch(value):
            self.dependencies["script_resource_candidates"].add(value.replace("\\", "/"))

    def actions(self, reader, depth=0, constants=None):
        if depth > MAX_DEPTH:
            reader.fail("AVM1 nesting exceeds inspection bound")
        constants = list(constants or [])
        starts, branches = set(), []
        last_literal = None
        while reader.left:
            start = reader.offset
            starts.add(start)
            opcode = reader.read("B")
            self.action_count += 1
            if self.action_count > MAX_ACTIONS:
                reader.fail("AVM1 action count exceeds inspection bound")
            self.opcodes[ACTION_NAMES.get(opcode, f"unknown_0x{opcode:02x}")] += 1
            if opcode not in ACTION_NAMES:
                self.limitations.add("Unknown AVM1 opcodes were length-checked but their payload semantics are opaque.")
            size = reader.read("<H") if opcode >= 0x80 else 0
            body = Reader(reader.take(size), reader.base + reader.offset - size)
            if opcode == 0:
                if reader.left and any(reader.take(reader.left)):
                    reader.fail("nonzero data after AVM1 End")
                break
            if opcode == 0x96:
                while body.left:
                    kind = body.read("B")
                    value = None
                    if kind == 0:
                        value = body.cstring()
                        self.literal(value)
                    elif kind == 1:
                        body.floats(1, "<")
                    elif kind in (2, 3):
                        pass
                    elif kind in (4, 5):
                        body.take(1)
                    elif kind == 6:
                        raw = bytes(body.take(8))
                        value_float = struct.unpack("<d", raw[4:] + raw[:4])[0]
                        if not math.isfinite(value_float):
                            body.fail("non-finite AVM1 double")
                    elif kind == 7:
                        body.take(4)
                    elif kind in (8, 9):
                        index = body.read("B" if kind == 8 else "<H")
                        if index >= len(constants):
                            # Pools can be inherited or supplied along another control-flow path.
                            self.features["unresolved_constant_references"] += 1
                            self.limitations.add("Some AVM1 constant-pool references need runtime scope/control-flow resolution.")
                        else:
                            value = constants[index]
                    else:
                        body.fail("unknown AVM1 Push value type")
                    last_literal = value
            elif opcode == 0x88:
                count = body.read("<H")
                if count > body.left:
                    body.fail("invalid AVM1 constant-pool count")
                constants = [body.cstring() for _ in range(count)]
                for value in constants:
                    self.literal(value)
            elif opcode in (0x9B, 0x8E):
                name = body.cstring()
                self.counts["script_functions"] += 1
                if name and IDENTIFIER.fullmatch(name):
                    self.functions.add(name)
                parameters = body.read("<H")
                if opcode == 0x8E:
                    registers = body.read("B") + 1
                    body.take(2)
                if parameters > body.left:
                    body.fail("invalid AVM1 parameter count")
                for _ in range(parameters):
                    if opcode == 0x8E and body.read("B") >= registers:
                        body.fail("AVM1 parameter register outside register list")
                    body.cstring()
                code_size = body.read("<H")
                # Most files include code in ActionLength; old exporters may exclude it.
                if code_size > body.left and body.left == 0:
                    nested = Reader(reader.take(code_size), reader.base + reader.offset - code_size)
                    self.limitations.add("An AVM1 function body extends past its action header length, as accepted by the native interpreter.")
                else:
                    nested = Reader(body.take(code_size), body.base + body.offset - code_size)
                self.actions(nested, depth + 1, constants)
                last_literal = None
            elif opcode == 0x94:
                code_size = body.read("<H")
                source = body if code_size <= body.left else reader
                self.actions(Reader(source.take(code_size), source.base + source.offset - code_size), depth + 1, constants)
            elif opcode == 0x8F:
                flags = body.read("B")
                sizes = [body.read("<H") for _ in range(3)]
                body.take(1) if flags & 4 else body.cstring()
                for code_size in sizes:
                    source = body if code_size <= body.left else reader
                    self.actions(Reader(source.take(code_size), source.base + source.offset - code_size), depth + 1, constants)
                self.features["try_blocks"] += 1
            elif opcode in (0x99, 0x9D):
                delta = body.read("<h")
                branches.append(reader.offset + delta)
                last_literal = None
            elif opcode in (0x83,):
                self.literal(body.cstring())
                self.literal(body.cstring())
            elif opcode in (0x8B, 0x8C):
                self.literal(body.cstring())
            elif opcode in (0x81,):
                body.take(2)
            elif opcode == 0x8A:
                body.take(3)
            elif opcode in (0x87, 0x8D, 0x9A):
                body.take(1)
            elif opcode == 0x9F:
                flags = body.read("B")
                if flags & 2:
                    body.take(2)
            elif opcode >= 0x80:
                body.take(body.left)
                self.limitations.add("Opaque long AVM1 actions were length-checked; payload semantics may require additional inspection.")
            elif opcode in (0x3D, 0x52, 0x40, 0x53):
                self.features["dynamic_calls"] += 1
                if last_literal and IDENTIFIER.fullmatch(last_literal):
                    self.call_candidates.add(last_literal)
                last_literal = None
            elif opcode not in (0x4C,):
                last_literal = None
            body.end()
        starts.add(reader.offset)
        for target in branches:
            if target not in starts:
                reader.fail("AVM1 branch target outside action boundaries")

    def filter_list(self, reader):
        count = reader.read("B")
        self.features["filters"] += count
        for _ in range(count):
            kind = reader.read("B")
            self.filter_types.add(kind)
            sizes = {0: 23, 1: 9, 2: 15, 3: 27, 6: 80}
            if kind in sizes:
                if kind == 6:
                    reader.floats(20, "<")
                else:
                    reader.take(sizes[kind])
            elif kind in (4, 7):
                colors = reader.read("B")
                reader.take(colors * 5 + 19)
            elif kind == 5:
                x, y = reader.read("BB")
                reader.floats(2 + x * y, "<")
                reader.take(5)
            else:
                reader.fail("unknown SWF filter encoding")
        self.limitations.add("Native SWF skips filter rendering and the fields following a filter list.")

    def clip_actions(self, reader):
        data = reader.take(reader.left)
        last_error = None
        for width in (4, 2):
            trial = Reader(data, reader.base + reader.offset - len(data))
            try:
                if trial.read("<H") != 0:
                    trial.fail("nonzero ClipActions reserved field")
                trial.take(width)
                records = []
                while True:
                    flags = trial.read("<I" if width == 4 else "<H")
                    if flags == 0:
                        break
                    size = trial.read("<I")
                    body = Reader(trial.take(size), trial.base + trial.offset - size)
                    if flags & 0x20000:
                        body.take(1)
                    records.append(body)
                trial.end()
            except SwfError as error:
                last_error = error
                continue
            for body in records:
                self.actions(body)
            self.features["clip_action_records"] += len(records)
            self.limitations.add("ClipActions are present but the native engine ignores them.")
            return
        raise last_error

    def place(self, reader, tag):
        flags = reader.read("B")
        flags2 = reader.read("B") if tag == 70 else 0
        if flags2 & 0xE0:
            reader.fail("reserved PlaceObject3 flags")
        depth = reader.read("<H")
        if depth == 0:
            reader.fail("zero display-list depth")
        if flags2 & 8 or (flags2 & 16 and flags & 2):
            reader.cstring()
            self.features["class_names"] += 1
        character = reader.read("<H") if flags & 2 else None
        if not flags & 1 and character is None:
            reader.fail("new PlaceObject without a character")
        matrix = _matrix(reader) if flags & 4 else None
        if matrix is not None:
            if matrix[2] or matrix[3]:
                self.features["rotated_or_sheared_placements"] += 1
        color = _color(reader) if flags & 8 else None
        if color:
            if color[0] != [1.0] * 4:
                self.features["color_multiply"] += 1
            if color[0][3] != 1:
                self.features["alpha_multiply"] += 1
            if any(color[1]):
                self.features["color_add"] += 1
        ratio = reader.read("<H") if flags & 16 else None
        name = reader.cstring() if flags & 32 else None
        clip_depth = reader.read("<H") if flags & 64 else None
        if clip_depth is not None:
            if clip_depth <= depth:
                reader.fail("clipDepth does not follow mask depth")
            self.features["masks"] += 1
        if flags2 & 1:
            self.filter_list(reader)
        blend = reader.read("B") if flags2 & 2 else 0
        self.blends.add(blend)
        if blend > 14:
            reader.fail("invalid SWF blend mode")
        if flags2 & 4:
            reader.take(1)
            self.features["cache_as_bitmap"] += 1
        if flags & 128:
            self.clip_actions(reader)
        reader.end()
        return {"character": character, "depth": depth, "name": name, "matrix": matrix,
                "color": {"multiply": color[0], "add": color[1]} if color else None,
                "ratio": ratio, "clip_depth": clip_depth, "blend_mode": blend, "move": bool(flags & 1)}

    def sprite(self, reader, dictionary_id):
        frames = reader.read(">H")
        if frames == 0:
            reader.fail("zero SWF frame count")
        offset_count = reader.count(limit=65_537, stride=4)
        if offset_count != frames + 1:
            reader.fail("frame-offset count does not match frame count")
        offsets = [reader.read(">I") for _ in range(offset_count)]
        if offsets[0] or offsets != sorted(offsets):
            reader.fail("invalid SWF frame offsets")
        labels = []
        for _ in range(reader.count(stride=8)):
            frame = reader.read(">I")
            if not 1 <= frame <= frames:
                reader.fail("frame label outside timeline")
            labels.append({"frame": frame, "name": reader.string()})
        buffer_size = reader.read(">I")
        blob_base = reader.base + reader.offset
        blob = Reader(reader.take(buffer_size), blob_base)
        commands = []
        command_count = reader.count(stride=8)
        if offsets[-1] != command_count:
            reader.fail("last frame offset does not match command count")
        frame = 1
        for index in range(command_count):
            while frame < len(offsets) - 1 and index >= offsets[frame]:
                frame += 1
            tag, size = reader.read(">iI")
            if tag not in TAG_NAMES:
                reader.fail("unknown retained native SWF command tag")
            body = Reader(blob.take(size), blob.base + blob.offset - size)
            self.counts["commands"] += 1
            self.counts[TAG_NAMES[tag]] += 1
            if tag in (26, 70):
                placement = self.place(body, tag)
                placement["frame"] = frame
                commands.append(placement)
                if placement["character"] is not None:
                    self.references.append((placement["character"], "display"))
            elif tag == 12:
                self.actions(body)
            else:
                body.take(2)
                body.end()
        init_count = reader.count(stride=4)
        for _ in range(init_count):
            size = reader.read(">I")
            self.actions(Reader(blob.take(size), blob.base + blob.offset - size))
        blob.end()
        self.counts["init_actions"] += init_count
        self.counts["frames"] += frames
        sprite = {"dictionary_id": dictionary_id, "frames": frames, "labels": labels,
                  "commands": command_count, "command_bytes": buffer_size, "placements": commands}
        self.sprites.append(sprite)
        return sprite

    def shape(self, reader, morph):
        result = {"start_bounds": _bounds(reader), "end_bounds": _bounds(reader), "fills": [], "lines": []}
        if morph:
            self.features["morphs"] += 1
        for _ in range(reader.count(stride=73)):
            kind, subtype = reader.read("BB")
            if kind not in (0, 1, 4) or (kind == 1 and subtype not in (0, 2, 3)) or (kind == 4 and subtype > 3):
                reader.fail("unknown fill style")
            colors = list(reader.take(8))
            matrices = [reader.floats(6), reader.floats(6)]
            gradients = reader.read("B")
            if gradients > 16:
                reader.fail("gradient record count exceeds native array")
            reader.take(gradients * 10)
            focal = reader.floats(1)[0]
            bitmap = reader.read(">H")
            if kind == 4 and bitmap != 65_535:
                self.references.append((bitmap, "image"))
            start_count, start_bounds = _vertices(reader)
            end_count, end_bounds = _vertices(reader)
            if end_count not in (0, start_count) or morph and end_count != start_count:
                reader.fail("morph vertex counts differ")
            indices = _indices(reader, start_count)
            self.counts["vertices"] += start_count + end_count
            self.counts["indices"] += indices
            self.features[{0: "solid_fills", 1: "gradient_fills", 4: "bitmap_fills"}[kind]] += 1
            result["fills"].append({"type": kind, "subtype": subtype, "bitmap_id": bitmap if kind == 4 else None,
                                    "matrices": matrices, "gradient_records": gradients, "focal_point": focal,
                                    "color_alpha": [colors[3], colors[7]], "vertices": [start_count, end_count],
                                    "vertex_bounds": [start_bounds, end_bounds], "indices": indices})
        for _ in range(reader.count(stride=24)):
            widths = list(reader.read(">HH"))
            reader.take(8)
            start_count, start_bounds = _vertices(reader)
            end_count, end_bounds = _vertices(reader)
            if end_count not in (0, start_count) or morph and end_count != start_count:
                reader.fail("morph line vertex counts differ")
            indices = _indices(reader, start_count)
            self.features["strokes"] += 1
            self.counts["vertices"] += start_count + end_count
            self.counts["indices"] += indices
            result["lines"].append({"widths": widths, "vertices": [start_count, end_count],
                                    "vertex_bounds": [start_bounds, end_bounds], "indices": indices})
        return result

    def dictionary(self, reader):
        for dictionary_id in range(reader.count(limit=MAX_DICTIONARY, stride=4)):
            kind = reader.read(">i")
            if not 0 <= kind < len(DICT_NAMES):
                reader.fail("unknown native dictionary type")
            entry = {"id": dictionary_id, "type": DICT_NAMES[kind]}
            if kind == 1:
                name = reader.string()
                dimensions = list(reader.read(">4i"))
                width, x, height, y = dimensions
                if width <= 0 or height <= 0 or x < 0 or y < 0:
                    reader.fail("invalid SWF image dimensions or atlas offset")
                entry.update({"material": None if name.startswith(".") else name, "atlas": name.startswith("."),
                              "size": [width, height], "atlas_offset": [x, y], "channel_scale": reader.floats(4)})
                if not name.startswith("."):
                    self.dependencies["images"].add(name)
                else:
                    self.features["atlas_images"] += 1
            elif kind in (2, 3):
                entry.update(self.shape(reader, kind == 3))
            elif kind == 4:
                self.sprite(reader, dictionary_id)
            elif kind == 5:
                name = reader.string()
                self.dependencies["fonts"].add(name)
                entry.update({"name": name, "metrics": list(reader.read(">3h"))})
                count = reader.count(stride=12)
                entry["glyphs"] = count
                for _ in range(count):
                    reader.take(4)
                    vertices, _ = _vertices(reader)
                    _indices(reader, vertices)
            elif kind == 6:
                entry["bounds"] = _bounds(reader)
                entry["matrix"] = reader.floats(6)
                records = []
                for _ in range(reader.count(stride=15)):
                    font = reader.read(">H")
                    reader.take(4)
                    reader.take(6)
                    first, count = reader.read(">HB")
                    self.references.append((font, "font"))
                    records.append((font, first, count))
                glyph_count = reader.count(stride=8)
                glyphs = [reader.read(">Ii") for _ in range(glyph_count)]
                for font, first, count in records:
                    if first + count > glyph_count:
                        reader.fail("static text glyph range outside list")
                    for glyph, _ in glyphs[first:first + count]:
                        self.references.append((font, ("glyph", glyph)))
                entry.update({"records": len(records), "glyphs": glyph_count})
                self.features["static_text"] += 1
            elif kind == 7:
                entry["bounds"] = _bounds(reader)
                flags, font, font_height = reader.read(">IHH")
                if flags & ~63:
                    reader.fail("unknown edit text flags")
                self.references.append((font, "font"))
                reader.take(4)
                maximum, align = reader.read(">Hi")
                if not 0 <= align <= 3:
                    reader.fail("unknown edit text alignment")
                reader.take(8)
                variable, initial = reader.string(), reader.string()
                self.dependencies["localization_ids"].update(LOCALIZATION.findall(initial))
                self.literal_codepoints.update(ord(char) for char in initial if char.isprintable())
                entry.update({"flags": flags, "font_id": font, "font_height": font_height, "max_length": maximum,
                              "align": align, "variable_sha256": hashlib.sha256(variable.encode()).hexdigest() if variable else None,
                              "initial_text_bytes": len(initial.encode()), "initial_text_sha256": hashlib.sha256(initial.encode()).hexdigest()})
                self.features["edit_text"] += 1
            self.entries.append(entry)
        # Bounds use the vertex AABB, so they conservatively enclose the actual UVs.
        # Native swfMatrix_t::Inverse leaves translation zero for nonsingular matrices.
        for entry in self.entries:
            for fill in entry.get("fills", []):
                bitmap = fill["bitmap_id"]
                if bitmap is None or bitmap == 65_535 or bitmap >= len(self.entries):
                    continue
                image = self.entries[bitmap]
                if image["type"] != "image":
                    continue
                shape_bounds = entry["start_bounds"]
                uv_bounds = []
                for matrix, vertex_bounds in zip(fill["matrices"], fill["vertex_bounds"]):
                    if vertex_bounds is None:
                        uv_bounds.append(None)
                        continue
                    xx, yy, xy, yx, tx, ty = matrix
                    determinant = xx * yy - yx * xy
                    if abs(determinant) >= 1.1754943508222875e-38:
                        xx, yy, xy, yx, tx, ty = yy / determinant, xx / determinant, -xy / determinant, -yx / determinant, 0, 0
                    corners = []
                    for x in (vertex_bounds[0], vertex_bounds[2]):
                        for y in (vertex_bounds[1], vertex_bounds[3]):
                            u = (x - shape_bounds[0]) * 20 / image["size"][0]
                            v = (y - shape_bounds[1]) * 20 / image["size"][1]
                            corners.append((u * xx + v * xy + tx, v * yy + u * yx + ty))
                    uv_bounds.append([min(u for u, _ in corners), min(v for _, v in corners),
                                      max(u for u, _ in corners), max(v for _, v in corners)])
                fill["bitmap_pre_atlas_uv_bounds_conservative"] = uv_bounds
        for dictionary_id, expected in self.references:
            if not 0 <= dictionary_id < len(self.entries):
                reader.fail("dictionary reference outside list")
            entry = self.entries[dictionary_id]
            if expected == "display":
                if entry["type"] not in ("shape", "morph", "sprite", "text", "edittext", "image"):
                    reader.fail("display reference has invalid dictionary type")
            elif isinstance(expected, tuple):
                if entry["type"] != "font" or expected[1] >= entry["glyphs"]:
                    reader.fail("static text glyph outside font")
            elif entry["type"] != expected:
                reader.fail("dictionary reference has wrong type")

    def paths(self):
        sprites = {sprite["dictionary_id"]: sprite for sprite in self.sprites}
        result = []
        stack = [(None, "_root", ())]
        while stack:
            dictionary_id, path, parents = stack.pop()
            sprite = sprites[dictionary_id]
            for placement in sprite["placements"]:
                character = placement["character"]
                if character is None:
                    continue
                child = path + "." + (placement["name"] or f"@{placement['depth']}")
                result.append({"path": child, "character": character, "type": self.entries[character]["type"],
                               "frame": placement["frame"]})
                if len(result) >= MAX_PATHS:
                    self.limitations.add("Instance-path expansion reached its 20000-entry bound; sprite dictionary summaries remain complete.")
                    return result
                if character in sprites:
                    if character in parents or len(parents) >= MAX_DEPTH:
                        self.limitations.add("Recursive/deep sprite placement prevents complete static instance-path expansion.")
                    else:
                        stack.append((character, child, parents + (character,)))
        return sorted(result, key=lambda item: (item["path"], item["frame"], item["character"]))


def inspect_swf(data: bytes) -> dict:
    """Return a deterministic JSON-safe inventory, or raise SwfError for invalid input.

    File size, list counts, reads, finite floats, triangle indices, dictionary
    references, frame ranges, action boundaries and recursion are bounded. The
    result omits general text, AVM1 source bytes and raw script string literals.
    """
    if not isinstance(data, bytes):
        raise TypeError("inspect_swf expects immutable bytes")
    if len(data) > MAX_BYTES:
        raise SwfError("BSWF exceeds 64 MiB inspection bound")
    reader = Reader(data)
    if reader.read(">I") != 0x42535710:
        reader.fail("unsupported native BSWF magic/version; expected BSWF16")
    timestamp = reader.read(">q")
    width, height = reader.floats(2)
    rate = reader.read(">H")
    if not 0 < width <= 1_048_576 or not 0 < height <= 1_048_576 or rate == 0:
        reader.fail("invalid BSWF frame dimensions/rate")
    inspector = Inspector()
    inspector.sprite(reader, None)
    inspector.dictionary(reader)
    reader.end()
    paths = inspector.paths()
    return {
        "format": "BSWF16", "version": 16, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
        "source_timestamp": timestamp, "frame": {"width": width, "height": height, "rate": rate / 256},
        "dictionary_counts": dict(sorted(Counter(entry["type"] for entry in inspector.entries).items())),
        "counts": dict(sorted(inspector.counts.items())),
        "features": {**dict(sorted(inspector.features.items())), "blend_modes": sorted(inspector.blends),
                     "filter_types": sorted(inspector.filter_types)},
        "dependencies": {name: sorted(values) for name, values in sorted(inspector.dependencies.items())},
        "dictionary": inspector.entries, "sprites": inspector.sprites, "instance_paths": paths,
        "text": {"literal_codepoints": sorted(inspector.literal_codepoints),
                 "coverage_scope": "Conservative union of printable edittext initial values and AVM1 string literals/pools; includes identifiers and paths, excludes runtime-generated strings."},
        "scripts": {"opcodes": dict(sorted(inspector.opcodes.items())), "actions": inspector.action_count,
                    "string_sha256": sorted(inspector.string_hashes), "defined_functions": sorted(inspector.functions),
                    "literal_call_candidates": sorted(inspector.call_candidates)},
        "limitations": sorted(inspector.limitations), "dynamic_dependency_closure": False,
    }
