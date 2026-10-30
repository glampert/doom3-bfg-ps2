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
  `ps2::system::heap`), dlmalloc arena commitment, and fixed `PT_LOAD` (ELF, BSS). Tagged
  backing lives inside the arena: never add it on top. Kernel (first MiB), stacks, VRAM
  and GS buffers are separate again.
- Untagged arena commitment depends on libc buffering and allocation order; across
  toolchains or small changes compare tagged ledgers, not the arena.
- Debug `PT_LOAD` depends on the SDK install path (ps2sdk's inline `packet2_*` asserts
  embed `__FILE__`; a deeper path measured 256 bytes more). Compare debug sizes within one
  install only; release builds match across installs.
- `make test-headless-retail` boots production with the retail containers and prints
  `[D3BFG] MEMORY`/`ARENA`/`TAG` ledgers after `startup-foundation`, `startup-decls` and
  `startup-game`; an allocation failure lists every live tag first. Use these to judge any
  budget change.
- Count allocations, not only requested bytes: per-allocation overhead is 3.32 MB (25% of
  backing) at retail startup (tagged header, up to 15 bytes of alignment slack, dlmalloc
  rounding).
- Full shutdown must recover the exact pre-boot baseline: 7,424 / 7,600 bytes in 4
  allocations (production). Warm map/movie shutdown must be exact too.
- Warm native dictionary string pools (and fonts/images, which stay cached until UI
  shutdown) once before comparing repeated-reload ledgers, then require exact stability.
  Clear queued console history before a final release checkpoint (its echo adds a
  transient 64-byte string). Real-time animation leaves a few KB of varying script state.
- The script program's fixed arrays (3,515,712 bytes: statements, functions, globals and
  their defaults) are `TAG_SCRIPT` heap allocations from `BeginCompilation` to `FreeData`
  (game init to game shutdown), not BSS. Ledgers before 2026-10-30 count them in `PT_LOAD`
  instead, so add 3.5 MB to older startup/UI ledgers when comparing.

## Current picture

- Retail boot (debug, 2026-10-20): startup ends at 10,148,418 requested / 13,473,216
  backing bytes in 75,748 allocations; peak 10,582,854 / 13,910,756, arena 14,523,035.
  With the kernel's first MiB and 11.17 / 11.60 MB of `PT_LOAD` then, 6.8 MB debug /
  6.4 MB release remained before any map, stacks and load peaks; at today's `PT_LOAD` that is
  about 8.6 / 9.4 MB. With the script storage on the heap and the multiplayer code gone
  (2026-10-30), retail `startup-game` is 13,600,574 / 16,900,752 bytes in 75,116
  allocations (debug).
- Boot owners (backing): compressed declaration text 4.76 MB (kept resident by
  `USE_COMPRESSED_DECLS`, 11,947 declarations), script 6.34 MB (3.52 MB of it the fixed
  program storage), declaration objects
  1.78 MB, strings 1.23 MB, container file tables 1.08 MB, smoke particles 0.68 MB,
  event callbacks 0.64 MB.
- Production `PT_LOAD` (2026-10-30): 5,017,324 debug / 4,441,260 release bytes, from the
  `--gc-sections` runnable link, `-O2` release, `-Os` engine cold sources, heap script
  storage, smaller console/command/scratch buffers, no multiplayer code and no developer
  commands in release (11.45 / 11.91 MB before). `.text` is 3.92 / 3.41 MB and `.bss`
  0.73 / 0.72 MB. Retail scripts use 95,386 of 131,072
  statements. `report.json` keeps the gate link's `PT_LOAD` too; compare the runnable
  figure. Buffers still waiting on measurements (entity, event, input-device and GUI
  register capacities) are listed in the reference's "Smaller static buffers" section.
- Latest UI ledgers (debug, NTSC = PAL; requested / backing / allocations, 2026-10-24):
  menu opened 10,938,718 / 11,646,016 / 15,984; Settings screen 11,266,366 / 12,006,908 /
  16,737; menu peak 11,304,182 / 12,050,964 (arena 12,206,803); menu release 5,212,391 /
  5,326,988 / 2,733; quit run peak 11,358,206 / 12,114,412; dialog at rest 4,380,385 /
  4,525,300 / 3,447; font screen warm 2,727,335 / 2,832,204 / 2,537; visible console peak
  2,440,512 / 2,539,056. Executing shell peaked at about 11 MB earlier (2026-10-23).
- Measured map baseline (authored map, 2026-10-11): fixed `PT_LOAD` 10.9 MB, three 60-frame
  reloads about 2.22 MB requested; no retail map transition peak is established. mars_city1
  `.map` is 5.48 MB (4.85 MB brush/patch text), its container holds 5.4 MB of binary
  collision and 2 MB of AAS.
- Device/UI caps: two 128 KiB GS command halves, 2 MiB owned RGBA pixels and 2 MiB texture
  VRAM by default; one 2 MiB texture owner for converted retail images (images keep all
  pages resident, nothing streams per frame; CLUT textures charged in 8 KiB PSMCT32 pages).
  PSMCT16 color plus Z16 take 1,376,256 NTSC / 1,572,864 PAL bytes. UI collection owns 4,096
  vertices, 6,144 indices and 128 runs (146,944 bytes); the depth-mask capture buffers add
  9,216 bytes; a drawn movie with screen placements adds three channel CLUT textures
  (72 KiB VRAM for the shell). Loading a texture briefly holds its largest page encoded
  and decoded (at most about 384 KB). Main stack reserve is 128 KiB.
- Glyph sets and charset: Sarasori's pre-rendered set is 18 sizes on two PSMT4 pages
  (136 KiB VRAM); the console charset is one 256x256 PSMT4 page (32 KiB). The UICT4
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
| Executable size, Script program storage on the heap, Smaller static buffers, Multiplayer code compiled out, Release console commands | section GC, `-O2`/`-Os` experiments; script arrays moved to `TAG_SCRIPT`; console/command/scratch buffers and the measurement backlog; multiplayer strip by step; release command set (2026-10-30) |
| Quake II reference | allocator and accounting notes |
