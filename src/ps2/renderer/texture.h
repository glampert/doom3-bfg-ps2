// ================================================================================================
// File: texture.h
// Brief: Own bounded direct texture pixels and retire queued references before VRAM reuse.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once
#include "ps2/renderer/gs.h"

namespace ps2::tex
{
struct Handle { u32 id = 0; };
struct Stats { u32 pixelBytes; u32 peakPixelBytes; u32 textures; u32 uploads; u32 evictions; };
bool Init(u32 vramBytes, u32 pixelBudgetBytes);
void Shutdown();
bool IsInitialized();
Handle Create(int width, int height, Sampler sampler, const void * rgba, size_t sourceBytes, size_t strideBytes);
bool Update(Handle handle, const void * rgba, size_t sourceBytes, size_t strideBytes);
void Destroy(Handle handle);
// Binding can synchronously drain pending draws, evict and upload. No address may survive that fence.
bool Bind(Handle handle, gs::TextureBinding & output);
Stats GetStats();
} // namespace ps2::tex
