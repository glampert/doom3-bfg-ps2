#!/usr/bin/env python3
# ================================================================================================
# File: test_authored_ui.py
# Brief: Check the authored native SWF container, AVM1 payloads and font ABI before target loading.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import importlib.util
import math
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("build_authored_ui", ROOT / "src/tools/scripts/build_authored_ui.py")
builder = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(builder)


class Reader:
    def __init__(self, data):
        self.data = data
        self.offset = 0

    def take(self, length):
        if length < 0 or length > len(self.data) - self.offset:
            raise ValueError("truncated native UI resource")
        start = self.offset
        self.offset += length
        return self.data[start:self.offset]

    def unpack(self, format_string):
        values = struct.unpack(format_string, self.take(struct.calcsize(format_string)))
        return values[0] if len(values) == 1 else values

    def count(self, limit):
        value = self.unpack(">i")
        if not 0 <= value <= limit:
            raise ValueError("native UI count exceeds authored profile")
        return value

    def text(self):
        length = self.unpack("<i")
        if not 0 <= length <= 96:
            raise ValueError("invalid native string length")
        return self.take(length).decode("ascii")

    def vectors(self, count):
        values = self.unpack(">" + "f" * count)
        if not all(math.isfinite(value) and abs(value) <= 32768 for value in values):
            raise ValueError("invalid authored float")
        return values


def read_sprite(reader):
    frames = reader.unpack(">H")
    if not 1 <= frames <= 4:
        raise ValueError("invalid authored frame count")
    offsets = [reader.unpack(">I") for _ in range(reader.count(5))]
    if len(offsets) != frames + 1 or offsets[0] != 0 or offsets != sorted(offsets):
        raise ValueError("invalid native frame offsets")
    if reader.count(0) != 0:
        raise ValueError("unexpected frame labels")
    blob = reader.take(reader.count(8192))
    count = reader.count(32)
    commands = []
    consumed = 0
    for _ in range(count):
        tag, length = reader.unpack(">iI")
        if tag not in (12, 26) or length > len(blob) - consumed:
            raise ValueError("invalid native sprite command")
        commands.append((tag, blob[consumed:consumed + length]))
        consumed += length
    if consumed != len(blob) or offsets[-1] != count or reader.count(0) != 0:
        raise ValueError("invalid native command buffer accounting")
    return {"frames": frames, "offsets": offsets, "commands": commands}


def read_shape(reader):
    bounds = reader.vectors(8)
    fills = []
    for _ in range(reader.count(4)):
        kind, subtype = reader.unpack(">BB")
        colors = reader.take(8)
        matrices = reader.vectors(12)
        if kind not in (0, 4) or subtype != 3 or reader.unpack(">B") != 0:
            raise ValueError("unsupported authored fill")
        focal, bitmap = reader.unpack(">fH")
        vertices = [reader.vectors(2) for _ in range(reader.count(16))]
        if reader.count(0) != 0:
            raise ValueError("unexpected morph vertices")
        indexes = [reader.unpack(">H") for _ in range(reader.count(24))]
        if focal != 0 or len(indexes) % 3 or any(index >= len(vertices) for index in indexes):
            raise ValueError("invalid native fill geometry")
        fills.append({"kind": kind, "bitmap": bitmap, "vertices": vertices, "indexes": indexes,
                      "colors": colors, "matrices": matrices})
    if reader.count(0) != 0:
        raise ValueError("unexpected line draw")
    return {"bounds": bounds, "fills": fills}


