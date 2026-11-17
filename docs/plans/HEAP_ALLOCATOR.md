# Plan: a PS2-specific heap to replace dlmalloc

## Context

Every allocation on the EE goes through `ps2::system::heap` (`src/ps2/system/heap.cpp`). That layer puts a
20-byte `Header {base, requested, backing, magic, tag}` in front of a dlmalloc 2.7.2 chunk and pads every
request to 16 bytes. dlmalloc adds 4 bytes per chunk, rounds to 8 bytes and has a 16-byte minimum chunk.
newlib's `malloc` and `_malloc_r` functions also go to dlmalloc (`src/external/dlmalloc/dlmalloc.c`).

At retail boot this costs **3.32 MB of overhead across about 75,700 allocations**: 25% of the backing
memory, about 44 bytes per allocation. Most of these allocations are tiny. SCRIPT alone makes 33,714 of them,
averaging about 41 requested bytes, and DECLTEXT, DECL and STRING look similar. The level envelope in
RENDERING_ARCHITECTURE §16.1 is already 0.3 to 1.3 MB over the available headroom before collision, AAS or
entities are counted, so this overhead is the largest saving that needs no change to id's data.

**Goal:** a dlmalloc-inspired allocator owned by the backend:
- It takes all of sbrk's memory once.
- Small requests come from headerless fixed-size slot pages with free lists.
- Large requests come from an O(1) TLSF heap with an 8-byte header.
- Alignment is the request's natural one, 4 to 16 bytes, or up to 64 on request.
- Freed memory is poisoned with a fixed pattern in debug builds only.
- Every check exists in debug (assertion) builds only.
- Handle-based relocatable arenas for streamed assets are designed here and built when area streaming starts
  (R2).

**Decisions from the user**
- Per-tag ledgers exist in debug builds only, kept in the allocator's own records. Release keeps exact backing bytes and
  counts per tier, plus totals.
- The relocatable arena is designed now and built later. It is called `RelocatableArena`, in
  `src/ps2/system/relocatable.{h,cpp}`.
- `new` of size zero is a bug: assertion builds stop on it. `Mem_Alloc(0)` keeps returning NULL.
- Allocations before `main` become a hard error once the current four are removed. libc's own start-up
  allocations are exempt (see Arena layout).
- `GetRequestedSize` and `idClass::memused` become debug-only. The only reader of `memused` is
  `idClass::DisplayInfo_f`, the `game_memory` command, which is already registered only under
  `#if !defined( ID_PS2 ) || PS2_D3BFG_DEBUG` (`src/neo/d3xp/gamesys/SysCmds.cpp:2332`).
- `Mem_Alloc` keeps its 16-byte guarantee. An audit moves the callers that do not need it to an explicit
  allocation with a lower alignment.
- The allocator, the ledger and the debug tracking are classes. `heap.cpp` holds them as `static constinit`
  singletons, so no state is scattered across file-level variables.

## Constraints found in the code

**Free path**
- Every free path calls `heap::Free(p)` without a size or a tag: `Mem_Free`/`Mem_Free16`, all tagged deletes,
  `idClass::operator delete`, plain `delete[]` on `new (TAG) T[]`, and backend code.
- Pointers from different families are freed through each other. The size must come from the address alone.

**Alignment needs**
- `Mem_Alloc` is an inline alias of `Mem_Alloc16` (`src/neo/idlib/Heap.h:61`).
- These assert 16-byte alignment: idBlockAlloc (`Heap.h:395`), the ODS arrays, `idDrawVert` arrays, VecX/MatX
  and the Lcp paths.
- The backend's `lq`/`sq` helpers (`src/ps2/qwords.h`) silently drop the low four address bits. A buffer that
  is not 16-aligned therefore corrupts data without faulting.
- DMA buffers ask for 64 explicitly: the chain rings, VU chains, texture pixels, D3WD/D3TX and the world state
  blocks.
- No SIF DMA target uses the heap.

**Type alignment on the EE**
- `__STDCPP_DEFAULT_NEW_ALIGNMENT__` and `__BIGGEST_ALIGNMENT__` are both 8.
- `ld`/`sd` on an address that is not 8-aligned raises an address error.
- Over-aligned types reach the `align_val_t` forms of `new` (`decal_t`).

