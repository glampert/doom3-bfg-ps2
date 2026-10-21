// ================================================================================================
// File: retail_movie.cpp
// Brief: Own English resource screens, bounded retail timelines and the fully drawn native dialog.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/retail_movie.h"
#include "ps2/renderer/render_system.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include "ps2/ui/draw.h"
#include "ps2/ui/movie.h"
#include "ps2/ui/paletted_image.h"
#include "ps2/ui/retail_assets.h"
#include "ps2/ui/retail_owner.h"
#include "ps2/ui/swf_runtime.h"
#include <idlib/precompiled.h>
#include <renderer/Font.h>
#include <swf/SWF.h>

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
static idSWF * s_resourceMovie = nullptr;
static idSWFTextInstance * s_texts[3] = {};
static int s_movieIndex = -1;
static u32 s_executionTicks = 0;
static u32 s_nativeSteps = 0;
// The last timeline clock; full drawing reuses it so synchronous redraws present the same movie state.
static int s_movieTime = 0;
static constexpr const char * kLabels[] = { "#str_swf_campaign", "#str_swf_settings", "#str_swf_quit" };

void CloseResourceMovie()
{
    const int releasedMovie = s_movieIndex;

    for (auto *& text : s_texts)
    {
        delete text;
        text = nullptr;
    }

    if (s_resourceMovie != nullptr && s_resourceMovie->IsResourceExecution())
    {
        ps2::ui::retail::DetachOwner(*s_resourceMovie);
    }

    delete s_resourceMovie;
    s_resourceMovie = nullptr;
    s_movieIndex = -1;
    s_executionTicks = 0;
    s_nativeSteps = 0;
    s_movieTime = 0;

    if (releasedMovie >= 0)
    {
        const auto heap = ps2::system::heap::GetTotalStats();
        const auto textures = ps2::ui::GetPaletteStats();
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] RETAIL released movie=%d images=%u heap=%zu/%zu/%zu\n",
                 releasedMovie, textures.images, heap.requestedBytes, heap.backingBytes, heap.allocationCount);
    }
}

ps2::ui::retail::FontRecord CurrentFont()
{
    ps2::ui::retail::FontRecord record;
    if (!ps2::ui::retail::GetCatalog().GetFont(s_font, record) || s_font >= s_fontCount || s_fonts[s_font] == nullptr)
    {
        ps2::FatalError("native retail font proof ownership changed");
    }

    return record;
}

void CountHierarchy(const idSWFSpriteInstance * instance, unsigned level, unsigned & sprites, unsigned & texts, unsigned & depth)
{
    if (instance == nullptr || level > ps2::ui::swf::kMaxHierarchyDepth || ++sprites > ps2::ui::swf::kMaxSprites)
    {
        ps2::FatalError("retail execution hierarchy exceeds admitted limits");
    }

    depth = Max(depth, level);
    for (int index = 0; index < instance->displayList.Num(); ++index)
    {
        const auto & entry = instance->displayList[index];
        if (entry.spriteInstance != nullptr)
        {
            CountHierarchy(entry.spriteInstance, level + 1, sprites, texts, depth);
        }

        if (entry.textInstance != nullptr && ++texts > ps2::ui::swf::kMaxTexts)
        {
            ps2::FatalError("retail execution text instances exceed admitted limits");
        }
    }
}

