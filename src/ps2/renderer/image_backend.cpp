// ================================================================================================
// File: image_backend.cpp
// Brief: Attach bounded texture ownership to native white and authored console image declarations.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/image_backend.h"
#include "ps2/renderer/null_render.h"
#include "ps2/renderer/render_system.h"
#include "ps2/renderer/vram.h"
#include "ps2/system/heap.h"
#include "ps2/ui/authored_ui_data.h"
#include "ps2/ui/page_image.h"
#include "ps2/ui/paletted_image.h"
#include "ps2/ui/retail_assets.h"

#include <idlib/precompiled.h>
#include <new>
#include <renderer/Image.h>

struct idImage::backendState_t
{
    ps2::renderer::tex::Handle texture;
    int storageBytes;
    ps2::ui::PagedImage * pages = nullptr;
};

namespace
{
static bool s_initialized = false;
static idImage * s_charsetImage = nullptr;
static idImage * s_fontImage = nullptr;
static idImage * s_movieImage = nullptr;
static idImage * s_retailImages[ps2::ui::retail::kMaxImages] = {};
static u32 s_retailImageCount = 0;
static ps2::ui::PageCache s_pageCache;
static constexpr u32 kPageSlots = 4;
static constexpr const char * kWhiteName = "_white";
static constexpr const char * kCharsetName = "textures/bigchars";
// The original console charset, as the retail cook catalogs it: sixteen 16x16 cells per axis.
static constexpr const char * kCharsetImage = "generated/images/textures/bigchars#__0200.bimage";
static constexpr int kCharsetAtlasSize = 256;
static constexpr size_t kCharsetAtlasBytes = 256U * 256U * 4U;
static constexpr const char * kFontName = "textures/ps2_ascii";
// The white texel's page: the only texture the image manager creates itself.
static constexpr u32 kWhitePageBytes = 8192;

const ps2::ui::authored::PageResource * PageResource(const char * name)
{
    for (const auto & resource : ps2::ui::authored::kPageResources)
    {
        if (name != nullptr && idStr::Icmp(name, resource.imageName) == 0)
        {
            return &resource;
        }
    }

    return nullptr;
}

bool IsWhiteName(const char * name)
{
    return name != nullptr && idStr::Icmp(name, kWhiteName) == 0;
}

bool IsCharsetName(const char * name)
{
    return name != nullptr && idStr::Icmp(name, kCharsetName) == 0;
}

bool IsFontName(const char * name)
{
    return name != nullptr && idStr::Icmp(name, kFontName) == 0;
}

bool IsRetailImage(const idImage * image)
{
    for (u32 i = 0; i < s_retailImageCount; ++i)
    {
        if (image == s_retailImages[i])
        {
            return true;
        }
    }

    return false;
}

bool SupportedImage(const idImage * image)
{
    return s_initialized && image != nullptr && (image == globalImages->whiteImage || image == s_charsetImage || image == s_fontImage || image == s_movieImage || IsRetailImage(image));
}

bool SupportedSampler(const idImage * image, textureFilter_t filter, textureRepeat_t repeat)
{
    if (!SupportedImage(image))
    {
        return false;
    }
    if (image == globalImages->whiteImage)
    {
        return (filter == TF_DEFAULT || filter == TF_NEAREST || filter == TF_LINEAR) &&
               (repeat == TR_REPEAT || repeat == TR_CLAMP);
    }

    if (IsRetailImage(image))
    {
        return (filter == TF_DEFAULT || filter == TF_LINEAR) && repeat == TR_CLAMP;
    }
    return (filter == TF_DEFAULT || filter == TF_NEAREST) && repeat == TR_CLAMP;
}

ps2::renderer::tex::Sampler ImageSampler(const idImage * image)
{
    return { ps2::renderer::tex::Filter::Nearest, image == globalImages->whiteImage ? ps2::renderer::tex::Wrap::Repeat : ps2::renderer::tex::Wrap::Clamp,
             image == globalImages->whiteImage ? ps2::renderer::tex::Wrap::Repeat : ps2::renderer::tex::Wrap::Clamp };
}

} // namespace

