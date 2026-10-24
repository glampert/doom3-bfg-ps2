#!/usr/bin/env python3
# ================================================================================================
# File: ui_retail_catalog.py
# Brief: Emit a compact English-only image/font catalog without runtime JSON or SWF admission.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""UICT4 contains checked image descriptors, native font identities, layout metrics and glyph sets.

This is a dependency catalog for a bounded native probe. It does not authorize
retail movies, material expressions, masks, special blends or Shell startup.
All integer fields are little endian. String offsets are relative to the final
NUL-terminated ASCII pool; descriptor offsets are absolute file offsets. Each image
descriptor is the header and tables of its GSTX file, so the target admits the exact
converted texture before reading any pixel data.

A font record's last two words locate its glyph set table (zero without one). The tables follow
the materials in font order: u32 image index, size count, entry count, 0; per size u32 scale x/y
(GS pixels per atlas texel) and stroke weight (atlas texels), all 16.16, first entry and entry
count; per entry u32 codepoint, u16 bitmap x/y/width/height in the image's logical texels (all
zero: nothing to draw), u16 flags (bit 0: the bitmap holds the glyph left and its outline right,
at one width), u16 zero and s16 ink origin x/y in 1/16 GS pixels. Entries of one size are sorted
by codepoint. The image has one placement per page, each a band mapping 1:1 onto its page, and
every bitmap lies inside one band.
"""

import hashlib
from pathlib import Path
import re
import struct

from ui_font import inspect_font
from ui_gstx import Texture, fnv1a32
from ui_material import canonical_material_name
from ui_resources import logical_name


MAGIC = 0x54434955
VERSION = 4
FLAG_ENGLISH = 1
MAX_BYTES = 128 * 1024
MAX_IMAGES = 32
MAX_FONTS = 8
MAX_ALIASES = 128
MAX_MATERIALS = 64
MAX_TOTAL_PAGES = 256
MAX_FONT_GLYPHS = 2048
MAX_FONT_BYTES = 12 + MAX_FONT_GLYPHS * 14
MAX_DESCRIPTOR_BYTES = 64 * 1024
MAX_IMAGE_BYTES = 16 * 1024 * 1024
HEADER = struct.Struct("<16I")
IMAGE = struct.Struct("<8I")
FONT = struct.Struct("<14I")
ALIAS = struct.Struct("<2I")
MATERIAL = struct.Struct("<2I")
GLYPH_SET = struct.Struct("<4I")
GLYPH_SIZE = struct.Struct("<5I")
GLYPH_ENTRY = struct.Struct("<I6H2h")
MAX_GLYPH_SIZES = 32
MAX_GLYPH_ENTRIES = 4096
MAX_GLYPH_BANDS = 16
_SHA256 = re.compile(r"[0-9a-f]{64}\Z")
_PATH = re.compile(r"[a-z0-9_./#-]+\Z")
_FAMILY = re.compile(r"[a-z0-9_]{1,63}\Z")
_ALIAS = re.compile(r"[A-Za-z0-9_ -]{0,127}\Z")


def _path(value):
    if not isinstance(value, str) or logical_name(value) != value or not _PATH.fullmatch(value):
        raise ValueError("UICT path must be canonical relative ASCII")
    return value


def _text(value, *, empty=False):
    if (not isinstance(value, str) or len(value) > 255 or (not value and not empty) or
            any(ord(character) < 32 or ord(character) > 126 for character in value)):
        raise ValueError("UICT identity must be bounded printable ASCII")
    return value


def _integer(value, name, minimum, maximum):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError("UICT invalid integer: " + name)
    return value


def _source(directory, asset, extension, maximum):
    if not isinstance(asset, dict):
        raise ValueError("UICT asset descriptor required")
    digest = asset.get("sha256")
    if not isinstance(digest, str) or not _SHA256.fullmatch(digest):
        raise ValueError("UICT asset SHA256 required")
    relative = "assets/" + digest + extension
    if asset.get("path") != relative:
        raise ValueError("UICT asset must use its content-addressed path")
    length = _integer(asset.get("bytes"), "asset bytes", 1, maximum)
    path = directory / relative
    if ((directory / "assets").is_symlink() or path.is_symlink() or not path.is_file() or
            not path.resolve().is_relative_to(directory.resolve()) or path.stat().st_size != length):
        raise ValueError("UICT missing, unsafe or wrong-length source asset")
    return path, length, digest


def _descriptor(directory, asset):
    path, length, digest = _source(directory, asset, ".gstx", MAX_IMAGE_BYTES)
    raw = path.read_bytes()
    if len(raw) != length or hashlib.sha256(raw).hexdigest() != digest:
        raise ValueError("UICT image asset SHA256/length mismatch")
    texture = Texture(raw)
    if (asset.get("logical_width"), asset.get("logical_height")) != (texture.width, texture.height):
        raise ValueError("UICT image dimensions differ from cooked metadata")
    return texture.width, texture.height, length, len(texture.pages), texture.descriptor(raw), texture


def _fixed(value, name, maximum):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not 0 <= value <= maximum:
        raise ValueError("UICT glyph set value out of range: " + name)
    return int(round(value * 65536))


def _glyph_table(glyph_set, image_indices, images):
    """One font's glyph set table; every bitmap must lie inside one 1:1 band of the set's image."""
    if not isinstance(glyph_set, dict) or glyph_set.get("image") not in image_indices:
        raise ValueError("UICT glyph set image missing")
    image_index = image_indices[glyph_set["image"]]
    placements = images[image_index][6].placements
    if not 1 <= len(placements) <= MAX_GLYPH_BANDS:
        raise ValueError("UICT glyph set image needs 1-16 band placements")
    bands = []
    for placement in placements:
        match, source, dest = placement["match"], placement["source"], placement["dest"]
        if tuple(match) != tuple(source) or tuple(dest[2:]) != tuple(match[2:]):
            raise ValueError("UICT glyph set band is not a 1:1 placement")
        bands.append(tuple(match))
    sizes = glyph_set.get("sizes")
    if not isinstance(sizes, list) or not 1 <= len(sizes) <= MAX_GLYPH_SIZES:
        raise ValueError("UICT glyph set size count out of range")

    def bitmap(value, outline):
        if value is None:
            if outline:
                raise ValueError("UICT glyph outline without a bitmap")
            return 0, 0, 0, 0
        if not isinstance(value, list) or len(value) != 4:
            raise ValueError("UICT glyph rectangle must have four values")
        x, y, width, height = (_integer(component, "glyph rectangle", 0, 0xFFFF) for component in value)
        if width == 0 or height == 0:
            raise ValueError("UICT glyph rectangle is empty")
        if not any(bx <= x and by <= y and x + width <= bx + bw and y + height <= by + bh for bx, by, bw, bh in bands):
            raise ValueError("UICT glyph rectangle is outside every band")
        # A glyph with an outline holds both bitmaps side by side, at one width.
        if outline and width % 2:
            raise ValueError("UICT glyph outline pair has an odd width")
        return x, y, width, height

    def origin(value):
        if not isinstance(value, list) or len(value) != 2:
            raise ValueError("UICT glyph origin must be a pair")
        return [_integer(component, "glyph origin", -1024 * 16, 1024 * 16) for component in value]

    size_data, entry_data = bytearray(), bytearray()
    entries = 0
    for size in sizes:
        scale, weight, glyphs = size.get("scale"), size.get("stroke_weight"), size.get("glyphs")
        if not isinstance(scale, list) or len(scale) != 2 or not isinstance(glyphs, list) or not glyphs:
            raise ValueError("UICT glyph size invalid")
        scale_x, scale_y = _fixed(scale[0], "scale", 16.0), _fixed(scale[1], "scale", 16.0)
        stroke = _fixed(weight, "stroke weight", 16.0)
        if scale_x == 0 or scale_y == 0:
            raise ValueError("UICT glyph size scale is zero")
        size_data.extend(GLYPH_SIZE.pack(scale_x, scale_y, stroke, entries, len(glyphs)))
        previous = -1
        for glyph in glyphs:
            codepoint = _integer(glyph.get("codepoint"), "codepoint", 0, 0x10FFFF)
            if codepoint <= previous:
                raise ValueError("UICT glyph entries must be sorted by codepoint")
            previous = codepoint
            outline = glyph.get("outline")
            if not isinstance(outline, bool) or (stroke == 0 and outline):
                raise ValueError("UICT glyph outline flag invalid for its size")
            entry_data.extend(GLYPH_ENTRY.pack(codepoint, *bitmap(glyph.get("rect"), outline), int(outline), 0,
                                               *origin(glyph.get("origin"))))
        entries += len(glyphs)
    if entries > MAX_GLYPH_ENTRIES:
        raise ValueError("UICT glyph set entry count out of range")
    return GLYPH_SET.pack(image_index, len(sizes), entries, 0) + bytes(size_data) + bytes(entry_data), entries


def build_catalog(manifest, directory):
    """Return deterministic UICT bytes and an inspection-only safe summary.

    Asset byte sources have already been generated in a private cook staging
    directory. The catalog preserves original native font records; old-font
    metrics are independently retained for point sizes 12, 24 and 48.
    """
    directory = Path(directory)
    if (manifest.get("mode") != "cook" or manifest.get("supported_languages") != ["english"] or
            manifest.get("runtime_ready") is not False or manifest.get("issues")):
        raise ValueError("UICT requires an unresolved-free English cook, without retail movie activation")
    cooked = manifest["cooked"]
    source_images = cooked["runtime_images"]
    source_fonts = manifest["fonts"]
    if not 1 <= len(source_images) <= MAX_IMAGES or not 1 <= len(source_fonts) <= MAX_FONTS:
        raise ValueError("UICT image/font count exceeds bound")
    images = []
    image_indices = {}
    page_count = 0
    for name, asset in sorted(source_images.items()):
        _path(name)
        width, height, length, count, descriptor, texture = _descriptor(directory, asset)
        page_count += count
        if page_count > MAX_TOTAL_PAGES:
            raise ValueError("UICT total page count exceeds bound")
        image_indices[name] = len(images)
        images.append((name, asset["path"], width, height, length, descriptor, texture))
    resolution = {}
    for entry in manifest["image_resolutions"]:
        name = logical_name(entry["logical_image"])
        if entry.get("status") != "resolved" or entry.get("selected") not in image_indices:
            raise ValueError("UICT unresolved image binding")
        index = image_indices[entry["selected"]]
        if name in resolution and resolution[name] != index:
            raise ValueError("UICT ambiguous image binding")
        resolution[name] = index
    fonts = []
    font_indices = {}
    for name, details in sorted(source_fonts.items()):
        _path(name)
        pieces = name.split("/")
        if len(pieces) != 3 or pieces[0] != "newfonts" or pieces[2] != "48.dat":
            raise ValueError("UICT unsupported native font path")
        family = pieces[1]
        if not _FAMILY.fullmatch(family):
            raise ValueError("UICT unsupported native font family identity")
        image_index = resolution.get(name[:-4] + ".tga")
        if image_index is None:
            raise ValueError("UICT font atlas image missing")
        source = cooked["assets"].get(name)
        path, length, digest = _source(directory, source, ".dat", MAX_FONT_BYTES)
        raw = path.read_bytes()
        if len(raw) != length or hashlib.sha256(raw).hexdigest() != digest:
            raise ValueError("UICT font asset SHA256/length mismatch")
        inspected_font = inspect_font(raw, atlas_width=images[image_index][2], atlas_height=images[image_index][3])
        if inspected_font["glyph_count"] > MAX_FONT_GLYPHS:
            raise ValueError("UICT native font exceeds 2048 glyph bound")
        if details.get("bytes") != length or details.get("sha256") != digest:
            raise ValueError("UICT font metadata differs from retained source")
        metrics = []
        legacy = details.get("legacy_layout_metrics", [])
        if len(legacy) != 3:
            raise ValueError("UICT native font needs all three legacy layout metrics")
        for point_size, entry in zip((12, 24, 48), legacy):
            if entry.get("point_size") != point_size:
                raise ValueError("UICT legacy layout metrics order mismatch")
            for field in ("scaled_max_advance", "scaled_max_height"):
                metrics.append(_integer(entry.get(field), field, 1, 4096))
        font_indices[family] = len(fonts)
        fonts.append((family, name, source["path"], length, fnv1a32(raw), image_index, *metrics))
    aliases = {}
    for family, index in sorted(font_indices.items()):
        aliases[family] = index
    for entry in manifest["font_resolutions"]:
        if entry.get("family") not in font_indices:
            raise ValueError("UICT resolved font family missing")
        index = font_indices[entry["family"]]
        for value in [entry["requested"], *entry["alias_chain"]]:
            value = value.replace("fonts/", "")
            if not _ALIAS.fullmatch(value):
                raise ValueError("UICT unsupported native font alias identity")
            if value in aliases and aliases[value] != index:
                raise ValueError("UICT conflicting font alias")
            aliases[value] = index
    for key, target in sorted(manifest["language"]["font_aliases"].items()):
        if not key.startswith("#font_"):
            raise ValueError("UICT invalid English font alias")
        # Resolution is performed by the inventory's native-compatible alias walk.
        matches = [entry for entry in manifest["font_resolutions"] if entry["requested"] == target]
        if len(matches) != 1:
            raise ValueError("UICT English font alias target missing")
        value, index = key[6:].replace("fonts/", ""), font_indices[matches[0]["family"]]
        if not _ALIAS.fullmatch(value):
            raise ValueError("UICT unsupported English font alias identity")
        if value in aliases and aliases[value] != index:
            raise ValueError("UICT conflicting English font alias")
        aliases[value] = index
    if not 1 <= len(aliases) <= MAX_ALIASES:
        raise ValueError("UICT font alias count exceeds bound")
    materials = {}
    omitted_materials = []
    for requested, details in sorted(manifest["materials"]["requested"].items()):
        name = _path(canonical_material_name(requested))
        if name in ("_white", "guisolid"):
            continue
        if details.get("definition") != "implicit-image-default":
            omitted_materials.append({"name": name, "definition": details.get("definition"),
                                      "reason": "Only implicit image-default bindings are admitted; source declaration semantics remain unavailable."})
            continue
        maps = details.get("map_resources", [])
        if len(maps) != 1 or details.get("stage_count") != 1:
            raise ValueError("UICT material needs exactly one image stage")
        image = logical_name(maps[0])
        candidates = [resolution[image]] if image in resolution else []
        if not candidates:
            for suffix in (".tga", ".png", ".jpg", ".dds"):
                if image + suffix in resolution:
                    candidates.append(resolution[image + suffix])
        if not candidates or len(set(candidates)) != 1:
            raise ValueError("UICT material image binding missing or ambiguous")
        index = candidates[0]
        if name in materials and materials[name] != index:
            raise ValueError("UICT conflicting canonical material binding")
        materials[name] = index
    if len(materials) > MAX_MATERIALS:
        raise ValueError("UICT material count exceeds bound")
    strings = bytearray()
    string_offsets = {}

    def intern(value):
        _text(value, empty=True)
        if value not in string_offsets:
            string_offsets[value] = len(strings)
            strings.extend(value.encode("ascii") + b"\0")
        return string_offsets[value]

    glyph_sets = cooked.get("glyph_sets", {})
    if not isinstance(glyph_sets, dict) or any(name not in source_fonts for name in glyph_sets):
        raise ValueError("UICT glyph set names an unknown font")
    images_offset = HEADER.size
    fonts_offset = images_offset + len(images) * IMAGE.size
    aliases_offset = fonts_offset + len(fonts) * FONT.size
    materials_offset = aliases_offset + len(aliases) * ALIAS.size
    glyph_tables = bytearray()
    glyph_records = {}
    glyph_entries = 0
    for family, name, *_ in fonts:
        if name in glyph_sets:
            table, entries = _glyph_table(glyph_sets[name], image_indices, images)
            glyph_records[name] = (materials_offset + len(materials) * MATERIAL.size + len(glyph_tables), len(table))
            glyph_tables.extend(table)
            glyph_entries += entries
    descriptors_offset = materials_offset + len(materials) * MATERIAL.size + len(glyph_tables)
    descriptors = bytearray()
    image_data = bytearray()
    for name, path, width, height, length, descriptor, _ in images:
        image_data.extend(IMAGE.pack(intern(name), intern(path), width, height, length,
                                     descriptors_offset + len(descriptors), len(descriptor), 0))
        descriptors.extend(descriptor)
    font_data = bytearray()
    for family, name, path, *values in fonts:
        font_data.extend(FONT.pack(intern(family), intern(name), intern(path), *values, *glyph_records.get(name, (0, 0))))
    alias_data = b"".join(ALIAS.pack(intern(name), index) for name, index in sorted(aliases.items()))
    material_data = b"".join(MATERIAL.pack(intern(name), index) for name, index in sorted(materials.items()))
    if len(descriptors) > MAX_DESCRIPTOR_BYTES:
        raise ValueError("UICT image descriptors exceed 64 KiB bound")
    strings_offset = descriptors_offset + len(descriptors)
    total_bytes = strings_offset + len(strings)
    if total_bytes > MAX_BYTES:
        raise ValueError("UICT catalog exceeds 128 KiB bound")
    header = HEADER.pack(MAGIC, VERSION, total_bytes, FLAG_ENGLISH, len(images), len(fonts), len(aliases),
                         len(materials), images_offset, fonts_offset, aliases_offset, materials_offset,
                         descriptors_offset, strings_offset, len(strings), 0)
    raw = bytes(header + image_data + font_data + alias_data + material_data + glyph_tables + descriptors + strings)
    return raw, {"format": "UICT4", "version": VERSION, "language": "english", "image_count": len(images),
                 "font_count": len(fonts), "alias_count": len(aliases), "material_count": len(materials),
                 "page_count": page_count, "descriptor_bytes": len(descriptors), "string_bytes": len(strings),
                 "glyph_set_count": len(glyph_records), "glyph_entry_count": glyph_entries, "glyph_table_bytes": len(glyph_tables),
                 "material_policy": "implicit-image-default only; native wrappers provide one constant image stage",
                 "omitted_materials": omitted_materials,
                 "blockers": (["Explicit material declarations are omitted from target bindings; their semantics need separate admission."]
                              if omitted_materials else []),
                 "policy": "Image/font probe admission only; retail BSWF, masks, blends and Shell remain unavailable."}
