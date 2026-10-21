#!/usr/bin/env python3
# ================================================================================================
# File: build_authored_ui.py
# Brief: Author a small native BSWF16 timeline and ASCII font without any retail game assets.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import tempfile

from ui_pages import write_pages
from ui_page_package import HEADER, RECORD, write_page_package

REPO_ROOT = Path(__file__).resolve().parents[3]
ASSET_ROOT = Path("src/tests/data/ui")
HEADER_PATH = Path("src/ps2/ui/authored_ui_data.h")
SWF_PATH = "generated/swf/ps2_demo.bswf"
FONT_PATH = "newfonts/ps2_ascii/48.dat"
MATERIAL_PATH = "materials/ps2_ui.mtr"
MOVIE_IMAGE_PATH = "textures/ps2_movie.uipg"
FONT_IMAGE_PATH = "textures/ps2_ascii.uipg"


def be(format_string, *values):
    return struct.pack(">" + format_string, *values)


def string(value):
    # Native idFile::ReadString uses little endian even inside the big endian BSWF container.
    encoded = value.encode("ascii")
    return struct.pack("<i", len(encoded)) + encoded


class Bits:
    def __init__(self):
        self.values = []

    def put(self, value, count):
        self.values.extend((value >> bit) & 1 for bit in reversed(range(count)))

    def signed_values(self, values, count_bits):
        width = max(1, *(abs(value).bit_length() + 1 for value in values))
        if width >= (1 << count_bits):
            raise ValueError("SWF signed field exceeds its bit count")
        self.put(width, count_bits)
        for value in values:
            self.put(value, width)

    def data(self):
        values = self.values + [0] * (-len(self.values) % 8)
        return bytes(sum(values[index + bit] << (7 - bit) for bit in range(8))
                     for index in range(0, len(values), 8))


def matrix(xx=1, yy=1, xy=0, yx=0, tx=0, ty=0):
    bits = Bits()
    scaled = (round(xx * 65536), round(yy * 65536))
    rotated = (round(yx * 65536), round(xy * 65536))
    bits.put(scaled != (65536, 65536), 1)
    if scaled != (65536, 65536):
        bits.signed_values(scaled, 5)
    bits.put(rotated != (0, 0), 1)
    if rotated != (0, 0):
        bits.signed_values(rotated, 5)
    bits.signed_values((round(tx * 20), round(ty * 20)), 5)
    return bits.data()


def color_transform(multipliers):
    bits = Bits()
    bits.put(0, 1)  # No additive terms: the first profile uses straight color/alpha modulation.
    bits.put(1, 1)
    bits.signed_values(tuple(round(value * 256) for value in multipliers), 4)
    return bits.data()


def place(depth, character=None, transform=None, name=None, color=None, move=False, blend=None, clip_depth=None):
    if blend is not None and blend != 8:
        raise ValueError("authored special blending supports only native additive mode 8")
    if clip_depth is not None and not depth < clip_depth <= 65535:
        raise ValueError("authored mask must end above its placement depth")
    flags = int(move) | (2 if character is not None else 0) | (4 if transform is not None else 0)
    flags |= (8 if color is not None else 0) | (32 if name is not None else 0)
    flags |= 64 if clip_depth is not None else 0
    payload = bytes((flags,)) + (b"\x02" if blend is not None else b"") + struct.pack("<H", depth)
    if character is not None:
        payload += struct.pack("<H", character)
    if transform is not None:
        payload += transform
    if color is not None:
        payload += color_transform(color)
    if name is not None:
        payload += name.encode("ascii") + b"\0"
    if clip_depth is not None:
        payload += struct.pack("<H", clip_depth)
    if blend is not None:
        payload += bytes((blend,))
    return (70 if blend is not None else 26), payload  # No clip actions or filters.


def action(opcode, payload=b""):
    return bytes((opcode,)) + (struct.pack("<H", len(payload)) if opcode >= 0x80 else b"") + payload


