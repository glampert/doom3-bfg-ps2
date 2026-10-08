
# PS2 backend cvars

No PS2 backend cvars are implemented yet. The root `Makefile` is still the Quake II
reference build, so its `ps2_*` definitions and defaults do not describe this port.

When a cvar is added to `src/ps2/`, record its name, debug and release defaults, flags,
whether it changes live or needs a restart, and the source that registers it. Keep
archived cvars registered in both configurations so an alternate build does not discard
the saved value. Proposed controls remain in [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)
until implemented and tested.
