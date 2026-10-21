// ================================================================================================
// File: bimage.cpp
// Brief: Validate native BIMAGE v10 files and decode their DXT1/DXT5 levels to straight RGBA8 with
// the engine's integer interpolation, so converted textures start from the pixels it would see.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/bimage.h"
#include "assets/bytes.h"

#include <algorithm>
#include <array>
#include <set>
#include <utility>

namespace assets
{
namespace
{

static constexpr std::uint32_t kBimageMagic = 0x0A4D4942;
static constexpr int kMaxDimension = 16384;
static constexpr int kMaxLevels = 15;
static constexpr std::size_t kHeaderBytes = 36;
static constexpr std::size_t kLevelHeaderBytes = 20;
static constexpr std::uint64_t kMaxDecodedBytes = 64ULL * 1024ULL * 1024ULL;

static int ReadBeInt(std::span<const std::uint8_t> bytes, std::size_t offset)
{
    return static_cast<int>(ReadBe32(bytes.data() + offset));
}

// Bytes per texel for uncompressed formats; zero marks the block-compressed ones.
static int TexelBytes(int format)
{
    switch (format)
    {
    case 1:
    case 2:
    case 9:
    case 11:
        return 4;
    case 4:
    case 10:
    case 12:
        return 2;
    case 3:
    case 5:
    case 6:
        return 1;
    default:
        return 0;
    }
}

static bool KnownFormat(int format)
{
    return format >= 1 && format <= 12;
}

static std::uint64_t StoredBytes(int width, int height, int format)
{
    if (format == kBimageFormatDxt1 || format == kBimageFormatDxt5)
    {
        const std::uint64_t blocks = static_cast<std::uint64_t>((width + 3) / 4) * static_cast<std::uint64_t>((height + 3) / 4);
        return blocks * (format == kBimageFormatDxt1 ? 8U : 16U);
    }

    return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * static_cast<std::uint64_t>(TexelBytes(format));
}

static int BitLength(int value)
{
    int bits = 0;

    while (value > 0)
    {
        ++bits;
        value >>= 1;
    }

    return bits;
}

using Rgba = std::array<std::uint8_t, 4>;

static Rgba Rgb565(std::uint16_t value)
{
    const int red = (value >> 11) & 31;
    const int green = (value >> 5) & 63;
    const int blue = value & 31;
    return { static_cast<std::uint8_t>((red << 3) | (red >> 2)), static_cast<std::uint8_t>((green << 2) | (green >> 4)),
             static_cast<std::uint8_t>((blue << 3) | (blue >> 2)), 255 };
}

// DXTDecoder.cpp's palettes: DXT5 color blocks always use four colors.
static std::array<Rgba, 4> ColorPalette(std::uint16_t color0, std::uint16_t color1, bool forceFour)
{
    const Rgba first = Rgb565(color0);
    const Rgba second = Rgb565(color1);
    std::array<Rgba, 4> colors = { first, second, Rgba{}, Rgba{} };

    if (forceFour || color0 > color1)
    {
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            colors[2][channel] = static_cast<std::uint8_t>((2 * first[channel] + second[channel]) / 3);
            colors[3][channel] = static_cast<std::uint8_t>((first[channel] + 2 * second[channel]) / 3);
        }

        colors[2][3] = 255;
        colors[3][3] = 255;
    }
    else
    {
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            colors[2][channel] = static_cast<std::uint8_t>((first[channel] + second[channel]) / 2);
        }

        colors[2][3] = 255;
        colors[3] = { 0, 0, 0, 0 };
    }

    return colors;
}

static std::array<std::uint8_t, 8> AlphaPalette(int first, int second)
{
    std::array<std::uint8_t, 8> alphas{};
    alphas[0] = static_cast<std::uint8_t>(first);
    alphas[1] = static_cast<std::uint8_t>(second);

    if (first > second)
    {
        for (int index = 1; index < 7; ++index)
        {
            alphas[static_cast<std::size_t>(index + 1)] = static_cast<std::uint8_t>(((7 - index) * first + index * second) / 7);
        }
    }
    else
    {
        for (int index = 1; index < 5; ++index)
        {
            alphas[static_cast<std::size_t>(index + 1)] = static_cast<std::uint8_t>(((5 - index) * first + index * second) / 5);
        }

        alphas[6] = 0;
        alphas[7] = 255;
    }

    return alphas;
}

} // namespace

