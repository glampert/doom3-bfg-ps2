// ================================================================================================
// File: texture_convert.h
// Brief: Convert one engine image into a GSTX texture: downscale each placement, choose 4- or
// 8-bit indices by measured error, share palettes between placements and pack the pages.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "assets/gstx.h"
#include "assets/palette.h"

#include <string>
#include <vector>

namespace assets
{

struct PlacementRequest
{
    // Owns the triangles whose UV centroid falls inside it; disjoint from every other match.
    Rect match;
    // The texels that placement maps onto its page: the match plus the border bilinear reads.
    Rect source;
    // Overrides the policy scale for this placement when positive.
    double scale = 0.0;
};

struct ConvertPolicy
{
    // Destination size over source size, before the page limit; never enlarges.
    double scale = 0.5;
    int maxPageExtent = kGstxMaxPageExtent;
    // A placement takes 4-bit indices when its own 16-color palette stays within both limits.
    double t4MaxRmse = 3.0;
    int t4MaxError = 48;
    bool sharePalettes = true;
    GroupPolicy group8{ 256, 2.5, 0.5, 48 };
    GroupPolicy group4{ 16, 3.5, 0.75, 48 };
    // Coverage-only images (white RGB everywhere) share one alpha ramp with this many levels.
    int alphaLevels = 16;
};

struct PlacementResult
{
    GstxPlacement placement;
    IndexFormat format = IndexFormat::T8;
    // Converted texels against the downscaled reference, before the GS samples them.
    QuantizeStats quality;
};

struct ConvertResult
{
    GstxTexture texture;
    std::vector<PlacementResult> placements;
    bool alphaOnly = false;
};

// An empty request list converts the whole image as one placement.
bool ConvertTexture(const RgbaImage & image, const std::vector<PlacementRequest> & requests, const ConvertPolicy & policy,
                    ConvertResult & output, std::string & error);

// The logical image as the target would sample it: each match rectangle reconstructed from the
// converted pages, transparent elsewhere.
RgbaImage RenderGstx(const GstxTexture & texture);

} // namespace assets
