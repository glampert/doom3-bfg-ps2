// ================================================================================================
// File: logical_renderer.cpp
// Brief: Bind id's tr/renderSystem, program manager, resolution scale and vertex cache: CPU models and worlds; the rest fails by name.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/logical_renderer.h"
#include "ps2/system/log.h"
#include "ps2/ui/draw.h"
#include <idlib/precompiled.h>
#include <renderer/ResolutionScale.h>
#include <renderer/tr_local.h>

// Native world declarations depend on frontend types from tr_local.h.
#include <renderer/RenderWorld_local.h>

namespace ps2::renderer
{

[[noreturn]] PS2_COLD_FUNC void Unavailable(const char * method)
{
    FatalError("renderer capability unavailable: %s", method);
}

} // namespace ps2::renderer

// Keep the native frontend storage and interface identity; no frame arenas or device are created.
idRenderSystemLocal tr;
idRenderSystem * renderSystem = &tr;
backEndState_t backEnd{};
glconfig_t glConfig{};
idGuiModel * tr_guiModel = nullptr;

namespace
{

static bool s_logicalInitialized = false;

void CheckNoResources(const idRenderSystemLocal & renderer, const char * method)
{
    if (s_logicalInitialized || renderer.registered || renderer.worlds.Num() != 0 || renderer.fonts.Num() != 0 || renderer.guiModel != nullptr ||
        renderer.unitSquareTriangles != nullptr || renderer.zeroOneCubeTriangles != nullptr ||
        renderer.testImageTriangles != nullptr || renderer.testVideo != nullptr) [[unlikely]]
    {
        ps2::renderer::Unavailable(method);
    }
}

void CheckDemoInactive(const char * method)
{
    PS2_Assert(common != nullptr);
    if (common == nullptr || common->WriteDemo() != nullptr) [[unlikely]]
    {
        ps2::renderer::Unavailable(method);
    }
}

} // namespace

namespace ps2::renderer
{

bool IsLogicalInitialized()
{
    return s_logicalInitialized;
}

void AdvanceLogicalFrame()
{
    if (!s_logicalInitialized || tr.frameCount == INT_MAX) [[unlikely]]
    {
        Unavailable("invalid logical renderer frame");
    }
    ++tr.frameCount;
}

#if defined(PS2_D3BFG_FOUNDATION)

PS2_COLD_FUNC void InitLogical(const char *)
{
    Unavailable("logical renderer requires the resident campaign target");
}

PS2_COLD_FUNC void ShutdownLogical()
{
    Unavailable("logical renderer requires the resident campaign target");
}

#else // PS2_D3BFG_FOUNDATION

PS2_COLD_FUNC void InitLogical(const char * defaultMaterialName)
{
    PS2_Assert(defaultMaterialName != nullptr);
    if (s_logicalInitialized || !common->IsInitialized() || defaultMaterialName == nullptr || defaultMaterialName[0] == '\0') [[unlikely]]
    {
        Unavailable("invalid logical renderer startup");
    }

    CheckNoResources(tr, "logical renderer startup with live resources");
    tr.defaultMaterial = declManager->FindMaterial(defaultMaterialName, false);
    if (tr.defaultMaterial == nullptr || tr.defaultMaterial->GetNumStages() != 0) [[unlikely]]
    {
        Unavailable("logical renderer requires a stage-free default material");
    }

    renderModelManager->Init();

    // id's Init sets the identity space; the portal flow projects each portal's winding through it
    // for the screen rectangle that scissors the areas behind it (an all-zero matrix maps every
    // corner to the world origin).
    tr.identitySpace.modelMatrix[0 * 4 + 0] = 1.0f;
    tr.identitySpace.modelMatrix[1 * 4 + 1] = 1.0f;
    tr.identitySpace.modelMatrix[2 * 4 + 2] = 1.0f;
    s_logicalInitialized = true;

    // Native model queries cache by renderer frame; zero is the uninitialized definition stamp.
    AdvanceLogicalFrame();
}

PS2_COLD_FUNC void ShutdownLogical()
{
    if (!s_logicalInitialized || tr.worlds.Num() != 0) [[unlikely]]
    {
        Unavailable("logical renderer shutdown with live worlds");
    }

    renderModelManager->Shutdown();
    cmdSystem->RemoveFlaggedCommands(CMD_FL_RENDERER);
    tr.worlds.Clear();
    tr.defaultMaterial = nullptr;
    s_logicalInitialized = false;
}

#endif // PS2_D3BFG_FOUNDATION

} // namespace ps2::renderer

