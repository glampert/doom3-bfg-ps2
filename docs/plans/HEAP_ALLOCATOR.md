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
- Per-tag ledgers exist in debug builds only, kept in a side table. Release keeps exact backing bytes and
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

### Code structure

All of this lives in namespace `ps2::system::heap`. Every class has a `constexpr` constructor and does
nothing until `Init`, so a `constinit` singleton has no static-initialization order to get wrong.

| File | Contents |
| --- | --- |
| `heap.h` | Public API, unchanged in shape (`TryAlloc`, `Alloc`, `Free`, `GetStats`, `GetTotalStats`, `GetArenaStats`, `ResetPeaks`), plus `kNaturalAlignment` and debug-only `GetRequestedSize` |
| `heap_allocator.{h,cpp}` | `class Allocator`: arena, frontier, `Init(begin, end)`, `Alloc(size, align)`, `Free`, `Realloc`, `UsableSize`, `GetTierStats`. It owns a `SmallTier` and a `LargeTier` member and has no EE dependencies, so host tests build their own instance on a buffer |
| `heap_small.cpp`, `heap_large.cpp` | `SmallTier` (slot pages) and `LargeTier` (TLSF), declared in `heap_allocator.h` |
| `heap_ledger.{h,cpp}` | `class Ledger`: exact totals, tier counters, and libc `Stats` kept apart from the tagged totals. `class Tracker` (debug only): the side table of tag and requested size, live-pointer checks, and per-tag `Stats` |
| `heap.cpp` | `static constinit Allocator s_allocator; static constinit Ledger s_ledger;` and, in debug, `static constinit Tracker s_tracker;`. Also the public API, every `operator new`/`delete` form, the C/newlib entry points (`malloc`, `free`, `calloc`, `realloc`, `memalign`, `posix_memalign`, `aligned_alloc`, `valloc`, `cfree`, `malloc_usable_size`, `_malloc_r` and the rest) and `_ps2sdk_memory_init` |
| `relocatable.{h,cpp}` | `class RelocatableArena` (R2) |

`src/external/dlmalloc/` is deleted.

### Arena layout (one land grab)

The heap is set up in **`extern "C" void _ps2sdk_memory_init()`**:
- It runs after crt0's `SetupHeap` and before libcglue and every constructor.
- It calls `s_allocator.Init(sbrk(0), EndOfHeap())` after one `sbrk` of the whole range, and never calls sbrk
  again.
- The allocation fast path needs no lazy-init branch.

`main` calls `heap::EnterMain()`, which records the owner thread and closes the pre-main window.

In assertion builds, any **tagged** allocation (`heap::Alloc`, `Mem_*`, `operator new`) made before
`EnterMain()` is fatal and logs `__builtin_return_address(0)` for addr2line. The libc entry points are allowed
before main, because libcglue's start-up may need them. They are charged to the libc `Stats`.

```
_end                                                             EndOfHeap()   stack
 | page descriptors | large tier (TLSF) grows up -> | gap | <- small pages grow down |
                    ^ s_largeTop                          ^ s_smallLow
```

- **Page descriptors** come first in the arena: 12 bytes for each 4 KiB page the small tier could reach,
  about 75 KB.
- **Small tier** pages are taken downward from the top. An emptied page goes on a free-page list. If it was
  the lowest page, the frontier moves back up.
- **Large tier** (TLSF) owns `[descriptors end, s_largeTop)`. Its last block is a wilderness block that grows
  into the gap, like dlmalloc's `top`. A free block touching the top merges back into the gap.
- **When the tiers meet**, the small tier first takes a page-aligned 4 KiB block from TLSF, and the large tier
  first takes free small pages next to the frontier. Both are rare slow paths.
- **Tier dispatch** on free is a single compare: `p >= s_smallLow` means small, anything else is large.

### Small tier: headerless slot pages (up to 512 bytes; phase 0 tunes the cutoff)

