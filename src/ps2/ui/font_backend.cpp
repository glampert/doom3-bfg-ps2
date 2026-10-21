// ================================================================================================
// File: font_backend.cpp
// Brief: Admit checked English font records and preserve native modern glyphs and legacy layout.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/font_backend.h"
#include "ps2/renderer/image_backend.h"
#include "ps2/renderer/render_system.h"
#include "ps2/system/log.h"
#include "ps2/type_query.h"
#include "ps2/ui/authored_assets.h"
#include "ps2/ui/draw.h"
#include "ps2/ui/font_format.h"
#include "ps2/ui/retail_assets.h"
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
static constexpr int kMaxCachedFonts = 9;
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

namespace ps2::ui::fonts
{
bool IsInitialized()
{
    return s_initialized;
}

PS2_COLD_FUNC void Init()
{
    if (s_initialized || !ui::IsInitialized() || renderer::FrameStarted() || tr.fonts.Num() != 0)
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
    if (!ui::IsInitialized() || renderer::FrameStarted())
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

idFile * OpenFontAsset(const char * name)
{
    if (!s_initialized)
    {
        FatalError("native UI font opened before font startup");
    }

    if (IsFontName(name))
    {
        return ui::authored::OpenAsset("newfonts/ps2_ascii/48.dat");
    }

    ui::retail::FontRecord record{};
    if (!ui::retail::FindFont(name, record))
    {
        FatalError("native UI font outside the admitted English profile: %s", name != nullptr ? name : "<null>");
    }

    return ui::retail::OpenFont(record);
}

bool PreflightFont(idFile * file, const char * name)
{
    if (IsFontName(name))
    {
        return PreflightAuthoredFont(file, name);
    }

    ui::retail::FontRecord record{};
    ui::retail::ImageRecord image{};
    const auto * const memory = CheckedCast<const idFile_Memory *>(file);
    if (!s_initialized || memory == nullptr || memory->Tell() != 0 || !ui::retail::FindFont(name, record) ||
        memory->Length() != static_cast<int>(record.bytes) ||
        !ui::retail::GetCatalog().GetImage(record.imageIndex, image))
    {
        return false;
    }

    FontFormatSummary summary{};
    return ValidateFontBytes(memory->GetDataPtr(), record.bytes, image.width, image.height, summary);
}

void ValidateFontMaterial(const idMaterial * material, const char * name)
{
    if (IsFontName(name))
    {
        ValidateAuthoredFontMaterial(material);
        return;
    }

    ui::retail::FontRecord record{};
    ui::retail::ImageRecord image{};
    if (!s_initialized || material == nullptr || material->GetNumStages() != 1 ||
        material->ConstantRegisters() == nullptr || material->TestMaterialFlag(MF_DEFAULTED | MF_POLYGONOFFSET) ||
        !ui::retail::FindFont(name, record) || !ui::retail::GetCatalog().GetImage(record.imageIndex, image))
    {
        FatalError("native English font requires one admitted constant atlas material");
    }

    const shaderStage_t & stage = *material->GetStage(0);
    if (stage.texture.image == nullptr || stage.texture.image != globalImages->GetImage(image.name) ||
        stage.lighting != SL_AMBIENT || !stage.texture.image->IsLoaded() ||
        stage.texture.image->GetUploadWidth() != static_cast<int>(image.width) ||
        stage.texture.image->GetUploadHeight() != static_cast<int>(image.height))
    {
        FatalError("native English font material does not reference its admitted atlas: %s", record.family);
    }
}

void GetFontLayout(const char * name, int index, int & width, int & height)
{
    if (!s_initialized || index < 0 || index >= 3)
    {
        FatalError("invalid native UI font layout query");
    }

    if (IsFontName(name))
    {
        width = 16;
        height = 14;
        return;
    }

    ui::retail::FontRecord record{};
    if (!ui::retail::FindFont(name, record))
    {
        FatalError("native UI font layout outside the admitted English profile");
    }

    width = static_cast<int>(record.maxAdvance[static_cast<u32>(index)]);
    height = static_cast<int>(record.maxHeight[static_cast<u32>(index)]);
}

void ValidateAuthoredFontMaterial(const idMaterial * material)
{
    if (!s_initialized || material == nullptr || material->GetNumStages() != 1 ||
        material->ConstantRegisters() == nullptr || material->TestMaterialFlag(MF_DEFAULTED | MF_POLYGONOFFSET))
    {
        FatalError("native UI font requires one constant authored atlas material");
    }
    const shaderStage_t & stage = *material->GetStage(0);
    if (stage.texture.image != renderer::AuthoredFontImage() || stage.lighting != SL_AMBIENT ||
        stage.texture.image == nullptr || !stage.texture.image->IsLoaded() ||
        stage.texture.image->GetUploadWidth() != 512 || stage.texture.image->GetUploadHeight() != 128)
    {
        FatalError("native UI font material does not reference the authored 512x128 atlas");
    }
}
} // namespace ps2::ui::fonts

idFont * idRenderSystemLocal::RegisterFont(const char * fontName)
{
    if (this != &tr || !ps2::ui::fonts::IsInitialized() || !ps2::ui::IsInitialized() || fontName == nullptr)
    {
        ps2::FatalError("native UI font registration outside initialized ownership");
    }

    ps2::ui::retail::FontRecord record{};
    idStr request(fontName);
    request.Replace("fonts/", "");
    const char * canonical = kFontName;
    if (!IsFontName(request))
    {
        if (!ps2::ui::retail::FindFont(request, record))
        {
            ps2::FatalError("native UI font is outside the admitted English profile: %s", fontName);
        }

        canonical = record.family;
    }

    for (int index = 0; index < fonts.Num(); ++index)
    {
        if (strcmp(fonts[index]->GetName(), canonical) == 0)
        {
            fonts[index]->Touch();
            return fonts[index];
        }
    }

    if (fonts.Num() >= kMaxCachedFonts)
    {
        ps2::FatalError("native UI font cache exceeded the bounded English profile");
    }

    auto * const font = new (TAG_FONT) idFont(canonical);
    fonts.Append(font);
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] UI font ready name=%s cached=%d\n", canonical, fonts.Num());
    return font;
}

void idRenderSystemLocal::ResetFonts()
{
    if (this != &tr || !ps2::ui::fonts::IsInitialized() || ps2::renderer::FrameStarted())
    {
        ps2::FatalError("invalid native UI font reset ownership");
    }
    fonts.DeleteContents(true);
}
