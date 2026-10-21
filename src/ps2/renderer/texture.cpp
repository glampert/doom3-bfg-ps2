// ================================================================================================
// File: texture.cpp
// Brief: Retain RGBA and index payloads under an EE cap and fence dirty uploads and LRU VRAM reuse.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/texture.h"
#include "ps2/renderer/cmd_buffer.h"
#include "ps2/renderer/ui_draw.h"
#include "ps2/renderer/vram.h"
#include "ps2/system/heap.h"
#include <idlib/precompiled.h>

namespace ps2::tex
{
namespace
{
struct Record
{
    Handle handle;
    Layout layout;
    Sampler sampler;
    void * pixels;
    vram::Allocation allocation;
    u64 lastUse;
    bool resident;
    bool dirty;
};

static Record s_records[vram::kMaxAllocations] = {};
static Stats s_stats = {};
static u32 s_budget = 0;
static u32 s_nextId = 0;
static u64 s_use = 0;
static bool s_initialized = false;

Record & Find(Handle handle)
{
    if (!s_initialized || handle.id == 0)
    {
        FatalError("invalid texture handle");
    }
    for (auto & record : s_records)
    {
        if (record.handle.id == handle.id)
        {
            return record;
        }
    }
    FatalError("stale texture handle: %u", handle.id);
}

void Fence()
{
    cmdbuf::Kick();
    cmdbuf::WaitIdle();
}

void ReleaseVram(Record & record)
{
    if (record.resident && !vram::Release(record.allocation))
    {
        FatalError("texture VRAM ownership mismatch");
    }
    record.resident = false;
    record.allocation = {};
}

Record * FreeRecord()
{
    for (auto & record : s_records)
    {
        if (record.handle.id == 0)
        {
            return &record;
        }
    }

    return nullptr;
}

Handle Publish(Record & record, const Layout & layout, Sampler sampler, void * pixels)
{
    if (s_nextId == 0xFFFFFFFFU)
    {
        FatalError("texture handle generation exhausted");
    }

    record = { { ++s_nextId }, layout, sampler, pixels, {}, 0, false, true };
    s_stats.pixelBytes += static_cast<u32>(layout.bytes);

    if (s_stats.pixelBytes > s_stats.peakPixelBytes)
    {
        s_stats.peakPixelBytes = s_stats.pixelBytes;
    }

    ++s_stats.textures;
    return record.handle;
}

// Allocates VRAM, evicting least recently used textures other than `keep` after retiring every
// queued TEX0 address. A new allocation is dirty until its payload is uploaded.
bool MakeResident(Record & record, const Record * keep)
{
    if (record.resident)
    {
        return true;
    }

    if (record.layout.bytes > vram::GetStats().capacityBytes)
    {
        return false;
    }

    if (!vram::Allocate(record.layout.bytes, record.allocation))
    {
        Fence();

        do
        {
            Record * oldest = nullptr;

            for (auto & candidate : s_records)
            {
                if (candidate.resident && &candidate != keep && (oldest == nullptr || candidate.lastUse < oldest->lastUse))
                {
                    oldest = &candidate;
                }
            }

            if (oldest == nullptr)
            {
                return false;
            }

            ReleaseVram(*oldest);
            ++s_stats.evictions;
        } while (!vram::Allocate(record.layout.bytes, record.allocation));
    }

    record.resident = true;
    record.dirty = true;
    return true;
}

gs::TextureBinding Upload(Record & record)
{
    const gs::TextureBinding binding = { static_cast<u32>(record.allocation.byteAddress / 4U), record.layout, record.sampler };

    if (record.dirty)
    {
        gs::Upload(binding, record.pixels);
        record.dirty = false;
        ++s_stats.uploads;
    }

    record.lastUse = ++s_use;
    return binding;
}
} // namespace

bool IsInitialized()
{
    return s_initialized;
}

Stats GetStats()
{
    return s_stats;
}

PS2_COLD_FUNC bool Init(u32 vramBytes, u32 pixelBudgetBytes)
{
    if (s_initialized)
    {
        FatalError("texture owner initialized twice");
    }
    if (!gs::IsInitialized() || pixelBudgetBytes == 0 || pixelBudgetBytes > 2U * 1024U * 1024U ||
        !vram::Init(gs::FixedVramBytes(), vramBytes))
    {
        return false;
    }
    s_stats = {};
    s_budget = pixelBudgetBytes;
    s_use = 0;
    s_initialized = true;
    return true;
}

Handle Create(int width, int height, Sampler sampler, const void * rgba, size_t sourceBytes, size_t strideBytes)
{
    if (!s_initialized)
    {
        FatalError("texture creation before initialization");
    }
    Layout layout = {};
    if (!MakeLayout(width, height, sampler, layout) || layout.bytes > s_budget - s_stats.pixelBytes)
    {
        return {};
    }
    Record * const freeRecord = FreeRecord();
    if (freeRecord == nullptr)
    {
        return {};
    }
    void * const pixels = heap::TryAlloc(layout.bytes, TAG_RENDER, 64);
    if (pixels == nullptr)
    {
        return {};
    }
    if (!PadRgba32(layout, rgba, sourceBytes, strideBytes, pixels, layout.bytes))
    {
        heap::Free(pixels);
        return {};
    }
    return Publish(*freeRecord, layout, sampler, pixels);
}

Handle CreateIndexed(int width, int height, PixelFormat format, Sampler sampler, const void * indices, size_t bytes)
{
    if (!s_initialized)
    {
        FatalError("texture creation before initialization");
    }

    Layout layout = {};

    // Paletted pages clamp by region at draw time; repeat would sample across placements.
    if (!MakeIndexedLayout(width, height, format, layout) || sampler.wrapS != Wrap::Clamp || sampler.wrapT != Wrap::Clamp ||
        indices == nullptr || bytes != layout.bytes || layout.bytes > s_budget - s_stats.pixelBytes)
    {
        return {};
    }

    Record * const freeRecord = FreeRecord();

    if (freeRecord == nullptr)
    {
        return {};
    }

    void * const pixels = heap::TryAlloc(layout.bytes, TAG_RENDER, 64);

    if (pixels == nullptr)
    {
        return {};
    }

    memcpy(pixels, indices, layout.bytes);
    return Publish(*freeRecord, layout, sampler, pixels);
}

bool Update(Handle handle, const void * rgba, size_t sourceBytes, size_t strideBytes)
{
    ui::Flush();
    auto & record = Find(handle);

    if (record.layout.format != PixelFormat::Rgba32)
    {
        return false;
    }

    // Uploads are synchronous, so the CPU payload has no outstanding DMA reader.
    // Its old VRAM contents remain usable until Bind drains and uploads the dirty version.
    if (!PadRgba32(record.layout, rgba, sourceBytes, strideBytes, record.pixels, record.layout.bytes))
    {
        return false;
    }
    record.dirty = true;
    return true;
}

void Destroy(Handle handle)
{
    if (handle.id == 0)
    {
        return;
    }
    ui::Flush();
    auto & record = Find(handle);
    Fence();
    ReleaseVram(record);
    heap::Free(record.pixels);
    s_stats.pixelBytes -= static_cast<u32>(record.layout.bytes);
    --s_stats.textures;
    record = {};
}

bool Bind(Handle handle, gs::TextureBinding & output)
{
    auto & record = Find(handle);

    if (!MakeResident(record, nullptr))
    {
        return false;
    }

    output = Upload(record);
    return true;
}

bool BindPair(Handle first, Handle second, gs::TextureBinding & firstOutput, gs::TextureBinding & secondOutput)
{
    auto & firstRecord = Find(first);
    auto & secondRecord = Find(second);

    if (&firstRecord == &secondRecord || !MakeResident(firstRecord, &secondRecord) || !MakeResident(secondRecord, &firstRecord))
    {
        return false;
    }

    // Both addresses are final before either upload, so neither binding can go stale.
    firstOutput = Upload(firstRecord);
    secondOutput = Upload(secondRecord);
    return true;
}

PS2_COLD_FUNC void Shutdown()
{
    if (!s_initialized)
    {
        return;
    }
    Fence();
    for (auto & record : s_records)
    {
        if (record.handle.id != 0)
        {
            ReleaseVram(record);
            heap::Free(record.pixels);
            record = {};
        }
    }
    vram::Shutdown();
    s_stats.pixelBytes = s_stats.textures = 0;
    s_budget = 0;
    s_initialized = false;
}
} // namespace ps2::tex
