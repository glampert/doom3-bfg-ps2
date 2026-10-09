# Memory budget (32 MB EE RAM)

## Doom foundation baseline

The sole C/newlib arena is dlmalloc; `src/ps2/system/heap.*` adds exact requested/backing
accounting using Doom tags. Unsized free uses the allocation's metadata, and constructors
before `main` are included. The complete validation table is in
[PORT_STATUS.md](../../docs/PORT_STATUS.md).

After the resident-link services slice on 2026-10-09, debug/release fixed core ELF residency is
1,719,920 / 1,776,880 bytes, including 454,512 bytes of BSS. Core
initialization requests 19,837 bytes (26,896 backing);
the synthetic test peak is 45,715 bytes (53,500 backing). Both shut down to the exact
process-lifetime baseline of 1,024 requested / 1,068 backing bytes in one allocation.
dlmalloc's commitment after tests is 70,032 / 70,416 bytes, including untagged C/newlib
allocations and freed space. Do not double-count tagged backing on top of the arena.
The real portable `commonLocal` occupies 54,792 bytes; its desktop-only 2,304,000-byte
Classic framebuffer is absent. This prevents Classic residency when importing Common;
the previous small Common adapter did not contain that framebuffer either. Match tests
warm native dictionary string pools once, then require exact ledger stability over three
reloads and exact full-shutdown recovery. Snapshot/compression support is retained to
link Common's embedded value types; no online snapshots or game state are initialized.
The audio boundary adds unloaded sample metadata and ownership tests, without any
voice pool, sample payload or device. New language/frame cvars change startup/peak
totals; full shutdown still recovers exactly. The audio test must release its native
string allocation after every scope. Fixed ELF
and arena changes do not measure a full sound-world or SPU2 budget.
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