bool ParseBimage(std::span<const std::uint8_t> bytes, BimageInfo & output, std::string & error)
{
    if (bytes.size() < kHeaderBytes)
    {
        error = "truncated BIMAGE header";
        return false;
    }

    // Big-endian: int64 source time, then magic, type, format, color format, width, height, levels.
    BimageInfo info;
    const std::uint32_t magic = ReadBe32(bytes.data() + 8);
    info.textureType = ReadBeInt(bytes, 12);
    info.format = ReadBeInt(bytes, 16);
    info.colorFormat = ReadBeInt(bytes, 20);
    info.width = ReadBeInt(bytes, 24);
    info.height = ReadBeInt(bytes, 28);
    info.levels = ReadBeInt(bytes, 32);

    if (magic != kBimageMagic)
    {
        error = "unsupported BIMAGE magic/version";
        return false;
    }

    if (info.textureType != 1 && info.textureType != 2)
    {
        error = "unsupported BIMAGE texture type";
        return false;
    }

    if (!KnownFormat(info.format) || info.colorFormat < 0 || info.colorFormat > 3)
    {
        error = "unsupported BIMAGE format identifier";
        return false;
    }

    if (info.width < 1 || info.width > kMaxDimension || info.height < 1 || info.height > kMaxDimension)
    {
        error = "BIMAGE dimensions exceed bounds";
        return false;
    }

    if (info.levels < 1 || info.levels > std::min(kMaxLevels, BitLength(std::max(info.width, info.height))))
    {
        error = "BIMAGE mip count exceeds dimensions/bounds";
        return false;
    }

    const int faces = info.textureType == 2 ? 6 : 1;

    if (faces == 6 && info.width != info.height)
    {
        error = "BIMAGE cube faces are not square";
        return false;
    }

    std::uint64_t decoded = 0;

    for (int level = 0; level < info.levels; ++level)
    {
        decoded += static_cast<std::uint64_t>(std::max(1, info.width >> level)) * static_cast<std::uint64_t>(std::max(1, info.height >> level)) * 4U *
                   static_cast<std::uint64_t>(faces);
    }

    if (decoded > kMaxDecodedBytes)
    {
        error = "BIMAGE decoded pixels exceed 64 MiB bound";
        return false;
    }

    std::size_t offset = kHeaderBytes;
    std::set<std::pair<int, int> > seen;
    const bool compressed = info.format == kBimageFormatDxt1 || info.format == kBimageFormatDxt5;

    for (int index = 0; index < info.levels * faces; ++index)
    {
        if (bytes.size() - offset < kLevelHeaderBytes)
        {
            error = "truncated BIMAGE mip header";
            return false;
        }

        BimageLevel mip;
        mip.level = ReadBeInt(bytes, offset);
        mip.face = ReadBeInt(bytes, offset + 4);
        const int storedWidth = ReadBeInt(bytes, offset + 8);
        const int storedHeight = ReadBeInt(bytes, offset + 12);
        const int size = ReadBeInt(bytes, offset + 16);
        offset += kLevelHeaderBytes;

        if (mip.level < 0 || mip.level >= info.levels || mip.face < 0 || mip.face >= faces || !seen.insert({ mip.level, mip.face }).second)
        {
            error = "invalid/duplicate BIMAGE mip level or face";
            return false;
        }

        mip.width = std::max(1, info.width >> mip.level);
        mip.height = std::max(1, info.height >> mip.level);
        // Native cube compression retains its padded 4x4 dimensions for tail levels.
        const int expectedWidth = faces == 6 && compressed ? std::max(4, mip.width) : mip.width;
        const int expectedHeight = faces == 6 && compressed ? std::max(4, mip.height) : mip.height;

        if (storedWidth != expectedWidth || storedHeight != expectedHeight)
        {
            error = "inconsistent BIMAGE mip dimensions";
            return false;
        }

        if (size < 0 || static_cast<std::uint64_t>(size) != StoredBytes(storedWidth, storedHeight, info.format))
        {
            error = "incorrect BIMAGE mip payload size";
            return false;
        }

        if (static_cast<std::size_t>(size) > bytes.size() - offset)
        {
            error = "truncated BIMAGE mip payload";
            return false;
        }

        mip.offset = offset;
        mip.bytes = static_cast<std::size_t>(size);
        offset += mip.bytes;
        info.mips.push_back(mip);
    }

    if (offset != bytes.size())
    {
        error = "trailing BIMAGE bytes";
        return false;
    }

    std::sort(info.mips.begin(), info.mips.end(), [](const BimageLevel & first, const BimageLevel & second)
              {
                  return std::pair(first.level, first.face) < std::pair(second.level, second.face);
              });
    output = std::move(info);
    return true;
}

