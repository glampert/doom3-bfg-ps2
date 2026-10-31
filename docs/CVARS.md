
# PS2 backend cvars

The production loop registers the backend cvar below using Doom's
native command and cvar system. Test executables do not register production controls.

| Name | Debug default | Release default | Flags | Behavior and registration |
| --- | --- | --- | --- | --- |
| `ps2_maxFrames` | `0` | `0` | `CVAR_SYSTEM`, `CVAR_INTEGER` | Live, unarchived completed-frame limit, clamped to 0–1,000,000. Zero runs until quit; a positive value requests shutdown once reached. Registered in `src/ps2/system/headless.cpp`. |

Presentation is an early package policy, not a cvar: `presentation.cfg` beside the
ELF contains `headless`, `ntsc` or `pal`, the visible modes optionally followed by
`menu`, which opens the native shell menu at startup. A missing file selects headless.
The policy is read before Common startup and device allocation; `d3bfg.cfg` runs
afterward, after the menu has opened.
Production registers `ps2_status` in both modes. Visible mode additionally registers
the other `CMD_FL_SYSTEM` commands below. These commands have no debug/release
default or archive behavior:

| Command | Arguments and behavior |
| --- | --- |
| `ps2_status` | No arguments; reports startup state and completed frames in headless or visible production. Registered in `src/ps2/system/headless.cpp`. |
| `ps2_consoleText` | One quoted printable ASCII argument, delivered to the active native console edit field. |
| `ps2_consoleKey` | One named native console key; `toggle` opens/closes the console over an active movie. |
| `ps2_capture` | One checked `.tga` basename; capture the next completed presentation, including before queued quit. |
| `ps2_uiRetail` | `load`, `unload`, `reload`, `status`, `font INDEX`, `movie shell\|dialog\|none`, `execute shell\|dialog`, `dialog quit`, `draw shell`, `menu`, `key NAME`, `clock fixed\|real` or `logo on\|off`; opt-in native English font/movie resource screen. The local catalog must be staged before loading, using a fresh visible process. Font indices 0–4 select the cooked English families. Movie inspection/execution requires `make stage-ui-movies` staging and a loaded resource screen. `movie` keeps timelines inactive; `execute` enables checked native timeline/AVM1 work with minimal widget owners. `dialog quit` and `draw shell` draw the original dialog and shell in full. `menu` opens the native shell handler's menu; `key` then posts a keyboard key (`enter`, `escape`, `up`, `down`, `left`, `right`) or pad button (`cross`, `circle`, `dpad_up`, `dpad_down`, `dpad_left`, `dpad_right`) through Common's event loop while the console is closed. `clock fixed` (before a load) advances executing movies 1/60 s per frame; `logo` plays the drawn shell's logo transition. |