def read_swf(data):
    reader = Reader(data)
    if reader.unpack(">IqffH") != (0x42535710, -1, 640, 480, 1536):
        raise ValueError("invalid authored BSWF16 header")
    main = read_sprite(reader)
    dictionary = []
    for _ in range(reader.count(16)):
        kind = reader.unpack(">i")
        if kind == 0:
            entry = {}
        elif kind == 1:
            entry = {"name": reader.text(), "dimensions": reader.unpack(">4i"), "channels": reader.vectors(4)}
        elif kind == 2:
            entry = read_shape(reader)
        elif kind == 4:
            entry = read_sprite(reader)
        elif kind == 5:
            entry = {"name": reader.text(), "metrics": reader.unpack(">3h"), "glyphs": reader.count(0)}
        elif kind == 7:
            entry = {"bounds": reader.vectors(4), "flags": reader.unpack(">I"), "font": reader.unpack(">H"),
                     "height": reader.unpack(">H"), "color": reader.take(4), "limits": reader.unpack(">HiHHHh"),
                     "variable": reader.text(), "text": reader.text()}
        else:
            raise ValueError("unexpected native dictionary entry")
        dictionary.append({"kind": kind, **entry})
    if reader.offset != len(data):
        raise ValueError("trailing bytes in native UI resource")
    return main, dictionary


def actions(data):
    reader = Reader(data)
    result = []
    while reader.offset != len(data):
        opcode = reader.unpack("B")
        length = reader.unpack("<H") if opcode >= 0x80 else 0
        payload = reader.take(length)
        result.append((opcode, payload))
        if opcode == 0 and reader.offset != len(data):
            raise ValueError("unexpected bytes after AVM1 End")
    if not result or result[-1][0] != 0:
        raise ValueError("unterminated AVM1 stream")
    return result