**Start-up order** (ps2sdk `startup/src/crt0.c:85-118`)
1. `SetupHeap(&_end, _heap_size)`
2. The weak hook **`_ps2sdk_memory_init()`** (`:24`, `:98`)
3. `_libcglue_init()`, which may malloc through pthread or stdio setup
4. Global constructors
5. `main`

ps2sdk's `_sbrk` (`libcglue/src/glue.c:785`) bumps a pointer from `&_end` up to `EndOfHeap()`, which sits below
the 128 KiB main stack. That leaves about 24.4 MB (debug) or 25.0 MB (release) for the heap. There is one
engine thread and no locking.

**Today's pre-main allocations**
- There are four tagged ones, 7,424 requested / 7,600 backing bytes, from native globals. They are not yet
  identified.
- `src/tests/smoketests/heap_tests.cpp:25-42` adds a fifth on purpose (`allocation-before-main`).

**Gates**
- Many gates compare exact `(requested, backing, count)` tuples, release included:
  - `BASELINE = (7424, 7600, 4)` in `src/tools/scripts/run_console.py:16`, used by four runners
  - five host fixture logs
  - the on-target `reload-ledger` checks
- With no pre-main allocations the baseline becomes **(0, 0, 0)** in every build.

**Streamed data**
- DMA chains REF the D3WD packets and the D3TX streams in place.
- `tex::Image::stream` is a raw pointer.
- In-flight chains read the double-buffered state blocks.
- So relocation is safe only at a `dma::WaitIdle` fence, or for blocks no in-flight chain references.

## Design

Phase 0's measurements (docs/reference/memory-budget.md, 2026-11-17) settled the class table, the cutoff
and the metadata layout below. The first draft had 40 classes up to 512 bytes, a 75 KB descriptor table
and a 1 MB debug side table; each lost to the alternatives here.

### Code structure

All of this lives in namespace `ps2::system::heap`. Every class has a `constexpr` constructor and does
nothing until `Init`, so a `constinit` singleton has no static-initialization order to get wrong.

| File | Contents |
| --- | --- |
| `heap.h` | Public API (`TryAlloc`, `Alloc`, `Free`, `GetTotalStats`, `GetLibcStats`, `GetArenaStats`, `ResetPeaks`, `EnterMain`), `kNaturalAlignment`, and the tracking-only `GetStats(tag)` and `GetRequestedSize` |
| `heap_allocator.h` | `class Allocator` with the small tier's inline fast paths and the `constexpr` class table. It has no EE dependencies, so the host tests build their own instance on a buffer |
| `heap_allocator.cpp`, `heap_small.cpp`, `heap_large.cpp` | Arena setup, realloc and statistics; the small tier's slow paths (pages, frontier, tracking); the TLSF tier |
| `heap.cpp` | `constinit static Allocator s_allocator; constinit static Ledger s_ledger;` (`Ledger` is a class local to the file), the public API, every `operator new`/`delete` form, the C/newlib entry points (`malloc`, `free`, `calloc`, `realloc`, `memalign`, `posix_memalign`, `aligned_alloc`, `valloc`, `cfree`, `malloc_usable_size`, `_malloc_r` and the rest) and `_ps2sdk_memory_init` |
| `relocatable.{h,cpp}` | `class RelocatableArena` (R2) |

`src/external/dlmalloc/` is deleted. The first draft's separate `Tracker` class went away once tracking moved
into the allocator's own records (below).

### Arena layout (one land grab)

The heap is set up in **`extern "C" void _ps2sdk_memory_init()`**:
- It runs after crt0's `SetupHeap` and before libcglue and every constructor.
- It calls `s_allocator.Init(sbrk(0), EndOfHeap())` after one `sbrk` of the whole range, and never calls sbrk
  again.
- The allocation fast path needs no lazy-init branch. Host tests initialise a static 32 MiB arena on first use.

`main` calls `heap::EnterMain()`, which records the owner thread and closes the pre-main window. In tracking
builds, any **tagged** allocation (`heap::Alloc`, `Mem_*`, `operator new`) made before `EnterMain()` is fatal
and logs `__builtin_return_address(0)` for addr2line. The libc entry points are allowed before main, because
libcglue's start-up may need them; they are charged to the libc `Stats`.

