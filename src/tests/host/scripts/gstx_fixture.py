#!/usr/bin/env python3
# ================================================================================================
# File: gstx_fixture.py
# Brief: Build small converted GSTX textures and their content-addressed cook records for host tests.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""Fixtures shaped like converter output: fonts are white PSMT4 at sixteen alpha levels, and
art is PSMT8. Every placement covers its whole image at full resolution, so a logical texel
center samples its own texel exactly."""

import hashlib
from pathlib import Path
import sys

_SCRIPTS = str(Path(__file__).resolve().parents[3] / "tools/scripts")
if _SCRIPTS not in sys.path:
    sys.path.insert(0, _SCRIPTS)
import ui_gstx


ALPHA_RAMP = [(255, 255, 255, level * 17) for level in range(16)]


def _page_extent(value: int, step: int) -> int:
    return max(step, (value + step - 1) // step * step)


def _whole(width: int, height: int) -> list:
    rect = (0, 0, width, height)
    return [{"match": rect, "source": rect, "dest": rect, "page": 0, "clut": 0}]


def alpha_texture(width: int, height: int, alpha: bytes) -> bytes:
    """White texels with each straight alpha byte rounded to the nearest of sixteen levels."""
    page_width, page_height = _page_extent(width, 128), _page_extent(height, 128)
    indices = bytearray(page_width * page_height // 2)
    for y in range(height):
        for x in range(width):
            level = (alpha[y * width + x] * 15 + 127) // 255
            offset = y * page_width + x
            indices[offset // 2] |= level << (4 if offset & 1 else 0)
    page = {"width": page_width, "height": page_height, "format": ui_gstx.PSMT4, "data": bytes(indices)}
    return ui_gstx.build_texture(width, height, [page], [ALPHA_RAMP], _whole(width, height))


def rgba_alpha(rgba: bytes) -> bytes:
    return bytes(rgba[3::4])


def solid_texture(width: int, height: int, color: tuple) -> bytes:
    """One opaque color through a 256-entry palette whose other entries are transparent black."""
    page_width, page_height = _page_extent(width, 128), _page_extent(height, 64)
    page = {"width": page_width, "height": page_height, "format": ui_gstx.PSMT8,
            "data": bytes(page_width * page_height)}
    palette = [tuple(color)] + [(0, 0, 0, 0)] * 255
    clut = [palette[ui_gstx.clut_swizzle(index)] for index in range(256)]
    return ui_gstx.build_texture(width, height, [page], [clut], _whole(width, height))


def store(directory: Path, raw: bytes) -> dict:
    """Store bytes, valid or not, under their content-addressed cook path; return the identity."""
    digest = hashlib.sha256(raw).hexdigest()
    relative = "assets/" + digest + ".gstx"
    (Path(directory) / "assets").mkdir(parents=True, exist_ok=True)
    (Path(directory) / relative).write_bytes(raw)
    return {"path": relative, "bytes": len(raw), "sha256": digest}


def write_asset(directory: Path, raw: bytes) -> dict:
    """Store a texture as a content-addressed cook asset; return its runtime image record."""
    texture = ui_gstx.Texture(raw)
    return {**store(directory, raw), "format": "GSTX1", "logical_width": texture.width,
            "logical_height": texture.height, "table_bytes": texture.table_bytes}
