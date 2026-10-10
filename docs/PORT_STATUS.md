# Port status and headless acceptance

M0–M2 passed on 2026-10-08 with ps2dev GCC 15.2.0 and PCSX2 2.6.3. The current
foundation now runs real `idCommonLocal::Init` / `Shutdown` through staged core and
offline services. The M2b campaign gate compiles all 281 retained units in debug and
release; audited JPEG/zlib integration now closes the retained resident link in both
configurations. M2b compile/link acceptance is complete. The first partial M3 game
fixture now runs native initialization, playerless entity/script ticks, native target
activation, timed events, removal/cancellation and stable map reloads. Bounded logical
PCM samples and deterministic headless voices are usable as
standalone services. Collision/player startup, broader map loading and native sound/
render integration remain pending; headless gameplay work takes priority.
The sections below preserve each slice's
historical measurements; current acceptance evidence is at the end.

## Initial M2 build and runtime evidence

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

## Initial M2 measured memory

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
| Classic dependencies | Resolved by the Common/offline slice below; portable startup/frame code excludes Classic |
| Logical sound | SDK-free compile boundary supplied; meaningful resource/timing/voice behavior remains required before M3 |
| Resident link | Supply/audit required renderer, audio, resource, UI, filesystem/vendor and platform services; retain registrations and measure memory before game-fixture boot |

This remaining-boundary table reflects the latest sound-boundary slice.
Their portability and validation evidence follows below.

M2b is incomplete; M3 game-fixture initialization has not begun. At the header-pass stage the executable still
used the M2 core adapter. The header pass regression runs were debug
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

## Common and SWF error control flow

Portable common errors now terminate through the shared fatal sink, without consulting
renderer/session state, copying to a clipboard or opening a desktop dialog. Early
printing before CVar initialization and non-main-thread diagnostics use that sink too.
The Init/Frame blocks preserve ordinary scope while reserving exception recovery for
desktop builds. They still need the planned startup, Classic and session work before
they compile or run; no Common lifecycle acceptance follows from this slice.

SWF JPEG decoding uses a bounded, stateful backend adapter. It preserves JPEG tables
between tags, returns RGBA data with opaque alpha, and rejects exhausted input, markers
outside the supplied buffer, invalid dimensions and output larger than 4 MiB. The
initial policy limits either dimension to 1024 and terminates malformed loads after
freeing owned output and codec state. This is not recoverable image-loading rollback.

The host harness uses the real shipped codec with authored encoded pixels. ASan/UBSan
check repeated decoding, separated tables, pixel/alpha output and exact ledger recovery,
plus five fatal cases including a failure after output allocation. A test fatal sink
inspects cleanup; the production logger's termination has separate existing regressions.
The generator exposed an upstream encoder over-read from short standard Huffman tables;
a tagged fix copies only their symbol count. The codec retains its shipped float DCT.

Debug/release compile the two newly portable campaign units and the strict backend
adapter. The campaign is now 265/274 in each configuration, with nine failures remaining;
Make still exits 2. Default debug/release foundation builds pass; the adapter is compiled
separately rather than linked into that foundation. Target codec import and JPEG runtime
acceptance remain part of the resident-link work. All 45 host regressions pass, and
`make compiledb` produces 292 entries. No retail assets or new vendor imports are used.

## Filesystem, ZIP and key translation

The next M2b slice replaces campaign filesystem Win32 handles, size queries, directory
creation, removal and rename calls with bounded shared services. Paths keep console
device prefixes and forward slashes; logical paths reject traversal, absolute/device
names and truncated joins. Stream lengths preserve the cursor and reject values outside
the engine's signed range. Whole-file reads publish buffers and ownership counts only
after complete input, including journal paths; failed/partial memory loads are closed
and freed. CRC tools use `std::unique_ptr` in place of removed `std::auto_ptr`.

`Sys_ListFiles` supplies regular-file and directory filters, skips dot entries/symlinks,
matches extensions without case sensitivity and clears results on errors. Sys directory
and existing-file writability queries use actual stat results. This does not add case-
folded OS lookup or symlink resolution. Host ZIP dates use stat time encoded as UTC,
at two-second resolution, clamped to 1980..2107. The EE explicitly uses 1980-01-01
until driver timestamps are validated; missing stat fails on both platforms. The SDK
and PCSX2 disagree on fio year encoding, so target metadata availability does not
prove date accuracy. Key bindings retain the engine's existing localized label table without
calling Windows keyboard-layout APIs. Keyboard/controller drivers are still pending.

The foundation now opens real `idFile_Permanent` streams instead of buffering every
open. An authored 70 KiB fixture verifies this, while explicit bulk/memory fixture reads
keep the 64 KiB limit. Memory-file end seeks retain their original positive backward
distance; permanent streams use stdio's signed end offset. The campaign filesystem is
compiled separately; its initialization, resource/container loading, ZIP compression
linking and full game lifecycle remain untested. Foundation-level filesystem listing,
write and resource APIs remain explicit unsupported operations.

The one-time ps2sdk ROM FILEIO patch runs after SIF initialization, without resetting
the loader's IOP. It fixes removal fallthrough and getstat/dread interrupt protection;
failed patching disables removal. The default fio driver has no native rename and
returns `ENOSYS`. PCSX2 2.6.3 deletes a file but reports `ENODEV`; the wrapper preserves
that failure. The regression checks the exact error and absence of both file and an
unwanted same-named directory. This is error/side-effect acceptance, not successful
removal acceptance on hardware. Source evidence and limits are in
[ps2-platform.md](../.claude/rules/ps2-platform.md).

| Filesystem-slice regression | Result |
| --- | --- |
| Debug/release campaign, all 274 units selected | 268 compiled, six failed; Make exited 2 |
| Debug/release EE core | Strict compile/link passed |
| `make compile-core` | Passed; all 55 scalar idlib/framework units retained |
| `make compiledb` | 294 entries generated |
| `make test-host` | Shared heap/class/type/JPEG checks and 47 Python regressions passed under ASan/UBSan |
| Debug core smoke | Passed: `20261009T041229Z_smoke_7adb7a1edf5b4e47` |
| Release core smoke | Passed: `20261009T041258Z_smoke_ceacd45e4e7a4445` |
| Debug missing-fixture scenario | Expected failure accepted: `20261009T041325Z_smoke_f394343797c449f6` |
| Debug compiler regression | 15 cases passed; first `20261009T040616Z_script_18f7b83c04e84a55`, last `20261009T040640Z_script_644fc2b734fd4972` |
| Release compiler regression | 15 cases passed; first `20261009T040711Z_script_34252bfbe4d7456c`, last `20261009T040736Z_script_7090edc20fe3434b` |
| Fixed core ELF residency | 1,581,744 / 1,638,512 bytes (debug/release) |
| Core BSS | 394,160 / 394,224 bytes (included in residency) |
| Core initialized/peak/shutdown tagged heap | Matches the foundation baseline |
| Arena commitment after core tests | 44,368 / 44,944 bytes (debug/release) |

The six remaining campaign failures are `Common.cpp`, `common_frame.cpp`,
`snd_emitter.cpp`, `snd_shader.cpp`, `snd_system.cpp` and `snd_world.cpp`. The full
manifest stays selected and the gate still fails. Next come staged common startup,
Classic removal and offline-session services, then the logical-sound/backend boundary
and resident game link. M3 game-fixture initialization has not begun.

## Real Common lifecycle and offline sessions

The foundation now instantiates the real `idCommonLocal`, using its constructor,
`Init`, `Shutdown` and initialization state from `Common.cpp`. The former `CoreCommon`
subclass is removed. Portable startup tracks completed system, idlib, command, cvar,
fixture-filesystem, synchronous-job and offline-session stages. Shutdown unwinds those
stages in reverse order and is idempotent, including deliberately stopped startup.
Static CVar registration is one-shot; each partial-startup probe launches a new process.
Game, presentation, dialogs and save capabilities remain explicitly deferred.

Portable Common startup/frame paths exclude Classic imports, events, title switching,
ticks and drawing; its layout omits the 2,304,000-byte Classic framebuffer and material.
The physical Classic tree remains unchanged and excluded from all targets. `com_smp`
is registered as zero/ROM, game workers are not created and game/draw dispatch is
synchronous regardless of forced cvar writes. The static `GetGameAPI` import now compiles
with matching C linkage and version/interface checks. It is not called by this
foundation, and neither game execution nor resident game linking is accepted yet.

