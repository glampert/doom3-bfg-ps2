# Memory budget (32 MB EE RAM): Quake II reference

The allocator names and measurements below describe `quake2-ps2`; this repository has
no PS2 heap yet. Record Doom's own ELF, BSS, heap and transition peaks before setting
its budget. See [IMPLEMENTATION_PLAN.md](../../IMPLEMENTATION_PLAN.md#7-memory-feasibility-and-budgets).

- A single program-wide dlmalloc heap (`system/heap.h`) carries per-tag accounting
  (`ps2::heap::MemTag`). Kernel, ELF image and stack are booked as `MemTag::ElfSys`, so the
  tags add up to the whole 32 MB. dlmalloc's page size is pinned to 4096 because ps2sdk's
  `sysconf` fails (see `ps2-platform.md`).
