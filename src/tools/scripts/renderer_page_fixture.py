#!/usr/bin/env python3
# ================================================================================================
# File: renderer_page_fixture.py
# Brief: Stage authored multi-page atlas bytes and isolated malformed variants for renderer checks.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import hashlib
from pathlib import Path
import struct
import tempfile

from ui_pages import write_pages
from ui_page_package import write_page_package


WIDTH = 600
HEIGHT = 320
NAMES = ("renderer-pages.uip", "renderer-pages-bad-header.uip", "renderer-pages-bad-table.uip",
         "renderer-pages-truncated.uip", "renderer-pages-bad-pixel.uip")


def authored_rgba():
    top = bytes((255, 0, 0, 255)) * 254 + bytes((0, 255, 0, 255)) * 254 + bytes((0, 0, 255, 255)) * 92
    bottom = bytes((0, 255, 255, 255)) * 254 + bytes((255, 0, 255, 255)) * 254 + bytes((255, 255, 0, 255)) * 92
    return top * 254 + bottom * 66


def stage(directory: Path) -> dict:
    """Use the real page cookers inside the runner's fresh private output directory."""
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".renderer-pages-", dir=directory) as temporary:
        staging = Path(temporary)
        layout = write_pages(WIDTH, HEIGHT, authored_rgba(), staging)
        package = write_page_package(layout, staging)
        good = (staging / package["path"]).read_bytes()
    header = bytearray(good)
    header[0] ^= 0xFF
    table = bytearray(good)
    struct.pack_into("<I", table, 32, package["data_offset"] + 1)
    pixel = bytearray(good)
    pixel[package["data_offset"]] ^= 0xFF
    variants = (good, header, table, good[:-1], pixel)
    metadata = {}
    for name, data in zip(NAMES, variants, strict=True):
        (directory / name).write_bytes(data)
        metadata[name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    return metadata