The offline session owns one local user on device zero and real native profile stats
and 128 achievement bits. Registration is idempotent; sign-out releases lobbies, changes
input routing and invalidates handles. New registration starts a fresh transient profile.
Default-profile lookup shares the active profile without resetting its data. Match
parameters are copied into local lobbies; StartMatch enters LOADING and only explicit
LoadingFinished enters INGAME. Frame/Pump never pretends a map has loaded. Three reloads
after warming native string-pool capacity retain the exact heap ledger.

Profile persistence reports ERR and preserves current stats/bits. There are no save
processors, multi-megabyte save buffers, network peers, online presence, renderer or
sound device behind these services. Unsupported operations fail explicitly, including
attempting to dereference an absent save manager. Native game achievement-manager
execution, player input and persistent saves remain later work.

Six native units move from PS2 replacement into retained runtime: profile, local user,
sign-in, snapshots, lightweight compression and snapshot jobs. The snapshot dependencies
link Common's embedded values without dropping object bodies; they do not enable online
services. Every formerly selected campaign unit remains selected, giving 280 units.
The ordinary core still links all selected objects directly without garbage collection.

| Common/offline regression | Result |
| --- | --- |
| Debug/release EE core | Strict compile/link passed |
| `make compile-core` | 52 scalar idlib + 10 framework/session support units passed |
| Debug/release campaign, all 280 units selected | 276 compiled; only four logical-sound units failed on `dxsdkver.h`; Make exited 2 |
| Campaign backend adapters | Lifecycle/offline/JPEG objects compiled with full headers and strict warnings |
| `make inventory` / `make compiledb` | 458 units classified, 88 Classic units excluded; 304 compile database entries |
| `make test-host` | Shared ASan/UBSan fixtures and all 55 Python regressions passed |
| Final debug core smoke | Passed: `20261009T054623Z_smoke_ec14c8d265424898` |
| Final release core smoke | Passed: `20261009T054659Z_smoke_1d84ce6ef0ba47cb` |
| Debug missing-fixture regression | Expected failure accepted: `20261009T054355Z_smoke_e05a652ddddd4f03` |
| Release missing-fixture regression | Expected failure accepted: `20261009T054726Z_smoke_20b73ae157c14b73` |
| Debug Common probes | All 14 passed; first `20261009T054107Z_smoke_96b7e89f61da4f08`, last `20261009T054135Z_smoke_8fa443596c3b43fa` |
| Release Common probes | All 14 passed; first `20261009T054149Z_smoke_0805fd26f6f043c5`, last `20261009T054214Z_smoke_8418d4fdc67b418b` |
| Debug script regressions | All 15 passed; first `20261009T054416Z_script_db39ac2b85044ffa`, last `20261009T054441Z_script_8c305a287c254fa8` |
| Release script regressions | All 15 passed; first `20261009T054521Z_script_5372b11cf0ef49d1`, last `20261009T054546Z_script_abd9ce34721241df` |

Seven Common probes verify each partial-startup stop, exact reverse cleanup and full
ledger recovery. Seven require the expected fatal after their own fresh run identity:
online flags, missing user, invalid loading order, network matchmaking, Classic title
switching, unavailable save manager and nonzero input device. The runner rejects
unrelated failures, unexpected returns, stale identities, watchdogs and TLB/bus errors.
Eight regular core markers cover Common/cvar identity, users/profiles/achievements,
unavailable persistence, match transitions/copying, reload accounting and sign-out.

| Common/offline core memory | Debug | Release |
| --- | ---: | ---: |
| Fixed ELF residency (`PT_LOAD`) | 1,705,136 bytes | 1,761,712 bytes |
| ELF BSS (included above) | 454,192 bytes | 454,192 bytes |
| Real portable `commonLocal` (included in BSS) | 54,792 bytes | 54,792 bytes |
| Initialized requested / backing / count | 18,945 / 25,708 bytes / 165 | 18,945 / 25,708 bytes / 165 |
| Smoke peak requested / backing | 44,887 / 52,376 bytes | 44,887 / 52,376 bytes |
| Arena commitment after tests | 68,432 bytes | 69,200 bytes |
| After shutdown requested / backing / count | 1,024 / 1,068 bytes / 1 | 1,024 / 1,068 bytes / 1 |

The previous small Common adapter never contained the Classic framebuffer; excluding
it from real Common prevents new residency rather than claiming a measured saving
against that adapter. These figures include the synthetic offline tests and retained
support, not campaign/map state. Kernel/stacks, real hardware cache behavior and game
transition peaks remain unmeasured. No retail assets are used.

At this slice, next were the logical-sound/XAudio header split and resident-link adapters,
then the authored M3 game fixture. The following slice completes the compile boundary;
Common/offline acceptance alone did not establish that gate.


## M2b logical sound: portable sample/voice/device boundary

The four retained logical sound units now compile without DirectX/XAudio SDK headers.
`snd_local.h` selects `ps2/audio/sound_backend.*` and portable stream-context types;
the desktop branch is preserved. `GetIXAudio2` returns null for its optional desktop
video-device query. Emitter, shader, world and system logic remain selected and their
playback bodies are unchanged. No sound-disable cvar or source exclusion bypasses
the campaign gate. All 280 retained units compile in both configurations.

The initial boundary owns only unloaded sample metadata: native strings, reference and
purge flags, and last-played time. Resource loading, default generation, duration/rate/
channel/encoding/amplitude queries and device/voice operations terminate with the method
name. No sample is reported loaded, no voice is created and no device is initialized.
This is compile acceptance; real timing/completion/voice behavior remains required
before M3, and audsrv/SPU2 output remains later work. Native sound worlds are not part
of the foundation runtime.

New backend code compiles strictly with full campaign headers in core and campaign
object trees. Cold-source optimization now applies to both trees. The foundation still
links all selected objects directly without GC. Source dispositions stay unchanged;
no vendor, retail-data or backend-cvar dependency is added. The compile database has
306 entries.

| Sound-boundary gate | Result |
| --- | --- |
| Debug/release core and all 280 campaign units | Compile/link and compile-only gates passed; Make exited 0 |
| `make compile-core` | 52 scalar idlib + 10 framework/session support units passed |
| SDK dependency audit | No DirectX/XAudio header in the four logical sound or backend dependency files |
| `make test-host` | Shared ASan/UBSan fixtures and all 55 Python regressions passed |
| Debug core smoke | Passed: `20261009T071044Z_smoke_52b4d68ab89c461c` |
| Release core smoke | Passed: `20261009T071102Z_smoke_8e4c1da14d8e4fba` |
| Debug Common/audio probes | All 17 passed; first `20261009T071130Z_smoke_70d54275e3404be3`, last `20261009T071202Z_smoke_e28341232621491a` |
| Release Common/audio probes | All 17 passed; first `20261009T071221Z_smoke_f539b59c90ab4f51`, last `20261009T071253Z_smoke_7616cb5f14164575` |
| Debug missing-fixture regression | Expected failure accepted: `20261009T071311Z_smoke_b80d519648e24922` |
| Release missing-fixture regression | Expected failure accepted: `20261009T071336Z_smoke_e3d51e68d59d406f` |
| Debug/release isolated script probe | Compile/link passed with the shared audio boundary; interpreter remains unexecuted |

The added core check requires sample strings and flags to preserve exact heap ownership
over three scopes. Three new expected-fatal probes exercise resource loading, duration
queries and device initialization; the matrix includes the seven existing partial
startup stops and seven offline/precondition failures. Its runner rejects unexpected
returns, stale identities, unrelated fatal diagnostics and emulator faults.

| Sound-boundary core memory | Debug | Release |
| --- | ---: | ---: |
| Fixed ELF residency (`PT_LOAD`) | 1,707,056 bytes | 1,763,632 bytes |
| ELF BSS (included above) | 454,192 bytes | 454,192 bytes |
| Initialized requested / backing / count | 18,945 / 25,708 bytes / 165 | 18,945 / 25,708 bytes / 165 |
| Smoke peak requested / backing | 44,887 / 52,376 bytes | 44,887 / 52,376 bytes |
| Arena commitment after tests | 70,608 bytes | 71,376 bytes |
| After shutdown requested / backing / count | 1,024 / 1,068 bytes / 1 | 1,024 / 1,068 bytes / 1 |

Tagged startup, peak and final ledgers match the preceding slice. The audio check
releases its native string storage each time. The fixed ELF and arena totals include
this synthetic test boundary, without voice pools, sample payloads or a sound device.
They establish no campaign audio or transition budget.

