// ================================================================================================
// File: texture_convert.cpp
// Brief: Convert one engine image into a GSTX texture: downscale each placement, choose 4- or
// 8-bit indices by measured error, share palettes between placements and pack the pages.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/texture_convert.h"
#include "assets/rect_pack.h"

#include <algorithm>
#include <cmath>

namespace assets
{
namespace
{

static bool Inside(const Rect & inner, const Rect & outer)
{
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

static bool Overlap(const Rect & first, const Rect & second)
{
    return first.x < second.x + second.width && second.x < first.x + first.width && first.y < second.y + second.height &&
           second.y < first.y + first.height;
}

// Downscaled size for one placement: the requested scale, then whatever keeps it within a page.
static void DestinationSize(const Rect & source, double scale, int maxExtent, int & width, int & height)
{
    const double limited = std::min({ scale, 1.0, static_cast<double>(maxExtent) / source.width,
                                      static_cast<double>(maxExtent) / source.height });
    width = std::clamp(static_cast<int>(std::lround(source.width * limited)), 1, maxExtent);
    height = std::clamp(static_cast<int>(std::lround(source.height * limited)), 1, maxExtent);
}

struct Work
{
    PlacementRequest request;
    RgbaImage pixels;
    ColorHistogram histogram;
    IndexFormat format = IndexFormat::T8;
    int group = 0;
    int clut = 0;
    std::vector<std::uint8_t> indices;
    QuantizeStats quality;
    // RMSE with a palette of its own, the reference a shared palette is held to.
    double soloRmse = 0.0;
    PackItem pack;
};

} // namespace

bool ConvertTexture(const RgbaImage & image, const std::vector<PlacementRequest> & requests, const ConvertPolicy & policy,
                    ConvertResult & output, std::string & error)
{
    const Rect bounds{ 0, 0, image.width, image.height };
    std::vector<PlacementRequest> placements = requests;

    if (placements.empty())
    {
        placements.push_back({ bounds, bounds, 0.0 });
    }

    if (policy.maxPageExtent < 128 || policy.maxPageExtent > kGstxMaxPageExtent || !(policy.scale > 0.0) ||
        placements.size() > static_cast<std::size_t>(kGstxMaxPlacements))
    {
        error = "conversion policy or placement count out of range";
        return false;
    }

    std::vector<Work> work(placements.size());

    for (std::size_t index = 0; index < placements.size(); ++index)
    {
        const PlacementRequest & request = placements[index];

        if (request.match.width < 1 || request.match.height < 1 || !Inside(request.match, request.source) ||
            !Inside(request.source, bounds))
        {
            error = "placement " + std::to_string(index) + " rectangles invalid for the source image";
            return false;
        }

        for (std::size_t other = 0; other < index; ++other)
        {
            if (Overlap(request.match, placements[other].match))
            {
                error = "placements " + std::to_string(other) + " and " + std::to_string(index) + " overlap";
                return false;
            }
        }

        Work & entry = work[index];
        entry.request = request;
        int width = 0;
        int height = 0;
        DestinationSize(request.source, request.scale > 0.0 ? request.scale : policy.scale, policy.maxPageExtent, width, height);
        const RgbaImage cropped = Crop(image, request.source);
        entry.pixels = width == cropped.width && height == cropped.height ? cropped : ResampleArea(cropped, width, height);
        entry.histogram.Add(entry.pixels);
    }

    ConvertResult result;
    result.alphaOnly = policy.alphaLevels > 1 && policy.alphaLevels <= 16 &&
                       std::all_of(work.begin(), work.end(), [](const Work & entry)
                                   {
                                       return entry.histogram.WhiteRgb();
                                   });
    std::vector<std::vector<PaletteColor> > palettes;
    std::vector<IndexFormat> paletteFormats;

    if (result.alphaOnly)
    {
        // Coverage-only images share one alpha ramp; 4-bit indices hold every level.
        ColorHistogram merged;

        for (Work & entry : work)
        {
            merged.Merge(entry.histogram);
            entry.format = IndexFormat::T4;
            entry.clut = 0;
        }

        palettes.push_back(BuildAlphaPalette(merged, policy.alphaLevels));
        paletteFormats.push_back(IndexFormat::T4);
    }
    else
    {
        for (Work & entry : work)
        {
            std::vector<std::uint8_t> indices;
            const QuantizeStats stats = MapToPalette(entry.pixels, BuildPalette(entry.histogram, 16), indices);
            entry.format = stats.rmse <= policy.t4MaxRmse && stats.maxError <= policy.t4MaxError ? IndexFormat::T4 : IndexFormat::T8;
            entry.soloRmse = entry.format == IndexFormat::T4 ? stats.rmse
                                                             : MapToPalette(entry.pixels, BuildPalette(entry.histogram, 256), indices).rmse;
        }

        for (const IndexFormat format : { IndexFormat::T8, IndexFormat::T4 })
        {
            std::vector<std::size_t> members;
            std::vector<const ColorHistogram *> histograms;

            for (std::size_t index = 0; index < work.size(); ++index)
            {
                if (work[index].format == format)
                {
                    members.push_back(index);
                    histograms.push_back(&work[index].histogram);
                }
            }

            if (members.empty())
            {
                continue;
            }

            const GroupPolicy & group = format == IndexFormat::T8 ? policy.group8 : policy.group4;
            std::vector<int> assignment(members.size());

            for (std::size_t index = 0; index < members.size(); ++index)
            {
                assignment[index] = static_cast<int>(index);
            }

            if (policy.sharePalettes)
            {
                assignment = GroupPalettes(histograms, group);
            }

            // The shared palette is rebuilt from every member's texels, which can move a member past
            // the error the merge estimate allowed; such a member leaves for a palette of its own.
            const auto BuildGroup = [&](int number)
            {
                ColorHistogram merged;

                for (std::size_t index = 0; index < members.size(); ++index)
                {
                    if (assignment[index] == number)
                    {
                        merged.Merge(work[members[index]].histogram);
                    }
                }

                return BuildPalette(merged, PaletteSize(format));
            };

            int groups = *std::max_element(assignment.begin(), assignment.end()) + 1;
            std::vector<std::vector<PaletteColor> > groupPalettes;

            for (int number = 0; number < groups; ++number)
            {
                groupPalettes.push_back(BuildGroup(number));
            }

            bool changed = true;

            while (changed)
            {
                changed = false;

                for (std::size_t index = 0; index < members.size(); ++index)
                {
                    const int number = assignment[index];
                    const bool shared = std::count(assignment.begin(), assignment.end(), number) > 1;
                    Work & entry = work[members[index]];

                    if (!shared)
                    {
                        continue;
                    }

                    const QuantizeStats stats = MapToPalette(entry.pixels, groupPalettes[static_cast<std::size_t>(number)], entry.indices);

                    if (stats.rmse <= std::max(group.maxRmse, entry.soloRmse + group.maxIncrease) &&
                        (group.maxError <= 0 || stats.maxError <= group.maxError))
                    {
                        continue;
                    }

                    assignment[index] = groups++;
                    groupPalettes[static_cast<std::size_t>(number)] = BuildGroup(number);
                    groupPalettes.push_back(BuildGroup(assignment[index]));
                    changed = true;
                }
            }

            for (int number = 0; number < groups; ++number)
            {
                for (std::size_t index = 0; index < members.size(); ++index)
                {
                    if (assignment[index] == number)
                    {
                        work[members[index]].clut = static_cast<int>(palettes.size());
                    }
                }

                palettes.push_back(groupPalettes[static_cast<std::size_t>(number)]);
                paletteFormats.push_back(format);
            }
        }
    }

    for (Work & entry : work)
    {
        entry.quality = MapToPalette(entry.pixels, palettes[static_cast<std::size_t>(entry.clut)], entry.indices);
    }

    // Pages per format: 8-bit pages first, then 4-bit, numbered across both.
    GstxTexture & texture = result.texture;
    texture.logicalWidth = image.width;
    texture.logicalHeight = image.height;

    for (const IndexFormat format : { IndexFormat::T8, IndexFormat::T4 })
    {
        std::vector<PackItem> items;
        std::vector<std::size_t> members;

        for (std::size_t index = 0; index < work.size(); ++index)
        {
            if (work[index].format == format)
            {
                items.push_back({ work[index].pixels.width, work[index].pixels.height });
                members.push_back(index);
            }
        }

        if (items.empty())
        {
            continue;
        }

        std::vector<PackedPage> packed;

        if (!PackPages(items, policy.maxPageExtent, policy.maxPageExtent, GsPageWidth(format), GsPageHeight(format), packed))
        {
            error = "placements do not fit the page limits";
            return false;
        }

        const std::size_t firstPage = texture.pages.size();
        std::vector<std::vector<std::uint8_t> > pageIndices;

        for (const PackedPage & page : packed)
        {
            GstxPage target;
            target.width = page.width;
            target.height = page.height;
            target.format = format;
            texture.pages.push_back(target);
            pageIndices.emplace_back(static_cast<std::size_t>(page.width) * static_cast<std::size_t>(page.height), 0);
        }

        for (std::size_t item = 0; item < items.size(); ++item)
        {
            Work & entry = work[members[item]];
            entry.pack = items[item];
            std::vector<std::uint8_t> & indices = pageIndices[static_cast<std::size_t>(entry.pack.page)];
            const int pageWidth = packed[static_cast<std::size_t>(entry.pack.page)].width;

            for (int y = 0; y < entry.pixels.height; ++y)
            {
                for (int x = 0; x < entry.pixels.width; ++x)
                {
                    indices[static_cast<std::size_t>(entry.pack.y + y) * static_cast<std::size_t>(pageWidth) + static_cast<std::size_t>(entry.pack.x + x)] =
                        entry.indices[static_cast<std::size_t>(y) * static_cast<std::size_t>(entry.pixels.width) + static_cast<std::size_t>(x)];
                }
            }

            entry.pack.page += static_cast<int>(firstPage);
        }

        for (std::size_t page = 0; page < packed.size(); ++page)
        {
            GstxPage & target = texture.pages[firstPage + page];
            target.data = PackIndices(pageIndices[page], target.width, target.height, format);
        }
    }

    for (std::size_t index = 0; index < palettes.size(); ++index)
    {
        texture.cluts.push_back({ paletteFormats[index], ClutTexels(palettes[index], paletteFormats[index]) });
    }

    for (const Work & entry : work)
    {
        PlacementResult placement;
        placement.placement.match = entry.request.match;
        placement.placement.source = entry.request.source;
        placement.placement.dest = { entry.pack.x, entry.pack.y, entry.pixels.width, entry.pixels.height };
        placement.placement.page = entry.pack.page;
        placement.placement.clut = entry.clut;
        placement.format = entry.format;
        placement.quality = entry.quality;
        texture.placements.push_back(placement.placement);
        result.placements.push_back(placement);
    }

    output = std::move(result);
    return true;
}

RgbaImage RenderGstx(const GstxTexture & texture)
{
    RgbaImage image;
    image.Allocate(texture.logicalWidth, texture.logicalHeight);

    for (const GstxPlacement & placement : texture.placements)
    {
        for (int y = placement.match.y; y < placement.match.y + placement.match.height; ++y)
        {
            for (int x = placement.match.x; x < placement.match.x + placement.match.width; ++x)
            {
                PaletteColor color;

                if (SampleGstx(texture, x + 0.5, y + 0.5, color))
                {
                    std::uint8_t * texel = image.At(x, y);
                    texel[0] = color.r;
                    texel[1] = color.g;
                    texel[2] = color.b;
                    texel[3] = color.a;
                }
            }
        }
    }

    return image;
}

} // namespace assets
