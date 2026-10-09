# Port status and EE foundation acceptance

M0–M2 passed on 2026-10-08 with ps2dev GCC 15.2.0 and PCSX2 2.6.3. The current
foundation now runs real `idCommonLocal::Init` / `Shutdown` through staged core and
offline services. The M2b campaign gate compiles all 281 retained units in debug and
release; logical sound now uses a portable sample/voice/device boundary. Resident game
linking and M3 game initialization remain pending. The sections below preserve each
slice's historical measurements; the latest resident-link evidence is at the end.

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
