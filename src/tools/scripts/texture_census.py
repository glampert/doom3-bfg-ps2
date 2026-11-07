#!/usr/bin/env python3
# ================================================================================================
# File: texture_census.py
# Brief: Count the retail BIMAGE textures by usage, format and colour format, so the GSTX v2 cook
# knows which decoders it needs and how much texel data each class carries.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""Retail texture census.

Reads only each BIMAGE header (ui_bimage.py has the layout) from the containers named on the
command line, in load order; a later container's record of the same name wins, as in the engine.
The texture usage is the first two digits of the generated name's `#__UUCC` suffix
(Image_load.cpp's GetGeneratedName; textureUsage_t in Image.h).

    texture_census.py --data gamedata/d3_bfg [--map mars_city1] [--json out.json]

Without --map the census covers _common, _ordered and every map container; with it, the map's
own container (the images its level load reads).
"""

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

import ui_bimage  # noqa: E402
import ui_resources  # noqa: E402

USAGES = ["specular", "diffuse", "default", "bump", "font", "light", "lut_mono", "lut_alpha",
          "lut_rgb1", "lut_rgba", "coverage", "depth"]
_HEADER = struct.Struct(">q7i")


def usage_of(name: str) -> str:
    marker = name.rfind("#__")
    if marker < 0 or not name[marker + 3:marker + 5].isdigit():
        return "unnamed"

    usage = int(name[marker + 3:marker + 5])
    return USAGES[usage] if usage < len(USAGES) else "usage%d" % usage


def read_header(stream, entry):
    stream.seek(entry.offset)
    data = stream.read(_HEADER.size)
    if len(data) != _HEADER.size:
        raise ValueError("truncated BIMAGE header: " + entry.name)

    _, magic, texture_type, image_format, color_format, width, height, levels = _HEADER.unpack(data)
    if magic != ui_bimage.BIMAGE_MAGIC:
        raise ValueError("not a BIMAGE: " + entry.name)

    return texture_type, image_format, color_format, width, height, levels


def collect(paths):
    selected = {}
    for path in paths:
        container = ui_resources.Container(path)
        with container.path.open("rb") as stream:
            for entry in container.entries:
                if entry.name.endswith(".bimage") and entry.length:
                    selected[entry.name] = read_header(stream, entry) + (entry.length,)

    return selected


def census(selected):
    classes = defaultdict(lambda: {"images": 0, "texels": 0, "stored_bytes": 0, "max_extent": Counter(),
                                   "non_pow2": 0, "mipped": 0})
    for name, (texture_type, image_format, color_format, width, height, levels, length) in selected.items():
        faces = 6 if texture_type == 2 else 1
        key = (usage_of(name), "cube" if faces == 6 else "2d",
               ui_bimage._FORMATS.get(image_format, ("fmt%d" % image_format,))[0],
               ui_bimage._COLOR_FORMATS.get(color_format, "cfm%d" % color_format))
        entry = classes[key]
        entry["images"] += 1
        entry["texels"] += width * height * faces
        entry["stored_bytes"] += length
        entry["max_extent"][max(width, height)] += 1
        entry["non_pow2"] += (width & (width - 1)) != 0 or (height & (height - 1)) != 0
        entry["mipped"] += levels > 1

    return classes


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data", type=Path, required=True, help="retail installation (holds _common.resources)")
    parser.add_argument("--map", help="one map's container, e.g. mars_city1")
    parser.add_argument("--json", type=Path, help="also write the census as JSON")
    args = parser.parse_args()

    if args.map:
        paths = [args.data / "maps" / (args.map + ".resources")]
    else:
        paths = [args.data / "_common.resources", args.data / "_ordered.resources"]
        paths += sorted((args.data / "maps").glob("*.resources"))

    selected = collect(paths)
    classes = census(selected)
    rows = sorted(classes.items(), key=lambda item: -item[1]["texels"])
    total_texels = sum(entry["texels"] for _, entry in rows)

    print("%d images, %.1f Mtexels, %.1f MiB stored, from %d containers" % (
        len(selected), total_texels / 1e6, sum(entry["stored_bytes"] for _, entry in rows) / 2**20, len(paths)))
    print("%-10s %-4s %-11s %-16s %6s %8s %6s %9s %5s  %s" % (
        "usage", "type", "format", "colorFormat", "images", "Mtexels", "share", "MiB", "npot", "max extent: images"))
    for (usage, kind, image_format, color_format), entry in rows:
        extents = " ".join("%d:%d" % pair for pair in sorted(entry["max_extent"].items()))
        print("%-10s %-4s %-11s %-16s %6d %8.2f %5.1f%% %9.2f %5d  %s" % (
            usage, kind, image_format, color_format, entry["images"], entry["texels"] / 1e6,
            100.0 * entry["texels"] / max(1, total_texels), entry["stored_bytes"] / 2**20, entry["non_pow2"], extents))

    if args.json:
        args.json.write_text(json.dumps({
            "containers": [path.name for path in paths],
            "images": len(selected),
            "classes": [dict(usage=usage, type=kind, format=image_format, color_format=color_format,
                             images=entry["images"], texels=entry["texels"], stored_bytes=entry["stored_bytes"],
                             non_pow2=entry["non_pow2"], mipped=entry["mipped"],
                             max_extent={str(k): v for k, v in sorted(entry["max_extent"].items())})
                        for (usage, kind, image_format, color_format), entry in rows],
        }, indent=2) + "\n")


if __name__ == "__main__":
    main()
