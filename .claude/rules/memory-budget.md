# Memory budget (32 MB EE RAM)

This file is the current summary. Dated per-slice measurements, ledgers and the Quake II
reference are in [docs/reference/memory-budget.md](../../docs/reference/memory-budget.md);
aggregate status is in [PORT_STATUS.md](../../docs/PORT_STATUS.md). Earlier production
sizes and live ledgers must not be used as current budgets. Quake figures are separate
reference observations. The authored placeholder movie, its 1 MiB page cache and UIPG
pages were removed on 2026-10-29; "authored UI" figures in the history predate that and
have not been re-measured.

## How to measure and compare

- Keep three numbers apart: tagged ledgers (requested / backing bytes / allocations from
  `ps2::system::heap`), arena commitment (the heap's arena less the gap between its tiers), and
  fixed `PT_LOAD` (ELF, BSS). Tagged backing lives inside the arena: never add it on top. Kernel (first MiB), stacks, VRAM
  and GS buffers are separate again.
- Production's `PT_LOAD` (report.json's sum) includes the boot data segment, 190,784 bytes of
  IOP storage drivers that the heap takes back during boot (2026-11-17): compare the first
  segment, or subtract it, against older figures. The reclaimed block is committed-but-free in
  the arena until large allocations reuse it, so early `ARENA` checkpoints show it as `free`.
- Untagged arena commitment depends on libc buffering and allocation order; across
  toolchains or small changes compare tagged ledgers, not the arena.
- Debug `PT_LOAD` depends on the SDK install path (ps2sdk's inline `packet2_*` asserts
  embed `__FILE__`; a deeper path measured 256 bytes more). Compare debug sizes within one
  install only; release builds match across installs.
- `make test-headless-retail` boots production with the retail containers and prints
  `[D3BFG] MEMORY`/`ARENA`/`HEAP` ledgers (and `TAG` in debug) after `startup-foundation`,
  `startup-decls` and `startup-game`; an allocation failure lists every live tag first (debug).
  Use these to judge any budget change.
- Release builds keep no requested sizes or per-tag ledgers: their `requested` is the backing.
  Compare requested bytes and tags in debug; backing and counts in either.
- Count allocations, not only requested bytes. Under dlmalloc, per-allocation overhead was
  3.32 MB (25 % of backing) at retail startup. The port's heap (2026-11-17,
  [HEAP_ALLOCATOR.md](../../docs/plans/HEAP_ALLOCATOR.md)) has no small-allocation header: 94 %
  of allocations are 256 bytes or less and take a slot of the nearest of 48 classes.
- Nothing allocates before `main` (2026-11-17): every image starts at 0 / 0 / 0, and full
  shutdown must return to it exactly. A global whose constructor allocates gets
  `ps2::DeferredObject` storage that its owner constructs and destroys (`deferred_object.h`).
  Warm map/movie shutdown must be exact too.
- Warm native dictionary string pools (and fonts/images, which stay cached until UI
  shutdown) once before comparing repeated-reload ledgers, then require exact stability.
  The retail world gate cycles mars_city1 this way (`ps2_memory cycleN` after `clearHistory`:
  id's console history keeps a growing buffer per HostFs line); after the warm cycle the ledger
  holds exactly (debug, 2026-11-26: 2,328,449 / 2,354,316 bytes, 3,399 allocations, the
  `ps2_keyboard` cvar's 144 bytes more than 2026-11-25's).
  Clear queued console history before a final release checkpoint (its echo adds a
  transient 64-byte string). Real-time animation leaves a few KB of varying script state.
- Production loads the host-cooked script program (2026-11-18): `SCRIPT` at retail
  `startup-game` is 1.62 MB in 5 allocations debug (196 KB of it the debug-only lines), against
  4.93 MB in 33,714 when the compiler ran at boot. Ledgers from 2026-10-30 to 2026-11-17 hold
  the compiler's state (its 3,515,712-byte fixed arrays on `TAG_SCRIPT`; before 2026-10-30
  those were in `PT_LOAD`); `make SCRIPT_CHECK=1` still builds that path for an A/B.

## Current picture

