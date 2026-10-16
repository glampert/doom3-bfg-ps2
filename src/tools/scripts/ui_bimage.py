#!/usr/bin/env python3
# ================================================================================================
# File: ui_bimage.py
# Brief: Bound native binary-image metadata and decode UI DXT pixels without target allocations.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""Native BIMAGE v10 metadata and the first UI cooking color formats.

The layouts and integer interpolation follow BinaryImageData.h, BinaryImage.cpp,
ImageOpts.h and DXT/DXTDecoder.cpp. Green-alpha reproduces gl_Image.cpp's white
RGB/green-alpha texture swizzle. This module never writes extracted retail data.
"""

import hashlib
import struct


MAX_DECODED_BYTES = 64 * 1024 * 1024
MAX_DIMENSION = 16384
MAX_LEVELS = 15
BIMAGE_VERSION = 10
BIMAGE_MAGIC = 0x0A4D4942
FMT_DXT1 = 7
FMT_DXT5 = 8
CFM_DEFAULT = 0
CFM_NORMAL_DXT5 = 1
CFM_YCOCG_DXT5 = 2
CFM_GREEN_ALPHA = 3
_HEADER = struct.Struct(">q7i")
_IMAGE = struct.Struct(">5i")
_TEXTURE_TYPES = {1: "TT_2D", 2: "TT_CUBIC"}
_FORMATS = {
    1: ("FMT_RGBA8", 4), 2: ("FMT_XRGB8", 4), 3: ("FMT_ALPHA", 1),
    4: ("FMT_L8A8", 2), 5: ("FMT_LUM8", 1), 6: ("FMT_INT8", 1),
    FMT_DXT1: ("FMT_DXT1", 0), FMT_DXT5: ("FMT_DXT5", 0),
    9: ("FMT_DEPTH", 4), 10: ("FMT_X16", 2), 11: ("FMT_Y16_X16", 4),
    12: ("FMT_RGB565", 2),
}
_COLOR_FORMATS = {
    CFM_DEFAULT: "CFM_DEFAULT", CFM_NORMAL_DXT5: "CFM_NORMAL_DXT5",
    CFM_YCOCG_DXT5: "CFM_YCOCG_DXT5", CFM_GREEN_ALPHA: "CFM_GREEN_ALPHA",
}


def _stored_size(width, height, image_format):
    if image_format in (FMT_DXT1, FMT_DXT5):
        return ((width + 3) // 4) * ((height + 3) // 4) * (8 if image_format == FMT_DXT1 else 16)
    return width * height * _FORMATS[image_format][1]


def inspect_bimage(data):
    """Validate every header/mip and return JSON-ready metadata and content hashes.

    The 64 MiB envelope covers all RGBA mip/face output, before any decoding.
    Unknown versions/formats, inconsistent dimensions, duplicate/missing levels,
    incorrect block sizes and unconsumed bytes are rejected, including in mips the
    caller does not intend to decode. Cube metadata is supported; decoding is 2D.
    """
    view = memoryview(data)
    if view.nbytes < _HEADER.size:
        raise ValueError("truncated BIMAGE header")
    source_time, magic, texture_type, image_format, color_format, width, height, levels = _HEADER.unpack_from(view)
    if magic != BIMAGE_MAGIC:
        raise ValueError("unsupported BIMAGE magic/version")
    if texture_type not in _TEXTURE_TYPES:
        raise ValueError("unsupported BIMAGE texture type")
    if image_format not in _FORMATS or color_format not in _COLOR_FORMATS:
        raise ValueError("unsupported BIMAGE format identifier")
    if not (1 <= width <= MAX_DIMENSION and 1 <= height <= MAX_DIMENSION):
        raise ValueError("BIMAGE dimensions exceed bounds")
    if not (1 <= levels <= min(MAX_LEVELS, max(width, height).bit_length())):
        raise ValueError("BIMAGE mip count exceeds dimensions/bounds")
    faces = 6 if texture_type == 2 else 1
    if faces == 6 and width != height:
        raise ValueError("BIMAGE cube faces are not square")
    decoded_bytes = width * height * 4 * faces
    decoded_mip_bytes = sum(max(1, width >> level) * max(1, height >> level) * 4 * faces
                            for level in range(levels))
    if decoded_mip_bytes > MAX_DECODED_BYTES:
        raise ValueError("BIMAGE decoded pixels exceed 64 MiB bound")
    offset = _HEADER.size
    payload_bytes = 0
    mips = []
    seen = set()
    for _ in range(levels * faces):
        if _IMAGE.size > view.nbytes - offset:
            raise ValueError("truncated BIMAGE mip header")
        level, face, mip_width, mip_height, size = _IMAGE.unpack_from(view, offset)
        offset += _IMAGE.size
        if not (0 <= level < levels and 0 <= face < faces) or (level, face) in seen:
            raise ValueError("invalid/duplicate BIMAGE mip level or face")
        expected_width, expected_height = max(1, width >> level), max(1, height >> level)
        # Native cube compression retains its padded 4x4 dimensions for tail levels.
        stored_width = max(4, expected_width) if faces == 6 and image_format in (FMT_DXT1, FMT_DXT5) else expected_width
        stored_height = max(4, expected_height) if faces == 6 and image_format in (FMT_DXT1, FMT_DXT5) else expected_height
        if (mip_width, mip_height) != (stored_width, stored_height):
            raise ValueError("inconsistent BIMAGE mip dimensions")
        if size != _stored_size(mip_width, mip_height, image_format):
            raise ValueError("incorrect BIMAGE mip payload size")
        if size > view.nbytes - offset:
            raise ValueError("truncated BIMAGE mip payload")
        mips.append({"level": level, "face": face, "width": expected_width, "height": expected_height,
                     "stored_width": mip_width, "stored_height": mip_height,
                     "offset": offset, "size": size,
                     "decoded_bytes": expected_width * expected_height * 4,
                     "sha256": hashlib.sha256(view[offset:offset + size]).hexdigest()})
        seen.add((level, face))
        offset += size
        payload_bytes += size
    if offset != view.nbytes:
        raise ValueError("trailing BIMAGE bytes")
    return {"version": BIMAGE_VERSION, "source_time": source_time,
            "texture_type": _TEXTURE_TYPES[texture_type], "texture_type_id": texture_type,
            "format": _FORMATS[image_format][0], "format_id": image_format,
            "color_format": _COLOR_FORMATS[color_format], "color_format_id": color_format,
            "width": width, "height": height, "num_levels": levels,
            "decoded_bytes": decoded_bytes, "decoded_mip_bytes": decoded_mip_bytes,
            "stored_bytes": view.nbytes, "payload_bytes": payload_bytes,
            "sha256": hashlib.sha256(view).hexdigest(),
            "mips": sorted(mips, key=lambda mip: (mip["level"], mip["face"]))}


def _rgb565(value):
    red, green, blue = (value >> 11) & 31, (value >> 5) & 63, value & 31
    return ((red << 3) | (red >> 2), (green << 2) | (green >> 4), (blue << 3) | (blue >> 2))


def _color_palette(color0, color1, force_four):
    first, second = _rgb565(color0), _rgb565(color1)
    colors = [(*first, 255), (*second, 255)]
    if force_four or color0 > color1:
        colors.append(tuple((2 * first[channel] + second[channel]) // 3 for channel in range(3)) + (255,))
        colors.append(tuple((first[channel] + 2 * second[channel]) // 3 for channel in range(3)) + (255,))
    else:
        colors.append(tuple((first[channel] + second[channel]) // 2 for channel in range(3)) + (255,))
        colors.append((0, 0, 0, 0))
    return colors


def _alpha_palette(first, second):
    if first > second:
        return [first, second] + [((7 - index) * first + index * second) // 7 for index in range(1, 7)]
    return [first, second] + [((5 - index) * first + index * second) // 5 for index in range(1, 5)] + [0, 255]


def decode_level(data, level=0):
    """Return (width, height, RGBA bytes) for one validated 2D DXT UI mip.

    Green-alpha restores white RGB and coverage in alpha. ChannelScale belongs to
    the referring SWF dictionary, not BIMAGE, and remains a separate cooking step.
    Normal/YCoCg encodings fail explicitly rather than producing incorrect colors.
    """
    info = inspect_bimage(data)
    if info["texture_type_id"] != 1:
        raise ValueError("unsupported BIMAGE decode: cube texture")
    image_format, color_format = info["format_id"], info["color_format_id"]
    if color_format not in (CFM_DEFAULT, CFM_GREEN_ALPHA):
        raise ValueError("unsupported BIMAGE decode color format: " + info["color_format"])
    if image_format not in (FMT_DXT1, FMT_DXT5):
        raise ValueError("unsupported BIMAGE decode format: " + info["format"])
    if not isinstance(level, int) or isinstance(level, bool) or not 0 <= level < info["num_levels"]:
        raise ValueError("invalid BIMAGE decode mip level")
    mip = info["mips"][level]
    width, height = mip["width"], mip["height"]
    view = memoryview(data)
    offset = mip["offset"]
    output = bytearray(width * height * 4)
    for block_y in range(0, height, 4):
        for block_x in range(0, width, 4):
            alphas = alpha_indices = None
            if image_format == FMT_DXT5:
                alphas = _alpha_palette(view[offset], view[offset + 1])
                alpha_indices = int.from_bytes(view[offset + 2:offset + 8], "little")
                offset += 8
            color0, color1, color_indices = struct.unpack_from("<HHI", view, offset)
            offset += 8
            colors = _color_palette(color0, color1, image_format == FMT_DXT5)
            for pixel in range(16):
                x, y = block_x + pixel % 4, block_y + pixel // 4
                if x >= width or y >= height:
                    continue
                red, green, blue, alpha = colors[(color_indices >> (2 * pixel)) & 3]
                if alphas is not None:
                    alpha = alphas[(alpha_indices >> (3 * pixel)) & 7]
                if color_format == CFM_GREEN_ALPHA:
                    red, green, blue, alpha = 255, 255, 255, green
                dest = (y * width + x) * 4
                output[dest:dest + 4] = bytes((red, green, blue, alpha))
    return width, height, bytes(output)
