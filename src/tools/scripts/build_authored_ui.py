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
import struct

REPO_ROOT = Path(__file__).resolve().parents[3]
ASSET_ROOT = Path("assets/ui")
HEADER_PATH = Path("src/ps2/ui/authored_ui_data.h")
SWF_PATH = "generated/swf/ps2_demo.bswf"
FONT_PATH = "newfonts/ps2_ascii/48.dat"
MATERIAL_PATH = "materials/ps2_ui.mtr"


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


def place(depth, character=None, transform=None, name=None, color=None, move=False):
    flags = int(move) | (2 if character is not None else 0) | (4 if transform is not None else 0)
    flags |= (8 if color is not None else 0) | (32 if name is not None else 0)
    payload = bytes((flags,)) + struct.pack("<H", depth)
    if character is not None:
        payload += struct.pack("<H", character)
    if transform is not None:
        payload += transform
    if color is not None:
        payload += color_transform(color)
    if name is not None:
        payload += name.encode("ascii") + b"\0"
    return 26, payload  # PlaceObject2, with no masks, clip actions, filters or special blends.


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


def shape(width, height, color=(255, 255, 255, 255), bitmap=None):
    bounds = (0, 0, width, height)
    output = be("i", 2) + be("8f", *(bounds + bounds)) + be("i", 1)
    output += bytes((4 if bitmap is not None else 0, 3)) + bytes(color) * 2
    # SWF bitmap matrices act on twips and are inverted by native RenderShape.
    scale = 20 * width / 16 if bitmap is not None else 1
    output += be("6f", scale, scale, 0, 0, 0, 0) * 2
    output += bytes((0,)) + be("fH", 0, 65535 if bitmap is None else bitmap)
    vertices = (0, 0, width, 0, width, height, 0, height)
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
    dictionary.append(be("i", 1) + string(".") + be("4i4f", 16, 16, 16, 64, 1, 1, 1, 1))
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
    main = [place(1, 1, matrix()), place(2, 2, matrix(tx=32, ty=24)),
            place(3, 4, matrix(xx=.9, yy=.9, xy=-.2, yx=.2, tx=452, ty=136)),
            place(4, 6, matrix(tx=64, ty=216), "pulse"),
            place(5, 8, matrix(tx=64, ty=312), "focusFrame")]
    for depth, character, x, y, name in ((6, 10, 64, 48, "title"), (7, 11, 64, 88, "subtitle"),
                                        (8, 12, 64, 408, "status"), (9, 13, 76, 320, "continueLabel"),
                                        (10, 14, 76, 368, "optionsLabel"), (11, 15, 64, 176, "caption")):
        main.append(place(depth, character, matrix(tx=x, ty=y), name))
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
                        ("generated/swf/ps2_demo.tga", "textures/bigchars"),
                        ("newfonts/ps2_ascii/48.tga", "textures/ps2_ascii")):
        materials.append(f"{name}\n{{\n    {{\n        blend blend\n        map {image}\n"
                         "        nearest\n        clamp\n        vertexColor\n    }\n}\n")
    return ("// This source code is released under the GNU GPL-3.0-or-later license.\n"
            "// Authored native SWF materials use backend-owned white, console and guttered font images.\n\n"
            + "\n".join(materials)).encode("ascii")


def fnv1a64(data):
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"


def header(assets):
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
             + "\n".join(entries) + "\n};\n} // namespace ps2::ui::authored\n")
    return text.encode("ascii")


def outputs():
    assets = {SWF_PATH: build_swf(), FONT_PATH: build_font(), MATERIAL_PATH: build_materials()}
    manifest = {
        "generator": "src/tools/scripts/build_authored_ui.py",
        "license": "GPL-3.0-or-later",
        "profile": "BSWF16 native mask-free authored UI; no retail data",
        "frame": {"width": 640, "height": 480, "rate": 6, "main_frames": 1, "pulse_frames": 4},
        "dictionary": ["null", "background shape", "panel shape", "ASCII A bitmap", "transformed bitmap shape",
                       "pulse shape", "pulse timeline sprite", "focus shape", "focus sprite", "ps2_ascii font",
                       "title edittext", "subtitle edittext", "status edittext", "continue edittext", "options edittext",
                       "caption edittext"],
        "script": {"functions": ["focusNext", "select"], "variables": ["focusIndex", "selectCount", "selected"],
                   "actions": ["End", "Add", "Subtract", "Multiply", "GetVariable", "Push", "DefineFunction",
                               "Add2", "GetMember", "SetMember", "Increment"]},
        "resources": {path: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), "fnv1a64": fnv1a64(data)}
                      for path, data in assets.items()},
    }
    result = {ASSET_ROOT / "base" / path: data for path, data in assets.items()}
    result[ASSET_ROOT / "manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode("ascii")
    result[HEADER_PATH] = header(assets)
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
