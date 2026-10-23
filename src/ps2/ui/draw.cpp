// ================================================================================================
// File: draw.cpp
// Brief: Adapt native quad/text contracts to bounded ordered GS sprites on a 640x480 canvas.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/draw.h"
#include "ps2/renderer/image_backend.h"
#include "ps2/renderer/null_render.h"
#include "ps2/renderer/render_system.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include "ps2/ui/masks.h"
#include "ps2/ui/page_image.h"
#include <bit>
#include <cstdint>
#include <idlib/precompiled.h>
#include <renderer/tr_local.h>

namespace
{
static bool s_initialized = false;
static bool s_swfDraw = false;
static uint64 s_savedSwfState = 0;
static uint32 s_savedSwfColor = 0;
static const void * s_maskKeys[ps2::ui::kMaxActiveMasks] = {};
static ps2::ui::ConvexMask s_masks[ps2::ui::kMaxActiveMasks] = {};
static int s_maskCount = 0;
static const void * s_captureMask = nullptr;
static ps2::ui::MaskBuilder s_maskBuilder;
// A mask the convex bridge cannot express is replayed into the GS depth buffer, one at a time.
static constexpr int kMaxCaptureTriangles = 256;
static constexpr int kMaxCaptureLines = 192;
static ps2::ui::MaskPoint (*s_captureTriangles)[3] = nullptr;
static ps2::ui::MaskPoint (*s_captureLines)[2] = nullptr;
static int s_captureTriangleCount = 0;
static int s_captureLineCount = 0;
static bool s_captureOverflow = false;
static const void * s_depthMaskKey = nullptr;
static int s_depthBounds[4] = {};
// The EE's native RGBA byte order is little endian; no idlib swap initialization is required.
static_assert(std::endian::native == std::endian::little);
static constexpr uint64 kBlendAlpha = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
static constexpr uint64 kBlendSwf = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
static constexpr uint64 kBlendSwfAdd = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
// SWF screen asks for DST_COLOR/ONE with a subtract op; the shipped GL backend ignores blend ops,
// so the PC result is Cd + Cd * Cs.
static constexpr uint64 kBlendSwfScreen = GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE;
static constexpr uint64 kAllowedState = GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS | GLS_DEPTHMASK | GLS_DEPTHFUNC_BITS | GLS_OVERRIDE;
// Native SWF also draws outlines in line mode and requests the screen blend op.
static constexpr uint64 kAllowedSwfState = kAllowedState | GLS_POLYMODE_LINE | GLS_BLENDOP_SUB;

struct Profile
{
    const idImage * image;
    ps2::renderer::gs::BlendMode blend;
    ps2::renderer::gs::Color tint;
    bool vertexColor;
    bool enabled;
    bool swf = false;
    // Native line mode: each triangle draws its edges as one-pixel lines.
    bool lines = false;
};

struct Batch
{
    Profile profile;
    int firstVertex;
    int vertexCount;
    int firstIndex;
    int indexCount;
};

static idDrawVert * s_vertices = nullptr;
static triIndex_t * s_indexes = nullptr;
static Batch * s_batches = nullptr;
static int s_vertexCount = 0;
static int s_indexCount = 0;
static int s_batchCount = 0;
static bool s_flushing = false;
static ps2::ui::GeometryStats s_geometryStats = {};

void FreeGeometry()
{
    ps2::system::heap::Free(s_captureLines);
    ps2::system::heap::Free(s_captureTriangles);
    ps2::system::heap::Free(s_batches);
    ps2::system::heap::Free(s_indexes);
    ps2::system::heap::Free(s_vertices);
    s_captureLines = nullptr;
    s_captureTriangles = nullptr;
    s_batches = nullptr;
    s_indexes = nullptr;
    s_vertices = nullptr;
    s_vertexCount = s_indexCount = s_batchCount = 0;
}

// The pixel rectangle holding a fixed point, widened to cover every pixel a primitive may touch.
void GrowBounds(int (&bounds)[4], int x, int y)
{
    const int left = x / 16;
    const int top = y / 16;
    const int right = Min((x + 15) / 16 + 1, ps2::renderer::gs::Width());
    const int bottom = Min((y + 15) / 16 + 1, ps2::renderer::gs::Height());

    if (bounds[0] >= bounds[2])
    {
        bounds[0] = left;
        bounds[1] = top;
        bounds[2] = right;
        bounds[3] = bottom;
        return;
    }

    bounds[0] = Min(bounds[0], left);
    bounds[1] = Min(bounds[1], top);
    bounds[2] = Max(bounds[2], right);
    bounds[3] = Max(bounds[3], bottom);
}

// Replays a captured mask into the depth buffer and records the pixels it may cover.
void WriteDepthMask()
{
    using namespace ps2::ui;
    const Viewport viewport{ ps2::renderer::gs::Width(), ps2::renderer::gs::Height() };
    int bounds[4] = {};

    for (int index = 0; index < s_captureTriangleCount; ++index)
    {
        const auto & points = s_captureTriangles[index];
        const Vertex input[3] = { { points[0].x, points[0].y, 0.0f, 0.0f, 0, 0, 0, 255 },
                                  { points[1].x, points[1].y, 0.0f, 0.0f, 0, 0, 0, 255 },
                                  { points[2].x, points[2].y, 0.0f, 0.0f, 0, 0, 0, 255 } };
        FixedTriangle pieces[kMaxClippedTriangles];
        int count = 0;

        if (MapTriangle(input, viewport, 1, 1, pieces, count) == Result::Invalid)
        {
            ps2::FatalError("invalid native SWF mask triangle");
        }

        for (int piece = 0; piece < count; ++piece)
        {
            for (const auto & vertex : pieces[piece].vertices)
            {
                GrowBounds(bounds, vertex.x, vertex.y);
            }

            ps2::renderer::MaskTriangleFixed(pieces[piece]);
        }
    }

    for (int index = 0; index < s_captureLineCount; ++index)
    {
        const auto & points = s_captureLines[index];
        const Vertex input[2] = { { points[0].x, points[0].y, 0.0f, 0.0f, 0, 0, 0, 255 },
                                  { points[1].x, points[1].y, 0.0f, 0.0f, 0, 0, 0, 255 } };
        FixedLine line = {};
        const Result result = MapLine(input, viewport, line);

        if (result == Result::Invalid)
        {
            ps2::FatalError("invalid native SWF mask line");
        }

        if (result == Result::Draw)
        {
            for (const auto & vertex : line.vertices)
            {
                GrowBounds(bounds, vertex.x, vertex.y);
            }

            ps2::renderer::MaskLineFixed(line);
        }
    }

    for (int i = 0; i < 4; ++i)
    {
        s_depthBounds[i] = bounds[i];
    }
}

void Require(const idRenderSystemLocal * owner, const char * method, bool drawing = false)
{
    PS2_Assert(owner != nullptr);
    if (!s_initialized || owner != &tr || !ps2::renderer::IsInitialized() || (drawing && !ps2::renderer::FrameStarted()))
    {
        ps2::renderer::Unavailable(method);
    }
}

bool PortableFloat(float value)
{
    const auto bits = std::bit_cast<std::uint32_t>(value) & 0x7FFFFFFFU;
    return bits < 0x7F800000U && (bits == 0 || bits >= 0x00800000U);
}

bool UnitValue(float value)
{
    return PortableFloat(value) && value >= 0.0f && value <= 1.0f;
}

u8 ByteColor(float value)
{
    if (!UnitValue(value))
    {
        ps2::FatalError("invalid native UI color");
    }
    return static_cast<u8>(value * 255.0f); // Match native PackColor/Ftob truncation.
}

ps2::renderer::gs::Color CurrentColor()
{
    const uint32 packed = tr.currentColorNativeBytesOrder;
    return { static_cast<u8>(packed & 255U), static_cast<u8>((packed >> 8) & 255U),
             static_cast<u8>((packed >> 16) & 255U), static_cast<u8>((packed >> 24) & 255U) };
}

ps2::renderer::gs::BlendMode Blend(uint64 state)
{
    const uint64 equation = state & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS);
    const uint64 depth = state & GLS_DEPTHFUNC_BITS;
    // The subtract op belongs only to screen blending, whose PC result ignores it.
    const bool screen = s_swfDraw && equation == kBlendSwfScreen;
    if ((state & ~(s_swfDraw ? kAllowedSwfState : kAllowedState)) != 0 || ((state & GLS_BLENDOP_SUB) != 0 && !screen) ||
        (depth != GLS_DEPTHFUNC_LESS && depth != GLS_DEPTHFUNC_ALWAYS) ||
        (equation != GLS_DEFAULT && equation != kBlendAlpha &&
         !(s_swfDraw && (equation == kBlendSwf || equation == kBlendSwfAdd || screen))))
    {
        ps2::renderer::Unavailable("native UI blend/state profile");
    }

