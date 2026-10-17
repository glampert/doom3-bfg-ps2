// ================================================================================================
// File: retail_movie.cpp
// Brief: Measure native English font paging in production before retail SWF and Shell activation.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/retail_movie.h"
#include "ps2/renderer/image_backend.h"
#include "ps2/renderer/render_system.h"
#include "ps2/renderer/ui_draw.h"
#include "ps2/renderer/ui_page_image.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include "ps2/ui/movie.h"
#include "ps2/ui/retail_assets.h"
#include <idlib/precompiled.h>
#include <renderer/Font.h>

namespace
{
static constexpr float kScales[] = { 0.25f, 0.5f, 1.0f };
static constexpr float kBaselines[] = { 120.0f, 220.0f, 350.0f };
static constexpr char kText[] = "Campaign";
static idFont * s_fonts[ps2::ui::retail::kMaxFonts] = {};
static bool s_initialized = false;
static bool s_loaded = false;
static u32 s_fontCount = 0;
static u32 s_font = 0;
static u32 s_frames = 0;
static u32 s_cycle = 0;

ps2::ui::retail::FontRecord CurrentFont()
{
    ps2::ui::retail::FontRecord record;
    if (!ps2::ui::retail::GetCatalog().GetFont(s_font, record) || s_font >= s_fontCount || s_fonts[s_font] == nullptr)
    {
        ps2::FatalError("native retail font proof ownership changed");
    }

    return record;
}

void Status()
{
    const auto heap = ps2::heap::GetTotalStats();
    if (!s_loaded)
    {
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] RETAIL state=unloaded cycle=%u heap=%zu/%zu/%zu\n",
                 s_cycle, heap.requestedBytes, heap.backingBytes, heap.allocationCount);
        return;
    }

    const auto font = CurrentFont();
    const auto pages = ps2::render::GetUiPageStats();
    ps2::Log(ps2::LogLevel::Info,
             "[D3BFG] RETAIL state=loaded cycle=%u font=%u name=%s frames=%u heap=%zu/%zu/%zu\n",
             s_cycle, s_font, font.family, s_frames, heap.requestedBytes, heap.backingBytes, heap.allocationCount);
    ps2::Log(ps2::LogLevel::Info,
             "[D3BFG] RETAIL pages images=%u slots=%u cache=%u scratch=%u metadata=%u loads=%u hits=%u evictions=%u triangles=%u\n",
             pages.images, static_cast<u32>(pages.cacheBytes / ps2::ui::kPageBytes), pages.cacheBytes,
             pages.scratchBytes, pages.metadataBytes, pages.loads, pages.hits, pages.evictions, pages.triangles);
    for (int index = 0; index < ps2::ArrayLength(kScales); ++index)
    {
        const float scale = kScales[index];
        const float width = s_fonts[s_font]->GetMaxCharWidth(scale);
        const float height = s_fonts[s_font]->GetLineHeight(scale);
        if (width != scale * static_cast<float>(font.maxAdvance[index]) ||
            height != scale * static_cast<float>(font.maxHeight[index]))
        {
            ps2::FatalError("native retail font legacy metrics differ from the admitted source");
        }

        ps2::Log(ps2::LogLevel::Info, "[D3BFG] RETAIL metrics font=%u scale=%u width=%.2f height=%.2f\n",
                 s_font, static_cast<u32>(index), static_cast<double>(width), static_cast<double>(height));
    }
}

void OpenConsole()
{
    if (!console->Active())
    {
        const sysEvent_t open{ SE_KEY, K_GRAVE, 1, 0, nullptr, 0 };
        if (!console->ProcessEvent(&open, false))
        {
            ps2::FatalError("cannot reopen native console after the retail font proof");
        }
    }
}

void Unload(bool restoreConsole)
{
    if (ps2::rs::FrameStarted())
    {
        ps2::FatalError("cannot unload the retail font proof during drawing");
    }

    s_loaded = false;
    if (restoreConsole)
    {
        OpenConsole();
    }

    Status();
}

void Load()
{
    if (s_loaded || ps2::movie::IsLoaded() || ps2::rs::FrameStarted() || s_cycle == UINT_MAX)
    {
        ps2::FatalError("invalid native retail font proof load ownership");
    }

    if (!ps2::ui::retail::IsInitialized())
    {
        ps2::ui::retail::Init();
        ps2::ui::retail::RegisterMaterials();
        const auto & catalog = ps2::ui::retail::GetCatalog();
        s_fontCount = catalog.FontCount();
        if (s_fontCount == 0 || s_fontCount > static_cast<u32>(ps2::ArrayLength(s_fonts)))
        {
            ps2::FatalError("retail font proof requires a bounded nonempty font catalog");
        }

        for (u32 index = 0; index < s_fontCount; ++index)
        {
            ps2::ui::retail::FontRecord font;
            if (!catalog.GetFont(index, font))
            {
                ps2::FatalError("retail font proof catalog record missing");
            }

            s_fonts[index] = renderSystem->RegisterFont(font.family);
            if (s_fonts[index] == nullptr)
            {
                ps2::FatalError("retail font proof native registration failed");
            }
        }
    }

    s_loaded = true;
    s_frames = 0;
    ++s_cycle;
    console->Close();
    Status();
}