class AuthoredNativeUI(unittest.TestCase):
    def test_tracked_resources_and_compiled_whitelist_are_reproducible(self):
        for relative, expected in builder.outputs().items():
            with self.subTest(path=str(relative)):
                self.assertEqual((ROOT / relative).read_bytes(), expected)
        assets = builder.outputs()
        header = assets[builder.HEADER_PATH].decode("ascii")
        arrays = re.findall(r"static constexpr std::uint8_t kResource\d+\[\] = \{(.*?)\};", header, re.S)
        for array, path in zip(arrays, (builder.SWF_PATH, builder.FONT_PATH, builder.MATERIAL_PATH), strict=True):
            self.assertEqual(bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", array)),
                             assets[builder.ASSET_ROOT / "base" / path])

    def test_native_container_and_bitmap_references(self):
        main, dictionary = read_swf(builder.build_swf())
        self.assertEqual(main["frames"], 1)
        self.assertEqual(len(dictionary), 16)
        self.assertEqual([entry["kind"] for entry in dictionary], [0, 2, 2, 1, 2, 2, 4, 2, 4, 5, 7, 7, 7, 7, 7, 7])
        self.assertEqual(dictionary[3]["dimensions"], (16, 16, 16, 64))
        bitmap_fill = dictionary[4]["fills"][0]
        self.assertEqual((bitmap_fill["kind"], bitmap_fill["bitmap"]), (4, 3))
        self.assertEqual(bitmap_fill["indexes"], [0, 1, 2, 0, 2, 3])
        self.assertEqual(dictionary[9]["name"], "ps2_ascii")
        self.assertEqual(dictionary[12]["text"], "TAB TO FOCUS / ENTER TO SELECT")
        for entry in dictionary[10:]:
            self.assertEqual(entry["font"], 9)
            self.assertEqual(entry["flags"], 8)  # Read-only native edittext.

    def test_mask_free_timeline_contains_real_color_transform_commands(self):
        _, dictionary = read_swf(builder.build_swf())
        pulse = dictionary[6]
        self.assertEqual((pulse["frames"], pulse["offsets"]), (4, [0, 1, 2, 3, 4]))
        commands = pulse["commands"]
        self.assertEqual(len({payload for _, payload in commands}), 4)
        for index, (tag, payload) in enumerate(commands):
            self.assertEqual(tag, 26)
            self.assertEqual(payload[0], 14 if index == 0 else 9)
            self.assertFalse(payload[0] & 0xC0)  # No clip actions or clip depth.

    def test_native_actionscript_functions_own_focus_and_selection_state(self):
        main, _ = read_swf(builder.build_swf())
        script = actions(main["commands"][-1][1])
        functions = {}
        for opcode, payload in script:
            if opcode != 0x9B:
                continue
            name, rest = payload.split(b"\0", 1)
            parameters, length = struct.unpack("<HH", rest[:4])
            self.assertEqual(parameters, 0)
            self.assertEqual(length, len(rest) - 4)
            functions[name.decode("ascii")] = actions(rest[4:])
        self.assertEqual(set(functions), {"focusNext", "select"})
        focus_payload = b"".join(payload for _, payload in functions["focusNext"])
        select_payload = b"".join(payload for _, payload in functions["select"])
        self.assertIn(b"focusFrame\0", focus_payload)
        self.assertIn(b"_y\0", focus_payload)
        self.assertIn(b"FOCUS \0", focus_payload)
        self.assertIn(b"selectCount\0", select_payload)
        self.assertIn(b"SELECTED \0", select_payload)
        self.assertIn(0x50, [opcode for opcode, _ in functions["select"]])
        # No loops, jumps, dynamic calls, imports, file actions or native callbacks in this profile.
        allowed = {0, 0x0A, 0x0B, 0x0C, 0x1C, 0x47, 0x4E, 0x4F, 0x50, 0x96, 0x9B}
        for stream in [script, *functions.values()]:
            self.assertTrue(all(opcode in allowed for opcode, _ in stream))

    def test_font_matches_native_mixed_endian_padded_glyph_abi(self):
        font = builder.build_font()
        self.assertEqual(len(font), 1342)
        self.assertEqual(struct.unpack(">Ihhhh", font[:12]), (0x6964662A, 48, 14, 0, 95))
        for index, code in enumerate(range(32, 127)):
            width, height, top, left, advance, pad, s, t = struct.unpack("<BBbbBBHH", font[12 + index * 10:22 + index * 10])
            self.assertEqual((width, height, top, left, advance, pad), (15, 14, 14, 0, 16, 0))
            self.assertEqual((s, t), ((code & 15) * 32 + 1, (code >> 4) * 16 + 1))
            self.assertGreater(s, 0)  # Native scaledGlyph expands every glyph UV by half a texel.
            self.assertGreater(t, 0)
            self.assertLess(s + width, 512)
            self.assertLess(t + height, 128)
        self.assertEqual(struct.unpack("<95I", font[962:]), tuple(range(32, 127)))

    def test_materials_only_reference_backend_intrinsics(self):
        materials = builder.build_materials().decode("ascii")
        self.assertEqual(re.findall(r"        map (\S+)", materials), ["_white", "_white", "textures/bigchars", "textures/ps2_ascii"])
        self.assertEqual(materials.count("blend blend"), 4)
        self.assertNotRegex(materials, r"(?i)(\.dds|\.png|\.jpg|ui/assets|guis/assets)")

    def test_container_truncation_and_count_corruption_are_detected(self):
        data = builder.build_swf()
        for length in (0, 1, 21, 50, len(data) - 1):
            with self.subTest(length=length), self.assertRaises(ValueError):
                read_swf(data[:length])
        corrupted = bytearray(data)
        corrupted[24:28] = struct.pack(">i", 0x7FFFFFFF)  # main frame-offset count.
        with self.assertRaises(ValueError):
            read_swf(corrupted)
        with self.assertRaises(ValueError):
            read_swf(data + b"\0")

    def test_check_mode_detects_stale_assets_without_overwriting(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for relative, content in builder.outputs().items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
            command = [sys.executable, str(ROOT / "src/tools/scripts/build_authored_ui.py"), "--root", str(root), "--check"]
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
            path = root / builder.ASSET_ROOT / "base" / builder.SWF_PATH
            path.write_bytes(b"stale")
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(path.read_bytes(), b"stale")


if __name__ == "__main__":
    unittest.main()
