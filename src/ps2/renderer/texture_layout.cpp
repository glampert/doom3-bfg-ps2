// ================================================================================================
// File: texture_layout.cpp
// Brief: Separate logical sampling extents from page-safe PSMCT32 upload storage.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/texture_layout.h"

#include <limits>

namespace ps2::tex
{
namespace
{

static constexpr int kPageWidth = 64;
static constexpr int kPageHeight = 32;
static constexpr size_t kPixelBytes = 4;

static bool IsPowerOfTwo(int value)
{
    return (value > 0) && ((value & (value - 1)) == 0);
}

static int PowerOfTwoExtent(int value, std::uint8_t & exponent)
{
    int extent = 1;
    exponent = 0;
    while (extent < value)
    {
        extent *= 2;
        ++exponent;
    }
    return extent;
}

static bool CanonicalLayout(int width, int height, Layout & output)
{
    if ((width < 1) || (height < 1) || (width > kMaxDimension) || (height > kMaxDimension))
    {
        return false;
    }

    Layout layout{};
    layout.width = width;
    layout.height = height;
    layout.potWidth = PowerOfTwoExtent(width, layout.widthLog2);
    layout.potHeight = PowerOfTwoExtent(height, layout.heightLog2);
    layout.stridePixels = (layout.potWidth < kPageWidth) ? kPageWidth : layout.potWidth;
    layout.storageHeight = (layout.potHeight < kPageHeight) ? kPageHeight : layout.potHeight;

    // PSMCT32 pages cover 64x32 texels. Full pages prevent GS swizzling from reaching the next owner.
    // The dimension limit bounds this product to one MiB, independently of the caller's stride.
    layout.bytes = static_cast<std::uint32_t>(layout.stridePixels * layout.storageHeight * 4);
    output = layout;
    return true;
}

static bool IsCanonical(const Layout & layout)
{
    Layout expected{};
    return CanonicalLayout(layout.width, layout.height, expected) &&
           (layout.potWidth == expected.potWidth) && (layout.potHeight == expected.potHeight) &&
           (layout.stridePixels == expected.stridePixels) &&
           (layout.storageHeight == expected.storageHeight) && (layout.bytes == expected.bytes) &&
           (layout.widthLog2 == expected.widthLog2) && (layout.heightLog2 == expected.heightLog2);
}

static bool ValidSampler(const Sampler & sampler)
{
    return ((sampler.filter == Filter::Nearest) || (sampler.filter == Filter::Linear)) &&
           ((sampler.wrapS == Wrap::Clamp) || (sampler.wrapS == Wrap::Repeat)) &&
           ((sampler.wrapT == Wrap::Clamp) || (sampler.wrapT == Wrap::Repeat));
}

static bool DisjointBuffers(const void * source, size_t sourceBytes,
                            const void * destination, size_t destinationBytes)
{
    const auto sourceBegin = reinterpret_cast<std::uintptr_t>(source);
    const auto destinationBegin = reinterpret_cast<std::uintptr_t>(destination);
    static constexpr auto kMaxAddress = std::numeric_limits<std::uintptr_t>::max();
    if ((sourceBytes > (kMaxAddress - sourceBegin)) ||
        (destinationBytes > (kMaxAddress - destinationBegin)))
    {
        return false;
    }
    return ((sourceBegin + sourceBytes) <= destinationBegin) ||
           ((destinationBegin + destinationBytes) <= sourceBegin);
}

} // namespace

bool MakeLayout(int width, int height, const Sampler & sampler, Layout & output)
{
    if (!ValidSampler(sampler) ||
        ((sampler.wrapS == Wrap::Repeat) && !IsPowerOfTwo(width)) ||
        ((sampler.wrapT == Wrap::Repeat) && !IsPowerOfTwo(height)))
    {
        return false;
    }
    return CanonicalLayout(width, height, output);
}

bool PadRgba32(const Layout & layout, const void * source, size_t sourceBytes,
               size_t sourceStrideBytes, void * destination, size_t destinationBytes)
{
    if (!IsCanonical(layout) || (source == nullptr) || (destination == nullptr) ||
        (destinationBytes < static_cast<size_t>(layout.bytes)))
    {
        return false;
    }

    const size_t rowBytes = static_cast<size_t>(layout.width) * kPixelBytes;
    const size_t priorRows = static_cast<size_t>(layout.height - 1);
    if ((sourceStrideBytes < rowBytes) ||
        ((priorRows > 0) && (sourceStrideBytes >
                             ((std::numeric_limits<size_t>::max() - rowBytes) / priorRows))))
    {
        return false;
    }
    const size_t requiredSourceBytes = (priorRows * sourceStrideBytes) + rowBytes;
    if ((sourceBytes < requiredSourceBytes) ||
        !DisjointBuffers(source, requiredSourceBytes, destination, static_cast<size_t>(layout.bytes)))
    {
        return false;
    }

    const auto * sourcePixels = static_cast<const std::uint8_t *>(source);
    auto * destinationPixels = static_cast<std::uint8_t *>(destination);
    for (int y = 0; y < layout.storageHeight; ++y)
    {
        const int sourceY = (y < layout.height) ? y : (layout.height - 1);
        const auto * sourceRow = sourcePixels + (static_cast<size_t>(sourceY) * sourceStrideBytes);
        auto * destinationRow = destinationPixels +
                                (static_cast<size_t>(y) * static_cast<size_t>(layout.stridePixels) * kPixelBytes);
        for (int x = 0; x < layout.stridePixels; ++x)
        {
            const int sourceX = (x < layout.width) ? x : (layout.width - 1);
            const auto * pixel = sourceRow + (static_cast<size_t>(sourceX) * kPixelBytes);
            auto * paddedPixel = destinationRow + (static_cast<size_t>(x) * kPixelBytes);
            paddedPixel[0] = pixel[0];
            paddedPixel[1] = pixel[1];
            paddedPixel[2] = pixel[2];
            paddedPixel[3] = static_cast<std::uint8_t>(
            ((static_cast<std::uint32_t>(pixel[3]) * 128U) + 127U) / 255U);
        }
    }
    return true;
}

} // namespace ps2::tex
