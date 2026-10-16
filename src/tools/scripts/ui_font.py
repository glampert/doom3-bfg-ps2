#!/usr/bin/env python3
# ================================================================================================
# File: ui_font.py
# Brief: Inspect bounded native font metrics and localization without publishing localized text.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import hashlib
import math
import struct


FONT_MAGIC = 0x6964662A
FONT_HEADER = struct.Struct(">Ihhhh")
FONT_GLYPH = struct.Struct("<BBbbBxHH")
MAX_GLYPHS = 32767  # The native loader stores the count in a signed short.
MAX_ATLAS_DIMENSION = 16384
MAX_LANGUAGE_BYTES = 16 * 1024 * 1024
MAX_LANGUAGE_ENTRIES = 131072
MAX_LANGUAGE_STRING = 65536
MAX_REDIRECTION_DEPTH = 2  # idLangDict::MAX_REDIRECTION_DEPTH includes the initial lookup.
OLD_GLYPH = struct.Struct("<7i4fi32s")
OLD_GLYPHS = 256


def _scalar(codepoint):
    return 0 <= codepoint <= 0x10FFFF and not 0xD800 <= codepoint <= 0xDFFF


def inspect_font(data: bytes, *, atlas_width=None, atlas_height=None) -> dict:
    """Read Font.cpp's 48.dat ABI, including its unused byte of glyph padding.

    Optional atlas dimensions validate source rectangles, while sampling_bounds and
    needs_sampling_gutter expose the native renderer's extra half texel separately.
    The inspector does not silently clamp or change source glyph coordinates.
    """
    if not isinstance(data, bytes):
        raise TypeError("font input must be bytes")
    if not FONT_HEADER.size <= len(data) <= FONT_HEADER.size + MAX_GLYPHS * 14:
        raise ValueError("invalid native font length")
    magic, point_size, ascender, descender, count = FONT_HEADER.unpack_from(data)
    if magic != FONT_MAGIC or point_size != 48:
        raise ValueError("unsupported native font magic or point size")
    if not 1 <= count <= MAX_GLYPHS:
        raise ValueError("invalid native font glyph count")
    if not 0 <= ascender <= 4096 or not -4096 <= descender <= 4096:
        raise ValueError("invalid native font vertical metrics")
    if len(data) != FONT_HEADER.size + count * (FONT_GLYPH.size + 4):
        raise ValueError("native font glyph/index length mismatch")
    if (atlas_width is None) != (atlas_height is None):
        raise ValueError("native font needs both atlas dimensions")
    if atlas_width is not None:
        if (not isinstance(atlas_width, int) or not isinstance(atlas_height, int) or
                isinstance(atlas_width, bool) or isinstance(atlas_height, bool) or
                not 1 <= atlas_width <= MAX_ATLAS_DIMENSION or
                not 1 <= atlas_height <= MAX_ATLAS_DIMENSION):
            raise ValueError("invalid native font atlas dimensions")

    index_offset = FONT_HEADER.size + count * FONT_GLYPH.size
    codepoints = list(struct.unpack_from("<" + "I" * count, data, index_offset))
    if any(not _scalar(value) for value in codepoints):
        raise ValueError("invalid native font Unicode codepoint")
    if any(first > second for first, second in zip(codepoints, codepoints[1:])):
        raise ValueError("native font indices must be sorted")
    # Retail Latin files repeat some non-ASCII characters. Native binary search
    # selects the last occurrence; report this rather than silently deduplicating.
    selected_indices = {codepoint: index for index, codepoint in enumerate(codepoints)}
    duplicates = sorted({first for first, second in zip(codepoints, codepoints[1:]) if first == second})
    if any(codepoint < 128 and index > 127 for codepoint, index in selected_indices.items()):
        raise ValueError("native font ASCII cache index exceeds signed byte")

    glyphs = []
    required_width, required_height, padding_nonzero = 0, 0, 0
    sampling = []
    gutter = []
    for index, codepoint in enumerate(codepoints):
        offset = FONT_HEADER.size + index * FONT_GLYPH.size
        width, height, top, left, advance, s, t = FONT_GLYPH.unpack_from(data, offset)
        if s + width > MAX_ATLAS_DIMENSION or t + height > MAX_ATLAS_DIMENSION:
            raise ValueError("native font glyph exceeds atlas coordinate bound")
        if atlas_width is not None and (s + width > atlas_width or t + height > atlas_height):
            raise ValueError("native font glyph exceeds atlas dimensions")
        required_width = max(required_width, s + width)
        required_height = max(required_height, t + height)
        padding_nonzero += int(data[offset + 5] != 0)
        glyphs.append({"codepoint": codepoint, "width": width, "height": height,
                       "top": top, "left": left, "advance": advance, "s": s, "t": t,
                       "selected_for_codepoint": selected_indices[codepoint] == index})
        if width and height:
            bounds = [s - 0.5, t - 0.5, s + width + 0.5, t + height + 0.5]
            sampling.append(bounds)
            if (bounds[0] < 0 or bounds[1] < 0 or
                    (atlas_width is not None and (bounds[2] > atlas_width or bounds[3] > atlas_height))):
                gutter.append(codepoint)
    sampling_bounds = ([min(bounds[0] for bounds in sampling), min(bounds[1] for bounds in sampling),
                        max(bounds[2] for bounds in sampling), max(bounds[3] for bounds in sampling)]
                       if sampling else [0, 0, 0, 0])
    return {"format": "native-idf42", "sha256": hashlib.sha256(data).hexdigest(),
            "bytes": len(data), "header_bytes": FONT_HEADER.size, "glyph_record_bytes": FONT_GLYPH.size,
            "point_size": point_size, "ascender": ascender, "descender": descender,
            "glyph_count": count, "unique_glyph_count": len(selected_indices),
            "codepoints": sorted(selected_indices), "character_indices": codepoints, "glyphs": glyphs,
            "duplicate_codepoints": duplicates,
            "ascii_printable_count": sum(32 <= value <= 126 for value in selected_indices),
            "required_atlas_extent": [required_width, required_height],
            "atlas_dimensions": [atlas_width, atlas_height] if atlas_width is not None else None,
            "sampling_bounds": sampling_bounds, "needs_sampling_gutter": gutter,
            "padding_nonzero_count": padding_nonzero,
            "max_advance": max(glyph["advance"] for glyph in glyphs),
            "max_height": max(glyph["height"] for glyph in glyphs)}