The remaining M2b work is the explicit resident campaign link: supply/audit renderer,
resource, UI, filesystem/vendor and platform services without discarding registrations.
Then implement meaningful logical rendering/audio queries for the authored M3 fixture.


## M2b resident link: reproducible gate and first dependency providers

The new `make link-game` uses all retained campaign objects directly, with strict
resident support and the required backend adapters. It has no GC, no foundation
filesystem/Common substitute, and no unresolved-symbol suppression. Reports under
`build/<config>/resident/` preserve raw logs, a map, source/object/dependency hashes,
compiler flags, real exit status, and unique symbols with categories and requestors.
A failed attempt removes stale resident images. Success also requires registration
roots and every input in the map, then a MIPS executable with load segments. The
resident entry deliberately rejects execution until M3 startup exists.

The first exploratory 280-unit link found 216 unresolved symbols. OS/time/language
providers, explicit source-model import rejection, shared Common offline/query/reset/
deferred providers, and retained native campaign achievements now reduce this to 171
in both configurations. Every originally selected campaign unit remains selected.
Achievements moves from deferred into runtime, giving 281 campaign units and 297
direct link objects; all compile in debug and release with strict new-code warnings.
Classic achievement evaluation fails explicitly without importing Classic headers.
Native achievement execution and Common populated-snapshot reset remain untested
until the real game fixture.

| Remaining resident symbol group | Debug | Release |
| --- | ---: | ---: |
| Renderer/image/vertex/cinematic/demo services and cvars | 87 | 87 |
| Deferred shell/multiplayer/leaderboard/save services and cvars | 61 | 61 |
| JPEG/zlib codecs | 17 | 17 |
| Input/usercmd providers and controls | 4 | 4 |
| Deferred sound-window UI | 2 | 2 |
| Total unresolved / duplicate definitions | 171 / 0 | 171 / 0 |

The actual linker exits 1; Make exits 2. Neither configuration emits a resident ELF.
This keeps M2b incomplete. These link maps are failed-attempt evidence, not resident
size measurements. The next closure groups are renderer contracts, explicit deferred
game/UI services, input/usercmd and audited codec dependencies. M3 game startup/ticks,
logical rendering/audio behavior and authored map/reload acceptance still follow.

`sys_services.cpp` supplies six language identifiers, bounded nonnegative duration
formatting and UTC formatting of supplied timestamps. It does not validate a clock or
storage timestamps. OS capabilities fail with their method names. Source-model import
rejects ASE/LWO/Maya formats with a host-conversion diagnostic. No vendor or retail
data is imported. Common's inactive-demo and offline query checks recover the ledger.
The compile database now has 311 entries. Six new host gate regressions bring the
suite to 61, alongside the shared sanitizer fixtures.

| Resident-link regression | Result |
| --- | --- |
| Debug/release core and all 281 campaign units | Strict core compile/link and campaign compilation passed |
| Debug/release `make link-game` | Failed as required: 171 unresolved symbols, 0 duplicates, 297 inputs retained |
| Source audit / compilation database | 458 shipped units classified, 88 Classic units excluded; 311 entries |
| `make test-host` | Shared ASan/UBSan fixtures and all 61 Python regressions passed |
| Debug core smoke | Passed: `20261009T115156Z_smoke_129dc91bbac4469d` |
| Release core smoke | Passed: `20261009T115235Z_smoke_07574528ae824bfc` |
| Debug startup/deferred probes | All 22 passed; first `20261009T115411Z_smoke_cddffa2b2070409c`, last `20261009T115456Z_smoke_3d1be72de733413b` |
| Release startup/deferred probes | All 22 passed; first `20261009T115609Z_smoke_8982619674e84488`, last `20261009T115651Z_smoke_8f67b474d3f840a8` |
| Debug missing-fixture scenario | Expected failure and cleanup passed: `20261009T115811Z_smoke_c657465e7f7344ca` |
| Release missing-fixture scenario | Expected failure and cleanup passed: `20261009T120107Z_smoke_cc6ab812c1a24df7` |
| Debug script regressions | Include passed: `20261009T120131Z_script_e3358fea192e486f`; syntax failure/cleanup passed: `20261009T120204Z_script_8207dbc40d204674` |
| Release script regressions | Include passed: `20261009T120230Z_script_f0061bd22c324dec`; syntax failure/cleanup passed: `20261009T120255Z_script_056a3a4d4fbb4f22` |


| Resident-service core memory | Debug | Release |
| --- | ---: | ---: |
| Fixed ELF residency (`PT_LOAD`) | 1,719,920 bytes | 1,776,880 bytes |
| ELF BSS (included above) | 454,512 bytes | 454,512 bytes |
| Initialized requested / backing / count | 19,837 / 26,896 bytes / 172 | 19,837 / 26,896 bytes / 172 |
| Smoke peak requested / backing | 45,715 / 53,500 bytes | 45,715 / 53,500 bytes |
| Arena commitment after tests | 70,032 bytes | 70,416 bytes |
| After shutdown requested / backing / count | 1,024 / 1,068 bytes / 1 | 1,024 / 1,068 bytes / 1 |

New retained language/frame cvars change startup/peak ledgers; shutdown still recovers
exactly. The three source importers and OS failures are exercised in fresh processes
with assertions on and off. These numbers cover the foundation, not a resident game,
real model/sample payloads, snapshot queues populated by simulation, or map transitions.

## M2b renderer interface stubs

Four new backend units bind the native render-system vtable and frontend globals,
image/shader/resolution interfaces, cinematics (including the sound-window GUI), and
vertex/index/joint buffers. All 87 renderer symbols and two sound-window symbols from
the previous link are resolved. `renderSystem` points to native `tr`; no upstream
renderer layouts or source dispositions change. The 42 referenced renderer cvars
retain native defaults, declared flags, bounds and completions in both configurations.

This is a typed interface boundary. Inactive device/stereo/background-swap queries,
unloaded image metadata, empty buffer/cache cleanup and a disabled resolution policy
are supported. Initialization, live dimensions, resource loading/allocation, shader
queries and draw submission fail with the method name. World demo writes may return
when no recorder exists; active demos fail. No desktop frame/vertex pools, texture
payloads, logical world or GS device are created. Meaningful material/model/world
contracts remain required for M3.

| Remaining resident symbol group | Debug | Release |
| --- | ---: | ---: |
| Deferred shell/multiplayer/leaderboard/save services and cvars | 61 | 61 |
| JPEG/zlib codecs | 17 | 17 |
| Input/usercmd providers and controls | 4 | 4 |
| Total unresolved / duplicate definitions | 82 / 0 | 82 / 0 |

Both configurations retain all 301 direct link inputs without GC. The linker exits 1,
Make exits 2, and neither emits a resident ELF. M2b remains incomplete; the next link
closure groups are deferred game/UI services, input/usercmd and audited codecs. The
failed maps establish no resident memory or game-boot acceptance.

| Renderer-interface regression | Result |
| --- | --- |
| Debug/release core and all 281 campaign units | Strict core compile/link and campaign compilation passed |
| Debug/release `make link-game` | Failed as required: 82 unresolved symbols, 0 duplicates, all 301 inputs retained |
| Source audit / compilation database | 458 shipped units classified, 88 Classic units excluded; 316 entries |
| `make test-host` | Shared ASan/UBSan fixtures and all 61 Python regressions passed |
| Debug core smoke | Passed: `20261009T122354Z_smoke_a93d770c16554a0f` |
| Release core smoke | Passed: `20261009T122516Z_smoke_81d69c82c10443ba` |
| Debug startup/deferred probes | All 30 passed; first `20261009T122554Z_smoke_b868529526aa4e4b`, last `20261009T122652Z_smoke_e303676aea714d35` |
| Release startup/deferred probes | All 30 passed; first `20261009T122804Z_smoke_59cb86c41cdf4522`, last `20261009T122905Z_smoke_0b3bfa749ec34374` |
| Debug missing-fixture scenario | Expected failure and cleanup passed: `20261009T123025Z_smoke_3aef1eb4d3e843cd` |
| Release missing-fixture scenario | Expected failure and cleanup passed: `20261009T123132Z_smoke_de424f39cbcb4f6a` |
| Debug/release isolated script probe builds | Strict compilation and link passed; their separate GC boundary remains unchanged |
| Debug script regressions | Include passed: `20261009T123200Z_script_b55a8f6e361148c5`; syntax failure/cleanup passed: `20261009T123205Z_script_88b374a8b6a9447c` |
| Release script regressions | Include passed: `20261009T123210Z_script_eae539db43e2440d`; syntax failure/cleanup passed: `20261009T123214Z_script_73be471c21174caa` |