    if (screen)
    {
        return ps2::renderer::gs::BlendMode::Screen;
    }

    if (s_swfDraw && equation == kBlendSwfAdd)
    {
        return ps2::renderer::gs::BlendMode::AdditiveAlpha;
    }

    return equation == GLS_DEFAULT ? ps2::renderer::gs::BlendMode::Replace : ps2::renderer::gs::BlendMode::StraightAlpha;
}

uint64 CombinedState(uint64 stageState)
{
    return (tr.currentGLState & GLS_OVERRIDE) != 0 ? tr.currentGLState & ~GLS_OVERRIDE : tr.currentGLState | stageState;
}

Profile MaterialProfile(const idMaterial * material, ps2::renderer::gs::Color base)
{
    if (material == nullptr || material->GetNumStages() != 1 || material->ConstantRegisters() == nullptr ||
        material->TestMaterialFlag(MF_DEFAULTED | MF_POLYGONOFFSET) || material->HasSubview() || material->Deform() != DFRM_NONE ||
        material->GetNumRegisters() < 1 || material->GetNumRegisters() > MAX_EXPRESSION_REGISTERS)
    {
        ps2::renderer::Unavailable("native UI requires one constant material stage");
    }
    const shaderStage_t & stage = *material->GetStage(0);
    if (stage.lighting != SL_AMBIENT || stage.hasAlphaTest || stage.newStage != nullptr ||
        stage.texture.texgen != TG_EXPLICIT || stage.texture.hasMatrix || stage.texture.cinematic != nullptr ||
        stage.texture.dynamic != DI_STATIC || stage.privatePolygonOffset != 0.0f || stage.ignoreAlphaTest ||
        (stage.vertexColor != SVC_IGNORE && stage.vertexColor != SVC_MODULATE))
    {
        ps2::renderer::Unavailable("native UI material stage profile");
    }
    const int registers = material->GetNumRegisters();
    const auto Register = [registers, material](int index)
    {
        if (index < 0 || index >= registers)
        {
            ps2::FatalError("native UI material register outside bounds");
        }
        const float value = material->ConstantRegisters()[index];
        if (!UnitValue(value))
        {
            ps2::FatalError("native UI material register outside unit range");
        }
        return value;
    };
    const float condition = Register(stage.conditionRegister);
    const u8 channels[] = { base.r, base.g, base.b, base.a };
    u8 modulated[4] = {};
    for (int i = 0; i < 4; ++i)
    {
        modulated[i] = static_cast<u8>(Register(stage.color.registers[i]) * static_cast<float>(channels[i]));
    }
    ps2::renderer::ValidateUiImage(stage.texture.image);
    const uint64 state = CombinedState(stage.drawStateBits);
    return { stage.texture.image, Blend(state), { modulated[0], modulated[1], modulated[2], modulated[3] }, stage.vertexColor == SVC_MODULATE,
             condition != 0.0f, s_swfDraw, (state & GLS_POLYMODE_LINE) != 0 };
}

float DecodeHalf(halfFloat_t value)
{
    const u32 bits = value;
    const u32 exponent = (bits >> 10) & 31U;
    const u32 mantissa = bits & 1023U;
    if (exponent == 31U)
    {
        ps2::FatalError("native UI half UV is not finite");
    }
    // Native F16toF32 uses powf and maps Inf/NaN to small numbers. Decode checked bits directly.
    if (exponent != 0)
    {
        return std::bit_cast<float>(((bits & 32768U) << 16) | ((exponent + 112U) << 23) | (mantissa << 13));
    }
    const float magnitude = static_cast<float>(mantissa) * (1.0f / 16777216.0f);
    return (bits & 32768U) != 0 ? -magnitude : magnitude;
}

struct DecodedVertex
{
    ps2::ui::Vertex vertex;
    // Native GUI shading adds color2 * 2 - 1 to the textured color: per unit, 2 * byte - 255.
    s16 add[3];
};

