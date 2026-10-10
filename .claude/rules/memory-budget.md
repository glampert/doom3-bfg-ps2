# Memory budget (32 MB EE RAM)

## Doom foundation baseline

The sole C/newlib arena is dlmalloc; `src/ps2/system/heap.*` adds exact requested/backing
accounting using Doom tags. Unsized free uses the allocation's metadata, and constructors
before `main` are included. The complete validation table is in
[PORT_STATUS.md](../../docs/PORT_STATUS.md).

After the M3 collision slice on 2026-10-10, debug/release fixed core ELF residency is
1,881,520 / 1,954,864 bytes, including 459,824 bytes of BSS. Core initialization
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
The passing whole-object resident link with bounded collision/physics has
10,909,030 / 11,328,614 bytes of fixed PT_LOAD residency, including 5,238,118 bytes of
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

Adding queued entity-argument events and script removal raises game Init to 1,387,660
requested / 1,479,636 backing bytes in 2,251 allocations. After eight ticks and target
removal the ledger is 1,494,580 / 1,588,900 / 2,307; the three-cycle peak is 1,498,492 /
1,593,476 bytes. Event argument storage uses native
`idDynamicBlockAlloc<byte, 16 * 1024, 256>`, with base blocks retained until event shutdown. Warm map-shutdown ledgers remain
exact, and full game/decl/Common shutdown recovers 7,424 / 7,600 / 4. Arena commitment
after tests is 1,615,514 / 1,617,178 bytes (debug/release). Entity removal does not imply
that every native allocator returns its warmed backing immediately.

The fixed floor/wall collision fixture raises game Init to 1,396,264 requested /
1,488,684 backing bytes in 2,261 allocations. After eight ticks, requested/backing/count
is 1,735,756 / 1,834,088 / 2,406 for brush conversion, 1,735,772 / 1,833,700 / 2,395
for text-cache loading and 1,733,524 / 1,831,432 / 2,394 for binary-cache loading.
Conversion scratch raises the three-cycle peak to 2,057,428 / 2,153,928 bytes;
arena commitment after tests is 2,335,898 / 2,332,442 bytes (debug/release).
All COLLISION/PHYSICS_CLIP allocations and the trace-model cache are released at each
map shutdown, whose warm ledger remains exact across the different cache paths.
The native missing-`.proc` warning retains 608 requested bytes until Common shutdown;
portable shutdown now clears diagnostic lists before idlib teardown. Full game/decl/Common
shutdown still recovers exactly 7,424 / 7,600 / 4. No players, AAS/PVS or sound/render
worlds are initialized; these figures cover only the fixed collision/physics fixture.

Adding falling and grounded-sliding probes leaves game Init and the conversion peak
unchanged. Live requested/backing/count after eight ticks is now 1,741,392 / 1,840,352 /
2,420 for conversion, 1,741,408 / 1,839,964 / 2,409 for text loading and 1,739,160 /
1,837,696 / 2,408 for binary loading in both configurations. Final arena commitment
is 2,332,442 / 2,333,594 bytes (debug/release); full shutdown remains exactly 7,424 /
7,600 / 4.
The extra native entities/physics/contact storage is transient and every map shutdown
still releases all COLLISION/PHYSICS_CLIP allocations and trace-model cache entries.

Adding the native player-physics probe raises game Init by 8 requested bytes to
1,396,272 / 1,488,684 / 2,261. Registration now covers 160 classes; event callbacks
remain 496,480 requested bytes. After eight ticks, requested/backing/count is
1,747,464 / 1,846,840 / 2,430 for conversion, 1,747,480 / 1,846,452 / 2,419 for text
loading and 1,745,232 / 1,844,184 / 2,418 for binary loading. The conversion peak is
2,057,436 / 2,153,928 bytes; final arena commitment is 2,332,442 / 2,334,106 bytes
(debug/release). Synthetic commands reuse the existing heap-allocated `idUserCmdMgr`;
its buffers reset between maps and no input device or full player entity is acquired.
Clip ownership and full pre-boot recovery remain exact at 7,424 / 7,600 / 4.

