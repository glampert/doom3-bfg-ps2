// ================================================================================================
// File: image_backend.cpp
// Brief: Attach bounded texture ownership to native white and authored console image declarations.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/image_backend.h"
#include "ps2/renderer/charset.h"
#include "ps2/renderer/null_render.h"
#include "ps2/renderer/render_system.h"
#include "ps2/renderer/vram.h"
#include "ps2/system/heap.h"

#include <idlib/precompiled.h>
#include <renderer/Image.h>
#include <new>

struct idImage::backendState_t
{
    ps2::tex::Handle texture;
    int storageBytes;
};

namespace
{
static bool s_initialized = false;
static idImage * s_charsetImage = nullptr;
static constexpr const char * kWhiteName = "_white";
static constexpr const char * kCharsetName = "textures/bigchars";

bool IsWhiteName(const char * name)
{
    return name != nullptr && idStr::Icmp(name, kWhiteName) == 0;
}
bool IsCharsetName(const char * name)
{
    return name != nullptr && idStr::Icmp(name, kCharsetName) == 0;
}
bool SupportedImage(const idImage * image)
{
    return s_initialized && image != nullptr && (image == globalImages->whiteImage || image == s_charsetImage);
}
bool SupportedSampler(const idImage * image, textureFilter_t filter, textureRepeat_t repeat)
{
    if (!SupportedImage(image)) { return false; }
    if (image == globalImages->whiteImage)
    {
        return (filter == TF_DEFAULT || filter == TF_NEAREST || filter == TF_LINEAR) &&
               (repeat == TR_REPEAT || repeat == TR_CLAMP);
    }
    return (filter == TF_DEFAULT || filter == TF_NEAREST) && repeat == TR_CLAMP;
}
ps2::tex::Sampler ImageSampler(const idImage * image)
{
    return {ps2::tex::Filter::Nearest, image == globalImages->whiteImage ? ps2::tex::Wrap::Repeat : ps2::tex::Wrap::Clamp,
            image == globalImages->whiteImage ? ps2::tex::Wrap::Repeat : ps2::tex::Wrap::Clamp};
}
} // namespace

namespace ps2::render
{
bool InitConsoleImages()
{
    if (s_initialized) { FatalError("console images initialized twice"); }
    if (!rs::IsInitialized() || !tex::IsInitialized() || rs::FrameStarted() ||
        vram::GetStats().capacityBytes < kConsoleAtlasBytes) { return false; }
    globalImages->Init();
    return s_initialized;
}
void ShutdownConsoleImages()
{
    if (s_initialized) { globalImages->Shutdown(); }
}
bool ConsoleImagesInitialized() { return s_initialized; }
idImage * ConsoleWhiteImage() { return s_initialized ? globalImages->whiteImage : nullptr; }
idImage * ConsoleCharsetImage() { return s_initialized ? s_charsetImage : nullptr; }
tex::Handle ConsoleImageTexture(const idImage * image)
{
    if (!SupportedImage(image) || image->backendState == nullptr)
    {
        Unavailable("ConsoleImageTexture outside the loaded console profile");
    }
    return image->backendState->texture;
}
} // namespace ps2::render

bool idImage::IsLoaded() const { return backendState != nullptr; }

void idImage::PurgeImage()
{
    if (backendState == nullptr) { return; }
    if (!SupportedImage(this) || !ps2::tex::IsInitialized())
    {
        ps2::render::Unavailable("idImage::PurgeImage outside the console profile");
    }
    ps2::tex::Destroy(backendState->texture);
    ps2::heap::Free(backendState);
    backendState = nullptr;
}

int idImage::StorageSize() const { return backendState != nullptr ? backendState->storageBytes : 0; }