DecodedVertex DecodeVertex(const idDrawVert & vertex, const Profile & profile)
{
    if (!PortableFloat(vertex.xyz.z) || vertex.xyz.z != 0.0f)
    {
        ps2::FatalError("native UI vertex requires zero depth");
    }

    DecodedVertex output = {};

    for (int channel = 0; channel < 4; ++channel)
    {
        const u8 value = vertex.color2[channel];

        // PackColor(0.5) gives 127 and a cleared vertex 128: both are the zero term. Only native
        // SWF shading admits other color terms, and only for RGB.
        if (value == 128 || (profile.swf && value == 127))
        {
            continue;
        }

        if (!profile.swf || channel == 3)
        {
            ps2::renderer::Unavailable("native UI additive vertex color");
        }

        output.add[channel] = static_cast<s16>(2 * static_cast<int>(value) - 255);
    }

    const auto Modulate = [](u8 color, u8 tint)
    { return static_cast<u8>((static_cast<u32>(color) * tint + 127U) / 255U); };
    const auto & tint = profile.tint;
    output.vertex = { vertex.xyz.x, vertex.xyz.y, DecodeHalf(vertex.st[0]), DecodeHalf(vertex.st[1]),
                      profile.vertexColor ? Modulate(vertex.color[0], tint.r) : tint.r,
                      profile.vertexColor ? Modulate(vertex.color[1], tint.g) : tint.g,
                      profile.vertexColor ? Modulate(vertex.color[2], tint.b) : tint.b,
                      profile.vertexColor ? Modulate(vertex.color[3], tint.a) : tint.a };
    return output;
}

// The shading of a primitive whose vertices share one native color term.
template <size_t Count>
ps2::renderer::Shading MakeShading(const DecodedVertex (&vertices)[Count], ps2::renderer::gs::BlendMode blend)
{
    ps2::renderer::Shading shading;
    shading.blend = blend;
    shading.masked = s_depthMaskKey != nullptr;
    bool any = false;
    bool negative = false;
    bool black = true;

    for (int channel = 0; channel < 3; ++channel)
    {
        shading.add[channel] = vertices[0].add[channel];
        any = any || shading.add[channel] != 0;
        negative = negative || shading.add[channel] < 0;
    }

    for (const auto & decoded : vertices)
    {
        for (int channel = 0; channel < 3; ++channel)
        {
            if (decoded.add[channel] != shading.add[channel])
            {
                ps2::renderer::Unavailable("native UI varying additive color");
            }
        }

        black = black && decoded.vertex.r == 0 && decoded.vertex.g == 0 && decoded.vertex.b == 0;
    }

    // A zero color multiply leaves only the additive color, drawn in one constant-color pass.
    shading.replaceColor = any && black && !negative;
    return shading;
}

idDrawVert * Allocate(int vertexCount, const triIndex_t * indexes, int indexCount, const Profile & profile)
{
    Require(&tr, "native UI indexed allocation", true);
    if (s_flushing || vertexCount <= 0 || vertexCount > ps2::ui::kMaxVertices || indexes == nullptr ||
        indexCount <= 0 || indexCount > ps2::ui::kMaxIndexes || indexCount % 3 != 0 ||
        (profile.blend != ps2::renderer::gs::BlendMode::Replace && profile.blend != ps2::renderer::gs::BlendMode::StraightAlpha &&
         profile.blend != ps2::renderer::gs::BlendMode::AdditiveAlpha && profile.blend != ps2::renderer::gs::BlendMode::Screen))
    {
        ps2::FatalError("invalid native UI indexed request");
    }
    ps2::renderer::ValidateUiImage(profile.image);
    for (int index = 0; index < indexCount; ++index)
    {
        if (indexes[index] >= vertexCount)
        {
            ps2::FatalError("native UI index outside vertex span");
        }
    }
    // Prior callers have completed their write-only spans; reject oversized requests before any flush.
    if (vertexCount > ps2::ui::kMaxVertices - s_vertexCount || indexCount > ps2::ui::kMaxIndexes - s_indexCount ||
        s_batchCount == ps2::ui::kMaxDraws)
    {
        ps2::ui::Flush();
    }
    idDrawVert * const output = s_vertices + s_vertexCount;
    s_batches[s_batchCount++] = { profile, s_vertexCount, vertexCount, s_indexCount, indexCount };
    memcpy(s_indexes + s_indexCount, indexes, static_cast<size_t>(indexCount) * sizeof(*indexes));
    s_vertexCount += vertexCount;
    s_indexCount += indexCount;
    if (static_cast<u32>(s_vertexCount) > s_geometryStats.peakVertices)
    {
        s_geometryStats.peakVertices = static_cast<u32>(s_vertexCount);
    }
    if (static_cast<u32>(s_indexCount) > s_geometryStats.peakIndexes)
    {
        s_geometryStats.peakIndexes = static_cast<u32>(s_indexCount);
    }
    if (static_cast<u32>(s_batchCount) > s_geometryStats.peakDraws)
    {
        s_geometryStats.peakDraws = static_cast<u32>(s_batchCount);
    }
    return output;
}

void CountTriangles(u32 count)
{
    if (s_geometryStats.triangles > UINT_MAX - count)
    {
        ps2::FatalError("native UI geometry counter overflow");
    }

    s_geometryStats.triangles += count;
}

bool HasColorTerm(const ps2::renderer::Shading & shading)
{
    return shading.replaceColor || shading.add[0] != 0 || shading.add[1] != 0 || shading.add[2] != 0;
}

// Screen passes carry the source in alpha; an additive term has no place in that equation.
void ValidateShading(const ps2::renderer::Shading & shading)
{
    if (shading.blend == ps2::renderer::gs::BlendMode::Screen && HasColorTerm(shading))
    {
        ps2::renderer::Unavailable("native SWF screen blending with an additive color term");
    }
}

// Counts a drawn source primitive's screen blending and additive color term.
void CountShading(const ps2::renderer::Shading & shading)
{
    const bool colorTerm = HasColorTerm(shading);

    if (s_geometryStats.screen == UINT_MAX || s_geometryStats.colorTerms == UINT_MAX)
    {
        ps2::FatalError("native UI geometry counter overflow");
    }

    s_geometryStats.screen += shading.blend == ps2::renderer::gs::BlendMode::Screen ? 1U : 0U;
    s_geometryStats.colorTerms += colorTerm ? 1U : 0U;
}

