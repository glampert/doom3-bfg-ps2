// ================================================================================================
// File: rect_pack.h
// Brief: Pack texture placements into bounded pages with the least area, so converted UI pages
// waste little EE memory, VRAM or disc beside the images they hold.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <vector>

namespace assets
{

struct PackItem
{
    int width = 0;
    int height = 0;
    // Outputs.
    int page = -1;
    int x = -1;
    int y = -1;
};

struct PackedPage
{
    int width = 0;
    int height = 0;
};

// Packs every item without rotation into pages of at most maxWidth x maxHeight. Page widths are
// multiples of widthStep and heights of heightStep; each page takes the smallest such area that
// holds the items assigned to it. Fails if an item alone exceeds the page bounds.
bool PackPages(std::vector<PackItem> & items, int maxWidth, int maxHeight, int widthStep, int heightStep,
               std::vector<PackedPage> & pages);

} // namespace assets