def push(*values):
    payload = b""
    for value in values:
        if isinstance(value, str):
            payload += b"\0" + value.encode("ascii") + b"\0"
        elif isinstance(value, int):
            payload += b"\7" + struct.pack("<i", value)
        else:
            raise ValueError("authored AVM1 Push supports only ASCII strings and integers")
    return action(0x96, payload)


def get_variable(name):
    return push(name) + action(0x1C)


def root_member(name):
    return get_variable("_root") + push(name) + action(0x4E)


def set_root(name, value):
    return get_variable("_root") + push(name) + value + action(0x4F)


def set_child(child, name, value):
    return root_member(child) + push(name) + value + action(0x4F)


def function(name, body):
    body += action(0)
    # AVM1 DefineFunction carries the function body after its parameter/code-size header.
    payload = name.encode("ascii") + b"\0" + struct.pack("<HH", 0, len(body)) + body
    return action(0x9B, payload)


def main_actions():
    focus = set_root("focusIndex", push(1) + root_member("focusIndex") + action(0x0B))
    focus += set_child("focusFrame", "_y", root_member("focusIndex") + push(48) + action(0x0C)
                       + push(312) + action(0x0A))
    focus += set_child("status", "text", push("FOCUS ") + root_member("focusIndex") + action(0x47))
    selected = set_root("selectCount", root_member("selectCount") + action(0x50))
    selected += set_root("selected", root_member("focusIndex"))
    selected += set_child("status", "text", push("SELECTED ") + root_member("selected") + action(0x47))
    output = set_root("focusIndex", push(0)) + set_root("selectCount", push(0)) + set_root("selected", push(-1))
    output += function("focusNext", focus) + function("select", selected)
    return 12, output + action(0)


def sprite(frames):
    commands = [command for frame in frames for command in frame]
    offsets = [0]
    for frame in frames:
        offsets.append(offsets[-1] + len(frame))
    blob = b"".join(payload for _, payload in commands)
    output = be("Hi", len(frames), len(offsets)) + be("I" * len(offsets), *offsets)
    output += be("i", 0)  # No labels.
    output += be("I", len(blob)) + blob + be("i", len(commands))
    output += b"".join(be("iI", tag, len(payload)) for tag, payload in commands)
    output += be("i", 0)  # No DoInitAction commands.
    return output


def shape(width, height, color=(255, 255, 255, 255), bitmap=None, points=None):
    bounds = (0, 0, width, height)
    output = be("i", 2) + be("8f", *(bounds + bounds)) + be("i", 1)
    output += bytes((4 if bitmap is not None else 0, 3)) + bytes(color) * 2
    # SWF bitmap matrices act on twips and are inverted by native RenderShape.
    scale = 20 * width / 16 if bitmap is not None else 1
    output += be("6f", scale, scale, 0, 0, 0, 0) * 2
    output += bytes((0,)) + be("fH", 0, 65535 if bitmap is None else bitmap)
    vertices = points if points is not None else (0, 0, width, 0, width, height, 0, height)
    if len(vertices) != 8:
        raise ValueError("authored shapes require four convex vertices")
    output += be("i8f", 4, *vertices) + be("i", 0)
    output += be("i6H", 6, 0, 1, 2, 0, 2, 3) + be("i", 0)
    return output


def edit_text(text, width=510, height=28, font_height=960, color=(240, 248, 255, 255)):
    output = be("i4fIHH", 7, 0, 0, width, height, 8, 9, font_height) + bytes(color)
    output += be("HiHHHh", 96, 0, 0, 0, 0, 0)
    return output + string("") + string(text)


