
# PS2 backend cvars

No `ps2_*` backend controls are registered yet. The foundation uses Doom's existing
command and cvar system. The following engine cvar has a PS2-specific policy:

| Name | Debug default | Release default | Flags | Behavior and registration |
| --- | --- | --- | --- | --- |
| `jobs_numThreads` | `0` | `0` | `CVAR_INTEGER`, `CVAR_NOCHEAT`, `CVAR_INIT` | Initial synchronous scheduler; worker creation is disabled and explicit parallelism requests still execute on the caller. Registered in `src/neo/idlib/ParallelJobList.cpp`. |

`jobs_numThreads` is not archived. Its init flag rejects direct cvar console commands;
even a forced internal change cannot enable workers in this milestone. Other imported
engine cvars retain their upstream defaults. The test bootstrap may register temporary
fixture cvars; those are test inputs rather than user-facing backend controls.

When a cvar is added to `src/ps2/`, record its name, debug and release defaults, flags,
whether it changes live or needs a restart, and the source that registers it. Keep
archived cvars registered in both configurations so an alternate build does not discard
the saved value. Proposed controls remain in [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)
until implemented and tested.
