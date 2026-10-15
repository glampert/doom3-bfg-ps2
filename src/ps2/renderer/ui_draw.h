// ================================================================================================
// File: ui_draw.h
// Brief: Activate native screen-space console drawing independently of logical worlds and OpenGL.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once
#include "ps2/renderer/gs.h"

class idDrawVert;
class idImage;

namespace ps2::ui
{
// Device ownership remains with rs; initialize after it, shut down before it.
bool Init();
void Shutdown();
bool IsInitialized();
static constexpr int kMaxVertices = 4096;
static constexpr int kMaxIndexes = 6144;
static constexpr int kMaxDraws = 128;
struct GeometryStats { u32 peakVertices; u32 peakIndexes; u32 peakDraws; u32 flushes; u32 triangles; };
GeometryStats GetGeometryStats();
// Write the complete returned span before the next allocation/draw. A flush consumes and recycles
// these CPU spans; no pointer or index handle may survive a flush or frame boundary.
idDrawVert * AllocTrisForImage(int vertices, const u16 * indexes, int indexCount,
                             const idImage * image, gs::BlendMode blend);
void DrawIndexed(const idDrawVert * vertices, int vertexCount, const u16 * indexes, int indexCount,
                 const idImage * image, gs::BlendMode blend);
void BeginGeometryFrame();
void Flush();
// Native SWF's zero-additive premultiplied shader profile maps to straight RGBA GS draws.
// Scope it explicitly; ordinary UI callers retain their stricter state/color2 contract.
void BeginSwfDraw();
void EndSwfDraw();
} // namespace ps2::ui
