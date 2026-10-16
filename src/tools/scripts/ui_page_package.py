#!/usr/bin/env python3
# ================================================================================================
# File: ui_page_package.py
# Brief: Stream checked RGBA atlas pages into the bounded little-endian native UIPG container.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""One-atlas host package writer; retail target admission remains a separate gate."""

import hashlib
import math
import os
from pathlib import Path
import re
import struct
import tempfile


MAGIC = 0x47504955
VERSION = 1
PAGE_EXTENT = 256
GUTTER = 1
INTERIOR_EXTENT = 254
PAGE_BYTES = 262144
MAX_DIMENSION = 16384
MAX_DECODED_BYTES = 64 * 1024 * 1024
MAX_PAGES = 1024
HEADER = struct.Struct("<8I")
RECORD = struct.Struct("<II")
_SHA256 = re.compile(r"^[0-9a-f]{64}$")


def fnv1a32(data, seed=2166136261):
    for value in data:
        seed = ((seed ^ value) * 16777619) & 0xFFFFFFFF
    return seed


def _integer(value, name):
    if type(value) is not int:
        raise ValueError("UIPG layout integer required: " + name)
    return value


def _sha(value):
    if not isinstance(value, str) or not _SHA256.fullmatch(value):
        raise ValueError("UIPG layout has invalid SHA256")
    return value


def _validate_layout(layout, directory):
    if not isinstance(layout, dict):
        raise ValueError("UIPG layout must be an object")
    width = _integer(layout.get("source_width"), "source_width")
    height = _integer(layout.get("source_height"), "source_height")
    if not (1 <= width <= MAX_DIMENSION and 1 <= height <= MAX_DIMENSION) or width * height * 4 > MAX_DECODED_BYTES:
        raise ValueError("UIPG atlas dimensions exceed 16384/64 MiB bounds")
    columns, rows = (width + 253) // 254, (height + 253) // 254
    page_count = columns * rows
    if not 1 <= page_count <= MAX_PAGES:
        raise ValueError("UIPG page count exceeds 1024 bound")
    expected = {"version": 1, "source_rgba_bytes": width * height * 4,
                "logical_rgba_bytes": width * height * 4, "page_width": PAGE_EXTENT,
                "page_height": PAGE_EXTENT, "gutter": GUTTER, "interior_extent": INTERIOR_EXTENT,
                "columns": columns, "rows": rows, "page_count": page_count,
                "cooked_rgba_bytes": page_count * PAGE_BYTES}
    for name, value in expected.items():
        if _integer(layout.get(name), name) != value:
            raise ValueError("UIPG layout count/extent mismatch: " + name)
    _sha(layout.get("source_sha256"))
    _sha(layout.get("cooked_sha256"))
    pages = layout.get("pages")
    if not isinstance(pages, list) or len(pages) != page_count:
        raise ValueError("UIPG layout page list length mismatch")
    files, hashes = [], set()
    for index, page in enumerate(pages):
        if not isinstance(page, dict):
            raise ValueError("UIPG page record must be an object")
        column, row = index % columns, index // columns
        origin_x, origin_y = column * 254, row * 254
        content_width, content_height = min(254, width - origin_x), min(254, height - origin_y)
        for key, expected_value in (("index", index), ("column", column), ("row", row), ("bytes", PAGE_BYTES)):
            if _integer(page.get(key), key) != expected_value:
                raise ValueError("UIPG page ordering/length mismatch")
        for key, expected_value in (("atlas_origin", [origin_x, origin_y]),
                                    ("source_rect", [origin_x, origin_y, content_width, content_height]),
                                    ("interior_rect", [1, 1, content_width, content_height])):
            value = page.get(key)
            if (not isinstance(value, list) or any(type(item) is not int for item in value) or value != expected_value):
                raise ValueError("UIPG page source/interior rectangle mismatch")
        affine = page.get("uv_affine", {})
        expected_affine = {"scale": [width / 256, height / 256], "bias": [(1 - origin_x) / 256, (1 - origin_y) / 256]}
        for key, expected_value in expected_affine.items():
            value = affine.get(key) if isinstance(affine, dict) else None
            if (not isinstance(value, list) or len(value) != 2 or
                    any(type(item) not in (int, float) or not math.isfinite(item) for item in value) or value != expected_value):
                raise ValueError("UIPG page UV affine mismatch")
        digest = _sha(page.get("sha256"))
        relative = "assets/" + digest + ".rgba"
        if page.get("path") != relative:
            raise ValueError("UIPG page path is not its content-addressed RGBA file")
        path = directory / relative
        if (path.is_symlink() or (directory / "assets").is_symlink() or
                not path.resolve().is_relative_to(directory.resolve())):
            raise ValueError("UIPG source page escapes its directory")
        if not path.is_file() or path.stat().st_size != PAGE_BYTES:
            raise ValueError("UIPG source page has missing/truncated/oversized pixels")
        files.append(path)
        hashes.add(digest)
    if _integer(layout.get("unique_rgba_bytes"), "unique_rgba_bytes") != len(hashes) * PAGE_BYTES:
        raise ValueError("UIPG layout unique pixel byte count mismatch")
    data_offset = HEADER.size + RECORD.size * page_count
    file_size = data_offset + PAGE_BYTES * page_count
    if file_size >= 0x7FFFFFFF:
        raise ValueError("UIPG file exceeds signed 32-bit offset bound")
    return width, height, page_count, data_offset, file_size, files


