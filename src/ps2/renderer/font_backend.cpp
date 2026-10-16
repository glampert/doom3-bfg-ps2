// ================================================================================================
// File: font_backend.cpp
// Brief: Reuse native idFont metrics with an authored ASCII atlas and bounded owned cache.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/font_backend.h"
#include "ps2/renderer/image_backend.h"
#include "ps2/renderer/render_system.h"
#include "ps2/renderer/ui_draw.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>
#include <renderer/Font.h>
#include <renderer/tr_local.h>

namespace
{
static constexpr char kFontName[] = "ps2_ascii";
static constexpr int kGlyphCount = 95;
static constexpr int kHeaderBytes = 12;
static constexpr int kGlyphBytes = 10;
static constexpr int kFileBytes = kHeaderBytes + kGlyphCount * (kGlyphBytes + 4);
static bool s_initialized = false;

bool IsFontName(const char * name)
{
    return name != nullptr && (strcmp(name, kFontName) == 0 || strcmp(name, "fonts/ps2_ascii") == 0);
}

u16 ReadLittleShort(const u8 * bytes)
{
    return static_cast<u16>(static_cast<u16>(bytes[0]) | (static_cast<u16>(bytes[1]) << 8));
}

u32 ReadLittleInt(const u8 * bytes)
{
    return static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8) |
           (static_cast<u32>(bytes[2]) << 16) | (static_cast<u32>(bytes[3]) << 24);
}
} // namespace

namespace ps2::fonts
{
bool IsInitialized()
{
    return s_initialized;
}

PS2_COLD_FUNC void Init()
{
    if (s_initialized || !ui::IsInitialized() || rs::FrameStarted() || tr.fonts.Num() != 0)
    {
        FatalError("invalid native UI font startup ownership");
    }
    s_initialized = true;
}

PS2_COLD_FUNC void Shutdown()
{
    if (!s_initialized)
    {
        return;
    }
    if (!ui::IsInitialized() || rs::FrameStarted())
    {
        FatalError("invalid native UI font shutdown ownership");
    }
    tr.fonts.DeleteContents(true);
    s_initialized = false;
}

bool PreflightAuthoredFont(idFile * file, const char * name)
{
    if (!s_initialized || file == nullptr || !IsFontName(name) || file->Length() != kFileBytes || file->Tell() != 0)
    {
        return false;
    }
    u8 bytes[kFileBytes] = {};
    if (file->Read(bytes, kFileBytes) != kFileBytes)
    {
        return false;
    }
    // Native glyphInfo_t stores a zero padding byte before the little-endian atlas coordinates.
    static constexpr u8 kHeader[kHeaderBytes] = { 'i', 'd', 'f', 42, 0, 48, 0, 14, 0, 0, 0, 95 };
    if (memcmp(bytes, kHeader, sizeof(kHeader)) != 0)
    {
        return false;
    }
    for (int i = 0; i < kGlyphCount; ++i)
    {
        const int code = i + 32;
        const auto * const glyph = bytes + kHeaderBytes + i * kGlyphBytes;
        if (glyph[0] != 15 || glyph[1] != 14 || glyph[2] != 14 || glyph[3] != 0 || glyph[4] != 16 || glyph[5] != 0 ||
            ReadLittleShort(glyph + 6) != (code & 15) * 32 + 1 || ReadLittleShort(glyph + 8) != (code >> 4) * 16 + 1 ||
            ReadLittleInt(bytes + kHeaderBytes + kGlyphCount * kGlyphBytes + i * 4) != static_cast<u32>(code))
        {
            return false;
        }
    }
    return file->Seek(0, FS_SEEK_SET) == 0;
}

void ValidateAuthoredFontMaterial(const idMaterial * material)
{
    if (!s_initialized || material == nullptr || material->GetNumStages() != 1 ||
        material->ConstantRegisters() == nullptr || material->TestMaterialFlag(MF_DEFAULTED | MF_POLYGONOFFSET))
    {
        FatalError("native UI font requires one constant authored atlas material");
    }
    const shaderStage_t & stage = *material->GetStage(0);
    if (stage.texture.image != render::AuthoredFontImage() || stage.lighting != SL_AMBIENT ||
        stage.texture.image == nullptr || !stage.texture.image->IsLoaded() ||
        stage.texture.image->GetUploadWidth() != 512 || stage.texture.image->GetUploadHeight() != 128)
    {
        FatalError("native UI font material does not reference the authored 512x128 atlas");
    }
}
} // namespace ps2::fonts

idFont * idRenderSystemLocal::RegisterFont(const char * fontName)
{
    if (this != &tr || !ps2::fonts::IsInitialized() || !ps2::ui::IsInitialized() || !IsFontName(fontName) || fonts.Num() > 1)
    {
        ps2::FatalError("native UI font is outside the authored ASCII profile");
    }
    if (fonts.Num() == 1)
    {
        if (strcmp(fonts[0]->GetName(), kFontName) != 0)
        {
            ps2::FatalError("native UI font cache ownership changed");
        }
        fonts[0]->Touch();
        return fonts[0];
    }
    auto * const font = new (TAG_FONT) idFont(kFontName);
    fonts.Append(font);
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] UI font ready name=%s glyphs=%d atlas=512x128\n", kFontName, kGlyphCount);
    return font;
}

void idRenderSystemLocal::ResetFonts()
{
    if (this != &tr || !ps2::fonts::IsInitialized() || ps2::rs::FrameStarted())
    {
        ps2::FatalError("invalid native UI font reset ownership");
    }
    fonts.DeleteContents(true);
}
