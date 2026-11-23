// ================================================================================================
// File: world_textures.cpp
// Brief: A cooked world's textures (the map's D3TX): residency in one VRAM arena with LRU eviction
// and in-chain uploads, and each batch's state, the VIF blocks the display list unpacks into the
// batch's input half for the world program to kick: the material's texture registers and drawing
// tags, and the lightmap page's TEX0 for the other context, or for a material pass the pass's
// texture with blending tags and its colour, evaluated once a frame where time drives it; and the
// sky box, drawn under the pass from the cooked cube faces. A pass may plan its textures first:
// they go in before it draws, the arena repacked when they will not fit around the ones resident.
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
#include <draw_primitives.h>
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
    u32 base;         // block address of the image while resident
    u32 lastFrame;    // the last world pass that drew it
    u32 plannedFrame; // the last world pass that planned it (PlanBatch): never evicted during it
    u32 generation; // changes with every upload, so stale states are rewritten
    bool resident;
    bool passOnly;  // only material passes draw it: evicted before any diffuse texture or page
    bool sky;       // a sky box face: evicted first, even after this frame drew it (below)
};

struct MaterialState
{
    u32 generation[2];
    u32 focal[2];
};

// A material pass at run time: its colour words this frame (0x80 = 1.0) and whether it draws, and
// what each context's colour block holds.
struct PassState
{
    u32 color[4];
    bool enabled;
    bool written[2];
    u32 writtenColor[2][4];
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

// The pass colour block's flag word (word 3 of its second qword): negative, where a lightmap page
// block has 1 (lit) or 0. The programs give every vertex the block's first qword as its colour.
static constexpr u32 kConstantColorFlag = 0xFFFFFFFFU;

static u64 TestState(u32 alphaRef)
{
    // Depth GEQUAL with writes (gs::BeginDepthPass); alpha-tested stages pass at alpha >= the
    // material's reference (0x80 = 1.0) and keep frame and depth where they fail, as id's
    // fragment discard does.
    return alphaRef != 0 ? GS_SET_TEST(1, 5, alphaRef, 0, 0, 0, 1, 2) : GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, 2);
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
                       u64 clamp, u64 test, bool blended = false)
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
    block[8] = gs::DrawingTag(gs::Primitive::TriangleStrip, gs::VertexFormat::StColorPosition, context, textured, blended);
    block[9] = gs::DrawingTag(gs::Primitive::Triangle, gs::VertexFormat::StColorPosition, context, textured, blended);

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

// A pass's colour block, in the page block's place: the colour every vertex takes, and the flag.
static void WriteColor(qword_t * block, const u32 (&color)[4])
{
    block[0].sw[0] = static_cast<s32>(dma::kVifNop);
    block[0].sw[1] = static_cast<s32>(dma::kVifNop);
    block[0].sw[2] = static_cast<s32>(dma::VifStcycl(1U, 1U));
    block[0].sw[3] = static_cast<s32>(dma::VifUnpack(dma::UnpackFormat::V4_32, vu::kBatchLightmapQwords, vu::kBatchLightmapOffset, true, false));
    block[1].sw[0] = color[0];
    block[1].sw[1] = color[1];
    block[1].sw[2] = color[2];
    block[1].sw[3] = color[3];
    block[2].sw[0] = 0;
    block[2].sw[1] = 0;
    block[2].sw[2] = 0;
    block[2].sw[3] = kConstantColorFlag;

    SyncDCache(block, block + kPageStrideQwords);
}

// The LOD for a texture of 'image' at the material's UV density: LOD = log2(w) + K, the texels a
// pixel spans at depth w, the material's UV density times the texture's size over the pixels a
// unit spans at depth 1.
static u64 Tex1(const tex::Image & image, float uvDensityLog2, float focalLog2)
{
    const float widthLog2 = static_cast<float>((image.tex0 >> 26) & 0xFU);
    const float heightLog2 = static_cast<float>((image.tex0 >> 30) & 0xFU);
    const float k = uvDensityLog2 + 0.5f * (widthLog2 + heightLog2) - focalLog2;
    int fixed = static_cast<int>(k * 16.0f + (k >= 0.0f ? 0.5f : -0.5f));
    fixed = fixed < -2048 ? -2048 : fixed > 2047 ? 2047 : fixed;
    return image.tex1 | (static_cast<u64>(static_cast<u32>(fixed) & 0xFFFU) << 32);
}