`ps2_uiRetail` is registered in `src/ps2/ui/movie.cpp` and the console/capture
controls in `src/ps2/system/presentation.cpp`. Native console input takes priority over
SWF input. Package-root `presentation.cfg` selects visible startup and the startup menu;
there is no UI-enable cvar. See
[developer guide](DEVELOPMENT.md#production-packages-and-controls) for complete argument limits and usage.

The following engine cvars have PS2-specific policies:

| Name | Debug default | Release default | Flags | Behavior and registration |
| --- | --- | --- | --- | --- |
| `jobs_numThreads` | `0` | `0` | `CVAR_INTEGER`, `CVAR_NOCHEAT`, `CVAR_INIT` | Initial synchronous scheduler; worker creation is disabled and explicit parallelism requests still execute on the caller. Registered in `src/neo/idlib/ParallelJobList.cpp`. |
| `com_smp` | `0` | `0` | `CVAR_BOOL`, `CVAR_SYSTEM`, `CVAR_NOCHEAT`, `CVAR_ROM` | Synchronous game/draw dispatch; portable startup creates no game worker. Registered in `src/neo/framework/Common.cpp`, including the foundation. Production calls native `RunFrame` on the caller and stays in `NOMAP`. |
| `sys_lang` | `english` | `english` | `CVAR_SYSTEM`, `CVAR_INIT` | Resource language identifier; six engine language names are exposed, but the current UI resource provider admits English only. Restart policy follows `CVAR_INIT`. Registered in `src/ps2/system/sys_services.cpp`. |
| `saveGame_enable` | `0` | `0` | `CVAR_BOOL`, `CVAR_ROM` | Read-only disabled save policy in both core and campaign builds; neither live nor restart changes enable storage. Forced internal writes still encounter the unavailable session save manager. Registered in `src/ps2/system/save_metadata.cpp`. |
| `in_useJoystick` | `0` | `0` | `CVAR_ARCHIVE`, `CVAR_BOOL`, `CVAR_ROM` | Read-only disabled controller preference in both configurations. Neither live nor restart changes create an input source; forced internal writes still fail at sampling. Registered in `src/ps2/input/usercmd_stub.cpp`. |
| `in_joystickRumble` | `0` | `0` | `CVAR_SYSTEM`, `CVAR_ARCHIVE`, `CVAR_BOOL`, `CVAR_ROM` | Read-only disabled rumble preference; forced writes cannot enable `Sys_SetRumble`. Registered in `src/ps2/input/usercmd_stub.cpp`. |

`jobs_numThreads` is not archived. Its init flag rejects direct cvar console commands;
even a forced internal change cannot enable workers in this milestone. `com_smp` is
not archived, and forced writes cannot change the portable synchronous dispatch path.
The input preferences retain their native archive flags and add ROM while unavailable;
rumble changes its native default from one to zero. Forced values may be archived but
do not enable a service. Other imported engine cvars retain their upstream defaults.
Production console startup forces `com_allowConsole=1`, `s_noSound=1` and
`g_xp_bind_run_once=1` before game initialization; these are startup policy overrides,
not changed declarations/defaults. They open the native console, keep playback inactive
and avoid executing desktop control bindings. The retail screens are explicit visible-mode
commands, and the native menu opens at startup only under a `menu` presentation policy.
The test bootstrap may register temporary fixture cvars; those are test inputs rather than user-facing backend controls.

When a cvar is added to `src/ps2/`, record its name, debug and release defaults, flags,
whether it changes live or needs a restart, and the source that registers it. Keep
archived cvars registered in both configurations so an alternate build does not discard
the saved value. Proposed controls remain in [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)
until implemented and tested.

## Retained renderer frontend controls

The 42 controls below are registered by [render_cvars.cpp](../src/ps2/renderer/render_cvars.cpp)
in both core and campaign builds. The 24 developer controls (isolation, skip, show and joint-name
toggles) are `DEV_CVAR`s: PS2 release builds compile them out with every reader, behaving as
their defaults, so their release default reads "absent". The archived `r_debugArrowStep`
stays registered in release so a saved value survives, but release code never reads it. Native defaults, declared flags, numeric bounds and
completion handlers are preserved. They remain live cvars under the engine's cheat
policy; neither changing them nor setting a skip flag initializes the renderer.
World drawing, general image/shader loading and native vertex-cache allocation remain
unavailable. The separate console and retail UI paths use their bounded 2D providers;
these retained frontend cvar registrations do not initialize a GS device or select UI presentation.

| Name | Debug default | Release default | Declaration flags | Bounds / metadata |
| --- | --- | --- | --- | --- |
| `r_useLightPortalFlow` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_checkBounds` | `0` | absent | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_useConstantMaterials` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_useSilRemap` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_useNodeCommonChildren` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_useShadowSurfaceScissor` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_useCachedDynamicModels` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_znear` | `3` | `3` | `CVAR_RENDERER`, `CVAR_FLOAT` | Clamped to 0.001–200. |
| `r_jitter` | `0` | absent | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_skipSuppress` | `0` | absent | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_skipDeforms` | `0` | `0` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_skipFrontEnd` | `0` | absent | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_skipUpdates` | `0` | absent | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_skipDecals` | `0` | absent | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_skipOverlays` | `0` | absent | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_skipSubviews` | `0` | absent | `CVAR_RENDERER`, `CVAR_INTEGER` | Native frontend control. |
| `r_skipGuiShaders` | `0` | absent | `CVAR_RENDERER`, `CVAR_INTEGER` | Clamped to 0–3; integer completion. |
| `r_skipParticles` | `0` | absent | `CVAR_RENDERER`, `CVAR_INTEGER` | Clamped to 0–1; integer completion. |
| `r_useLightPortalCulling` | `1` | `1` | `CVAR_RENDERER`, `CVAR_INTEGER` | Clamped to 0–2; integer completion. |
| `r_useLightAreaCulling` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_useLightScissors` | `3` | `3` | `CVAR_RENDERER`, `CVAR_INTEGER` | Clamped to 0–3; integer completion. |
| `r_useEntityPortalCulling` | `1` | `1` | `CVAR_RENDERER`, `CVAR_INTEGER` | Clamped to 0–2; integer completion. |
| `r_subviewOnly` | `0` | absent | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_flareSize` | `1` | `1` | `CVAR_RENDERER`, `CVAR_FLOAT` | Native frontend control. |
| `r_skipPrelightShadows` | `0` | absent | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_useShadowDepthBounds` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_screenFraction` | `100` | absent | `CVAR_RENDERER`, `CVAR_INTEGER` | Native frontend control. |
| `r_usePortals` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_singleLight` | `-1` | absent | `CVAR_RENDERER`, `CVAR_INTEGER` | Native frontend control. |
| `r_singleEntity` | `-1` | absent | `CVAR_RENDERER`, `CVAR_INTEGER` | Native frontend control. |
| `r_singleSurface` | `-1` | absent | `CVAR_RENDERER`, `CVAR_INTEGER` | Native frontend control. |
| `r_singleArea` | `0` | absent | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_lightAllBackFaces` | `0` | absent | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_showUpdates` | `0` | absent | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_showLightScissors` | `0` | absent | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_useEntityCallbacks` | `1` | `1` | `CVAR_RENDERER`, `CVAR_BOOL` | Native frontend control. |
| `r_showSkel` | `0` | absent | `CVAR_RENDERER`, `CVAR_INTEGER` | Clamped to 0–2; integer completion. |
| `r_jointNameScale` | `0.02` | absent | `CVAR_RENDERER`, `CVAR_FLOAT` | Native frontend control. |
| `r_jointNameOffset` | `0.5` | absent | `CVAR_RENDERER`, `CVAR_FLOAT` | Native frontend control. |
| `r_debugArrowStep` | `120` | `120` | `CVAR_RENDERER`, `CVAR_ARCHIVE`, `CVAR_INTEGER` | Clamped to 0–120. |
| `r_materialOverride` | empty string | absent | `CVAR_RENDERER` | Native material-name completion; world-material override drawing remains unavailable. |
| `stereoRender_swapEyes` | `0` | `0` | `CVAR_BOOL`, `CVAR_ARCHIVE` | Native frontend control. |