PS2_COLD_FUNC void idImage::LoadPageImage(const ps2::ui::authored::PageResource & resource)
{
    idImage * const image = this;
    if (ps2::renderer::FrameStarted() || image->backendState != nullptr)
    {
        ps2::FatalError("invalid native UI page load ownership");
    }

    if (!s_pageCache.IsInitialized() && !s_pageCache.Init(kPageSlots, { ps2::renderer::tex::Filter::Nearest }))
    {
        ps2::FatalError("native UI shared page budget unavailable");
    }

    auto * const pages = new (TAG_RENDER) ps2::ui::PageImage;
    bool valid = pages->Open(fileSystem->RelativePathToOSPath(resource.path), s_pageCache);
    const auto & header = pages->GetHeader();
    valid = valid && header.width == resource.width && header.height == resource.height &&
            header.fileBytes == resource.fileBytes && header.pageCount == resource.pageCount;

    if (valid)
    {
        const auto * const records = pages->GetRecords();

        for (u32 page = 0; page < header.pageCount; ++page)
        {
            valid = valid && records[page].checksum == resource.checksums[page];
        }
    }

    if (!valid)
    {
        delete pages;
        ps2::FatalError("authored UI page profile mismatch: %s", resource.path);
    }

    // Only checked metadata is resident here. Each page is verified against the admitted table
    // before its first upload; a corrupt unread page cannot replace a valid cached page.
    image->backendState = new (ps2::system::heap::Alloc(sizeof(idImage::backendState_t), TAG_RENDER))
    idImage::backendState_t{ {}, static_cast<int>(header.pageCount * sizeof(ps2::ui::PageRecord)), pages };
    image->opts = idImageOpts();
    image->opts.width = static_cast<int>(header.width);
    image->opts.height = static_cast<int>(header.height);
    image->opts.format = FMT_RGBA8;
    image->opts.numLevels = 1;
    image->filter = TF_NEAREST;
    image->repeat = TR_CLAMP;
    image->usage = TD_FONT;
}

PS2_COLD_FUNC void idImage::LoadRetailPageImage(const ps2::ui::retail::ImageRecord & resource)
{
    if (!ps2::ui::retail::IsInitialized() || ps2::renderer::FrameStarted() || backendState != nullptr || !IsRetailImage(this))
    {
        ps2::FatalError("invalid retail UI image load ownership");
    }

    bool font = false;
    const auto & catalog = ps2::ui::retail::GetCatalog();

    for (std::uint32_t index = 0; index < catalog.FontCount(); ++index)
    {
        ps2::ui::retail::FontRecord record;
        ps2::ui::retail::ImageRecord image;
        catalog.GetFont(index, record);
        catalog.GetImage(record.imageIndex, image);
        font = font || strcmp(image.name, resource.name) == 0;
    }

    // The converted GSTX texture is admitted by the header and tables the catalog carries, then
    // stays resident as paletted pages until the image is purged.
    char path[256];
    idStr::snPrintf(path, static_cast<int>(sizeof(path)), "retail/%s", resource.path);
    auto * const pages = new (TAG_RENDER) ps2::ui::PalettedImage;
    const bool valid = pages->Open(fileSystem->RelativePathToOSPath(path), resource.descriptor, resource.descriptorBytes, resource.fileBytes);
    const auto & header = pages->GetHeader();

    if (!valid || header.logicalWidth != resource.width || header.logicalHeight != resource.height)
    {
        delete pages;
        ps2::FatalError("retail UI texture profile mismatch: %s", resource.name);
    }

    // Nothing is shared between paletted images, so each reports its own pixels and tables.
    const auto stats = pages->GetStats();
    backendState = new (ps2::system::heap::Alloc(sizeof(backendState_t), TAG_RENDER))
    backendState_t{ {}, static_cast<int>(stats.cacheBytes + stats.metadataBytes), pages };
    opts = idImageOpts();
    opts.width = static_cast<int>(resource.width);
    opts.height = static_cast<int>(resource.height);
    opts.format = FMT_RGBA8;
    opts.numLevels = 1;
    filter = TF_LINEAR;
    repeat = TR_CLAMP;
    usage = font ? TD_FONT : TD_DEFAULT;
}

