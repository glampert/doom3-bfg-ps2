// ================================================================================================
// File: rect_pack.cpp
// Brief: Pack texture placements into bounded pages with the least area, so converted UI pages
// waste little EE memory, VRAM or disc beside the images they hold.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/rect_pack.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <numeric>

namespace assets
{
namespace
{

struct Segment
{
    int x = 0;
    int y = 0;
    int width = 0;
};

struct Placement
{
    std::size_t item = 0;
    int x = 0;
    int y = 0;
};

static int RoundUp(int value, int step)
{
    return (value + step - 1) / step * step;
}

// Skyline bottom-left: each item goes where its top edge ends lowest, ties to the left. Items that
// would cross maxHeight are left for a later page. Returns the used height.
static int Skyline(const std::vector<PackItem> & items, const std::vector<std::size_t> & order, int width, int maxHeight,
                   std::vector<Placement> & placed)
{
    std::vector<Segment> skyline = { { 0, 0, width } };
    int used = 0;
    placed.clear();

    for (const std::size_t index : order)
    {
        const PackItem & item = items[index];
        int bestY = std::numeric_limits<int>::max();
        int bestX = 0;
        std::size_t bestSegment = skyline.size();

        for (std::size_t segment = 0; segment < skyline.size(); ++segment)
        {
            const int x = skyline[segment].x;

            if (x + item.width > width)
            {
                break;
            }

            // The item rests on the highest segment it spans.
            int y = 0;
            int covered = 0;

            for (std::size_t span = segment; span < skyline.size() && covered < item.width; ++span)
            {
                y = std::max(y, skyline[span].y);
                covered += skyline[span].width;
            }

            if (y + item.height <= maxHeight && y < bestY)
            {
                bestY = y;
                bestX = x;
                bestSegment = segment;
            }
        }

        if (bestSegment == skyline.size())
        {
            continue;
        }

        placed.push_back({ index, bestX, bestY });
        used = std::max(used, bestY + item.height);
        // Replace the covered segments with the item's top edge.
        const Segment top = { bestX, bestY + item.height, item.width };
        std::vector<Segment> next;

        for (const Segment & segment : skyline)
        {
            const int end = segment.x + segment.width;

            if (end <= top.x || segment.x >= top.x + top.width)
            {
                next.push_back(segment);
                continue;
            }

            if (segment.x < top.x)
            {
                next.push_back({ segment.x, segment.y, top.x - segment.x });
            }

            if (end > top.x + top.width)
            {
                next.push_back({ top.x + top.width, segment.y, end - top.x - top.width });
            }
        }

        next.push_back(top);
        std::sort(next.begin(), next.end(), [](const Segment & first, const Segment & second)
                  {
                      return first.x < second.x;
                  });
        // Merge equal-height neighbors so later items see wide flat spans.
        skyline.clear();

        for (const Segment & segment : next)
        {
            if (!skyline.empty() && skyline.back().y == segment.y && skyline.back().x + skyline.back().width == segment.x)
            {
                skyline.back().width += segment.width;
            }
            else
            {
                skyline.push_back(segment);
            }
        }
    }

    return used;
}

} // namespace

bool PackPages(std::vector<PackItem> & items, int maxWidth, int maxHeight, int widthStep, int heightStep,
               std::vector<PackedPage> & pages)
{
    pages.clear();

    if (widthStep < 1 || heightStep < 1 || maxWidth < widthStep || maxHeight < heightStep)
    {
        return false;
    }

    for (const PackItem & item : items)
    {
        if (item.width < 1 || item.height < 1 || item.width > maxWidth || item.height > maxHeight)
        {
            return false;
        }
    }

    // Tallest first, then widest: the skyline's usual order for mixed sizes.
    std::vector<std::size_t> remaining(items.size());
    std::iota(remaining.begin(), remaining.end(), std::size_t{ 0 });
    std::sort(remaining.begin(), remaining.end(), [&](std::size_t first, std::size_t second)
              {
                  if (items[first].height != items[second].height)
                  {
                      return items[first].height > items[second].height;
                  }

                  if (items[first].width != items[second].width)
                  {
                      return items[first].width > items[second].width;
                  }

                  return first < second;
              });

    while (!remaining.empty())
    {
        // Prefer the smallest page that takes everything left; otherwise fill the widest page.
        std::vector<Placement> best;
        long long bestArea = std::numeric_limits<long long>::max();
        int bestWidth = 0;
        int bestHeight = 0;

        for (int width = widthStep; width <= maxWidth; width += widthStep)
        {
            std::vector<Placement> placed;
            const int height = Skyline(items, remaining, width, maxHeight, placed);

            if (placed.size() != remaining.size())
            {
                continue;
            }

            const long long area = static_cast<long long>(width) * RoundUp(height, heightStep);

            if (area < bestArea)
            {
                bestArea = area;
                best = placed;
                bestWidth = width;
                bestHeight = RoundUp(height, heightStep);
            }
        }

        if (best.empty())
        {
            const int width = maxWidth / widthStep * widthStep;
            const int height = Skyline(items, remaining, width, maxHeight, best);

            if (best.empty())
            {
                return false;
            }

            bestWidth = width;
            bestHeight = std::min(RoundUp(height, heightStep), maxHeight);
        }

        const int page = static_cast<int>(pages.size());
        pages.push_back({ bestWidth, bestHeight });
        std::vector<bool> taken(items.size(), false);

        for (const Placement & placement : best)
        {
            items[placement.item].page = page;
            items[placement.item].x = placement.x;
            items[placement.item].y = placement.y;
            taken[placement.item] = true;
        }

        std::erase_if(remaining, [&](std::size_t index)
                      {
                          return taken[index];
                      });
    }

    return true;
}

} // namespace assets
