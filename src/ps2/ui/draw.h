// ================================================================================================
// File: draw.h
// Brief: Activate native screen-space console drawing independently of logical worlds and OpenGL.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/common.h"
#include "ps2/renderer/gs.h"

class idDrawVert;
class idImage;
class idMaterial;

namespace ps2::ui
{

// Device ownership remains with the renderer; initialize after it, shut down before it.
PS2_COLD_FUNC bool Init();
PS2_COLD_FUNC void Shutdown();
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

    // Captures the convex bridge could not take, drawn into the GS depth buffer instead.
    u32 depthMasks;

    // Native line-mode segments drawn (SWF shape outlines).
    u32 lines;

    // Source primitives drawn with screen blending, and with a color transform's additive term.
    u32 screen;
    u32 colorTerms;
};

const GeometryStats & GetGeometryStats();

void BeginGeometryFrame();
void EndGeometryFrame();
void Flush();

// Write the complete returned span before the next allocation/draw. A flush consumes and recycles
// these CPU spans; no pointer or index handle may survive a flush or frame boundary.
idDrawVert * AllocTrisForImage(int vertices, const u16 * indexes, int indexCount,
                               const idImage * image, renderer::gs::BlendMode blend);

void DrawIndexed(const idDrawVert * vertices, int vertexCount, const u16 * indexes, int indexCount,
                 const idImage * image, renderer::gs::BlendMode blend);

// Draws a virtual-screen rectangle from `image` with the tint and blend `material` takes under the
// current color and GL state, so a font's pre-rendered glyph draws like its atlas quad would.
void DrawImageRect(float x, float y, float width, float height, float s0, float t0, float s1, float t1,
                   const idImage * image, const idMaterial * material);

// Native SWF draws: normal, additive and screen blends, color transform terms and line-mode
// outlines map to GS passes. Scope it explicitly; ordinary UI callers retain their stricter
// state/color2 contract.
void BeginSwfDraw();
void EndSwfDraw();

// Masks collect native transformed fill triangles and outline segments without drawing. A completed
// convex mask clips subsequent geometry on the CPU until its display-entry key is removed; removal
// need not be in stack order. Any other mask (concave, holed, disconnected, stroked or large) goes
// into the GS depth buffer, which holds one at a time beside the convex masks.
void BeginSwfMask(const void * key);
void EndSwfMask();
void RemoveSwfMask(const void * key);
void ValidateSwfMaskState(int activeMasks, bool capturing);

} // namespace ps2::ui
