
# PS2 backend cvars

No `ps2_*` backend controls are registered yet. The foundation uses Doom's existing
command and cvar system. The following engine cvars have PS2-specific policies:

| Name | Debug default | Release default | Flags | Behavior and registration |
| --- | --- | --- | --- | --- |
| `jobs_numThreads` | `0` | `0` | `CVAR_INTEGER`, `CVAR_NOCHEAT`, `CVAR_INIT` | Initial synchronous scheduler; worker creation is disabled and explicit parallelism requests still execute on the caller. Registered in `src/neo/idlib/ParallelJobList.cpp`. |
| `com_smp` | `0` | `0` | `CVAR_BOOL`, `CVAR_SYSTEM`, `CVAR_NOCHEAT`, `CVAR_ROM` | Synchronous game/draw dispatch; portable startup creates no game worker. Registered in `src/neo/framework/Common.cpp`, including the foundation. Game execution is still deferred. |
| `sys_lang` | `english` | `english` | `CVAR_SYSTEM`, `CVAR_INIT` | Resource language identifier; six engine language names are exposed. Restart policy follows `CVAR_INIT`. Registered in `src/ps2/system/sys_services.cpp`. |
| `net_clientMaxPrediction` | `5000` | `5000` | `CVAR_SYSTEM`, `CVAR_INTEGER`, `CVAR_NOCHEAT` | Retained frame metadata in milliseconds; no online service is enabled. Registered in `src/ps2/system/common_campaign.cpp`. |
| `net_ucmdRate` | `40` | `40` | `CVAR_SYSTEM`, `CVAR_INTEGER` | Retained usercmd interval metadata in milliseconds; network entry points fail explicitly. Registered in `src/ps2/system/common_campaign.cpp`. |

`jobs_numThreads` is not archived. Its init flag rejects direct cvar console commands;
even a forced internal change cannot enable workers in this milestone. `com_smp` is
not archived, and forced writes cannot change the portable synchronous dispatch path.
Other imported engine cvars retain their upstream defaults. The test bootstrap may register temporary
fixture cvars; those are test inputs rather than user-facing backend controls.

When a cvar is added to `src/ps2/`, record its name, debug and release defaults, flags,
whether it changes live or needs a restart, and the source that registers it. Keep
archived cvars registered in both configurations so an alternate build does not discard
the saved value. Proposed controls remain in [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)
until implemented and tested.

## Retained renderer frontend controls

The 42 controls below are registered by [render_cvars.cpp](../src/ps2/renderer/render_cvars.cpp)
in both core and campaign builds. Native defaults, declared flags, numeric bounds and
completion handlers are preserved. They remain live cvars under the engine's cheat
policy; neither changing them nor setting a skip flag initializes the renderer.
Drawing, image/shader loading and buffer allocation fail at the interface until their
logical contracts exist. No GS/device behavior is implemented by this registration.

| Name | Debug default | Release default | Declaration flags | Bounds / metadata |
| --- | --- | --- | --- | --- |
| `r_useLightPortalFlow` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_checkBounds` | `0` | `0` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_useConstantMaterials` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_useSilRemap` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_useNodeCommonChildren` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_useShadowSurfaceScissor` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_useCachedDynamicModels` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_znear` | `3` | `3` | `CVAR_RENDERER`, `CVAR_FLOAT` | Clamped to 0.001–200. |
| `r_jitter` | `0` | `0` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_skipSuppress` | `0` | `0` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_skipDeforms` | `0` | `0` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_skipFrontEnd` | `0` | `0` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_skipUpdates` | `0` | `0` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_skipDecals` | `0` | `0` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_skipOverlays` | `0` | `0` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_skipSubviews` | `0` | `0` | `CVAR_RENDERER`, `CVAR_INTEGER` | Native frontend control. |
| `r_skipGuiShaders` | `0` | `0` | `CVAR_RENDERER`, `CVAR_INTEGER` | Clamped to 0–3; integer completion. |
| `r_skipParticles` | `0` | `0` | `CVAR_RENDERER`, `CVAR_INTEGER` | Clamped to 0–1; integer completion. |
| `r_useLightPortalCulling` | `1` | `1` | `CVAR_RENDERER`, `CVAR_INTEGER` | Clamped to 0–2; integer completion. |
| `r_useLightAreaCulling` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_useLightScissors` | `3` | `3` | `CVAR_RENDERER`, `CVAR_INTEGER` | Clamped to 0–3; integer completion. |
| `r_useEntityPortalCulling` | `1` | `1` | `CVAR_RENDERER`, `CVAR_INTEGER` | Clamped to 0–2; integer completion. |
| `r_subviewOnly` | `0` | `0` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_flareSize` | `1` | `1` | `CVAR_RENDERER`, `CVAR_FLOAT` | Native frontend control. |
| `r_skipPrelightShadows` | `0` | `0` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_useShadowDepthBounds` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_screenFraction` | `100` | `100` | `CVAR_RENDERER`, `CVAR_INTEGER` | Native frontend control. |
| `r_usePortals` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_singleLight` | `-1` | `-1` | `CVAR_RENDERER`, `CVAR_INTEGER` | Native frontend control. |
| `r_singleEntity` | `-1` | `-1` | `CVAR_RENDERER`, `CVAR_INTEGER` | Native frontend control. |
| `r_singleSurface` | `-1` | `-1` | `CVAR_RENDERER`, `CVAR_INTEGER` | Native frontend control. |
| `r_singleArea` | `0` | `0` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_lightAllBackFaces` | `0` | `0` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_showUpdates` | `0` | `0` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_showLightScissors` | `0` | `0` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_useEntityCallbacks` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_showSkel` | `0` | `0` | `CVAR_RENDERER`, `CVAR_INTEGER` | Clamped to 0–2; integer completion. |
| `r_jointNameScale` | `0.02` | `0.02` | `CVAR_RENDERER`, `CVAR_FLOAT` | Native frontend control. |
| `r_jointNameOffset` | `0.5` | `0.5` | `CVAR_RENDERER`, `CVAR_FLOAT` | Native frontend control. |
| `r_debugArrowStep` | `120` | `120` | `CVAR_RENDERER`, `CVAR_ARCHIVE`, `CVAR_INTEGER` | Clamped to 0–120. |
| `r_materialOverride` | empty string | empty string | `CVAR_RENDERER` | Native material-name completion; declaration services remain deferred. |
| `stereoRender_swapEyes` | `0` | `0` | `CVAR_BOOL`, `CVAR_ARCHIVE` | Native frontend control. |