def _file_hash(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            result.update(block)
    return result.hexdigest()


def write_page_package(layout, directory: Path, output: Path | None = None):
    """Stream one validated page layout to ``assets/<whole-file-SHA256>.uip``.

    The caller supplies a private staging output. Each source page is streamed in
    64 KiB chunks, checked against its SHA256 and recorded with native FNV1a32.
    No source atlas allocation, resampling or implicit retail admission occurs.
    """
    directory = Path(directory)
    output = directory if output is None else Path(output)
    width, height, count, data_offset, file_size, files = _validate_layout(layout, directory)
    if output.is_symlink() or (output / "assets").is_symlink():
        raise ValueError("UIPG output directories must not be symlinks")
    asset_dir = output / "assets"
    asset_dir.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w+b", prefix=".uip-", dir=asset_dir, delete=False) as stream:
            temporary = Path(stream.name)
            if stream.write(HEADER.pack(MAGIC, VERSION, width, height, PAGE_EXTENT, GUTTER, count, 0)) != HEADER.size:
                raise OSError("short UIPG header write")
            if stream.write(bytes(RECORD.size * count)) != RECORD.size * count:
                raise OSError("short UIPG record table write")
            cooked_hash = hashlib.sha256()
            for index, path in enumerate(files):
                page_hash, checksum, length = hashlib.sha256(), 2166136261, 0
                stream.seek(data_offset + index * PAGE_BYTES)
                with path.open("rb") as source:
                    while True:
                        block = source.read(65536)
                        if not block:
                            break
                        length += len(block)
                        if length > PAGE_BYTES:
                            raise ValueError("UIPG source page grew during streaming")
                        page_hash.update(block)
                        cooked_hash.update(block)
                        checksum = fnv1a32(block, checksum)
                        if stream.write(block) != len(block):
                            raise OSError("short UIPG pixel write")
                if length != PAGE_BYTES or page_hash.hexdigest() != layout["pages"][index]["sha256"]:
                    raise ValueError("UIPG source page SHA256/length mismatch")
                stream.seek(HEADER.size + index * RECORD.size)
                if stream.write(RECORD.pack(data_offset + index * PAGE_BYTES, checksum)) != RECORD.size:
                    raise OSError("short UIPG page record write")
            if cooked_hash.hexdigest() != layout["cooked_sha256"]:
                raise ValueError("UIPG ordered page SHA256 mismatch")
            stream.flush()
        if temporary.stat().st_size != file_size:
            raise ValueError("UIPG final file length mismatch")
        digest = _file_hash(temporary)
        relative = "assets/" + digest + ".uip"
        destination = output / relative
        if destination.is_symlink():
            raise ValueError("UIPG content target must not be a symlink")
        try:
            os.link(temporary, destination)
        except FileExistsError:
            if destination.stat().st_size != file_size or _file_hash(destination) != digest:
                raise ValueError("UIPG existing content-addressed file differs")
        return {"format": "UIPG1", "version": VERSION, "path": relative, "sha256": digest,
                "bytes": file_size, "source_width": width, "source_height": height,
                "page_extent": PAGE_EXTENT, "gutter": GUTTER, "page_count": count,
                "header_bytes": HEADER.size, "record_bytes": RECORD.size,
                "page_bytes": PAGE_BYTES, "data_offset": data_offset,
                "page_checksum": "fnv1a32"}
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
