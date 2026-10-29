// ================================================================================================
// File: page_image.h
// Brief: Draw native UI images through cooked texture pages, splitting triangles at page bounds.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/renderer/render_system.h"
#include "ps2/renderer/texture.h"
#include "ps2/ui/page_geometry.h"

namespace ps2::ui
{

struct PageImageStats
{
    u32 loads = 0;
    u32 hits = 0;
    u32 evictions = 0;
    u32 triangles = 0;
    u32 cacheBytes = 0;
    u32 scratchBytes = 0;
    u32 metadataBytes = 0;
};

// A native UI image drawn through cooked pages, such as the resident GSTX paletted pages of
// PalettedImage. Triangles carry native atlas-normalized UVs.
class PagedImage
{
public:
    virtual ~PagedImage() = default;
    virtual bool IsOpen() const = 0;
    virtual PageImageStats GetStats() const = 0;
    virtual bool ValidateTriangle(const Vertex (&vertices)[3]) const = 0;

    // Screen blending needs each color channel's coverage as alpha (white or channel CLUTs).
    virtual bool SupportsScreen() const = 0;

    // Submission order is preserved. A required caller must handle false; a late failure does not
    // roll back already submitted pieces. Each piece draws every pass of the shading in turn.
    virtual bool DrawTriangle(const Vertex (&vertices)[3], const renderer::Shading & shading) = 0;
};

} // namespace ps2::ui
