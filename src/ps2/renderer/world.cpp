// ================================================================================================
// File: world.cpp
// Brief: Load a cooked world and draw its batches through world_lm.vcl: frame constants, the depth
// pass state, one program bind, then one REF per batch whose tag also starts the previous one.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/world.h"
#include "ps2/renderer/dma.h"
#include "ps2/renderer/dma_list.h"
#include "ps2/renderer/render_system.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <idlib/precompiled.h> // memTag_t for the world allocation
#include <draw_primitives.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <kernel.h>

#include <cstdio>

namespace ps2::renderer::world
{
namespace
{

static_assert(kInputHalfQwords == kVuInputHalfQwords, "world_format.h and vu_memory.h disagree on the input half");

// z/w runs from +1 at the near plane to -1 at the far one; the program's ftoi4 multiplies by 16,
// so scale and offset 0xFFFF / 32 map it onto [0, 0xFFFF], larger nearer (GEQUAL).
static constexpr float kDepthScale = static_cast<float>(0xFFFF) / 32.0f;

// A triangle survives the guard band judgement out to |x/w| and |y/w| of 0.8, the reach of the
// 12.4 window coordinates (the visible screen ends near 0.13); the clipper cuts to that band.
static constexpr float kGuardBandScale = 1.0f / 0.8f;

// The clipper cuts this fraction of w inside each plane, so the survivors' own judgement agrees,
// and scales distances so the ftoi4 sign of a product of two survives (quake2-ps2's values).
static constexpr float kClipShrink = 0.001f;
static constexpr float kClipDistanceScale = 2048.0f * 2048.0f;

// PACKED RGBAQ then XYZ2 per vertex; NLOOP and EOP are the program's to fill.
u64 DrawingTag(u64 primitive, gs::DrawContext context)
{
    const u64 prim = GS_SET_PRIM(primitive, 1, 0, 0, 0, 0, 0, static_cast<u64>(context), 0);
    return GIF_SET_TAG(0, 0, 1, prim, GIF_FLG_PACKED, 2);
}

} // namespace

PS2_COLD_FUNC bool Load(const char * path, Level & level)
{
    level = {};
    std::FILE * file = std::fopen(path, "rb");

    if (file == nullptr)
    {
        Log(LogLevel::Error, "cannot open world %s\n", path);
        return false;
    }

    const bool seeked = std::fseek(file, 0, SEEK_END) == 0;
    const long length = seeked ? std::ftell(file) : -1;
    const bool rewound = std::fseek(file, 0, SEEK_SET) == 0;

    if (!seeked || !rewound || length <= 0 || static_cast<unsigned long>(length) > kMaxFileBytes)
    {
        std::fclose(file);
        Log(LogLevel::Error, "unusable world file size in %s\n", path);
        return false;
    }

    const u32 bytes = static_cast<u32>(length);
    void * storage = system::heap::TryAlloc(bytes, TAG_RENDER, kPacketAlignment);

    if (storage == nullptr)
    {
        std::fclose(file);
        Log(LogLevel::Error, "no memory for world %s (%u bytes)\n", path, bytes);
        return false;
    }

    const bool read = std::fread(storage, 1, bytes, file) == bytes;
    const bool closed = std::fclose(file) == 0;
    const char * error = read && closed ? Validate(static_cast<const u8 *>(storage), bytes, level.sections) : "read failed";

    if (error != nullptr)
    {
        system::heap::Free(storage);
        level = {};
        Log(LogLevel::Error, "world %s refused: %s\n", path, error);
        return false;
    }

    // The packets are read by the DMA from here on: nothing the cache holds may land on them later.
    SyncDCache(storage, static_cast<u8 *>(storage) + bytes);

    level.storage = storage;
    level.bytes = bytes;

    const Sections & sections = level.sections;
    Log(LogLevel::Info, "[D3BFG] world %s: areas=%u surfaces=%u batches=%u portals=%u nodes=%u packets=%u bytes=%u\n", path,
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
    constants.gsScale = { 2048.0f, 2048.0f, kDepthScale, 1.0f - kClipShrink };
    constants.gsOffset = { 2048.0f + static_cast<float>(gs::Width()) * 0.5f, 2048.0f + static_cast<float>(gs::Height()) * 0.5f, kDepthScale,
                           kClipDistanceScale };
    constants.clipScale = { kGuardBandScale, kGuardBandScale, 1.0f, 0.0f };
    constants.stripTag[0] = DrawingTag(PRIM_TRIANGLE_STRIP, context);
    constants.stripTag[1] = static_cast<u64>(GIF_REG_RGBAQ) | (static_cast<u64>(GIF_REG_XYZ2) << 4);
    constants.listTag[0] = DrawingTag(PRIM_TRIANGLE, context);
    constants.listTag[1] = constants.stripTag[1];
    return constants;
}

void Draw(const Level & level, const math::Mat4 & viewProjection)
{
    PS2_AssertMsg(level.storage != nullptr, "drawing an unloaded world");

    const gs::DrawContext context = renderer::DrawingContext();
    dma::DisplayList & list = dma::List();

    dma::Ensure(vu::kFrameConstantQwords);
    vu::EmitFrameConstants(list, MakeFrameConstants(viewProjection, context));
    gs::BeginDepthPass(context);

    dma::Ensure(vu::kBindQwords + 1U);
    const vu::MicroAddr entry = vu::Bind(list, vu::Program::WorldLightmapped);
    list.Cnt(0U, dma::VifBase(kVuInputBase), dma::VifOffset(kVuInputHalfQwords));

    // A packet ends with ITOP; the tag after it starts the program on it, which is the next
    // batch's REF (its VIFcodes run before its data lands in the other half) or a closing CNT.
    const Sections & sections = level.sections;
    u32 start = dma::kVifNop;

    for (u32 index = 0; index < sections.batchCount; ++index)
    {
        const BatchRecord & batch = sections.batches[index];
        dma::Ensure(1U);
        list.Ref(sections.packets + batch.packetOffset, batch.packetQwords, dma::kVifNop, start);
        start = dma::VifMscal(static_cast<u32>(entry));
    }

    dma::Ensure(1U);
    list.Cnt(0U, dma::kVifNop, start);
    gs::EndDepthPass(context);
}

} // namespace ps2::renderer::world