// The untextured state per context: written once, never changed after; and the page state of a
// batch without a lightmap pass (the program never kicks it).
alignas(64) static qword_t s_untextured[2][kStateStrideQwords];
static bool s_untexturedWritten[2] = {};

// A sky batch's state per context, written once: depth GEQUAL with writes, and an alpha test every
// pixel fails keeping depth alone (ATST NEVER, AFAIL ZB_ONLY).
alignas(64) static qword_t s_skyDepth[2][kStateStrideQwords];
static bool s_skyDepthWritten[2] = {};
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
        WriteState(s_untextured[index], context, false, 0, 0, 0, 0, 0, TestState(0));
        s_untexturedWritten[index] = true;
    }

    return s_untextured[index];
}

// ------------------------------------------------------------------------------------------------
// The sky box
// ------------------------------------------------------------------------------------------------

// A face corner while it is clipped: its clip x, y and w, and the face's S and T.
struct SkyVertex
{
    float x;
    float y;
    float w;
    float s;
    float t;
};

// A face's four corners, each of the five planes adding one (two more than a convex polygon needs,
// against a cut that rounding leaves with four crossings).
static constexpr u32 kSkyMaxVertices = 11;
static constexpr u32 kSkyPlanes = 5;

// The guard band the faces are cut to, vu::kGuardBandScale's reach of the 12.4 window, and the
// least w a corner keeps (a direction's w is its depth, the box's corners about 1 to 1.7 away).
static constexpr float kSkyGuardBand = 0.8f;
static constexpr float kSkyMinW = 1.0f / 1024.0f;

// GL's cube faces in the D3TX's order (+X, -X, +Y, -Y, +Z, -Z), from its cube map selection table:
// texel (s, t) of a face looks along normal + (2s - 1) sAxis + (2t - 1) tAxis.
struct SkyFaceAxes
{
    float normal[3];
    float sAxis[3];
    float tAxis[3];
};

static constexpr SkyFaceAxes kSkyFaces[tex::kPackSkyFaces] = {
    { {  1.0f,  0.0f,  0.0f }, {  0.0f, 0.0f, -1.0f }, { 0.0f, -1.0f,  0.0f } },
    { { -1.0f,  0.0f,  0.0f }, {  0.0f, 0.0f,  1.0f }, { 0.0f, -1.0f,  0.0f } },
    { {  0.0f,  1.0f,  0.0f }, {  1.0f, 0.0f,  0.0f }, { 0.0f,  0.0f,  1.0f } },
    { {  0.0f, -1.0f,  0.0f }, {  1.0f, 0.0f,  0.0f }, { 0.0f,  0.0f, -1.0f } },
    { {  0.0f,  0.0f,  1.0f }, {  1.0f, 0.0f,  0.0f }, { 0.0f, -1.0f,  0.0f } },
    { {  0.0f,  0.0f, -1.0f }, { -1.0f, 0.0f,  0.0f }, { 0.0f, -1.0f,  0.0f } },
};

// How far inside plane 'plane' a corner lies (negative outside): the guard band's four sides, then w.
static float SkyDistance(const SkyVertex & vertex, u32 plane)
{
    switch (plane)
    {
        case 0:
            return kSkyGuardBand * vertex.w - vertex.x;

        case 1:
            return kSkyGuardBand * vertex.w + vertex.x;

        case 2:
            return kSkyGuardBand * vertex.w - vertex.y;

        case 3:
            return kSkyGuardBand * vertex.w + vertex.y;

        default:
            return vertex.w - kSkyMinW;
    }
}