def build_swf():
    dictionary = [be("i", 0), shape(640, 480, (16, 24, 40, 255)),
                  shape(576, 432, (32, 48, 64, 255))]
    # Native BSWF16 interleaves imageSize[j], imageAtlasOffset[j] for x then y.
    dictionary.append(be("i", 1) + string(".") + be("4i4f", 16, 246, 16, 64, 1, 1, 1, 1))
    dictionary.append(shape(128, 128, bitmap=3))
    dictionary.append(shape(220, 42))
    colors = ((0.1, 0.85, 1, 1), (0.8, 0.25, 1, 0.4), (1, 0.5, 0.1, 0.7), (0.2, 1, 0.3, 1))
    pulse_frames = [[place(1, 5, matrix(), color=colors[0])]]
    pulse_frames.extend([place(1, color=color, move=True)] for color in colors[1:])
    dictionary.append(be("i", 4) + sprite(pulse_frames))
    dictionary.append(shape(220, 36, (24, 192, 224, 110)))
    dictionary.append(be("i", 4) + sprite([[place(1, 7, matrix())]]))
    dictionary.append(be("i", 5) + string("ps2_ascii") + be("3hi", 14, 0, 0, 0))
    dictionary.extend((edit_text("DOOM 3 BFG / PS2", font_height=1200),
                       edit_text("AUTHORED NATIVE SWF"),
                       edit_text("TAB TO FOCUS / ENTER TO SELECT"),
                       edit_text("CONTINUE"), edit_text("OPTIONS"),
                       edit_text("TIMELINE COLOR + ALPHA")))
    # One affine trapezoid and two child timelines exercise geometric masks without a new fixture.
    dictionary.append(shape(320, 48, points=(0, 0, 320, 0, 290, 48, 30, 48)))
    lifecycle = [
        place(1, 5, matrix(xx=140 / 220, yy=48 / 42), clip_depth=5),
        place(2, 5, matrix(xx=140 / 220, yy=48 / 42, tx=80), clip_depth=7),
        place(3, 5, matrix(xx=260 / 220, yy=8 / 42, ty=4), color=(1, .25, .125, 1)),
        place(4, 5, matrix(xx=260 / 220, yy=8 / 42, ty=14), color=(.125, 1, .25, .5)),
        place(5, 5, matrix(xx=260 / 220, yy=8 / 42, ty=24), color=(.5, .25, .125, .5), blend=8),
        place(6, 5, matrix(xx=260 / 220, yy=6 / 42, ty=34), color=(.25, 1, .25, 1)),
        place(8, 5, matrix(xx=8 / 220, yy=8 / 42, tx=244, ty=4)),
        place(9, 5, matrix(xx=20 / 220, yy=48 / 42, tx=240), clip_depth=11),
        place(10, 5, matrix(xx=20 / 220, yy=48 / 42, tx=250), clip_depth=11),
        place(11, 5, matrix(xx=50 / 220, yy=8 / 42, tx=230, ty=16), color=(.75, .125, .75, 1)),
        place(12, 5, matrix(xx=42 / 220, yy=8 / 42, tx=238, ty=28)),
    ]
    dictionary.append(be("i", 4) + sprite([lifecycle]))
    page_and_text = [
        place(1, 5, matrix(xx=96 / 220, yy=88 / 42), color=(.25, .25, .25, 1)),
        place(2, 5, matrix(xx=42 / 220, yy=58 / 42, tx=8, ty=2), clip_depth=3),
        place(3, 4, matrix(xx=.5, yy=.5)),
        place(4, 5, matrix(xx=44 / 220, yy=20 / 42, tx=10, ty=66), clip_depth=5),
        place(5, 19, matrix(ty=68)),
        place(6, 5, matrix(xx=8 / 220, yy=8 / 42, tx=80, ty=72), color=(1, .5, 0, 1)),
    ]
    dictionary.append(be("i", 4) + sprite([page_and_text]))
    dictionary.append(edit_text("A A A"))
    main = [place(1, 1, matrix()), place(2, 2, matrix(tx=32, ty=24)),
            place(3, 4, matrix(xx=.9, yy=.9, xy=-.2, yx=.2, tx=452, ty=136)),
            place(4, 6, matrix(tx=64, ty=216), "pulse"),
            place(5, 8, matrix(tx=64, ty=312), "focusFrame")]
    for depth, character, x, y, name in ((6, 10, 64, 48, "title"), (7, 11, 64, 88, "subtitle"),
                                        (8, 12, 64, 408, "status"), (9, 13, 76, 320, "continueLabel"),
                                        (10, 14, 76, 368, "optionsLabel"), (11, 15, 64, 176, "caption")):
        main.append(place(depth, character, matrix(tx=x, ty=y), name))
    # Exercise native RenderShape's mode 8, including alpha-zero bitmap holes and the page seam.
    for depth, y, color in ((12, 304, (.5, .25, .125, 0)), (13, 332, (.5, .25, .125, .5)),
                             (14, 360, (.5, .25, .125, 1)), (15, 388, (1, 1, 1, 1))):
        height = 16 if depth == 15 else 24
        main.append(place(depth, 5, matrix(xx=96 / 220, yy=height / 42, tx=336, ty=y), color=color, blend=8))
    main.append(place(16, 4, matrix(xx=.75, yy=.75, tx=480, ty=304), color=(.5, .25, .125, .5), blend=8))
    main.append(place(17, 5, matrix(xx=32 / 220, yy=16 / 42, tx=368, ty=340),
                      color=(.5, .25, .125, .5), blend=8))
    main.extend((place(18, 16, matrix(tx=64, ty=120), clip_depth=20),
                 place(19, 5, matrix(xx=320 / 220, yy=48 / 42, tx=64, ty=120), color=(0, 0, .5, 1)),
                 place(20, 17, matrix(tx=64, ty=120)),
                 place(21, 5, matrix(xx=8 / 220, yy=8 / 42, tx=52, ty=148)),
                 place(22, 16, matrix(xx=.3, yy=88 / 48, tx=300, ty=204), clip_depth=23),
                 place(23, 18, matrix(tx=300, ty=204)),
                 place(24, 5, matrix(xx=8 / 220, yy=8 / 42, tx=288, ty=280))))
    main.append(main_actions())
    header = be("IqffH", 0x42535710, -1, 640, 480, 6 * 256)
    return header + sprite([main]) + be("i", len(dictionary)) + b"".join(dictionary)


