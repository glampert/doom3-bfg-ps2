#!/usr/bin/env python3
# ================================================================================================
# File: ui_material.py
# Brief: Index checked material declarations and literal dependency candidates without native parsing.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from dataclasses import dataclass
import hashlib
import re

from ui_resources import logical_name


MAX_MATERIAL_BYTES = 16 * 1024 * 1024
MAX_MATERIAL_DECLARATIONS = 16384
MAX_MATERIAL_TOKENS = 1_000_000
MAX_TOKEN_BYTES = 4096
MAX_BRACE_DEPTH = 32
NON_MATERIAL_TYPES = {"table", "skin", "sound", "entitydef", "mapdef", "fx", "particle",
                      "articulatedfigure", "pda", "email", "video", "audio", "export"}
IMAGE_OPERATORS = {"heightmap", "addnormals", "smoothnormals", "add", "scale", "invertalpha",
                   "invertcolor", "makeintensity", "makealpha"}
FRAGMENT_OPTIONS = {"cubemap", "cameracubemap", "nearest", "linear", "clamp", "noclamp",
                    "zeroclamp", "alphazeroclamp", "highquality", "uncompressed", "nopicmip"}
UNSUPPORTED_FEATURES = {
    "if": "conditional-stage", "alphatest": "alpha-test", "deform": "deform",
    "polygonoffset": "polygon-offset", "privatepolygonoffset": "polygon-offset",
    "program": "shader-program", "vertexprogram": "shader-program", "fragmentprogram": "shader-program",
    "glslprogram": "shader-program", "fragmentmap": "shader-program", "vertexparm": "shader-program",
    "texgen": "texgen", "screen": "texgen", "screen2": "texgen", "glasswarp": "texgen",
    "remoterendermap": "render-target", "mirrorrendermap": "render-target", "xrayrendermap": "render-target",
    "videomap": "cinematic", "soundmap": "cinematic", "scroll": "texture-matrix",
    "translate": "texture-matrix", "scale": "texture-matrix", "centerscale": "texture-matrix",
    "shear": "texture-matrix", "rotate": "texture-matrix", "colored": "entity-color-registers",
    "inversevertexcolor": "inverse-vertex-color",
}
_DYNAMIC = re.compile(r"\b(?:time|parm\d+|global\d+|sound)\b", re.IGNORECASE)
_NUMBER = re.compile(r"^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$")


def _material_identity(name):
    name = name.replace("\\", "/").lower()
    if (not name or len(name) > 255 or name.startswith("/") or name.endswith("/") or
            any(part in (".", "..") for part in name.split("/")) or
            any(ord(char) < 32 or ord(char) > 126 or char == ":" for char in name)):
        raise ValueError("material name must be a bounded relative ASCII identity")
    return name


def canonical_material_name(name: str) -> str:
    """Mirror DeclManager identity without applying filesystem path aliases."""
    # Native MakeNameCanonical preserves repeated separators in declaration keys;
    # collapsing resource output paths here would merge distinct native materials.
    name = _material_identity(name)
    dot = name.rfind(".")
    result = name[:dot] if dot >= 0 else name
    return _material_identity(result)


@dataclass(frozen=True)
class _Token:
    value: str
    start: int
    end: int
    line: int
    quoted: bool = False


def _tokens(data: bytes):
    # Native declaration lexing works on bytes and disables string escape handling.
    # Latin-1 keeps byte offsets exact; resource names receive separate ASCII checks.
    text = data.decode("latin-1")
    offset, line, count = 0, 1, 0
    while offset < len(text):
        if text[offset].isspace():
            line += int(text[offset] == "\n")
            offset += 1
            continue
        if text.startswith("//", offset):
            end = text.find("\n", offset + 2)
            offset = len(text) if end < 0 else end
            continue
        if text.startswith("/*", offset):
            end = text.find("*/", offset + 2)
            if end < 0:
                raise ValueError("unterminated material comment")
            line += text.count("\n", offset, end + 2)
            offset = end + 2
            continue
        start, start_line, quoted = offset, line, False
        if text[offset] in ('"', "'"):
            quote = text[offset]
            quoted = True
            offset += 1
            value_start = offset
            end = text.find(quote, offset)
            if end < 0 or end - value_start > MAX_TOKEN_BYTES:
                raise ValueError("unterminated or oversized material string")
            value = text[value_start:end]
            if "\n" in value or "\r" in value:
                raise ValueError("material string crosses a line")
            offset = end + 1
        elif text[offset] in "{}()[],":
            value = text[offset]
            offset += 1
        else:
            while offset < len(text) and not text[offset].isspace() and text[offset] not in "{}()[],\"'":
                # ReadName with native LEXFL_ALLOWPATHNAMES keeps internal '//'.
                # Line comments are recognized between tokens, before this loop.
                if text.startswith("/*", offset):
                    break
                offset += 1
            if offset == start or offset - start > MAX_TOKEN_BYTES:
                raise ValueError("invalid or oversized material token")
            value = text[start:offset]
        count += 1
        if count > MAX_MATERIAL_TOKENS:
            raise ValueError("material token count exceeds inspection bound")
        yield _Token(value, start, offset, start_line, quoted)


