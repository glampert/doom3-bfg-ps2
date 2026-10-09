# Port status and EE foundation acceptance

M0–M2 passed on 2026-10-08 with ps2dev GCC 15.2.0 and PCSX2 2.6.3. This is the
scalar Doom foundation: real idlib, command/CVar/file services, synchronous jobs and
PS2 system/heap adapters. It does not run `idCommonLocal::Init` or initialize the game.

## Build and runtime evidence

| Gate | Result |
| --- | --- |
| `make` and `make release` | Compile and link passed; strict new-code warnings remain errors |
| `make compile-core` | 52 idlib and 3 framework units compiled; archive also contains 4 backend/vendor support units |
| `make inventory` | 458 BFG units classified; 88 Classic Doom units excluded from every target |
| `make compiledb` | 283 entries generated from successful real Make dry runs |
| `make test-host` | Shared heap tests passed under ASan/UBSan; 27 runner regression tests passed |
| SDK-only platform probe | Passed: `20261008T070310Z_smoke_88ff01f3485548c6` |
| Final debug core smoke | Passed: `20261008T073710Z_smoke_88a676a12f834106` |
| Final release core smoke | Passed: `20261008T073729Z_smoke_daeb6577543542f2` |
| Final debug missing-fixture scenario | Expected failure accepted: `20261008T073822Z_smoke_b5b081a1d81c4b8a` |

Each emulator run is archived locally under `build/test-results/<run-id>/`, with its
manifest, authored inputs, result JSON, logs, matched ELF/symbols/map, configuration
snapshot and build reports. The negative scenario fails only the fixture-read check;
the remaining checks, including shutdown, must pass. Retail assets are not used.

The core checks registration and command buffering, `exec` and cvar rules, parser
macros and malformed input, bounded fixture I/O and path rejection, scalar matrix and
packed vertex formats, geometry edge cases, polynomial lifetime, owned event queues,
single-thread primitives and job dependencies/synchronization. Allocator checks cover
pre-main allocation, alignment, tagged over-aligned `new`, all global allocation/delete
forms, overflow and exact accounting on unsized free.

## Measured memory

| Measurement | Debug | Release |
| --- | ---: | ---: |
| Fixed ELF residency (`PT_LOAD` memory size) | 1,560,800 bytes | 1,617,760 bytes |
| ELF `.bss` (included above) | 392,288 bytes | 392,288 bytes |
| Initialized core requested / backing | 13,231 / 17,716 bytes | 13,231 / 17,716 bytes |
| Smoke peak requested / backing | 22,597 / 27,544 bytes | 22,597 / 27,544 bytes |
| dlmalloc arena commitment after tests | 44,832 bytes | 45,216 bytes |
| After shutdown requested / backing / count | 1,024 / 1,068 bytes / 1 | 1,024 / 1,068 bytes / 1 |

The retained allocation is a process-lifetime idlib hash table created before `main`.
Shutdown must return exactly to this baseline. Arena commitment includes untagged
C/newlib allocations, allocator overhead and freed space; it must not be added to the
tagged backing total again. ELF residency includes SDK/library support and is separate
from the arena. These figures exclude kernel reservation and stack usage, and establish
no campaign or map-transition budget. PCSX2 does not prove hardware cache correctness
or throughput.

## M2b in progress: campaign headers and source portability

The first M2b pass on 2026-10-08 removes the campaign precompiled header's mandatory
OpenGL SDK dependency. Menu declarations use public display-mode contracts; texture
and shader headers retain common metadata while keeping GL handles/uniform state
behind `ID_OPENGL`. Portable backend state is opaque, and its implementation remains
required for the resident link. Portal debug drawing uses the render-world line API
on portable builds. These changes do not implement a renderer or fake successful
resource loads.

Initial source fixes rename C++20's reserved `requires` identifiers without changing
the map key or save layout, use the existing portable force-inline macro, fix a Windows
include separator, and restrict missing generated `TypeInfo.h` includes to their
optional memory diagnostics. The engine's `idClass`/`idTypeInfo` hierarchy is retained.

| Gate | Result |
| --- | --- |
| Debug campaign, all 274 units attempted | 190 compiled, 84 failed; Make exited 2 |
| Release campaign, all 274 units attempted | 194 compiled, 80 failed; Make exited 2 |
| Debug and release headless core | Compile/link and PCSX2 smoke passed |
| Debug missing-fixture scenario | Expected failure accepted; remaining checks and shutdown passed |
| Source inventory | Unchanged: all 274 campaign units remain selected |