The three new required core markers verify interface identity/inactive state, repeated
native image/string/buffer scope cleanup with exact heap recovery, and disabled
resolution/cvar metadata. Eight negative probes exercise renderer initialization,
width, draw submission, image/vertex/shader loading, cinematic allocation and demo
output. Each requires its own run identity and the method's fatal diagnostic, with
assertions enabled and disabled; unexpected returns or emulator faults cannot pass.
World demo guards have compile acceptance; actual render-world mutation awaits M3.

| Renderer-interface core memory | Debug | Release |
| --- | ---: | ---: |
| Fixed ELF residency (`PT_LOAD`) | 1,745,136 bytes | 1,801,840 bytes |
| ELF BSS (included above) | 459,632 bytes | 459,632 bytes |
| Initialized requested / backing / count | 30,597 / 41,624 bytes / 266 | 30,597 / 41,624 bytes / 266 |
| Smoke peak requested / backing | 56,475 / 68,228 bytes | 56,475 / 68,228 bytes |
| Arena commitment after tests | 85,776 bytes | 86,416 bytes |
| After shutdown requested / backing / count | 1,024 / 1,068 bytes / 1 | 1,024 / 1,068 bytes / 1 |

The extra native globals and cvars raise foundation residency and registration costs.
These measurements include only the foundation and metadata tests, without render
worlds, geometry/image payloads, frame arenas or game/map-transition state.


## M2b in progress: deferred game and UI providers

The 2026-10-10 slice adds three strict, cold campaign providers: shared
`multiplayer_stub.cpp` and `save_metadata.cpp`, plus campaign-only `shell_stub.cpp`.
They resolve all 61 deferred symbols from the preceding resident link without removing
any campaign object, changing native layouts or enabling an excluded upstream unit.

The native multiplayer constructor initializes all fields without consulting other
globals before main. Offline Reset/Precache preserve the unconditional single-player
map-startup calls; inactive queries and empty scoreboard shutdown are usable. Match
execution, snapshots, chat, team/flag services, scoreboard activation and game-mode
enumeration terminate with the native method name. Offline leaderboard lifecycle
registers zero online definitions and makes no map/mode/title-storage requests.

Native save descriptions retain their dictionary/string ownership and copy assignment.
Clear restores empty fields, an undamaged flag and zero date. `saveGame_enable=0` uses
BOOL/ROM flags in both builds; forced internal writes still cannot obtain the session
save manager. No save pipeline or physical storage is created. The native retry-dialog
entry terminates explicitly. Shell providers supply the native vtable and required
save/load-screen enumeration calls; empty cleanup clears list metadata, while active
resources and presentation/enumeration/leaderboard operations fail explicitly. Shell
and retry-dialog runtime execution still require the resident fixture.

| Remaining resident symbol group | Debug | Release |
| --- | ---: | ---: |
| JPEG/zlib codecs | 17 | 17 |
| Input/usercmd providers and controls | 4 | 4 |
| Total unresolved / duplicate definitions | 21 / 0 | 21 / 0 |

Both configurations retain 304 direct inputs: 281 campaign, fourteen campaign backend,
eight strict resident support and dlmalloc. Neither uses GC or emits a resident ELF;
the linker exits 1 and Make exits 2. M2b remains incomplete. Input/usercmd and audited
codecs are the next closure groups; meaningful logical render/sound and M3 game startup
still follow. Failed maps establish no resident memory or game-boot acceptance.

Three new required core markers exercise native multiplayer reset/precache and repeated
empty cleanup with exact heap recovery, independently copied save descriptions and
repeated destruction after warming dictionary string pools, and read-only save policy.
Six new fresh-process probes cover match ticks, variadic chat, snapshot writes,
scoreboard activation, mode enumeration and forced save enablement. Expected-fatal
acceptance requires its own run/begin identity and diagnostic; emulator faults,
watchdogs and unexpected returns fail. Assertions are enabled and disabled in the
debug/release matrices.

| Deferred-interface core memory | Debug | Release |
| --- | ---: | ---: |
| Fixed ELF residency (`PT_LOAD`) | 1,752,496 bytes | 1,809,072 bytes |
| ELF BSS (included above) | 459,696 bytes | 459,696 bytes |
| Initialized requested / backing / count | 30,873 / 41,984 bytes / 268 | 30,873 / 41,984 bytes / 268 |
| Smoke peak requested / backing | 56,751 / 68,588 bytes | 56,751 / 68,588 bytes |
| Arena commitment after tests | 86,608 bytes | 87,376 bytes |
| After shutdown requested / backing / count | 1,024 / 1,068 bytes / 1 | 1,024 / 1,068 bytes / 1 |

The shared metadata tests and disabled save cvar change foundation totals only. No
shell screens, multiplayer match, save manager or pipeline is initialized. Dictionary
pools retain reusable capacity until native idlib shutdown; repeated metadata scopes
stabilize after warming and full shutdown recovers the exact process-lifetime baseline.
These figures exclude kernel/stacks and do not establish a resident campaign budget.


| Deferred-interface regression | Result |
| --- | --- |
| Debug/release core and all 281 campaign units | Strict core compile/link and campaign compilation passed |
| Debug/release `make link-game` | Failed as required: 21 unresolved symbols, 0 duplicates, all 304 inputs retained |
| Source audit / compilation database | 458 shipped units classified, 88 Classic units excluded; 320 entries |
| `make test-host` | Shared ASan/UBSan fixtures and all 61 Python regressions passed |
| Debug core smoke | Passed: `20261009T232708Z_smoke_2ef4365b0b91442d` |
| Release core smoke | Passed: `20261009T232739Z_smoke_2255ae32ea664cd5` |
| Debug startup/deferred probes | All 36 passed; first `20261009T232830Z_smoke_3a4f3f4801264fbc`, last `20261009T232939Z_smoke_dfc9269081c74c07` |
| Release startup/deferred probes | All 36 passed; first `20261009T232953Z_smoke_205112a106ef421a`, last `20261009T233100Z_smoke_88e2202ade8e45bd` |
| Debug missing-fixture scenario | Expected failure and cleanup passed: `20261009T233129Z_smoke_6c2c6bff97964704` |
| Release missing-fixture scenario | Expected failure and cleanup passed: `20261009T233133Z_smoke_2f47e02291fb45d9` |
| Debug/release isolated script probe builds | Strict compilation/link passed; separate GC boundary unchanged |
| Debug script regressions | Include passed: `20261009T233136Z_script_117bf90e3537435c`; syntax failure/cleanup passed: `20261009T233140Z_script_2686cbe3847742d7` |
| Release script regressions | Include passed: `20261009T233143Z_script_2b8f7c7596824e7c`; syntax failure/cleanup passed: `20261009T233147Z_script_7f46bc8fb85b4748` |


## M2b in progress: input/usercmd interface

The 2026-10-10 slice adds shared, cold `ps2/input/usercmd_stub.cpp`, compiled with
strict warnings against the native contracts in both core and campaign object trees.
It binds `usercmdGen` to an allocation-free typed provider and preserves all four input
symbols from the preceding resident link. No native source disposition or layout changes.

The native action table keeps all 47 names, their enum mapping/order and final null
sentinel. Its exact `UB_MAX_BUTTONS` extent is checked at compile time because retained
SWF code indexes the table backwards. Case-insensitive command lookup supports exact
native names, including all impulses; unknown commands return `UB_NONE`, while null
strings fail explicitly in debug and release. Clear/ClearAngles/Shutdown are usable on
the empty provider, and invalid button/key indices retain the native minus-one result.

Initialization, map preparation, inhibition, mouse/key/button sampling and current
command building/access fail with the native interface method. No input state,
neutral player command, connected controller, SDK module or polling buffer is fabricated.
`in_useJoystick` and `in_joystickRumble` default to zero, preserve their archive flags and
add ROM in both builds. Direct cvar commands reject changes; forced programmatic writes
cannot enable sampling or the existing unavailable `Sys_SetRumble`. Real device input
and explicit synthetic fixture commands remain later runtime work.

| Remaining resident symbol group | Debug | Release |
| --- | ---: | ---: |
| JPEG/zlib codecs | 17 | 17 |
| Total unresolved / duplicate definitions | 17 / 0 | 17 / 0 |