```
arena start (page-aligned)                                         EndOfHeap()   stack
 | large tier (TLSF) grows up ->          | gap |          <- small pages grow down |
                                          ^ m_largeTop     ^ m_smallLow
```

- **Small tier** pages are taken downward from the top. A released page sets its bit in a free-page bitmap; if
  it is the lowest page, the frontier rises past it and every free page right above it.
- **Large tier** (TLSF) owns `[start, m_largeTop)`. A free block that ends at the top always merges back into
  the gap, like dlmalloc's `top`, so the top block is always in use.
- **When the gap is spent**, the small tier borrows a page-aligned 4 KiB block from TLSF; it goes back to TLSF
  as soon as it is empty.
- **Tier dispatch** on free is one bit test: a 1 KiB bitmap marks every page holding slots, wherever it sits.
- **The main stack** sits right above the arena's top, so an overflow lands on the first small pages. The
  heap sets `_stack_size` (256 KiB, measured 154 KB peak in the retail menus) and tracking builds paint the
  stack and check its bottom 1 KiB every Common frame (2026-11-17).

### Small tier: headerless slot pages (up to 256 bytes)

**Size classes**, 48: 4 to 128 bytes in steps of 4, then 136 to 256 in steps of 8. On the retail boot's live
set, 94 % of allocations are 256 bytes or less, and 41 % are exactly 29 to 32 (idStr buffers).

**Pages and alignment**
- Each page is one 4 KiB span of one class. A 16-byte header comes first and slot *i* sits at
  `page + 16 + i × size`, so a class is aligned to its size's lowest set bit, capped at 16.
- The class comes from `constexpr` tables `forSize[alignLevel][(size + 3) >> 2]` for alignments of 4, 8 and
  16. Each entry is the smallest class that is big enough and aligned enough.
- Requests aligned to 32 or more go to the large tier.

