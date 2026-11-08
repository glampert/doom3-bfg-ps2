// ================================================================================================
// File: bimage.h
// Brief: Validate native BIMAGE v10 files and decode their DXT1/DXT5 and RGB565 levels to straight
// RGBA8 with the engine's integer interpolation and colour-format conversions, so converted textures
// start from the pixels it would see.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "assets/image.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace assets
{

// Native textureFormat_t and textureColor_t identifiers (ImageOpts.h).
static constexpr int kBimageFormatDxt1 = 7;
static constexpr int kBimageFormatDxt5 = 8;
static constexpr int kBimageFormatRgb565 = 12;
static constexpr int kBimageColorDefault = 0;
static constexpr int kBimageColorNormalDxt5 = 1;
static constexpr int kBimageColorYCoCgDxt5 = 2;
static constexpr int kBimageColorGreenAlpha = 3;

struct BimageLevel
{
    int level = 0;
    int face = 0;
    int width = 0;
    int height = 0;
    std::size_t offset = 0;
    std::size_t bytes = 0;
};

struct BimageInfo
{
    int textureType = 0;
    int format = 0;
    int colorFormat = 0;
    int width = 0;
    int height = 0;
    int levels = 0;
    std::vector<BimageLevel> mips;
};

// Validates the header and every level, including those the caller will not decode: unknown
// formats, inconsistent dimensions, duplicate levels, wrong payload sizes and trailing bytes fail.
bool ParseBimage(std::span<const std::uint8_t> bytes, BimageInfo & output, std::string & error);

// Decodes one 2D level of the encodings retail ships (texture_census.py): DXT1/DXT5 as stored,
// green-alpha as white RGB with the coverage in alpha (gl_Image.cpp's swizzle), YCoCg DXT5 to RGB
// with alpha 255 (ConvertCoCgSYToRGB), normal-map DXT5 to the normal as RGB (0..255 for -1..1,
// Z rebuilt) with alpha 255, and big-endian RGB565 to RGB with alpha 255. Other formats are refused.
bool DecodeBimage(std::span<const std::uint8_t> bytes, int level, RgbaImage & output, std::string & error);

} // namespace assets