Both configurations retain all 305 direct inputs: 281 campaign, fifteen campaign
backend, eight strict resident support and dlmalloc. The linker exits 1, Make exits 2,
and no resident ELF is emitted. Neither archives nor GC hide dependencies. M2b remains
incomplete until audited codec integration and resident registration/map acceptance;
meaningful logical services and actual game initialization still follow in M3.

Three new required core markers verify table extent/order, independent command names,
case and impulse-prefix handling; repeated empty cleanup and invalid indices with exact
ledger recovery; and disabled controller policy. Eleven fresh-process probes cover the
eight unavailable interface methods, null command validation and forced controller/
rumble preferences. Their acceptance requires the matching run/begin identity and fatal
message; emulator faults, watchdogs or unexpected returns fail.

| Input-interface core memory | Debug | Release |
| --- | ---: | ---: |
| Fixed ELF residency (`PT_LOAD`) | 1,757,104 bytes | 1,813,680 bytes |
| ELF BSS (included above) | 459,824 bytes | 459,824 bytes |
| Initialized requested / backing / count | 31,425 / 42,704 bytes / 272 | 31,425 / 42,704 bytes / 272 |
| Smoke peak requested / backing | 57,367 / 69,372 bytes | 57,367 / 69,372 bytes |
| Arena commitment after tests | 86,096 bytes | 86,864 bytes |
| After shutdown requested / backing / count | 1,024 / 1,068 bytes / 1 | 1,024 / 1,068 bytes / 1 |

The extra cvars and native action metadata change foundation residency/registration and
synthetic command-buffer peaks only. Full shutdown still recovers the exact process-
lifetime hash allocation. No device or command-generation buffers are acquired. Arena
commitment includes C/newlib allocation, freed capacity and fragmentation; changes do
not measure a controller/game budget. Kernel, stacks and resident campaign state remain
unmeasured; failed resident maps are not memory acceptance.


| Input-interface regression | Result |
| --- | --- |
| Debug/release core and all 281 campaign units | Strict core compile/link and campaign compilation passed |
| Debug/release `make link-game` | Failed as required: 17 codec symbols, 0 duplicates, all 305 inputs retained |
| Source audit / compilation database | 458 shipped units classified, 88 Classic units excluded; 322 entries |
| `make test-host` | Shared ASan/UBSan fixtures and all 62 Python regressions passed |
| Debug core smoke | Passed: `20261009T233851Z_smoke_747c22b0d9ec4a6a` |
| Release core smoke | Passed: `20261009T233933Z_smoke_205b2602957d459c` |
| Debug startup/deferred/input probes | All 47 passed; first `20261009T234024Z_smoke_ad44531f44c64483`, last `20261009T234154Z_smoke_fe7bfa083d214e55` |
| Release startup/deferred/input probes | All 47 passed; first `20261009T234247Z_smoke_0c3bcfa3f4274cc3`, last `20261009T234416Z_smoke_472321d529ab447a` |
| Debug missing-fixture scenario | Expected failure and cleanup passed: `20261009T234447Z_smoke_a707ad752a3e4865` |
| Release missing-fixture scenario | Expected failure and cleanup passed: `20261009T234451Z_smoke_6120795604a64e81` |
| Debug/release isolated script probe builds | Strict compilation/link passed; separate GC boundary unchanged |
| Debug script regressions | Include passed: `20261009T234455Z_script_8d6e8c87b601413e`; syntax failure/cleanup passed: `20261009T234458Z_script_0cebbf6202544eff` |
| Release script regressions | Include passed: `20261009T234502Z_script_e2a6e21856d64ec1`; syntax failure/cleanup passed: `20261009T234505Z_script_4a9c5e3e30504adf` |

The negative-probe classifier now restricts acceptance to the first fatal line and a
name boundary after the expected diagnostic. An InitForNewMap failure cannot satisfy
an Init probe, and a later matching message cannot hide an unrelated initial fatal.
The added host regression tests both rejection cases. All debug negative artifacts were
also checked under this stricter classifier; the release matrix ran it directly.


## M2b accepted: audited codecs and retained resident link

The codec slice completed on 2026-10-10 moves the bundled engine-modified JPEG 6
and zlib 1.2.3 under `src/external/`. All 96 existing package files are byte-for-byte
relocations, preserving notices and the prior tagged JPEG encoder Huffman-copy fix.
`README.ijg` restores the original IJG distribution terms from the archived 6a README;
the codec itself is not upgraded. Original reference-project paths are remapped by the
source inventory, with all 458 shipped units and original project counts still audited.
Thirty-four vendor sources move from optional to the reviewed runtime disposition.

The target explicitly builds nine streaming/CRC zlib C units and 25 JPEG decompression
C++ units with separate vendor flag/compiler/source stamps. The strict
`ps2/system/codec_memory.cpp` supplies checked C-linkage zlib allocation defaults,
JPEG no-backing-store heap hooks and shared diagnostic forwarding. All default codec
allocations use native ZIP/JPG tags; caller-provided stream callbacks retain priority.
No gzip file I/O, JPEG encoder or temporary-file memory manager is linked on the EE.
`compress.c` is needed by deflate's internal `compressBound` reference.

JPEG keeps the engine's four-byte RGBA output and float DCT. The bounded SWF adapter
now has target runtime acceptance for complete images, table-only/abbreviated reuse,
pixel ranges and opaque alpha. It enforces encoded-input boundaries, a 1024-pixel
limit per dimension and a 4 MiB output cap. The legacy `jdatasrc.cpp` reader blindly
refills without a length; it is excluded, and portable source `LoadJPG` now fails
explicitly rather than using it. Converted texture/source-image loading remains later
work. This is codec acceptance, not SWF presentation or renderer resource loading.

| Resident link gate | Debug | Release |
| --- | ---: | ---: |
| Retained direct inputs | 340 | 340 |
| Unresolved symbols / duplicates | 0 / 0 | 0 / 0 |
| Required registration roots present | 11 / 11 | 11 / 11 |
| Missing map inputs | 0 | 0 |
| Linker / Make exit | 0 / 0 | 0 / 0 |
| Fixed PT_LOAD bytes | 10,846,310 | 11,265,830 |
| Included BSS bytes | 5,237,734 | 5,237,798 |

The inputs are all 281 native campaign, sixteen strict campaign backend, eight strict
resident support, dlmalloc and 34 codec objects. No archives or garbage collection hide
dependencies. Both configurations produce matched resident stripped/unstripped ELFs,
with retained game/class/CVar registration roots and every object verified in the map.
Reports and hashes are under `build/<config>/resident/`. The resident entry still
rejects execution pending M3 startup; it has not been used as a game boot test.
M2b compile/link acceptance is complete. Static image sizes do not measure kernel,
stacks, constructor/startup allocations, initialized game state or transition peaks.

Four required `codecs/` smoke markers cover a known CRC vector; independently authored
wrapped/raw zlib streams with small input/output chunks and compression round trips;
checksum/truncation errors, every partial deflate allocation and lazy inflate-window
allocation failure; and repeated JPEG/full/table/abbreviated decoding. Each scope
recovers the exact requested/backing/count ledger. Five fresh-process JPEG probes reject
null/empty input, overlong markers, truncation, signatures and oversized dimensions
with asserts enabled and disabled. The host fatal probes also require full codec/output
cleanup before the fatal sink, now including the tagged JPEG allocator state.

| Core/codec memory | Debug | Release |
| --- | ---: | ---: |
| Fixed PT_LOAD bytes | 1,862,320 | 1,934,512 |
| Included BSS bytes | 459,824 | 459,824 |
| Initialized requested / backing bytes | 31,425 / 42,704 | 31,425 / 42,704 |
| Initialized allocations | 272 | 272 |
| Synthetic peak requested / backing bytes | 315,905 / 327,668 | 315,905 / 327,668 |
| dlmalloc commitment after tests | 345,424 | 346,960 |
| After-shutdown requested / backing bytes | 1,024 / 1,068 | 1,024 / 1,068 |
| After-shutdown allocations | 1 | 1 |

The larger fixture peak measures compression windows and tiny authored images, not
retail assets or a running campaign. Tagged backing is inside the dlmalloc arena and
must not be added again. Whole-game feasibility and map reload peaks remain M3 work.