void ExecutionStatus()
{
    if (!s_resourceMovie->IsResourceExecution())
    {
        return;
    }

    auto * const root = s_resourceMovie->GetRootObject().GetSprite();
    if (root == nullptr)
    {
        ps2::FatalError("retail execution root instance missing");
    }

    unsigned sprites = 0;
    unsigned texts = 0;
    unsigned depth = 0;
    CountHierarchy(root, 1, sprites, texts, depth);
    ps2::ui::retail::OwnerStatus();
    const auto stats = ps2::ui::swf::GetStats();
    ps2::Log(ps2::LogLevel::Info,
             "[D3BFG] RETAIL execution movie=%d ticks=%u root=%u sprites=%u texts=%u instructions=%lu calls=%lu commands=%lu depth=%u native_steps=%u\n",
             s_movieIndex, s_executionTicks, static_cast<unsigned>(root->GetCurrentFrame()), sprites, texts,
             static_cast<unsigned long>(stats.instructions), static_cast<unsigned long>(stats.calls),
             static_cast<unsigned long>(stats.commands), depth, s_nativeSteps);
    ps2::Log(ps2::LogLevel::Info,
             "[D3BFG] RETAIL execution peaks instructions=%lu calls=%lu commands=%lu call_depth=%lu visit_depth=%lu stack=%lu objects=%lu functions=%lu strings=%lu string_bytes=%lu traces=%lu\n",
             static_cast<unsigned long>(stats.peakInstructions), static_cast<unsigned long>(stats.peakCalls),
             static_cast<unsigned long>(stats.peakCommands), static_cast<unsigned long>(stats.peakDepth),
             static_cast<unsigned long>(stats.peakVisitDepth), static_cast<unsigned long>(stats.peakStack),
             static_cast<unsigned long>(stats.peakObjects), static_cast<unsigned long>(stats.peakFunctions),
             static_cast<unsigned long>(stats.peakStrings), static_cast<unsigned long>(stats.peakStringBytes),
             static_cast<unsigned long>(stats.traces));
}

void Status()
{
    const auto heap = ps2::system::heap::GetTotalStats();
    if (!s_loaded)
    {
        ps2::Log(ps2::LogLevel::Info, "[D3BFG] RETAIL state=unloaded cycle=%u heap=%zu/%zu/%zu\n",
                 s_cycle, heap.requestedBytes, heap.backingBytes, heap.allocationCount);
        return;
    }

    if (s_resourceMovie != nullptr)
    {
        const auto summary = ps2::ui::retail::GetMovieSummary();
        ps2::Log(ps2::LogLevel::Info,
                 "[D3BFG] RETAIL movie=%d state=loaded frames=%u dictionary=%lu texts=%lu preview=%lu english=%d scripts=%s heap=%zu/%zu/%zu\n",
                 s_movieIndex, s_frames, static_cast<unsigned long>(summary.dictionary),
                 static_cast<unsigned long>(summary.editTexts), static_cast<unsigned long>(summary.previewText),
                 idLocalization::GetDictionary().GetNumKeyVals(), s_resourceMovie->IsResourceExecution() ? "active" : "inactive", heap.requestedBytes, heap.backingBytes, heap.allocationCount);

        ExecutionStatus();

        if (s_resourceMovie->IsResourceRender())
        {
            const auto geometry = ps2::ui::GetGeometryStats();
            ps2::Log(ps2::LogLevel::Info,
                     "[D3BFG] RETAIL draw movie=%d full=1 triangles=%u flushes=%u masks=%u peak_masks=%u peak_vertices=%u peak_indexes=%u peak_draws=%u\n",
                     s_movieIndex, geometry.triangles, geometry.flushes, geometry.masks, geometry.peakMasks,
                     geometry.peakVertices, geometry.peakIndexes, geometry.peakDraws);
        }

        for (int index = 0; index < ps2::ArrayLength(s_texts) && s_texts[index] != nullptr; ++index)
        {
            ps2::Log(ps2::LogLevel::Info, "[D3BFG] RETAIL label=%d id=%s width=%.2f font=%u height=%u\n",
                     index, kLabels[index], static_cast<double>(s_texts[index]->GetTextLength()),
                     static_cast<unsigned>(s_texts[index]->editText->fontID),
                     static_cast<unsigned>(s_texts[index]->editText->fontHeight));
        }
    }

    const auto font = CurrentFont();
    const auto textures = ps2::ui::GetPaletteStats();
    const auto residency = ps2::renderer::tex::GetStats();
    ps2::Log(ps2::LogLevel::Info,
             "[D3BFG] RETAIL state=loaded cycle=%u font=%u name=%s frames=%u heap=%zu/%zu/%zu\n",
             s_cycle, s_font, font.family, s_frames, heap.requestedBytes, heap.backingBytes, heap.allocationCount);
    ps2::Log(ps2::LogLevel::Info,
             "[D3BFG] RETAIL textures images=%u pages=%u cluts=%u pixels=%u metadata=%u triangles=%u uploads=%u evictions=%u\n",
             textures.images, textures.pages, textures.cluts, textures.pixelBytes, textures.metadataBytes, textures.triangles,
             residency.uploads, residency.evictions);
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
    if (ps2::renderer::FrameStarted())
    {
        ps2::FatalError("cannot unload the retail font proof during drawing");
    }

    CloseResourceMovie();
    s_loaded = false;
    if (restoreConsole)
    {
        OpenConsole();
    }

    Status();
}

