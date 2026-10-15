// ================================================================================================
// File: ui_draw.cpp
// Brief: Adapt native quad/text contracts to bounded ordered GS sprites on a 640x480 canvas.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/ui_draw.h"
#include "ps2/renderer/image_backend.h"
#include "ps2/renderer/null_render.h"
#include "ps2/renderer/render_system.h"
#include "ps2/system/log.h"
#include "ps2/system/heap.h"
#include <idlib/precompiled.h>
#include <renderer/tr_local.h>
#include <bit>
#include <cstdint>

namespace
{
static bool s_initialized = false;
static bool s_swfDraw = false;
static uint64 s_savedSwfState = 0;
static uint32 s_savedSwfColor = 0;
// The EE's native RGBA byte order is little endian; no idlib swap initialization is required.
static_assert(std::endian::native == std::endian::little);
static constexpr uint64 kBlendAlpha = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
static constexpr uint64 kBlendSwf = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
static constexpr uint64 kAllowedState = GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS | GLS_DEPTHMASK | GLS_DEPTHFUNC_BITS | GLS_OVERRIDE;
struct Profile
{
    const idImage * image;
    ps2::gs::BlendMode blend;
    ps2::gs::Color tint;
    bool vertexColor;
    bool enabled;
    bool swf = false;
};
struct Batch { Profile profile; int firstVertex; int vertexCount; int firstIndex; int indexCount; };
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
    ps2::heap::Free(s_batches);
    ps2::heap::Free(s_indexes);
    ps2::heap::Free(s_vertices);
    s_batches = nullptr;
    s_indexes = nullptr;
    s_vertices = nullptr;
    s_vertexCount = s_indexCount = s_batchCount = 0;
}