| Validation | Result |
| --- | --- |
| Debug/release core and campaign builds | Passed with the real EE compiler; new backend/tests retain strict `-Werror`, vendor warnings stay visible |
| Debug/release resident link | Passed; all 340 objects retained and registrations/map/executable checked |
| Host ASan/UBSan and Python suite | Passed: 66 regressions, including shared codec and three relocation/exclusion audit cases |
| Debug/release core smoke and missing fixture | Passed |
| Debug/release Common/failure matrices | Passed: 52 each, seven partial stages and 45 expected fatals |
| Debug/release script probe builds | Passed |
| Script include and syntax cases in each configuration | Passed; the complete script matrix was not repeated in this slice |
| Source audit / compile database | Passed: 458 shipped units, 88 Classic units excluded, 358 target entries |

Archived final core runs:

- Debug: `20261010T000147Z_smoke_d25c2c48c39c450c`.
- Release: `20261010T000507Z_smoke_f613ee5449bf4cad`.
- Debug 52-probe span: `20261010T000215Z_smoke_d41a90fbe7354024` through
  `20261010T000359Z_smoke_20a53ffbfa9e4bf9`.
- Release 52-probe span: `20261010T000517Z_smoke_0096d0aac71049a7` through
  `20261010T000657Z_smoke_a59297ff113a4fdd`.
- Missing fixture debug/release: `20261010T000714Z_smoke_259f47faa21b4b1f` /
  `20261010T000720Z_smoke_bfafe72445f74b22`.
- Script include/syntax debug: `20261010T000716Z_script_558cafb6d7a44df3` /
  `20261010T000718Z_script_1885970c7ab84c13`.
- Script include/syntax release: `20261010T000722Z_script_b55a5b8e64d247db` /
  `20261010T000725Z_script_2182f4a5de40423e`.

Next is meaningful logical render/sound behavior and authored headless game-fixture
startup: actual class/decl/game initialization, deterministic synthetic commands,
entity/script/collision checks and measured reloads. Game ticks, interpreter execution,
map loading, physical input, GS presentation and SPU2 output are not accepted yet.

## M3 prerequisite: bounded logical PCM samples

On 2026-10-10 the portable sample provider gained real authored-fixture WAV loading,
timing and amplitude. `idSoundSample` owns one tagged AUDIO payload, records the stream
timestamp, reports frames per channel and distinguishes loaded/generated/unloaded data.
The shared `ps2/audio/pcm_wave.*` parser reads headers through a bounded callback;
the adapter then reads only PCM into its final allocation through `OpenFileRead`.
It does not duplicate an entire file in the core's 64 KiB memory-file buffer.

The accepted fixture format is ordinary RIFF PCM, signed little-endian 16-bit,
mono/stereo, 8–48 kHz. Limits are 256 KiB PCM per sample, another 64 KiB for container
metadata and at most 256 chunks. RIFF/chunk sizes, odd padding, duplicate/missing
format/data, frame alignment, rate/byte-rate and format fields are checked before
allocation. Samples shorter than 1 ms are rejected because native emitter looping
uses modulo duration. Unsupported compression/extensible formats remain explicit
failures. These are fixture bounds, not a measured aggregate retail sound-cache budget.

Duration uses exact 64-bit frame/rate arithmetic: 11,025 frames at 11,025 Hz give
1,000 ms instead of the native rounded-rate helper's 1,002 ms. Amplitude is the peak
absolute PCM value over the containing 60 Hz window, across all channels, normalized
by 32,768. Negative/out-of-range times return zero without integer overflow.
Generated defaults contain 256 real mono frames at 8 kHz, lasting 32 ms; they use
the same query path as loaded data.

Rename, reload, explicit purge and destruction release prior payloads while retaining
the native reference/never-purge/last-played metadata. Owning samples cannot be copied.
The stream closes and any temporary payload is freed before reporting a fatal load
failure; fatal logging does not unwind stack objects. Missing/malformed inputs cannot
silently become generated defaults. `_default` names or explicit `MakeDefault` create
the real default sample. Unloaded queries and physical device/voice calls still fail.
No sound cvars, vendor changes or native campaign-source edits were needed.

The core directly exercises standalone sample objects. Native sound-world/shader/emitter
initialization, voice start/pause/loop/completion state, retail `.idwav`/ADPCM loading,
SPU2 playback, renderer logic and `idGameLocal` startup are not accepted by this slice.
M3 game initialization, interpreter execution, authored map ticks and reload acceptance
remain pending.

### Build and runtime evidence

| Gate | Result |
| --- | --- |
| Debug/release `headless-core compile-game link-game` | All passed; new backend/test sources compile with strict `-Werror` |
| Campaign/resident manifest | All 281 campaign units retained; 341 direct resident inputs, 11 registration roots, no GC/unresolved/duplicate symbols |
| Inventory / compile database | 458 shipped units remain classified; 359 target entries; 24 core and 17 campaign backend units |
| Host ASan/UBSan | Shared heap checks and all 71 Python regressions passed |
| Debug/release core smoke | Three required audio markers passed, with exact per-scope ledgers |
| Debug/release Common matrix | All 56 fresh-process scenarios passed in each configuration: seven partial shutdowns and 49 expected fatal probes |
| Debug/release missing fixture | Expected fixture failure accepted; audio checks and final shutdown still passed |

Positive audio checks repeat mono/stereo loading, same-name reloads, purge, name changes,
generated defaults and destruction over three cycles. The 88,200-byte stereo payload
exceeds the memory-file limit and must load through a stream. Independent host parser
fixtures cover valid chunk reordering/odd padding, every truncated prefix, format/layout
errors, read failures and 8,192 deterministic corruptions. The runner stages six authored
WAVs and archives their hashes. Seven audio failure probes cover missing input, unloaded
duration, unsupported format, truncation, chunk overflow, payload budget and device Init;
release observes the same failures with assertions disabled.

Current matched run identities:

- Core debug/release: `20261010T002629Z_smoke_0f5e346848134dfd` /
  `20261010T002850Z_smoke_7c6d31a7084d48a7`.
- Debug 56-probe span: `20261010T002639Z_smoke_ade764ec2df94723` through
  `20261010T002837Z_smoke_0625710d2290408b`.
- Release 56-probe span: `20261010T002910Z_smoke_42eda1ae284d4f89` through
  `20261010T003106Z_smoke_19fd1ff9491c4ed4`.
- Missing fixture debug/release: `20261010T003120Z_smoke_5c3dee26c4fd4e0e` /
  `20261010T003145Z_smoke_f46a7e8a165240fb`.

### Measured memory

| Measurement | Debug | Release |
| --- | ---: | ---: |
| Core fixed `PT_LOAD` residency | 1,869,232 bytes | 1,941,552 bytes |
| Core BSS (included above) | 459,824 bytes | 459,824 bytes |
| Initialized requested / backing / allocations | 31,425 / 42,704 / 272 | 31,425 / 42,704 / 272 |
| Whole smoke peak requested / backing | 315,905 / 327,668 bytes | 315,905 / 327,668 bytes |
| Maximum AUDIO payload requested | 88,200 bytes | 88,200 bytes |
| dlmalloc commitment after tests | 346,704 bytes | 344,016 bytes |
| Final requested / backing / allocations | 1,024 / 1,068 / 1 | 1,024 / 1,068 / 1 |
| Resident fixed `PT_LOAD` residency | 10,850,790 bytes | 11,270,310 bytes |
| Resident BSS (included above) | 5,237,734 bytes | 5,237,798 bytes |

The whole-smoke peak remains dominated by the codec fixtures; samples do not overlap
that phase. AUDIO returns to zero after every loaded/default scope. Arena figures include
untagged newlib allocations and freed capacity; do not add tagged backing again. Resident
sizes describe a static image whose entry rejects game startup. They do not measure
initialized game/sound worlds, voice pools, retail content or map-transition peaks.

## M3 prerequisite: deterministic logical voices

The next sound slice supplies playback state through the native `idSoundVoice` and
`idSoundHardware` interfaces, selected explicitly with `InitHeadless` and a monotonic
microsecond clock. Unconfigured hardware `Init` still rejects physical device startup.
Tests advance a fixture clock directly; they neither sleep to infer completion nor
initialize a native sound world.

The shared timeline preserves fractional playback phase in Q16 microseconds and
quantizes pitch to Q16 in [0, 8], including zero to freeze playback. Segment lengths
derive from actual frame counts/rates rather than integer-millisecond durations.
Start/seek subtracts the lead-in before wrapping a loop, supports different sample
rates across segments and completes an exhausted one-shot immediately. Pause/resume
preserves phase; stop returns to idle; neither resume nor pause resurrects completion.
Pitch changes charge elapsed time to the old rate before selecting the new rate.
Backward clocks fail in release too, and bounded modular arithmetic handles maximum
64-bit clock jumps without overflow or dependence on update frequency.

