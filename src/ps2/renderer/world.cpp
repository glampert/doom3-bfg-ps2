// ================================================================================================
// File: world.cpp
// Brief: Load a cooked world and draw its batches through world_lm.vusl (or world_strips.vusl): frame
// constants, the depth pass state, one program bind, then one REF'd batch per cooked packet.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/world.h"
#include "ps2/renderer/dma.h"
#include "ps2/renderer/dma_list.h"
#include "ps2/renderer/render_system.h"
#include "ps2/renderer/texture_format.h"
#include "ps2/renderer/world_textures.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <idlib/precompiled.h> // memTag_t for the world allocation
#include <kernel.h>

#include <cstdio>

namespace ps2::renderer::world
{

namespace
{

static bool s_texturesEnabled = true;
static bool s_lightmapsEnabled = true;
static Textures * s_passTextures = nullptr;
static TextureStats s_lastStats = {};

// The batches whose materials draw additive passes, queued until the pass's areas are all in:
// the diffuse textures then hold the arena first, and a pass whose texture finds no room left
// simply does not draw. Depth GEQUAL makes the order safe: a pass lands only where its own
// surface is the nearest. A full queue draws what it holds.
static constexpr u32 kMaxQueuedPassBatches = 512;
static u32 s_queuedPasses[kMaxQueuedPassBatches];
static u32 s_queuedPassCount = 0;
static const Level * s_passLevel = nullptr;

// PlanArea has begun the next pass's plan.
static bool s_planning = false;

// Whether a pass plans (PlanBatch) and steps over (AdvanceBatch) this batch: all but a textured
// pass's sky batches, which write depth alone.
static bool Planned(const Level & level, const BatchRecord & batch)
{
    return !(s_texturesEnabled && IsSky(level.textures, batch.material));
}

static void DrawQueuedPasses()
{
    dma::DisplayList & list = dma::List();
    const Sections & sections = s_passLevel->sections;

    for (u32 queued = 0; queued < s_queuedPassCount; ++queued)
    {
        const BatchRecord & batch = sections.batches[s_queuedPasses[queued]];
        const u32 passes = PassCount(s_passTextures, batch.material);

        for (u32 pass = 0; pass < passes; ++pass)
        {
            const void * state = nullptr;
            const void * color = nullptr;

            // Each batch again with the pass's texture, a constant colour and additive tags.
            if (PassBlocks(s_passTextures, list, batch.material, pass, state, color))
            {
                list.DrawBatch(sections.packets + batch.packetOffset, batch.packetQwords, state, kBatchStateBlockQwords, color, kPageStateBlockQwords);
            }
        }
    }

    s_queuedPassCount = 0;
}

} // namespace


static_assert(kInputHalfQwords == vu::kInputHalfQwords, "world_format.h and the world programs disagree on the input half");
static_assert(kBatchQwords == vu::kBatchLightmapOffset && kBatchStateQwords == vu::kBatchLightmapQwords + vu::kBatchStateQwords,
              "world_format.h and the world programs disagree on where the batch state starts");
static_assert(kBatchStateBlockQwords == 1U + vu::kBatchStateQwords && kPageStateBlockQwords == 1U + vu::kBatchLightmapQwords,
              "a state block is its VIFcode and the qwords it unpacks");

PS2_COLD_FUNC void * ReadWhole(const char * path, u32 limit, u32 & bytes)
{
    bytes = 0;
    std::FILE * file = std::fopen(path, "rb");

    if (file == nullptr) [[unlikely]]
    {
        Log(LogLevel::Error, "cannot open %s\n", path);
        return nullptr;
    }

    const bool seeked = std::fseek(file, 0, SEEK_END) == 0;
    const long length = seeked ? std::ftell(file) : -1;
    const bool rewound = std::fseek(file, 0, SEEK_SET) == 0;

    if (!seeked || !rewound || length <= 0 || static_cast<unsigned long>(length) > limit) [[unlikely]]
    {
        std::fclose(file);
        Log(LogLevel::Error, "unusable file size in %s\n", path);
        return nullptr;
    }

    const u32 size = static_cast<u32>(length);
    void * storage = system::heap::TryAlloc(size, TAG_RENDER, kPacketAlignment);

    if (storage == nullptr) [[unlikely]]
    {
        std::fclose(file);
        Log(LogLevel::Error, "no memory for %s (%u bytes)\n", path, size);
        return nullptr;
    }

    const bool read = std::fread(storage, 1, size, file) == size;
    const bool closed = std::fclose(file) == 0;

    if (!read || !closed) [[unlikely]]
    {
        system::heap::Free(storage);
        Log(LogLevel::Error, "%s refused: read failed\n", path);
        return nullptr;
    }

    bytes = size;
    return storage;
}

PS2_COLD_FUNC bool Load(const char * path, Level & level)
{
    level = {};
    u32 bytes = 0;
    void * storage = ReadWhole(path, kMaxFileBytes, bytes);
    return storage != nullptr && Adopt(storage, bytes, path, level);
}

PS2_COLD_FUNC bool LoadTextures(const char * path, Level & level)
{
    PS2_AssertMsg(level.storage != nullptr && level.textures == nullptr, "textures for no level, or twice");
    level.textures = OpenTextures(path, path, level.sections);
    return level.textures != nullptr;
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
    Log(LogLevel::Info, "[D3BFG] world %s: areas=%u surfaces=%u batches=%u portals=%u nodes=%u packets=%u bytes=%u merged_entities=%u\n", name,
        static_cast<u32>(sections.areaCount), static_cast<u32>(sections.surfaceCount), static_cast<u32>(sections.batchCount),
        static_cast<u32>(sections.portalCount), static_cast<u32>(sections.nodeCount), static_cast<u32>(sections.packetBytes), bytes,
        static_cast<u32>(sections.entityCount));

    return true;
}

PS2_COLD_FUNC void Unload(Level & level)
{
    // A frame still in flight may read the packets: the caller has ended its frames.
    PS2_AssertMsg(!dma::IsInitialized() || !dma::InFlight(), "world unloaded while its packets may be in flight");

    if (s_passTextures == level.textures)
    {
        s_passTextures = nullptr;
    }

    FreeTextures(level.textures);

    if (level.storage != nullptr)
    {
        system::heap::Free(level.storage);
    }

    level = {};
}

vu::FrameConstants MakeFrameConstants(const math::Mat4 & viewProjection, gs::DrawContext context)
{
    const math::Vec4 scale  = gs::WindowScale();
    const math::Vec4 offset = gs::WindowOffset();

    // The window state and the null tag are DisplayList::SetFrameConstants's.
    vu::FrameConstants constants = {};
    CopyQwords<sizeof(math::Mat4) / 16>(&constants.mvp, &viewProjection);
    constants.gsScale          = { scale.x, scale.y, scale.z };
    constants.clipShrink       = 1.0f - vu::kClipShrink;
    constants.gsOffset         = { offset.x, offset.y, offset.z };
    constants.clipDistance     = vu::kClipDistanceScale;
    constants.clipScale        = { vu::kGuardBandScale, vu::kGuardBandScale, 1.0f };
    constants.stripTag         = gs::DrawingTag(gs::Primitive::TriangleStrip, gs::VertexFormat::StColorPosition, context);
    constants.listTag          = gs::DrawingTag(gs::Primitive::Triangle, gs::VertexFormat::StColorPosition, context);
    constants.lightmapStripTag = gs::LightmapTag(gs::Primitive::TriangleStrip, gs::VertexFormat::StColorPosition, context);
    constants.lightmapListTag  = gs::LightmapTag(gs::Primitive::Triangle, gs::VertexFormat::StColorPosition, context);
    return constants;
}

void SetTexturesEnabled(bool enabled)
{
    s_texturesEnabled = enabled;
}

bool TexturesEnabled()
{
    return s_texturesEnabled;
}

void SetLightmapsEnabled(bool enabled)
{
    s_lightmapsEnabled = enabled;
}

bool LightmapsEnabled()
{
    return s_lightmapsEnabled;
}

const TextureStats & LastPassTextureStats()
{
    return s_lastStats;
}

void BeginPass(const Level & level, const math::Mat4 & viewProjection, float time, bool sky, int viewArea)
{
    s_passTextures = level.textures;
    s_passLevel = &level;
    s_queuedPassCount = 0;
    s_planning = false;

    const gs::DrawContext context = renderer::DrawingContext();
    dma::DisplayList & list = dma::List();

    // GS pixels per world unit at depth 1, for the mip LOD: clip x per view x (the length of the
    // matrix's first column, the view rotation being orthonormal) times the window scale.
    const float columnX = viewProjection.m[0][0] * viewProjection.m[0][0] + viewProjection.m[1][0] * viewProjection.m[1][0] +
                          viewProjection.m[2][0] * viewProjection.m[2][0];
    BeginTextures(s_passTextures, list, context, gs::WindowScale().x * math::Sqrtf(columnX), s_texturesEnabled, s_lightmapsEnabled, time,
                  viewArea);

    list.SetFrameConstants(MakeFrameConstants(viewProjection, context));

    // Under everything the pass draws, in the 2D environment's masked depth.
    if (sky)
    {
        DrawSkyBox(s_passTextures, list, context, viewProjection);
    }

    gs::BeginDepthPass(list, context);
    list.SetProgram(level.sections.indexedStrips ? vu::Program::WorldLightmapped : vu::Program::WorldStrips);
}

bool PlanArea(const Level & level, u32 area)
{
    PS2_AssertMsg(level.storage != nullptr && area < level.sections.areaCount, "planning an area of no loaded world");

    if (!s_planning)
    {
        BeginPlan(level.textures);
        s_planning = true;
    }

    PlanAreaTextures(level.textures, area);
    const AreaRecord & record = level.sections.areas[area];
    bool sky = false;

    for (u32 index = record.firstBatch; index < record.firstBatch + record.batchCount; ++index)
    {
        const BatchRecord & batch = level.sections.batches[index];

        // A textured pass draws a sky batch untextured and unlit, into depth.
        if (!Planned(level, batch))
        {
            sky = true;
            continue;
        }

        PlanBatch(level.textures, batch.material, batch.lightmapPage, s_texturesEnabled, s_lightmapsEnabled);
    }

    return sky;
}

void DrawAreaSky(const Level & level, u32 area)
{
    PS2_AssertMsg(level.storage != nullptr && area < level.sections.areaCount, "drawing an area of no loaded world");

    if (!s_texturesEnabled)
    {
        return;
    }

    dma::DisplayList & list = dma::List();
    const Sections & sections = level.sections;
    const AreaRecord & record = sections.areas[area];

    for (u32 index = record.firstBatch; index < record.firstBatch + record.batchCount; ++index)
    {
        const BatchRecord & batch = sections.batches[index];

        // Depth alone, and never a lightmap pass over the sky behind.
        if (IsSky(s_passTextures, batch.material))
        {
            list.DrawBatch(sections.packets + batch.packetOffset, batch.packetQwords, SkyDepthState(), kBatchStateBlockQwords,
                           PageState(nullptr, list, kNoLightmap), kPageStateBlockQwords);
        }
    }
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

        if (Planned(level, batch))
        {
            AdvanceBatch(s_passTextures);
        }

        const void * state = BatchState(s_passTextures, list, batch.material);

        if (state == nullptr)
        {
            continue;
        }

        const void * page = PageState(s_passTextures, list, batch.lightmapPage);
        list.DrawBatch(sections.packets + batch.packetOffset, batch.packetQwords, state, kBatchStateBlockQwords, page, kPageStateBlockQwords);
    }