idRenderSystemLocal::idRenderSystemLocal()
    : registered(false), takingScreenshot(false), frameCount(0), viewCount(0), frameShaderTime(0.0f), ambientLightVector(0.0f, 0.0f, 0.0f, 0.0f), worlds(), primaryWorld(nullptr), primaryRenderView{}, primaryView(nullptr), whiteMaterial(nullptr), charSetMaterial(nullptr), defaultPointLight(nullptr), defaultProjectedLight(nullptr), defaultMaterial(nullptr), testImage(nullptr), testVideo(nullptr), testVideoStartTime(0), ambientCubeImage(nullptr), viewDef(nullptr), pc{}, identitySpace{}, renderCrops{}, currentRenderCrop(0), guiRecursionLevel(0), currentColorNativeBytesOrder(0xFFFFFFFFU), currentGLState(0), guiModel(nullptr), fonts(), gammaTable{}, unitSquareTriangles(nullptr), zeroOneCubeTriangles(nullptr), testImageTriangles(nullptr), unitSquareSurface_{}, zeroOneCubeSurface_{}, testImageSurface_{}, frontEndJobList(nullptr), timerQueryId(0)
{
    Clear();
}

idRenderSystemLocal::~idRenderSystemLocal()
{
    CheckNoResources(*this, "idRenderSystemLocal::~idRenderSystemLocal");
}

void idRenderSystemLocal::Clear()
{
    // Clear only inactive native value state; logical ownership must be shut down first.
    CheckNoResources(*this, "idRenderSystemLocal::Clear with live resources");
    registered = takingScreenshot = false;
    frameCount = viewCount = testVideoStartTime = currentRenderCrop = guiRecursionLevel = 0;
    frameShaderTime = 0.0f;
    ambientLightVector.Zero();
    worlds.Clear();
    fonts.Clear();
    primaryWorld = nullptr;
    primaryView = viewDef = nullptr;
    whiteMaterial = charSetMaterial = defaultPointLight = defaultProjectedLight = defaultMaterial = nullptr;
    testImage = ambientCubeImage = nullptr;
    testVideo = nullptr;
    guiModel = nullptr;
    frontEndJobList = nullptr;
    currentColorNativeBytesOrder = 0xFFFFFFFFU;
    currentGLState = 0;
    timerQueryId = 0;
    memset(static_cast<void *>(&primaryRenderView), 0, sizeof(primaryRenderView));
    memset(static_cast<void *>(&identitySpace), 0, sizeof(identitySpace));
    memset(static_cast<void *>(renderCrops), 0, sizeof(renderCrops));
    memset(static_cast<void *>(&pc), 0, sizeof(pc));
    memset(static_cast<void *>(gammaTable), 0, sizeof(gammaTable));
    memset(static_cast<void *>(&unitSquareSurface_), 0, sizeof(unitSquareSurface_));
    memset(static_cast<void *>(&zeroOneCubeSurface_), 0, sizeof(zeroOneCubeSurface_));
    memset(static_cast<void *>(&testImageSurface_), 0, sizeof(testImageSurface_));
}

// These queries describe an inactive backend, not successful logical initialization or a display mode.
bool R_IsInitialized()
{
    return ps2::ui::IsInitialized();
}

bool idRenderSystemLocal::IsOpenGLRunning() const
{
    return false;
}

bool idRenderSystemLocal::IsFullScreen() const
{
    return this == &tr && ps2::ui::IsInitialized();
}

bool idRenderSystemLocal::HasQuadBufferSupport() const
{
    return false;
}

bool idRenderSystemLocal::IsStereoScopicRenderingSupported() const
{
    return false;
}

stereo3DMode_t idRenderSystemLocal::GetStereo3DMode() const
{
    return STEREO3D_OFF;
}

stereo3DMode_t idRenderSystemLocal::GetStereoScopicRenderingMode() const
{
    return STEREO3D_OFF;
}

bool idRenderSystemLocal::AreAutomaticBackgroundSwapsRunning(autoRenderIconType_t *) const
{
    return false;
}

PS2_COLD_FUNC void idRenderSystemLocal::Shutdown()
{
    if (this == &tr)
    {
        ps2::ui::Shutdown();
    }
    Clear();
}