void Triangle(const ps2::ui::Vertex (&input)[3], const idImage * image,
              const ps2::renderer::Shading & shading, bool emit)
{
    using namespace ps2::ui;
    FixedTriangle fixed[kMaxClippedTriangles];
    auto * const pages = ps2::renderer::UiPageImage(image);
    int fixedCount = 0;
    // Validate the original input too: an empty/offscreen mask must never hide malformed UVs.
    if (pages != nullptr ? !pages->ValidateTriangle(input) : MapTriangle(input, { ps2::renderer::gs::Width(), ps2::renderer::gs::Height() }, image->GetUploadWidth(), image->GetUploadHeight(), fixed, fixedCount) == Result::Invalid)
    {
        ps2::FatalError("invalid native UI triangle or UV");
    }

    // A screen pass reads one color channel as alpha: white texels, or a paletted channel CLUT.
    if (shading.blend == ps2::renderer::gs::BlendMode::Screen &&
        (pages != nullptr ? !pages->SupportsScreen() : image != ps2::renderer::ConsoleWhiteImage()))
    {
        ps2::renderer::Unavailable("native SWF screen blending over this image");
    }

    ValidateShading(shading);

    if (emit)
    {
        CountShading(shading);
    }

    // Most text/console geometry has no mask. Keep it on the original page/fixed path.
    if (s_maskCount == 0)
    {
        if (emit && pages != nullptr)
        {
            const u32 before = pages->GetStats().triangles;
            if (!pages->DrawTriangle(input, shading))
            {
                ps2::FatalError("native UI paged triangle failed");
            }

            CountTriangles(pages->GetStats().triangles - before);
        }
        else if (emit)
        {
            CountTriangles(static_cast<u32>(fixedCount));
            for (int triangle = 0; triangle < fixedCount; ++triangle)
            {
                if (!ps2::renderer::TexturedTriangleFixed(ps2::renderer::ConsoleImageTexture(image), fixed[triangle], shading))
                {
                    ps2::FatalError("native UI triangle texture residency failed");
                }
            }
        }

        return;
    }

    Vertex clipped[kMaxMaskClipVertices];
    int count = 0;
    if (!ClipMaskTriangle(input, s_masks, s_maskCount, clipped, count))
    {
        ps2::FatalError("native UI mask clipping exceeds bounded geometry");
    }

    for (int i = 1; i + 1 < count; ++i)
    {
        const Vertex piece[3] = { clipped[0], clipped[i], clipped[i + 1] };
        if (pages != nullptr)
        {
            if (!pages->ValidateTriangle(piece))
            {
                ps2::FatalError("invalid native UI masked page geometry");
            }

            if (emit)
            {
                const u32 before = pages->GetStats().triangles;
                if (!pages->DrawTriangle(piece, shading))
                {
                    ps2::FatalError("native UI paged triangle failed");
                }

                CountTriangles(pages->GetStats().triangles - before);
            }
        }
        else
        {
            if (MapTriangle(piece, { ps2::renderer::gs::Width(), ps2::renderer::gs::Height() }, image->GetUploadWidth(),
                            image->GetUploadHeight(), fixed, fixedCount) == Result::Invalid)
            {
                ps2::FatalError("invalid native UI masked triangle");
            }

            if (emit)
            {
                CountTriangles(static_cast<u32>(fixedCount));
                for (int triangle = 0; triangle < fixedCount; ++triangle)
                {
                    if (!ps2::renderer::TexturedTriangleFixed(ps2::renderer::ConsoleImageTexture(image), fixed[triangle], shading))
                    {
                        ps2::FatalError("native UI triangle texture residency failed");
                    }
                }
            }
        }
    }
}

void Line(const ps2::ui::Vertex (&input)[2], const ps2::renderer::Shading & shading, bool emit)
{
    using namespace ps2::ui;
    const Viewport viewport{ ps2::renderer::gs::Width(), ps2::renderer::gs::Height() };
    FixedLine fixed = {};

    // Validate the original segment even when a mask or the screen hides all of it.
    if (MapLine(input, viewport, fixed) == Result::Invalid)
    {
        ps2::FatalError("invalid native UI line");
    }

    ValidateShading(shading);

    Vertex visible[2] = { input[0], input[1] };
    bool shown = true;

    if (s_maskCount != 0 && !ClipMaskLine(input, s_masks, s_maskCount, visible, shown))
    {
        ps2::FatalError("native UI mask clipping rejects a line");
    }

    if (!emit || !shown || MapLine(visible, viewport, fixed) != Result::Draw)
    {
        return;
    }

    CountShading(shading);

    if (s_geometryStats.lines == UINT_MAX)
    {
        ps2::FatalError("native UI geometry counter overflow");
    }

    ++s_geometryStats.lines;

    if (!ps2::renderer::LineFixed(fixed, shading))
    {
        ps2::FatalError("native UI line submission failed");
    }
}

void Emit(const ps2::ui::Rect & rect, const idImage * image, ps2::renderer::gs::Color color, ps2::renderer::gs::BlendMode blend)
{
    if (s_captureMask != nullptr)
    {
        ps2::FatalError("native SWF mask requires indexed fill geometry");
    }
    if (image == nullptr || !image->IsLoaded())
    {
        ps2::renderer::Unavailable("native UI unloaded image");
    }
    auto * const pages = ps2::renderer::UiPageImage(image);

    // PageImage validates full-atlas UVs, including the admitted retail font's outer half texel.
    // MapRect validates rectangle positions/sizes independently of texture coordinates here.
    auto validation = rect;

    if (pages != nullptr)
    {
        validation.s0 = validation.t0 = 0.0f;
        validation.s1 = validation.t1 = 1.0f;
    }

    ps2::ui::FixedRect fixed = {};
    const auto result = ps2::ui::MapRect(validation, { ps2::renderer::gs::Width(), ps2::renderer::gs::Height() },
                                         pages != nullptr ? 1 : image->GetUploadWidth(),
                                         pages != nullptr ? 1 : image->GetUploadHeight(), fixed);
    if (result == ps2::ui::Result::Invalid)
    {
        ps2::FatalError("invalid native UI rectangle or UV");
    }
    if (result == ps2::ui::Result::Clipped && pages == nullptr)
    {
        return;
    }

    ps2::renderer::Shading shading;
    shading.blend = blend;
    shading.masked = s_depthMaskKey != nullptr;

    if (pages != nullptr || s_maskCount != 0 || shading.masked || blend == ps2::renderer::gs::BlendMode::Screen)
    {
        const ps2::ui::Vertex corners[] = {
            { rect.x, rect.y, rect.s0, rect.t0, color.r, color.g, color.b, color.a },
            { rect.x + rect.width, rect.y, rect.s1, rect.t0, color.r, color.g, color.b, color.a },
            { rect.x + rect.width, rect.y + rect.height, rect.s1, rect.t1, color.r, color.g, color.b, color.a },
            { rect.x, rect.y + rect.height, rect.s0, rect.t1, color.r, color.g, color.b, color.a }
        };
        const ps2::ui::Vertex triangles[][3] = {
            { corners[0], corners[1], corners[2] },
            { corners[0], corners[2], corners[3] }
        };

        for (const auto & triangle : triangles)
        {
            Triangle(triangle, image, shading, false);
        }

        if (result == ps2::ui::Result::Clipped)
        {
            return;
        }

        // Direct glyph/quad draws must retire older indexed SWF runs before loading cache pages.
        ps2::ui::Flush();

        for (const auto & triangle : triangles)
        {
            Triangle(triangle, image, shading, true);
        }

        return;
    }

    if (!ps2::renderer::TexturedRectFixed(ps2::renderer::ConsoleImageTexture(image), fixed, color, blend))
    {
        ps2::FatalError("native UI texture residency failed");
    }
}

