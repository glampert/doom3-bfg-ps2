// ================================================================================================
// File: scene.cpp
// Brief: Take id's views after the portal flow and draw the world areas they reach. Each area's
// model entity lands on the view entity list only when a portal chain to it is visible, so
// drawing those areas is id's portal culling; batch culling and entities come later.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/renderer/scene.h"
#include "ps2/math/math.h"
#include "ps2/math/vec_mat.h"
#include "ps2/renderer/gs.h"
#include "ps2/renderer/render_system.h"
#include "ps2/renderer/world.h"
#include "ps2/ui/draw.h"

#include <idlib/precompiled.h>
#include <renderer/tr_local.h>

// Native world declarations depend on frontend types from tr_local.h.
#include <renderer/RenderWorld_local.h>

namespace ps2::renderer::scene
{
namespace
{

static bool s_initialized = false;
static Stats s_stats = {};

// id projects to a far plane at infinity, which the 16-bit depth buffer cannot hold; Doom's rooms
// end well inside this.
static constexpr float kFarPlane = 8192.0f;

// The world pass's clip space (math::PerspectiveProjection) for id's camera: eye space as
// math::LookAt builds it (x right, y up, z behind the eye) from id's forward, left and up axes.
math::Mat4 ViewProjection(const viewDef_t & view)
{
    const renderView_t & camera = view.renderView;
    const idMat3 & axis = camera.viewaxis;

    const math::Vec3 right = { -axis[1].x, -axis[1].y, -axis[1].z };
    const math::Vec3 up = { axis[2].x, axis[2].y, axis[2].z };
    const math::Vec3 back = { -axis[0].x, -axis[0].y, -axis[0].z };
    const math::Vec3 eye = { camera.vieworg.x, camera.vieworg.y, camera.vieworg.z };

    const math::Mat4 viewMatrix = {{ { right.x, up.x, back.x, 0.0f },
                                     { right.y, up.y, back.y, 0.0f },
                                     { right.z, up.z, back.z, 0.0f },
                                     { -math::Dot(right, eye), -math::Dot(up, eye), -math::Dot(back, eye), 1.0f } }};

    // R_SetupProjectionMatrix's near plane and field of view; the aspect carries fov_x.
    const float zNear = camera.cramZNear ? r_znear.GetFloat() * 0.25f : r_znear.GetFloat();
    const float tanX = idMath::Tan(camera.fov_x * idMath::PI / 360.0f);
    const float tanY = idMath::Tan(camera.fov_y * idMath::PI / 360.0f);
    const float width = static_cast<float>(gs::Width());
    const float height = static_cast<float>(gs::Height());

    return viewMatrix * math::PerspectiveProjection(math::DegToRad(camera.fov_y), tanX / tanY, width, height, zNear, kFarPlane);
}

// The area a cooked world's area model draws, or -1 for any other entity. An area model's
// entity has a single reference, into its own area, and is that area's local model.
int EntityArea(const idRenderWorldLocal & renderWorld, const viewEntity_t & entity)
{
    const idRenderEntityLocal * def = entity.entityDef;
    const idRenderModel * model = def->parms.hModel;

    if (model == nullptr || !model->IsStaticWorldModel() || def->entityRefs == nullptr)
    {
        return -1;
    }

    const int area = def->entityRefs->area->areaNum;
    const bool owned = area >= 0 && area < renderWorld.localModels.Num() && renderWorld.localModels[area] == model;
    return owned ? area : -1;
}

} // namespace

PS2_COLD_FUNC void Init()
{
    PS2_AssertMsg(!s_initialized, "scene initialized twice");

    R_InitFrameData();
    s_stats = {};
    s_initialized = true;
}

PS2_COLD_FUNC void Shutdown()
{
    if (!s_initialized)
    {
        return;
    }

    R_ShutdownFrameData();
    s_initialized = false;
}

bool IsInitialized()
{
    return s_initialized;
}

void BeginFrame()
{
    PS2_AssertMsg(s_initialized, "scene frame without its frame arena");

    R_ToggleSmpFrame();
}

void RenderView(const viewDef_t & view)
{
    PS2_AssertMsg(s_initialized && renderer::FrameStarted(), "a view drawn outside a renderer frame");

    // id's RenderScene closes any gui drawing first; the 2D queued so far draws under the view.
    ui::Flush();

    const auto & renderWorld = *static_cast<const idRenderWorldLocal *>(view.renderWorld);
    const world::Level * level = renderWorld.cookedLevel;

    Stats stats = s_stats;
    stats.views = s_stats.views + 1U;
    stats.frame = renderer::FrameCount() + 1U;
    stats.area = view.areaNum;
    stats.entities = 0;
    stats.areas = 0;
    stats.batches = 0;

    bool drawing = false;

    for (const viewEntity_t * entity = view.viewEntitys; entity != nullptr; entity = entity->next)
    {
        ++stats.entities;

        // An empty scissor means the entity is only listed for shadows.
        const int area = level != nullptr && !entity->scissorRect.IsEmpty() ? EntityArea(renderWorld, *entity) : -1;

        if (area < 0)
        {
            continue;
        }

        if (!drawing)
        {
            world::BeginPass(ViewProjection(view));
            drawing = true;
        }

        world::DrawArea(*level, static_cast<u32>(area));
        ++stats.areas;
        stats.batches += level->sections.areas[area].batchCount;
    }

    if (drawing)
    {
        world::EndPass();
    }

    const int used = Max(frameData->frameMemoryAllocated.GetValue(), frameData->highWaterAllocated);
    stats.arenaBytes = Max(stats.arenaBytes, static_cast<std::uint32_t>(used));
    s_stats = stats;
}

Stats GetStats()
{
    return s_stats;
}

} // namespace ps2::renderer::scene
