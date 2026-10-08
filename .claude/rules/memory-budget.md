# Memory budget (32 MB EE RAM)

## Doom foundation baseline

The sole C/newlib arena is dlmalloc; `src/ps2/system/heap.*` adds exact requested/backing
accounting using Doom tags. Unsized free uses the allocation's metadata, and constructors
before `main` are included. The complete validation table is in
[PORT_STATUS.md](../../docs/PORT_STATUS.md).

After the class-allocation slice on 2026-10-09, debug/release fixed ELF residency is
1,564,000 / 1,620,768 bytes, including 392,288 / 392,352 bytes of BSS. Core
initialization requests 13,231 bytes (17,716 backing);
the synthetic test peak is 22,597 bytes (27,544 backing). Both shut down to the exact
process-lifetime baseline of 1,024 requested / 1,068 backing bytes in one allocation.
dlmalloc's commitment after tests is 45,728 / 46,304 bytes, including untagged C/newlib
allocations and freed space. Do not double-count tagged backing on top of the arena.
Class `memused` counts object bytes without another size prefix; shared heap metadata
supplies exact unsized-delete accounting while preserving object alignment. The
allocation tests use small probe classes and do not measure resident campaign state.

Kernel reservation, stacks, full game state, assets, GS VRAM and transition peaks remain
unmeasured. The initial core does not establish campaign feasibility. See
[IMPLEMENTATION_PLAN.md](../../docs/IMPLEMENTATION_PLAN.md#7-memory-feasibility-and-budgets).

## Quake II reference

The allocator names and whole-system accounting below describe `quake2-ps2`.

- A single program-wide dlmalloc heap (`system/heap.h`) carries per-tag accounting
  (`ps2::heap::MemTag`). Kernel, ELF image and stack are booked as `MemTag::ElfSys`, so the
  tags add up to the whole 32 MB. dlmalloc's page size is pinned to 4096 because ps2sdk's
  `sysconf` fails (see `ps2-platform.md`).
