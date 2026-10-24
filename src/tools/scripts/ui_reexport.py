#!/usr/bin/env python3
# ================================================================================================
# File: ui_reexport.py
# Brief: Describe the retail UI textures as an asset_reexport job (SWF atlas images, English font
# glyphs, whole images) and run the C++ converter, validating every GSTX file it returns.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""The cook owns inspection and policy; src/tools/asset_reexport owns pixel conversion.

Placements tell the converter which logical texels each image owns. A SWF atlas image owns its
content rectangle and maps its 4-aligned allocation block (content plus the clamped border id's
packer adds). A font glyph owns its rectangle and maps it plus a one-texel ring, so bilinear
filtering at the glyph edge reads the same neighbors as the original atlas. Only the glyphs
English text can reach are kept; drawing any other glyph fails on the target.
"""

import json
from pathlib import Path
import re
import subprocess

from ui_gstx import Texture
from ui_resources import logical_name

REPO_ROOT = Path(__file__).resolve().parents[3]
TOOL = REPO_ROOT / "build/tools/asset_reexport"
# Bounds as asset_reexport applies them; integer bounds admit only integers.
POLICY_FIELDS = {
    "scale": (0.01, 1.0), "max_page_extent": (128, 512), "t4_max_rmse": (0.0, 255.0), "t4_max_error": (0, 255),
    "share_palettes": None, "group8_max_rmse": (0.0, 255.0), "group8_max_increase": (0.0, 255.0),
    "group4_max_rmse": (0.0, 255.0), "group4_max_increase": (0.0, 255.0), "group_max_error": (0, 255),
    "alpha_levels": (2, 16),
}
_OUTPUT = re.compile(r"[a-z0-9_.-]{1,128}\Z")
# Printable ASCII stays available for runtime text (numbers, names, console) beyond the dictionary.
ASCII_PRINTABLE = set(range(32, 127))
MAX_TOOL_SECONDS = 600


def validate_policy(policy: dict) -> dict:
    """The converter policy from the UI profile; every field is required and bounded."""
    if not isinstance(policy, dict) or set(policy) != set(POLICY_FIELDS):
        raise ValueError("retail UI conversion policy must name exactly: " + ", ".join(sorted(POLICY_FIELDS)))
    for key, bounds in POLICY_FIELDS.items():
        value = policy[key]
        if bounds is None:
            if type(value) is not bool:
                raise ValueError("retail UI conversion policy field must be a boolean: " + key)
        elif (type(value) not in ((int,) if type(bounds[0]) is int else (int, float)) or
              not bounds[0] <= value <= bounds[1]):
            raise ValueError("retail UI conversion policy field out of range: " + key)
    if policy["max_page_extent"] % 128:
        raise ValueError("retail UI conversion page extent must be a multiple of 128")
    return dict(policy)


def output_name(resource: str) -> str:
    """A plain, unique output file name for a converted image resource."""
    stem = resource.split("#", 1)[0]
    stem = stem[len("generated/images/"):] if stem.startswith("generated/images/") else stem
    name = re.sub(r"[^a-z0-9_.-]", "_", stem.lower().replace("/", "_")) + ".gstx"
    if not _OUTPUT.fullmatch(name):
        raise ValueError("retail UI conversion output name is not portable: " + resource)
    return name


def _allocation(size: int) -> int:
    # SWF_Image.cpp: DXT blocks of four texels with at least two texels of clamped border.
    blocks = (size + 3) >> 2
    if (blocks << 2) - size < 2:
        blocks += 1
    return blocks << 2


def swf_placements(swf: dict) -> list:
    placements = []
    for entry in swf["dictionary"]:
        if entry["type"] != "image" or not entry["atlas"]:
            continue
        (width, height), (x, y) = entry["size"], entry["atlas_offset"]
        if x < 1 or y < 1:
            raise ValueError("retail UI atlas image lacks its packing border")
        placements.append({"match": [x, y, width, height], "source": [x - 1, y - 1, _allocation(width), _allocation(height)]})
    return placements


def font_placements(font: dict, codepoints: set, width: int, height: int) -> list:
    """The glyphs native lookup selects for the given codepoints, each with a one-texel ring.

    Native text draws a quad for empty glyphs (spaces) too, half a texel past the glyph on every side,
    so its triangles' centroids fall a sixth of a texel either side: an empty axis matches the ring."""
    rectangles = []
    for glyph in font["glyphs"]:
        if glyph["codepoint"] not in codepoints or not glyph.get("selected_for_codepoint", True):
            continue
        rectangle = (glyph["s"], glyph["t"], glyph["width"], glyph["height"])
        if rectangle[0] + rectangle[2] > width or rectangle[1] + rectangle[3] > height:
            raise ValueError("retail UI font glyph outside its atlas")
        if rectangle not in rectangles:
            rectangles.append(rectangle)
    placements = []
    for x, y, w, h in sorted(rectangles, key=lambda value: (value[1], value[0])):
        left, top = max(0, x - 1), max(0, y - 1)
        right, bottom = min(width, x + w + 1), min(height, y + h + 1)
        match = [left if w == 0 else x, top if h == 0 else y, right - left if w == 0 else w, bottom - top if h == 0 else h]
        placements.append({"match": match, "source": [left, top, right - left, bottom - top]})
    return placements


