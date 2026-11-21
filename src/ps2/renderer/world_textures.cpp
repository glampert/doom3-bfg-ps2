// ================================================================================================
// File: world_textures.cpp
// Brief: A cooked world's textures (the map's D3TX): residency in one VRAM arena with LRU eviction
// and in-chain uploads, and each batch's state, the VIF blocks the display list unpacks into the
// batch's input half for the world program to kick: the material's texture registers and drawing
// tags, and the lightmap page's TEX0 for the other context.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/world_textures.h"
#include "ps2/fnv.h"
#include "ps2/renderer/texture_format.h"
#include "ps2/renderer/vif.h"
#include "ps2/renderer/vram.h"
#include "ps2/renderer/vu.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <bit>
#include <gif_tags.h>
#include <gs_gp.h>
#include <kernel.h>

#include <idlib/precompiled.h> // TAG_RENDER

namespace ps2::renderer::world
{
namespace
{

// Each state block in its own cache lines (10 qwords rounded up to 64 bytes, a page's 3 to 4), so
// writing one back never touches another that a chain in flight reads.
static constexpr u32 kStateStrideQwords = 12;
static constexpr u32 kPageStrideQwords = 4;

// TEX0.TFX DECAL: the lightmap pass takes the texel's alpha alone, the frame its colour.
static constexpr u64 kTex0Decal = 1ULL << 35;

// VRAM the arena leaves to the UI's own textures (the console charset, the menus' pages).
static constexpr u32 kUiReserveBytes = 256U * 1024U;

static constexpr u32 kGsBlockBytes = 256;

struct Slot
{
    u32 base;       // block address of the image while resident
    u32 lastFrame;  // the last world pass that drew it
    u32 generation; // changes with every upload, so stale states are rewritten
    bool resident;
};

struct MaterialState
{
    u32 generation[2];
    u32 focal[2];
};

struct PageBlockState
{
    u32 generation[2];
};

struct Range
{
    u32 start;
    u32 count;
};

static u64 TestState(bool alphaTest)
{
    // Depth GEQUAL with writes (gs::BeginDepthPass); alpha-tested stages pass at alpha >= 0.5
    // (0x40 on the GS's 0..0x80 scale) and keep frame and depth where they fail, as id's
    // fragment discard does.
    return alphaTest ? GS_SET_TEST(1, 5, 0x40, 0, 0, 0, 1, 2) : GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, 2);
}

// log2 to about 1/200: the exponent plus a quadratic over the mantissa. TEX1.K holds 1/16 steps.
static float ApproxLog2(float value)
{
    const u32 bits = std::bit_cast<u32>(value);
    const float exponent = static_cast<float>(static_cast<int>((bits >> 23) & 0xFFU) - 127);
    const float mantissa = std::bit_cast<float>((bits & 0x007FFFFFU) | 0x3F800000U) - 1.0f;
    return exponent + mantissa * (1.3465f - 0.3465f * mantissa);
}

static void WriteState(qword_t * block, gs::DrawContext context, bool textured, u64 tex0, u64 tex1, u64 miptbp1, u64 miptbp2,
                       u64 clamp, u64 test)
{
    const u64 c = context == gs::DrawContext::Ctx1 ? 1U : 0U;

    block[0].sw[0] = static_cast<s32>(dma::kVifNop);
    block[0].sw[1] = static_cast<s32>(dma::kVifNop);
    block[0].sw[2] = static_cast<s32>(dma::VifStcycl(1U, 1U));
    block[0].sw[3] = static_cast<s32>(dma::VifUnpack(dma::UnpackFormat::V4_32, vu::kBatchStateQwords, vu::kBatchStateOffset, true, false));

    block[1].dw[0] = GIF_SET_TAG(6, 1, 0, 0, GIF_FLG_PACKED, 1);
    block[1].dw[1] = GIF_REG_AD;
    block[2].dw[0] = tex0;
    block[2].dw[1] = GS_REG_TEX0_1 + c;
    block[3].dw[0] = tex1;
    block[3].dw[1] = GS_REG_TEX1_1 + c;
    block[4].dw[0] = miptbp1;
    block[4].dw[1] = GS_REG_MIPTBP1_1 + c;
    block[5].dw[0] = miptbp2;
    block[5].dw[1] = GS_REG_MIPTBP2_1 + c;
    block[6].dw[0] = clamp;
    block[6].dw[1] = GS_REG_CLAMP_1 + c;
    block[7].dw[0] = test;
    block[7].dw[1] = GS_REG_TEST_1 + c;
    block[8] = gs::DrawingTag(gs::Primitive::TriangleStrip, gs::VertexFormat::StColorPosition, context, textured);
    block[9] = gs::DrawingTag(gs::Primitive::Triangle, gs::VertexFormat::StColorPosition, context, textured);

    SyncDCache(block, block + kStateStrideQwords);
}

// The page's A+D packet, with word 3 of its register qword the program's flag.
static void WritePage(qword_t * block, gs::DrawContext context, u64 tex0, bool lit)
{
    const u64 other = gs::OtherContext(context) == gs::DrawContext::Ctx1 ? 1U : 0U;

    block[0].sw[0] = static_cast<s32>(dma::kVifNop);
    block[0].sw[1] = static_cast<s32>(dma::kVifNop);
    block[0].sw[2] = static_cast<s32>(dma::VifStcycl(1U, 1U));
    block[0].sw[3] = static_cast<s32>(dma::VifUnpack(dma::UnpackFormat::V4_32, vu::kBatchLightmapQwords, vu::kBatchLightmapOffset, true, false));
    block[1].dw[0] = GIF_SET_TAG(1, 1, 0, 0, GIF_FLG_PACKED, 1);
    block[1].dw[1] = GIF_REG_AD;
    block[2].dw[0] = tex0;
    block[2].dw[1] = (GS_REG_TEX0_1 + other) | (lit ? 1ULL << 32 : 0U);

    SyncDCache(block, block + kPageStrideQwords);
}

// The untextured state per context: written once, never changed after; and the page state of a
// batch without a lightmap pass (the program never kicks it).
alignas(64) static qword_t s_untextured[2][kStateStrideQwords];
static bool s_untexturedWritten[2] = {};
alignas(64) static qword_t s_unlit[kPageStrideQwords];
static bool s_unlitWritten = false;

// The pass in progress, for worlds without textures too.
static gs::DrawContext s_context = gs::DrawContext::Ctx0;
static bool s_textured = true;
static bool s_lightmapped = true;

static const void * Unlit()
{
    if (!s_unlitWritten)
    {
        WritePage(s_unlit, gs::DrawContext::Ctx0, 0, false);
        s_unlitWritten = true;
    }

    return s_unlit;
}

static const void * Untextured(gs::DrawContext context)
{
    const int index = context == gs::DrawContext::Ctx1 ? 1 : 0;

    if (!s_untexturedWritten[index])
    {
        WriteState(s_untextured[index], context, false, 0, 0, 0, 0, 0, TestState(false));
        s_untexturedWritten[index] = true;
    }

    return s_untextured[index];
}

} // namespace

