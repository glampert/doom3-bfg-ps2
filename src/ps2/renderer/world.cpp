// ================================================================================================
// File: world.cpp
// Brief: Load a cooked world and draw its batches through world_lm.vcl (or world_strips.vcl): frame
// constants, the depth pass state, one program bind, then one REF'd batch per cooked packet.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/world.h"
#include "ps2/renderer/dma.h"
#include "ps2/renderer/dma_list.h"
#include "ps2/renderer/render_system.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <idlib/precompiled.h> // memTag_t for the world allocation
#include <kernel.h>

#include <cstdio>

namespace ps2::renderer::world
{

static_assert(kInputHalfQwords == kVuInputHalfQwords, "world_format.h and vu_memory.h disagree on the input half");

PS2_COLD_FUNC bool Load(const char * path, Level & level)
{
    level = {};
    std::FILE * file = std::fopen(path, "rb");

    if (file == nullptr) [[unlikely]]
    {
        Log(LogLevel::Error, "cannot open world %s\n", path);
        return false;
    }

    const bool seeked = std::fseek(file, 0, SEEK_END) == 0;
    const long length = seeked ? std::ftell(file) : -1;
    const bool rewound = std::fseek(file, 0, SEEK_SET) == 0;

    if (!seeked || !rewound || length <= 0 || static_cast<unsigned long>(length) > kMaxFileBytes) [[unlikely]]
    {
        std::fclose(file);
        Log(LogLevel::Error, "unusable world file size in %s\n", path);
        return false;
    }

    const u32 bytes = static_cast<u32>(length);
    void * storage = system::heap::TryAlloc(bytes, TAG_RENDER, kPacketAlignment);

    if (storage == nullptr) [[unlikely]]
    {
        std::fclose(file);
        Log(LogLevel::Error, "no memory for world %s (%u bytes)\n", path, bytes);
        return false;
    }

    const bool read = std::fread(storage, 1, bytes, file) == bytes;
    const bool closed = std::fclose(file) == 0;

    if (!read || !closed) [[unlikely]]
    {
        system::heap::Free(storage);
        Log(LogLevel::Error, "world %s refused: read failed\n", path);
        return false;
    }

    return Adopt(storage, bytes, path, level);
}

PS2_COLD_FUNC bool Adopt(void * storage, u32 bytes, const char * name, Level & level)
{
    level = {};
    const char * error = Validate(static_cast<const u8 *>(storage), bytes, level.sections);

    if (error != nullptr) [[unlikely]]
    {
        system::heap::Free(storage);
        level = {};
        Log(LogLevel::Error, "world %s refused: %s\n", name, error);
        return false;
    }

    // The packets are read by the DMA from here on: nothing the cache holds may land on them later.
    SyncDCache(storage, static_cast<u8 *>(storage) + bytes);

    level.storage = storage;
    level.bytes = bytes;

    const Sections & sections = level.sections;
    Log(LogLevel::Info, "[D3BFG] world %s: areas=%u surfaces=%u batches=%u portals=%u nodes=%u packets=%u bytes=%u\n", name,
        static_cast<u32>(sections.areaCount), static_cast<u32>(sections.surfaceCount), static_cast<u32>(sections.batchCount),
        static_cast<u32>(sections.portalCount), static_cast<u32>(sections.nodeCount), static_cast<u32>(sections.packetBytes), bytes);

    return true;
}

PS2_COLD_FUNC void Unload(Level & level)
{
    // A frame still in flight may read the packets: the caller has ended its frames.
    PS2_AssertMsg(!dma::IsInitialized() || !dma::InFlight(), "world unloaded while its packets may be in flight");

    if (level.storage != nullptr)
    {
        system::heap::Free(level.storage);
    }

    level = {};
}

vu::FrameConstants MakeFrameConstants(const math::Mat4 & viewProjection, gs::DrawContext context)
{
    vu::FrameConstants constants;
    constants.viewProjection = viewProjection;
    constants.gsScale        = gs::WindowScale();
    constants.gsScale.w      = 1.0f - vu::kClipShrink;
    constants.gsOffset       = gs::WindowOffset();
    constants.gsOffset.w     = vu::kClipDistanceScale;
    constants.clipScale      = { vu::kGuardBandScale, vu::kGuardBandScale, 1.0f, 0.0f };
    constants.stripTag       = gs::DrawingTag(gs::Primitive::TriangleStrip, gs::VertexFormat::ColorPosition, context);
    constants.listTag        = gs::DrawingTag(gs::Primitive::Triangle, gs::VertexFormat::ColorPosition, context);
    return constants;
}

void BeginPass(const Level & level, const math::Mat4 & viewProjection)
{
    const gs::DrawContext context = renderer::DrawingContext();
    dma::DisplayList & list = dma::List();

    list.SetFrameConstants(MakeFrameConstants(viewProjection, context));
    gs::BeginDepthPass(list, context);
    list.SetProgram(level.sections.indexedStrips ? vu::Program::WorldLightmapped : vu::Program::WorldStrips);
}

void DrawArea(const Level & level, u32 area)
{
    PS2_AssertMsg(level.storage != nullptr && area < level.sections.areaCount, "drawing an area of no loaded world");

    dma::DisplayList & list = dma::List();
    const Sections & sections = level.sections;
    const AreaRecord & record = sections.areas[area];

    for (u32 index = record.firstBatch; index < record.firstBatch + record.batchCount; ++index)
    {
        const BatchRecord & batch = sections.batches[index];
        list.DrawBatch(sections.packets + batch.packetOffset, batch.packetQwords);
    }
}

void EndPass()
{
    dma::DisplayList & list = dma::List();

    list.EndBatches();
    gs::EndDepthPass(list, renderer::DrawingContext());
}

void Draw(const Level & level, const math::Mat4 & viewProjection)
{
    PS2_AssertMsg(level.storage != nullptr, "drawing an unloaded world");

    BeginPass(level, viewProjection);

    for (u32 area = 0; area < level.sections.areaCount; ++area)
    {
        DrawArea(level, area);
    }

    EndPass();
}

} // namespace ps2::renderer::world