namespace ps2::renderer
{
bool InitConsoleImages()
{
    if (s_initialized)
    {
        FatalError("console images initialized twice");
    }
    if (!renderer::IsInitialized() || !tex::IsInitialized() || renderer::FrameStarted() ||
        gs::GetVramStats().capacityBytes < kWhitePageBytes)
    {
        return false;
    }
    globalImages->Init();
    return s_initialized;
}

void ShutdownConsoleImages()
{
    if (s_initialized)
    {
        globalImages->Shutdown();
    }
}

bool ConsoleImagesInitialized()
{
    return s_initialized;
}

idImage * ConsoleWhiteImage()
{
    return s_initialized ? globalImages->whiteImage : nullptr;
}

idImage * ConsoleCharsetImage()
{
    return s_initialized ? s_charsetImage : nullptr;
}

void OpenConsoleCharset()
{
    if (!s_initialized || s_charsetImage != nullptr || !ui::retail::IsInitialized() || renderer::FrameStarted())
    {
        FatalError("console charset requires a fresh image profile and the retail catalog");
    }

    ui::retail::ImageRecord image;

    // The charset has an explicit retail material (colored, like guiSolid), which the catalog does
    // not bind, so the image is found by its own name; the console draws it without a material.
    if (!ui::retail::GetCatalog().FindImage(kCharsetImage, image) || image.width != kCharsetAtlasSize ||
        image.height != kCharsetAtlasSize)
    {
        FatalError("retail console charset missing from the UI catalog: %s", kCharsetImage);
    }

    s_charsetImage = globalImages->ImageFromFile(image.name, TF_LINEAR, TR_CLAMP, TD_FONT, CF_2D);

    if (s_charsetImage == nullptr || !s_charsetImage->IsLoaded())
    {
        FatalError("retail console charset did not load");
    }
}

#if PS2_D3BFG_TESTS
bool InstallConsoleCharset(const void * rgba, size_t bytes)
{
    if (!s_initialized || s_charsetImage != nullptr || rgba == nullptr || bytes != kCharsetAtlasBytes || renderer::FrameStarted() ||
        globalImages->images.Num() != 1)
    {
        FatalError("console charset installed outside a fresh image profile");
    }

    // Texture VRAM is charged at bind time, so an atlas the whole budget could never hold is
    // refused here, as the former intrinsic was; the pixel budget is checked by Create.
    if (gs::GetVramStats().capacityBytes < kCharsetAtlasBytes)
    {
        return false;
    }

    const tex::Sampler sampler = {};
    const auto texture = tex::Create(kCharsetAtlasSize, kCharsetAtlasSize, sampler, rgba, bytes,
                                     static_cast<size_t>(kCharsetAtlasSize) * 4U);
    auto * const storage = system::heap::TryAlloc(sizeof(idImage::backendState_t), TAG_RENDER);

    // A budget that cannot hold the atlas leaves no partial ownership behind.
    if (texture.id == 0 || storage == nullptr)
    {
        tex::Destroy(texture);
        system::heap::Free(storage);
        return false;
    }

    s_charsetImage = new (TAG_IMAGE) idImage(kCharsetName);
    s_charsetImage->backendState = new (storage) idImage::backendState_t{ texture, static_cast<int>(kCharsetAtlasBytes) };
    s_charsetImage->opts.width = s_charsetImage->opts.height = kCharsetAtlasSize;
    s_charsetImage->opts.format = FMT_RGBA8;
    s_charsetImage->opts.numLevels = 1;
    s_charsetImage->filter = TF_NEAREST;
    s_charsetImage->repeat = TR_CLAMP;
    s_charsetImage->usage = TD_FONT;
    s_charsetImage->referencedOutsideLevelLoad = true;
    globalImages->images.Append(s_charsetImage);
    return true;
}
#endif

idImage * AuthoredFontImage()
{
    return s_initialized ? s_fontImage : nullptr;
}

tex::Handle ConsoleImageTexture(const idImage * image)
{
    ValidateUiImage(image);

    if (image->backendState->pages != nullptr)
    {
        Unavailable("single texture requested for a paged native UI image");
    }

    return image->backendState->texture;
}

void ValidateUiImage(const idImage * image)
{
    if (!SupportedImage(image) || image->backendState == nullptr ||
        (image->backendState->pages != nullptr ? !image->backendState->pages->IsOpen() : image->backendState->texture.id == 0))
    {
        Unavailable("native UI image outside the loaded authored profile");
    }
}

ui::PagedImage * UiPageImage(const idImage * image)
{
    ValidateUiImage(image);
    return image->backendState->pages;
}

ui::PageCacheStats GetUiPageStats()
{
    return s_pageCache.GetStats();
}
} // namespace ps2::renderer