void Require(const idRenderSystemLocal * owner, const char * method, bool drawing = false)
{
    PS2_Assert(owner != nullptr);
    if (!s_initialized || owner != &tr || !ps2::rs::IsInitialized() || (drawing && !ps2::rs::FrameStarted()))
    {
        ps2::render::Unavailable(method);
    }
}
bool PortableFloat(float value)
{
    const auto bits = std::bit_cast<std::uint32_t>(value) & 0x7FFFFFFFU;
    return bits < 0x7F800000U && (bits == 0 || bits >= 0x00800000U);
}
bool UnitValue(float value) { return PortableFloat(value) && value >= 0.0f && value <= 1.0f; }
u8 ByteColor(float value)
{
    if (!UnitValue(value)) { ps2::FatalError("invalid native UI color"); }
    return static_cast<u8>(value * 255.0f); // Match native PackColor/Ftob truncation.
}
ps2::gs::Color CurrentColor()
{
    const uint32 packed = tr.currentColorNativeBytesOrder;
    return {static_cast<u8>(packed & 255U), static_cast<u8>((packed >> 8) & 255U),
            static_cast<u8>((packed >> 16) & 255U), static_cast<u8>((packed >> 24) & 255U)};
}
ps2::gs::BlendMode Blend(uint64 state)
{
    const uint64 equation = state & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS);
    const uint64 depth = state & GLS_DEPTHFUNC_BITS;
    if ((state & ~kAllowedState) != 0 || (depth != GLS_DEPTHFUNC_LESS && depth != GLS_DEPTHFUNC_ALWAYS) ||
        (equation != GLS_DEFAULT && equation != kBlendAlpha && !(s_swfDraw && equation == kBlendSwf)))
    {
        ps2::render::Unavailable("native UI blend/state profile");
    }
    return equation == GLS_DEFAULT ? ps2::gs::BlendMode::Replace : ps2::gs::BlendMode::StraightAlpha;
}
uint64 CombinedState(uint64 stageState)
{
    return (tr.currentGLState & GLS_OVERRIDE) != 0 ? tr.currentGLState & ~GLS_OVERRIDE : tr.currentGLState | stageState;
}
Profile MaterialProfile(const idMaterial * material, ps2::gs::Color base)
{
    if (material == nullptr || material->GetNumStages() != 1 || material->ConstantRegisters() == nullptr ||
        material->TestMaterialFlag(MF_DEFAULTED | MF_POLYGONOFFSET) || material->HasSubview() || material->Deform() != DFRM_NONE ||
        material->GetNumRegisters() < 1 || material->GetNumRegisters() > MAX_EXPRESSION_REGISTERS)
    {
        ps2::render::Unavailable("native UI requires one constant material stage");
    }
    const shaderStage_t & stage = *material->GetStage(0);
    if (stage.lighting != SL_AMBIENT || stage.hasAlphaTest || stage.newStage != nullptr ||
        stage.texture.texgen != TG_EXPLICIT || stage.texture.hasMatrix || stage.texture.cinematic != nullptr ||
        stage.texture.dynamic != DI_STATIC || stage.privatePolygonOffset != 0.0f || stage.ignoreAlphaTest ||
        (stage.vertexColor != SVC_IGNORE && stage.vertexColor != SVC_MODULATE))
    {
        ps2::render::Unavailable("native UI material stage profile");
    }
    const int registers = material->GetNumRegisters();
    const auto Register = [registers, material](int index) {
        if (index < 0 || index >= registers) { ps2::FatalError("native UI material register outside bounds"); }
        const float value = material->ConstantRegisters()[index];
        if (!UnitValue(value)) { ps2::FatalError("native UI material register outside unit range"); }
        return value;
    };
    const float condition = Register(stage.conditionRegister);
    const u8 channels[] = {base.r, base.g, base.b, base.a};
    u8 modulated[4] = {};
    for (int i = 0; i < 4; ++i) { modulated[i] = static_cast<u8>(Register(stage.color.registers[i]) * static_cast<float>(channels[i])); }
    (void)ps2::render::ConsoleImageTexture(stage.texture.image);
    return {stage.texture.image, Blend(CombinedState(stage.drawStateBits)),
            {modulated[0], modulated[1], modulated[2], modulated[3]}, stage.vertexColor == SVC_MODULATE,
            condition != 0.0f, s_swfDraw};
}
float DecodeHalf(halfFloat_t value)
{
    const u32 bits = value;
    const u32 exponent = (bits >> 10) & 31U;
    const u32 mantissa = bits & 1023U;
    if (exponent == 31U) { ps2::FatalError("native UI half UV is not finite"); }
    // Native F16toF32 uses powf and maps Inf/NaN to small numbers. Decode checked bits directly.
    if (exponent != 0) { return std::bit_cast<float>(((bits & 32768U) << 16) | ((exponent + 112U) << 23) | (mantissa << 13)); }
    const float magnitude = static_cast<float>(mantissa) * (1.0f / 16777216.0f);
    return (bits & 32768U) != 0 ? -magnitude : magnitude;
}
ps2::ui::Vertex DecodeVertex(const idDrawVert & vertex, const Profile & profile)
{
    if (!PortableFloat(vertex.xyz.z) || vertex.xyz.z != 0.0f) { ps2::FatalError("native UI vertex requires zero depth"); }
    for (int channel = 0; channel < 4; ++channel)
    {
        if (vertex.color2[channel] != 128 && !(profile.swf && vertex.color2[channel] == 127))
        {
            ps2::render::Unavailable("native UI additive vertex color");
        }
    }
    const auto Modulate = [](u8 color, u8 tint) { return static_cast<u8>((static_cast<u32>(color) * tint + 127U) / 255U); };
    const auto & tint = profile.tint;
    return {vertex.xyz.x, vertex.xyz.y, DecodeHalf(vertex.st[0]), DecodeHalf(vertex.st[1]),
            profile.vertexColor ? Modulate(vertex.color[0], tint.r) : tint.r,
            profile.vertexColor ? Modulate(vertex.color[1], tint.g) : tint.g,
            profile.vertexColor ? Modulate(vertex.color[2], tint.b) : tint.b,
            profile.vertexColor ? Modulate(vertex.color[3], tint.a) : tint.a};
}
idDrawVert * Allocate(int vertexCount, const triIndex_t * indexes, int indexCount, const Profile & profile)
{
    Require(&tr, "native UI indexed allocation", true);
    if (s_flushing || vertexCount <= 0 || vertexCount > ps2::ui::kMaxVertices || indexes == nullptr ||
        indexCount <= 0 || indexCount > ps2::ui::kMaxIndexes || indexCount % 3 != 0 ||
        (profile.blend != ps2::gs::BlendMode::Replace && profile.blend != ps2::gs::BlendMode::StraightAlpha))
    {
        ps2::FatalError("invalid native UI indexed request");
    }
    (void)ps2::render::ConsoleImageTexture(profile.image);
    for (int index = 0; index < indexCount; ++index)
    {
        if (indexes[index] >= vertexCount) { ps2::FatalError("native UI index outside vertex span"); }
    }
    // Prior callers have completed their write-only spans; reject oversized requests before any flush.
    if (vertexCount > ps2::ui::kMaxVertices - s_vertexCount || indexCount > ps2::ui::kMaxIndexes - s_indexCount ||
        s_batchCount == ps2::ui::kMaxDraws)
    {
        ps2::ui::Flush();
    }
    idDrawVert * const output = s_vertices + s_vertexCount;
    s_batches[s_batchCount++] = {profile, s_vertexCount, vertexCount, s_indexCount, indexCount};
    memcpy(s_indexes + s_indexCount, indexes, static_cast<size_t>(indexCount) * sizeof(*indexes));
    s_vertexCount += vertexCount;
    s_indexCount += indexCount;
    if (static_cast<u32>(s_vertexCount) > s_geometryStats.peakVertices) { s_geometryStats.peakVertices = static_cast<u32>(s_vertexCount); }
    if (static_cast<u32>(s_indexCount) > s_geometryStats.peakIndexes) { s_geometryStats.peakIndexes = static_cast<u32>(s_indexCount); }
    if (static_cast<u32>(s_batchCount) > s_geometryStats.peakDraws) { s_geometryStats.peakDraws = static_cast<u32>(s_batchCount); }
    return output;
}
void Emit(const ps2::ui::Rect & rect, const idImage * image, ps2::gs::Color color, ps2::gs::BlendMode blend)
{
    if (image == nullptr || !image->IsLoaded()) { ps2::render::Unavailable("native UI unloaded image"); }
    const auto texture = ps2::render::ConsoleImageTexture(image);
    ps2::ui::FixedRect fixed = {};
    const auto result = ps2::ui::MapRect(rect, {ps2::gs::Width(), ps2::gs::Height()},
                                       image->GetUploadWidth(), image->GetUploadHeight(), fixed);
    if (result == ps2::ui::Result::Invalid) { ps2::FatalError("invalid native UI rectangle or UV"); }
    if (result == ps2::ui::Result::Clipped) { return; }
    if (!ps2::rs::TexturedRectFixed(texture, fixed, color, blend)) { ps2::FatalError("native UI texture residency failed"); }
}
void Character(int x, int y, int character, bool big)
{
    const int ch = character & 255;
    if (ch == ' ') { return; }
    const int width = big ? BIGCHAR_WIDTH : SMALLCHAR_WIDTH;
    const int height = SMALLCHAR_HEIGHT;
    const float s = static_cast<float>(ch & 15) / 16.0f;
    const float t = static_cast<float>(ch >> 4) / 16.0f;
    Emit({static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height),
          s, t, s + 0.0625f, t + 0.0625f}, ps2::render::ConsoleCharsetImage(), CurrentColor(), Blend(CombinedState(kBlendAlpha)));
}
void String(idRenderSystemLocal * owner, int x, int y, const char * string, const idVec4 & color, bool forceColor, bool big)
{
    if (string == nullptr) { ps2::FatalError("native UI text is null"); }
    int bytes = 0;
    while (bytes < MAX_STRING_CHARS && string[bytes] != '\0') { ++bytes; }
    if (bytes == MAX_STRING_CHARS || x < -32768 || x > 32768 || y < -32768 || y > 32768)
    {
        ps2::FatalError("native UI text exceeds bounded input profile");
    }
    int glyphs = 0;
    for (int i = 0; i < bytes; ++i)
    {
        if (idStr::IsColor(string + i)) { ++i; }
        else { ++glyphs; }
    }
    const int advance = big ? BIGCHAR_WIDTH : SMALLCHAR_WIDTH;
    if (glyphs > (32768 - x) / advance) { ps2::FatalError("native UI text coordinate overflow"); }
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
bool IsInitialized() { return s_initialized; }
PS2_COLD_FUNC bool Init()
{
    if (s_initialized || tr.registered || tr.guiModel != nullptr || render::ConsoleImagesInitialized())
    {
        FatalError("native UI initialized with existing ownership");
    }
    if (!rs::IsInitialized() || rs::FrameStarted()) { return false; }
    s_vertices = static_cast<idDrawVert *>(heap::TryAlloc(sizeof(idDrawVert) * kMaxVertices, TAG_RENDER, 16));
    s_indexes = static_cast<triIndex_t *>(heap::TryAlloc(sizeof(triIndex_t) * kMaxIndexes, TAG_RENDER, 16));
    s_batches = static_cast<Batch *>(heap::TryAlloc(sizeof(Batch) * kMaxDraws, TAG_RENDER, 16));
    if (s_vertices == nullptr || s_indexes == nullptr || s_batches == nullptr || !render::InitConsoleImages())
    {
        FreeGeometry();
        return false;
    }
    s_geometryStats = {};
    tr.registered = true;
    tr.currentGLState = 0;
    tr.currentColorNativeBytesOrder = 0xFFFFFFFFU;
    s_initialized = true;
    Log(LogLevel::Info, "[D3BFG] UI adapters ready virtual=640x480 physical=%dx%d\n", gs::Width(), gs::Height());
    return true;
}
PS2_COLD_FUNC void Shutdown()
{
    if (!s_initialized) { return; }
    if (rs::FrameStarted()) { FatalError("native UI shutdown with an open frame"); }
    if (s_batchCount != 0 || s_flushing || s_swfDraw) { FatalError("native UI shutdown with pending geometry"); }
    FreeGeometry();
    render::ShutdownConsoleImages();
    tr.whiteMaterial = tr.charSetMaterial = nullptr;
    tr.registered = false;
    tr.currentGLState = 0;
    tr.currentColorNativeBytesOrder = 0xFFFFFFFFU;
    s_initialized = false;
}
GeometryStats GetGeometryStats() { return s_geometryStats; }
void BeginSwfDraw()
{
    Require(&tr, "native SWF draw scope", true);
    if (s_swfDraw) { FatalError("nested native SWF drawing"); }
    Flush();
    s_savedSwfState = tr.currentGLState;
    s_savedSwfColor = tr.currentColorNativeBytesOrder;
    s_swfDraw = true;
}
void EndSwfDraw()
{
    if (!s_swfDraw) { FatalError("native SWF draw scope not active"); }
    Flush();
    s_swfDraw = false;
    tr.currentGLState = s_savedSwfState;
    tr.currentColorNativeBytesOrder = s_savedSwfColor;
}
void BeginGeometryFrame()
{
    if (!s_initialized) { return; }
    if (s_batchCount != 0 || s_flushing || s_swfDraw) { FatalError("native UI geometry crossed a frame boundary"); }
}
idDrawVert * AllocTrisForImage(int vertices, const u16 * indexes, int indexCount, const idImage * image, gs::BlendMode blend)
{
    return Allocate(vertices, indexes, indexCount, {image, blend, {255, 255, 255, 255}, true, true});
}
void DrawIndexed(const idDrawVert * vertices, int vertexCount, const u16 * indexes, int indexCount,
                 const idImage * image, gs::BlendMode blend)
{
    if (vertices == nullptr) { FatalError("native UI indexed vertices are null"); }
    idDrawVert * const output = AllocTrisForImage(vertexCount, indexes, indexCount, image, blend);
    memmove(static_cast<void *>(output), vertices, static_cast<size_t>(vertexCount) * sizeof(*vertices));
}
void Flush()
{
    if (!s_initialized || s_flushing || s_batchCount == 0) { return; }
    Require(&tr, "native UI geometry flush", true);
    PS2_Assert(s_vertexCount > 0 && s_indexCount > 0);
    FixedTriangle output[kMaxClippedTriangles];
    const auto Map = [&output](const Batch & batch, int first, int & count) {
        Vertex input[3];
        for (int corner = 0; corner < 3; ++corner)
        {
            const int index = s_indexes[batch.firstIndex + first + corner];
            input[corner] = DecodeVertex(s_vertices[batch.firstVertex + index], batch.profile);
        }
        return MapTriangle(input, {gs::Width(), gs::Height()}, batch.profile.image->GetUploadWidth(),
                           batch.profile.image->GetUploadHeight(), output, count);
    };
    // Validate the complete queued collection before binding textures or emitting any GS packet.
    for (int batchIndex = 0; batchIndex < s_batchCount; ++batchIndex)
    {
        const Batch & batch = s_batches[batchIndex];
        (void)render::ConsoleImageTexture(batch.profile.image);
        for (int index = 0; index < batch.indexCount; index += 3)
        {
            int count = 0;
            if (Map(batch, index, count) == Result::Invalid) { FatalError("invalid native UI triangle or UV"); }
        }
    }
    s_flushing = true;
    if (s_geometryStats.flushes == UINT_MAX) { FatalError("native UI geometry counter overflow"); }
    ++s_geometryStats.flushes;
    for (int batchIndex = 0; batchIndex < s_batchCount; ++batchIndex)
    {
        const Batch & batch = s_batches[batchIndex];
        if (!batch.profile.enabled) { continue; }
        const auto texture = render::ConsoleImageTexture(batch.profile.image);
        for (int index = 0; index < batch.indexCount; index += 3)
        {
            int count = 0;
            if (Map(batch, index, count) == Result::Invalid) { FatalError("native UI geometry changed during flush"); }
            if (s_geometryStats.triangles > UINT_MAX - static_cast<u32>(count)) { FatalError("native UI geometry counter overflow"); }
            s_geometryStats.triangles += static_cast<u32>(count);
            for (int triangle = 0; triangle < count; ++triangle)
            {
                if (!rs::TexturedTriangleFixed(texture, output[triangle], batch.profile.blend))
                {
                    FatalError("native UI triangle texture residency failed");
                }
            }
        }
    }
    s_vertexCount = s_indexCount = s_batchCount = 0;
    s_flushing = false;
}
} // namespace ps2::ui