bool DecodeBimage(std::span<const std::uint8_t> bytes, int level, RgbaImage & output, std::string & error)
{
    BimageInfo info;

    if (!ParseBimage(bytes, info, error))
    {
        return false;
    }

    if (info.textureType != 1)
    {
        error = "unsupported BIMAGE decode: cube texture";
        return false;
    }

    if (info.colorFormat != kBimageColorDefault && info.colorFormat != kBimageColorGreenAlpha)
    {
        error = "unsupported BIMAGE decode color format";
        return false;
    }

    if (info.format != kBimageFormatDxt1 && info.format != kBimageFormatDxt5)
    {
        error = "unsupported BIMAGE decode format";
        return false;
    }

    if (level < 0 || level >= info.levels)
    {
        error = "invalid BIMAGE decode mip level";
        return false;
    }

    const BimageLevel & mip = info.mips[static_cast<std::size_t>(level)];
    const bool dxt5 = info.format == kBimageFormatDxt5;
    const bool greenAlpha = info.colorFormat == kBimageColorGreenAlpha;
    RgbaImage image;
    image.Allocate(mip.width, mip.height);
    std::size_t offset = mip.offset;

    for (int blockY = 0; blockY < mip.height; blockY += 4)
    {
        for (int blockX = 0; blockX < mip.width; blockX += 4)
        {
            std::array<std::uint8_t, 8> alphas{};
            std::uint64_t alphaIndices = 0;

            if (dxt5)
            {
                alphas = AlphaPalette(bytes[offset], bytes[offset + 1]);

                for (int index = 0; index < 6; ++index)
                {
                    alphaIndices |= static_cast<std::uint64_t>(bytes[offset + 2 + static_cast<std::size_t>(index)]) << (8 * index);
                }

                offset += 8;
            }

            const std::uint16_t color0 = ReadLe16(bytes.data() + offset);
            const std::uint16_t color1 = ReadLe16(bytes.data() + offset + 2);
            const std::uint32_t colorIndices = ReadLe32(bytes.data() + offset + 4);
            offset += 8;
            const std::array<Rgba, 4> colors = ColorPalette(color0, color1, dxt5);

            for (int pixel = 0; pixel < 16; ++pixel)
            {
                const int x = blockX + pixel % 4;
                const int y = blockY + pixel / 4;

                if (x >= mip.width || y >= mip.height)
                {
                    continue;
                }

                Rgba texel = colors[(colorIndices >> (2 * pixel)) & 3U];

                if (dxt5)
                {
                    texel[3] = alphas[(alphaIndices >> (3 * pixel)) & 7U];
                }

                if (greenAlpha)
                {
                    texel = { 255, 255, 255, texel[1] };
                }

                std::copy(texel.begin(), texel.end(), image.At(x, y));
            }
        }
    }

    output = std::move(image);
    return true;
}

} // namespace assets