bool idImage::IsLoaded() const
{
    return backendState != nullptr;
}

void idImage::PurgeImage()
{
    if (backendState == nullptr)
    {
        return;
    }
    if (!SupportedImage(this) || !ps2::renderer::tex::IsInitialized())
    {
        ps2::renderer::Unavailable("idImage::PurgeImage outside the console profile");
    }
    if (backendState->pages != nullptr)
    {
        delete backendState->pages;
    }
    else
    {
        ps2::renderer::tex::Destroy(backendState->texture);
    }

    ps2::system::heap::Free(backendState);
    backendState = nullptr;
}

int idImage::StorageSize() const
{
    return backendState != nullptr ? backendState->storageBytes : 0;
}

void idImage::GenerateImage(const byte * rgba, int width, int height, textureFilter_t tf, textureRepeat_t tr, textureUsage_t tu)
{
    const bool white = this == globalImages->whiteImage;
    const int expectedWidth = white ? 1 : kCharsetAtlasSize;
    const int expectedHeight = white ? 1 : kCharsetAtlasSize;
    if (this == s_fontImage || this == s_movieImage || IsRetailImage(this) || !SupportedSampler(this, tf, tr) || (tu != TD_DEFAULT && (white || tu != TD_FONT)) ||
        width != expectedWidth || height != expectedHeight)
    {
        ps2::renderer::Unavailable("idImage::GenerateImage outside the console profile");
    }
    if (rgba == nullptr)
    {
        ps2::FatalError("console image generation requires RGBA pixels");
    }
    if (white && (rgba[0] != 255 || rgba[1] != 255 || rgba[2] != 255 || rgba[3] != 255))
    {
        ps2::renderer::Unavailable("idImage::GenerateImage nonwhite intrinsic pixels");
    }
    const auto sampler = ImageSampler(this);
    ps2::renderer::tex::Layout layout = {};
    if (!ps2::renderer::tex::MakeLayout(width, height, sampler, layout))
    {
        ps2::FatalError("invalid console image layout");
    }
    PurgeImage();
    const size_t stride = static_cast<size_t>(width) * 4U;
    const auto texture = ps2::renderer::tex::Create(width, height, sampler, rgba, stride * static_cast<size_t>(height), stride);
    if (texture.id == 0)
    {
        ps2::FatalError("console image allocation failed");
    }
    backendState = new (ps2::system::heap::Alloc(sizeof(backendState_t), TAG_RENDER)) backendState_t{ texture, static_cast<int>(layout.bytes) };
    opts = idImageOpts();
    opts.width = width;
    opts.height = height;
    opts.format = FMT_RGBA8;
    opts.numLevels = 1;
    filter = tf;
    repeat = tr;
    usage = tu;
}