The adapter allocates all 48 logical voice slots in one 7,296-byte AUDIO block on the
EE. Exhaustion returns null; free scans pool addresses before dereferencing a caller's
pointer and rejects double/foreign frees. Reused slots restore native control defaults.
Allocated voices pin both samples, including self-loops, through idle, stop and
completion until explicit free or hardware shutdown. Sample purge, reload, rename and
destruction reject outstanding pins. Synchronous shutdown releases all references and
the pool, is idempotent and permits reinitialization with the selected clock.

Amplitude queries follow the active sample's 60 Hz pre-gain peak envelope. Paused,
stopped and complete voices return zero; active `SSF_NO_FLICKER` returns one. Gain and
spatial controls remain native metadata; this slice supplies neither mixed output RMS
nor a surround matrix. The base voice constructor is supplied with native defaults,
while surround operations remain explicit failures. Native `SoundVoice.cpp` and its
`s_subFraction` cvar remain excluded.

These are direct logical-service fixtures. Native sound-system/world/channel startup,
shader/emitter integration, retail `.idwav`/ADPCM data, aggregate cache budgets and
audsrv/SPU2 output remain pending. M3 game initialization, interpreter execution and map
loading are not accepted by this slice.

### Acceptance

| Gate | Result |
| --- | --- |
| Debug/release `headless-core`, `compile-game`, `link-game` | Passed; strict backend warnings remain errors |
| Campaign/resident manifest | All 281 campaign units retained; 342 direct resident inputs, 11 registration roots, no GC/unresolved/duplicate symbols |
| Inventory / compile database | 458 shipped units remain classified; 361 target entries; 25 core and 18 campaign backend units, 14 smoke units |
| Host ASan/UBSan | Shared heap checks and all 73 Python regressions passed; shared timeline tests run with assertions enabled and disabled |
| Debug/release core smoke | Passed; all seven required audio markers and exact shutdown ledger recovery |
| Debug/release Common matrix | All 66 fresh-process scenarios passed in each configuration: seven partial shutdowns and 59 expected fatal probes |
| Debug/release missing-fixture scenarios | Expected failure and cleanup passed |

The shared timeline checks one-shot completion at a fractional-millisecond frame
boundary, lead-in/loop transitions at different rates, seek offsets, pause/resume/stop,
fractional pitch and freeze, maximum clocks/seeks and update-cadence independence.
Native fixtures check real PCM envelopes, `SSF_NO_FLICKER`, sample lifetime pinning,
48-slot exhaustion, free/reuse defaults, repeated shutdown/restart and destruction with
live voices. Ten added fatal probes reject an absent/backward clock, unloaded voice
samples, negative seek, invalid pitch, pinned sample purge, double/foreign free, channel
mismatch and unknown flags. Existing physical-device rejection remains required.

Matched run identities:

- Core debug/release: `20261010T005457Z_smoke_4e4a41c49b6d4e40` /
  `20261010T005933Z_smoke_ea882f743f50456a`.
- Debug 66-probe span: `20261010T005605Z_smoke_4d1fecba8508427f` through
  `20261010T005812Z_smoke_ae4b217567874ae4`.
- Release 66-probe span: `20261010T005952Z_smoke_56b9e1a02180489a` through
  `20261010T010203Z_smoke_a8872f943e8c447f`.
- Missing fixture debug/release: `20261010T010230Z_smoke_1f90751eea474a78` /
  `20261010T010256Z_smoke_0cb30a8007b74f29`.

### Measured memory

| Measurement | Debug | Release |
| --- | ---: | ---: |
| Core fixed `PT_LOAD` residency | 1,881,264 bytes | 1,954,480 bytes |
| Core BSS (included above) | 459,824 bytes | 459,824 bytes |
| Initialized requested / backing / allocations | 31,425 / 42,704 / 272 | 31,425 / 42,704 / 272 |
| Whole smoke peak requested / backing | 315,905 / 327,668 bytes | 315,905 / 327,668 bytes |
| Logical voice pool requested | 7,296 bytes / 48 slots | 7,296 bytes / 48 slots |
| Maximum AUDIO request across tests | 88,200 bytes | 88,200 bytes |
| dlmalloc commitment after tests | 346,960 bytes | 347,472 bytes |
| Final requested / backing / allocations | 1,024 / 1,068 / 1 | 1,024 / 1,068 / 1 |
| Resident fixed `PT_LOAD` residency | 10,856,358 bytes | 11,276,966 bytes |
| Resident BSS (included above) | 5,237,798 bytes | 5,237,798 bytes |

The whole-smoke peak remains codec dominated. Sample payload and pool phases do not
overlap that peak, and AUDIO returns to zero after the audio tests. Repeated pool
fill/exhaust/free/reuse/shutdown/restart scopes recover the exact requested/backing/count
ledger. Arena figures include untagged newlib allocations and freed capacity; tagged
backing is already contained within them. The 48-slot pool is a logical-state limit,
not a physical SPU2 voice budget. Resident sizes still describe a static image whose
entry rejects game startup; full sound worlds, retail caches and transition peaks remain
unmeasured.

## M3 initial game fixture: native boot and playerless ticks

The user prioritized getting a simple headless game boot/tick before sound or rendering
integration. The retained resident ELF now has an explicit manifest-selected logic
fixture entry. It runs staged native Common foundation startup, the real loose-file
filesystem and declaration manager, versioned `GetGameAPI` imports, and native
`idGameLocal::Init`. Native event/class registration reports 535 event definitions,
159 classes (including the logic probe), and 496,480 bytes of event callbacks.

The runner authors minimal declarations, both mandatory default script files, and a
worldspawn-only `.map`/script. The fixture's startup method bounds the map to 64 KiB,
rejects primitives and unsupported keys, parses it with `idMapFile`, and runs native
`InitScriptForMap` and `SpawnMapEntities`. A test entity is created through the real
class factory and spawn chain. Every cycle must advance eight native `RunFrame` calls,
eight entity `Think` calls and eight script increments through native `sys.waitFrame`
events. Native 60 Hz time advances to 133 ms by frame eight. Worldspawn's delayed script
starts on the first frame; work after a `waitFrame` resumes on a later native frame.

Three cycles perform native map shutdown and reload. Pending thread/events are canceled,
map script definitions reset and each warm reload recovers the same tagged ledger.
Game, declaration and Common shutdown then return exactly to the pre-boot baseline.
Missing maps and a source-located error in `maps/logic.script` fail usefully in both
configurations. Retail files are neither read nor staged.

This is the first **partial M3 acceptance**. The bounded startup method is separate from
regular `InitFromNewMap`; players, collision/AAS/PVS setup, physical input, sound/render
worlds, shell presentation and saves remain pending. No PCM payload/voice pool or GS/
frame/texture buffers are initialized. An empty native `idUserCmdMgr` is heap allocated
for the frame interface, without injecting player commands. The fixture's `platform`
marker covers entry/manifest/host I/O; `core SKIP` means the core test suite is separate,
although Common foundation startup runs. This does not establish full campaign startup.

Next candidates are native event-driven entity behavior and bounded collision/physics
checks, followed by synthetic player commands as their dependencies become available.
Sound and rendering integration are deferred while headless simulation grows.

### Acceptance

| Gate | Result |
| --- | --- |
| Debug/release `headless-game` | Strict compile and retained-object link passed |
| Campaign/resident manifest | All 281 native campaign units retained; 344 direct inputs, 11 registration roots, no GC/unresolved/duplicate symbols |
| Inventory / compile database | 458 shipped units classified; 363 target entries; 19 campaign backend and 9 resident support units |
| Host ASan/UBSan and classifiers | Shared checks and all 77 Python regressions passed; four game-runner regressions cover incomplete/stale results, tick traces, wrong fatalities and artifact identity |
| Debug/release positive game fixture | All six required game checks and all 24 frame/entity/script traces passed |
| Debug/release missing-map and script-error probes | Expected first fatal and source identity accepted; no crash/watchdog/result file |
| Debug/release core regression | Passed; existing core residency and exact shutdown baseline unchanged |

Resident reports now include matched runnable/unstripped ELF and map hashes. The runner
validates these and the flags before archiving reports, map, response file, settings,
manifest and authored file hashes. Its classifier requires the matching closed result,
all native checks and every tick trace; process exit alone cannot pass. The existing
66-process Common matrix remains separate and was not rerun in this slice.