int idRenderSystemLocal::GetWidth() const { Require(this, "idRenderSystemLocal::GetWidth"); return ps2::gs::Width(); }
int idRenderSystemLocal::GetHeight() const { Require(this, "idRenderSystemLocal::GetHeight"); return ps2::gs::Height(); }
float idRenderSystemLocal::GetPixelAspect() const
{
    Require(this, "idRenderSystemLocal::GetPixelAspect");
    return 4.0f * static_cast<float>(ps2::gs::Height()) / (3.0f * static_cast<float>(ps2::gs::Width()));
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
    Emit({x, y, width, height, 0.0f, 0.0f, 1.0f, 1.0f}, ps2::render::ConsoleWhiteImage(), CurrentColor(), Blend(CombinedState(kBlendAlpha)));
}
void idRenderSystemLocal::DrawStretchPic(float x, float y, float width, float height,
                                       float s0, float t0, float s1, float t1, const idMaterial * material)
{
    Require(this, "idRenderSystemLocal::DrawStretchPic", true);
    const Profile profile = MaterialProfile(material, CurrentColor());
    if (!profile.enabled) { return; }
    Emit({x, y, width, height, s0, t0, s1, t1}, profile.image, profile.tint, profile.blend);
}
void idRenderSystemLocal::DrawStretchPic(const idVec4 & tl, const idVec4 & trCorner, const idVec4 & br, const idVec4 & bl, const idMaterial * material)
{
    Require(this, "idRenderSystemLocal::DrawStretchPic", true);
    const idVec4 * const corners[] = {&tl, &trCorner, &br, &bl};
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
    static constexpr triIndex_t kQuadIndexes[] = {0, 1, 2, 0, 2, 3};
    const Profile profile = MaterialProfile(material, CurrentColor());
    for (const auto * corner : corners)
    {
        if (corner->z < 0.0f || corner->w < 0.0f ||
            corner->z > 1023.0f / static_cast<float>(profile.image->GetUploadWidth()) ||
            corner->w > 1023.0f / static_cast<float>(profile.image->GetUploadHeight()))
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
    if (stereo != STEREO_DEPTH_TYPE_NONE) { ps2::render::Unavailable("native UI stereo depth"); }
    if (material == nullptr) { return nullptr; }
    return Allocate(vertices, indexes, count, MaterialProfile(material, {255, 255, 255, 255}));
}
void idRenderSystemLocal::DrawStretchTri(const idVec2 & p1, const idVec2 & p2, const idVec2 & p3,
                                       const idVec2 & t1, const idVec2 & t2, const idVec2 & t3, const idMaterial * material)
{
    Require(this, "idRenderSystemLocal::DrawStretchTri", true);
    if (material == nullptr) { return; }
    static constexpr triIndex_t kIndexes[] = {1, 0, 2};
    const Profile profile = MaterialProfile(material, CurrentColor());
    const idVec2 positions[] = {p1, p2, p3};
    const idVec2 coords[] = {t1, t2, t3};
    ps2::ui::Vertex input[3];
    for (int corner = 0; corner < 3; ++corner)
    {
        input[corner] = {positions[corner].x, positions[corner].y, coords[corner].x, coords[corner].y, 255, 255, 255, 255};
    }
    ps2::ui::FixedTriangle output[ps2::ui::kMaxClippedTriangles];
    int count = 0;
    if (ps2::ui::MapTriangle(input, {ps2::gs::Width(), ps2::gs::Height()}, profile.image->GetUploadWidth(),
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

void idRenderSystemLocal::DrawSmallChar(int x, int y, int ch) { Require(this, "idRenderSystemLocal::DrawSmallChar", true); Character(x, y, ch, false); }
void idRenderSystemLocal::DrawBigChar(int x, int y, int ch) { Require(this, "idRenderSystemLocal::DrawBigChar", true); Character(x, y, ch, true); }
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
