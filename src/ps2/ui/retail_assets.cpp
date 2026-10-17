// ================================================================================================
// File: retail_assets.cpp
// Brief: Publish checked English resources and separately admit retail movie storage and execution.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/ui/retail_assets.h"
#include "ps2/renderer/image_backend.h"
#include "ps2/renderer/render_system.h"
#include "ps2/renderer/ui_draw.h"
#include "ps2/renderer/ui_font_format.h"
#include "ps2/renderer/ui_page_image.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"

#include <idlib/precompiled.h>

namespace ps2::ui::retail
{
namespace
{
static void * s_bytes = nullptr;
static CatalogView s_catalog;
static std::uint32_t s_profile[16] = {};
static bool s_language = false;
static SwfSummary s_movieSummary;
static bool s_executionRequested = false;
static bool s_executionAdmitted = false;
static constexpr const char * kMovies[] = { "generated/swf/shell.bswf", "generated/swf/dialog.bswf" };
static constexpr const char * kMovieFiles[] = { "retail/shell.bswf", "retail/dialog.bswf" };

void * Snapshot(const char * path, std::uint32_t length, std::uint32_t checksum)
{
    idFile * const source = fileSystem->OpenFileRead(path);

    if (source == nullptr)
    {
        FatalError("retail movie resource missing: %s", path);
    }

    if (source->Length() != static_cast<int>(length))
    {
        delete source;
        FatalError("retail movie resource length mismatch: %s", path);
    }

    void * const bytes = heap::Alloc(length, TAG_SWF);
    const bool read = source->Read(bytes, static_cast<int>(length)) == static_cast<int>(length);
    delete source;

    if (!read || PageChecksum(bytes, length) != checksum)
    {
        heap::Free(bytes);
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

bool IsInitialized()
{
    return s_bytes != nullptr;
}

PS2_COLD_FUNC void Init()
{
    if (IsInitialized() || !ui::IsInitialized() || rs::FrameStarted() || render::GetUiPageStats().cacheBytes != 0)
    {
        FatalError("retail UI catalog requires an idle fresh image profile");
    }

    idFile * const source = fileSystem->OpenFileRead("retail_ui.uic");

    if (source == nullptr)
    {
        FatalError("retail UI catalog missing");
    }

    const int length = source->Length();

    if (length < 64 || static_cast<size_t>(length) > kMaxCatalogBytes)
    {
        delete source;
        FatalError("retail UI catalog length invalid");
    }

    void * const bytes = heap::Alloc(static_cast<size_t>(length), TAG_RENDER);
    const bool read = source->Read(bytes, length) == length;
    delete source;
    CatalogView catalog;

    if (!read || !catalog.Init(bytes, static_cast<size_t>(length)))
    {
        heap::Free(bytes);
        FatalError("retail UI catalog invalid");
    }

    s_bytes = bytes;
    s_catalog = catalog;
    Log(LogLevel::Info, "[D3BFG] RETAIL catalog ready bytes=%d images=%lu fonts=%lu materials=%lu language=english\n",
        length, static_cast<unsigned long>(catalog.ImageCount()), static_cast<unsigned long>(catalog.FontCount()),
        static_cast<unsigned long>(catalog.MaterialCount()));
}

PS2_COLD_FUNC void Shutdown()
{
    if (!IsInitialized())
    {
        return;
    }

    if (rs::FrameStarted() || render::GetUiPageStats().images != 0)
    {
        FatalError("retail UI catalog shutdown with active image ownership");
    }

    if (s_language)
    {
        idLocalization::GetDictionary().ReleaseResources();
        s_language = false;
    }

    memset(s_profile, 0, sizeof(s_profile));
    s_movieSummary = {};
    s_executionRequested = false;
    s_executionAdmitted = false;
    heap::Free(s_bytes);
    s_bytes = nullptr;
    s_catalog = {};
}

const CatalogView & GetCatalog()
{
    if (!IsInitialized())
    {
        FatalError("retail UI catalog unavailable");
    }

    return s_catalog;
}

bool FindFont(const char * requested, FontRecord & output)
{
    return IsInitialized() && s_catalog.FindFont(requested, output);
}

PS2_COLD_FUNC idFile * OpenFont(const FontRecord & font)
{
    FontRecord admitted;
    ImageRecord image;

    if (!FindFont(font.family, admitted) || font.path != admitted.path || font.bytes != admitted.bytes ||
        font.checksum != admitted.checksum || !s_catalog.GetImage(admitted.imageIndex, image))
    {
        FatalError("retail font outside the active catalog");
    }

    char path[256];
    idStr::snPrintf(path, static_cast<int>(sizeof(path)), "retail/%s", admitted.path);
    idFile * const source = fileSystem->OpenFileRead(path);

    if (source == nullptr)
    {
        FatalError("retail font missing: %s", admitted.family);
    }

    if (source->Length() != static_cast<int>(admitted.bytes))
    {
        delete source;
        FatalError("retail font length mismatch: %s", admitted.family);
    }

    auto * const bytes = static_cast<char *>(heap::Alloc(admitted.bytes, TAG_FONT));
    const bool read = source->Read(bytes, static_cast<int>(admitted.bytes)) == static_cast<int>(admitted.bytes);
    delete source;
    fonts::FontFormatSummary summary;

    if (!read || PageChecksum(bytes, admitted.bytes) != admitted.checksum ||
        !fonts::ValidateFontBytes(bytes, admitted.bytes, image.width, image.height, summary))
    {
        heap::Free(bytes);
        FatalError("retail font content invalid: %s", admitted.family);
    }

    Log(LogLevel::Info, "[D3BFG] RETAIL font admitted name=%s glyphs=%lu duplicates=%lu edge_glyphs=%lu\n", admitted.family,
        static_cast<unsigned long>(summary.glyphCount), static_cast<unsigned long>(summary.duplicateCodepoints),
        static_cast<unsigned long>(summary.samplingGutterGlyphs));
    auto * const snapshot = new (TAG_FONT) idFile_Memory(admitted.logicalPath, static_cast<const char *>(bytes), static_cast<int>(admitted.bytes));
    snapshot->TakeDataOwnership();
    return snapshot;
}

PS2_COLD_FUNC void PrepareMovie(unsigned index, bool execute)
{
    if (!IsInitialized() || rs::FrameStarted() || index >= 2)
    {
        FatalError("invalid retail movie preparation ownership");
    }

    s_executionRequested = execute;
    s_executionAdmitted = false;

    if (s_profile[0] == 0)
    {
        idFile * const profile = fileSystem->OpenFileRead("retail/movies.uim");
        unsigned char raw[64] = {};
        const bool read = profile != nullptr && profile->Length() == 64 && profile->Read(raw, 64) == 64;
        delete profile;

        if (!read)
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
            s_profile[7] == 0 || s_profile[7] > 16384)
        {
            FatalError("retail movie profile invalid");
        }

        for (unsigned movie = 0; movie < 2; ++movie)
        {
            const unsigned offset = 8 + movie * 4;

            if (s_profile[offset] < 64 || s_profile[offset] > kMaxSwfBytes || s_profile[offset + 2] == 0 ||
                s_profile[offset + 2] > 1024 || s_profile[offset + 3] >= s_profile[offset + 2])
            {
                FatalError("retail movie profile bounds invalid");
            }
        }
    }

    if (!s_language)
    {
        if (idLocalization::GetDictionary().GetNumKeyVals() != 0)
        {
            FatalError("retail English dictionary requires exclusive localization ownership");
        }

        auto * const bytes = static_cast<byte *>(Snapshot("retail/english.lang", s_profile[5], s_profile[6]));
        std::uint32_t entries = 0;

        if (!ValidateEnglishBytes(bytes, s_profile[5], entries) || entries != s_profile[7])
        {
            heap::Free(bytes);
            FatalError("retail English dictionary structural preflight failed");
        }

        idLocalization::VerifyUTF8(bytes, static_cast<int>(s_profile[5]), "retail/english.lang");
        const bool loaded = idLocalization::LoadDictionary(bytes, static_cast<int>(s_profile[5]), "retail/english.lang");
        heap::Free(bytes);

        if (!loaded || idLocalization::GetDictionary().GetNumKeyVals() != static_cast<int>(s_profile[7]))
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

    if (index == 2 || !s_language || rs::FrameStarted())
    {
        FatalError("retail movie outside the prepared inactive profile");
    }

    const unsigned offset = 8 + index * 4;
    auto * const bytes = static_cast<char *>(Snapshot(kMovieFiles[index], s_profile[offset], s_profile[offset + 1]));
    SwfSummary summary;

    if (!ValidateSwfBytes(bytes, s_profile[offset], summary, MovieIdentity) ||
        summary.dictionary != s_profile[offset + 2] || summary.previewText != s_profile[offset + 3])
    {
        heap::Free(bytes);
        FatalError("retail movie structural preflight failed: %s", logicalPath);
    }

    if (s_executionRequested)
    {
        SwfExecutionSummary execution;
        if (!ValidateSwfExecutionBytes(bytes, s_profile[offset], execution))
        {
            heap::Free(bytes);
            FatalError("retail movie execution preflight failed: %s", logicalPath);
        }

        s_executionAdmitted = true;
        Log(LogLevel::Info,
            "[D3BFG] RETAIL executable movie=%u actions=%lu functions=%lu placements=%lu removals=%lu\n",
            index, static_cast<unsigned long>(execution.actions), static_cast<unsigned long>(execution.functions),
            static_cast<unsigned long>(execution.placements), static_cast<unsigned long>(execution.removals));
    }

    s_movieSummary = summary;
    Log(LogLevel::Info, "[D3BFG] RETAIL BSWF admitted movie=%u dictionary=%lu sprites=%lu commands=%lu fonts=%lu texts=%lu preview=%lu scripts=inactive\n",
        index, static_cast<unsigned long>(summary.dictionary), static_cast<unsigned long>(summary.sprites),
        static_cast<unsigned long>(summary.commands), static_cast<unsigned long>(summary.fonts),
        static_cast<unsigned long>(summary.editTexts), static_cast<unsigned long>(summary.previewText));
    auto * const snapshot = new (TAG_SWF) idFile_Memory(logicalPath, static_cast<const char *>(bytes), static_cast<int>(s_profile[offset]));
    snapshot->TakeDataOwnership();
    return snapshot;
}

bool MovieExecutionAdmitted()
{
    return IsInitialized() && s_language && s_executionAdmitted;
}

SwfSummary GetMovieSummary()
{
    return s_movieSummary;
}
} // namespace ps2::ui::retail