void Load()
{
    if (s_loaded || ps2::ui::authored::IsLoaded() || ps2::renderer::FrameStarted() || s_cycle == UINT_MAX)
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

// Present draws the complete dialog movie with the native quit confirmation instead of isolated labels.
void LoadResourceMovie(unsigned index, bool execute = false, bool present = false)
{
    if (!s_loaded || ps2::renderer::FrameStarted() || index >= 2 || (present && (!execute || index != 1)))
    {
        ps2::FatalError("retail movie requires an idle loaded resource screen");
    }

    CloseResourceMovie();
    ps2::ui::retail::PrepareMovie(index, execute, present);
    s_resourceMovie = new (TAG_SWF) idSWF(index == 0 ? "shell" : "dialog", nullptr, true);
    const auto summary = ps2::ui::retail::GetMovieSummary();
    auto * const entry = s_resourceMovie->GetDictionaryEntry(static_cast<int>(summary.previewText));

    if (!s_resourceMovie->IsLoaded() || entry->type != SWF_DICT_EDITTEXT || entry->edittext == nullptr)
    {
        ps2::FatalError("retail movie text prototype missing after native loading");
    }

    auto * const font = s_resourceMovie->FindDictionaryEntry(entry->edittext->fontID, SWF_DICT_FONT);

    if (font == nullptr || font->font == nullptr || font->font->fontID == nullptr ||
        font->font->fontID->GetAscender(1.0f) <= 0.0f)
    {
        ps2::FatalError("retail movie text requires a positive native ascender");
    }

    for (int label = 0; label < ps2::ArrayLength(s_texts) && !present; ++label)
    {
        if (idLocalization::FindString(kLabels[label]) == nullptr)
        {
            ps2::FatalError("required retail English label missing: %s", kLabels[label]);
        }

        s_texts[label] = new (TAG_SWF) idSWFTextInstance;
        s_texts[label]->Init(entry->edittext, s_resourceMovie);
        s_texts[label]->variable.Clear();
        s_texts[label]->SetText(kLabels[label]);
    }

    if (execute)
    {
        if (present)
        {
            ps2::ui::retail::ShowQuitDialog(*s_resourceMovie);
        }
        else
        {
            ps2::ui::retail::AttachOwner(*s_resourceMovie, index);
        }

        ps2::ui::swf::ResetStats();
        const ps2::ui::swf::ExecutionScope scope(s_resourceMovie->GetName());
        s_resourceMovie->EnableResourceExecution();
        s_nativeSteps = 1;
    }

    if (present)
    {
        s_resourceMovie->EnableResourceRender();
    }

    s_movieIndex = static_cast<int>(index);
    s_movieTime = Sys_Milliseconds();
    s_frames = 0;
    Status();
}

void Control(const idCmdArgs & args)
{
    if (!s_initialized || args.Argc() < 2 || args.Argc() > 3)
    {
        ps2::FatalError("ps2_uiRetail requires load, unload, reload, status, font INDEX, movie NAME, execute NAME or dialog quit");
    }

    const char * const action = args.Argv(1);
    if (strcmp(action, "dialog") == 0)
    {
        if (args.Argc() != 3 || strcmp(args.Argv(2), "quit") != 0)
        {
            ps2::FatalError("ps2_uiRetail dialog requires quit");
        }

        LoadResourceMovie(1, true, true);
        return;
    }

    if (strcmp(action, "movie") == 0 || strcmp(action, "execute") == 0)
    {
        if (args.Argc() != 3)
        {
            ps2::FatalError("ps2_uiRetail movie requires shell, dialog or none");
        }

        if (strcmp(args.Argv(2), "shell") == 0)
        {
            LoadResourceMovie(0, strcmp(action, "execute") == 0);
        }
        else if (strcmp(args.Argv(2), "dialog") == 0)
        {
            LoadResourceMovie(1, strcmp(action, "execute") == 0);
        }
        else if (strcmp(args.Argv(2), "none") == 0)
        {
            if (strcmp(action, "execute") == 0)
            {
                ps2::FatalError("ps2_uiRetail execute requires shell or dialog");
            }

            CloseResourceMovie();
            s_frames = 0;
            Status();
        }
        else
        {
            ps2::FatalError("unknown retail resource movie");
        }

        return;
    }

    if (strcmp(action, "font") == 0)
    {
        const char * const index = args.Argv(2);
        if (!s_loaded || args.Argc() != 3 || index[0] < '0' || index[0] > '7' ||
            index[1] != '\0' || static_cast<u32>(index[0] - '0') >= s_fontCount || ps2::renderer::FrameStarted())
        {
            ps2::FatalError("ps2_uiRetail font requires one admitted font index while loaded");
        }

        CloseResourceMovie();
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

namespace ps2::ui::retail
{
void InitScreen()
{
    if (s_initialized || !ui::IsInitialized())
    {
        FatalError("invalid native retail font proof startup");
    }

    cmdSystem->AddCommand("ps2_uiRetail", Control, CMD_FL_SYSTEM,
                          "control English resources with load/unload/reload/status, font INDEX, movie NAME, execute NAME or dialog quit");
    s_initialized = true;
}

void ShutdownScreen()
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

bool IsScreenLoaded()
{
    return s_loaded;
}

void AdvanceScreen(bool advance)
{
    if (!advance || s_resourceMovie == nullptr || !s_resourceMovie->IsResourceExecution())
    {
        return;
    }

    if (renderer::FrameStarted() || s_executionTicks == UINT_MAX || s_nativeSteps > UINT_MAX - 10)
    {
        FatalError("retail execution frame ownership or counter overflow");
    }

    const int time = Sys_Milliseconds();
    const ui::swf::ExecutionScope scope(s_resourceMovie->GetName());
    s_nativeSteps += s_resourceMovie->AdvanceResourceExecution(time);
    s_movieTime = time;
    ++s_executionTicks;
}

void RenderScreen(bool advance)
{
    if (!s_loaded)
    {
        return;
    }

    if (!ui::IsInitialized() || !renderer::FrameStarted() || ui::authored::IsLoaded())
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

    if (s_resourceMovie != nullptr && s_resourceMovie->IsResourceRender())
    {
        // The timeline advanced before this GS frame; native Render only draws. A scope still bounds
        // any script work a draw could reach, such as text-variable lookups.
        renderSystem->SetGLState(0);
        renderSystem->SetColor(colorWhite);
        ui::BeginSwfDraw();

        {
            const ui::swf::ExecutionScope scope(s_resourceMovie->GetName());
            s_resourceMovie->Render(renderSystem, s_movieTime);
        }

        ui::EndSwfDraw();
        renderSystem->SetColor(colorWhite);
        return;
    }

    if (s_resourceMovie != nullptr)
    {
        renderSystem->SetGLState(0);
        renderSystem->DrawSmallStringExt(48, 32, s_movieIndex == 0 ? "Retail shell text resources" : "Retail dialog text resources", colorWhite, true);
        renderSystem->DrawSmallStringExt(48, 52, s_resourceMovie->IsResourceExecution() ? "Native timeline/scripts active; full movie drawing remains pending" : "Native English layout; timelines, masks and Shell remain inactive", colorWhite, true);
        ui::BeginSwfDraw();

        for (int index = 0; index < ArrayLength(s_texts); ++index)
        {
            s_resourceMovie->RenderResourceText(renderSystem, s_texts[index], 48.0f, 100.0f + 110.0f * static_cast<float>(index));
        }

        ui::EndSwfDraw();
        renderSystem->SetColor(colorWhite);
        return;
    }

    const auto font = CurrentFont();
    renderSystem->SetGLState(0);
    renderSystem->DrawSmallStringExt(48, 32, font.family, colorWhite, true);
    renderSystem->DrawSmallStringExt(48, 52, "Native English paletted fonts; campaign and Shell remain inactive", colorWhite, true);
    renderSystem->SetColor(idVec4(240.0f / 255.0f, 248.0f / 255.0f, 1.0f, 1.0f));
    for (int index = 0; index < ArrayLength(kScales); ++index)
    {
        DrawText(s_fonts[s_font], kScales[index], kBaselines[index]);
    }

    renderSystem->SetColor(colorWhite);
}
} // namespace ps2::ui::retail
