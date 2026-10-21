// ================================================================================================
// File: texture.h
// Brief: Own bounded direct and paletted texture pixels and retire queued references before VRAM reuse.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once
#include "ps2/renderer/gs.h"

namespace ps2::renderer::tex
{
struct Handle
{
    u32 id = 0;
};

struct Stats
{
    u32 pixelBytes;
    u32 peakPixelBytes;
    u32 textures;
    u32 uploads;
    u32 evictions;
};

bool Init(u32 vramBytes, u32 pixelBudgetBytes);
void Shutdown();
bool IsInitialized();
Handle Create(int width, int height, Sampler sampler, const void * rgba, size_t sourceBytes, size_t strideBytes);
// Copies exactly MakeIndexedLayout's bytes of GS index data (PSMT4: even texel in the low nibble).
Handle CreateIndexed(int width, int height, PixelFormat format, Sampler sampler, const void * indices, size_t bytes);
bool Update(Handle handle, const void * rgba, size_t sourceBytes, size_t strideBytes);
void Destroy(Handle handle);
// Binding can synchronously drain pending draws, evict and upload. No address may survive that fence.
bool Bind(Handle handle, gs::TextureBinding & output);
// Both textures resident at once (a paletted page and its CLUT texture); neither evicts the other.
bool BindPair(Handle first, Handle second, gs::TextureBinding & firstOutput, gs::TextureBinding & secondOutput);
Stats GetStats();
} // namespace ps2::renderer::tex