void Character(int x, int y, int character, bool big)
{
    const int ch = character & 255;
    if (ch == ' ')
    {
        return;
    }
    const int width = big ? BIGCHAR_WIDTH : SMALLCHAR_WIDTH;
    const int height = SMALLCHAR_HEIGHT;
    const float s = static_cast<float>(ch & 15) / 16.0f;
    const float t = static_cast<float>(ch >> 4) / 16.0f;
    Emit({ static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height),
           s, t, s + 0.0625f, t + 0.0625f },
         ps2::renderer::ConsoleCharsetImage(), CurrentColor(), Blend(CombinedState(kBlendAlpha)));
}

void String(idRenderSystemLocal * owner, int x, int y, const char * string, const idVec4 & color, bool forceColor, bool big)
{
    if (string == nullptr)
    {
        ps2::FatalError("native UI text is null");
    }
    int bytes = 0;
    while (bytes < MAX_STRING_CHARS && string[bytes] != '\0')
    {
        ++bytes;
    }
    if (bytes == MAX_STRING_CHARS || x < -32768 || x > 32768 || y < -32768 || y > 32768)
    {
        ps2::FatalError("native UI text exceeds bounded input profile");
    }
    int glyphs = 0;
    for (int i = 0; i < bytes; ++i)
    {
        if (idStr::IsColor(string + i))
        {
            ++i;
        }
        else
        {
            ++glyphs;
        }
    }
    const int advance = big ? BIGCHAR_WIDTH : SMALLCHAR_WIDTH;
    if (glyphs > (32768 - x) / advance)
    {
        ps2::FatalError("native UI text coordinate overflow");
    }
    owner->SetColor(color);
    const auto * cursor = reinterpret_cast<const unsigned char *>(string);
    int pen = x;
    while (*cursor != 0)
    {
        if (idStr::IsColor(reinterpret_cast<const char *>(cursor)))
        {
            if (!forceColor)
            {
                idVec4 next = cursor[1] == C_COLOR_DEFAULT ? color : idStr::ColorForIndex(cursor[1]);
                next[3] = color[3];
                owner->SetColor(next);
            }
            cursor += 2;
            continue;
        }
        Character(pen, y, *cursor, big);
        pen += advance;
        ++cursor;
    }
    owner->SetColor(colorWhite);
}
} // namespace