The four extra release successes at this stage were model files with RTTI casts inside
debug assertions; the checked-query slice below resolves them. AAS, collision, declarations, substantial renderer
frontend code and many campaign support units now compile with the full header boundary.
Successful objects do not establish a game link or runtime acceptance.

Full pass logs are local at `build/{debug,release}/campaign-compile.log`. Reproduce
with `make -B -k -j4 compile-game` and `make -B -k -j4 BUILD=release compile-game`;
`-k` collects failures across the manifest and still returns failure. Keep the normal
`make compile-game` gate failing until every selected unit compiles.

| Remaining compile boundary | Current failures / required work |
| --- | --- |
| Platform services | Windows APIs in filesystem/ZIP, key translation and common diagnostics |
| Common errors | Exception-based error handling in `Common_printf.cpp` |
| Classic dependencies | `Common.cpp` and `common_frame.cpp` still import Classic headers; remove those paths instead of restoring Classic linkage |
| Logical sound | Four retained sound units import `snd_local.h` and its XAudio SDK types |
| SWF image failures | Exception-based image error reporting remains |
| Resident link | Supply declared renderer/audio/offline-session/platform replacements; audit registrations and memory before game-fixture boot |

M2b is incomplete; M3 game-fixture initialization has not begun. The executable still
uses the M2 core bootstrap. The header pass regression runs were debug
`20261008T102332Z_smoke_22a44eba3d404921` and release
`20261008T102441Z_smoke_daf97f5405f540e1`; measured ELF residency and core heap totals
then matched the foundation baseline above. The expected missing-fixture run
is `20261008T102618Z_smoke_eab26c2bbe0849c4`.

There is no map loader boot, GS/VU renderer, audible output, controller input, save
support, physical storage bring-up or custom EE exception handler in this slice.
Classic Doom is excluded but its tree has not been deleted. Imported third-party
notices remain intact; see [REUSE.md](REUSE.md).

## Shared diagnostics and assertion helpers

Backend and target smoke diagnostics now use the synchronous `ps2::Log` / `LogV`
stdout sink. Info output preserves console fragments; warning, error and fatal levels
add a prefix and newline. Required system/core/heap failures share `FatalError` /
`FatalErrorV`. The fatal path has a TODO for an emergency on-screen display.
The result JSON remains an explicit data-file write.

`common.h` supplies the adapted Quake II `PS2_Assert` / `PS2_AssertMsg` helpers,
`ArrayLength`, and portable printf/cold attributes. Assertions are available for
future preconditions; existing runtime validation is retained. New source banners
use the license sentence, with reference attribution in README.md.

| Gate | Result |
| --- | --- |
| Debug/release EE core | Strict compile/link passed; fixed residency 1,560,928 / 1,617,888 bytes |
| `make compile-core` | Passed; archive now includes the shared logger alongside the four previous support units |
| `make compiledb` | 284 entries generated |
| `make test-host` | Shared heap ASan/UBSan tests and 32 Python regressions passed, including five new diagnostics/helper checks |
| Debug core smoke | Passed: `20261008T124025Z_smoke_5b18ec5cfb904157` |
| Release core smoke | Passed: `20261008T124133Z_smoke_9e5ffcf0ea9a416a` |
| SDK-only platform smoke | Passed: `20261008T124209Z_smoke_3b2548edb388408d` |
| Debug missing-fixture scenario | Expected failure accepted: `20261008T124251Z_smoke_4777f12ded474419` |

The host checks exercise untruncated 4 KiB output and variadic forwarding, enabled
assertion evaluation exactly once, disabled condition/message side effects, assertion
source locations, and fatal/heap-failure output flushed to stdout before termination.
Initialized, peak and shutdown tagged heap totals match the foundation baseline.
Measured arena commitment after tests is now 44,704 / 45,088 bytes (debug/release).
Campaign compile/link status at this point remained the initial M2b progress reported above.

## Game class allocation and factories