def build_font():
    output = be("Ihhhh", 0x6964662A, 48, 14, 0, 95)
    for code in range(32, 127):
        output += struct.pack("<BBbbBxHH", 15, 14, 14, 0, 16, (code & 15) * 32 + 1, (code >> 4) * 16 + 1)
    return output + b"".join(struct.pack("<I", code) for code in range(32, 127))


def build_materials():
    materials = []
    for name, image in (("_white", "_white"), ("guiSolid", "_white"),
                        ("generated/swf/ps2_demo.tga", "textures/ps2_movie"),
                        ("newfonts/ps2_ascii/48.tga", "textures/ps2_ascii")):
        materials.append(f"{name}\n{{\n    {{\n        blend blend\n        map {image}\n"
                         "        nearest\n        clamp\n        vertexColor\n    }\n}\n")
    return ("// This source code is released under the GNU GPL-3.0-or-later license.\n"
            "// Authored native SWF materials use backend-owned white and cooked movie/font pages.\n\n"
            + "\n".join(materials)).encode("ascii")


def charset_rows():
    """Keep the host's authored glyph coverage tied to the native canonical bit patterns."""
    source = (REPO_ROOT / "src/ps2/ui/charset.cpp").read_text()
    ascii_match = re.search(r"kAscii\[95\]\[7\]\s*=\s*\{(.*?)\n\};", source, re.S)
    box_match = re.search(r"kBox\[7\]\s*=\s*\{([^}]+)\}", source)
    if ascii_match is None or box_match is None:
        raise ValueError("native authored glyph rows are missing")
    rows = [tuple(int(value) for value in re.findall(r"\d+", row))
            for row in re.findall(r"\{([^}]+)\}", ascii_match[1])]
    box = tuple(int(value) for value in re.findall(r"\d+", box_match[1]))
    if len(rows) != 95 or any(len(row) != 7 or any(not 0 <= value <= 31 for value in row)
                              for row in [*rows, box]):
        raise ValueError("native authored glyph rows exceed the 95 by 5x7 profile")
    return rows, box