void idImage::SetSamplerState(textureFilter_t tf, textureRepeat_t tr)
{
    if (!SupportedSampler(this, tf, tr))
    {
        ps2::renderer::Unavailable("idImage::SetSamplerState outside the console profile");
    }
    // The single white texel has identical output under nearest/linear and repeat/clamp.
    filter = tf;
    repeat = tr;
}

PS2_COLD_FUNC void idImageManager::Init()
{
    if (s_initialized || this != globalImages || images.Num() != 0)
    {
        ps2::FatalError("console image manager initialized with existing ownership");
    }
    if (!ps2::renderer::IsInitialized() || !ps2::renderer::tex::IsInitialized() || ps2::renderer::FrameStarted())
    {
        ps2::renderer::Unavailable("idImageManager::Init without an idle console device");
    }
    // The white texel is the only intrinsic. The console charset comes from the retail catalog
    // (OpenConsoleCharset) or, in the core test image, from the installed authored fixture.
    static constexpr byte kWhite[4] = { 255, 255, 255, 255 };
    const ps2::renderer::tex::Sampler whiteSampler = { ps2::renderer::tex::Filter::Nearest, ps2::renderer::tex::Wrap::Repeat, ps2::renderer::tex::Wrap::Repeat };
    const auto whiteTexture = ps2::renderer::tex::Create(1, 1, whiteSampler, kWhite, sizeof(kWhite), sizeof(kWhite));
    auto * const whiteStorage = ps2::system::heap::TryAlloc(sizeof(idImage::backendState_t), TAG_RENDER);
    if (whiteTexture.id == 0 || whiteStorage == nullptr)
    {
        ps2::renderer::tex::Destroy(whiteTexture);
        ps2::system::heap::Free(whiteStorage);
        return;
    }
    ps2::renderer::tex::Layout whiteLayout = {};
    if (!ps2::renderer::tex::MakeLayout(1, 1, whiteSampler, whiteLayout))
    {
        ps2::FatalError("invalid white intrinsic layout");
    }
    whiteImage = new (TAG_IMAGE) idImage(kWhiteName);
    whiteImage->backendState = new (whiteStorage) idImage::backendState_t{ whiteTexture, static_cast<int>(whiteLayout.bytes) };
    whiteImage->opts.width = whiteImage->opts.height = 1;
    whiteImage->opts.format = FMT_RGBA8;
    whiteImage->opts.numLevels = 1;
    whiteImage->filter = TF_NEAREST;
    whiteImage->repeat = TR_REPEAT;
    whiteImage->usage = TD_DEFAULT;
    whiteImage->referencedOutsideLevelLoad = true;
    images.SetGranularity(2);
    images.Append(whiteImage);
    s_initialized = true;
}

PS2_COLD_FUNC void idImageManager::Shutdown()
{
    if (!s_initialized)
    {
        return;
    }
    if (this != globalImages || ps2::renderer::FrameStarted())
    {
        ps2::FatalError("console image shutdown requires idle ownership");
    }
    for (int i = 0; i < images.Num(); ++i)
    {
        images[i]->PurgeImage();
    }
    s_pageCache.Shutdown();
    ps2::ui::retail::Shutdown();
    images.DeleteContents(true);
    imageHash.Clear();
    whiteImage = nullptr;
    s_charsetImage = nullptr;
    s_fontImage = nullptr;
    s_movieImage = nullptr;
    memset(s_retailImages, 0, sizeof(s_retailImages));
    s_retailImageCount = 0;
    s_initialized = false;
}

