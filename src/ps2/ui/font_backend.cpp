// ================================================================================================
// File: font_backend.cpp
// Brief: Admit checked English font records and preserve native modern glyphs and legacy layout.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/font_backend.h"
#include "ps2/renderer/gs.h"
#include "ps2/renderer/image_backend.h"
#include "ps2/renderer/render_system.h"
#include "ps2/system/log.h"
#include "ps2/type_query.h"
#include "ps2/ui/authored_assets.h"
#include "ps2/ui/draw.h"
#include "ps2/ui/font_format.h"
#include "ps2/ui/gstx_format.h"
#include "ps2/ui/retail_assets.h"
#include <cmath>
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
static constexpr float kVirtualWidth = 640.0f;
static constexpr float kVirtualHeight = 480.0f;
// A size matches a request within 1% on each axis; layout keeps native positions, so a glyph off
// by that much is still placed exactly.
static constexpr float kGlyphScaleTolerance = 0.01f;
static constexpr float kStrokeWeightTolerance = 0.01f;
static constexpr u32 kMaxLoggedGlyphSizes = 32;
static bool s_initialized = false;

// A registered retail font and, when the catalog has one, its glyph set image and tables.
struct GlyphSetState
{
    const idFont * font = nullptr;
    const idImage * image = nullptr;
    float inverseWidth = 0.0f;
    float inverseHeight = 0.0f;
};

struct LoggedGlyphSize
{
    const idFont * font = nullptr;
    s32 scaleX = 0;
    s32 scaleY = 0;
    s32 strokeWeight = 0;
};

static GlyphSetState s_glyphSets[ps2::ui::retail::kMaxFonts] = {};
static LoggedGlyphSize s_loggedSizes[kMaxLoggedGlyphSizes] = {};
static u32 s_loggedSizeCount = 0;

s32 Fixed(float value)
{
    return static_cast<s32>(std::floor(value * 65536.0f + 0.5f));
}

void ClearGlyphSets()
{
    for (auto & state : s_glyphSets)
    {
        state = {};
    }

    s_loggedSizeCount = 0;
}

// Loads the glyph set image when a retail font registers, outside any frame like its atlas.
void RegisterGlyphSet(const idFont * font, const ps2::ui::retail::FontRecord & record)
{
    const auto & catalog = ps2::ui::retail::GetCatalog();
    GlyphSetState & state = s_glyphSets[record.index];
    ps2::ui::retail::GlyphSetRecord set;
    ps2::ui::retail::ImageRecord image;
    ps2::ui::gstx::Header header;
    state = {};
    state.font = font;

    if (!catalog.GetGlyphSet(record, set))
    {
        return;
    }

    if (!catalog.GetImage(set.imageIndex, image) || !ps2::ui::gstx::DecodeTables(image.descriptor, image.descriptorBytes, header))
    {
        ps2::FatalError("retail glyph set image outside the catalog: %s", record.family);
    }

    state.image = globalImages->ImageFromFile(image.name, TF_LINEAR, TR_CLAMP, TD_FONT);
    state.inverseWidth = 1.0f / static_cast<float>(image.width);
    state.inverseHeight = 1.0f / static_cast<float>(image.height);
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] UI glyph set ready font=%s sizes=%lu glyphs=%lu\n", record.family,
             static_cast<unsigned long>(set.sizeCount), static_cast<unsigned long>(set.entryCount));
}

// Records a size the font has no bitmaps for, once, so the screens drawing it can have it cooked.
void LogGlyphSize(const idFont * font, float scaleX, float scaleY, float strokeWeight)
{
    const LoggedGlyphSize size = { font, Fixed(scaleX), Fixed(scaleY), Fixed(strokeWeight) };

    for (u32 index = 0; index < s_loggedSizeCount; ++index)
    {
        const LoggedGlyphSize & logged = s_loggedSizes[index];

        if (logged.font == size.font && logged.scaleX == size.scaleX && logged.scaleY == size.scaleY &&
            logged.strokeWeight == size.strokeWeight)
        {
            return;
        }
    }

    if (s_loggedSizeCount == kMaxLoggedGlyphSizes)
    {
        return;
    }

    s_loggedSizes[s_loggedSizeCount++] = size;
    ps2::Log(ps2::LogLevel::Info, "[D3BFG] UI glyph size font=%s scale=%.5f,%.5f stroke=%.3f prerendered=no\n", font->GetName(),
             static_cast<double>(scaleX), static_cast<double>(scaleY), static_cast<double>(strokeWeight));
}

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
    ClearGlyphSets();
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
    const auto * const memory = DynCast<const idFile_Memory *>(file);
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