Extending the player-physics fixture to 88 frames per map adds jump/landing and
crouch/standing shape checks without changing native Init or the conversion peak.
Live requested/backing/count after those frames is 1,747,756 / 1,848,712 / 2,466 for
conversion, 1,747,772 / 1,848,324 / 2,455 for text loading and 1,745,524 / 1,846,056 /
2,454 for binary loading. Final arena commitment is 2,333,466 / 2,335,386 bytes
(debug/release); the peak stays 2,057,436 / 2,153,928. Both clip heights are exercised
and every map shutdown still releases all COLLISION/PHYSICS_CLIP allocations and
trace-model cache entries. Warm reload ledgers match; full pre-boot recovery remains
exact at 7,424 / 7,600 / 4. The fixture still spawns no `idPlayer` or physical input device.

Adding bounded native `idPlayer` startup/ticks extends each map to 99 frames. Native
Init now requests 1,399,564 / 1,495,080 / 2,334 with the authored player script object.
Live requested/backing/count after all ticks is 1,793,384 / 1,896,880 / 2,528 for
conversion, 1,793,400 / 1,896,492 / 2,517 for text loading and 1,791,152 / 1,894,224 /
2,516 for binary loading in both configurations. Conversion scratch still determines
the 2,060,728 / 2,160,324 peak. Final arena commitment is 2,339,866 / 2,341,786 bytes
(debug/release). These figures include the player, native actor/animation threads,
script storage, empty inventory and native collision shape; HUD/PDA, model, weapons,
view effects, AAS/PVS and sound/render worlds are absent. The completed probes now
deactivate correctly instead of accumulating out-of-bounds warnings. Map shutdown
releases all COLLISION/PHYSICS_CLIP/PHYSICS_CLIP_ENTITY allocations and trace-model
cache entries, and full shutdown still recovers exactly 7,424 / 7,600 / 4.

Native player jump/crouch and three authored actor states extend each map to 195
frames. Script functions/counters raise native Init to 1,401,332 / 1,499,096 / 2,386.
Live requested/backing/count after those ticks is 1,797,968 / 1,903,668 / 2,579 for
conversion, 1,797,984 / 1,903,280 / 2,568 for text loading and 1,795,736 / 1,901,012 /
2,567 for binary loading in both configurations. Peak remains conversion scratch at
2,062,496 / 2,164,340 bytes. Fixed resident PT_LOAD is 10,903,526 / 11,323,622 bytes
(debug/release), including unchanged BSS of 5,238,118. Final arena commitment is
2,347,034 / 2,344,730 bytes. Clip/collision/cache teardown, warm reload and full
7,424 / 7,600 / 4 recovery remain exact; no presentation/AAS/PVS resources were added.

Adding the fixed roof and native blocked-standing lane extends each map to 275 frames.
Native Init stays 1,401,332 / 1,499,096 / 2,386. Live requested/backing/count is now
1,799,972 / 1,906,528 / 2,600 for conversion, 1,799,988 / 1,905,880 / 2,582 for text
loading and 1,797,796 / 1,903,676 / 2,581 for binary loading in both configurations.
Conversion scratch still determines the peak at 2,065,148 / 2,167,856 bytes. Fixed
resident PT_LOAD is 10,909,030 / 11,328,614 bytes (debug/release), with unchanged
BSS. Final arena commitment is 2,349,722 / 2,347,930 bytes. Disabling completed probe
bodies retains their ownership until map shutdown; every collision/clip/cache scope
is still released, warm ledgers match and full recovery remains 7,424 / 7,600 / 4.

Kernel reservation, stacks, full game state, assets, GS VRAM and transition peaks remain
unmeasured. The initial core does not establish campaign feasibility. See
[IMPLEMENTATION_PLAN.md](../../docs/IMPLEMENTATION_PLAN.md#7-memory-feasibility-and-budgets).

## Quake II reference

The allocator names and whole-system accounting below describe `quake2-ps2`.

- A single program-wide dlmalloc heap (`system/heap.h`) carries per-tag accounting
  (`ps2::heap::MemTag`). Kernel, ELF image and stack are booked as `MemTag::ElfSys`, so the
  tags add up to the whole 32 MB. dlmalloc's page size is pinned to 4096 because ps2sdk's
  `sysconf` fails (see `ps2-platform.md`).