idImage * idImageManager::GetImage(const char * name) const
{
    if (!s_initialized || this != globalImages)
    {
        ps2::renderer::Unavailable("idImageManager::GetImage outside the console profile");
    }
    if (IsWhiteName(name))
    {
        return whiteImage;
    }
    if (IsCharsetName(name))
    {
        return s_charsetImage;
    }
    if (IsFontName(name))
    {
        return s_fontImage;
    }
    if (s_movieImage != nullptr && PageResource(name) != nullptr)
    {
        return s_movieImage;
    }

    for (u32 i = 0; i < s_retailImageCount; ++i)
    {
        if (name != nullptr && strcmp(name, s_retailImages[i]->GetName()) == 0)
        {
            return s_retailImages[i];
        }
    }
    return nullptr;
}

idImage * idImageManager::GetImageWithParameters(const char * name, textureFilter_t tf, textureRepeat_t tr,
                                                 textureUsage_t tu, cubeFiles_t cube) const
{
    idImage * const image = GetImage(name);
    if (image == nullptr || !SupportedSampler(image, tf, tr) || cube != CF_2D ||
        (tu != TD_DEFAULT && (image == whiteImage || tu != TD_FONT)))
    {
        return nullptr;
    }
    return image;
}

idImage * idImageManager::ImageFromFile(const char * name, textureFilter_t tf, textureRepeat_t tr,
                                        textureUsage_t tu, cubeFiles_t cube)
{
    if (!s_initialized || this != globalImages)
    {
        ps2::renderer::Unavailable("idImageManager::ImageFromFile");
    }

    ps2::ui::retail::ImageRecord retail;

    if (ps2::ui::retail::IsInitialized() && ps2::ui::retail::GetCatalog().FindImage(name, retail))
    {
        if ((tf != TF_DEFAULT && tf != TF_LINEAR) || tr != TR_CLAMP || cube != CF_2D ||
            (tu != TD_DEFAULT && tu != TD_FONT))
        {
            ps2::FatalError("retail native UI sampler profile invalid: %s", retail.name);
        }

        idImage * image = GetImage(name);

        if (image == nullptr)
        {
            if (s_retailImageCount == static_cast<u32>(ps2::ArrayLength(s_retailImages)))
            {
                ps2::FatalError("retail native image count exceeds bounded catalog");
            }

            image = new (TAG_IMAGE) idImage(retail.name);
            s_retailImages[s_retailImageCount++] = image;
            images.Append(image);
            image->referencedOutsideLevelLoad = true;
        }

        if (!image->IsLoaded())
        {
            image->LoadRetailPageImage(retail);
        }

        image->AddReference();
        return image;
    }

    const auto * const resource = PageResource(name);

    if (resource != nullptr && GetImage(name) == nullptr)
    {
        if ((tf != TF_DEFAULT && tf != TF_NEAREST) || tr != TR_CLAMP ||
            (tu != TD_DEFAULT && tu != TD_FONT) || cube != CF_2D)
        {
            ps2::renderer::Unavailable("authored native page sampler profile");
        }
        idImage * const image = new (TAG_IMAGE) idImage(resource->imageName);
        (IsFontName(name) ? s_fontImage : s_movieImage) = image;
        images.Append(image);
        image->referencedOutsideLevelLoad = true;
        image->LoadPageImage(*resource);
    }
    idImage * const image = GetImageWithParameters(name, tf, tr, tu, cube);
    if (image == nullptr)
    {
        ps2::FatalError("idImageManager::ImageFromFile outside the authored UI profile name=%s filter=%d repeat=%d usage=%d cube=%d",
                        name != nullptr ? name : "<null>", static_cast<int>(tf), static_cast<int>(tr),
                        static_cast<int>(tu), static_cast<int>(cube));
    }
    if (!image->IsLoaded())
    {
        if (resource == nullptr)
        {
            ps2::renderer::Unavailable("idImageManager::ImageFromFile with purged intrinsic");
        }
        image->LoadPageImage(*resource);
    }
    image->AddReference();
    return image;
}