Matched run identities:

- Game debug/release: `20261010T015305Z_smoke_c83e5988bc694d78` /
  `20261010T015444Z_smoke_d51eea27a8634979`.
- Missing map debug/release: `20261010T015345Z_smoke_22f482aa20ff4356` /
  `20261010T015844Z_smoke_05cce4b8be354a35`.
- Script error debug/release: `20261010T015405Z_smoke_a9c2ef96a4c345b1` /
  `20261010T015906Z_smoke_149c44a655ae45b6`.
- Core debug/release: `20261010T015943Z_smoke_75209349e43a4cad` /
  `20261010T020128Z_smoke_9b451a2d01bf47cf`.

The positive archives also pass the final strengthened classifier requiring all 24
tick traces and the begin marker, checked after the emulator runs.

### Measured memory

| Measurement | Debug | Release |
| --- | ---: | ---: |
| Resident fixed `PT_LOAD` residency | 10,875,622 bytes | 11,296,102 bytes |
| Resident BSS (included above) | 5,237,990 bytes | 5,237,990 bytes |
| After native game Init: requested / backing / count | 1,387,268 / 1,478,856 / 2,242 | 1,387,268 / 1,478,856 / 2,242 |
| After eight ticks: requested / backing / count | 1,477,476 / 1,570,932 / 2,287 | 1,477,476 / 1,570,932 / 2,287 |
| Peak requested / backing | 1,479,588 / 1,573,440 bytes | 1,479,588 / 1,573,440 bytes |
| dlmalloc commitment after three cycles | 1,592,602 bytes | 1,594,010 bytes |
| Full shutdown requested / backing / count | 7,424 / 7,600 / 4 | 7,424 / 7,600 / 4 |

The resident baseline includes more native globals than the core's 1,024-byte allocation;
each executable must recover its own pre-init ledger. Native script reporting counts
3,515,932 bytes of inline static storage already contained in the ELF measurement.
Arena commitment includes tagged backing, untagged libc allocations and freed capacity;
do not add backing again. These figures exclude kernel/stacks and establish only
logic-fixture residency, without player/collision, retail assets or transition peaks.

## M3 small step: script-driven native target activation

The authored script now activates a native `idTarget_SessionCommand` on frame four
through `EV_Activate`. The target spawns through the real native class factory with
only a name, command and `noclipmodel` key; no presentation or collision resources
are acquired. Both target and logic-probe script references bind through native
`idEntity::SetName` after map script compilation. The logic probe supplies the required
non-null activator. Worldspawn was named before its map script compiled, so a newly
declared `$logic_world` reference would remain null; the first EE run caught this and
correctly failed the command/tick/cleanup checks.

Native activation copies `fixture-activated` into the game session-command string.
Native `BuildReturnValue` returns it and clears that string in the same frame. All
three reload cycles require that token only on frame four, empty returns on every
other frame and no pending command after each return. Map shutdown must remove the
target, its script binding and the map's script function. Warm reload and full shutdown
ledgers recover exactly. The test inspects the returned token without passing it to
Common; this does not request a real map transition.

| Gate | Result |
| --- | --- |
| Debug/release `make test-game` | All three scenarios passed in each build: native ticks/activation/reloads, missing map and script error |
| Retained resident link | 344 inputs and all 11 registration roots; no GC, unresolved symbols or duplicates |
| Positive game classifier | Seven required checks, all 24 tick traces and all 24 command traces passed |
| Host ASan/UBSan and classifiers | All 78 Python regressions passed; the added runner check rejects missing, early/late, repeated and unconsumed commands |
| Core and source inventory | Core code/source lists unchanged; 458 shipped units classified, existing 363-entry compile database remains valid; core emulator matrix not rerun |

Matched run identities:

- Game debug/release: `20261010T022506Z_smoke_8ee277e1f5364bfc` /
  `20261010T022541Z_smoke_e8a8389360fd4f64`.
- Missing map debug/release: `20261010T022508Z_smoke_4cb0edcda1294b5e` /
  `20261010T022543Z_smoke_ead5ef8d4a7c494c`.
- Script error debug/release: `20261010T022511Z_smoke_1cff43212a27445a` /
  `20261010T022546Z_smoke_ebde26265f4e4597`.

### Measured memory

| Measurement | Debug | Release |
| --- | ---: | ---: |
| Resident fixed `PT_LOAD` residency | 10,876,518 bytes | 11,296,742 bytes |
| Resident BSS (included above) | 5,237,990 bytes | 5,237,990 bytes |
| After native game Init: requested / backing / count | 1,387,484 / 1,479,328 / 2,248 | 1,387,484 / 1,479,328 / 2,248 |
| After eight ticks: requested / backing / count | 1,479,724 / 1,574,004 / 2,307 | 1,479,724 / 1,574,004 / 2,307 |
| Peak requested / backing | 1,481,836 / 1,576,512 bytes | 1,481,836 / 1,576,512 bytes |
| Full shutdown requested / backing / count | 7,424 / 7,600 / 4 | 7,424 / 7,600 / 4 |

These measurements cover only the logic fixture, not player/collision, media, retail
assets or real map transitions. Sound and rendering remain deferred. Next candidates
were delayed entity-event/lifetime checks and a bounded collision fixture.

## M3 small step: timed entity events and script removal

The resident fixture now posts two native `EV_Activate` events at game time zero.
The 90 ms activation must wait until frame six (100 ms), with no command on frame
five (83 ms). The existing script activation still returns its command on frame
four. Each native return consumes the command in the same frame.

On frame seven (116 ms), the authored script calls the target's native `remove()`
event. `EV_SafeRemove` queues zero-delay deletion; the native destructor invalidates
the spawn generation, removes the entity name and cancels its remaining events.
The cached `idEntityPtr` must stop validating/resolving on that frame. Frame eight
(133 ms) must return no command, proving that the queued 120 ms activation was
canceled. The probe continues thinking and the script continues incrementing through
all eight frames. No freed target pointer is dereferenced by the fixture.

| Gate | Result |
| --- | --- |
| Debug/release `make test-game` | All three scenarios passed in each configuration: timed activation/removal/reloads, missing map and script error |
| Retained resident link | 344 inputs and 11 registration roots, no GC, unresolved symbols or duplicates; strict fixture compilation passed |
| Positive game classifier | Nine required checks; three event-posting traces and 24 each of tick, command and lifetime traces passed |
| Host sanitizer fixtures and classifiers | All 79 Python regressions passed; classifiers reject wrong deadlines, absent posts, early removal, stale handles and canceled-event commands |
| Core and source inventory | Core/source lists unchanged; existing 458-unit inventory and 363-entry compile database remain valid; core emulator matrix not rerun |

Matched run identities:

- Game debug/release: `20261010T080638Z_smoke_73a32da401664f40` /
  `20261010T080718Z_smoke_a8d781ff3c2e44c8`.
- Missing map debug/release: `20261010T080641Z_smoke_bb1907b129af405f` /
  `20261010T080721Z_smoke_cb53a2b0ba3c4acc`.
- Script error debug/release: `20261010T080644Z_smoke_95e024c459e34f3c` /
  `20261010T080723Z_smoke_bbb002312ec84aae`.

### Measured memory

| Measurement | Debug | Release |
| --- | ---: | ---: |
| Resident fixed `PT_LOAD` residency | 10,877,286 bytes | 11,297,510 bytes |
| Resident BSS (included above) | 5,237,990 bytes | 5,237,990 bytes |
| After native game Init: requested / backing / count | 1,387,660 / 1,479,636 / 2,251 | 1,387,660 / 1,479,636 / 2,251 |
| After eight ticks and target removal: requested / backing / count | 1,494,580 / 1,588,900 / 2,307 | 1,494,580 / 1,588,900 / 2,307 |
| Peak requested / backing | 1,498,492 / 1,593,476 bytes | 1,498,492 / 1,593,476 bytes |
| Arena commitment after three cycles | 1,615,514 bytes | 1,617,178 bytes |
| Full game/decl/Common shutdown: requested / backing / count | 7,424 / 7,600 / 4 | 7,424 / 7,600 / 4 |

Native queued event arguments warm a 16 KiB block allocator; deleting the target
does not immediately release that allocator's backing. Warm map-shutdown ledgers
stay exact across reloads and full shutdown recovers the original pre-boot baseline.
These measurements still cover only the playerless logic fixture. The next candidate
is bounded collision/physics initialization and traces, before player simulation.
Sound and rendering integration remain deferred.
