
# PS2 backend cvars

The production loop registers the backend control below using Doom's
native command and cvar system. Test executables do not register production controls.

| Name | Debug default | Release default | Flags | Behavior and registration |
| --- | --- | --- | --- | --- |
| `ps2_maxFrames` | `0` | `0` | `CVAR_SYSTEM`, `CVAR_INTEGER` | Live, unarchived completed-frame limit, clamped to 0–1,000,000. Zero runs until quit; a positive value requests shutdown once reached. Registered in `src/ps2/system/headless.cpp`. |

Presentation is an early package policy, not a cvar: `presentation.cfg` beside the
ELF contains `headless`, `ntsc` or `pal`. A missing file selects headless. The policy
is read before Common startup and device allocation; `d3bfg.cfg` runs afterward.
Visible mode additionally registers the following `CMD_FL_SYSTEM` console commands,
not cvars. They have no debug/release default or archive behavior:

| Command | Arguments and behavior |
| --- | --- |
| `ps2_consoleText` | One quoted printable ASCII argument, delivered to the active native console edit field. |
| `ps2_consoleKey` | One named native console key; `toggle` opens/closes the console over an active movie. |
| `ps2_capture` | One checked `.tga` basename; capture the next completed presentation, including before queued quit. |
| `ps2_ui` | One of `load`, `unload`, `reload`, `status`, `pause`, `resume`; controls the authored native SWF. Load closes the console; unload reopens it. |
| `ps2_uiKey` | `tab` or `enter`; queue native SWF key events for the loaded movie while the console is closed. |
| `ps2_uiRetail` | `load`, `unload`, `reload`, `status`, or `font INDEX`; opt-in native English font resource screen. The local catalog must be staged before loading, using a fresh visible process. Font indices 0–4 select the cooked English families. |

`ps2_ui` and `ps2_uiKey` are registered in `src/ps2/ui/movie.cpp`, `ps2_uiRetail` in
`src/ps2/ui/retail_movie.cpp`, and the console/capture controls in
`src/ps2/system/presentation.cpp`. Native console input takes
priority over SWF input. The UI package selects startup loading with a `ps2_ui load`
command in its packaged `d3bfg.cfg`; there is no UI-enable cvar. See
[README.md](../README.md#build) for complete argument limits and usage.

The following engine cvars have PS2-specific policies:

| Name | Debug default | Release default | Flags | Behavior and registration |
| --- | --- | --- | --- | --- |
| `jobs_numThreads` | `0` | `0` | `CVAR_INTEGER`, `CVAR_NOCHEAT`, `CVAR_INIT` | Initial synchronous scheduler; worker creation is disabled and explicit parallelism requests still execute on the caller. Registered in `src/neo/idlib/ParallelJobList.cpp`. |
| `com_smp` | `0` | `0` | `CVAR_BOOL`, `CVAR_SYSTEM`, `CVAR_NOCHEAT`, `CVAR_ROM` | Synchronous game/draw dispatch; portable startup creates no game worker. Registered in `src/neo/framework/Common.cpp`, including the foundation. Production calls native `RunFrame` on the caller and stays in `NOMAP`. |
| `sys_lang` | `english` | `english` | `CVAR_SYSTEM`, `CVAR_INIT` | Resource language identifier; six engine language names are exposed. Restart policy follows `CVAR_INIT`. Registered in `src/ps2/system/sys_services.cpp`. |
| `net_clientMaxPrediction` | `5000` | `5000` | `CVAR_SYSTEM`, `CVAR_INTEGER`, `CVAR_NOCHEAT` | Retained frame metadata in milliseconds; no online service is enabled. Registered in `src/ps2/system/common_campaign.cpp`. |
| `saveGame_enable` | `0` | `0` | `CVAR_BOOL`, `CVAR_ROM` | Read-only disabled save policy in both core and campaign builds; neither live nor restart changes enable storage. Forced internal writes still encounter the unavailable session save manager. Registered in `src/ps2/system/save_metadata.cpp`. |
| `in_useJoystick` | `0` | `0` | `CVAR_ARCHIVE`, `CVAR_BOOL`, `CVAR_ROM` | Read-only disabled controller preference in both configurations. Neither live nor restart changes create an input source; forced internal writes still fail at sampling. Registered in `src/ps2/input/usercmd_stub.cpp`. |
| `in_joystickRumble` | `0` | `0` | `CVAR_SYSTEM`, `CVAR_ARCHIVE`, `CVAR_BOOL`, `CVAR_ROM` | Read-only disabled rumble preference; forced writes cannot enable `Sys_SetRumble`. Registered in `src/ps2/input/usercmd_stub.cpp`. |
| `net_ucmdRate` | `40` | `40` | `CVAR_SYSTEM`, `CVAR_INTEGER` | Retained usercmd interval metadata in milliseconds; network entry points fail explicitly. Registered in `src/ps2/system/common_campaign.cpp`. |

`jobs_numThreads` is not archived. Its init flag rejects direct cvar console commands;
even a forced internal change cannot enable workers in this milestone. `com_smp` is
not archived, and forced writes cannot change the portable synchronous dispatch path.
The input preferences retain their native archive flags and add ROM while unavailable;
rumble changes its native default from one to zero. Forced values may be archived but
do not enable a service. Other imported engine cvars retain their upstream defaults.
Production console startup forces `com_allowConsole=1`, `s_noSound=1` and
`g_xp_bind_run_once=1` before game initialization; these are startup policy overrides,
not changed declarations/defaults. They open the native console, keep playback inactive
and avoid executing desktop control bindings. The authored SWF is an explicit visible-mode
command; retail menu startup and physical input devices remain inactive.
The test bootstrap may register temporary fixture cvars; those are test inputs rather than user-facing backend controls.

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
World drawing, general image/shader loading and native vertex-cache allocation remain
unavailable. The separate console/authored-SWF paths use their bounded 2D providers;
these retained frontend cvar registrations do not initialize a GS device or select UI presentation.

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