struct Textures
{
    void * storage;
    u32 bytes;
    tex::Pack pack;
    tex::Image * images;
    Slot * slots;
    MaterialState * states;
    qword_t * blocks;
    PageBlockState * pageStates;
    qword_t * pageBlocks;
    Range * free;
    u32 freeCount;
    gs::VramAllocation arena;
    u32 arenaBase;
    u32 arenaBlocks;
    u32 frame;
    u32 generation;
    u32 focalVersion;
    float focal;
    float focalLog2;
    TextureStats stats;
};

// ------------------------------------------------------------------------------------------------
// Loading
// ------------------------------------------------------------------------------------------------

PS2_COLD_FUNC static u32 NamesHash(const Sections & sections)
{
    u32 hash = kFnv1a32Basis;

    for (u32 index = 0; index < sections.materialCount; ++index)
    {
        const MaterialRecord & material = sections.materials[index];

        // Each name with its NUL, as the D3WD Strings section holds them.
        hash = Fnv1a32(sections.strings + material.nameOffset, material.nameBytes + 1U, hash);
    }

    return hash;
}

PS2_COLD_FUNC Textures * AdoptTextures(void * storage, u32 bytes, const char * name, const Sections & sections)
{
    auto * textures = static_cast<Textures *>(system::heap::TryAlloc(sizeof(Textures), TAG_RENDER, 64));
    const u32 imageCapacity = tex::kPackMaxTextures;
    auto * images = static_cast<tex::Image *>(system::heap::TryAlloc(sizeof(tex::Image) * imageCapacity, TAG_RENDER));
    const char * error = textures == nullptr || images == nullptr ? "no memory for its tables" : nullptr;
    tex::Pack pack = {};

    if (error == nullptr)
    {
        error = tex::ValidatePack(static_cast<const u8 *>(storage), bytes, pack, images, imageCapacity);
    }

    if (error == nullptr && (pack.materialCount != sections.materialCount || pack.materialsHash != NamesHash(sections)))
    {
        error = "its materials are not the world's";
    }

    if (error == nullptr && pack.pageCount != sections.lightmapPages)
    {
        error = "its lightmap pages are not the world's";
    }

    Slot * slots = nullptr;
    MaterialState * states = nullptr;
    qword_t * blocks = nullptr;
    PageBlockState * pageStates = nullptr;
    qword_t * pageBlocks = nullptr;
    Range * ranges = nullptr;

    if (error == nullptr)
    {
        const u32 textureCount = pack.textureCount > 0 ? pack.textureCount : 1U;
        slots = static_cast<Slot *>(system::heap::TryAlloc(sizeof(Slot) * textureCount, TAG_RENDER));
        states = static_cast<MaterialState *>(system::heap::TryAlloc(sizeof(MaterialState) * (pack.materialCount + 1U), TAG_RENDER));
        blocks = static_cast<qword_t *>(system::heap::TryAlloc(sizeof(qword_t) * kStateStrideQwords * 2U * (pack.materialCount + 1U), TAG_RENDER, 64));
        ranges = static_cast<Range *>(system::heap::TryAlloc(sizeof(Range) * (textureCount + 1U), TAG_RENDER));
        pageStates = static_cast<PageBlockState *>(system::heap::TryAlloc(sizeof(PageBlockState) * (pack.pageCount + 1U), TAG_RENDER));
        pageBlocks = static_cast<qword_t *>(system::heap::TryAlloc(sizeof(qword_t) * kPageStrideQwords * 2U * (pack.pageCount + 1U), TAG_RENDER, 64));
        error = slots == nullptr || states == nullptr || blocks == nullptr || ranges == nullptr || pageStates == nullptr || pageBlocks == nullptr
                    ? "no memory for its residency"
                    : nullptr;
    }

    if (error != nullptr) [[unlikely]]
    {
        Log(LogLevel::Error, "textures %s refused: %s\n", name, error);
        system::heap::Free(pageBlocks);
        system::heap::Free(pageStates);
        system::heap::Free(ranges);
        system::heap::Free(blocks);
        system::heap::Free(states);
        system::heap::Free(slots);
        system::heap::Free(images);
        system::heap::Free(textures);
        system::heap::Free(storage);
        return nullptr;
    }

    // The streams are DMA territory from here on.
    SyncDCache(storage, static_cast<u8 *>(storage) + bytes);

    *textures = {};
    textures->storage = storage;
    textures->bytes = bytes;
    textures->pack = pack;
    textures->images = images;
    textures->slots = slots;
    textures->states = states;
    textures->blocks = blocks;
    textures->pageStates = pageStates;
    textures->pageBlocks = pageBlocks;
    textures->free = ranges;

    for (u32 index = 0; index < pack.textureCount; ++index)
    {
        slots[index] = {};
    }

    for (u32 index = 0; index < pack.materialCount; ++index)
    {
        states[index] = {};
    }

    for (u32 index = 0; index < pack.pageCount; ++index)
    {
        pageStates[index] = {};
    }

    u32 vramBytes = 0;

    for (u32 index = 0; index < pack.textureCount; ++index)
    {
        vramBytes += images[index].blocks * kGsBlockBytes;
    }

    Log(LogLevel::Info, "[D3BFG] world textures %s: textures=%u materials=%u pages=%u vram=%u bytes=%u\n", name,
        static_cast<u32>(pack.textureCount), static_cast<u32>(pack.materialCount), static_cast<u32>(pack.pageCount), vramBytes, bytes);
    return textures;
}

