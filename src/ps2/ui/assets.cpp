// ================================================================================================
// File: assets.cpp
// Brief: Publish checked English resources and separately admit retail movie storage and execution.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/assets.h"
#include "ps2/renderer/image_backend.h"
#include "ps2/renderer/render_system.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include "ps2/ui/draw.h"
#include "ps2/ui/font_format.h"
#include "ps2/ui/paletted_image.h"

#include <idlib/precompiled.h>

namespace ps2::ui
{
namespace
{

static void * s_bytes = nullptr;
static CatalogView s_catalog;
static std::uint32_t s_profile[16] = {};
static bool s_language = false;

static constexpr const char * kMovies[] = { "generated/swf/shell.bswf", "generated/swf/dialog.bswf" };
static constexpr const char * kMovieFiles[] = { "retail/shell.bswf", "retail/dialog.bswf" };
// The names native idSWF gives the two movies.
static constexpr const char * kMovieNames[] = { "swf/shell.swf", "swf/dialog.swf" };

// Each movie's admission from its last preparation, so the shell and the dialog can be resident at
// once. A preparation admits one construction.
struct MovieAdmission
{
    bool prepared = false;
    bool executionRequested = false;
    bool executionAdmitted = false;
    bool renderRequested = false;
    bool renderAdmitted = false;
    std::uint32_t screenBlends = 0;
    swf::Summary summary;
};

static MovieAdmission s_movies[2];

PS2_COLD_FUNC void * Snapshot(const char * path, std::uint32_t length, std::uint32_t checksum)
{
    idFile * const source = fileSystem->OpenFileRead(path);

    if (source == nullptr) [[unlikely]]
    {
        FatalError("retail movie resource missing: %s", path);
    }

    if (source->Length() != static_cast<int>(length))
    {
        delete source;
        FatalError("retail movie resource length mismatch: %s", path);
    }

    void * const bytes = system::heap::Alloc(length, TAG_SWF);
    const bool read = source->Read(bytes, static_cast<int>(length)) == static_cast<int>(length);
    delete source;

    if (!read || gstx::Checksum(bytes, length) != checksum)
    {
        system::heap::Free(bytes);
        FatalError("retail movie resource checksum mismatch: %s", path);
    }

    return bytes;
}

bool MovieIdentity(const char * name, bool font, void *)
{
    if (font)
    {
        FontRecord record;
        return FindFont(name, record);
    }

    idStr canonical(name);
    canonical.ToLower();
    canonical.BackSlashesToSlashes();
    canonical.StripFileExtension();

    for (std::uint32_t index = 0; index < s_catalog.MaterialCount(); ++index)
    {
        MaterialRecord record;
        s_catalog.GetMaterial(index, record);

        if (canonical == record.name)
        {
            return true;
        }
    }

    return false;
}

} // namespace

bool IsCatalogInitialized()
{
    return s_bytes != nullptr;
}

PS2_COLD_FUNC void InitCatalog()
{
    if (IsCatalogInitialized() || !ui::IsInitialized() || renderer::FrameStarted() || ui::GetPaletteStats().images != 0) [[unlikely]]
    {
        FatalError("retail UI catalog requires an idle fresh image profile");
    }

    idFile * const source = fileSystem->OpenFileRead("retail_ui.uic");

    if (source == nullptr) [[unlikely]]
    {
        FatalError("retail UI catalog missing");
    }

    const int length = source->Length();

    if (length < 64 || static_cast<size_t>(length) > kMaxCatalogBytes)
    {
        delete source;
        FatalError("retail UI catalog length invalid");
    }

    void * const bytes = system::heap::Alloc(static_cast<size_t>(length), TAG_RENDER);
    const bool read = source->Read(bytes, length) == length;
    delete source;
    CatalogView catalog;

    if (!read || !catalog.Init(bytes, static_cast<size_t>(length)))
    {
        system::heap::Free(bytes);
        FatalError("retail UI catalog invalid");
    }

    s_bytes = bytes;
    s_catalog = catalog;
    Log(LogLevel::Info, "[D3BFG] RETAIL catalog ready bytes=%d images=%lu fonts=%lu materials=%lu language=english\n",
        length, static_cast<unsigned long>(catalog.ImageCount()), static_cast<unsigned long>(catalog.FontCount()),
        static_cast<unsigned long>(catalog.MaterialCount()));
}

PS2_COLD_FUNC void ShutdownCatalog()
{
    if (!IsCatalogInitialized())
    {
        return;
    }

    if (renderer::FrameStarted() || ui::GetPaletteStats().images != 0) [[unlikely]]
    {
        FatalError("retail UI catalog shutdown with active image ownership");
    }

    if (s_language)
    {
        idLocalization::GetDictionary().ReleaseResources();
        s_language = false;
    }

    memset(s_profile, 0, sizeof(s_profile));

    for (auto & movie : s_movies)
    {
        movie = {};
    }

    system::heap::Free(s_bytes);
    s_bytes = nullptr;
    s_catalog = {};
}

const CatalogView & GetCatalog()
{
    PS2_AssertMsg(IsCatalogInitialized(), "retail UI catalog unavailable");
    return s_catalog;
}

bool FindFont(const char * requested, FontRecord & output)
{
    return IsCatalogInitialized() && s_catalog.FindFont(requested, output);
}

PS2_COLD_FUNC idFile * OpenFont(const FontRecord & font)
{
    FontRecord admitted;
    ImageRecord image;

    if (!FindFont(font.family, admitted) || font.path != admitted.path || font.bytes != admitted.bytes ||
        font.checksum != admitted.checksum || !s_catalog.GetImage(admitted.imageIndex, image)) [[unlikely]]
    {
        FatalError("retail font outside the active catalog");
    }

    char path[256];
    idStr::snPrintf(path, static_cast<int>(sizeof(path)), "retail/%s", admitted.path);
    idFile * const source = fileSystem->OpenFileRead(path);

    if (source == nullptr) [[unlikely]]
    {
        FatalError("retail font missing: %s", admitted.family);
    }

    if (source->Length() != static_cast<int>(admitted.bytes))
    {
        delete source;
        FatalError("retail font length mismatch: %s", admitted.family);
    }

    auto * const bytes = static_cast<char *>(system::heap::Alloc(admitted.bytes, TAG_FONT));
    const bool read = source->Read(bytes, static_cast<int>(admitted.bytes)) == static_cast<int>(admitted.bytes);
    delete source;
    fonts::FontFormatSummary summary;

    if (!read || gstx::Checksum(bytes, admitted.bytes) != admitted.checksum ||
        !fonts::ValidateFontBytes(bytes, admitted.bytes, image.width, image.height, summary))
    {
        system::heap::Free(bytes);
        FatalError("retail font content invalid: %s", admitted.family);
    }

    Log(LogLevel::Info, "[D3BFG] RETAIL font admitted name=%s glyphs=%lu duplicates=%lu edge_glyphs=%lu\n", admitted.family,
        static_cast<unsigned long>(summary.glyphCount), static_cast<unsigned long>(summary.duplicateCodepoints),
        static_cast<unsigned long>(summary.samplingGutterGlyphs));
    auto * const snapshot = new (TAG_FONT) idFile_Memory(admitted.logicalPath, static_cast<const char *>(bytes), static_cast<int>(admitted.bytes));
    snapshot->TakeDataOwnership();
    return snapshot;
}

PS2_COLD_FUNC void PrepareMovie(unsigned index, bool execute, bool render)
{
    if (!IsCatalogInitialized() || renderer::FrameStarted() || index >= 2 || (render && !execute)) [[unlikely]]
    {
        FatalError("invalid retail movie preparation ownership");
    }

    auto & admission = s_movies[index];
    admission = {};
    admission.prepared = true;
    admission.executionRequested = execute;
    admission.renderRequested = render;

    if (s_profile[0] == 0)
    {
        idFile * const profile = fileSystem->OpenFileRead("retail/movies.uim");
        unsigned char raw[64] = {};
        const bool read = profile != nullptr && profile->Length() == 64 && profile->Read(raw, 64) == 64;
        delete profile;

        if (!read) [[unlikely]]
        {
            FatalError("retail movie profile missing or truncated");
        }

        for (unsigned word = 0; word < 16; ++word)
        {
            for (unsigned byte = 0; byte < 4; ++byte)
            {
                s_profile[word] |= static_cast<std::uint32_t>(raw[word * 4 + byte]) << (byte * 8);
            }
        }

        if (s_profile[0] != 0x564d4955U || s_profile[1] != 1 || s_profile[2] != 64 ||
            s_profile[3] != 1 || s_profile[4] != 2 || s_profile[5] == 0 || s_profile[5] > 1024 * 1024 ||
            s_profile[7] == 0 || s_profile[7] > 16384) [[unlikely]]
        {
            FatalError("retail movie profile invalid");
        }

        for (unsigned movie = 0; movie < 2; ++movie)
        {
            const unsigned offset = 8 + movie * 4;

            if (s_profile[offset] < 64 || s_profile[offset] > swf::kMaxBytes || s_profile[offset + 2] == 0 ||
                s_profile[offset + 2] > 1024 || s_profile[offset + 3] >= s_profile[offset + 2]) [[unlikely]]
            {
                FatalError("retail movie profile bounds invalid");
            }
        }
    }

    if (!s_language)
    {
        if (idLocalization::GetDictionary().GetNumKeyVals() != 0) [[unlikely]]
        {
            FatalError("retail English dictionary requires exclusive localization ownership");
        }

        auto * const bytes = static_cast<byte *>(Snapshot("retail/english.lang", s_profile[5], s_profile[6]));
        std::uint32_t entries = 0;

        if (!swf::ValidateEnglishBytes(bytes, s_profile[5], entries) || entries != s_profile[7])
        {
            system::heap::Free(bytes);
            FatalError("retail English dictionary structural preflight failed");
        }

        idLocalization::VerifyUTF8(bytes, static_cast<int>(s_profile[5]), "retail/english.lang");
        const bool loaded = idLocalization::LoadDictionary(bytes, static_cast<int>(s_profile[5]), "retail/english.lang");
        system::heap::Free(bytes);

        if (!loaded || idLocalization::GetDictionary().GetNumKeyVals() != static_cast<int>(s_profile[7])) [[unlikely]]
        {
            FatalError("retail English dictionary entry count invalid");
        }

        s_language = true;
        Log(LogLevel::Info, "[D3BFG] RETAIL English ready entries=%lu bytes=%lu\n",
            static_cast<unsigned long>(s_profile[7]), static_cast<unsigned long>(s_profile[5]));
    }
}

PS2_COLD_FUNC idFile * OpenMovie(const char * logicalPath)
{
    unsigned index = 0;

    for (; index < 2; ++index)
    {
        if (logicalPath != nullptr && strcmp(logicalPath, kMovies[index]) == 0)
        {
            break;
        }
    }

    if (index == 2 || !s_language || !s_movies[index].prepared || renderer::FrameStarted()) [[unlikely]]
    {
        FatalError("retail movie outside the prepared inactive profile");
    }

    auto & movie = s_movies[index];
    movie.prepared = false;
    const unsigned offset = 8 + index * 4;
    auto * const bytes = static_cast<char *>(Snapshot(kMovieFiles[index], s_profile[offset], s_profile[offset + 1]));
    swf::Summary summary;

    if (!swf::ValidateBytes(bytes, s_profile[offset], summary, MovieIdentity) ||
        summary.dictionary != s_profile[offset + 2] || summary.previewText != s_profile[offset + 3])
    {
        system::heap::Free(bytes);
        FatalError("retail movie structural preflight failed: %s", logicalPath);
    }

    if (movie.executionRequested)
    {
        swf::ExecutionSummary execution;
        if (!swf::ValidateExecutionBytes(bytes, s_profile[offset], execution))
        {
            system::heap::Free(bytes);
            FatalError("retail movie execution preflight failed: %s", logicalPath);
        }

        movie.executionAdmitted = true;
        Log(LogLevel::Info,
            "[D3BFG] RETAIL executable movie=%u actions=%lu functions=%lu placements=%lu removals=%lu\n",
            index, static_cast<unsigned long>(execution.actions), static_cast<unsigned long>(execution.functions),
            static_cast<unsigned long>(execution.placements), static_cast<unsigned long>(execution.removals));

        if (movie.renderRequested)
        {
            // Outlines draw as GS lines, screen blending in per-channel passes and additive RGB in a
            // constant-color pass; an additive alpha term has no 2D path. Masks are admitted here, and
            // the draw path still refuses two non-convex masks at once.
            if (execution.additiveAlphas != 0)
            {
                system::heap::Free(bytes);
                FatalError("retail movie draw features unavailable: %s additive_alphas=%lu", logicalPath,
                           static_cast<unsigned long>(execution.additiveAlphas));
            }

            movie.renderAdmitted = true;
            movie.screenBlends = execution.screenBlends;
            Log(LogLevel::Info, "[D3BFG] RETAIL drawable movie=%u masks=%lu strokes=%lu screen_blends=%lu additive_colors=%lu\n",
                index, static_cast<unsigned long>(execution.masks), static_cast<unsigned long>(summary.strokes),
                static_cast<unsigned long>(execution.screenBlends), static_cast<unsigned long>(execution.additiveColors));
        }
    }

    movie.summary = summary;
    Log(LogLevel::Info, "[D3BFG] RETAIL BSWF admitted movie=%u dictionary=%lu sprites=%lu commands=%lu fonts=%lu texts=%lu preview=%lu scripts=inactive\n",
        index, static_cast<unsigned long>(summary.dictionary), static_cast<unsigned long>(summary.sprites),
        static_cast<unsigned long>(summary.commands), static_cast<unsigned long>(summary.fonts),
        static_cast<unsigned long>(summary.editTexts), static_cast<unsigned long>(summary.previewText));
    auto * const snapshot = new (TAG_SWF) idFile_Memory(logicalPath, static_cast<const char *>(bytes), static_cast<int>(s_profile[offset]));
    snapshot->TakeDataOwnership();
    return snapshot;
}

int MovieIndex(const char * name)
{
    for (int index = 0; index < ArrayLength(kMovieNames); ++index)
    {
        if (name != nullptr && strcmp(name, kMovieNames[index]) == 0)
        {
            return index;
        }
    }

    return -1;
}

bool MovieExecutionAdmitted(int index)
{
    return IsCatalogInitialized() && s_language && index >= 0 && index < ArrayLength(s_movies) && s_movies[index].executionAdmitted;
}

bool MovieRenderAdmitted(int index)
{
    return MovieExecutionAdmitted(index) && s_movies[index].renderAdmitted;
}

bool MovieUsesScreenBlending(int index)
{
    return MovieRenderAdmitted(index) && s_movies[index].screenBlends != 0;
}

swf::Summary GetMovieSummary(int index)
{
    if (index < 0 || index >= ArrayLength(s_movies)) [[unlikely]]
    {
        FatalError("retail movie summary outside the fixed profile");
    }

    return s_movies[index].summary;
}

} // namespace ps2::ui
