// ================================================================================================
// File: masks.h
// Brief: Admit exact convex triangle masks and clip native UI geometry without stencil storage.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/ui/triangles.h"

namespace ps2::ui
{

struct MaskPoint
{
    float x;
    float y;
};

static constexpr int kMaxMaskPoints = 16;
static constexpr int kMaxActiveMasks = 4;
static constexpr int kMaxMaskTriangles = 64;
static constexpr int kMaxMaskClipVertices = 3 + kMaxMaskPoints * kMaxActiveMasks;

struct ConvexMask
{
    MaskPoint points[kMaxMaskPoints];
    int count;
};

class MaskBuilder
{
public:
    void Reset();

    // The triangles may use either winding. Degenerate triangles contribute no coverage. Rejection
    // remains latched until Reset; at most one incidence in each direction is permitted per edge.
    bool AddTriangle(const MaskPoint (&triangle)[3]);

    // Empty coverage is valid. Nonempty coverage must have exactly one convex boundary, with no
    // disconnected components, holes, overlap or nonconforming shared edges. Invalid is transactional.
    bool Finish(ConvexMask & output) const;

private:
    struct Edge
    {
        MaskPoint from;
        MaskPoint to;
        bool paired;
    };

    static constexpr int kMaxEdges = 3 * kMaxMaskTriangles;

    Edge m_edges[kMaxEdges]{};
    int m_edgeCount = 0;
    int m_triangleCount = 0;
    bool m_valid = true;
};

// All inputs are checked before clipping: normal/zero floats, positions within +/-32768 and UVs
// within +/-1023. Each mask is empty or a convex CCW polygon. Invalid leaves both outputs untouched;
// successful empty coverage only sets count to zero. Zero masks preserves the original triangle.
// Original edge / mask plane constraints reconstruct intersections without chained RGBA rounding.
bool ClipMaskTriangle(const Vertex (&input)[3], const ConvexMask * masks, int maskCount,
                      Vertex (&output)[kMaxMaskClipVertices], int & count);

} // namespace ps2::ui