namespace ps2::ui
{
bool IsInitialized()
{
    return s_initialized;
}

PS2_COLD_FUNC bool Init()
{
    if (s_initialized || tr.registered || tr.guiModel != nullptr || renderer::ConsoleImagesInitialized())
    {
        FatalError("native UI initialized with existing ownership");
    }
    if (!renderer::IsInitialized() || renderer::FrameStarted())
    {
        return false;
    }
    s_vertices = static_cast<idDrawVert *>(system::heap::TryAlloc(sizeof(idDrawVert) * kMaxVertices, TAG_RENDER, 16));
    s_indexes = static_cast<triIndex_t *>(system::heap::TryAlloc(sizeof(triIndex_t) * kMaxIndexes, TAG_RENDER, 16));
    s_batches = static_cast<Batch *>(system::heap::TryAlloc(sizeof(Batch) * kMaxDraws, TAG_RENDER, 16));
    s_captureTriangles = static_cast<MaskPoint(*)[3]>(system::heap::TryAlloc(sizeof(MaskPoint[3]) * kMaxCaptureTriangles, TAG_RENDER, 16));
    s_captureLines = static_cast<MaskPoint(*)[2]>(system::heap::TryAlloc(sizeof(MaskPoint[2]) * kMaxCaptureLines, TAG_RENDER, 16));
    if (s_vertices == nullptr || s_indexes == nullptr || s_batches == nullptr || s_captureTriangles == nullptr ||
        s_captureLines == nullptr || !renderer::InitConsoleImages())
    {
        FreeGeometry();
        return false;
    }
    s_geometryStats = {};
    tr.registered = true;
    tr.currentGLState = 0;
    tr.currentColorNativeBytesOrder = 0xFFFFFFFFU;
    s_initialized = true;
    Log(LogLevel::Info, "[D3BFG] UI adapters ready virtual=640x480 physical=%dx%d\n", renderer::gs::Width(), renderer::gs::Height());
    return true;
}

PS2_COLD_FUNC void Shutdown()
{
    if (!s_initialized)
    {
        return;
    }
    if (renderer::FrameStarted())
    {
        FatalError("native UI shutdown with an open frame");
    }
    if (s_batchCount != 0 || s_flushing || s_swfDraw || s_captureMask != nullptr || s_maskCount != 0 || s_depthMaskKey != nullptr)
    {
        FatalError("native UI shutdown with pending geometry");
    }
    FreeGeometry();
    renderer::ShutdownConsoleImages();
    tr.whiteMaterial = tr.charSetMaterial = nullptr;
    tr.registered = false;
    tr.currentGLState = 0;
    tr.currentColorNativeBytesOrder = 0xFFFFFFFFU;
    s_initialized = false;
}

GeometryStats GetGeometryStats()
{
    return s_geometryStats;
}

void BeginSwfDraw()
{
    Require(&tr, "native SWF draw scope", true);
    if (s_swfDraw || s_captureMask != nullptr || s_maskCount != 0 || s_depthMaskKey != nullptr)
    {
        FatalError("nested native SWF drawing");
    }
    Flush();
    s_savedSwfState = tr.currentGLState;
    s_savedSwfColor = tr.currentColorNativeBytesOrder;
    s_swfDraw = true;
}

void EndSwfDraw()
{
    if (!s_swfDraw)
    {
        FatalError("native SWF draw scope not active");
    }

    if (s_captureMask != nullptr || s_maskCount != 0 || s_depthMaskKey != nullptr)
    {
        FatalError("native SWF draw ended with outstanding mask ownership");
    }

    Flush();
    s_swfDraw = false;
    tr.currentGLState = s_savedSwfState;
    tr.currentColorNativeBytesOrder = s_savedSwfColor;
}

void BeginSwfMask(const void * key)
{
    Require(&tr, "native SWF mask capture", true);
    if (!s_swfDraw || s_flushing || s_captureMask != nullptr || key == nullptr || key == s_depthMaskKey)
    {
        FatalError("invalid native SWF mask begin");
    }

    for (int i = 0; i < s_maskCount; ++i)
    {
        if (s_maskKeys[i] == key)
        {
            FatalError("duplicate native SWF mask key");
        }
    }

    Flush();
    s_maskBuilder.Reset();
    s_captureTriangleCount = s_captureLineCount = 0;
    s_captureOverflow = false;
    s_captureMask = key;
}

void EndSwfMask()
{
    Require(&tr, "native SWF mask completion", true);
    if (!s_swfDraw || s_flushing || s_captureMask == nullptr)
    {
        FatalError("native SWF mask capture not active");
    }

    Flush();

    if (s_geometryStats.masks == UINT_MAX || s_geometryStats.depthMasks == UINT_MAX)
    {
        FatalError("native UI geometry counter overflow");
    }

    // The convex CPU bridge keeps exact geometric clipping; outlines and any other fill topology
    // become the one depth mask, whose coverage follows the GS rasterization of the native geometry.
    if (s_captureLineCount == 0 && s_maskCount < kMaxActiveMasks && s_maskBuilder.Finish(s_masks[s_maskCount]))
    {
        s_maskKeys[s_maskCount++] = s_captureMask;
    }
    else
    {
        if (s_depthMaskKey != nullptr)
        {
            renderer::Unavailable("nested native SWF depth masks");
        }

        if (s_captureOverflow)
        {
            FatalError("native SWF mask exceeds bounded fill geometry");
        }

        WriteDepthMask();
        s_depthMaskKey = s_captureMask;
        ++s_geometryStats.depthMasks;
    }

    s_captureMask = nullptr;
    ++s_geometryStats.masks;
    s_geometryStats.peakMasks = Max(s_geometryStats.peakMasks, static_cast<u32>(s_maskCount + (s_depthMaskKey != nullptr ? 1 : 0)));
}

void RemoveSwfMask(const void * key)
{
    Require(&tr, "native SWF mask removal", true);
    if (!s_swfDraw || s_flushing || s_captureMask != nullptr || key == nullptr)
    {
        FatalError("invalid native SWF mask removal");
    }

    if (key == s_depthMaskKey)
    {
        // Only this mask wrote depth 0, and only inside its bounds: restore 1 there.
        Flush();
        renderer::ClearMask(s_depthBounds[0], s_depthBounds[1], s_depthBounds[2], s_depthBounds[3]);
        s_depthMaskKey = nullptr;
        return;
    }

    int index = 0;
    while (index < s_maskCount && s_maskKeys[index] != key)
    {
        ++index;
    }

    if (index == s_maskCount)
    {
        FatalError("unknown native SWF mask key");
    }

    Flush();
    for (int i = index; i + 1 < s_maskCount; ++i)
    {
        s_masks[i] = s_masks[i + 1];
        s_maskKeys[i] = s_maskKeys[i + 1];
    }

    s_masks[--s_maskCount] = {};
    s_maskKeys[s_maskCount] = nullptr;
}

void ValidateSwfMaskState(int activeMasks, bool capturing)
{
    const int active = s_maskCount + (s_depthMaskKey != nullptr ? 1 : 0);

    if (!s_swfDraw || s_flushing || capturing != (s_captureMask != nullptr) ||
        activeMasks != (capturing ? 0 : active))
    {
        FatalError("native SWF mask state disagrees with active geometry");
    }
}

void BeginGeometryFrame()
{
    if (!s_initialized)
    {
        return;
    }
    if (s_batchCount != 0 || s_flushing || s_swfDraw || s_captureMask != nullptr || s_maskCount != 0 || s_depthMaskKey != nullptr)
    {
        FatalError("native UI geometry crossed a frame boundary");
    }
}

void EndGeometryFrame()
{
    if (s_initialized && (s_swfDraw || s_captureMask != nullptr || s_maskCount != 0 || s_depthMaskKey != nullptr))
    {
        FatalError("native UI frame ended with outstanding SWF ownership");
    }

    Flush();
}

void DrawImageRect(float x, float y, float width, float height, float s0, float t0, float s1, float t1,
                   const idImage * image, const idMaterial * material)
{
    Require(&tr, "native UI image rectangle", true);
    const Profile profile = MaterialProfile(material, CurrentColor());

    if (profile.enabled)
    {
        Emit({ x, y, width, height, s0, t0, s1, t1 }, image, profile.tint, profile.blend);
    }
}

idDrawVert * AllocTrisForImage(int vertices, const u16 * indexes, int indexCount, const idImage * image, renderer::gs::BlendMode blend)
{
    return Allocate(vertices, indexes, indexCount, { image, blend, { 255, 255, 255, 255 }, true, true });
}

void DrawIndexed(const idDrawVert * vertices, int vertexCount, const u16 * indexes, int indexCount,
                 const idImage * image, renderer::gs::BlendMode blend)
{
    if (vertices == nullptr)
    {
        FatalError("native UI indexed vertices are null");
    }
    idDrawVert * const output = AllocTrisForImage(vertexCount, indexes, indexCount, image, blend);
    memmove(static_cast<void *>(output), vertices, static_cast<size_t>(vertexCount) * sizeof(*vertices));
}

void Flush()
{
    if (!s_initialized || s_flushing || s_batchCount == 0)
    {
        return;
    }
    Require(&tr, "native UI geometry flush", true);
    PS2_Assert(s_vertexCount > 0 && s_indexCount > 0);
    if (s_geometryStats.flushes == UINT_MAX)
    {
        FatalError("native UI geometry counter overflow");
    }

    ++s_geometryStats.flushes;
    if (s_captureMask != nullptr)
    {
        // Mask fills and outlines are coverage, independent of UV/color or existing parent clips.
        for (int batchIndex = 0; batchIndex < s_batchCount; ++batchIndex)
        {
            const Batch & batch = s_batches[batchIndex];
            if (!batch.profile.enabled)
            {
                continue;
            }

            for (int first = 0; first < batch.indexCount; first += 3)
            {
                MaskPoint points[3];
                for (int corner = 0; corner < 3; ++corner)
                {
                    const int index = s_indexes[batch.firstIndex + first + corner];
                    const auto & position = s_vertices[batch.firstVertex + index].xyz;
                    if (!PortableFloat(position.x) || !PortableFloat(position.y) || !PortableFloat(position.z) || position.z != 0.0f)
                    {
                        FatalError("native SWF mask vertex requires zero depth");
                    }

                    points[corner] = { position.x, position.y };
                }

                // A line-mode triangle covers its edges; the convex bridge takes fills only.
                if (batch.profile.lines)
                {
                    for (int edge = 0; edge < 3; ++edge)
                    {
                        const MaskPoint from = points[edge];
                        const MaskPoint to = points[(edge + 1) % 3];

                        if (from.x == to.x && from.y == to.y)
                        {
                            continue;
                        }

                        if (s_captureLineCount == kMaxCaptureLines)
                        {
                            FatalError("native SWF mask exceeds bounded outline geometry");
                        }

                        s_captureLines[s_captureLineCount][0] = from;
                        s_captureLines[s_captureLineCount++][1] = to;
                    }

                    continue;
                }

                // A rejected convex capture stays rejected; the depth mask still needs the triangle.
                (void)s_maskBuilder.AddTriangle(points);

                if (s_captureTriangleCount == kMaxCaptureTriangles)
                {
                    s_captureOverflow = true;
                    continue;
                }

                for (int corner = 0; corner < 3; ++corner)
                {
                    s_captureTriangles[s_captureTriangleCount][corner] = points[corner];
                }

                ++s_captureTriangleCount;
            }
        }

        s_vertexCount = s_indexCount = s_batchCount = 0;
        return;
    }

    const auto Visit = [](const Batch & batch, int first, bool emit)
    {
        DecodedVertex decoded[3];
        for (int corner = 0; corner < 3; ++corner)
        {
            const int index = s_indexes[batch.firstIndex + first + corner];
            decoded[corner] = DecodeVertex(s_vertices[batch.firstVertex + index], batch.profile);
        }

        if (batch.profile.lines)
        {
            // Native line mode draws all three edges, as GL does: an outline segment arrives as
            // (a, a, b) and draws a to b in both directions, while its zero-length edge draws nothing.
            for (int edge = 0; edge < 3; ++edge)
            {
                const DecodedVertex ends[2] = { decoded[edge], decoded[(edge + 1) % 3] };

                if (ends[0].vertex.x == ends[1].vertex.x && ends[0].vertex.y == ends[1].vertex.y)
                {
                    continue;
                }

                const Vertex segment[2] = { ends[0].vertex, ends[1].vertex };
                Line(segment, MakeShading(ends, batch.profile.blend), emit);
            }

            return;
        }

        const Vertex input[3] = { decoded[0].vertex, decoded[1].vertex, decoded[2].vertex };
        Triangle(input, batch.profile.image, MakeShading(decoded, batch.profile.blend), emit);
    };
    // Preflight the complete collection, including clipped-away inputs, before any GS emission.
    for (int batchIndex = 0; batchIndex < s_batchCount; ++batchIndex)
    {
        const Batch & batch = s_batches[batchIndex];
        renderer::ValidateUiImage(batch.profile.image);
        for (int index = 0; index < batch.indexCount; index += 3)
        {
            Visit(batch, index, false);
        }
    }

    s_flushing = true;
    for (int batchIndex = 0; batchIndex < s_batchCount; ++batchIndex)
    {
        const Batch & batch = s_batches[batchIndex];
        if (!batch.profile.enabled)
        {
            continue;
        }

        for (int index = 0; index < batch.indexCount; index += 3)
        {
            Visit(batch, index, true);
        }
    }
    s_vertexCount = s_indexCount = s_batchCount = 0;
    s_flushing = false;
}
} // namespace ps2::ui

