// ================================================================================================
// File: retail_assets.cpp
// Brief: Publish bounded catalog/font snapshots without admitting retail scripts or unchecked counts.
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
} // namespace ps2::ui::retail
