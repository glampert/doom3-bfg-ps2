// ================================================================================================
// File: texture_layout.h
// Brief: Validate direct RGBA and paletted textures and charge whole GS pages before allocating or uploading.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2::renderer::tex
{

static constexpr int kMaxDimension = 512;

enum class PixelFormat : std::uint8_t
{
    Rgba32,
    // GS PSMT8/PSMT4: indices into a CLUT each draw names separately.
    Index8,
    Index4
};

enum class Filter : std::uint8_t
{
    Nearest,
    Linear
};

enum class Wrap : std::uint8_t
{
    Clamp,
    Repeat
};

struct Sampler
{
    Filter filter = Filter::Nearest;
    Wrap wrapS = Wrap::Clamp;
    Wrap wrapT = Wrap::Clamp;
};

struct Layout
{
    int width;
    int height;
    int potWidth;
    int potHeight;
    int stridePixels;
    int storageHeight;
    std::uint32_t bytes;
    std::uint8_t widthLog2;
    std::uint8_t heightLog2;
    PixelFormat format = PixelFormat::Rgba32;
};

// Repeat currently requires a power-of-two logical extent on the corresponding axis.
// Failed validation leaves the output unchanged.
bool MakeLayout(int width, int height, const Sampler & sampler, Layout & output);

// Paletted layouts carry no padding: widths are whole multiples of 128 texels (TBW must be even for
// PSMT8/PSMT4) and heights whole GS pages (64 rows for PSMT8, 128 for PSMT4), up to 512, so the
// index data is exactly its VRAM footprint and uploads as one rectangle.
bool MakeIndexedLayout(int width, int height, PixelFormat format, Layout & output);

// Source is straight RGBA8; destination is RGBA8 with GS alpha unity represented by 128.
// Every padded texel repeats the nearest logical edge. Buffers must not overlap.
// Source capacity includes the last row's logical pixels, without requiring trailing padding.
bool PadRgba32(const Layout & layout, const void * source, size_t sourceBytes,
               size_t sourceStrideBytes, void * destination, size_t destinationBytes);

} // namespace ps2::renderer::tex