int idRenderSystemLocal::GetWidth() const
{
    Require(this, "idRenderSystemLocal::GetWidth");
    return ps2::renderer::gs::Width();
}

int idRenderSystemLocal::GetHeight() const
{
    Require(this, "idRenderSystemLocal::GetHeight");
    return ps2::renderer::gs::Height();
}

float idRenderSystemLocal::GetPixelAspect() const
{
    Require(this, "idRenderSystemLocal::GetPixelAspect");
    return 4.0f * static_cast<float>(ps2::renderer::gs::Height()) / (3.0f * static_cast<float>(ps2::renderer::gs::Width()));
}

void idRenderSystemLocal::SetColor(const idVec4 & color)
{
    Require(this, "idRenderSystemLocal::SetColor");
    const uint32 packed = static_cast<uint32>(ByteColor(color[0])) | (static_cast<uint32>(ByteColor(color[1])) << 8) |
                          (static_cast<uint32>(ByteColor(color[2])) << 16) | (static_cast<uint32>(ByteColor(color[3])) << 24);
    currentColorNativeBytesOrder = packed;
}

uint32 idRenderSystemLocal::GetColor()
{
    Require(this, "idRenderSystemLocal::GetColor");
    return currentColorNativeBytesOrder;
}

void idRenderSystemLocal::SetGLState(uint64 state)
{
    Require(this, "idRenderSystemLocal::SetGLState");
    (void)Blend(state);
    currentGLState = state;
}

void idRenderSystemLocal::DrawFilled(const idVec4 & color, float x, float y, float width, float height)
{
    Require(this, "idRenderSystemLocal::DrawFilled", true);
    SetColor(color);
    Emit({ x, y, width, height, 0.0f, 0.0f, 1.0f, 1.0f }, ps2::renderer::ConsoleWhiteImage(), CurrentColor(), Blend(CombinedState(kBlendAlpha)));
}

void idRenderSystemLocal::DrawStretchPic(float x, float y, float width, float height,
                                         float s0, float t0, float s1, float t1, const idMaterial * material)
{
    Require(this, "idRenderSystemLocal::DrawStretchPic", true);
    const Profile profile = MaterialProfile(material, CurrentColor());
    if (!profile.enabled)
    {
        return;
    }
    Emit({ x, y, width, height, s0, t0, s1, t1 }, profile.image, profile.tint, profile.blend);
}

