// ================================================================================================
// File: renderer_tests.cpp
// Brief: Ensure renderer stubs expose inactive state, recover metadata storage and fail required work.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/renderer_tests.h"
#include "ps2/system/heap.h"
#include "ps2/system/log.h"
#include <idlib/precompiled.h>
#include <renderer/ResolutionScale.h>
#include <renderer/tr_local.h>

namespace ps2::smoketests
{
namespace
{
bool Check(const char * name, bool passed)
{
    Log(LogLevel::Info, "[D3BFG] CHECK renderer/%s %s\n", name, passed ? "PASS" : "FAIL");
    return passed;
}
} // namespace

bool RunRendererTests()
{
    autoRenderIconType_t icon = AUTORENDER_HELLICON;
    bool passed = Check("inactive-interfaces", renderSystem == &tr && globalImages != nullptr && !R_IsInitialized() &&
                                               !tr.registered && !renderSystem->IsOpenGLRunning() && !renderSystem->IsFullScreen() &&
                                               !renderSystem->HasQuadBufferSupport() && !renderSystem->IsStereoScopicRenderingSupported() &&
                                               renderSystem->GetStereo3DMode() == STEREO3D_OFF && renderSystem->GetStereoScopicRenderingMode() == STEREO3D_OFF &&
                                               !renderSystem->AreAutomaticBackgroundSwapsRunning(&icon) && icon == AUTORENDER_HELLICON &&
                                               renderSystem->GetFrameCount() == 0 && tr.worlds.Num() == 0 && tr.fonts.Num() == 0 && tr.guiModel == nullptr &&
                                               !glConfig.vertexBufferObjectAvailable && !glConfig.uniformBufferAvailable &&
                                               glConfig.nativeScreenWidth == 0 && glConfig.nativeScreenHeight == 0 && globalImages->images.Num() == 0 &&
                                               globalImages->defaultImage == nullptr && globalImages->currentRenderImage == nullptr);

    const heap::Stats before = heap::GetTotalStats();
    bool metadata = true;
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        {
            idImage image("fixture/render/metadata-with-long-name-and-no-allocated-texture-payload");
            image.PurgeImage();
            metadata = metadata && !image.IsLoaded() && image.StorageSize() == 0 && image.GetUploadWidth() == 0 &&
                       image.GetUploadHeight() == 0 && idStr::Cmp(image.GetName(), "fixture/render/metadata-with-long-name-and-no-allocated-texture-payload") == 0;
            idVertexBuffer vertices;
            idIndexBuffer indices;
            idJointBuffer joints;
            vertices.FreeBufferObject();
            indices.FreeBufferObject();
            joints.FreeBufferObject();
            metadata = metadata && vertices.GetSize() == 0 && indices.GetSize() == 0 && joints.GetNumJoints() == 0 &&
                       vertices.GetAPIObject() == nullptr && indices.GetAPIObject() == nullptr && joints.GetAPIObject() == nullptr &&
                       !vertices.IsMapped() && !indices.IsMapped() && !joints.IsMapped();
            idRenderSystemLocal inactive;
            inactive.Clear();
            inactive.Shutdown();
            metadata = metadata && !inactive.registered && inactive.frameCount == 0 && inactive.worlds.Num() == 0;
        }
        vertexCache.FreeStaticData();
        vertexCache.Shutdown();
        const heap::Stats after = heap::GetTotalStats();
        metadata = metadata && after.requestedBytes == before.requestedBytes && after.backingBytes == before.backingBytes &&
                   after.allocationCount == before.allocationCount;
    }
    passed = Check("empty-resource-ledger", metadata) && passed;

    idStr text;
    float x = 0.0f;
    float y = 0.0f;
    resolutionScale.ResetToFullResolution();
    resolutionScale.SetCurrentGPUFrameTime(0);
    resolutionScale.GetCurrentResolutionScale(x, y);
    resolutionScale.GetConsoleText(text);
    const idCVar * nearPlane = cvarSystem->Find("r_znear");
    const idCVar * dynamicModels = cvarSystem->Find("r_useCachedDynamicModels");
    passed = Check("disabled-resolution-cvars", x == 1.0f && y == 1.0f && idStr::Cmp(text, "rendering unavailable") == 0 &&
                                                nearPlane != nullptr && nearPlane->GetFloat() == 3.0f && (nearPlane->GetFlags() & (CVAR_RENDERER | CVAR_FLOAT)) == (CVAR_RENDERER | CVAR_FLOAT) &&
                                                dynamicModels != nullptr && dynamicModels->GetBool()) &&
             passed;
    return passed;
}

bool RunRendererFailureProbe(const char * name)
{
    if (idStr::Cmp(name, "renderer-init") == 0)
    {
        renderSystem->Init();
    }
    else if (idStr::Cmp(name, "renderer-width") == 0)
    {
        (void)renderSystem->GetWidth();
    }
    else if (idStr::Cmp(name, "renderer-draw") == 0)
    {
        renderSystem->DrawStretchPic(0.0f, 0.0f, 16.0f, 16.0f, 0.0f, 0.0f, 1.0f, 1.0f, nullptr);
    }
    else if (idStr::Cmp(name, "renderer-image") == 0)
    {
        (void)globalImages->ImageFromFile("fixture/render/unimplemented", TF_DEFAULT, TR_REPEAT, TD_DEFAULT);
    }
    else if (idStr::Cmp(name, "renderer-vertices") == 0)
    {
        idDrawVert vertex{};
        (void)vertexCache.AllocVertex(&vertex, static_cast<int>(sizeof(vertex)));
    }
    else if (idStr::Cmp(name, "renderer-shader") == 0)
    {
        (void)renderProgManager.FindGLSLProgram("fixture/render/unimplemented", -1, -1);
    }
    else if (idStr::Cmp(name, "renderer-cinematic") == 0)
    {
        (void)idCinematic::Alloc();
    }
    else if (idStr::Cmp(name, "renderer-demo") == 0)
    {
        renderSystem->WriteDemoPics();
    }
    else
    {
        return false;
    }
    return true; // The runner rejects an unavailable operation returning normally.
}
} // namespace ps2::smoketests