def inspect_old_font(data: bytes) -> dict:
    """Inspect the 256 old glyph records used only for legacy line/advance metrics.

    Native LoadOldGlyphData reads exactly these records. The older fontInfo_t
    exporter may also append a float glyphScale and a 64-byte font name.
    """
    if not isinstance(data, bytes):
        raise TypeError("old font input must be bytes")
    records_size = OLD_GLYPHS * OLD_GLYPH.size
    if len(data) not in (records_size, records_size + 68):
        raise ValueError("invalid legacy font length")
    glyphs = []
    for index in range(OLD_GLYPHS):
        height, top, bottom, pitch, advance, width, image_height, s, t, s2, t2, junk, name = (
            OLD_GLYPH.unpack_from(data, index * OLD_GLYPH.size))
        if (not all(math.isfinite(value) for value in (s, t, s2, t2)) or
                not 0 <= width <= 256 or not 0 <= height <= 256 or pitch != width or image_height != height or
                not -4096 <= top <= 4096 or not -4096 <= bottom <= 4096 or
                not 0 <= advance <= 4096 or junk != 0 or
                not 0 <= s <= s2 <= 1 or not 0 <= t <= t2 <= 1 or
                abs((s2 - s) * 256 - width) > 0.0001 or abs((t2 - t) * 256 - height) > 0.0001):
            raise ValueError("invalid legacy font glyph metrics")
        name_end = name.find(b"\0")
        if name_end < 0:
            name_end = len(name)  # This field is unused by native legacy metric loading.
        try:
            material = name[:name_end].decode("ascii")
        except UnicodeDecodeError as error:
            raise ValueError("non-ASCII legacy font material name") from error
        if any(ord(character) < 32 or ord(character) == 127 for character in material):
            raise ValueError("invalid legacy font material name")
        glyphs.append({"codepoint": index, "width": width, "height": height, "top": top,
                       "bottom": bottom, "advance": advance, "uv": [s, t, s2, t2], "material": material,
                       "material_name_terminated": b"\0" in name})
    trailer = None
    if len(data) != records_size:
        scale, name = struct.unpack_from("<f64s", data, records_size)
        if not math.isfinite(scale) or not 0 < scale <= 16 or b"\0" not in name:
            raise ValueError("invalid legacy font trailer")
        try:
            name = name.split(b"\0", 1)[0].decode("ascii")
        except UnicodeDecodeError as error:
            raise ValueError("invalid legacy font trailer name") from error
        trailer = {"glyph_scale": scale, "name": name}
    return {"format": "legacy-256-glyph", "sha256": hashlib.sha256(data).hexdigest(),
            "bytes": len(data), "glyph_record_bytes": OLD_GLYPH.size, "glyph_count": OLD_GLYPHS,
            "max_advance": max(glyph["advance"] for glyph in glyphs),
            "max_height": max(glyph["height"] for glyph in glyphs), "glyphs": glyphs, "trailer": trailer}