PS2_COLD_FUNC void idRenderSystemLocal::ShutdownOpenGL()
{
    CheckNoResources(*this, "idRenderSystemLocal::ShutdownOpenGL");
}

void idRenderSystemLocal::EndAutomaticBackgroundSwaps()
{
}

// Views render at the full frame-buffer size: no resolution scaling, and the only crop is the
// whole screen (RenderScene's own). Render-to-texture crops come with their PS2 passes.
void idRenderSystemLocal::PerformResolutionScaling(int &, int &)
{
}

void idRenderSystemLocal::CropRenderSize(int width, int height)
{
    PS2_AssertMsg(width == GetWidth() && height == GetHeight(), "render crops other than the full screen are unavailable");
}

void idRenderSystemLocal::UnCrop()
{
}

void idRenderSystemLocal::GetCroppedViewport(idScreenRect * viewport)
{
    viewport->x1 = 0;
    viewport->y1 = 0;
    viewport->x2 = static_cast<short>(GetWidth() - 1);
    viewport->y2 = static_cast<short>(GetHeight() - 1);
    viewport->zmin = 0.0f;
    viewport->zmax = 1.0f;
}

#define RENDER_UNAVAILABLE(result, method, parameters) \
    PS2_COLD_FUNC result method parameters             \
    {                                                  \
        ps2::renderer::Unavailable(#method);           \
    }

RENDER_UNAVAILABLE(void, idRenderSystemLocal::Init, ())
RENDER_UNAVAILABLE(void, idRenderSystemLocal::InitOpenGL, ())
RENDER_UNAVAILABLE(void, idRenderSystemLocal::ResetGuiModels, ())
RENDER_UNAVAILABLE(float, idRenderSystemLocal::GetPhysicalScreenWidthInCentimeters, () const)
RENDER_UNAVAILABLE(void, idRenderSystemLocal::EnableStereoScopicRendering, (stereo3DMode_t) const)
RENDER_UNAVAILABLE(void, idRenderSystemLocal::BeginLevelLoad, ())
RENDER_UNAVAILABLE(void, idRenderSystemLocal::EndLevelLoad, ())
RENDER_UNAVAILABLE(void, idRenderSystemLocal::LoadLevelImages, ())
RENDER_UNAVAILABLE(void, idRenderSystemLocal::Preload, (const idPreloadManifest &, const char *))
RENDER_UNAVAILABLE(void, idRenderSystemLocal::BeginAutomaticBackgroundSwaps, (autoRenderIconType_t))
RENDER_UNAVAILABLE(void, idRenderSystemLocal::PrintMemInfo, (MemInfo_t *))
RENDER_UNAVAILABLE(void, idRenderSystemLocal::WriteDemoPics, ())
RENDER_UNAVAILABLE(void, idRenderSystemLocal::DrawDemoPics, ())
RENDER_UNAVAILABLE(const emptyCommand_t *, idRenderSystemLocal::SwapCommandBuffers, (uint64 *, uint64 *, uint64 *, uint64 *))
RENDER_UNAVAILABLE(void, idRenderSystemLocal::SwapCommandBuffers_FinishRendering, (uint64 *, uint64 *, uint64 *, uint64 *))
RENDER_UNAVAILABLE(const emptyCommand_t *, idRenderSystemLocal::SwapCommandBuffers_FinishCommandBuffers, ())
RENDER_UNAVAILABLE(void, idRenderSystemLocal::RenderCommandBuffers, (const emptyCommand_t *))
RENDER_UNAVAILABLE(void, idRenderSystemLocal::TakeScreenshot, (int, int, const char *, int, renderView_t *))
RENDER_UNAVAILABLE(void, idRenderSystemLocal::CaptureRenderToImage, (const char *, bool))
RENDER_UNAVAILABLE(void, idRenderSystemLocal::CaptureRenderToFile, (const char *, bool))
RENDER_UNAVAILABLE(bool, idRenderSystemLocal::UploadImage, (const char *, const byte *, int, int))

#if defined(PS2_D3BFG_FOUNDATION)
RENDER_UNAVAILABLE(idFont *, idRenderSystemLocal::RegisterFont, (const char *))
RENDER_UNAVAILABLE(void, idRenderSystemLocal::ResetFonts, ())
#endif // PS2_D3BFG_FOUNDATION

RENDER_UNAVAILABLE(void, R_AddDrawViewCmd, (viewDef_t *, bool))
RENDER_UNAVAILABLE(void, R_AddDrawPostProcess, (viewDef_t *))
RENDER_UNAVAILABLE(bool, R_GetModeListForDisplay, (int, idList<vidMode_t> &))
RENDER_UNAVAILABLE(void, RB_AddDebugLine, (const idVec4 &, const idVec3 &, const idVec3 &, int, bool))
RENDER_UNAVAILABLE(void, RB_AddDebugText, (const char *, const idVec3 &, float, const idVec4 &, const idMat3 &, int, int, bool))
RENDER_UNAVAILABLE(void, RB_AddDebugPolygon, (const idVec4 &, const idWinding &, int, bool))
RENDER_UNAVAILABLE(float, RB_DrawTextLength, (const char *, float, int))

void RB_ClearDebugText(int)
{
}

void RB_ClearDebugLines(int)
{
}

void RB_ClearDebugPolygons(int)
{
}

// Native world mutation can request demo writes while no recorder exists. Preserve that idle path.
void idRenderWorldLocal::WriteLoadMap()
{
    CheckDemoInactive("idRenderWorldLocal::WriteLoadMap");
}

void idRenderWorldLocal::WriteFreeEntity(int)
{
    CheckDemoInactive("idRenderWorldLocal::WriteFreeEntity");
}

void idRenderWorldLocal::WriteFreeLight(int)
{
    CheckDemoInactive("idRenderWorldLocal::WriteFreeLight");
}

void idRenderWorldLocal::WriteRenderView(const renderView_t *)
{
    CheckDemoInactive("idRenderWorldLocal::WriteRenderView");
}

void idRenderWorldLocal::WriteVisibleDefs(const viewDef_t *)
{
    CheckDemoInactive("idRenderWorldLocal::WriteVisibleDefs");
}

void idRenderWorldLocal::StopWritingDemo()
{
    CheckDemoInactive("idRenderWorldLocal::StopWritingDemo");
}

RENDER_UNAVAILABLE(void, idRenderWorldLocal::StartWritingDemo, (idDemoFile *))
RENDER_UNAVAILABLE(bool, idRenderWorldLocal::ProcessDemoCommand, (idDemoFile *, renderView_t *, int *))

#undef RENDER_UNAVAILABLE

#if defined(PS2_D3BFG_FOUNDATION)

idRenderWorld * idRenderSystemLocal::AllocRenderWorld()
{
    ps2::renderer::Unavailable("idRenderSystemLocal::AllocRenderWorld");
}

PS2_COLD_FUNC void idRenderSystemLocal::FreeRenderWorld(idRenderWorld *)
{
    ps2::renderer::Unavailable("idRenderSystemLocal::FreeRenderWorld");
}

#else // PS2_D3BFG_FOUNDATION

idRenderWorld * idRenderSystemLocal::AllocRenderWorld()
{
    if (!ps2::renderer::IsLogicalInitialized()) [[unlikely]]
    {
        ps2::renderer::Unavailable("idRenderSystemLocal::AllocRenderWorld");
    }
    auto * world = new (TAG_RENDER) idRenderWorldLocal;
    worlds.Append(world);
    return world;
}

PS2_COLD_FUNC void idRenderSystemLocal::FreeRenderWorld(idRenderWorld * world)
{
    auto * local = static_cast<idRenderWorldLocal *>(world);
    if (!ps2::renderer::IsLogicalInitialized() || local == nullptr || !worlds.Remove(local)) [[unlikely]]
    {
        ps2::renderer::Unavailable("idRenderSystemLocal::FreeRenderWorld");
    }
    if (primaryWorld == local)
    {
        primaryWorld = nullptr;
    }
    delete local;
}

#endif // PS2_D3BFG_FOUNDATION

// ------------------------------------------------------------------------------------------------
// Render programs and resolution scaling
// ------------------------------------------------------------------------------------------------

// id's GLSL program manager has no programs here (the VU1 programs are ps2::renderer::vu's); a
// material that looks one up fails by name.
idRenderProgManager renderProgManager;
idResolutionScale resolutionScale;

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
    if (backendState != nullptr) [[unlikely]]
    {
        ps2::FatalError("idRenderProgManager::~idRenderProgManager with backend state");
    }
}

