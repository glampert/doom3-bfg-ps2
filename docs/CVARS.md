
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
