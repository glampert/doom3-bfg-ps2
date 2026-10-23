# Memory budget (32 MB EE RAM)

Doom sections retain the measurements of their dated implementation slice. The
latest authored UI baseline is [the convex-mask budget](#doom-convex-ui-mask-budget-2026-10-18);
the retail boot and UI format measurements are in
[the 2026-10-20 budget section](#retail-boot-and-ui-formats-2026-10-20), and the
converted textures that replaced retail page streaming in
[the paletted UI section](#converted-retail-ui-textures-2026-10-20), with the menu
glyph sets, the console charset, the drawn shell, the glyph rectangles and the native
shell menu added in
[their own sections](#pre-rendered-menu-glyphs-2026-10-22); current
aggregate status is in [PORT_STATUS.md](../../docs/PORT_STATUS.md). Earlier
production sizes and live ledgers must not be used as current budgets. Quake figures
remain separately labeled reference observations.

Debug `PT_LOAD` depends on where the SDK is installed: asserts in ps2sdk's inline
`packet2_chain.h` and `packet2_vif.h` embed its absolute path through `__FILE__`. The same
sources built against a deeper SDK path measured 256 bytes more (2026-10-19). Compare debug
sizes within one install only; release builds carry no such paths and match across installs.

## Doom foundation baseline

The sole C/newlib arena is dlmalloc; `src/ps2/system/heap.*` adds exact requested/backing
accounting using Doom tags. Unsized free uses the allocation's metadata, and constructors
before `main` are included. The complete validation table is in
[PORT_STATUS.md](../../docs/PORT_STATUS.md).

The separate production console boot measured on 2026-10-11 UTC retains 10,863,078 /
11,282,726 fixed `PT_LOAD` bytes (debug/release). Native game/console startup requests
1,845,319 bytes, with 1,942,752 backing in 2,384 allocations. Live console input peaks
at 1,845,560 / 1,943,152 requested/backing bytes; arena peak commitment is 1,957,402 /
1,959,642 bytes. Both 120-frame and live-quit runs recover exactly 7,424 / 7,600 bytes
in four process-lifetime allocations. Tagged backing is inside the arena. NOMAP console
residency excludes loaded maps, retail assets, rendering/output devices, kernel and stacks.

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
10,913,894 / 11,333,222 bytes of fixed PT_LOAD residency, including 5,238,118 bytes of
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

Adding the two step boxes triggers native axial BSP construction and extends each map
to 331 frames. Native Init stays 1,401,332 / 1,499,096 / 2,386. Live requested/backing/count
is 1,810,892 / 1,919,088 / 2,640 for conversion, 1,810,908 / 1,918,004 / 2,611 for text
loading and 1,801,868 / 1,908,908 / 2,609 for binary loading in both configurations.
Conversion scratch raises the peak to 2,077,364 / 2,181,712 bytes. Fixed resident
PT_LOAD is 10,913,894 / 11,333,222 bytes (debug/release), including unchanged BSS
of 5,238,118. Final arena commitment is 2,353,050 / 2,351,514 bytes. BSP nodes/brush
references are map-owned; every collision/clip/cache scope is released, warm reload
ledgers match and full recovery remains exactly 7,424 / 7,600 / 4. These 2026-10-11
measurements cover bounded native stepping, not general stairs/slopes or campaign maps.

Kernel reservation, stacks, full game state, assets, GS VRAM and transition peaks remain
unmeasured. The initial core does not establish campaign feasibility. See
[IMPLEMENTATION_PLAN.md](../../docs/IMPLEMENTATION_PLAN.md#7-memory-feasibility-and-budgets).

## Doom authored native map baseline

On 2026-10-11 the native authored map/world/PVS/AAS/idle-AI path passes in debug and
release. Fixed resident `PT_LOAD` is 10,920,422 / 11,338,214 bytes, including 5,238,118
BSS bytes. Three 60-frame map reloads use about 2.22 MB requested / 2.33 MB backing
in 2,681 allocations. Tagged peak is 2,523,851 / 2,628,112 bytes; final arena commitment
is 2,645,530 bytes in both configurations. Full teardown returns to 7,424 / 7,600 bytes
in four pre-main allocations. Warm map shutdown is exact. See the current section in
[PORT_STATUS.md](../../docs/PORT_STATUS.md) for run identities and limits.

Native `R_IdentifySilEdges` originally used `MAX_SIL_EDGES` as list/hash granularity,
reserving multi-megabyte scratch even for the default cube and two tiny floor models.
That produced 8,175,854 requested peak and roughly 12 MB arena commitment. Bound PS2
scratch granularity by the surface's index count, capped at the native maximum:
at most one edge is appended per index. The edge algorithm/output stays native.
This is a measured loading peak reduction, not a retail-map capacity or hardware timing
result. The authored AI has no mesh/animation, no campaign player or Common transition
runs, and no texture/frame arenas or playback resources are initialized. Kernel and
stack accounting remains separate; tagged backing is part of the arena.

## Doom 2D device budgets

Accepted 2D-2 production `PT_LOAD` is 10,978,470 debug / 11,396,518 release bytes,
including approximately 80 KiB of persistent aligned capture strips/control storage.
That BSS remains resident in headless mode; the headless loop still allocates no device
command/pixel pools. Default device caps are two 128 KiB command halves, 2 MiB owned
RGBA pixels and 2 MiB texture VRAM. PSMCT16 color contexts plus Z16 consume 1,376,256
NTSC / 1,572,864 PAL bytes. Direct RGBA storage charges whole 64×32/8192-byte pages,
including deterministic padding, rather than logical width×height bytes.

The integrated small-pool scene owns 81,920 pixel bytes, peaks at 65,536 texture VRAM
bytes, and demonstrates three LRU evictions with queued-reference fences. Its separate
512 KiB split-upload exercise peaks at 1,051,671 requested / 1,052,076 backing bytes
including caller source plus owned copy. Arena peaks are 1,067,984 debug / 1,069,776
release bytes. Tagged bytes are contained in that arena. Capture is streamed through
32-row strips; no whole-frame RGB allocation is made. See PORT_STATUS for exact
isolated-test and production baselines; these are PCSX2 residency observations.

The first native adapter slice adds 270,336 owned texture bytes for the 256×256 RGBA
ASCII atlas and page-minimum white texel. Generation temporarily uses another 262,144
bytes, released before frames. Image declarations/state are owned and freed separately;
the default headless path never acquires them. At that adapter checkpoint production `PT_LOAD` was
10,992,678 debug / 11,412,390 release bytes with 355 retained inputs. Integrated-test
tagged peak remains 1,051,671 / 1,052,076 bytes; arena peaks are now 1,070,288 /
1,070,544 bytes. These include the earlier split-upload and adapter reload exercises.

The indexed 2D-4 slice on 2026-10-15 adds 146,944 requested bytes in three tagged CPU
arrays: 4,096 native vertices, 6,144 16-bit indices and 128 ordered draw records.
They are allocated only by native UI activation, recycled after completed draw runs
and released before device shutdown; no desktop vertex-cache pool is enabled.
At that indexed-geometry checkpoint production retained 357 inputs and 11,009,702 debug / 11,434,406 release
`PT_LOAD` bytes. The visible console peaks at 2,786,855 requested / 2,884,740 backing
bytes, with arena high-water 2,900,314 debug / 2,897,498 release. Both visible and
headless runs recover exactly 7,424 / 7,600 bytes in four allocations. Headless
allocates no UI/device pools. These authored console measurements exclude retail
SWF/font working sets and loaded campaign maps; see PORT_STATUS for matched runs.

The authored native SWF/font completion on 2026-10-15 retains 360 production inputs
and 11,018,214 debug / 11,435,878 release `PT_LOAD` bytes. The lazy 512×128 procedural
font atlas adds 262,144 owned RGBA bytes and a temporary source allocation of the same
size during generation. Native glyph/index arrays and font/material caches have separate
ownership. Compiled exact-resource validation payloads total 4,386 bytes in fixed ELF
residency, not tagged heap storage.
Debug/release NTSC/PAL three-load runs peak at 3,065,630 requested / 3,167,572 backing
bytes, with arena high-water 3,424,282 debug / 3,424,410 release. Every warm unload
returns to 2,792,635 / 2,892,808 bytes in 2,446 allocations after clearing command history;
fonts/materials remain cached until UI shutdown. Full shutdown recovers 7,424 / 7,600
bytes in four allocations. These are authored ASCII UI measurements, not retail menu,
localization or campaign-map budgets. Headless startup still allocates no UI pools.

## Retail UI host inventory (2D-5)

The 2026-10-16 host shell/dialog cook produces 143 logical 256×256 RGBA pages,
145 unique asset files / 34,336,948 bytes, plus a 10,173,242-byte manifest. Source
level-zero RGBA totals 26,220,672 bytes. These are ignored host storage totals, not
EE allocations or simultaneous GS residency. Never bulk-load that decoded package
to infer a target budget. The profile's eight-page / 2 MiB CPU pixel cache is proposed;
native references, metadata, font arrays and transition peaks still need measurement.
Pages include a one-texel gutter and 254×254 interior; deduplication makes unique
storage differ from logical page count. See docs/RETAIL_UI_PIPELINE.md.

The subsequent UIPG packaging step adds twelve indexed runtime files while retaining
the raw page/source assets for inspection: 157 unique files / 71,825,068 bytes, plus
a 10,180,729-byte diagnostic manifest. The EE reads the compact per-image index,
not that JSON report. The authored six-page test reserves two CPU texture slots
(524,288 bytes), one aligned input page (262,144 bytes) and six records (48 bytes):
786,480 requested RENDER bytes, separately from object/file/allocator overhead and
the caller's device. Warm drawing allocates no additional buffers. One VRAM page
forces eviction in the test; this deliberately constrained scene does not establish
the English retail shell's working set or a performance budget.

The native authored binding on 2026-10-17 shares four nearest/clamp texture slots
(1,048,576 bytes), one input scratch page (262,144 bytes) and seven page records
(56 bytes) across a 272×256 SWF atlas and 512×128 font atlas. This totals 1,310,776
requested RENDER payload bytes; objects, files, intrinsics and allocator overhead are
separate. `idImage::StorageSize` attributes only each paged image's records; count
shared pixels/scratch once through the cache stats. Four page loads remain warm
through three movie reloads, with zero evictions. Movie unload retains images/fonts;
UI shutdown releases them and recovers the complete startup ledger.
Debug/release production `PT_LOAD` is 11,039,078 / 11,461,990 bytes (363 direct inputs).
All four native UI runs peak at 3,878,214 requested / 3,984,104 backing bytes; arena
commitment is 4,210,330 debug / 4,213,402 release. Each warm unload returns to
3,842,041 / 3,942,880 bytes in 2,456 allocations. Full shutdown is exactly
7,424 / 7,600 bytes in four allocations. These are authored page-binding measurements;
the English retail menu's multi-font working set and transition peak remain unmeasured.

## English retail font resource screen (2D-5 slice)

Accepted 2026-10-17 in debug/release NTSC/PAL. Six linear/clamp pixel slots reserve
1,572,864 bytes, shared scratch reserves 262,144, and five open font image tables
reserve 384: 1,835,392 requested RENDER payload bytes. The 5,236-byte immutable UICT
catalog, font arrays, objects/files, intrinsics and allocator overhead are separate.
Twelve image descriptors are admitted but only the five font atlases are opened.
The test word's per-font working set is at most four pages. Sequential family changes
produce 22 page loads and 16 evictions; returning to the first reloads four pages and
its subsequent warm preview reload adds none. This is not the complete menu's set.

All four runs peak at 4,392,239 requested / 4,495,188 backing bytes; arena commitment
is 4,734,170 debug / 4,733,530 release. Warm preview unload retains native fonts/images
at 4,388,908 / 4,491,596 bytes in 2,497 allocations. Full shutdown returns exactly to
7,424 / 7,600 bytes in four allocations. Production fixed PT_LOAD is 11,055,910 debug /
11,478,438 release bytes with 368 direct objects. Keep shared payload, allocator
commitment and fixed ELF residency separate; do not add these as independent heaps.

## Inactive retail dictionaries and English text

The 2026-10-17 resource-only shell/dialog gate keeps all 9,097 English entries and
native BSWF dictionary geometry/timelines resident while rendering three isolated
labels. Native shell has 618 dictionary records; dialog has 56. Only one movie lives
at a time. File snapshots are freed after native parsing. Five font images plus two
lazy movie atlas tables share the existing six slots (1,572,864 bytes) and scratch
(262,144); table records total 984, making 1,835,992 requested RENDER payload bytes.
Only three font pages stream for these labels; neither movie bitmap atlas draws.

All four debug/release NTSC/PAL runs peak at 7,362,559 requested / 7,555,996 backing
bytes. Arena commitment is 7,590,362 debug / 7,588,954 release. After both atlas bindings
exist, repeated shell loads stay at 6,717,176 / 6,912,048 bytes in 4,704 allocations.
The first shell load is smaller by 1,360 requested / 1,812 backing bytes / 11 allocations
because the dialog's image/material metadata is still lazy. Full shutdown, including
localization key/hash/block pools, recovers 7,424 / 7,600 bytes in four allocations.
ClearDictionary alone retained 1,626,852 requested bytes above baseline; it is a reload
operation, so use the explicit full-release seam at shutdown. Current production
PT_LOAD is 11,066,918 debug / 11,490,086 release bytes (369 direct inputs). These are
inactive dictionary/text measurements, not active menu/campaign or hardware budgets.

## Scoped retail timelines and native widget owners

The 2026-10-18 debug/release NTSC/PAL gate activates the original shell/dialog with
minimal native carousel/Credits ownership. Shell frame one has 1,855 sprites / 485
texts / nine levels; animation grows within the 2,048 / 512 / 16 limits. All active
script sources/receivers retain bounded strict-ancestor snapshots until their calls
return. The fixed origin ledger is part of ELF residency, not a heap allocation.

Peak heap usage across the four execution runs is 11,137,065 requested / 11,838,584
backing bytes. Arena commitment is 11,986,395 debug / 11,984,219 release. Clock-driven
animation makes loaded ownership variable: bound it, then compare exact warm ownership
after movie/widget teardown (5,446,190 / 5,551,040 bytes in 2,548 allocations). Full
shutdown returns to 7,424 / 7,600 bytes in four allocations. Clear queued console
history before a final release checkpoint; its future echo identity otherwise adds
a transient 64-byte string even though shutdown frees it correctly.

The Doom 3 cover adds only metadata, bringing open tables to eight images / 1,032
record bytes. Six linear slots plus scratch and records request 1,836,040 RENDER
payload bytes, included in the heap totals above. Only three font pages stream, with
no eviction; movie/cover artwork does not draw yet. Current production PT_LOAD is
11,139,621 debug / 11,575,973 release bytes (373 direct inputs). Keep fixed residency,
heap backing and allocator arena separate. These are bounded initial timeline/text
measurements, not complete menu artwork, campaign or physical hardware budgets.

## Native SWF additive blend slice

The 2026-10-18 mode-8 path adds one GS blend equation, without a new surface, pixel
buffer or composition pass. The extended authored movie keeps its two images, four
nearest slots (1 MiB), 256 KiB scratch page and 56 page-record bytes. Six new placement
records reuse the existing dictionary/atlases; the BSWF is 2,548 bytes. Warm page loads
remain four with no eviction in debug/release NTSC/PAL.

The authored scene peaks at 3,879,948 requested / 3,985,980 backing bytes. Arena
commitment is 4,212,059 debug / 4,213,595 release. Warm unload retains 3,842,175 /
3,943,140 bytes in 2,459 allocations; full shutdown recovers 7,424 / 7,600 bytes in
four allocations. Production PT_LOAD is 11,139,749 debug / 11,576,485 release bytes,
with 373 direct inputs and no fixtures. These are authored UI measurements; the
retail movie's simultaneous artwork working set remains unmeasured.

## Quake II reference

The allocator names and whole-system accounting below describe `quake2-ps2`.

- A single program-wide dlmalloc heap (`system/heap.h`) carries per-tag accounting
  (`ps2::heap::MemTag`). Kernel, ELF image and stack are booked as `MemTag::ElfSys`, so the
  tags add up to the whole 32 MB. dlmalloc's page size is pinned to 4096 because ps2sdk's
  `sysconf` fails (see `ps2-platform.md`).


## Doom convex UI mask budget (2026-10-18)

The bounded CPU mask bridge adds **4,404 bytes** of fixed resident state: a 3,852-byte
triangle-edge builder, four 132-byte convex polygons, four display-entry keys and
capture/count fields on the EE ABI. It adds no pixel surfaces, VRAM allocations,
per-frame heap storage or new native fixture. Bounds are 64 input triangles per
capture, 16 simplified boundary points, four active masks, 64 planes and at most
67 clipped polygon vertices per source triangle.

GCC 15 debug `-fstack-usage` reports **7,440 bytes** for `ClipMaskTriangle`,
**1,888 bytes** for the caller's triangle dispatcher and **1,728 bytes** for
`MaskBuilder::Finish`. Native `RenderSprite` reports **1,248 bytes per recursive
level**. These are individual function frames, not a measured whole-program stack
high-water. The SDK linkfile and final link map reserve **128 KiB** for the main
stack; the admitted retail hierarchy cap is sixteen levels. The bounded call chain
fits that reserve, but this is not a complete campaign stack measurement.

The expanded twenty-entry authored movie peaks at **3,885,301 requested / 3,992,336
backing bytes** in all four debug/release NTSC/PAL runs. Arena high-water is
**4,212,891 debug / 4,214,043 release**. Each warm unload still returns exactly to
**3,842,175 / 3,943,140 bytes / 2,459 allocations**, and full shutdown to
**7,424 / 7,600 / 4**. Shared four-slot nearest cache, 256 KiB scratch and 56-byte
page metadata are unchanged. See PORT_STATUS for final link sizes and acceptance IDs.

## Doom toolchain rebuild comparison (2026-10-19)

The same production code on the old toolchain (2026-10-18 runs) and the rebuilt one
(ps2sdk `329e2bb`, newlib built with `HAVE_BLKSIZE`) gives identical tagged ledgers:
headless ready 1,845,287 / 1,942,720 bytes in 2,384 allocations, authored UI peak
3,885,301 / 3,992,336, the same warm unloads and exact 7,424 / 7,600 / 4 recovery. Arena
commitment moves only: +12,672 debug / +16,768 release bytes in the bounded headless
run, consistent with stdio buffers now sized from `st_blksize`, and +384 / −3,712 in the
NTSC authored UI run. Production `PT_LOAD` shrank by 384 bytes in both configurations,
to 11,159,013 / 11,596,133. Untagged arena commitment depends on libc buffering and
allocator layout; compare tagged ledgers across toolchains.

## Original dialog full draw (2026-10-20)

Drawing the original dialog through native `Render` adds no pixel surface, VRAM pool or
fixed buffer: it reuses the six linear retail slots (1.5 MiB), the 256 KiB scratch page
and the shared UI geometry spans. Six open images (five fonts plus the dialog atlas)
hold 504 bytes of page records. Production `PT_LOAD` grows by 4,992 debug / 4,864
release bytes, to 11,164,005 / 11,600,997, for the draw admission, dialog owner and
mask counters.

Debug and release, NTSC and PAL, all load the dialog at 5,673,819 requested / 5,807,980
backing bytes in 3,229 allocations and rest at 5,690,707 / 5,827,180 / 3,281 on every
load. Every release, cold or warm, returns to 5,443,407 / 5,547,524 / 2,531. The heap
peaks at 6,079,651 / 6,183,080, with 6,399,643 (debug) / 6,400,923 (release) bytes of
arena commitment, and full shutdown recovers 7,424 / 7,600 / 4 exactly. The dialog's script state adds about 17 KB once
its intro has run. This is a much smaller footprint than the executing shell's 11 MB
peak.

Its cost is bandwidth, not heap: about 51 RGBA32 page loads per frame (see
[gs-renderer.md](gs-renderer.md)). A screen whose page working set exceeds the six
slots streams 256 KiB per miss; paletted or downscaled pages change both the slot
count GS memory can hold and the bytes each miss moves.

## Retail boot and UI formats (2026-10-20)

`make test-retail-boot` boots production with the retail `_common`/`_ordered`
containers: every declaration folder, the full `doom_main.script`, `NOMAP` for 120
frames and exact shutdown. Production prints `[D3BFG] MEMORY`/`ARENA`/`TAG` ledgers
after `startup-foundation`, `startup-decls` and `startup-game`; an allocation failure
lists every live tag before the fatal. Use these to judge any budget change.

- Debug startup ends at 10,148,418 requested / 13,473,216 backing bytes in 75,748
  allocations; peak 10,582,854 / 13,910,756, arena 14,523,035. With the kernel's first
  MiB and 11.17 / 11.60 MB of `PT_LOAD`, 6.8 MB debug / 6.4 MB release remain before
  any map, stacks and load peaks included.
- Owners (backing): compressed declaration text 4.76 MB, script 2.83 MB (1.38 MB
  requested), declaration objects 1.78 MB, strings 1.23 MB, container file tables
  1.08 MB, smoke particles 0.68 MB, event callbacks 0.64 MB.
- **Per-allocation overhead is 3.32 MB (25% of backing).** Small declaration/script
  allocations each pay the tagged header, up to 15 bytes of alignment slack and
  dlmalloc rounding. Count allocations, not only requested bytes.
- `_ordered.resources` used to be read wholesale into RAM (11.3 MB); PS2 streams it.
  `idResourceCacheEntry` keeps a 256-byte `idStrStatic` name per file, about 300 bytes
  for each of 3,598 entries.
- The script program's static arrays (3,515,932 bytes of BSS) hold 95,386 of 131,072
  statements and 206,536 of 296,608 global bytes for the retail game scripts.
- mars_city1's `.map` is 5.48 MB, 4.85 MB of it brush/patch text and 592 KB entity
  text; its container also holds 5.4 MB of binary collision and 2 MB of AAS.

Retail UI art is DXT in the containers and RGBA32 in the current pages. Measured
candidates, as texture bytes without page padding: PSMT8 with one CLUT per atlas
image, halved where the art is never drawn enlarged, takes the dialog from 3.9 MB of
pages to 0.18 MB, the shell 15.7 → 1.18, HUD 1.57 → 0.10, PDA 2.36 → 0.34 and pause
6.55 → 0.70; English-only 16-level-alpha fonts take all five from 12.6 MB to 0.20 MB.
Every screen then fits beside the framebuffers in GS memory. Quality, halving safety
and the display simulation are in [gs-renderer.md](gs-renderer.md). Nothing is
re-encoded yet; these are survey measurements, not runtime budgets.

## Converted retail UI textures (2026-10-20)

The first conversion pass halves every UI texture, fonts included, keeps only English
glyphs and stores PSMT8/PSMT4 pages with shared CLUTs, RLE-compressed on disc. Host
measurements from the deterministic cook, then the target ledgers:

- 16 textures (the twelve admitted plus HUD, PDA, pause and loading for review) go from
  9,095,516 bytes of BIMAGE resources to **762,608 bytes** of GSTX files, **1,980,032**
  decoded and 1,941,504 of VRAM in whole 8 KiB pages. The admitted twelve are 562,496 on
  disc and 1,397,760 decoded. The cook package shrinks from 158 files / 71.8 MB (UIPG and
  raw pages) to 38 files / 2,466,069 bytes, and the UICT2 catalog is 25,032 bytes.
- An open image keeps all its pages resident in the texture owner, which charges CLUT
  textures in whole 8 KiB PSMCT32 pages. Owned texels: the five fonts 122,880 bytes, the
  drawn dialog with its fonts 327,680, shell, dialog, cover and fonts together 1,277,952,
  all beside the console's 278,528 and inside the 2 MiB owner cap. That replaces the
  retail profile's six 256 KiB slots plus 256 KiB scratch (1,835,008 bytes) whatever the
  screen, and nothing streams per frame.
- `RETAIL textures` reports CLUT texels unpadded: pixels 102,400 / metadata 26,220
  bytes for the fonts; 303,104 / 30,974 for the dialog render; 1,163,264 / 49,384 for
  the inactive movie screens, whose shell and dialog atlases now load at open although
  only fonts draw; 1,249,280 / 51,136 once the shell executes with the Doom 3 cover.
  Metadata is the tables plus the placement lookup grid.
- Loading one texture briefly holds its largest page encoded and decoded side by side
  (at most 383,627 bytes here) plus a CLUT staging image; both are freed before the
  image publishes.
- Target ledgers (debug, NTSC and PAL identical, 2026-10-21), requested / backing bytes:
  the font screen rests at 2,721,623 / 2,825,048 (4,388,939 / 4,491,628 with streamed
  pages on 2026-10-20) and its peak is now the console's own 2,786,855 / 2,884,740
  (arena 2,899,995). The drawn dialog rests at 4,232,661 / 4,370,244 in 3,299
  allocations (was 5,690,707 / 5,827,180 / 3,281) and peaks at 4,412,335 / 4,516,500,
  arena 4,710,427 (was 6,079,651 / 6,183,080 and 6,399,643). Inactive movies peak at
  6,791,425 / 6,986,752 (was 7,363,353 / 7,557,028), executing ones at 10,657,582 /
  11,361,276 (was 11,137,858 / 11,839,616). Every run recovers 7,424 / 7,600 / 4 at
  full shutdown.

## Pre-rendered menu glyphs (2026-10-22)

The dialog's text draws from a glyph set: Sarasori's glyphs and outlines at six exact
sizes (three per standard) in a 1,024 × 99 GSTX image with 612 placements and one
256 × 384 PSMT4 page. It is 62,816 bytes on disc (19,680 of tables), 49,216 decoded,
and takes 48 KiB of VRAM plus its CLUT texture's 8 KiB page; the UICT3 catalog grows from
25,032 to 52,405 bytes (the set's GSTX descriptor and its 7,552-byte glyph table).

A font's set opens with the font, so every retail screen holds it, including the font
screen, which never draws it. Each screen's loaded heap grows by about 107 KB: the set's
EE page and CLUT copies (57,344 bytes), its tables and placement lookup grid and the
larger catalog. Debug ledgers, NTSC and PAL identical, requested / backing bytes:

- Font screen warm 2,828,948 / 2,932,868 / 2,519 (was 2,721,623 / 2,825,048 / 2,510); its
  peak is now above the console's own, 2,921,146 / 3,025,108 with a 3,066,267 arena.
- Dialog at rest 4,339,994 / 4,478,064 / 3,308, every release 4,092,694 / 4,198,408 /
  2,558, peak 4,519,660 / 4,624,320, arena 4,729,243.
- Inactive movies peak 6,898,750 / 7,094,572 (arena 7,211,419); executing ones
  10,764,915 / 11,469,112 (arena 11,565,467).
- Full shutdown still recovers 7,424 / 7,600 / 4; the authored UI is unchanged.

Production `PT_LOAD` is 11,183,717 debug / 11,620,901 release bytes (+5,888 / +6,208),
for the glyph lookup and draw path and the catalog's glyph table checks.

## Retail console charset (2026-10-22)

The console draws the catalog's `textures/bigchars` instead of a 256 KiB RGBA atlas
generated at startup: one 256×256 PSMT4 page (32,768 bytes of VRAM plus its CLUT's
8 KiB page), an EE copy of the same size with 128 bytes of tables and the placement grid,
and the UICT3 catalog, now 52,691 bytes, which every visible package loads at startup.
The generation buffer (another 262,144 bytes, freed before the first frame) is gone too.

Debug ledgers, NTSC and PAL identical, requested / backing bytes:

- Visible console peak 2,408,956 / 2,507,416 (was 2,786,855 / 2,884,740), arena
  2,547,483 (was 2,900,123).
- Authored UI warm unload 3,674,447 / 3,775,752 / 2,466 (was 3,842,204 / 3,943,156 /
  2,459; the seven allocations are the catalog and the charset), peak 3,717,573 /
  3,824,948, arena 3,890,971.
- Font screen warm 2,608,786 / 2,713,020 / 2,525, peak 2,700,984 / 2,805,260, arena
  2,846,491. Dialog at rest 4,119,832 / 4,258,216 / 3,314, every release 3,872,532 /
  3,978,560 / 2,564, peak 4,299,498 / 4,404,472, arena 4,517,659. Inactive movies peak
  6,678,588 / 6,874,724 (arena 6,991,643); executing ones 10,544,753 / 11,249,264
  (arena 11,345,691). Each retail screen is about 220 KB lighter than with the glyph
  sets alone.
- Headless runs are unchanged (no presentation, no catalog). Full shutdown still
  recovers 7,424 / 7,600 / 4.

Production `PT_LOAD` is 11,182,309 debug / 11,619,493 release bytes (−1,408 each); the
core test image, which now carries the charset generator as a fixture, is 2,153,200 /
2,237,808 bytes (+384 / +384).

## SWF draw features and the drawn shell (2026-10-23)

Outlines, color terms, screen blending and depth masks add no surface or pixel pool:
masks use the Z16 buffer the device already owned, fog and per-channel passes are GS
state, and lines are primitives. Two costs remain:

- `ui::Init` allocates the depth-mask capture buffers, 256 triangles and 192 segments of
  `MaskPoint` (9,216 bytes in two allocations), in every visible run.
- A drawn movie with screen placements gets three channel CLUT textures laid out like its
  atlas's CLUT texture: 66 tiles and 73,728 EE bytes for the shell, and 72 KiB of VRAM once
  the first screen pass binds them (24 KiB each, whole 8 KiB pages).

Debug ledgers, NTSC and PAL identical, requested / backing bytes:

- Visible console peak 2,418,704 / 2,517,248 (was 2,408,956 / 2,507,416), arena
  2,557,275. Authored UI warm unload 3,684,195 / 3,785,584 / 2,468 (was 3,674,447 /
  3,775,752 / 2,466), peak 3,733,225 / 3,842,508, arena 3,908,955; the authored
  draw-features sprite adds its own dictionary entries.
- Font screen warm 2,618,654 / 2,722,948 / 2,527, peak 2,710,852 / 2,815,188, arena
  2,856,283. Dialog at rest 4,129,720 / 4,268,160 / 3,316, every release 3,882,420 /
  3,988,504 / 2,566, peak 4,309,366 / 4,414,400, arena 4,527,451. Inactive movies peak
  6,688,496 / 6,884,684 (arena 7,001,435); executing shell 10,554,685 / 11,259,240
  (arena 11,355,483).
- Drawn shell (fixed clock): loaded 10,159,135 / 10,836,304 / 15,300; on the start screen
  after 120 frames 10,403,639 / 11,102,616 / 15,794; every release 4,751,460 / 4,858,788 /
  2,587; peak 10,413,423 / 11,113,892, arena 11,257,179. Its textures: 9 images, 12 pages,
  30 CLUTs, 1,138,688 pixel and 69,194 metadata bytes, plus the channel CLUTs.
- Full shutdown still recovers 7,424 / 7,600 / 4; headless runs are unchanged.

Production `PT_LOAD` is 11,205,157 debug / 11,642,981 release bytes (+22,848 / +23,488),
for the GS passes, lines, depth masks, mask line clipping and the shell controls.

## Glyph rectangles and the shell's text sizes (2026-10-23)

Glyph sets now hold one 1:1 band placement per page, and each UICT4 glyph entry (20 bytes,
was 12) carries its own rectangle. Adding the start screen's sizes (Sarasori 10 and 13,
Arial Narrow 2.5 and 3, each per standard) makes 1,442 entries in two tables (29,152
bytes, was 7,552), while the descriptors fall from 41,120 to 21,696 bytes: the catalog is
55,009 bytes (was 52,691). The Sarasori set grows to ten sizes on one 256×512 PSMT4 page
(64 KiB of VRAM, was 48 KiB; 51,712 bytes on disc, was 62,816) and Arial Narrow gains a
128×128 set (8 KiB of VRAM, 992 bytes). With every retail font open, the texture counters
read 8 images, 221,184 pixel and 27,864 metadata bytes (was 7, 192,512 and 49,032): the
pages grow, the placement tables and lookup grids shrink.

Debug ledgers, NTSC and PAL identical, requested / backing bytes:

- Visible console peak 2,421,022 / 2,519,568, arena 2,557,531; authored UI warm unload
  3,686,513 / 3,787,904 / 2,468, peak 3,735,543 / 3,844,828, arena 3,909,211. Both move
  only by the larger catalog.
- Font screen warm 2,632,944 / 2,737,728 / 2,536 (+14,290, nine allocations for the
  Arial Narrow set), peak 2,749,984 / 2,854,816, arena 2,909,787. Dialog at rest
  4,144,002 / 4,282,940 / 3,325, every release 3,896,702 / 4,003,284 / 2,575, peak
  4,323,656 / 4,429,180, arena 4,687,451. Inactive movies peak 6,702,786 / 6,899,480
  (arena 6,923,867); executing shell 10,568,967 / 11,274,020 (arena 11,372,123).
- Drawn shell: loaded 10,173,425 / 10,851,100 / 15,309; start screen after 120 frames
  10,417,929 / 11,117,412 / 15,803; every release 4,765,750 / 4,873,584 / 2,596; peak
  10,427,713 / 11,128,688, arena 11,269,723.
- Full shutdown still recovers 7,424 / 7,600 / 4.

Arena commitment moved by allocation order, not size: the dialog's rose by 160,000 bytes
and the inactive movies' fell by 77,568, against requested peaks within 15 KB. Production
`PT_LOAD` is 11,205,029 debug (−128) / 11,642,981 release (unchanged) bytes; the core test
image is 2,177,840 / 2,263,344 bytes (+512 / +640).

## Native shell menu (2026-10-23)

The native shell handler with its press-start, root and credits screens adds code and
widgets but no pixel or texture storage beyond the drawn shell's: the same atlas, cover,
fonts, glyph sets and channel CLUTs. The root screen's two outlined Sarasori sizes per
standard grow that set to 14 sizes on one 384 × 512 PSMT4 page (96 KiB of VRAM, was
64 KiB) and the catalog to 63,329 bytes. Every retail screen therefore holds 41,523 more
requested bytes: the larger page, the catalog and the longer `ps2_uiRetail` help text.

Debug ledgers, NTSC and PAL identical, requested / backing bytes:

- Menu opened (frame 0) 10,315,824 / 10,998,684 / 15,429; start screen after 120 frames
  10,572,164 / 11,278,728 / 15,970; root screen 90 frames after sign-in 10,589,084 /
  11,298,280 / 16,030; every release 4,807,273 / 4,915,104 / 2,596, the same as the
  drawn shell's; peak 10,599,180 / 11,310,272, arena 11,464,467.
- Drawn shell: start screen after 120 frames 10,459,452 / 11,158,932 / 15,803, peak
  10,469,236 / 11,170,208 (arena 11,312,915). Dialog at rest 4,185,525 / 4,324,460 /
  3,325, every release 3,938,225 / 4,044,804 / 2,575, peak 4,365,179 / 4,470,700.
  Font screen warm 2,674,467 / 2,779,248 / 2,536. Inactive movies peak 6,744,309 /
  6,941,000; executing shell 10,610,490 / 11,315,540.
- Visible console peak 2,429,342 / 2,527,888 (the catalog's 8,320 more bytes); authored
  UI warm unload 3,694,884 / 3,796,272 / 2,468 (the catalog and the help text).
- Full shutdown still recovers 7,424 / 7,600 / 4.

Production `PT_LOAD` is 11,256,045 debug / 11,705,197 release bytes (+51,016 / +62,216),
for the shell handler, its two screens and the menu module; the core test image is
2,177,840 / 2,263,216 bytes.

## Native shell menu key input (2026-10-23)

Key routing adds no buffers: events go through Common's existing queue, and the event
admission walks the handler's live widgets (about thirty) without allocating. The
`ps2_uiRetail` help text grows by two bytes on every retail screen. Debug ledgers, NTSC
and PAL identical, requested / backing / allocations:

- Menu opened 10,315,826 / 10,998,684 / 15,429; start screen after 120 frames 10,572,166 /
  11,278,728 / 15,970; root after accept 10,589,086 / 11,298,280 / 16,030; Credits focused
  10,586,030 / 11,294,768 / 16,020; start screen after back 10,590,374 / 11,298,860 /
  16,013; peak 10,615,030 / 11,328,688, arena 11,478,163.
- The menu releases to 4,807,239 / 4,915,028 / 2,595, one 36-byte allocation below the
  drawn shell's 4,807,275 / 4,915,104 / 2,596: back signs the local user out, and the
  drawn shell keeps the user signed in at startup.
- Full shutdown still recovers 7,424 / 7,600 / 4; the font, authored UI, dialog and shell
  screens move by those two bytes.

Production `PT_LOAD` is 11,258,733 debug / 11,707,757 release bytes (+2,688 / +2,560).

## Native quit dialog beside the menu (2026-10-24)

The menu now initializes Common's dialog manager, which keeps the admitted dialog movie
resident beside the shell for the menu's lifetime: its dictionary, scripts and atlas. No
save indicator movie is opened. Debug ledgers, NTSC and PAL identical, requested /
backing / allocations:

- Menu opened 10,644,048 / 11,339,800 / 15,734 (+328,222 requested for the dialog movie);
  start screen after 120 frames 10,900,388 / 11,619,844 / 16,275; root after accept
  10,917,308 / 11,639,396 / 16,335; menu peak 10,943,252 / 11,669,804, arena 11,823,187.
- Quit confirmation shown 11,058,136 / 11,802,084 / 16,831; after cancel 11,045,000 /
  11,786,756 / 16,783; the quit run peaks at 11,063,600 / 11,808,304, arena 11,958,355.
- The menu releases to 5,017,881 / 5,126,500 / 2,611 after back (5,017,534 / 5,125,624 /
  2,598 at the quit): the dialog atlas stays open with the other warm images (11).
- Full shutdown still recovers 7,424 / 7,600 / 4, including the quit through the dialog.

Production `PT_LOAD` is 11,261,869 debug / 11,710,765 release bytes (+3,136 / +3,008).
