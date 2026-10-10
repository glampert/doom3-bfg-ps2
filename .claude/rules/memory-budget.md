# Memory budget (32 MB EE RAM)

## Doom foundation baseline

The sole C/newlib arena is dlmalloc; `src/ps2/system/heap.*` adds exact requested/backing
accounting using Doom tags. Unsized free uses the allocation's metadata, and constructors
before `main` are included. The complete validation table is in
[PORT_STATUS.md](../../docs/PORT_STATUS.md).

After the M3 logical voice slice on 2026-10-10, debug/release fixed core ELF residency is
1,881,264 / 1,954,480 bytes, including 459,824 bytes of BSS. Core initialization
requests 31,425 bytes (42,704 backing). The codec/compression synthetic peak is
315,905 requested / 327,668 backing bytes. Both shut down to the exact process-lifetime
baseline of 1,024 requested / 1,068 backing bytes in one allocation. dlmalloc commitment
after tests is 346,960 / 347,472 bytes, including untagged C/newlib allocations and
freed space. Do not double-count tagged backing on top of the arena.
The real portable `commonLocal` occupies 54,792 bytes; its desktop-only 2,304,000-byte
Classic framebuffer is absent. This prevents Classic residency when importing Common;
the previous small Common adapter did not contain that framebuffer either. Match tests
warm native dictionary string pools once, then require exact ledger stability over three
reloads and exact full-shutdown recovery. Snapshot/compression support is retained to
link Common's embedded value types; no online snapshots or game state are initialized.
The audio boundary owns fixture PCM payloads and an explicitly selected headless voice
pool. Its 48 slots occupy one 7,296-byte AUDIO allocation on the EE; this is logical
state, not a physical SPU2 voice budget. Allocated voices pin samples through stop and
completion until freed. Pool reuse/shutdown and sample scopes recover exact ledgers.
The maximum observed AUDIO request is 88,200 bytes; samples are read directly from
streams without a second whole-file buffer. The 256 KiB per-sample cap is an initial
fixture limit, not an aggregate sound-cache budget. Repeated loaded/default scopes
recover exact ledgers and AUDIO returns to zero. Language/frame and 42 renderer cvars change startup/peak
totals; full shutdown still recovers exactly. The audio test must release its native
string and payload allocations after every scope. Fixed ELF
and arena changes do not measure a full sound-world or SPU2 budget.
Renderer globals contain empty native metadata; the stub slice allocates no frame,
vertex-cache or texture payload pools. Its metadata scopes recover the exact ledger.
The deferred slice adds only native empty multiplayer and save-description tests plus
one disabled save cvar. Save-description tests warm native dictionary pools before
requiring exact repeated ledger recovery; full shutdown still returns to baseline.
Shell resources, save pipelines and multiplayer matches are not initialized. The input
provider adds two disabled archived cvars and native action metadata, without device
handles, polling buffers or generated player commands. Empty cleanup has no heap cost.
The passing whole-object resident link with script-driven target activation has
10,876,518 / 11,296,742 bytes of fixed PT_LOAD residency, including 5,237,990 bytes of
BSS in both configurations. Codec fixture memory measures compression
windows and tiny authored images, not retail JPEG/SWF assets or initialized game state.
All JPEG/zlib scopes recover exact ledgers; partial allocation failures release acquired
buffers, and no codec temporary-file backing store is enabled.
Class `memused` counts object bytes without another size prefix; shared heap metadata
supplies exact unsized-delete accounting while preserving object alignment. The
allocation tests use small probe classes and do not measure resident campaign state.

The initial playerless native game fixture on 2026-10-10 requests 1,387,268 bytes
(1,478,856 backing / 2,242 allocations) after game Init, and peaks at 1,479,588
requested / 1,573,440 backing bytes during world/entity/script startup and ticks.
Native class event callbacks account for 496,480 requested bytes. The script program
reports 3,515,932 bytes of inline static storage already included in ELF residency;
do not add it again to dynamic memory. dlmalloc commitment after three cycles is
1,592,602 / 1,594,010 bytes (debug/release), including untagged libc and freed capacity.
Each warm map shutdown recovers the same requested/backing/count ledger. Full shutdown
returns exactly to the resident pre-boot baseline of 7,424 / 7,600 bytes / 4 allocations,
which differs from the core baseline because more native globals are linked.
The native user-command manager is heap allocated to avoid its large stack footprint;
no players or player commands execute. No sound world, voice pool, collision/PVS, frame arena
or texture payload is initialized by this fixture. These measurements establish
logic-fixture residency only, not a full campaign or transition budget.

Adding the native command target and script `activate` declaration raises game Init to
1,387,484 requested / 1,479,328 backing bytes in 2,248 allocations. The three-cycle
activation fixture peaks at 1,481,836 / 1,576,512 bytes; arena commitment after tests
is 1,595,802 / 1,593,370 bytes (debug/release). Both still recover the exact 7,424 /
7,600 / 4 pre-boot ledger. The returned command is inspected without executing a
Common map transition, so no transition peak is established.

Kernel reservation, stacks, full game state, assets, GS VRAM and transition peaks remain
unmeasured. The initial core does not establish campaign feasibility. See
[IMPLEMENTATION_PLAN.md](../../docs/IMPLEMENTATION_PLAN.md#7-memory-feasibility-and-budgets).

## Quake II reference

The allocator names and whole-system accounting below describe `quake2-ps2`.

- A single program-wide dlmalloc heap (`system/heap.h`) carries per-tag accounting
  (`ps2::heap::MemTag`). Kernel, ELF image and stack are booked as `MemTag::ElfSys`, so the
  tags add up to the whole 32 MB. dlmalloc's page size is pinned to 4096 because ps2sdk's
  `sysconf` fails (see `ps2-platform.md`).