void idRenderSystemLocal::DrawStretchPic(const idVec4 & tl, const idVec4 & trCorner, const idVec4 & br, const idVec4 & bl, const idMaterial * material)
{
    Require(this, "idRenderSystemLocal::DrawStretchPic", true);
    const idVec4 * const corners[] = { &tl, &trCorner, &br, &bl };
    for (const auto * corner : corners)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (!PortableFloat((*corner)[i]) || (*corner)[i] < -32768.0f || (*corner)[i] > 32768.0f)
            {
                ps2::FatalError("invalid native UI quad coordinate");
            }
        }
    }
    if (tl.y == trCorner.y && bl.y == br.y && tl.x == bl.x && trCorner.x == br.x &&
        tl.z == bl.z && trCorner.z == br.z && tl.w == trCorner.w && bl.w == br.w &&
        trCorner.x >= tl.x && bl.y >= tl.y && trCorner.x - tl.x <= 32768.0f && bl.y - tl.y <= 32768.0f &&
        br.z >= tl.z && br.w >= tl.w)
    {
        DrawStretchPic(tl.x, tl.y, trCorner.x - tl.x, bl.y - tl.y, tl.z, tl.w, br.z, br.w, material);
        return;
    }
    static constexpr triIndex_t kQuadIndexes[] = { 0, 1, 2, 0, 2, 3 };
    const Profile profile = MaterialProfile(material, CurrentColor());
    const bool paged = ps2::renderer::UiPageImage(profile.image) != nullptr;
    for (const auto * corner : corners)
    {
        if (!paged && (corner->z < 0.0f || corner->w < 0.0f ||
                       corner->z > 1023.0f / static_cast<float>(profile.image->GetUploadWidth()) ||
                       corner->w > 1023.0f / static_cast<float>(profile.image->GetUploadHeight())))
        {
            ps2::FatalError("invalid native UI triangle or UV");
        }
    }
    idDrawVert * const vertices = Allocate(4, kQuadIndexes, 6, profile);
    for (int corner = 0; corner < 4; ++corner)
    {
        vertices[corner].Clear();
        vertices[corner].xyz.Set((*corners[corner])[0], (*corners[corner])[1], 0.0f);
        vertices[corner].SetTexCoord((*corners[corner])[2], (*corners[corner])[3]);
        vertices[corner].SetNativeOrderColor(0xFFFFFFFFU);
        vertices[corner].ClearColor2();
    }
}

idDrawVert * idRenderSystemLocal::AllocTris(int vertices, const triIndex_t * indexes, int count,
                                            const idMaterial * material, stereoDepthType_t stereo)
{
    Require(this, "idRenderSystemLocal::AllocTris", true);
    // Native GUI surfaces sort by stereo depth only with stereoscopic rendering on; mono ignores it.
    if (stereo != STEREO_DEPTH_TYPE_NONE && stereo != STEREO_DEPTH_TYPE_NEAR && stereo != STEREO_DEPTH_TYPE_MID &&
        stereo != STEREO_DEPTH_TYPE_FAR)
    {
        ps2::FatalError("invalid native UI stereo depth");
    }
    // Native SWF shapes can hold empty fill records; their callers skip a null span.
    if (material == nullptr || (vertices == 0 && count == 0))
    {
        return nullptr;
    }
    return Allocate(vertices, indexes, count, MaterialProfile(material, { 255, 255, 255, 255 }));
}

void idRenderSystemLocal::DrawStretchTri(const idVec2 & p1, const idVec2 & p2, const idVec2 & p3,
                                         const idVec2 & t1, const idVec2 & t2, const idVec2 & t3, const idMaterial * material)
{
    Require(this, "idRenderSystemLocal::DrawStretchTri", true);
    if (material == nullptr)
    {
        return;
    }
    static constexpr triIndex_t kIndexes[] = { 1, 0, 2 };
    const Profile profile = MaterialProfile(material, CurrentColor());
    const idVec2 positions[] = { p1, p2, p3 };
    const idVec2 coords[] = { t1, t2, t3 };
    ps2::ui::Vertex input[3];
    for (int corner = 0; corner < 3; ++corner)
    {
        input[corner] = { positions[corner].x, positions[corner].y, coords[corner].x, coords[corner].y, 255, 255, 255, 255 };
    }
    ps2::ui::FixedTriangle output[ps2::ui::kMaxClippedTriangles];
    int count = 0;
    auto * const pages = ps2::renderer::UiPageImage(profile.image);
    if (pages != nullptr ? !pages->ValidateTriangle(input)
                         : ps2::ui::MapTriangle(input, { ps2::renderer::gs::Width(), ps2::renderer::gs::Height() }, profile.image->GetUploadWidth(),
                                                profile.image->GetUploadHeight(), output, count) == ps2::ui::Result::Invalid)
    {
        ps2::FatalError("invalid native UI triangle or UV");
    }
    idDrawVert * const vertices = Allocate(3, kIndexes, 3, profile);
    for (int corner = 0; corner < 3; ++corner)
    {
        vertices[corner].Clear();
        vertices[corner].xyz.Set(positions[corner].x, positions[corner].y, 0.0f);
        vertices[corner].SetTexCoord(coords[corner]);
        vertices[corner].SetNativeOrderColor(0xFFFFFFFFU);
        vertices[corner].ClearColor2();
    }
}

void idRenderSystemLocal::DrawSmallChar(int x, int y, int ch)
{
    Require(this, "idRenderSystemLocal::DrawSmallChar", true);
    Character(x, y, ch, false);
}

void idRenderSystemLocal::DrawBigChar(int x, int y, int ch)
{
    Require(this, "idRenderSystemLocal::DrawBigChar", true);
    Character(x, y, ch, true);
}

void idRenderSystemLocal::DrawSmallStringExt(int x, int y, const char * text, const idVec4 & color, bool forceColor)
{
    Require(this, "idRenderSystemLocal::DrawSmallStringExt", true);
    String(this, x, y, text, color, forceColor, false);
}

void idRenderSystemLocal::DrawBigStringExt(int x, int y, const char * text, const idVec4 & color, bool forceColor)
{
    Require(this, "idRenderSystemLocal::DrawBigStringExt", true);
    String(this, x, y, text, color, forceColor, true);
}