def _resource(token, unresolved):
    if token.value in "{}()[]," or not token.value:
        raise ValueError("material image directive lacks a resource")
    try:
        return logical_name(token.value)
    except ValueError:
        unresolved.append({"literal": token.value, "line": token.line,
                           "reason": "resource-name-outside-relative-profile"})
        return None


def _image_argument(tokens, start, unresolved):
    if start >= len(tokens):
        raise ValueError("material image directive lacks an argument")
    if start + 1 >= len(tokens) or tokens[start + 1].value != "(" or tokens[start + 1].quoted:
        resource = _resource(tokens[start], unresolved)
        return [resource] if resource is not None else [], [], start + 1
    operators = [tokens[start].value.lower()]
    resources = []
    depth, cursor = 0, start + 1
    while cursor < len(tokens):
        token = tokens[cursor]
        if not token.quoted and token.value == "(":
            depth += 1
            if depth > MAX_BRACE_DEPTH:
                raise ValueError("material image expression exceeds nesting bound")
        elif not token.quoted and token.value == ")":
            depth -= 1
            if depth == 0:
                return resources, operators, cursor + 1
        elif not token.quoted and token.value in ("{", "}"):
            raise ValueError("unterminated material image expression")
        elif token.value != ",":
            if cursor + 1 < len(tokens) and tokens[cursor + 1].value == "(" and not token.quoted:
                operators.append(token.value.lower())
            elif not _NUMBER.fullmatch(token.value):
                resource = _resource(token, unresolved)
                if resource is not None:
                    resources.append(resource)
        cursor += 1
    raise ValueError("unterminated material image expression")


def _summary(data, name, start, end, tokens):
    maps, videos, sounds, editors, blends, operators, unresolved = [], [], [], [], [], [], []
    features, identifiers = set(), set()
    stages, depth, dynamic, cursor = 0, 0, False, 0
    for token in tokens:
        if not token.quoted and token.value == "{":
            if depth == 0:
                stages += 1
            depth += 1
        elif not token.quoted and token.value == "}":
            depth -= 1
    while cursor < len(tokens):
        token = tokens[cursor]
        word = token.value.lower()
        if token.quoted:
            cursor += 1
            continue
        if word in UNSUPPORTED_FEATURES:
            identifiers.add(word)
            features.add(UNSUPPORTED_FEATURES[word])
        dynamic |= word == "colored" or _DYNAMIC.search(word) is not None
        if word == "blend":
            if cursor + 1 >= len(tokens) or tokens[cursor + 1].value in ("{", "}"):
                raise ValueError("material blend directive lacks an argument")
            values = [tokens[cursor + 1].value.lower()]
            cursor += 2
            if cursor < len(tokens) and tokens[cursor].value == ",":
                if cursor + 1 >= len(tokens) or tokens[cursor + 1].value in ("{", "}"):
                    raise ValueError("material blend pair lacks an argument")
                values.append(tokens[cursor + 1].value.lower())
                cursor += 2
            blends.append(values)
            if values[0] in ("bumpmap", "diffusemap", "specularmap"):
                features.add("lighting-stage")
            continue
        if word in ("map", "cubemap", "cameracubemap", "diffusemap", "specularmap", "bumpmap", "qer_editorimage"):
            resources, image_operators, cursor = _image_argument(tokens, cursor + 1, unresolved)
            (editors if word == "qer_editorimage" else maps).extend(resources)
            operators.extend(image_operators)
            if word in ("diffusemap", "specularmap", "bumpmap"):
                stages += 1
                features.add("lighting-stage")
            elif word in ("cubemap", "cameracubemap"):
                features.add("cubemap")
            continue
        if word == "fragmentmap":
            cursor += 1
            if cursor >= len(tokens) or not tokens[cursor].value.isdecimal():
                raise ValueError("material fragment map lacks an image unit")
            cursor += 1
            while cursor < len(tokens) and tokens[cursor].value.lower() in FRAGMENT_OPTIONS:
                cursor += 1
            resources, image_operators, cursor = _image_argument(tokens, cursor, unresolved)
            maps.extend(resources)
            operators.extend(image_operators)
            continue
        if word in ("videomap", "soundmap"):
            cursor += 1
            if word == "videomap" and cursor < len(tokens) and tokens[cursor].value.lower() == "loop":
                cursor += 1
            if cursor >= len(tokens):
                raise ValueError("material cinematic directive lacks a resource")
            resource = _resource(tokens[cursor], unresolved)
            if resource is not None:
                (videos if word == "videomap" else sounds).append(resource)
            cursor += 1
            continue
        cursor += 1
    if operators:
        features.add("image-program")
    return {"name": canonical_material_name(name.value), "original_name": name.value,
            "definition": "explicit", "bytes": end - start, "offset": start, "line": name.line,
            "sha256": hashlib.sha256(data[start:end]).hexdigest(), "stage_count": stages,
            "stage_count_kind": "syntactic-before-native-implicit-stages",
            "map_resources": sorted(set(maps)), "editor_image_resources": sorted(set(editors)),
            "blends": blends, "dynamic_expression": bool(dynamic), "video_resources": sorted(set(videos)),
            "sound_resources": sorted(set(sounds)), "image_operators": sorted(set(operators)),
            "features": sorted(features), "unsupported_identifiers": sorted(identifiers),
            "unresolved_resources": unresolved,
            "unrecognized_image_operators": sorted(set(operators) - IMAGE_OPERATORS),
            "compatibility": "inventory-only-not-native-material-validation", "duplicate_definitions": []}


