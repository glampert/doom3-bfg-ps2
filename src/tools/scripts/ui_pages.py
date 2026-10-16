#!/usr/bin/env python3
# ================================================================================================
# File: ui_pages.py
# Brief: Cook lossless UI atlas tiles with explicit source coordinates and duplicated gutters.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""Host-only page candidates; the native SWF renderer does not bind these pages yet.

No source pixels are rescaled or discarded. Each page carries its full-atlas origin
and UV affine map, so later draw splitting can retain the original logical atlas.
The caller owns atomic package publication; output should be a private staging tree.
"""

import hashlib
from pathlib import Path


MAX_DECODED_BYTES = 64 * 1024 * 1024
MAX_PAGES = 1024
PAGE_EXTENT = 256
GUTTER = 1
INTERIOR_EXTENT = PAGE_EXTENT - 2 * GUTTER
PAGE_BYTES = PAGE_EXTENT * PAGE_EXTENT * 4


def _validate(width, height, rgba):
    if (not isinstance(width, int) or isinstance(width, bool) or
            not isinstance(height, int) or isinstance(height, bool) or
            width < 1 or height < 1 or width * height * 4 > MAX_DECODED_BYTES):
        raise ValueError("UI page dimensions exceed positive 64 MiB RGBA bound")
    try:
        view = memoryview(rgba).cast("B")
    except (TypeError, ValueError) as error:
        raise ValueError("UI pages require contiguous RGBA bytes") from error
    if view.nbytes != width * height * 4:
        raise ValueError("UI page RGBA byte count does not match dimensions")
    columns = (width + INTERIOR_EXTENT - 1) // INTERIOR_EXTENT
    rows = (height + INTERIOR_EXTENT - 1) // INTERIOR_EXTENT
    if columns * rows > MAX_PAGES:
        raise ValueError("UI page count exceeds 1024 bound")
    return view, columns, rows


def _page_pixels(view, atlas_width, atlas_height, origin_x, origin_y):
    page = bytearray(PAGE_BYTES)
    source_stride = atlas_width * 4
    page_stride = PAGE_EXTENT * 4
    # Internal gutters copy neighboring *global* source texels, preserving bilinear
    # filtering across a split. Clamp only at the full atlas boundary/POT padding.
    first_x = max(0, origin_x - GUTTER)
    end_x = min(atlas_width, origin_x - GUTTER + PAGE_EXTENT)
    left_padding = max(0, GUTTER - origin_x)
    right_padding = PAGE_EXTENT - left_padding - (end_x - first_x)
    for dest_y in range(PAGE_EXTENT):
        source_y = min(atlas_height - 1, max(0, origin_y + dest_y - GUTTER))
        start = source_y * source_stride + first_x * 4
        content = bytes(view[start:source_y * source_stride + end_x * 4])
        row = content[:4] * left_padding + content + content[-4:] * right_padding
        start = dest_y * page_stride
        page[start:start + page_stride] = row
    return bytes(page)


def _write_content(path, pixels):
    # Never replace an existing content-addressed file. A stale/corrupt file is a
    # failed candidate package, not permission to alter some earlier publication.
    if path.is_symlink():
        raise ValueError("UI page output must not be a symlink")
    try:
        with path.open("xb") as stream:
            count = stream.write(pixels)
            if count != len(pixels):
                raise OSError("short UI page write")
    except FileExistsError:
        if path.read_bytes() != pixels:
            raise ValueError("UI page content-addressed output has different bytes")


def write_pages(width, height, rgba, output: Path):
    """Write 256² RGBA tiles and return their deterministic JSON-ready layout.

    Interior rectangles cover the logical source exactly once in row-major order.
    All coordinates describe pixel *boundaries*. ``uv_affine`` maps full-atlas
    normalized UV to normalized page UV: ``page = source * scale + bias``. It
    therefore retains texel centers and must only be applied to the page's source
    rectangle after clipping/splitting; it is not a general texture repeat policy.
    """
    view, columns, rows = _validate(width, height, rgba)
    output = Path(output)
    if output.is_symlink() or (output / "assets").is_symlink():
        raise ValueError("UI page staging directories must not be symlinks")
    asset_dir = output / "assets"
    asset_dir.mkdir(parents=True, exist_ok=True)
    pages = []
    page_hashes = set()
    cooked_hash = hashlib.sha256()
    for row in range(rows):
        for column in range(columns):
            origin_x, origin_y = column * INTERIOR_EXTENT, row * INTERIOR_EXTENT
            content_width = min(INTERIOR_EXTENT, width - origin_x)
            content_height = min(INTERIOR_EXTENT, height - origin_y)
            pixels = _page_pixels(view, width, height, origin_x, origin_y)
            digest = hashlib.sha256(pixels).hexdigest()
            cooked_hash.update(pixels)
            relative = "assets/" + digest + ".rgba"
            _write_content(output / relative, pixels)
            page_hashes.add(digest)
            pages.append({"index": len(pages), "column": column, "row": row,
                          "atlas_origin": [origin_x, origin_y],
                          "source_rect": [origin_x, origin_y, content_width, content_height],
                          "interior_rect": [GUTTER, GUTTER, content_width, content_height],
                          "uv_affine": {"scale": [width / PAGE_EXTENT, height / PAGE_EXTENT],
                                        "bias": [(GUTTER - origin_x) / PAGE_EXTENT,
                                                 (GUTTER - origin_y) / PAGE_EXTENT]},
                          "sha256": digest, "path": relative, "bytes": PAGE_BYTES})
    return {"version": 1, "source_width": width, "source_height": height,
            "source_rgba_bytes": view.nbytes, "source_sha256": hashlib.sha256(view).hexdigest(),
            "page_width": PAGE_EXTENT, "page_height": PAGE_EXTENT, "gutter": GUTTER,
            "interior_extent": INTERIOR_EXTENT, "columns": columns, "rows": rows,
            "page_count": len(pages), "logical_rgba_bytes": view.nbytes,
            "cooked_rgba_bytes": len(pages) * PAGE_BYTES,
            "unique_rgba_bytes": len(page_hashes) * PAGE_BYTES,
            "cooked_sha256": cooked_hash.hexdigest(), "pages": pages}