The next M2b slice, completed on 2026-10-09, removes the four-byte size prefix from
portable `idClass` allocations. Objects retain the shared heap's alignment, including
explicit aligned new/delete for over-aligned derived types. Unsized delete reads the
requested size from the heap metadata. `memused` now counts object bytes, while heap
metadata/backing remains separately accounted; signed byte/object counters cannot
overflow or underflow silently.

Portable factories construct through required allocation, then call the existing
uninitialized-memory diagnostic hook. Allocation failure terminates with a useful
diagnostic. `SpawnEntityType` no longer catches allocation exceptions on the target
and still clears spawn arguments on normal return. Recoverable game-load failures
remain later work. Desktop exception branches are preserved. `TestGameAPI` also
initializes its import structure and sets the API version before validation.

| Gate | Result |
| --- | --- |
| Debug campaign, all 274 units attempted | 232 compiled, 42 failed; Make exited 2 |
| Release campaign, all 274 units attempted | 236 compiled, 38 failed; Make exited 2 |
| Debug/release EE core | Strict compile/link passed; fixed residency 1,564,000 / 1,620,768 bytes |
| `make compile-core` | Passed, including the new class allocation adapter |
| `make compiledb` | 286 entries generated |
| `make test-host` | Shared heap/class ASan/UBSan tests and 33 Python regressions passed |
| Debug core smoke | Passed: `20261008T131034Z_smoke_c39338dc7fe74181` |
| Release core smoke | Passed: `20261008T131817Z_smoke_09eee545a23d494e` |
| Debug missing-fixture scenario | Expected failure accepted: `20261008T131842Z_smoke_4abe5797d21f437d` |

All 274 campaign sources remain selected. The class and entity-spawn changes allow
42 more units to compile in each configuration. Remaining failures are script/SWF
exceptions, RTTI casts, Windows services, Classic references and XAudio dependencies;
the resident game link remains pending. Full pass logs retain the paths and reproduction
commands given above. The next source slice is explicit script error handling with
source-location diagnostics and cleanup.

The shared host/EE tests check 16/64/256-byte alignment, exact unsized/out-of-order
accounting, signed counter boundaries, rejected requests without mutation, zero-size
allocation, and factory construction/diagnostic/virtual-delete ordering. Host fatal
subprocess tests also pass with assertions disabled. These are real allocation-adapter
tests with small probe classes, not a linked game initialization or entity-spawn test.

Debug/release initialized, peak and shutdown tagged heap totals match the foundation
baseline. Arena commitment after tests is 45,728 / 46,304 bytes, and ELF BSS is
392,288 / 392,352 bytes. The added test state is part of this core measurement; no
campaign-memory claim follows from it.


## Script compilation without exceptions

The script compiler/program now compile on the EE with exceptions disabled. Required
syntax/type/storage errors terminate through a shared script diagnostic, including
console snippets. A source location is copied before cleanup releases parser sources,
partial program definitions and any owned input file. This is the plan's initial fatal
policy; recoverable console/script loading still needs transactional rollback before
normal gameplay/save loading. No C++ stack unwinding is implied by these callbacks.

The compiler now takes an explicit program and event-query services. An isolated EE
probe runs the real compiler and program with an empty event registry and authored
script text. It verifies repeated valid compilation/reset and exact tagged-heap recovery,
include loading, missing includes, syntax/lexer/vector/type/event errors, divide/remainder
by zero, nesting and global/function/statement limits. Failure cases require a matching
run identity, program cleanup before the expected source-located fatal diagnostic, and
no emulator crash or watchdog. Global capacity is checked before pointer arithmetic or
counter mutation. Recursive descent is limited to 64 guarded calls; bytecode source
locations cannot silently truncate their unsigned-short fields.

`make script-probe` builds `build/<config>-script/d3bfg.elf`; `make test-script` runs the
matrix, and `make BUILD=release test-script` selects assertions-disabled validation.
The probe deliberately discards unreferenced game/save/interpreter methods with linker
GC. It is separate from the regular core and campaign builds, which still retain all
selected objects. Native events, interpreter execution and game initialization are
outside this probe. Full campaign passes attempted every source: 234/274 debug and
238/274 release compiled; Make exited 2 for the remaining 40/36 failures.

Host cleanup/diagnostic and runner checks passed under the strict warning policy and
ASan/UBSan: 39 Python regressions plus the shared heap/class tests. `make compiledb`
now includes the probe's new source and generates 288 entries from checked Make dry
runs. The core host-root path conversion used for script filenames follows the existing
fixture path jail; traversal, absolute and other-device paths remain rejected.