// Always full resolution: there is no GPU timer to drive the scale.
idResolutionScale::idResolutionScale()
    : dropMilliseconds(0.0f), raiseMilliseconds(0.0f), framesAboveRaise(0), currentResolution(1.0f)
{
}

void idResolutionScale::ResetToFullResolution()
{
    currentResolution = 1.0f;
}

void idResolutionScale::GetCurrentResolutionScale(float & x, float & y)
{
    x = y = currentResolution;
}

void idResolutionScale::GetConsoleText(idStr & text)
{
    text = "rendering unavailable";
}

void idResolutionScale::SetCurrentGPUFrameTime(int microseconds)
{
    if (microseconds != 0) [[unlikely]]
    {
        ps2::FatalError("idResolutionScale::SetCurrentGPUFrameTime");
    }
}

// ------------------------------------------------------------------------------------------------
// Vertex cache
// ------------------------------------------------------------------------------------------------

// id's GPU buffers have no storage on the PS2 (world geometry is cooked into VU1 batches): the
// cache and its buffers construct empty, clean up empty, and any allocation fails by name.
idVertexCache vertexCache{};

// Empty construction must work before main without allocating desktop buffer pools.
idVertexBuffer::idVertexBuffer()
    : size(0), offsetInOtherBuffer(0), apiObject(nullptr)
{
}