bool FindPrerenderedGlyph(const idFont * font, float scaleX, float scaleY, float strokeWeight, std::uint32_t codepoint,
                          PrerenderedGlyph & output)
{
    if (!s_initialized || font == nullptr || !retail::IsInitialized())
    {
        return false;
    }

    std::uint32_t index = 0;

    while (index < retail::kMaxFonts && s_glyphSets[index].font != font)
    {
        ++index;
    }

    // Authored fonts have no catalog record.
    if (index == retail::kMaxFonts)
    {
        return false;
    }

    // Virtual-screen units to GS pixels, as every UI quad maps them.
    const float pixelX = scaleX * static_cast<float>(renderer::gs::Width()) / kVirtualWidth;
    const float pixelY = scaleY * static_cast<float>(renderer::gs::Height()) / kVirtualHeight;
    const auto & catalog = retail::GetCatalog();
    retail::FontRecord record;
    retail::GlyphSetRecord set;

    if (s_glyphSets[index].image != nullptr && catalog.GetFont(index, record) && catalog.GetGlyphSet(record, set))
    {
        for (std::uint32_t sizeIndex = 0; sizeIndex < set.sizeCount; ++sizeIndex)
        {
            retail::GlyphSize size;
            catalog.GetGlyphSize(record, sizeIndex, size);
            const float sizeX = static_cast<float>(size.scaleX) / 65536.0f;
            const float sizeY = static_cast<float>(size.scaleY) / 65536.0f;
            const float stroke = static_cast<float>(size.strokeWeight) / 65536.0f;

            if (std::fabs(pixelX - sizeX) > kGlyphScaleTolerance * sizeX || std::fabs(pixelY - sizeY) > kGlyphScaleTolerance * sizeY ||
                std::fabs(strokeWeight - stroke) > kStrokeWeightTolerance)
            {
                continue;
            }

            // Entries of one size are sorted by codepoint. A glyph the set lacks keeps the native quad.
            std::uint32_t low = size.firstEntry;
            std::uint32_t high = size.firstEntry + size.entryCount;

            while (low < high)
            {
                const std::uint32_t middle = low + (high - low) / 2;
                retail::GlyphEntry entry;
                catalog.GetGlyphEntry(record, middle, entry);

                if (entry.codepoint == codepoint)
                {
                    output = { index, entry.x, entry.y, entry.width, entry.height, entry.outline, entry.originX, entry.originY };
                    return true;
                }

                if (entry.codepoint < codepoint)
                {
                    low = middle + 1;
                }
                else
                {
                    high = middle;
                }
            }

            return false;
        }
    }

    LogGlyphSize(font, pixelX, pixelY, strokeWeight);
    return false;
}

void DrawPrerenderedGlyph(const PrerenderedGlyph & glyph, const idMaterial * fontMaterial, float originX, float originY, bool outline)
{
    if (glyph.width == 0 || (outline && !glyph.outline))
    {
        return;
    }

    if (glyph.font >= retail::kMaxFonts || s_glyphSets[glyph.font].image == nullptr)
    {
        FatalError("invalid native UI pre-rendered glyph");
    }

    const GlyphSetState & state = s_glyphSets[glyph.font];
    // With an outline the rectangle holds the glyph on its left half and the outline on its right.
    const std::uint32_t bitmapWidth = glyph.outline ? glyph.width / 2U : glyph.width;
    const std::uint32_t bitmapX = glyph.x + (outline ? bitmapWidth : 0U);
    const float width = static_cast<float>(renderer::gs::Width());
    const float height = static_cast<float>(renderer::gs::Height());
    // The bitmap's corner lands on a whole pixel, then moves half a pixel up and left: every pixel
    // samples a texel center, so bilinear filtering returns the pre-rendered texels exactly.
    const float x = originX * width / kVirtualWidth - static_cast<float>(glyph.originX) / 16.0f;
    const float y = originY * height / kVirtualHeight - static_cast<float>(glyph.originY) / 16.0f;
    const float left = std::floor(x + 0.5f) - 0.5f;
    const float top = std::floor(y + 0.5f) - 0.5f;
    ui::DrawImageRect(left * kVirtualWidth / width, top * kVirtualHeight / height, static_cast<float>(bitmapWidth) * kVirtualWidth / width,
                      static_cast<float>(glyph.height) * kVirtualHeight / height, static_cast<float>(bitmapX) * state.inverseWidth,
                      static_cast<float>(glyph.y) * state.inverseHeight, static_cast<float>(bitmapX + bitmapWidth) * state.inverseWidth,
                      static_cast<float>(glyph.y + glyph.height) * state.inverseHeight, state.image, fontMaterial);
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

    if (!IsFontName(canonical))
    {
        RegisterGlyphSet(font, record);
    }

    return font;
}

void idRenderSystemLocal::ResetFonts()
{
    if (this != &tr || !ps2::ui::fonts::IsInitialized() || ps2::renderer::FrameStarted())
    {
        ps2::FatalError("invalid native UI font reset ownership");
    }
    fonts.DeleteContents(true);
    ClearGlyphSets();
}