**Page header**, 16 bytes: `u16 freeHead` (a page offset, 0 for none), `u16 used`, `u8 class`, `u8 flags`,
`u16 prev`, `u16 next` (the class's partial list, as page indices), and in tracking builds the arena offset
of the page's shadow array. A header per page costs only for pages in use, about 55 KB less than a table for
every reachable page.

**Free list:** a `u16` page offset stored in the slot itself. No header, and no division on free. A new
page threads all its slots at once.

**Page policy**
- Each class allocates from the head of its partial list. A full page leaves it; a page that gains a free
  slot rejoins at the head.
- A page that becomes empty is kept listed if it is the class's only empty page. Of two, the higher one stays
  and the lower one is released, so low pages free up and the frontier can rise.
- A new page takes the highest free page first, then the gap, then a borrowed block.

**Release fast paths** (inline in `heap_allocator.h`)
- Alloc: a table lookup, a head load, a pop and two counter updates.
- Free: a bitmap test, a page mask, a push and two counter updates.

### Large tier: TLSF (above 256 bytes, or alignment of 32 or more)

- Two-level segregated fit: O(1), with boundary tags as in dlmalloc. There are exact 16-byte bins below
  256 bytes and 16 sub-bins per power of two above, up to 32 MB. The bitmaps are `u32`.
- `__builtin_clz` is a libgcc call on the EE, so `ps2::CountLeadingZeros` in `src/ps2/common.h` uses `plzcw`
  (plus an `sll` that sign-extends the result), with `__builtin_clz` on the host.
- The header is 8 bytes in release: `prevPhys` and `size|free|prevFree`, as arena offsets so the layout is the
  same on the host. A free block keeps its links 8 bytes in (the payload in release, the header's tracking
  fields otherwise), so 16 bytes hold one. Sizes are multiples of 16 and payloads are 16-aligned. Every
  remainder can therefore split off, and a block's size depends on its request alone: the gates compare warm
  reload ledgers exactly.
- An alignment of 32 or more searches for `size + align + minimum block` and splits off the leading gap, as
  `dlmemalign` does.
- Realloc grows a block in place into a free neighbour or the gap, and shrinks by splitting. Only newlib uses
  realloc.
- The search rounds a request up to the next bin (good fit), so a block of exactly the requested size in the
  same bin can be skipped while a larger bin or the gap has room.

### Alignment policy

`heap::Alloc(size, tag, alignment = kNaturalAlignment)`:
- Natural alignment is `min(lowbit(round_up(size, 4)), 16)`. It is safe for any `T` with
  `sizeof(T) == size`, because `alignof(T)` divides `sizeof(T)`.
- It costs nothing over 4-byte alignment: a size that is a multiple of 16 lands in a 16-aligned class anyway.

| Entry | Alignment | Size 0 |
| --- | --- | --- |
| global and tagged `new` / `new[]` (forms without `align_val_t`) | natural | assertion builds stop; release takes the 4-byte class without checking |
| `align_val_t` forms | as asked | same |
| `Mem_Alloc`, `Mem_ClearedAlloc`, `Mem_Alloc16`, `R_StaticAlloc` | **16** (id's contract, kept) | returns NULL |
| `Mem_AllocAligned(size, align, tag)` (exists, PS2-only, `Heap.cpp:40`), new `Mem_ClearedAllocAligned` | as asked, `MEM_ALIGN_NATURAL` allowed | returns NULL |
| `malloc`/`calloc`/`realloc`, newlib `_r` | `max(8, natural)`: C callers allocate header-plus-payload structs | C semantics |

**The `Mem_Alloc` audit** covers the units built by `config/sources.mk`: about 100 `Mem_Alloc`, 95
`Mem_ClearedAlloc` and 75 `R_StaticAlloc`/`R_ClearedStaticAlloc` sites tree-wide, fewer once unbuilt units
are dropped.
- **Moves to an explicit lower alignment** (natural or 4, with a `[PS2_D3BFG]` tag): sites whose data does not
  need 16. Examples are byte payloads such as DECLTEXT text and file buffers, `int` arrays, and `idList<T>`
  (`idListArrayNew` passes `alignof(T)`).
- **Stays on `Mem_Alloc`**: the rest. Unaudited code therefore stays safe by default. That covers
  `idDrawVert` and joint arrays, anything that reaches `lq`/`sq` or an ODS stream, and header-plus-payload
  structs.
- idBlockAlloc's 16-byte element rounding (`BLOCK_ALLOC_ALIGNMENT`) drops to `alignof(T)` under the same audit.

`Mem_AllocAligned` already has the signature the plan needs. The audit uses it rather than adding a
near-duplicate `Mem_AlignedAlloc`.

### Tracking builds: tags, checks and poison

Tracking follows `PS2_D3BFG_ASSERTS`, which is also defined on the host. It needs no side table: each tier
keeps the record in its own memory.

**Records**
- Small slots: each page's shadow array, allocated from the large tier, holds `{tag, slack}` per slot, where
  slack is the class size less the requested size (under 16, so 0xFF marks a free slot).
- Large blocks: the header grows to 16 bytes with `requested`, `tag` and a magic value.
- Together they give exact per-tag `Stats`, `requested` and `GetRequestedSize`. Shadows are about 2 bytes a
  slot, counted as `metadataBytes`.
- `class_alloc.cpp`'s `memused`/`numobjects` counting goes under `#if PS2_D3BFG_ASSERTS`: its only reader,
  the `game_memory` command, is debug-only (`SysCmds.cpp:2332`).

**Assertions** (`[[unlikely]]` fatals)
- a bad tag or alignment
- `new` of size 0
- a tagged allocation before `EnterMain()`
- a free of a pointer that is not live (double or foreign free), or of a C library pointer through
  `heap::Free` (and the reverse)
- page counters out of range
- an allocation from a thread other than the owner (`GetThreadId()`)

**Poison**
- On free, the payload is filled with `0xDEADBEEF` words; the slot's link and the TLSF links overwrite the first
  word or two. An odd pointer faults on its first word load.
- Fresh allocations are filled with `0xCDCDCDCD`.
- On reuse the poison is checked: every word of a small slot, and the first and last 256 bytes of a binned large
  block. A mismatch is fatal and names the address, size and word.

**Release**
- No checks. The ledger's underflow check is an assertion too.
- `Alloc` failure is fatal and `TryAlloc` returns null. A size beyond the arena returns null in every build.
- `calloc` overflow returns null with `ENOMEM`, which is C semantics and costs nothing.

### Ledgers and stats

- In release, `Stats` per tag and `requested` report **usable** bytes (the slot or block size). Totals of
  backing and count are exact in every build.
- `ArenaStats` gains tier detail:
  - small pages used, partial and free
  - TLSF free bytes and largest free block
  - the gap
  - metadata
- `lifecycle::PrintMemory` (`src/ps2/system/lifecycle.cpp:208`) prints the tier lines always, and the `TAG`
  lines in debug only.
- newlib allocations are counted in libc `Stats`, outside the tagged total, so stdio buffers held across
  shutdown cannot break "final == baseline".

### RelocatableArena (design only; built at R2)

**API** (`src/ps2/system/relocatable.{h,cpp}`, `ps2::system::heap::RelocatableArena`)
- `Init(bytes, tag)` takes one large-tier block per level or segment budget.
- `Handle Alloc(size, align)` returns a 16-bit index and a 16-bit generation.
- `void * Resolve(Handle)` gives a pointer that is valid until the next compaction.
- `Pin` / `Unpin` are used while DMA references a block.
- `Free(Handle)`.

**Compaction**
- Sliding: an 8-byte block header holds the handle index and size, and live, unpinned blocks are copied down.
- It runs from presentation at a frame boundary, after the fence for the chain that read the data.
- It is incremental, with a byte budget per frame. The budget comes from EE memcpy speed, roughly 1 MB per 5 to
  10 ms, to be measured.
- Each moved range is `SyncDCache`d before DMA.

**Client rules**
- Payloads hold offsets, not pointers. Owners re-resolve after compaction.
- Candidates, in order:
  1. `tex::Record.pixels`: already handle-based and fenced.
  2. D3TX segment dictionaries: `tex::Image::stream` becomes an offset.
  3. Sound PCM (`SoundSample::m_pcm`).
  4. Model batches.
- D3WD packets stay in the large tier until streaming splits them per area.

## Implementation phases

0. **Measure on the current allocator.** Done 2026-11-17.
   - Add a temporary debug histogram in `heap.cpp`: sizes in 4-byte buckets up to 1 KiB and log2 above, with
     alignment and tag. Dump it at `startup-game` (`make test-headless-retail`) and after a `test-map` load.
   - Log each pre-main allocation's return address to identify the four.
   - Record the results in `docs/reference/memory-budget.md`, then fix the class table, the cutoff and the
     tracker size.
   - This plan is `docs/plans/HEAP_ALLOCATOR.md`. Commit it with phase 0's measurements.
1. **Remove the pre-main allocations.** Done 2026-11-17: `ps2::DeferredObject` (`src/ps2/deferred_object.h`)
   holds `animationLib` (game Init to Shutdown) and the SWF sprite and text prototypes (first live movie to
   last), and File.cpp's dead write histogram is compiled out.
   - Make the four globals lazy, or give them static storage, with a `[PS2_D3BFG]` tag in id code.
   - Drop the `allocation-before-main` check from `heap_tests.cpp` and from `REQUIRED_CORE_CHECKS`
     (`src/tools/scripts/run_pcsx2_test.py:142`).
   - The baseline becomes (0, 0, 0). Runners stop hard-coding `BASELINE` and require 0/0/0.
   - Regenerate the fixture logs with their generators.
2. **Core classes, host-tested.** Done 2026-11-17 as `Allocator` (both tiers, `heap_allocator.h` and three
   sources), `Ledger` (in `heap.cpp`) and `ps2::CountLeadingZeros`; `make test-host` runs
   `heap_core_tests_0`/`_1` and `test_heap_core.py`. ASan manual poisoning of free slots was not added: the
   tracking build's poison checks catch writes after free, not reads.
   - `Allocator`, `SmallTier`, `LargeTier`, `Ledger`, `Tracker` and `CountLeadingZeros`.
   - Host harness `src/tests/host/heap_core_tests.cpp`, added to `HOST_HEAP_SOURCES` in the Makefile. It runs
     on its own `Allocator` over a buffer and covers:
     - randomized alloc, free and realloc against a reference model
     - every class and alignment
     - frontier exhaustion and recovery
     - TLSF coalescing
     - poison detection
     - ASan manual poisoning of free slots, proven by seeding defects
3. **Switch the EE over.** Done 2026-11-17. The core test image opens the heap in its stage provider's
   `InitTestServices`, since the platform image shares its main and links no heap.
   - `heap.cpp` on the singletons, `_ps2sdk_memory_init`, `EnterMain` from `src/ps2/system/main.cpp` and the
     test drivers' mains, and the libc entry points.
   - Delete `src/external/dlmalloc/` and the `HEAP_VENDOR_INCS` rules (Makefile:363-420).
   - In `config/sources.mk`, drop `CORE_C_SRC` (:255) and register the new files next to `heap.cpp`
     (:249, :595). Then run `make compiledb`.
   - Check with `nm` on the unstripped ELF that nothing else references `sbrk`/`_sbrk`, and that newlib's
     malloc objects are absent.
4. **Ledgers and checks.** Done 2026-11-17, except the EE expected-fatal probes: the host defect modes
   cover write after free and double free, while pre-main allocation and zero-size `new` have no target
   probe yet. No debug gate hit a zero-size `new`.
   - Tracker wiring, `PrintMemory`, and the debug-only `class_alloc` counting.
   - Update the heap and class-alloc smoke checks: natural alignment, tier boundaries, `requested == 37`
     (debug only), `invalid-requests` (assertion builds only).
   - Add debug expected-fatal probes for zero-size `new`, double free, write-after-free and pre-main
     allocation.
   - Fix any zero-size `new` the debug gates find, with `[PS2_D3BFG]`.
5. **Mem_Alloc audit.** Worth about 40 KB at the retail boot (phase 0), so it waits behind other levers.
   - Move the sites that do not need 16 to `Mem_AllocAligned`/`Mem_ClearedAllocAligned` one owner group at a
     time, then idBlockAlloc. Measure each step with `test-headless-retail`.
6. **Docs.** Done 2026-11-17.
   - `.claude/rules/memory-budget.md`: overhead and how to measure.
   - A dated before/after section in `docs/reference/memory-budget.md`.
   - `docs/DEVELOPMENT.md`'s third-party list.
   - README attribution: the quake2-ps2 dlmalloc wrapper is gone.
   - `.claude/rules/ps2-platform.md`: the dlmalloc page-size note.
7. **(R2)** Build `RelocatableArena` with `tex::Record.pixels`, then D3TX segments and sound.

## Expected outcome (phase 0's model; measured in phase 3)

- Small allocations drop from about 44 bytes of overhead to a few bytes of class rounding.
- Replaying the retail boot's live set gives 13,525,120 backing bytes against 16,538,096 today: about
  **3.0 MB more free EE RAM** at `startup-game`, with the pages perfectly packed.
- Measured (2026-11-17): debug 13,434,232 backing bytes for the same 13,231,527 requested in 75,081
  allocations, **3.10 MB less** than dlmalloc's 16,531,324, plus 172,912 bytes of tracking shadows; the arena
  peak fell from 17,581,068 to 14,425,296. Release 13,361,680 in 74,542 against 16,482,064 (**3.12 MB less**).
- Shorter alloc and free paths than the wrapper plus dlmalloc.

## Verification

- `make test-host`, which runs the new core harness.
- Debug: `test-renderer`, `test-common`, `test-game`, `test-map`, `test-headless`, `test-headless-retail` and
  `test-world`, then `test-ui`. `test-ui` takes about 8 minutes per standard; say so before starting it.
- Release runs of `test-headless-retail` and `test-map` only when asked.
- Compare `startup-game` MEMORY, ARENA and tier lines against 2026-10-31 (13,240,094 / 16,541,204 in 75,111,
  debug):
  - backing about 2.5 MB lower
  - counts down by the four pre-main allocations
  - warm reloads exactly stable
  - full shutdown at 0/0/0
- Microbenchmark: a performance.md test ELF replays a recorded allocation trace through dlmalloc plus the
  wrapper, then through the new heap, and compares instruction counts in PCSX2.
- CI: `make` and `make release` link cleanly under the strict `-Werror` set.
