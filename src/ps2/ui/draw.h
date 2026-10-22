// ================================================================================================
// File: draw.h
// Brief: Activate native screen-space console drawing independently of logical worlds and OpenGL.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once
#include "ps2/renderer/gs.h"

class idDrawVert;
class idImage;
class idMaterial;

namespace ps2::ui
{
// Device ownership remains with the renderer; initialize after it, shut down before it.
bool Init();
void Shutdown();
bool IsInitialized();
static constexpr int kMaxVertices = 4096;
static constexpr int kMaxIndexes = 6144;
static constexpr int kMaxDraws = 128;

struct GeometryStats
{
    u32 peakVertices;
    u32 peakIndexes;
    u32 peakDraws;
    u32 flushes;
    u32 triangles;
    // Completed native SWF mask captures and the most masks simultaneously clipping geometry.
    u32 masks;
    u32 peakMasks;
};

GeometryStats GetGeometryStats();
// Write the complete returned span before the next allocation/draw. A flush consumes and recycles
// these CPU spans; no pointer or index handle may survive a flush or frame boundary.
idDrawVert * AllocTrisForImage(int vertices, const u16 * indexes, int indexCount,
                               const idImage * image, renderer::gs::BlendMode blend);
void DrawIndexed(const idDrawVert * vertices, int vertexCount, const u16 * indexes, int indexCount,
                 const idImage * image, renderer::gs::BlendMode blend);
void BeginGeometryFrame();
void EndGeometryFrame();
void Flush();
// Draws a virtual-screen rectangle from `image` with the tint and blend `material` takes under the
// current color and GL state, so a font's pre-rendered glyph draws like its atlas quad would.
void DrawImageRect(float x, float y, float width, float height, float s0, float t0, float s1, float t1,
                   const idImage * image, const idMaterial * material);
// Native SWF's zero-additive color transform maps its normal/add blends to straight RGBA GS draws.
// Scope it explicitly; ordinary UI callers retain their stricter state/color2 contract.
void BeginSwfDraw();
void EndSwfDraw();
// Masks collect native transformed fill triangles without drawing. A completed convex mask clips
// subsequent geometry until its display-entry key is removed; removal need not be in stack order.
void BeginSwfMask(const void * key);
void EndSwfMask();
void RemoveSwfMask(const void * key);
void ValidateSwfMaskState(int activeMasks, bool capturing);
} // namespace ps2::ui