idIndexBuffer::idIndexBuffer()
    : size(0), offsetInOtherBuffer(0), apiObject(nullptr)
{
}

idJointBuffer::idJointBuffer()
    : numJoints(0), offsetInOtherBuffer(0), apiObject(nullptr)
{
}

idVertexBuffer::~idVertexBuffer()
{
    FreeBufferObject();
}

idIndexBuffer::~idIndexBuffer()
{
    FreeBufferObject();
}

idJointBuffer::~idJointBuffer()
{
    FreeBufferObject();
}

PS2_COLD_FUNC void idVertexBuffer::FreeBufferObject()
{
    if (apiObject != nullptr || size != 0 || offsetInOtherBuffer != 0) [[unlikely]]
    {
        ps2::renderer::Unavailable("idVertexBuffer::FreeBufferObject with backend state");
    }
}

PS2_COLD_FUNC void idIndexBuffer::FreeBufferObject()
{
    if (apiObject != nullptr || size != 0 || offsetInOtherBuffer != 0) [[unlikely]]
    {
        ps2::renderer::Unavailable("idIndexBuffer::FreeBufferObject with backend state");
    }
}

PS2_COLD_FUNC void idJointBuffer::FreeBufferObject()
{
    if (apiObject != nullptr || numJoints != 0 || offsetInOtherBuffer != 0) [[unlikely]]
    {
        ps2::renderer::Unavailable("idJointBuffer::FreeBufferObject with backend state");
    }
}

static PS2_COLD_FUNC void ClearEmptySet(geoBufferSet_t & buffers)
{
    buffers.vertexBuffer.FreeBufferObject();
    buffers.indexBuffer.FreeBufferObject();
    buffers.jointBuffer.FreeBufferObject();

    if (buffers.mappedVertexBase != nullptr || buffers.mappedIndexBase != nullptr || buffers.mappedJointBase != nullptr ||
        buffers.vertexMemUsed.GetValue() != 0 || buffers.indexMemUsed.GetValue() != 0 || buffers.jointMemUsed.GetValue() != 0 ||
        buffers.allocations != 0) [[unlikely]]
    {
        ps2::renderer::Unavailable("idVertexCache empty cleanup with live storage");
    }
}

PS2_COLD_FUNC void idVertexCache::FreeStaticData()
{
    ClearEmptySet(staticData);
    mostUsedVertex = mostUsedIndex = mostUsedJoint = 0;
}

PS2_COLD_FUNC void idVertexCache::Shutdown()
{
    FreeStaticData();

    for (geoBufferSet_t & buffers : frameData)
    {
        ClearEmptySet(buffers);
    }
}

#define RENDER_UNAVAILABLE(result, method, parameters) \
    PS2_COLD_FUNC result method parameters             \
    {                                                  \
        ps2::renderer::Unavailable(#method);           \
    }

RENDER_UNAVAILABLE(int, idRenderProgManager::FindVertexShader, (const char *))
RENDER_UNAVAILABLE(int, idRenderProgManager::FindFragmentShader, (const char *))
RENDER_UNAVAILABLE(int, idRenderProgManager::FindGLSLProgram, (const char *, int, int))
RENDER_UNAVAILABLE(vertCacheHandle_t, idVertexCache::ActuallyAlloc, (geoBufferSet_t &, const void *, int, cacheType_t))

#undef RENDER_UNAVAILABLE