PS2_COLD_FUNC void FreeTextures(Textures * textures)
{
    if (textures == nullptr)
    {
        return;
    }

    if (gs::IsVramLive(textures->arena))
    {
        gs::ReleaseVram(textures->arena);
    }

    system::heap::Free(textures->free);
    system::heap::Free(textures->pageBlocks);
    system::heap::Free(textures->pageStates);
    system::heap::Free(textures->blocks);
    system::heap::Free(textures->states);
    system::heap::Free(textures->slots);
    system::heap::Free(textures->images);
    system::heap::Free(textures->storage);
    system::heap::Free(textures);
}

// ------------------------------------------------------------------------------------------------
// The arena
// ------------------------------------------------------------------------------------------------

// The largest whole-page arena the texture range has left beyond the UI's reserve; every image
// leaves VRAM with the old one.
static void AllocateArena(Textures & textures)
{
    for (u32 slot = 0; slot < textures.pack.textureCount; ++slot)
    {
        textures.slots[slot].resident = false;
    }

    textures.arenaBlocks = 0;
    textures.freeCount = 0;
    textures.stats.residentBytes = 0;
    textures.stats.resident = 0;
    textures.stats.pagesResident = 0;
    const gs::VramStats & vram = gs::GetVramStats();
    const u32 available = vram.capacityBytes - vram.usedBytes;
    u32 bytes = available > kUiReserveBytes ? (available - kUiReserveBytes) / gs::kVramPageBytes * gs::kVramPageBytes : 0U;

    while (bytes > 0 && !gs::AllocateVram(bytes, textures.arena))
    {
        bytes -= gs::kVramPageBytes;
    }

    if (bytes == 0)
    {
        textures.arena = {};
        textures.stats.arenaBytes = 0;
        return;
    }

    textures.arenaBase = textures.arena.byteAddress / kGsBlockBytes;
    textures.arenaBlocks = bytes / kGsBlockBytes;
    textures.free[0] = { 0, textures.arenaBlocks };
    textures.freeCount = 1;
    textures.stats.arenaBytes = bytes;
}