void Control(const idCmdArgs & args)
{
    if (!s_initialized || args.Argc() < 2 || args.Argc() > 3)
    {
        ps2::FatalError("ps2_uiRetail requires load, unload, reload, status or font INDEX");
    }

    const char * const action = args.Argv(1);
    if (strcmp(action, "font") == 0)
    {
        const char * const index = args.Argv(2);
        if (!s_loaded || args.Argc() != 3 || index[0] < '0' || index[0] > '7' ||
            index[1] != '\0' || static_cast<u32>(index[0] - '0') >= s_fontCount || ps2::rs::FrameStarted())
        {
            ps2::FatalError("ps2_uiRetail font requires one admitted font index while loaded");
        }

        s_font = static_cast<u32>(index[0] - '0');
        s_frames = 0;
        Status();
        return;
    }

    if (args.Argc() != 2)
    {
        ps2::FatalError("ps2_uiRetail action has unexpected arguments");
    }

    if (strcmp(action, "load") == 0)
    {
        Load();
    }
    else if (strcmp(action, "unload") == 0)
    {
        Unload(true);
    }
    else if (strcmp(action, "reload") == 0)
    {
        Unload(false);
        Load();
    }
    else if (strcmp(action, "status") == 0)
    {
        Status();
    }
    else
    {
        ps2::FatalError("unknown retail font proof action: %s", action);
    }
}

void DrawText(idFont * font, float scale, float baseline)
{
    float x = 48.0f;
    for (size_t index = 0; index + 1 < sizeof(kText); ++index)
    {
        scaledGlyphInfo_t glyph;
        font->GetScaledGlyph(scale, static_cast<u32>(kText[index]), glyph);
        if (glyph.material == nullptr)
        {
            ps2::FatalError("retail font proof glyph is unavailable");
        }

        if (glyph.width > 0.0f && glyph.height > 0.0f)
        {
            // Match native SWF text's one-unit glyph expansion for bilinear glyph sampling.
            renderSystem->DrawStretchPic(x + glyph.left, baseline - glyph.top, glyph.width + 1.0f, glyph.height + 1.0f,
                                         glyph.s1, glyph.t1, glyph.s2, glyph.t2, glyph.material);
        }

        x += glyph.xSkip;
    }
}
} // namespace

namespace ps2::retail_movie
{
void Init()
{
    if (s_initialized || !ui::IsInitialized())
    {
        FatalError("invalid native retail font proof startup");
    }

    cmdSystem->AddCommand("ps2_uiRetail", Control, CMD_FL_SYSTEM,
                          "load/unload/reload/status the bounded English font proof, or select font INDEX");
    s_initialized = true;
}

void Shutdown()
{
    if (!s_initialized)
    {
        return;
    }

    if (s_loaded)
    {
        Unload(false);
    }

    cmdSystem->RemoveCommand("ps2_uiRetail");
    memset(s_fonts, 0, sizeof(s_fonts));
    s_fontCount = 0;
    s_initialized = false;
}

bool IsLoaded()
{
    return s_loaded;
}

void Render(bool advance)
{
    if (!s_loaded)
    {
        return;
    }

    if (!ui::IsInitialized() || !rs::FrameStarted() || movie::IsLoaded())
    {
        FatalError("invalid native retail font proof presentation ownership");
    }

    if (advance)
    {
        if (s_frames == UINT_MAX)
        {
            FatalError("retail font proof frame counter overflow");
        }

        ++s_frames;
    }

    const auto font = CurrentFont();
    renderSystem->SetGLState(0);
    renderSystem->DrawSmallStringExt(48, 32, font.family, colorWhite, true);
    renderSystem->DrawSmallStringExt(48, 52, "Native English font paging; campaign and Shell remain inactive", colorWhite, true);
    renderSystem->SetColor(idVec4(240.0f / 255.0f, 248.0f / 255.0f, 1.0f, 1.0f));
    for (int index = 0; index < ArrayLength(kScales); ++index)
    {
        DrawText(s_fonts[s_font], kScales[index], kBaselines[index]);
    }

    renderSystem->SetColor(colorWhite);
}
} // namespace ps2::retail_movie