- Retail boot (debug, 2026-10-20): startup ends at 10,148,418 requested / 13,473,216
  backing bytes in 75,748 allocations; peak 10,582,854 / 13,910,756, arena 14,523,035.
  With the kernel's first MiB and 11.17 / 11.60 MB of `PT_LOAD` then, 6.8 MB debug /
  6.4 MB release remained before any map, stacks and load peaks; at today's `PT_LOAD` that is
  about 8.6 / 9.4 MB. With the script storage on the heap and the multiplayer code gone
  (2026-10-30), retail `startup-game` is 13,600,574 / 16,900,752 bytes in 75,116
  allocations (debug); with 16-bit event maps and no developer cvars (2026-10-31) it is
  13,240,094 / 16,541,204 in 75,111 debug and 13,204,286 / 16,482,064 in 74,564 release.
  On the port's own heap (2026-11-17) the same boot is 13,231,527 / **13,434,232** in 75,081
  (debug): 3.10 MB less backing, with 172,912 bytes of tracking shadows on top; arena peak
  14,425,296 against 17,581,068. Release: **13,361,680** backing in 74,542 (3.12 MB less), arena
  peak 14,171,520, 14.3 MB of gap left at `startup-game`. With the cooked script program
  (2026-11-18): **9,810,624** backing in 35,245 debug, **9,541,700** in 34,710 release
  (−3.62 / −3.82 MB), arena committed 10,167,536 / 9,835,856. With declaration text read on
  demand (2026-11-24): **5,389,144** backing in 22,789 debug, **5,156,104** in 22,252 release
  (−4.42 / −4.39 MB), arena committed 5,711,728 / 5,423,040; the heap's gap at `startup-game` is
  21.6 MB debug.
- Boot owners (backing; dlmalloc figures, before the heap): compressed declaration text 4.76 MB (kept resident by
  `USE_COMPRESSED_DECLS`, 11,947 declarations; read on demand since 2026-11-24, none resident), script 6.34 MB (1.62 MB since the cooked program; 3.52 MB of it the fixed
  program storage), declaration objects
  1.78 MB, strings 1.23 MB, container file tables 1.08 MB, smoke particles 0.68 MB;
  event callback maps 126 KB (0.64 MB before they became 16-bit pool indexes).
- Production `PT_LOAD` (2026-10-31): 4,779,884 debug / 4,063,468 release bytes, from the
  `--gc-sections` runnable link, `-O2` without alignment padding, `-Os` engine cold sources,
  heap script storage, smaller console/command/scratch buffers, no multiplayer code, and no
  developer commands, debug drawing, developer prints or developer cvars in release
  (11.45 / 11.91 MB before). `.text` is 3.81 / 3.19 MB and `.bss` 0.60 / 0.58 MB; stripped
  ELFs 4.18 / 3.48 MB. The retail program has 95,386 statements and 3,070 functions; its
  cooked image is 1,531,584 bytes (1.22 MB resident). `report.json` keeps the gate link's `PT_LOAD` too; compare the runnable
  figure. Buffers still waiting on measurements (entity, event, input-device and GUI
  register capacities) are listed in the reference's "Smaller static buffers" section.
- Latest UI ledgers (debug, NTSC = PAL; requested / backing / allocations, 2026-10-24):
  menu opened 10,938,718 / 11,646,016 / 15,984; Settings screen 11,266,366 / 12,006,908 /
  16,737; menu peak 11,304,182 / 12,050,964 (arena 12,206,803); menu release 5,212,391 /
  5,326,988 / 2,733; quit run peak 11,358,206 / 12,114,412; dialog at rest 4,380,385 /
  4,525,300 / 3,447; font screen warm 2,727,335 / 2,832,204 / 2,537; visible console peak
  2,440,512 / 2,539,056. Executing shell peaked at about 11 MB earlier (2026-10-23).
