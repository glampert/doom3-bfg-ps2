// ================================================================================================
// File: image_stubs.cpp
// Brief: Keep image/shader/cinematic interfaces explicit without claiming loaded render resources.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/null_render.h"
#include <idlib/precompiled.h>
#include <renderer/ResolutionScale.h>
#include <renderer/tr_local.h>

namespace
{
static idImageManager s_imageManager;
} // namespace

idImageManager * globalImages = &s_imageManager;
idRenderProgManager renderProgManager;
idResolutionScale resolutionScale;

bool idImage::IsLoaded() const
{
    if (backendState != nullptr)
    {
        ps2::render::Unavailable("idImage::IsLoaded with backend state");
    }
    return false;
}
void idImage::PurgeImage()
{
    if (backendState != nullptr)
    {
        ps2::render::Unavailable("idImage::PurgeImage with backend state");
    }
}
int idImage::StorageSize() const
{
    if (backendState != nullptr)
    {
        ps2::render::Unavailable("idImage::StorageSize with backend state");
    }
    return 0; // An image declaration has no allocated texture storage.
}

idRenderProgManager::idRenderProgManager()
    : backendState(nullptr)
{
    for (int & shader : builtinShaders)
    {
        shader = -1;
    }
}
idRenderProgManager::~idRenderProgManager()
{
    if (backendState != nullptr)
    {
        ps2::render::Unavailable("idRenderProgManager::~idRenderProgManager with backend state");
    }
}

// This policy stays at full resolution without a device/timing source. It allocates no render target.
idResolutionScale::idResolutionScale()
    : dropMilliseconds(0.0f), raiseMilliseconds(0.0f), framesAboveRaise(0), currentResolution(1.0f) {}
void idResolutionScale::ResetToFullResolution() { currentResolution = 1.0f; }
void idResolutionScale::GetCurrentResolutionScale(float & x, float & y) { x = y = currentResolution; }
void idResolutionScale::GetConsoleText(idStr & text) { text = "rendering unavailable"; }
void idResolutionScale::SetCurrentGPUFrameTime(int microseconds)
{
    if (microseconds != 0)
    {
        ps2::render::Unavailable("idResolutionScale::SetCurrentGPUFrameTime");
    }
}