**Size classes**, about 40:
- 4 to 64 bytes in steps of 4
- 72 to 128 in steps of 8
- 144 to 256 in steps of 16
- 288 to 512 in steps of 32

**Pages and alignment**
- Each page is one 4 KiB span of one class, and slot *i* sits at `page + i × size`.
- A class's slot alignment is `gcd(size, 4096)` capped at 16: class 24 gives 8, class 48 gives 16.
- The class comes from `constexpr` tables `s_class[alignLevel][(size + 3) >> 2]` for alignments of 4, 8 and
  16. Each entry is the smallest class that is big enough and aligned enough.
- Requests aligned to 32 or more go to the large tier.

**Page descriptor**, 12 bytes:
- `u8 class`, `u8 flags`
- `u16 used`
- `u16 freeHead`, a page offset, 0xFFFF for none
- `u16 bump`, for lazy carving
- `u16 prev`, `u16 next`, which link the class's partial-page list

**Free list:** a `u16` page offset stored in the slot itself. No header, and no division on free.

**Page policy**
- Each class allocates from the head of its partial list.
- A page that becomes full leaves the list.
- A page that becomes empty goes back to the page pool. Each class keeps one empty page cached so it does not
  thrash.
- Debug and release follow the same policy, so their layouts match.

**Release fast paths**
- Alloc: a table lookup, a head load, a pop and two counter updates.
- Free: a compare, a page index, a push and two counter updates.

### Large tier: TLSF (above 512 bytes, or alignment of 32 or more)

- Two-level segregated fit: O(1), with boundary tags as in dlmalloc. There are 16 sub-bins per power of two,
  the first level covers 512 B to 32 MB, and the bitmaps are `u32`.
- `__builtin_clz` is a libgcc call on the EE, so a new `ps2::CountLeadingZeros` in `src/ps2/common.h` uses
  `plzcw`, with a portable version for the host.
- The header is 8 bytes: `prevPhys`, and `size|free|prevFree`. A free block keeps its links in the payload.
  Sizes are multiples of 16, so payloads are 16-aligned.
- An alignment of 64 or more searches for `size + align` and splits off the leading gap, as `dlmemalign` does.
- Realloc grows a block in place into a free neighbour or the wilderness, and shrinks by splitting. Only newlib
  uses realloc.

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

### Debug builds: tracking, checks and poison

The tracker is enabled when `PS2_D3BFG_ASSERTS || PS2_D3BFG_DEBUG`. It changes no layout: the same requests
land at the same addresses as in release.

**Tracker**
- An open-addressed table keyed by address, with 8-byte entries: `u32 address` and
  `u32 requested:24 | tag:8`. Requests of 16 MB or more use an escape value.
- 128 k entries, 1 MB, carved from the arena and counted under a "metadata" figure.
- It provides exact per-tag `Stats`, `requested`, and `GetRequestedSize`.
- `class_alloc.cpp`'s `memused`/`numobjects` counting goes under `#if PS2_D3BFG_DEBUG`, as do the
  `idClass::memused` updates.

**Assertions** (`[[unlikely]]` fatals)
- a bad tag, alignment or size
- `new` of size 0
- a tagged allocation before `EnterMain()`
- a free of a pointer that is not live (double or foreign free)
- a free that does not match its tier
- TLSF boundary-tag consistency
- page counters out of range
- an allocation from a thread other than the owner (`GetThreadId()`)

**Poison**
- On free, the payload is filled with `0xDEADBEEF` words. The small slot's first `u16` and the TLSF links are
  skipped. An odd pointer faults on its first word load.
- Fresh allocations are filled with `0xCDCDCDCD`.
- On reuse the poison is checked: every word of a small slot, and the first and last 256 bytes of a large
  block. A mismatch is fatal and names the address, tier, class and tag.

**Release**
- No checks.
- `Alloc` failure is fatal and `TryAlloc` returns null.
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