- Cooked retail world (2026-11-08): mars_city1's whole D3WD is 2,739,728 bytes (1,634,704 of
  VU1 packets, the rest the s16 CPU mirror, portals and 9,783 nodes), loaded whole into one
  `TAG_RENDER` allocation; the gate's views peak at 1,536 bytes of frame arena. Its diffuse
  textures (`mars_city1.d3tx`, 2026-11-08) are 1,976,832 bytes of VRAM for the whole map, more than
  640-wide 32/32 leaves (1.27 MB NTSC), so they live in an LRU arena (1,064,960 bytes at 640 NTSC
  32/32; the area 2 views use 517 KB). With the class A lightmaps (2026-11-08) the D3WD is 3,229,424
  bytes (chart seams split vertices; 3,055,840 before the static `func_static` models merged into
  the areas) and the D3TX 2,612,672 (8 pages, 532,480 bytes of VRAM sharing
  the arena), both whole in EE RAM (TAG_RENDER), plus 2 x 192 bytes of state blocks per material
  and 2 x 64 per page. With material passes (2026-11-21) the D3WD is 3,234,032 bytes (313
  materials with the parm variants) and the D3TX 2,713,792 (2,063,616 bytes of diffuse and pass
  VRAM), plus 2 x (192 + 64) bytes of blocks per pass. (Since 2026-11-25 the arena shares the UI's
  256 KiB: 1,327,104 bytes NTSC, 917,504 PAL at 640 wide, 32/32.) PAL's 640-wide 32/32 arena (655,360
  bytes) is 99.8 % full with the north view's diffuse textures and pages, so its glows drop. With
  the sky (2026-11-23) the D3TX is 2,810,944 bytes: six 128x128 faces, 95,488 bytes of VRAM, which
  PAL's north view leaves out too; the residency plan adds 4 bytes and a range per texture. At
  `sky_extent` 256 (mars_city1's job since 2026-11-24) the faces take 332,288 bytes and the D3TX is
  3,047,744; NTSC still evicts nothing, PAL's north views still drop the sky and glows. Streamed
  since 2026-11-24: only the D3TX's tables (12,504 bytes) and the copies of the textures that areas
  near the view need stay in EE memory (2 MiB budget). The retail gate's copies peaked at 1,213,888
  bytes, against the whole 3,047,744. With the static mesh props merged (2026-11-25) the D3WD is
  4,494,064 bytes (83,659 triangles, still loaded whole) and the streamed D3TX 4,040,832 (382
  textures, 3,351,552 bytes of VRAM); copies peak at 1,388,928. With the GUI and skinned props too:
  D3WD 4,959,312 (94,805 triangles), D3TX 4,653,056 (423 textures, 3,945,472 of VRAM), copies peak at
  1,722,432. Every PAL view now overflows its arena and loads the rest mid-pass (gs-renderer.md),
  uploading close to 1 MB a frame; NTSC's north views overflow slightly. With the rigid entities at
  rest (doors, movers): D3WD 5,365,984 (103,063 triangles), D3TX 5,020,800 (437 textures), copies peak
  at 1,737,088. With chroma subdivision (2026-11-25): D3WD 5,777,200 (118,427 triangles). With translucent
  stages (D3TX v5): D3TX 5,290,176 (468 textures, 4,563,200 bytes of VRAM, 108 passes).
- Measured map baseline (authored map, 2026-10-11): fixed `PT_LOAD` 10.9 MB, three 60-frame
  reloads about 2.22 MB requested; no retail map transition peak is established. mars_city1
  `.map` is 5.48 MB (4.85 MB brush/patch text), its container holds 5.4 MB of binary
  collision and 2 MB of AAS.
- Device/UI caps: two 128 KiB chain rings (one per in-flight frame, `chainRingBytes`; the list
  itself is built in the scratchpad), 2 MiB owned RGBA pixels and 2 MiB texture
  VRAM by default; one 2 MiB texture owner for converted retail images (images keep all
  pages resident, nothing streams per frame; CLUT textures charged in 8 KiB PSMCT32 pages).
  PSMCT16 color plus Z16 take 1,376,256 NTSC / 1,572,864 PAL bytes; the 32-bit profile
  (PSMCT32 plus Z16S, production's default) 2,293,760 / 2,621,440, capping the texture range at
  1,900,544 / 1,572,864. Production's default is 640 wide (2026-11-06): NTSC at 32 bits takes
  2,867,200 and leaves 1,327,104, PAL 3,276,800 and 917,504; the front end's 32/16 profile takes
  2,293,760 / 2,621,440 and leaves 1,900,544 / 1,572,864, which hold the retail menus; FRAME mode's half-height buffers pad to whole page rows
  (`renderer_capture.fixed_vram_bytes`). NTSC holds the whole retail UI; PAL evicts on screen changes (11 in
  the UI session). UI collection owns 4,096
  vertices, 6,144 indices and 128 runs (146,944 bytes); the depth-mask capture buffers add
  9,216 bytes; a drawn movie with screen placements adds three channel CLUT textures
  (72 KiB VRAM for the shell). Loading a texture briefly holds its largest page encoded
  and decoded (at most about 384 KB). Main stack reserve is 256 KiB (`heap::kMainStackBytes`); the
  retail menu session peaks at 154,376 bytes (2026-11-17). Visible packages also
  hold id's frame arena, one 128 KiB `TAG_RENDER` block from presentation start (2026-11-05;
  a graybox view uses 1,248 bytes), and a loaded cooked world its whole D3WD.
- Glyph sets and charset: Sarasori's pre-rendered set is 18 sizes on two PSMT4 pages,
  cooked for 640 wide (176 KiB VRAM; 136 KiB at 512's sizes); the console charset is one 256x256 PSMT4 page (32 KiB). The UICT4
  catalog is 74,499 bytes and every visible package pays it. Eight prompt icons add about
  98 KB of pixels, kept warm after a drawn movie releases.
- Hot PS2 structure: sound voice pool is 48 slots in one 7,296-byte AUDIO allocation
  (logical, not SPU2); the 256 KiB per-sample cap is a fixture limit, not a sound budget.

## Recurring lessons

- `R_IdentifySilEdges` used `MAX_SIL_EDGES` as list/hash granularity and reserved
  multi-megabyte scratch for tiny models (8.2 MB peak, about 12 MB arena). PS2 scratch is
  bounded by `max(1, min(numIndexes, MAX_SIL_EDGES))`.
- `_ordered.resources` is streamed, not read wholesale (it was 11.3 MB).
  `idResourceCacheEntry` holds a 256-byte `idStrStatic` name, about 300 bytes for each of
  3,598 entries (1.08 MB of tables).
- `idGameLocal::Init` preallocates `idSmokeParticles` (680 KB) before any map; most
  declaration text arrives during game startup.
- `ClearDictionary` alone retained 1.6 MB above baseline; use the explicit full-release
  seam at shutdown. Class `memused` counts object bytes only; heap metadata supplies
  unsized-delete accounting.
- Debug conversion scratch (collision `.cm` writing) determines map-fixture peaks, and
  completed probe bodies keep their ownership until map shutdown.
- Shrinking retail UI art is the real lever: paletted PSMT8/PSMT4 with one CLUT per atlas
  image, halved where never enlarged, English-only 16-level fonts (gs-renderer.md has the
  quality rules). RGBA32 pages would not fit GS memory beside the framebuffers.
- Arena commitment can move by allocation order alone (the dialog's rose 160,000 bytes
  while requested peaks stayed within 15 KB).
- Quake II reference: one program-wide dlmalloc heap with per-tag accounting and
  `MemTag::ElfSys` booking kernel, ELF and stack so tags sum to 32 MB; dlmalloc's page
  size is pinned to 4096 because `sysconf` fails (see [ps2-platform.md](ps2-platform.md)).
  Doom replaced dlmalloc with its own heap on 2026-11-17.

## Where the history is

All in [docs/reference/memory-budget.md](../../docs/reference/memory-budget.md), by dated
heading:

| Section | Covers |
| --- | --- |
| Doom foundation baseline | core, Common, audio, codec and game-fixture ledgers (2026-10-10/11) |
| Doom authored native map baseline | map/world/PVS/AAS ledgers, silhouette-edge scratch |
| Doom 2D device budgets | GS device caps, indexed UI pools, authored SWF/font ledgers |
| Retail UI host inventory, font screen, inactive dictionaries, timelines | 2026-10-16/18 retail UI slices |
| Native SWF additive blend, convex UI mask budget | 2026-10-18 |
| Toolchain rebuild comparison | 2026-10-19 |
| Original dialog full draw, Retail boot and UI formats, Converted retail UI textures | 2026-10-20 |
| Pre-rendered menu glyphs, Retail console charset | 2026-10-22 |
| SWF draw features, Glyph rectangles, Native shell menu (+ key input) | 2026-10-23 |
| Quit dialog, Pad prompt icons, Booting into the menu, Campaign and Settings | 2026-10-24 |
| Authored UI removal | 2026-10-29 |
| Executable size, Script program storage on the heap, Smaller static buffers, Multiplayer code compiled out, Release console commands, Debug-only tools, BSS cuts and developer cvars | section GC, `-O2`/`-Os` experiments; script arrays moved to `TAG_SCRIPT`; console/command/scratch buffers and the measurement backlog; multiplayer strip by step; release command set (2026-10-30); debug-only tools and developer prints, BSS cuts, event maps and developer cvars (2026-10-31) |
| Cooked script program | 2026-11-18: what the compiler left resident against what the image keeps; same-day compile-at-boot and cooked ledgers, debug and release |
| Declaration text on demand | 2026-11-24: retail ledgers before and after, debug and release; the read cost |
| Retail map game data | 2026-11-27: mars_city1's container, cooked entities, collision and AAS step by step (M3.1); the text `.map` parse it replaced |
| Quake II reference | allocator and accounting notes |