class _LanguageLexer:
    def __init__(self, text):
        self.text = text
        self.offset = 0

    def token(self):
        while self.offset < len(self.text):
            if self.text[self.offset].isspace():
                self.offset += 1
            elif self.text.startswith("//", self.offset):
                end = self.text.find("\n", self.offset + 2)
                self.offset = len(self.text) if end < 0 else end + 1
            else:
                break
        if self.offset == len(self.text):
            return None
        char = self.text[self.offset]
        self.offset += 1
        if char in "{}":
            return char, None
        if char != '"':
            raise ValueError("language dictionary expects a brace or quoted string")
        result = []
        while self.offset < len(self.text):
            char = self.text[self.offset]
            self.offset += 1
            if char == '"':
                return "string", "".join(result)
            if char in "\r\n":
                raise ValueError("language dictionary has an unterminated string")
            if char == "\\":
                if self.offset == len(self.text):
                    raise ValueError("language dictionary has an unterminated escape")
                char = self.text[self.offset]
                self.offset += 1
                escapes = {"n": "\n", "t": "\t", '"': '"', "\\": "\\"}
                if char not in escapes:
                    raise ValueError("unsupported language escape sequence")
                char = escapes[char]
            if ord(char) < 32 and char not in "\t\n":
                raise ValueError("language dictionary has an invalid control character")
            result.append(char)
            if len(result) > MAX_LANGUAGE_STRING:
                raise ValueError("language string exceeds inspection bound")
        raise ValueError("language dictionary has an unterminated string")


def inspect_language(data: bytes) -> dict:
    """Return safe metadata plus a private `entries` mapping for dependency work.

    Callers writing inventory reports must omit `entries`: its values are retail
    localized text. Hashes, IDs, glyph coverage and font resource aliases are safe
    to retain. ASCII and BOM-prefixed UTF-8 match native non-retail acceptance.
    """
    if not isinstance(data, bytes):
        raise TypeError("language input must be bytes")
    if not 1 <= len(data) <= MAX_LANGUAGE_BYTES:
        raise ValueError("invalid language dictionary length")
    bom = data.startswith(b"\xef\xbb\xbf")
    try:
        text = data[3:].decode("utf-8") if bom else data.decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError("language dictionary requires ASCII or BOM-prefixed UTF-8") from error
    if "\0" in text:
        raise ValueError("language dictionary contains NUL")
    lexer = _LanguageLexer(text)
    if lexer.token() != ("{", None):
        raise ValueError("language dictionary lacks opening brace")
    entries = {}
    duplicate_keys = []
    parsed_count = 0
    while True:
        token = lexer.token()
        if token == ("}", None):
            break
        if token is None or token[0] != "string":
            raise ValueError("language dictionary lacks a key or closing brace")
        key = token[1]
        if (not key or len(key) > 512 or not key.isascii() or
                any(character.isspace() or ord(character) < 32 for character in key)):
            raise ValueError("invalid language dictionary key")
        value = lexer.token()
        if value is None or value[0] != "string":
            raise ValueError("language dictionary key lacks a value")
        key = key.casefold()
        if key in entries:
            duplicate_keys.append(key)
        entries[key] = value[1]
        parsed_count += 1
        if parsed_count > MAX_LANGUAGE_ENTRIES:
            raise ValueError("language dictionary exceeds entry bound")
    if lexer.token() is not None:
        raise ValueError("language dictionary has trailing tokens")
    aliases = {key: value for key, value in entries.items() if key.startswith("#font_")}
    for value in aliases.values():
        if not value or len(value) > 256 or not value.isascii() or any(ord(char) < 32 for char in value):
            raise ValueError("invalid localization font alias")
    codepoints = sorted({ord(char) for key, value in entries.items() if not key.startswith("#font_") for char in value})
    return {"format": "native-lang", "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
            "encoding": "utf-8-bom" if bom else "ascii", "entry_count": len(entries),
            "parsed_entry_count": parsed_count, "duplicate_keys": duplicate_keys,
            "font_aliases": aliases, "codepoints": codepoints, "entries": entries}


def language_coverage(language: dict, keys, available_codepoints=None) -> dict:
    """Resolve native redirects and describe a selected label set without its text."""
    entries = language["entries"]
    available = set(available_codepoints) if available_codepoints is not None else None
    requested = sorted(set(key.casefold() for key in keys))
    records, union = [], set()
    for key in requested:
        current, chain = key, []
        value = None
        reason = None
        while len(chain) < MAX_REDIRECTION_DEPTH:
            if current in chain:
                reason = "redirect-cycle"
                break
            chain.append(current)
            if current not in entries:
                reason = "missing-key"
                break
            value = entries[current]
            if value.casefold().startswith("#str_"):
                current = value.casefold()
            else:
                break
        else:
            reason = "redirect-depth"
        if reason:
            records.append({"key": key, "resolved": False, "reason": reason, "chain": chain})
            continue
        codepoints = sorted(set(map(ord, value)))
        # Control characters select layout rather than a drawable glyph.
        drawable = [codepoint for codepoint in codepoints if codepoint >= 32 and codepoint != 127]
        union.update(drawable)
        records.append({"key": key, "resolved": True, "chain": chain, "characters": len(value),
                        "sha256": hashlib.sha256(value.encode("utf-8")).hexdigest(), "codepoints": codepoints,
                        "missing_glyphs": sorted(set(drawable) - available) if available is not None else None})
    return {"requested_count": len(requested), "resolved_count": sum(record["resolved"] for record in records),
            "records": records, "codepoints": sorted(union), "glyph_count": len(union),
            "missing_glyphs": sorted(union - available) if available is not None else None}
