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
#include <idlib/precompiled.h>
#include <renderer/tr_local.h>
#include <bit>
#include <cstdint>

namespace
{
static bool s_initialized = false;
// The EE's native RGBA byte order is little endian; no idlib swap initialization is required.
static_assert(std::endian::native == std::endian::little);
static constexpr uint64 kBlendAlpha = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
static constexpr uint64 kAllowedState = GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS | GLS_DEPTHMASK | GLS_DEPTHFUNC_BITS | GLS_OVERRIDE;

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
        (equation != GLS_DEFAULT && equation != kBlendAlpha))
    {
        ps2::render::Unavailable("native UI blend/state profile");
    }
    return equation == GLS_DEFAULT ? ps2::gs::BlendMode::Replace : ps2::gs::BlendMode::StraightAlpha;
}
uint64 CombinedState(uint64 stageState)
{
    return (tr.currentGLState & GLS_OVERRIDE) != 0 ? tr.currentGLState & ~GLS_OVERRIDE : tr.currentGLState | stageState;
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
    if (!rs::IsInitialized() || rs::FrameStarted() || !render::InitConsoleImages()) { return false; }
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
    render::ShutdownConsoleImages();
    tr.whiteMaterial = tr.charSetMaterial = nullptr;
    tr.registered = false;
    tr.currentGLState = 0;
    tr.currentColorNativeBytesOrder = 0xFFFFFFFFU;
    s_initialized = false;
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
    auto color = CurrentColor();
    const u8 channels[] = {color.r, color.g, color.b, color.a};
    u8 modulated[4] = {};
    for (int i = 0; i < 4; ++i) { modulated[i] = static_cast<u8>(Register(stage.color.registers[i]) * static_cast<float>(channels[i])); }
    const auto blend = Blend(CombinedState(stage.drawStateBits));
    // Validate the image/profile even for a disabled condition; no half-accepted stage.
    (void)ps2::render::ConsoleImageTexture(stage.texture.image);
    if (condition == 0.0f) { return; }
    Emit({x, y, width, height, s0, t0, s1, t1}, stage.texture.image,
         {modulated[0], modulated[1], modulated[2], modulated[3]}, blend);
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
    if (tl.y != trCorner.y || bl.y != br.y || tl.x != bl.x || trCorner.x != br.x ||
        tl.z != bl.z || trCorner.z != br.z || tl.w != trCorner.w || bl.w != br.w)
    {
        ps2::render::Unavailable("native UI quad requires axis-aligned positions and UVs");
    }
    DrawStretchPic(tl.x, tl.y, trCorner.x - tl.x, bl.y - tl.y, tl.z, tl.w, br.z, br.w, material);
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
