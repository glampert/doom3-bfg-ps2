#!/usr/bin/env python3
# ================================================================================================
# File: test_authored_ui.py
# Brief: Check the authored native SWF container, AVM1 payloads and font ABI before target loading.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import hashlib
import importlib.util
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "src/tools/scripts"))
from ui_page_package import fnv1a32

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
        if tag not in (12, 26, 70) or length > len(blob) - consumed:
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
    lines = []
    for _ in range(reader.count(1)):
        widths = reader.unpack(">HH")
        colors = reader.take(8)
        vertices = [reader.vectors(2) for _ in range(reader.count(16))]
        if reader.count(0) != 0:
            raise ValueError("unexpected morph line vertices")
        indexes = [reader.unpack(">H") for _ in range(reader.count(24))]
        if len(indexes) % 3 or any(index >= len(vertices) for index in indexes):
            raise ValueError("invalid native line geometry")
        lines.append({"widths": widths, "colors": colors, "vertices": vertices, "indexes": indexes})
    return {"bounds": bounds, "fills": fills, "lines": lines}


def read_swf(data):
    reader = Reader(data)
    if reader.unpack(">IqffH") != (0x42535710, -1, 640, 480, 1536):
        raise ValueError("invalid authored BSWF16 header")
    main = read_sprite(reader)
    dictionary = []
    for _ in range(reader.count(29)):
        kind = reader.unpack(">i")
        if kind == 0:
            entry = {}
        elif kind == 1:
            name = reader.text()
            width, x, height, y = reader.unpack(">4i")
            entry = {"name": name, "image_size": (width, height), "atlas_offset": (x, y),
                     "channels": reader.vectors(4)}
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
    def test_generated_header_uses_formatter_stable_cpp_layout(self):
        header = builder.header({"test/data": bytes((0, 127, 255))}).decode("ascii")
        expected = ("\n{\n"
                    "static constexpr std::uint8_t kResource0[] = {\n"
                    "    0x00,\n    0x7f,\n    0xff,\n};\n\n"
                    "struct Resource\n{\n"
                    "    const char * path;\n"
                    "    const std::uint8_t * bytes;\n"
                    "    std::size_t length;\n};\n\n"
                    "static constexpr Resource kResources[] = {\n"
                    '    { "test/data", kResource0, sizeof(kResource0) },\n'
                    "};\n} // namespace ps2::ui::authored\n")
        self.assertEqual(header.split("namespace ps2::ui::authored", 1)[1], expected)
        self.assertEqual(builder.header({"test/data": bytes((0, 127, 255))}).decode("ascii"), header)

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
        self.assertEqual(len(dictionary), 29)
        self.assertEqual([entry["kind"] for entry in dictionary],
                         [0, 2, 2, 1, 2, 2, 4, 2, 4, 5, 7, 7, 7, 7, 7, 7, 2, 4, 4, 7, 2, 2, 2, 2, 2, 2, 2, 2, 4])
        self.assertEqual(dictionary[3]["image_size"], (16, 16))
        self.assertEqual(dictionary[3]["atlas_offset"], (246, 64))
        bitmap_fill = dictionary[4]["fills"][0]
        self.assertEqual((bitmap_fill["kind"], bitmap_fill["bitmap"]), (4, 3))
        self.assertEqual(bitmap_fill["indexes"], [0, 1, 2, 0, 2, 3])
        self.assertEqual(dictionary[9]["name"], "ps2_ascii")
        self.assertEqual(dictionary[12]["text"], "TAB TO FOCUS / ENTER TO SELECT")
        for entry in [*dictionary[10:16], dictionary[19]]:
            self.assertEqual(entry["font"], 9)
            self.assertEqual(entry["flags"], 8)  # Read-only native edittext.

    def test_pulse_timeline_contains_real_color_transform_commands(self):
        _, dictionary = read_swf(builder.build_swf())
        pulse = dictionary[6]
        self.assertEqual((pulse["frames"], pulse["offsets"]), (4, [0, 1, 2, 3, 4]))
        commands = pulse["commands"]
        self.assertEqual(len({payload for _, payload in commands}), 4)
        for index, (tag, payload) in enumerate(commands):
            self.assertEqual(tag, 26)
            self.assertEqual(payload[0], 14 if index == 0 else 9)
            self.assertFalse(payload[0] & 0xC0)  # No clip actions or clip depth.

    def test_additive_placements_use_native_mode_eight_without_filters(self):
        from ui_swf import inspect_swf
        movie = inspect_swf(builder.build_swf())
        self.assertEqual(movie["features"]["blend_modes"], [0, 4, 8])
        self.assertEqual(movie["features"]["masks"], 10)
        self.assertEqual(movie["features"]["color_add"], 2)
        root = next(sprite for sprite in movie["sprites"] if sprite["dictionary_id"] is None)
        self.assertFalse(any(entry["color"] and any(entry["color"]["add"]) for entry in root["placements"]))
        additions = [entry for entry in root["placements"] if entry["blend_mode"] == 8]
        self.assertEqual(len(additions), 6)
        self.assertEqual([entry["color"]["multiply"][3] for entry in additions], [0, .5, 1, 1, .5, .5])
        self.assertEqual(additions[4]["character"], 4)  # Same original bitmap, including its page seam.

    def test_convex_masks_cover_nested_non_lifo_and_same_depth_expiry(self):
        from ui_swf import inspect_swf
        movie = inspect_swf(builder.build_swf())
        sprites = {entry["dictionary_id"]: entry for entry in movie["sprites"]}
        masks = {key: [(entry["depth"], entry["clip_depth"]) for entry in value["placements"]
                       if entry["clip_depth"] is not None] for key, value in sprites.items()}
        self.assertEqual(masks[None], [(18, 20), (22, 23)])
        self.assertEqual(masks[17], [(1, 5), (2, 7), (9, 11), (10, 11)])
        self.assertEqual(masks[18], [(2, 3), (4, 5)])
        self.assertEqual(masks[28], [(6, 7), (8, 9)])
        self.assertEqual(len(sprites[None]["placements"]), 25)
        _, dictionary = read_swf(builder.build_swf())
        fill = dictionary[16]["fills"][0]
        points = fill["vertices"]
        self.assertEqual(points, [(0, 0), (320, 0), (290, 48), (30, 48)])
        self.assertEqual(fill["indexes"], [0, 1, 2, 0, 2, 3])
        for first, second, third in zip(points, points[1:] + points[:1], points[2:] + points[:2]):
            self.assertGreater((second[0] - first[0]) * (third[1] - second[1])
                               - (second[1] - first[1]) * (third[0] - second[0]), 0)
        self.assertEqual(dictionary[19]["text"], "A A A")
        for depth, clip_depth in ((1, 1), (2, 1), (1, 65536)):
            with self.subTest(depth=depth, clip_depth=clip_depth), self.assertRaises(ValueError):
                builder.place(depth, 5, clip_depth=clip_depth)

    def test_draw_features_cover_outlines_color_terms_screen_and_depth_masks(self):
        from ui_swf import inspect_swf
        movie = inspect_swf(builder.build_swf())
        features = next(sprite for sprite in movie["sprites"] if sprite["dictionary_id"] == 28)
        placements = {entry["depth"]: entry for entry in features["placements"]}
        self.assertEqual(placements[4]["blend_mode"], 4)
        self.assertEqual(placements[2]["color"], {"multiply": [1, 1, 1, 1], "add": [.25, .5, 0, 0]})
        self.assertEqual(placements[5]["character"], 4)  # The bitmap A: its holes weight the color term.
        self.assertEqual(placements[5]["color"], {"multiply": [.25, .25, .25, 1], "add": [.5, .25, 0, 0]})
        _, dictionary = read_swf(builder.build_swf())
        # Native line records hold each edge as a degenerate (a, a, b) triangle around the loop.
        outline = dictionary[20]["lines"][0]
        self.assertEqual(outline["vertices"], [(0, 0), (40, 0), (40, 30), (0, 30)])
        self.assertEqual(outline["indexes"], [0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 0])
        self.assertEqual(outline["widths"], (20, 20))
        self.assertEqual((dictionary[26]["fills"], len(dictionary[26]["lines"])), ([], 1))
        # The L's triangles fill it exactly; its notch corner makes it concave.
        concave = dictionary[24]["fills"][0]
        self.assertEqual(concave["vertices"], [(0, 0), (40, 0), (40, 15), (15, 15), (15, 30), (0, 30)])
        area = sum(abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) / 2
                   for a, b, c in (tuple(concave["vertices"][index] for index in concave["indexes"][first:first + 3])
                                   for first in range(0, 12, 3)))
        self.assertEqual(area, 40 * 15 + 15 * 15)
        with self.assertRaises(ValueError):
            builder.place(1, 5, blend=3)
        with self.assertRaises(ValueError):
            builder.place(1, 5, additive=(.5, 0, 0, 0))

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

    def test_materials_only_reference_white_and_admitted_authored_pages(self):
        materials = builder.build_materials().decode("ascii")
        self.assertEqual(re.findall(r"        map (\S+)", materials), ["_white", "_white", "textures/ps2_movie", "textures/ps2_ascii"])
        self.assertEqual(materials.count("blend blend"), 4)
        self.assertNotRegex(materials, r"(?i)(\.dds|\.png|\.jpg|ui/assets|guis/assets)")

    def test_cooked_pages_reconstruct_identical_logical_atlases_and_neighbor_gutters(self):
        assets, pages = builder.page_assets()
        for font, page in zip((False, True), pages, strict=True):
            with self.subTest(path=page["path"]):
                width, height, pixels = builder.authored_pixels(font)
                data = assets[page["path"]]
                columns, rows = (width + 253) // 254, (height + 253) // 254
                self.assertEqual(struct.unpack_from("<8I", data), (0x47504955, 1, width, height, 256, 1, columns * rows, 0))
                self.assertEqual(len(data), 32 + columns * rows * (8 + 262144))
                self.assertEqual(page["file_bytes"], len(data))
                self.assertEqual(page["source_sha256"], hashlib.sha256(pixels).hexdigest())
                self.assertEqual(page["sha256"], hashlib.sha256(data).hexdigest())
                for index in range(columns * rows):
                    offset, checksum = struct.unpack_from("<II", data, 32 + index * 8)
                    payload = data[offset:offset + 262144]
                    self.assertEqual(offset, 32 + columns * rows * 8 + index * 262144)
                    self.assertEqual(checksum, fnv1a32(payload))
                    self.assertEqual(checksum, page["checksums"][index])
                    origin_x, origin_y = index % columns * 254, index // columns * 254
                    for y in range(min(254, height - origin_y)):
                        source = ((origin_y + y) * width + origin_x) * 4
                        target = ((y + 1) * 256 + 1) * 4
                        length = min(254, width - origin_x) * 4
                        self.assertEqual(payload[target:target + length], pixels[source:source + length])
                    for x, y in ((0, 0), (0, 128), (128, 0), (255, 0), (0, 255), (255, 255), (255, 128), (128, 255)):
                        source_x = min(width - 1, max(0, origin_x + x - 1))
                        source_y = min(height - 1, max(0, origin_y + y - 1))
                        source = (source_y * width + source_x) * 4
                        target = (y * 256 + x) * 4
                        self.assertEqual(payload[target:target + 4], pixels[source:source + 4])

    def test_page_admission_header_has_only_compact_descriptors_and_existing_legacy_whitelist(self):
        outputs = builder.outputs()
        header = outputs[builder.HEADER_PATH].decode("ascii")
        assets, pages = builder.page_assets()
        self.assertEqual([page["image_name"] for page in pages], ["textures/ps2_movie", "textures/ps2_ascii"])
        self.assertEqual([(page["width"], page["height"], page["page_count"]) for page in pages],
                         [(272, 256, 4), (512, 128, 3)])
        self.assertEqual(len(re.findall(r"static constexpr std::uint8_t kResource\d+\[\]", header)), 3)
        self.assertEqual(len(re.findall(r"static constexpr std::uint32_t kPageChecksums\d+\[\]", header)), 2)
        self.assertLess(len(header), 80000)
        for page in pages:
            self.assertIn('"' + page["path"] + '"', header)
            for checksum in page["checksums"]:
                self.assertIn(f"0x{checksum:08x}", header)
            self.assertEqual(outputs[builder.ASSET_ROOT / "base" / page["path"]], assets[page["path"]])

    def test_authored_atlas_coverage_preserves_native_printable_glyphs_and_font_padding(self):
        width, _, movie = builder.charset_pixels()
        font_width, _, font = builder.authored_pixels(True)
        # Independently dumped from the fixture's MakeConsoleCharset (and its former authored-font
        # variant) with the host C++ compiler.
        self.assertEqual(hashlib.sha256(movie).hexdigest(), "21e86cb9837b1c2621e3f94ccd0a7a50d12e7372f3b2ffa2a790c725ebe37451")
        self.assertEqual(hashlib.sha256(font).hexdigest(), "e0e942fb84833c257ce7a7972c7ba197911ad3dc41f3d7105d4304037a943545")
        for code in range(32, 127):
            source_x, source_y = (code & 15) * 16, (code >> 4) * 16
            target_x, target_y = (code & 15) * 32 + 1, (code >> 4) * 16 + 1
            for y in range(14):
                source = ((source_y + y + 1) * width + source_x) * 4
                target = ((target_y + y) * font_width + target_x) * 4
                self.assertEqual(font[target:target + 15 * 4], movie[source:source + 15 * 4])
            for x, y in ((target_x - 1, target_y), (target_x + 15, target_y),
                          (target_x, target_y - 1), (target_x, target_y + 14)):
                self.assertEqual(font[(y * font_width + x) * 4 + 3], 0)

    def test_native_movie_bitmap_crosses_a_page_seam_without_changing_its_source_pixels(self):
        source_width, source_height, source = builder.charset_pixels()
        width, height, movie = builder.authored_pixels()
        self.assertEqual((source_width, source_height, width, height), (256, 256, 272, 256))
        self.assertLess(246, 254)
        self.assertGreater(246 + 16, 254)
        for y in range(16):
            origin = ((64 + y) * source_width + 16) * 4
            target = ((64 + y) * width + 246) * 4
            self.assertEqual(movie[target:target + 16 * 4], source[origin:origin + 16 * 4])
        _, dictionary = read_swf(builder.build_swf())
        self.assertEqual(dictionary[3]["image_size"], (16, 16))
        self.assertEqual(dictionary[3]["atlas_offset"], (246, 64))
        self.assertEqual(dictionary[4]["fills"][0]["vertices"], [(0, 0), (128, 0), (128, 128), (0, 128)])

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
            # An empty PATH proves header generation/checking needs only Python,
            # including on a machine without clang-format or the PS2 toolchain.
            environment = {**os.environ, "PATH": ""}
            self.assertEqual(subprocess.run(command, capture_output=True, env=environment).returncode, 0)
            for relative in (builder.HEADER_PATH, builder.ASSET_ROOT / "base" / builder.SWF_PATH,
                             builder.ASSET_ROOT / "base" / builder.MOVIE_IMAGE_PATH):
                with self.subTest(path=str(relative)):
                    path = root / relative
                    original = path.read_bytes()
                    path.write_bytes(b"stale")
                    self.assertNotEqual(subprocess.run(command, capture_output=True, env=environment).returncode, 0)
                    self.assertEqual(path.read_bytes(), b"stale")
                    path.write_bytes(original)


if __name__ == "__main__":
    unittest.main()