| Script-slice regression | Result |
| --- | --- |
| Debug isolated compiler | 15 cases passed; first `20261009T000945Z_script_67af50b2c8e848cf`, last `20261009T001011Z_script_90f58539c51243e8` |
| Release isolated compiler | 15 cases passed; first `20261009T000850Z_script_49a1e1910df545e6`, last `20261009T000916Z_script_15a7691ba2894750` |
| Debug core smoke | Passed: `20261009T001047Z_smoke_eeb2a0d766a8443f` |
| Release core smoke | Passed: `20261009T001124Z_smoke_4b359a2dd290470b` |
| Debug missing-fixture scenario | Expected failure accepted: `20261009T001215Z_smoke_819e4462265e4587` |
| Fixed core ELF residency | 1,566,112 / 1,622,688 bytes (debug/release), including 392,864 bytes BSS |
| Core initialized/peak/shutdown tagged heap | Matches the foundation baseline |
| Arena commitment after core tests | 43,616 / 44,384 bytes (debug/release) |

The subsequent M2b slice implements checked type queries, as described below.
The complete resident game link and game-fixture boot remain pending.

## Checked hierarchy queries without RTTI

The 104 casts in the retained campaign sources now use `ps2::CheckedCast`. Portable
menu, GUI-variable, file and model declarations provide virtual type queries that
recognize their own type and public single-inheritance ancestors. One zero-initialized
token per type supplies identity across translation units without allocation or guarded
static initialization. Null input and incompatible dynamic types return null; const
qualification is preserved. Unregistered target types, const removal and sibling/cross
casts are rejected at compile time. Desktop builds retain the original RTTI behavior.
The existing `idClass`/`idTypeInfo` hierarchy is unchanged.

Cached beam/sprite/particle/MD5 models now validate the static-model type in release
before downcasting. Resource-file packaging closes an opened file if its memory-file
query is rejected. Deferred source bodies retain their desktop casts; no executable
`dynamic_cast` or `typeid` remains in the 274 selected campaign units.

Shared host/EE probes exercise inherited/const casts, null and wrong-type rejection,
and cross-translation-unit token/virtual-dispatch identity. EE checks also construct real
`idFile`/`idFile_Memory` objects and inspect the actual menu/GUI/model inheritance hooks.
The latter are declaration checks, not instantiated menu/model runtime acceptance.
Four host compiler tests cover the unsafe-query rejection contracts and desktop fallback.

| Type-query slice regression | Result |
| --- | --- |
| Debug campaign, all 274 units selected | 263 compiled, 11 failed; Make exited 2 |
| Release campaign, all 274 units selected | 263 compiled, 11 failed; Make exited 2 |
| Debug/release EE core | Strict compile/link passed |
| `make compile-core` | Passed; all 55 scalar idlib/framework units retained |
| `make compiledb` | 291 entries generated |
| `make test-host` | Shared heap/class/type ASan/UBSan checks and 43 Python regressions passed |
| Debug core smoke | Passed: `20261009T002551Z_smoke_2bb136000a614bff` |
| Release core smoke | Passed: `20261009T002606Z_smoke_41c364c696154f42` |
| Debug missing-fixture scenario | Expected failure accepted: `20261009T002623Z_smoke_f8bd1db25f654d60` |
| Rebuilt debug/release compiler probes | Valid compile/reset passed: `20261009T002457Z_script_97cc39f212ef4dc7` / `20261009T002519Z_script_f9cea71739e34839` |
| Fixed core ELF residency | 1,567,920 / 1,624,368 bytes (debug/release), including 392,880 bytes BSS |
| Core initialized/peak/shutdown tagged heap | Matches the foundation baseline |
| Arena commitment after core tests | 45,904 / 46,800 bytes (debug/release) |

The remaining compile failures are `Common.cpp`, `Common_printf.cpp`, `common_frame.cpp`,
`FileSystem.cpp`, `KeyInput.cpp`, `Zip.cpp`, four logical-sound units and `SWF_Image.cpp`.
The next M2b work is explicit common/SWF error control flow and portable platform,
offline-session and sound boundaries, followed by the resident link. All 274 sources
remain selected, and the compile gate still correctly fails while these blockers remain.