#define RENDER_UNAVAILABLE(result, method, parameters) \
    PS2_COLD_FUNC result method parameters { ps2::render::Unavailable(#method); }

RENDER_UNAVAILABLE(void, idImage::Reload, (bool))
RENDER_UNAVAILABLE(void, idImage::ActuallyLoadImage, (bool))
RENDER_UNAVAILABLE(void, idImage::Bind, ())
RENDER_UNAVAILABLE(void, idImage::AllocImage, (const idImageOpts &, textureFilter_t, textureRepeat_t))
RENDER_UNAVAILABLE(void, idImage::GenerateImage, (const byte *, int, int, textureFilter_t, textureRepeat_t, textureUsage_t))
RENDER_UNAVAILABLE(void, idImage::GenerateCubeImage, (const byte * [6], int, textureFilter_t, textureUsage_t))
RENDER_UNAVAILABLE(void, idImage::MakeDefault, ())
RENDER_UNAVAILABLE(void, idImage::SetSamplerState, (textureFilter_t, textureRepeat_t))
RENDER_UNAVAILABLE(void, idImage::CopyFramebuffer, (int, int, int, int))
RENDER_UNAVAILABLE(void, idImage::CopyDepthbuffer, (int, int, int, int))
RENDER_UNAVAILABLE(void, idImage::UploadScratch, (const byte *, int, int))
RENDER_UNAVAILABLE(void, idImage::SubImageUpload, (int, int, int, int, int, int, const void *, int) const)
RENDER_UNAVAILABLE(void, idImage::SetPixel, (int, int, int, const void *, int))
RENDER_UNAVAILABLE(void, idImage::Resize, (int, int))
RENDER_UNAVAILABLE(void, idImage::SetTexParameters, ())
RENDER_UNAVAILABLE(void, idImage::Print, () const)

RENDER_UNAVAILABLE(void, idImageManager::Init, ())
RENDER_UNAVAILABLE(void, idImageManager::Shutdown, ())
RENDER_UNAVAILABLE(void, idImageManager::StartBuild, ())
RENDER_UNAVAILABLE(void, idImageManager::FinishBuild, (bool))
RENDER_UNAVAILABLE(idImage *, idImageManager::ImageFromFile, (const char *, textureFilter_t, textureRepeat_t, textureUsage_t, cubeFiles_t))
RENDER_UNAVAILABLE(idImage *, idImageManager::ImageFromFunction, (const char *, void (*)(idImage *)))
RENDER_UNAVAILABLE(idImage *, idImageManager::GetImage, (const char *) const)
RENDER_UNAVAILABLE(idImage *, idImageManager::GetImageWithParameters, (const char *, textureFilter_t, textureRepeat_t, textureUsage_t, cubeFiles_t) const)
RENDER_UNAVAILABLE(idImage *, idImageManager::ScratchImage, (const char *, idImageOpts *, textureFilter_t, textureRepeat_t, textureUsage_t))
RENDER_UNAVAILABLE(idImage *, idImageManager::AllocImage, (const char *))
RENDER_UNAVAILABLE(idImage *, idImageManager::AllocStandaloneImage, (const char *))
RENDER_UNAVAILABLE(bool, idImageManager::ExcludePreloadImage, (const char *))
RENDER_UNAVAILABLE(void, idImageManager::PurgeAllImages, ())
RENDER_UNAVAILABLE(void, idImageManager::ReloadImages, (bool))
RENDER_UNAVAILABLE(void, idImageManager::UnbindAll, ())
RENDER_UNAVAILABLE(void, idImageManager::BindNull, ())
RENDER_UNAVAILABLE(void, idImageManager::BeginLevelLoad, ())
RENDER_UNAVAILABLE(void, idImageManager::EndLevelLoad, ())
RENDER_UNAVAILABLE(void, idImageManager::Preload, (const idPreloadManifest &, const bool &))
RENDER_UNAVAILABLE(int, idImageManager::LoadLevelImages, (bool))
RENDER_UNAVAILABLE(void, idImageManager::PrintMemInfo, (MemInfo_t *))
RENDER_UNAVAILABLE(void, idImageManager::CreateIntrinsicImages, ())

RENDER_UNAVAILABLE(void, idRenderProgManager::Init, ())
RENDER_UNAVAILABLE(void, idRenderProgManager::Shutdown, ())
RENDER_UNAVAILABLE(int, idRenderProgManager::FindVertexShader, (const char *))
RENDER_UNAVAILABLE(int, idRenderProgManager::FindFragmentShader, (const char *))
RENDER_UNAVAILABLE(int, idRenderProgManager::FindGLSLProgram, (const char *, int, int))
RENDER_UNAVAILABLE(void, idRenderProgManager::BindShader, (int, int))
RENDER_UNAVAILABLE(void, idRenderProgManager::Unbind, ())
RENDER_UNAVAILABLE(void, idRenderProgManager::SetRenderParm, (renderParm_t, const float *))
RENDER_UNAVAILABLE(void, idRenderProgManager::SetRenderParms, (renderParm_t, const float *, int))
RENDER_UNAVAILABLE(bool, idRenderProgManager::ShaderUsesJoints, () const)
RENDER_UNAVAILABLE(bool, idRenderProgManager::ShaderHasOptionalSkinning, () const)
RENDER_UNAVAILABLE(void, idRenderProgManager::LoadAllShaders, ())
RENDER_UNAVAILABLE(void, idRenderProgManager::KillAllShaders, ())
RENDER_UNAVAILABLE(const char *, idRenderProgManager::GetGLSLParmName, (int) const)
RENDER_UNAVAILABLE(void, idRenderProgManager::SetUniformValue, (renderParm_t, const float *))
RENDER_UNAVAILABLE(void, idRenderProgManager::CommitUniforms, ())
RENDER_UNAVAILABLE(void, idRenderProgManager::ZeroUniforms, ())
RENDER_UNAVAILABLE(void, idResolutionScale::InitForMap, (const char *))

RENDER_UNAVAILABLE(void, idCinematic::InitCinematic, ())
RENDER_UNAVAILABLE(void, idCinematic::ShutdownCinematic, ())
RENDER_UNAVAILABLE(idCinematic *, idCinematic::Alloc, ())
idCinematic::~idCinematic() = default;
RENDER_UNAVAILABLE(bool, idCinematic::InitFromFile, (const char *, bool))
RENDER_UNAVAILABLE(int, idCinematic::AnimationLength, ())
RENDER_UNAVAILABLE(cinData_t, idCinematic::ImageForTime, (int))
RENDER_UNAVAILABLE(void, idCinematic::Close, ())
RENDER_UNAVAILABLE(void, idCinematic::ResetTime, (int))
RENDER_UNAVAILABLE(int, idCinematic::GetStartTime, ())
RENDER_UNAVAILABLE(void, idCinematic::ExportToTGA, (bool))
RENDER_UNAVAILABLE(float, idCinematic::GetFrameRate, () const)
RENDER_UNAVAILABLE(bool, idSndWindow::InitFromFile, (const char *, bool))
RENDER_UNAVAILABLE(cinData_t, idSndWindow::ImageForTime, (int))
RENDER_UNAVAILABLE(int, idSndWindow::AnimationLength, ())

#undef RENDER_UNAVAILABLE