0. **Measure on the current allocator.**
   - Add a temporary debug histogram in `heap.cpp`: sizes in 4-byte buckets up to 1 KiB and log2 above, with
     alignment and tag. Dump it at `startup-game` (`make test-headless-retail`) and after a `test-map` load.
   - Log each pre-main allocation's return address to identify the four.
   - Record the results in `docs/reference/memory-budget.md`, then fix the class table, the cutoff and the
     tracker size.
   - This plan is `docs/plans/HEAP_ALLOCATOR.md`. Commit it with phase 0's measurements.
1. **Remove the pre-main allocations.**
   - Make the four globals lazy, or give them static storage, with a `[PS2_D3BFG]` tag in id code.
   - Drop the `allocation-before-main` check from `heap_tests.cpp` and from `REQUIRED_CORE_CHECKS`
     (`src/tools/scripts/run_pcsx2_test.py:142`).
   - The baseline becomes (0, 0, 0). Runners stop hard-coding `BASELINE` and require 0/0/0.
   - Regenerate the fixture logs with their generators.
2. **Core classes, host-tested.**
   - `Allocator`, `SmallTier`, `LargeTier`, `Ledger`, `Tracker` and `CountLeadingZeros`.
   - Host harness `src/tests/host/heap_core_tests.cpp`, added to `HOST_HEAP_SOURCES` in the Makefile. It runs
     on its own `Allocator` over a buffer and covers:
     - randomized alloc, free and realloc against a reference model
     - every class and alignment
     - frontier exhaustion and recovery
     - TLSF coalescing
     - poison detection
     - ASan manual poisoning of free slots, proven by seeding defects
3. **Switch the EE over.**
   - `heap.cpp` on the singletons, `_ps2sdk_memory_init`, `EnterMain` from `src/ps2/system/main.cpp` and the
     test drivers' mains, and the libc entry points.
   - Delete `src/external/dlmalloc/` and the `HEAP_VENDOR_INCS` rules (Makefile:363-420).
   - In `config/sources.mk`, drop `CORE_C_SRC` (:255) and register the new files next to `heap.cpp`
     (:249, :595). Then run `make compiledb`.
   - Check with `nm` on the unstripped ELF that nothing else references `sbrk`/`_sbrk`, and that newlib's
     malloc objects are absent.
4. **Ledgers and checks.**
   - Tracker wiring, `PrintMemory`, and the debug-only `class_alloc` counting.
   - Update the heap and class-alloc smoke checks: natural alignment, tier boundaries, `requested == 37`
     (debug only), `invalid-requests` (assertion builds only).
   - Add debug expected-fatal probes for zero-size `new`, double free, write-after-free and pre-main
     allocation.
   - Fix any zero-size `new` the debug gates find, with `[PS2_D3BFG]`.
5. **Mem_Alloc audit.**
   - Move the sites that do not need 16 to `Mem_AllocAligned`/`Mem_ClearedAllocAligned` one owner group at a
     time, then idBlockAlloc. Measure each step with `test-headless-retail`.
6. **Docs.**
   - `.claude/rules/memory-budget.md`: overhead and how to measure.
   - A dated before/after section in `docs/reference/memory-budget.md`.
   - `docs/DEVELOPMENT.md`'s third-party list.
   - README attribution: the quake2-ps2 dlmalloc wrapper is gone.
   - `.claude/rules/ps2-platform.md`: the dlmalloc page-size note.
7. **(R2)** Build `RelocatableArena` with `tex::Record.pixels`, then D3TX segments and sound.

## Expected outcome (estimates for phases 0 and 4 to confirm)

- Small allocations drop from about 44 bytes of overhead to 2 to 6 bytes of class rounding. Partial pages cost
  about 80 KB and descriptors about 75 KB.
- Net, about **2.5 to 2.9 MB more free EE RAM** at retail `startup-game`, more after the audit.
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