// First fit over the free ranges, in block order.
static bool TakeBlocks(Textures & textures, u32 count, u32 & start)
{
    for (u32 index = 0; index < textures.freeCount; ++index)
    {
        Range & range = textures.free[index];

        if (range.count < count)
        {
            continue;
        }

        start = range.start;
        range.start += count;
        range.count -= count;

        if (range.count == 0)
        {
            for (u32 move = index + 1U; move < textures.freeCount; ++move)
            {
                textures.free[move - 1U] = textures.free[move];
            }

            --textures.freeCount;
        }

        return true;
    }

    return false;
}

// Returns blocks to the sorted free list, merging with their neighbours.
static void GiveBlocks(Textures & textures, u32 start, u32 count)
{
    u32 at = 0;

    while (at < textures.freeCount && textures.free[at].start < start)
    {
        ++at;
    }

    for (u32 move = textures.freeCount; move > at; --move)
    {
        textures.free[move] = textures.free[move - 1U];
    }

    textures.free[at] = { start, count };
    ++textures.freeCount;

    if (at + 1U < textures.freeCount && textures.free[at].start + textures.free[at].count == textures.free[at + 1U].start)
    {
        textures.free[at].count += textures.free[at + 1U].count;

        for (u32 move = at + 2U; move < textures.freeCount; ++move)
        {
            textures.free[move - 1U] = textures.free[move];
        }

        --textures.freeCount;
    }

    if (at > 0 && textures.free[at - 1U].start + textures.free[at - 1U].count == textures.free[at].start)
    {
        textures.free[at - 1U].count += textures.free[at].count;

        for (u32 move = at + 1U; move < textures.freeCount; ++move)
        {
            textures.free[move - 1U] = textures.free[move];
        }

        --textures.freeCount;
    }
}

// Places the image, evicting the least recently drawn ones this pass has not drawn: their last
// readers are earlier chains, which finish before this frame's upload runs.
static bool MakeResident(Textures & textures, dma::DisplayList & list, u32 index)
{
    Slot & slot = textures.slots[index];
    const tex::Image & image = textures.images[index];
    u32 start = 0;

    while (!TakeBlocks(textures, image.blocks, start))
    {
        u32 victim = textures.pack.textureCount;

        for (u32 candidate = 0; candidate < textures.pack.textureCount; ++candidate)
        {
            const Slot & other = textures.slots[candidate];

            if (other.resident && other.lastFrame != textures.frame &&
                (victim == textures.pack.textureCount || other.lastFrame < textures.slots[victim].lastFrame))
            {
                victim = candidate;
            }
        }

        if (victim == textures.pack.textureCount)
        {
            return false;
        }

        Slot & evicted = textures.slots[victim];
        GiveBlocks(textures, evicted.base - textures.arenaBase, textures.images[victim].blocks);
        evicted.resident = false;
        textures.stats.residentBytes -= textures.images[victim].blocks * kGsBlockBytes;
        --textures.stats.resident;
        textures.stats.pagesResident -= textures.images[victim].usage == tex::ImageUsage::Lightmap ? 1U : 0U;
        ++textures.stats.evictions;
    }

    slot.base = textures.arenaBase + start;
    slot.resident = true;
    slot.generation = ++textures.generation;
    list.UploadImage(image.stream, image.streamQwords, slot.base);
    textures.stats.residentBytes += image.blocks * kGsBlockBytes;
    ++textures.stats.resident;
    textures.stats.pagesResident += image.usage == tex::ImageUsage::Lightmap ? 1U : 0U;
    ++textures.stats.uploads;
    return true;
}

// ------------------------------------------------------------------------------------------------
// Passes
// ------------------------------------------------------------------------------------------------