def index_materials(data: bytes) -> dict:
    """Return first-definition-wins material summaries from one checked .mtr file.

    This is a dependency candidate inventory, not the native material parser. It
    preserves evidence for explicit declarations and excludes editor-only images
    from runtime candidates; expression evaluation and capability acceptance wait
    for the renderer integration gate.
    """
    if not isinstance(data, bytes):
        raise TypeError("material input must be bytes")
    if len(data) > MAX_MATERIAL_BYTES or b"\0" in data:
        raise ValueError("invalid material file length or NUL")
    tokens = list(_tokens(data))
    result, cursor, declarations = {}, 0, 0
    while cursor < len(tokens):
        start = tokens[cursor].start
        kind = "material"
        if tokens[cursor].value.lower() == "material" or tokens[cursor].value.lower() in NON_MATERIAL_TYPES:
            kind = tokens[cursor].value.lower()
            cursor += 1
        if (cursor + 1 >= len(tokens) or tokens[cursor].value in ("{", "}") or
                tokens[cursor + 1].value != "{" or tokens[cursor + 1].quoted):
            raise ValueError("material declaration lacks name or opening brace")
        name = tokens[cursor]
        canonical = canonical_material_name(name.value) if kind == "material" else None
        cursor += 2
        content_start, depth = cursor, 1
        while cursor < len(tokens) and depth:
            token = tokens[cursor]
            if not token.quoted and token.value == "{":
                depth += 1
                if depth > MAX_BRACE_DEPTH:
                    raise ValueError("material declaration exceeds nesting bound")
            elif not token.quoted and token.value == "}":
                depth -= 1
            cursor += 1
        if depth:
            raise ValueError("material declaration lacks closing brace")
        declarations += 1
        if declarations > MAX_MATERIAL_DECLARATIONS:
            raise ValueError("material declaration count exceeds inspection bound")
        if kind != "material":
            continue
        summary = _summary(data, name, start, tokens[cursor - 1].end, tokens[content_start:cursor - 1])
        if canonical in result:
            result[canonical]["duplicate_definitions"].append(
                {key: summary[key] for key in ("original_name", "sha256", "bytes", "offset", "line")})
        else:
            result[canonical] = summary
    return result


def resolve_material(index: dict, name: str) -> dict:
    """Distinguish explicit declarations from native SetDefaultText's image default."""
    canonical = canonical_material_name(name)
    if canonical in index:
        return dict(index[canonical])
    return {"name": canonical, "original_name": name, "definition": "implicit-image-default",
            "stage_count": 1, "stage_count_kind": "generated-default", "map_resources": [logical_name(canonical)], "editor_image_resources": [],
            "blends": [["blend"]], "dynamic_expression": True, "video_resources": [], "sound_resources": [],
            "image_operators": [], "features": ["entity-color-registers"], "unsupported_identifiers": ["colored"],
            "unresolved_resources": [],
            "unrecognized_image_operators": [], "compatibility": "inventory-only-not-native-material-validation",
            "duplicate_definitions": []}