def english_codepoints(manifest_language: dict, swfs: dict) -> set:
    literals = set()
    for swf in swfs.values():
        literals.update(swf["text"]["literal_codepoints"])
    return (set(manifest_language["codepoints"]) | literals | ASCII_PRINTABLE) - {10, 13}


MAX_TEXTURE_POLICIES = 64


def validate_texture_policies(policies) -> dict:
    """Per-image policy overrides, keyed by the image or material the profile requests (the
    console charset keeps its full resolution). Each value names a subset of the policy fields."""
    if not isinstance(policies, dict) or len(policies) > MAX_TEXTURE_POLICIES:
        raise ValueError("retail UI texture_policies must map at most 64 images to policy overrides")
    validated = {}
    for name, override in policies.items():
        if not isinstance(name, str) or not name or len(name) > 512:
            raise ValueError("retail UI texture policy name is invalid")
        if not isinstance(override, dict) or not override or set(override) - set(POLICY_FIELDS):
            raise ValueError("retail UI texture policy must override known policy fields: " + name)
        complete = {**{key: (bounds[0] if bounds is not None else True) for key, bounds in POLICY_FIELDS.items()}, **override}
        validate_policy(complete)
        validated[logical_name(name)] = dict(override)
    return validated


MAX_GLYPH_SETS = 8
MAX_GLYPH_SIZES = 32


def validate_glyph_sets(glyph_sets) -> list:
    """Fonts to pre-render at exact on-screen sizes: GS pixels per atlas texel and stroke weight."""
    if not isinstance(glyph_sets, list) or len(glyph_sets) > MAX_GLYPH_SETS:
        raise ValueError("retail UI glyph_sets must be a list of at most 8 fonts")
    fonts = set()
    for entry in glyph_sets:
        if not isinstance(entry, dict) or set(entry) != {"font", "sizes"} or not isinstance(entry["font"], str):
            raise ValueError("retail UI glyph set needs exactly a font and its sizes")
        font = logical_name(entry["font"])
        if not font.startswith("newfonts/") or not font.endswith("/48.dat") or font in fonts:
            raise ValueError("retail UI glyph set font must be a distinct newfonts/<family>/48.dat")
        fonts.add(font)
        sizes = entry["sizes"]
        if not isinstance(sizes, list) or not 1 <= len(sizes) <= MAX_GLYPH_SIZES:
            raise ValueError("retail UI glyph set needs 1-32 sizes")
        for size in sizes:
            scale, weight = size.get("scale") if isinstance(size, dict) else None, size.get("stroke_weight") if isinstance(size, dict) else None
            if (set(size) != {"scale", "stroke_weight"} or not isinstance(scale, list) or len(scale) != 2 or
                    any(isinstance(value, bool) or not isinstance(value, (int, float)) or not 0 < value <= 4 for value in scale) or
                    isinstance(weight, bool) or not isinstance(weight, (int, float)) or not 0 <= weight <= 16):
                raise ValueError("retail UI glyph size needs a scale pair in (0, 4] and a stroke weight in [0, 16]")
    return glyph_sets