def charset_pixels(font=False):
    rows, box = charset_rows()

    def covered(code, x, y):
        if code == 10:
            return 2 <= x < 14 and 13 <= y < 15
        if code == 11:
            return 2 <= x < 14 and 1 <= y < 15
        if x >= 15 or not 1 <= y < 15:
            return False
        glyph = rows[code - 32] if 32 <= code <= 126 else box
        return bool(glyph[(y - 1) // 2] & (1 << (4 - x // 3)))

    width, height = (512, 128) if font else (256, 256)
    pixels = bytearray(bytes((255, 255, 255, 0)) * (width * height))
    for code in range(32, 127) if font else range(256):
        origin_x = (code & 15) * (32 if font else 16) + int(font)
        origin_y = (code >> 4) * 16 + int(font)
        for y in range(14 if font else 16):
            for x in range(15 if font else 16):
                offset = ((origin_y + y) * width + origin_x + x) * 4
                pixels[offset + 3] = 255 if covered(code, x, y + int(font)) else 0
    return width, height, bytes(pixels)


def authored_pixels(font=False):
    width, height, source = charset_pixels(font)
    if font:
        return width, height, source
    # Native RenderShape now samples an identical A across the x=254 page split.
    # Keep its displayed 16x16 image/transform unchanged while exercising two pages.
    width = 272
    pixels = bytearray(bytes((255, 255, 255, 0)) * (width * height))
    for y in range(height):
        pixels[y * width * 4:(y * width + 256) * 4] = source[y * 256 * 4:(y + 1) * 256 * 4]
    for y in range(16):
        origin = ((64 + y) * 256 + 16) * 4
        target = ((64 + y) * width + 246) * 4
        pixels[target:target + 16 * 4] = source[origin:origin + 16 * 4]
    return width, height, bytes(pixels)


def page_assets():
    """Use the same lossless page writers as the retail cook without embedding their pixels."""
    assets, descriptors = {}, []
    with tempfile.TemporaryDirectory(prefix="d3bfg-authored-ui-") as temporary:
        directory = Path(temporary)
        for path, image_name, font in ((MOVIE_IMAGE_PATH, "textures/ps2_movie", False),
                                       (FONT_IMAGE_PATH, "textures/ps2_ascii", True)):
            width, height, pixels = authored_pixels(font)
            layout = write_pages(width, height, pixels, directory)
            package = write_page_package(layout, directory)
            data = (directory / package["path"]).read_bytes()
            checksums = [RECORD.unpack_from(data, HEADER.size + index * RECORD.size)[1]
                         for index in range(package["page_count"])]
            assets[path] = data
            descriptors.append({"image_name": image_name, "path": path, "width": width, "height": height,
                                "file_bytes": len(data), "page_count": len(checksums), "checksums": checksums,
                                "sha256": hashlib.sha256(data).hexdigest(),
                                "source_sha256": hashlib.sha256(pixels).hexdigest()})
    return assets, descriptors


def fnv1a64(data):
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"


def header(assets, pages=()):
    text = ("// ================================================================================================\n"
            "// File: authored_ui_data.h\n"
            "// Brief: Exact authored native UI resources admitted before unchecked legacy parsing.\n"
            "// This source code is released under the GNU GPL-3.0-or-later license.\n"
            "// Generated by src/tools/scripts/build_authored_ui.py; edit the generator.\n"
            "// ================================================================================================\n\n"
            "#pragma once\n#include <cstddef>\n#include <cstdint>\n\nnamespace ps2::ui::authored\n{\n")
    entries = []
    for index, (path, data) in enumerate(assets.items()):
        name = f"kResource{index}"
        text += f"static constexpr std::uint8_t {name}[] = {{\n"
        # One byte per line is already a fixed point of the repository formatter;
        # generation and --check do not depend on an installed C++ formatter.
        for byte in data:
            text += f"    0x{byte:02x},\n"
        text += "};\n\n"
        entries.append(f'    {{ "{path}", {name}, sizeof({name}) }},')
    text += ("struct Resource\n{\n    const char * path;\n    const std::uint8_t * bytes;\n"
             "    std::size_t length;\n};\n\nstatic constexpr Resource kResources[] = {\n"
             + "\n".join(entries) + "\n};\n")
    if pages:
        for index, page in enumerate(pages):
            text += f"\nstatic constexpr std::uint32_t kPageChecksums{index}[] = {{\n"
            text += "".join(f"    0x{checksum:08x},\n" for checksum in page["checksums"])
            text += "};\n"
        text += ("\nstruct PageResource\n{\n    const char * imageName;\n    const char * path;\n"
                 "    std::uint32_t width;\n    std::uint32_t height;\n    std::uint32_t fileBytes;\n"
                 "    const std::uint32_t * checksums;\n    std::size_t pageCount;\n};\n\n"
                 "static constexpr PageResource kPageResources[] = {\n")
        for index, page in enumerate(pages):
            text += (f'    {{ "{page["image_name"]}", "{page["path"]}", {page["width"]}, {page["height"]}, '
                     f'{page["file_bytes"]}, kPageChecksums{index}, {page["page_count"]} }},\n')
        text += "};\n"
    text += "} // namespace ps2::ui::authored\n"
    return text.encode("ascii")


def outputs():
    assets = {SWF_PATH: build_swf(), FONT_PATH: build_font(), MATERIAL_PATH: build_materials()}
    image_assets, pages = page_assets()
    manifest = {
        "generator": "src/tools/scripts/build_authored_ui.py",
        "license": "GPL-3.0-or-later",
        "profile": "BSWF16 native convex-mask authored UI; no retail data",
        "blend_modes": [0, 8],
        "frame": {"width": 640, "height": 480, "rate": 6, "main_frames": 1, "pulse_frames": 4},
        "dictionary": ["null", "background shape", "panel shape", "ASCII A bitmap", "transformed bitmap shape",
                       "pulse shape", "pulse timeline sprite", "focus shape", "focus sprite", "ps2_ascii font",
                       "title edittext", "subtitle edittext", "status edittext", "continue edittext", "options edittext",
                       "caption edittext", "convex trapezoid mask", "mask lifecycle sprite",
                       "masked bitmap and text sprite", "masked A edittext"],
        "masks": {"placements": 8, "max_active": 3, "max_vertices": 4, "max_triangles": 2,
                  "profiles": ["affine convex", "nested", "crossing clip ranges", "same-depth expiry"]},
        "script": {"functions": ["focusNext", "select"], "variables": ["focusIndex", "selectCount", "selected"],
                   "actions": ["End", "Add", "Subtract", "Multiply", "GetVariable", "Push", "DefineFunction",
                               "Add2", "GetMember", "SetMember", "Increment"]},
        "resources": {path: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), "fnv1a64": fnv1a64(data)}
                      for path, data in assets.items()},
        "page_resources": pages,
    }
    result = {ASSET_ROOT / "base" / path: data for path, data in assets.items()}
    result.update({ASSET_ROOT / "base" / path: data for path, data in image_assets.items()})
    result[ASSET_ROOT / "manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode("ascii")
    result[HEADER_PATH] = header(assets, pages)
    return result


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
        parser.exit(1, "Authored UI outputs are stale: " + ", ".join(changed) + "\n")
    print("Authored UI outputs verified." if args.check else "Authored UI outputs generated.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