void idImage::GenerateImage(const byte * rgba, int width, int height, textureFilter_t tf, textureRepeat_t tr, textureUsage_t tu)
{
    const bool white = this == globalImages->whiteImage;
    if (!SupportedSampler(this, tf, tr) || (tu != TD_DEFAULT && (white || tu != TD_FONT)) ||
        (white ? (width != 1 || height != 1) : (width != ps2::render::kConsoleAtlasSize || height != ps2::render::kConsoleAtlasSize)))
    {
        ps2::render::Unavailable("idImage::GenerateImage outside the console profile");
    }
    if (rgba == nullptr) { ps2::FatalError("console image generation requires RGBA pixels"); }
    if (white && (rgba[0] != 255 || rgba[1] != 255 || rgba[2] != 255 || rgba[3] != 255))
    {
        ps2::render::Unavailable("idImage::GenerateImage nonwhite intrinsic pixels");
    }
    const auto sampler = ImageSampler(this);
    ps2::tex::Layout layout = {};
    if (!ps2::tex::MakeLayout(width, height, sampler, layout)) { ps2::FatalError("invalid console image layout"); }
    PurgeImage();
    const size_t stride = static_cast<size_t>(width) * 4U;
    const auto texture = ps2::tex::Create(width, height, sampler, rgba, stride * static_cast<size_t>(height), stride);
    if (texture.id == 0) { ps2::FatalError("console image allocation failed"); }
    backendState = new (ps2::heap::Alloc(sizeof(backendState_t), TAG_RENDER)) backendState_t{texture, static_cast<int>(layout.bytes)};
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
    if (!SupportedSampler(this, tf, tr)) { ps2::render::Unavailable("idImage::SetSamplerState outside the console profile"); }
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
    if (!ps2::rs::IsInitialized() || !ps2::tex::IsInitialized() || ps2::rs::FrameStarted())
    {
        ps2::render::Unavailable("idImageManager::Init without an idle console device");
    }
    auto * const atlas = ps2::heap::TryAlloc(ps2::render::kConsoleAtlasBytes, TAG_RENDER);
    if (atlas == nullptr) { return; }
    if (!ps2::render::MakeConsoleCharset(atlas, ps2::render::kConsoleAtlasBytes))
    {
        ps2::FatalError("authored console atlas generation failed");
    }
    static constexpr byte kWhite[4] = {255, 255, 255, 255};
    const ps2::tex::Sampler whiteSampler = {ps2::tex::Filter::Nearest, ps2::tex::Wrap::Repeat, ps2::tex::Wrap::Repeat};
    const ps2::tex::Sampler charsetSampler = {};
    const auto whiteTexture = ps2::tex::Create(1, 1, whiteSampler, kWhite, sizeof(kWhite), sizeof(kWhite));
    const auto charsetTexture = ps2::tex::Create(ps2::render::kConsoleAtlasSize, ps2::render::kConsoleAtlasSize,
                                               charsetSampler, atlas, ps2::render::kConsoleAtlasBytes,
                                               static_cast<size_t>(ps2::render::kConsoleAtlasSize) * 4U);
    ps2::heap::Free(atlas);
    auto * const whiteStorage = ps2::heap::TryAlloc(sizeof(idImage::backendState_t), TAG_RENDER);
    auto * const charsetStorage = ps2::heap::TryAlloc(sizeof(idImage::backendState_t), TAG_RENDER);
    if (whiteTexture.id == 0 || charsetTexture.id == 0 || whiteStorage == nullptr || charsetStorage == nullptr)
    {
        ps2::tex::Destroy(charsetTexture);
        ps2::tex::Destroy(whiteTexture);
        ps2::heap::Free(charsetStorage);
        ps2::heap::Free(whiteStorage);
        return;
    }
    whiteImage = new (TAG_IMAGE) idImage(kWhiteName);
    s_charsetImage = new (TAG_IMAGE) idImage(kCharsetName);
    ps2::tex::Layout whiteLayout = {};
    if (!ps2::tex::MakeLayout(1, 1, whiteSampler, whiteLayout)) { ps2::FatalError("invalid white intrinsic layout"); }
    whiteImage->backendState = new (whiteStorage) idImage::backendState_t{whiteTexture, static_cast<int>(whiteLayout.bytes)};
    s_charsetImage->backendState = new (charsetStorage) idImage::backendState_t{charsetTexture, static_cast<int>(ps2::render::kConsoleAtlasBytes)};
    whiteImage->opts.width = whiteImage->opts.height = 1;
    s_charsetImage->opts.width = s_charsetImage->opts.height = ps2::render::kConsoleAtlasSize;
    idImage * const intrinsicImages[] = {whiteImage, s_charsetImage};
    for (idImage * image : intrinsicImages)
    {
        image->opts.format = FMT_RGBA8;
        image->opts.numLevels = 1;
        image->filter = TF_NEAREST;
        image->repeat = image == whiteImage ? TR_REPEAT : TR_CLAMP;
        image->usage = image == whiteImage ? TD_DEFAULT : TD_FONT;
        image->referencedOutsideLevelLoad = true;
    }
    images.SetGranularity(2);
    images.Append(whiteImage);
    images.Append(s_charsetImage);
    s_initialized = true;
}

PS2_COLD_FUNC void idImageManager::Shutdown()
{
    if (!s_initialized) { return; }
    if (this != globalImages || ps2::rs::FrameStarted()) { ps2::FatalError("console image shutdown requires idle ownership"); }
    for (int i = 0; i < images.Num(); ++i) { images[i]->PurgeImage(); }
    images.DeleteContents(true);
    imageHash.Clear();
    whiteImage = nullptr;
    s_charsetImage = nullptr;
    s_initialized = false;
}

idImage * idImageManager::GetImage(const char * name) const
{
    if (!s_initialized || this != globalImages) { ps2::render::Unavailable("idImageManager::GetImage outside the console profile"); }
    if (IsWhiteName(name)) { return whiteImage; }
    if (IsCharsetName(name)) { return s_charsetImage; }
    return nullptr;
}

idImage * idImageManager::GetImageWithParameters(const char * name, textureFilter_t tf, textureRepeat_t tr,
                                               textureUsage_t tu, cubeFiles_t cube) const
{
    idImage * const image = GetImage(name);
    if (image == nullptr || !SupportedSampler(image, tf, tr) || cube != CF_2D ||
        (tu != TD_DEFAULT && (image == whiteImage || tu != TD_FONT))) { return nullptr; }
    return image;
}

idImage * idImageManager::ImageFromFile(const char * name, textureFilter_t tf, textureRepeat_t tr,
                                      textureUsage_t tu, cubeFiles_t cube)
{
    if (!s_initialized || this != globalImages)
    {
        ps2::render::Unavailable("idImageManager::ImageFromFile");
    }
    idImage * const image = GetImageWithParameters(name, tf, tr, tu, cube);
    if (image == nullptr) { ps2::render::Unavailable("idImageManager::ImageFromFile outside the console profile"); }
    if (!image->IsLoaded()) { ps2::render::Unavailable("idImageManager::ImageFromFile with purged intrinsic"); }
    image->AddReference();
    return image;
}