void BeginTextures(Textures * textures, gs::DrawContext context, float focal, bool textured, bool lightmapped)
{
    s_context = context;
    s_textured = textured;
    s_lightmapped = lightmapped;

    if (textures == nullptr)
    {
        return;
    }

    ++textures->frame;

    if (!gs::IsVramLive(textures->arena))
    {
        AllocateArena(*textures);
    }

    if (focal != textures->focal)
    {
        textures->focal = focal;
        textures->focalLog2 = ApproxLog2(focal > 1.0f ? focal : 1.0f);
        ++textures->focalVersion;
    }
}

const void * BatchState(Textures * textures, dma::DisplayList & list, u32 material)
{
    if (textures == nullptr || !s_textured)
    {
        return Untextured(s_context);
    }

    PS2_AssertMsg(material < textures->pack.materialCount, "batch material outside the texture package");
    const tex::PackMaterial & record = textures->pack.materials[material];

    if (record.texture == tex::kPackHidden)
    {
        return nullptr;
    }

    if (record.texture == tex::kPackNoTexture)
    {
        return Untextured(s_context);
    }

    Slot & slot = textures->slots[record.texture];

    if (!slot.resident && (textures->arenaBlocks == 0 || !MakeResident(*textures, list, record.texture)))
    {
        ++textures->stats.untexturedFallbacks;
        return Untextured(s_context);
    }

    slot.lastFrame = textures->frame;
    const int index = s_context == gs::DrawContext::Ctx1 ? 1 : 0;
    MaterialState & state = textures->states[material];
    qword_t * const block = textures->blocks + (material * 2U + static_cast<u32>(index)) * kStateStrideQwords;

    // Only this context's block is rewritten: the chain in flight drew the other one.
    if (state.generation[index] != slot.generation || state.focal[index] != textures->focalVersion)
    {
        const tex::Image & image = textures->images[record.texture];
        const float widthLog2 = static_cast<float>((image.tex0 >> 26) & 0xFU);
        const float heightLog2 = static_cast<float>((image.tex0 >> 30) & 0xFU);

        // LOD = log2(w) + K, the texels a pixel spans at depth w: the material's UV density times
        // the texture's size, over the pixels a unit spans at depth 1.
        const float k = record.uvDensityLog2 + 0.5f * (widthLog2 + heightLog2) - textures->focalLog2;
        int fixed = static_cast<int>(k * 16.0f + (k >= 0.0f ? 0.5f : -0.5f));
        fixed = fixed < -2048 ? -2048 : fixed > 2047 ? 2047 : fixed;
        const u64 tex1 = image.tex1 | (static_cast<u64>(static_cast<u32>(fixed) & 0xFFFU) << 32);
        const bool alphaTest = (textures->pack.textures[record.texture].flags & tex::kPackFlagAlphaTest) != 0;

        WriteState(block, s_context, true, tex::RelocateTex0(image.tex0, slot.base), tex1, tex::RelocateMiptbp(image.miptbp1, slot.base),
                   tex::RelocateMiptbp(image.miptbp2, slot.base), image.clamp, TestState(alphaTest));
        state.generation[index] = slot.generation;
        state.focal[index] = textures->focalVersion;
    }

    return block;
}

const void * PageState(Textures * textures, dma::DisplayList & list, u32 page)
{
    if (textures == nullptr || !s_lightmapped || page == kNoLightmap)
    {
        return Unlit();
    }

    PS2_AssertMsg(page < textures->pack.pageCount, "batch page outside the texture package");
    const u32 texture = textures->pack.pages[page];
    Slot & slot = textures->slots[texture];

    if (!slot.resident && (textures->arenaBlocks == 0 || !MakeResident(*textures, list, texture)))
    {
        ++textures->stats.unlitFallbacks;
        return Unlit();
    }

    slot.lastFrame = textures->frame;
    const int index = s_context == gs::DrawContext::Ctx1 ? 1 : 0;
    PageBlockState & state = textures->pageStates[page];
    qword_t * const block = textures->pageBlocks + (page * 2U + static_cast<u32>(index)) * kPageStrideQwords;

    // As with materials, only this context's block is rewritten.
    if (state.generation[index] != slot.generation)
    {
        WritePage(block, s_context, tex::RelocateTex0(textures->images[texture].tex0, slot.base) | kTex0Decal, true);
        state.generation[index] = slot.generation;
    }

    return block;
}

TextureStats GetTextureStats(const Textures * textures)
{
    return textures != nullptr ? textures->stats : TextureStats{};
}

} // namespace ps2::renderer::world