    // The material passes wait for the end of the pass (s_queuedPasses).
    for (u32 index = record.firstBatch; index < record.firstBatch + record.batchCount; ++index)
    {
        if (PassCount(s_passTextures, sections.batches[index].material) == 0)
        {
            continue;
        }

        if (s_queuedPassCount == kMaxQueuedPassBatches)
        {
            DrawQueuedPasses();
        }

        s_queuedPasses[s_queuedPassCount++] = index;
    }
}

void EndPass()
{
    dma::DisplayList & list = dma::List();

    DrawQueuedPasses();
    list.EndBatches();
    gs::EndDepthPass(list, renderer::DrawingContext());
    s_lastStats = GetTextureStats(s_passTextures);
}

void Draw(const Level & level, const math::Mat4 & viewProjection, float time)
{
    PS2_AssertMsg(level.storage != nullptr, "drawing an unloaded world");

    bool sky = false;

    for (u32 area = 0; area < level.sections.areaCount; ++area)
    {
        sky = PlanArea(level, area) || sky;
    }

    BeginPass(level, viewProjection, time, sky, -1);

    for (u32 area = 0; area < level.sections.areaCount && sky; ++area)
    {
        DrawAreaSky(level, area);
    }

    for (u32 area = 0; area < level.sections.areaCount; ++area)
    {
        DrawArea(level, area);
    }

    EndPass();
}

} // namespace ps2::renderer::world
