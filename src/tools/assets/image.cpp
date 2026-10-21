// ================================================================================================
// File: image.cpp
// Brief: Straight-alpha RGBA8 images, edge-clamped crops and exact-coverage area downscaling in
// premultiplied space, the reference operations every texture conversion starts from.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/image.h"

#include <algorithm>
#include <cmath>

namespace assets
{
namespace
{

struct Span
{
    int first = 0;
    int last = 0;
};

// Source texels [first, last] overlapped by destination texel `index`, with their coverage.
static Span Footprint(int index, double scale, int sourceExtent, std::vector<double> & weights)
{
    const double begin = index * scale;
    const double end = (index + 1) * scale;
    Span span;
    span.first = std::clamp(static_cast<int>(std::floor(begin)), 0, sourceExtent - 1);
    span.last = std::clamp(static_cast<int>(std::ceil(end)) - 1, span.first, sourceExtent - 1);
    weights.clear();

    for (int texel = span.first; texel <= span.last; ++texel)
    {
        const double low = std::max(begin, static_cast<double>(texel));
        const double high = std::min(end, static_cast<double>(texel + 1));
        weights.push_back(std::max(0.0, high - low));
    }

    return span;
}

} // namespace

void RgbaImage::Allocate(int newWidth, int newHeight)
{
    width = newWidth;
    height = newHeight;
    pixels.assign(static_cast<std::size_t>(newWidth) * static_cast<std::size_t>(newHeight) * 4U, 0);
}

const std::uint8_t * RgbaImage::At(int x, int y) const
{
    return &pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U];
}

std::uint8_t * RgbaImage::At(int x, int y)
{
    return &pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U];
}

RgbaImage Crop(const RgbaImage & source, const Rect & rect)
{
    RgbaImage output;
    output.Allocate(rect.width, rect.height);

    for (int y = 0; y < rect.height; ++y)
    {
        const int sourceY = std::clamp(rect.y + y, 0, source.height - 1);

        for (int x = 0; x < rect.width; ++x)
        {
            const int sourceX = std::clamp(rect.x + x, 0, source.width - 1);
            const std::uint8_t * texel = source.At(sourceX, sourceY);
            std::copy(texel, texel + 4, output.At(x, y));
        }
    }

    return output;
}

RgbaImage ResampleArea(const RgbaImage & source, int width, int height)
{
    RgbaImage output;
    output.Allocate(width, height);
    const double scaleX = static_cast<double>(source.width) / width;
    const double scaleY = static_cast<double>(source.height) / height;
    std::vector<double> weightsX;
    std::vector<double> weightsY;

    for (int y = 0; y < height; ++y)
    {
        const Span rows = Footprint(y, scaleY, source.height, weightsY);
        const std::vector<double> rowWeights = weightsY;

        for (int x = 0; x < width; ++x)
        {
            const Span columns = Footprint(x, scaleX, source.width, weightsX);
            double premultiplied[3] = {};
            double straight[3] = {};
            double alpha = 0.0;
            double area = 0.0;

            for (int row = rows.first; row <= rows.last; ++row)
            {
                for (int column = columns.first; column <= columns.last; ++column)
                {
                    const double weight = rowWeights[static_cast<std::size_t>(row - rows.first)] *
                                          weightsX[static_cast<std::size_t>(column - columns.first)];
                    const std::uint8_t * texel = source.At(column, row);
                    const double texelAlpha = texel[3] / 255.0;

                    for (int channel = 0; channel < 3; ++channel)
                    {
                        premultiplied[channel] += weight * texel[channel] * texelAlpha;
                        straight[channel] += weight * texel[channel];
                    }

                    alpha += weight * texelAlpha;
                    area += weight;
                }
            }

            std::uint8_t * texel = output.At(x, y);

            for (int channel = 0; channel < 3; ++channel)
            {
                const double value = alpha > 0.0 ? premultiplied[channel] / alpha : straight[channel] / area;
                texel[channel] = static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 255.0)));
            }

            texel[3] = static_cast<std::uint8_t>(std::lround(std::clamp(alpha / area * 255.0, 0.0, 255.0)));
        }
    }

    return output;
}

} // namespace assets