// Face 'face' through 'viewProjection' without its translation (a direction's clip coordinates),
// cut to the guard band and before the eye's plane: the corners of a convex polygon, or fewer than
// three when no part of the face is in view.
static u32 ClipSkyFace(const math::Mat4 & viewProjection, u32 face, SkyVertex (&output)[kSkyMaxVertices])
{
    const SkyFaceAxes & axes = kSkyFaces[face];
    SkyVertex buffers[2][kSkyMaxVertices];
    u32 count = 0;

    for (u32 corner = 0; corner < 4; ++corner)
    {
        // Around the face: (0, 0), (1, 0), (1, 1), (0, 1).
        const float s = corner == 1 || corner == 2 ? 1.0f : 0.0f;
        const float t = corner >= 2 ? 1.0f : 0.0f;
        float direction[3];

        for (u32 axis = 0; axis < 3; ++axis)
        {
            direction[axis] = axes.normal[axis] + (2.0f * s - 1.0f) * axes.sAxis[axis] + (2.0f * t - 1.0f) * axes.tAxis[axis];
        }

        SkyVertex & vertex = buffers[0][count++];
        vertex.x = direction[0] * viewProjection.m[0][0] + direction[1] * viewProjection.m[1][0] + direction[2] * viewProjection.m[2][0];
        vertex.y = direction[0] * viewProjection.m[0][1] + direction[1] * viewProjection.m[1][1] + direction[2] * viewProjection.m[2][1];
        vertex.w = direction[0] * viewProjection.m[0][3] + direction[1] * viewProjection.m[1][3] + direction[2] * viewProjection.m[2][3];
        vertex.s = s;
        vertex.t = t;
    }

    u32 from = 0;

    for (u32 plane = 0; plane < kSkyPlanes && count >= 3; ++plane)
    {
        const SkyVertex * input = buffers[from];
        SkyVertex * clipped = buffers[from ^ 1U];
        u32 kept = 0;

        for (u32 index = 0; index < count; ++index)
        {
            const SkyVertex & current = input[index];
            const SkyVertex & next = input[index + 1U == count ? 0U : index + 1U];
            const float here = SkyDistance(current, plane);
            const float there = SkyDistance(next, plane);

            if (here >= 0.0f && kept < kSkyMaxVertices)
            {
                clipped[kept++] = current;
            }

            // Crossing the plane: the edge's point on it (each value is linear along the edge).
            if ((here >= 0.0f) != (there >= 0.0f) && kept < kSkyMaxVertices)
            {
                const float f = here / (here - there);
                SkyVertex & cut = clipped[kept++];
                cut.x = current.x + f * (next.x - current.x);
                cut.y = current.y + f * (next.y - current.y);
                cut.w = current.w + f * (next.w - current.w);
                cut.s = current.s + f * (next.s - current.s);
                cut.t = current.t + f * (next.t - current.t);
            }
        }

        count = kept;
        from ^= 1U;
    }

    for (u32 index = 0; index < count; ++index)
    {
        output[index] = buffers[from][index];
    }

    return count;
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
    PassState * passes;
    MaterialState * passStates;
    qword_t * passBlocks;
    qword_t * colorBlocks;
    PageBlockState * pageStates;
    qword_t * pageBlocks;
    Range * free;
    u32 freeCount;
    // The coming pass's plan: the textures it draws in first-use order, and a scratch copy of the
    // free ranges for placing them in a simulation first.
    u32 * planOrder;
    Range * planFree;
    u32 planCount;
    u32 planFrame;
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
    PassState * passes = nullptr;
    MaterialState * passStates = nullptr;
    qword_t * passBlocks = nullptr;
    qword_t * colorBlocks = nullptr;
    u32 * planOrder = nullptr;
    Range * planFree = nullptr;

    if (error == nullptr)
    {
        const u32 textureCount = pack.textureCount > 0 ? pack.textureCount : 1U;
        slots = static_cast<Slot *>(system::heap::TryAlloc(sizeof(Slot) * textureCount, TAG_RENDER));
        states = static_cast<MaterialState *>(system::heap::TryAlloc(sizeof(MaterialState) * (pack.materialCount + 1U), TAG_RENDER));
        blocks = static_cast<qword_t *>(system::heap::TryAlloc(sizeof(qword_t) * kStateStrideQwords * 2U * (pack.materialCount + 1U), TAG_RENDER, 64));
        ranges = static_cast<Range *>(system::heap::TryAlloc(sizeof(Range) * (textureCount + 1U), TAG_RENDER));
        pageStates = static_cast<PageBlockState *>(system::heap::TryAlloc(sizeof(PageBlockState) * (pack.pageCount + 1U), TAG_RENDER));
        pageBlocks = static_cast<qword_t *>(system::heap::TryAlloc(sizeof(qword_t) * kPageStrideQwords * 2U * (pack.pageCount + 1U), TAG_RENDER, 64));
        passes = static_cast<PassState *>(system::heap::TryAlloc(sizeof(PassState) * (pack.passCount + 1U), TAG_RENDER));
        passStates = static_cast<MaterialState *>(system::heap::TryAlloc(sizeof(MaterialState) * (pack.passCount + 1U), TAG_RENDER));
        passBlocks = static_cast<qword_t *>(system::heap::TryAlloc(sizeof(qword_t) * kStateStrideQwords * 2U * (pack.passCount + 1U), TAG_RENDER, 64));
        colorBlocks = static_cast<qword_t *>(system::heap::TryAlloc(sizeof(qword_t) * kPageStrideQwords * 2U * (pack.passCount + 1U), TAG_RENDER, 64));
        planOrder = static_cast<u32 *>(system::heap::TryAlloc(sizeof(u32) * textureCount, TAG_RENDER));
        planFree = static_cast<Range *>(system::heap::TryAlloc(sizeof(Range) * (textureCount + 1U), TAG_RENDER));
        error = slots == nullptr || states == nullptr || blocks == nullptr || ranges == nullptr || pageStates == nullptr || pageBlocks == nullptr ||
                        passes == nullptr || passStates == nullptr || passBlocks == nullptr || colorBlocks == nullptr || planOrder == nullptr ||
                        planFree == nullptr
                    ? "no memory for its residency"
                    : nullptr;
    }

    if (error != nullptr) [[unlikely]]
    {
        Log(LogLevel::Error, "textures %s refused: %s\n", name, error);
        system::heap::Free(planFree);
        system::heap::Free(planOrder);
        system::heap::Free(colorBlocks);
        system::heap::Free(passBlocks);
        system::heap::Free(passStates);
        system::heap::Free(passes);
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
    textures->passes = passes;
    textures->passStates = passStates;
    textures->passBlocks = passBlocks;
    textures->colorBlocks = colorBlocks;
    textures->planOrder = planOrder;
    textures->planFree = planFree;

    // Every pass starts with its cooked colour; BeginTextures evaluates the ones time drives.
    for (u32 index = 0; index < pack.passCount; ++index)
    {
        const tex::PackPass & record = pack.passes[index];
        PassState & state = passes[index];
        state = {};
        state.enabled = record.condition == tex::kPackNoNode;

        for (u32 channel = 0; channel < 4; ++channel)
        {
            state.color[channel] = record.color[channel];
        }

        passStates[index] = {};
    }

    for (u32 index = 0; index < pack.textureCount; ++index)
    {
        slots[index] = {};
    }

    for (u32 index = 0; index < pack.passCount; ++index)
    {
        slots[pack.passes[index].texture].passOnly = true;
    }

    for (u32 index = 0; index < pack.materialCount; ++index)
    {
        if (pack.materials[index].texture < pack.textureCount)
        {
            slots[pack.materials[index].texture].passOnly = false;
        }
    }

    for (u32 face = 0; pack.sky != nullptr && face < tex::kPackSkyFaces; ++face)
    {
        slots[pack.sky->faces[face]].sky = true;
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

    Log(LogLevel::Info, "[D3BFG] world textures %s: textures=%u materials=%u pages=%u passes=%u vram=%u bytes=%u\n", name,
        static_cast<u32>(pack.textureCount), static_cast<u32>(pack.materialCount), static_cast<u32>(pack.pageCount), static_cast<u32>(pack.passCount),
        vramBytes, bytes);
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

    system::heap::Free(textures->planFree);
    system::heap::Free(textures->planOrder);
    system::heap::Free(textures->colorBlocks);
    system::heap::Free(textures->passBlocks);
    system::heap::Free(textures->passStates);
    system::heap::Free(textures->passes);
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

// Best fit over 'ranges' (the smallest that holds 'count' blocks, the lowest of equals), so a nearly
// full arena keeps its large holes for the large textures; from the top (the last range that fits,
// its end) for the material passes' and the sky's textures, so they stay out of the diffuse
// textures' way.
static bool TakeRange(Range * ranges, u32 & rangeCount, u32 count, u32 & start, bool fromTop)
{
    u32 best = rangeCount;

    for (u32 step = 0; step < rangeCount; ++step)
    {
        const u32 index = fromTop ? rangeCount - 1U - step : step;

        if (ranges[index].count < count)
        {
            continue;
        }

        if (fromTop)
        {
            best = index;
            break;
        }

        if (best == rangeCount || ranges[index].count < ranges[best].count)
        {
            best = index;
        }
    }

    if (best == rangeCount)
    {
        return false;
    }

    Range & range = ranges[best];

    if (fromTop)
    {
        start = range.start + range.count - count;
    }
    else
    {
        start = range.start;
        range.start += count;
    }

    range.count -= count;

    if (range.count == 0)
    {
        for (u32 move = best + 1U; move < rangeCount; ++move)
        {
            ranges[move - 1U] = ranges[move];
        }

        --rangeCount;
    }

    return true;
}

// Returns blocks to the sorted 'ranges', merging with their neighbours.
static void GiveRange(Range * ranges, u32 & rangeCount, u32 start, u32 count)
{
    u32 at = 0;

    while (at < rangeCount && ranges[at].start < start)
    {
        ++at;
    }

    for (u32 move = rangeCount; move > at; --move)
    {
        ranges[move] = ranges[move - 1U];
    }

    ranges[at] = { start, count };
    ++rangeCount;

    if (at + 1U < rangeCount && ranges[at].start + ranges[at].count == ranges[at + 1U].start)
    {
        ranges[at].count += ranges[at + 1U].count;

        for (u32 move = at + 2U; move < rangeCount; ++move)
        {
            ranges[move - 1U] = ranges[move];
        }

        --rangeCount;
    }

    if (at > 0 && ranges[at - 1U].start + ranges[at - 1U].count == ranges[at].start)
    {
        ranges[at - 1U].count += ranges[at].count;

        for (u32 move = at + 1U; move < rangeCount; ++move)
        {
            ranges[move - 1U] = ranges[move];
        }

        --rangeCount;
    }
}

static void Evict(Textures & textures, u32 index)
{
    Slot & slot = textures.slots[index];
    const tex::Image & image = textures.images[index];

    GiveRange(textures.free, textures.freeCount, slot.base - textures.arenaBase, image.blocks);
    slot.resident = false;
    textures.stats.residentBytes -= image.blocks * kGsBlockBytes;
    --textures.stats.resident;
    textures.stats.pagesResident -= image.usage == tex::ImageUsage::Lightmap ? 1U : 0U;
    ++textures.stats.evictions;
}

// How readily a resident texture gives way: sky faces first, then textures only material passes
// draw, then the rest; the least recently drawn first within each.
static u32 EvictionRank(const Slot & slot)
{
    return slot.sky ? 0U : slot.passOnly ? 1U : 2U;
}

// Places the image, evicting the least recently drawn ones this pass neither drew nor planned
// (PlanBatch): their last readers are earlier chains, which finish before this frame's upload runs,
// and a state block that names their address is never rewritten under a chain that reads it. Sky
// faces are evicted even after this pass drew them: the sky box draws first, its TEX0 inside the
// chain's own DIRECT data, and the GS draws it before any later upload overwrites its blocks.
// Textures only material passes draw go before diffuse ones. With 'staleOnly' (the sky), only
// textures the previous pass did not draw either give way: what a view draws every frame never
// makes room for the sky, which then waits for space or is left out.
static bool MakeResident(Textures & textures, dma::DisplayList & list, u32 index, bool fromTop = false, bool staleOnly = false)
{
    Slot & slot = textures.slots[index];
    const tex::Image & image = textures.images[index];
    u32 start = 0;

    while (!TakeRange(textures.free, textures.freeCount, image.blocks, start, fromTop))
    {
        u32 victim = textures.pack.textureCount;

        for (u32 candidate = 0; candidate < textures.pack.textureCount; ++candidate)
        {
            const Slot & other = textures.slots[candidate];

            if (!other.resident || other.plannedFrame == textures.frame || (other.lastFrame == textures.frame && !other.sky) ||
                (staleOnly && !other.sky && other.lastFrame + 1U >= textures.frame))
            {
                continue;
            }

            const Slot * best = victim == textures.pack.textureCount ? nullptr : &textures.slots[victim];

            if (best == nullptr || EvictionRank(other) < EvictionRank(*best) ||
                (EvictionRank(other) == EvictionRank(*best) && other.lastFrame < best->lastFrame))
            {
                victim = candidate;
            }
        }

        if (victim == textures.pack.textureCount)
        {
            return false;
        }

        Evict(textures, victim);
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
// Residency planning
// ------------------------------------------------------------------------------------------------

static void PlanTexture(Textures & textures, u32 index)
{
    Slot & slot = textures.slots[index];

    if (slot.plannedFrame != textures.planFrame)
    {
        slot.plannedFrame = textures.planFrame;
        textures.planOrder[textures.planCount++] = index;
    }
}

// Whether the planned textures not resident yet can be placed, in draw order, into the free ranges
// as they stand, or with every texture the plan does not name gone too: a simulation on a copy.
static bool PlanFits(Textures & textures, bool withoutUnplanned)
{
    u32 rangeCount = textures.freeCount;

    for (u32 range = 0; range < rangeCount; ++range)
    {
        textures.planFree[range] = textures.free[range];
    }

    for (u32 index = 0; index < textures.pack.textureCount && withoutUnplanned; ++index)
    {
        const Slot & slot = textures.slots[index];

        if (slot.resident && slot.plannedFrame != textures.frame)
        {
            GiveRange(textures.planFree, rangeCount, slot.base - textures.arenaBase, textures.images[index].blocks);
        }
    }

    for (u32 order = 0; order < textures.planCount; ++order)
    {
        const u32 index = textures.planOrder[order];
        u32 start = 0;

        if (!textures.slots[index].resident && !TakeRange(textures.planFree, rangeCount, textures.images[index].blocks, start, false))
        {
            return false;
        }
    }

    return true;
}

// Makes the planned textures resident before the pass draws, in its draw order. When a view brings
// in textures not resident yet and they do not fit the free ranges as they stand, every texture the
// pass does not need leaves first; if they still cannot be placed around the ones staying (the arena
// near full and fragmented), every texture leaves and the planned set goes in from an empty arena,
// which packs it without holes. Simulations on a copy of the free ranges decide, so the placement
// that follows is the one they found. Nothing of this pass references any texture yet, and the
// chain in flight finishes before this one's uploads run.
static void Preload(Textures & textures, dma::DisplayList & list)
{
    bool missing = false;

    for (u32 order = 0; order < textures.planCount && !missing; ++order)
    {
        missing = !textures.slots[textures.planOrder[order]].resident;
    }

    if (!missing || PlanFits(textures, false))
    {
        // Nothing to place, or room for it all: the pass places them as it draws.
        return;
    }

    const bool fits = PlanFits(textures, true);

    for (u32 index = 0; index < textures.pack.textureCount; ++index)
    {
        if (textures.slots[index].resident && (!fits || textures.slots[index].plannedFrame != textures.frame))
        {
            Evict(textures, index);
        }
    }

    textures.stats.repacks += fits ? 0U : 1U;

    // A texture that still finds no room falls back when its batch draws.
    for (u32 order = 0; order < textures.planCount; ++order)
    {
        const u32 index = textures.planOrder[order];

        if (!textures.slots[index].resident)
        {
            (void)MakeResident(textures, list, index);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Passes
// ------------------------------------------------------------------------------------------------

// The passes time drives, evaluated at 'time': their colour and whether they draw this frame.
static void EvaluatePasses(Textures & textures, float time)
{
    float registers[tex::kPackMaxNodesPerPass];

    for (u32 index = 0; index < textures.pack.passCount; ++index)
    {
        const tex::PackPass & record = textures.pack.passes[index];

        if (record.nodeCount == 0)
        {
            continue;
        }

        tex::EvaluatePassNodes(textures.pack, record, time, registers);
        PassState & state = textures.passes[index];
        state.enabled = record.condition == tex::kPackNoNode || registers[record.condition - record.firstNode] != 0.0f;

        for (u32 channel = 0; channel < 4; ++channel)
        {
            if (record.colorNodes[channel] != tex::kPackNoNode)
            {
                // 0x80 = 1.0, clamped to the colour byte.
                const float value = registers[record.colorNodes[channel] - record.firstNode] * 128.0f + 0.5f;
                state.color[channel] = value <= 0.0f ? 0U : value >= 255.0f ? 255U : static_cast<u32>(value);
            }
        }
    }
}

void BeginPlan(Textures * textures)
{
    if (textures != nullptr)
    {
        textures->planFrame = textures->frame + 1U;
        textures->planCount = 0;
    }
}

void PlanBatch(Textures * textures, u32 material, u32 page, bool textured, bool lightmapped)
{
    if (textures == nullptr)
    {
        return;
    }

    PS2_AssertMsg(material < textures->pack.materialCount && (page == kNoLightmap || page < textures->pack.pageCount),
                  "planned batch outside the texture package");
    const u32 texture = textures->pack.materials[material].texture;

    if (textured && texture < textures->pack.textureCount)
    {
        PlanTexture(*textures, texture);
    }

    if (lightmapped && page != kNoLightmap)
    {
        PlanTexture(*textures, textures->pack.pages[page]);
    }
}

void BeginTextures(Textures * textures, dma::DisplayList & list, gs::DrawContext context, float focal, bool textured, bool lightmapped, float time)
{
    s_context = context;
    s_textured = textured;
    s_lightmapped = lightmapped;

    if (textures == nullptr)
    {
        return;
    }

    ++textures->frame;
    EvaluatePasses(*textures, time);

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

    if (textures->planFrame == textures->frame && textures->arenaBlocks != 0)
    {
        Preload(*textures, list);
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

    if (record.texture == tex::kPackHidden || record.texture == tex::kPackSky)
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
        const bool alphaTest = (textures->pack.textures[record.texture].flags & tex::kPackFlagAlphaTest) != 0;

        WriteState(block, s_context, true, tex::RelocateTex0(image.tex0, slot.base), Tex1(image, record.uvDensityLog2, textures->focalLog2),
                   tex::RelocateMiptbp(image.miptbp1, slot.base), tex::RelocateMiptbp(image.miptbp2, slot.base), image.clamp,
                   TestState(alphaTest ? record.alphaRef : 0U));
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

u32 PassCount(const Textures * textures, u32 material)
{
    if (textures == nullptr || !s_textured)
    {
        return 0;
    }

    PS2_AssertMsg(material < textures->pack.materialCount, "batch material outside the texture package");
    return textures->pack.materials[material].passCount;
}

bool PassBlocks(Textures * textures, dma::DisplayList & list, u32 material, u32 pass, const void *& state, const void *& color)
{
    PS2_AssertMsg(textures != nullptr && material < textures->pack.materialCount && pass < textures->pack.materials[material].passCount,
                  "material pass outside the texture package");
    const tex::PackMaterial & owner = textures->pack.materials[material];
    const u32 index = owner.firstPass + pass;
    const tex::PackPass & record = textures->pack.passes[index];
    PassState & dynamic = textures->passes[index];

    if (!dynamic.enabled)
    {
        return false;
    }

    Slot & slot = textures->slots[record.texture];

    if (!slot.resident && (textures->arenaBlocks == 0 || !MakeResident(*textures, list, record.texture, true)))
    {
        ++textures->stats.passFallbacks;
        return false;
    }

    slot.lastFrame = textures->frame;
    const int context = s_context == gs::DrawContext::Ctx1 ? 1 : 0;
    MaterialState & written = textures->passStates[index];
    qword_t * const block = textures->passBlocks + (index * 2U + static_cast<u32>(context)) * kStateStrideQwords;
    qword_t * const colorBlock = textures->colorBlocks + (index * 2U + static_cast<u32>(context)) * kPageStrideQwords;

    // As with materials, only this context's blocks are rewritten: the chain in flight reads the other's.
    if (written.generation[context] != slot.generation || written.focal[context] != textures->focalVersion)
    {
        const tex::Image & image = textures->images[record.texture];

        // The base pass's UVs, density and depth; additive, its tags blending through ALPHA.
        WriteState(block, s_context, true, tex::RelocateTex0(image.tex0, slot.base), Tex1(image, owner.uvDensityLog2, textures->focalLog2),
                   tex::RelocateMiptbp(image.miptbp1, slot.base), tex::RelocateMiptbp(image.miptbp2, slot.base), image.clamp, TestState(0), true);
        written.generation[context] = slot.generation;
        written.focal[context] = textures->focalVersion;
    }

    bool same = dynamic.written[context];

    for (u32 channel = 0; channel < 4; ++channel)
    {
        same = same && dynamic.writtenColor[context][channel] == dynamic.color[channel];
    }

    if (!same)
    {
        WriteColor(colorBlock, dynamic.color);
        dynamic.written[context] = true;

        for (u32 channel = 0; channel < 4; ++channel)
        {
            dynamic.writtenColor[context][channel] = dynamic.color[channel];
        }
    }

    ++textures->stats.passes;
    state = block;
    color = colorBlock;
    return true;
}

bool IsSky(const Textures * textures, u32 material)
{
    PS2_AssertMsg(textures == nullptr || material < textures->pack.materialCount, "batch material outside the texture package");
    return textures != nullptr && textures->pack.materials[material].texture == tex::kPackSky;
}

const void * SkyDepthState()
{
    const int index = s_context == gs::DrawContext::Ctx1 ? 1 : 0;

    if (!s_skyDepthWritten[index])
    {
        WriteState(s_skyDepth[index], s_context, false, 0, 0, 0, 0, 0, GS_SET_TEST(1, 0, 0, 2, 0, 0, 1, 2));
        s_skyDepthWritten[index] = true;
    }

    return s_skyDepth[index];
}

void DrawSkyBox(Textures * textures, dma::DisplayList & list, gs::DrawContext context, const math::Mat4 & viewProjection)
{
    if (textures == nullptr || !s_textured || textures->pack.sky == nullptr)
    {
        return;
    }

    const tex::PackSky & sky = *textures->pack.sky;
    const u64 c = context == gs::DrawContext::Ctx1 ? 1U : 0U;
    const math::Vec4 scale = gs::WindowScale();
    const math::Vec4 offset = gs::WindowOffset();
    bool flushed = false;

    // Face by face, each made resident and drawn before the next: a face may evict one drawn before
    // it (MakeResident), never one still to draw.
    for (u32 face = 0; face < tex::kPackSkyFaces; ++face)
    {
        SkyVertex polygon[kSkyMaxVertices];
        const u32 count = ClipSkyFace(viewProjection, face, polygon);

        if (count < 3)
        {
            continue;
        }

        // From the arena's top, like the passes' textures, out of the diffuse textures' way.
        const u32 texture = sky.faces[face];
        Slot & slot = textures->slots[texture];

        if (!slot.resident && (textures->arenaBlocks == 0 || !MakeResident(*textures, list, texture, true, true)))
        {
            ++textures->stats.skyFallbacks;
            continue;
        }

        slot.lastFrame = textures->frame;
        ++textures->stats.skyFaces;

        if (!flushed)
        {
            list.FlushPaths();
            flushed = true;
        }

        // One A+D tag: TEX0, TEX1, CLAMP, TEST and PRIM, then ST, RGBAQ and XYZ2 per corner. One
        // level, bilinear, clamped at the face's edges; depth neither tested nor written (the 2D
        // environment masks it); a fan of perspective-correct STQ, the sky's colour modulating.
        const int writes = 5 + 3 * static_cast<int>(count);
        gs::GifWriter writer = list.BeginDirect(1 + writes);
        writer.BeginAd(writes);
        writer.Ad(GS_REG_TEX0_1 + c, tex::RelocateTex0(textures->images[texture].tex0, slot.base));
        writer.Ad(GS_REG_TEX1_1 + c, textures->images[texture].tex1);
        writer.Ad(GS_REG_CLAMP_1 + c, GS_SET_CLAMP(1, 1, 0, 0, 0, 0));
        writer.Ad(GS_REG_TEST_1 + c, GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, 1));
        writer.Ad(GS_REG_PRIM, GS_SET_PRIM(PRIM_TRIANGLE_FAN, PRIM_SHADE_GOURAUD, 1, 0, 0, 0, PRIM_MAP_ST, c, 0));

        for (u32 index = 0; index < count; ++index)
        {
            const SkyVertex & vertex = polygon[index];
            const float q = 1.0f / vertex.w;
            const float x = (vertex.x * q * scale.x + offset.x) * 16.0f + 0.5f;
            const float y = (vertex.y * q * scale.y + offset.y) * 16.0f + 0.5f;

            writer.Ad(GS_REG_ST, GS_SET_ST(std::bit_cast<u32>(vertex.s * q), std::bit_cast<u32>(vertex.t * q)));
            writer.Ad(GS_REG_RGBAQ, GS_SET_RGBAQ(sky.color[0], sky.color[1], sky.color[2], sky.color[3], std::bit_cast<u32>(q)));
            writer.Ad(GS_REG_XYZ2, GS_SET_XYZ(static_cast<u32>(x), static_cast<u32>(y), 0));
        }

        list.EndDirect(writer);
    }
}

TextureStats GetTextureStats(const Textures * textures)
{
    return textures != nullptr ? textures->stats : TextureStats{};
}

} // namespace ps2::renderer::world