def glyph_set_texture(font: str, image: str, font_details: dict, codepoints: set, sizes: list) -> dict:
    """The converter job for one font's pre-rendered glyphs; its output name stays distinct from the atlas."""
    glyphs = {glyph["codepoint"]: glyph for glyph in font_details["glyphs"] if glyph.get("selected_for_codepoint", True)}
    metrics = [{key: glyphs[codepoint][key] for key in ("codepoint", "s", "t", "width", "height", "left", "top")}
               for codepoint in sorted(codepoints) if codepoint in glyphs]
    return {"name": font + "#glyphs", "source": image, "output": output_name(font[:-len("/48.dat")] + "/ps2_glyphs"),
            "glyphs": {"glyphs": metrics, "sizes": [{"scale": list(size["scale"]), "stroke_weight": size["stroke_weight"]} for size in sizes]}}


def glyph_set_record(image_name: str, sizes: list, report_entry: dict) -> dict:
    """Catalog input for one glyph set: the configured sizes with the converter's rectangles and origins."""
    reported = report_entry.get("glyph_set")
    if not isinstance(reported, list) or len(reported) != len(sizes):
        raise ValueError("asset_reexport glyph set report differs from the job")
    return {"image": image_name,
            "sizes": [{"scale": list(size["scale"]), "stroke_weight": size["stroke_weight"], "glyphs": entry["glyphs"]}
                      for size, entry in zip(sizes, reported)]}


def build_job(policy: dict, textures: list) -> dict:
    outputs = [texture["output"] for texture in textures]
    if len(set(outputs)) != len(outputs):
        raise ValueError("retail UI conversion outputs collide")
    return {"version": 1, "containers": ["_common.resources", "_ordered.resources"], "policy": validate_policy(policy),
            "textures": textures}


def convert(job: dict, data_root: Path, work: Path, previews: Path | None = None, tool: Path = TOOL) -> dict:
    """Run the converter in a private directory; return {output: (bytes, Texture)} plus the report."""
    tool = Path(tool)
    if not tool.is_file():
        raise ValueError("asset_reexport is not built; run `make tools` first")
    work.mkdir(parents=True, exist_ok=False)
    job_path = work / "job.json"
    job_path.write_text(json.dumps(job, indent=1, sort_keys=True) + "\n")
    output = work / "output"
    command = [str(tool), "ui", "--data", str(data_root), "--job", str(job_path), "--output", str(output)]
    if previews is not None:
        command += ["--previews", str(previews)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=MAX_TOOL_SECONDS)
    if result.returncode != 0:
        raise ValueError("asset_reexport failed: " + (result.stderr.strip() or result.stdout.strip()))
    report = json.loads((output / "report.json").read_text())
    files = {}
    for texture in job["textures"]:
        data = (output / texture["output"]).read_bytes()
        parsed = Texture(data)
        entries = [entry for entry in report["textures"] if entry["output"] == texture["output"]]
        if len(entries) != 1 or entries[0]["file_bytes"] != len(data) or list(entries[0]["logical"]) != [parsed.width, parsed.height]:
            raise ValueError("asset_reexport report differs from its output: " + texture["output"])
        if texture.get("width") not in (None, parsed.width) or texture.get("height") not in (None, parsed.height):
            raise ValueError("asset_reexport output dimensions differ from the source: " + texture["output"])
        files[texture["output"]] = (data, parsed)
    return {"files": files, "report": report, "job": job_path.read_bytes(), "log": result.stdout}


def summary(report_entry: dict, texture: Texture) -> dict:
    """Manifest metadata for one converted texture."""
    return {"format": "GSTX1", "logical_width": texture.width, "logical_height": texture.height,
            "table_bytes": texture.table_bytes, "decoded_bytes": report_entry["decoded_bytes"],
            "vram_bytes": report_entry["vram_bytes"], "source_bytes": report_entry["source_bytes"],
            "pages": [{"width": page["width"], "height": page["height"],
                       "format": "psmt8" if page["format"] == 0x13 else "psmt4", "encoded_bytes": page["encoded_bytes"]}
                      for page in texture.pages],
            "cluts": [len(clut["texels"]) for clut in texture.cluts], "placements": len(texture.placements),
            "psmt8_placements": report_entry["psmt8_placements"], "psmt4_placements": report_entry["psmt4_placements"],
            "worst_rmse": max(entry["rmse"] for entry in report_entry["placements"]),
            "worst_error": max(entry["max_error"] for entry in report_entry["placements"])}
