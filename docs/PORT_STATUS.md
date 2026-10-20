# Port status and headless acceptance

## Current snapshot — 2026-10-20

The port is not playable yet. The latest rendering slice draws the
[original dialog in full](#original-dialog-drawn-in-full-2d-6-third-slice) through
native `idSWF::Render` (accepted 2026-10-20), after convex SWF masks (`ac9de81`,
2026-10-18). The
[memory budget investigation](#memory-budget-investigation-ui-formats-and-the-retail-boot-2026-10-20)
followed: production now boots the retail declarations and scripts, and the retail UI
art has measured candidate formats. `1f045d8` moved the authored fixtures into
`src/tests/data/` without changing their bytes.

| Area | Implemented and verified boundary | Remaining boundary |
| --- | --- | --- |
| M0–M2 / M2b | EE core startup, synchronous Common/services, retained campaign compile and resident link | Source inclusion is not campaign playability |
| M3 game fixtures | Native startup/events/scripts; restricted player movement, jump/crouch/steps over 331-frame reloads; authored map/world/PVS/collision/AAS and one idle AI | Full campaign player, animated AI/gameplay, retail map conversion and Common map transitions |
| Production loop | Native game initialization, console commands, sustained frames and ordered shutdown; game remains in `NOMAP` | Production map ownership and map simulation |
| Retail data boot | Retail `_common`/`_ordered` containers, every declaration folder and the full `doom_main.script` reach `NOMAP` in a 14.5 MB heap arena, with exact shutdown | Leaves about 6.4–6.8 MB for a level; budget levers measured, not applied |
| 2D-1–4 | GS device/textures/capture, real console, indexed UI, native fonts and an interactive authored SWF | Physical PS2/performance acceptance |
| 2D-5 | Retail inventory/cook, English fonts/layout, bounded original dictionaries/text and scoped timeline/AVM1 execution with minimal native widgets | General dynamic resource closure and active artwork working set |
| 2D-6 | Scoped additive blend 8, bounded convex masks, and the original dialog drawn in full with Common's quit message | Complex/holed/disconnected/stroked masks, screen blend 4, nonzero additive color transforms, and the surveyed paletted/halved page formats |
| 2D-7–8 | Not accepted | Shell drawing, native offline menu navigation, physical input and hardware validation |

`ps2_uiRetail dialog quit` draws the complete original dialog; other retail modes still
show isolated native English labels. Shell drawing and SWF event routing remain gated.
The dialog streams most of its RGBA32 atlas pages every frame, about 1.6 s per frame
in PCSX2, which motivates the asset recompression work selected next. No world
rendering or audible playback is implemented.

The last feature acceptance (2026-10-20) drew the original dialog in full in
debug/release NTSC/PAL, with reload-stable captures, exact warm releases, a negative
admission case and the affected regression gates; see its
[record](#original-dialog-drawn-in-full-2d-6-third-slice). The previous one (2026-10-18)
passed debug/release NTSC/PAL with 77 authored UI pixel checks, twelve invalid-resource
cases, nineteen core renderer cases per build, and bounded/live-quit production
headless regressions. The fixture-move gate also passed
334 host regressions plus C++ sanitizer harnesses, generator verification and
headless/UI staging. See the [convex-mask record](#native-convex-swf-masks-2d-6-second-slice)
for archive IDs, inspected captures and measured memory. [GitHub Actions](#github-actions-ci-2026-10-19)
now builds production debug/release on Linux and the host tools on three hosts. Every
regular target gate [passed again](#script-probe-repair-and-the-rebuilt-toolchain-2026-10-19)
on the toolchain rebuilt on 2026-10-19, including the repaired script probe.

Continue with [AGENT_HANDOVER.md](AGENT_HANDOVER.md) and the
[2D roadmap](RENDERER_2D_PLAN.md). The selected work is asset recompression/re-export
and fitting the game into the 32 MiB budget; its measurements are recorded and the
levers are the next decision. A complete shell frame and responsive offline menu
navigation follow. Required feature/resource admission precedes each
milestone. Preserve headless simulation and extend existing fixtures; further
standalone movement fixtures are paused.

The sections below are an append-only history of acceptance slices. Earlier source
counts, budgets, limitations and suggested next steps describe their dated slice;
they are not current instructions.

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

## M3 small step: bounded native collision and physics

The authored map now contains exactly two six-sided axial brushes: a floor and wall,
with stage-free solid materials. The fixture validates every plane, material and map
key before calling native collision conversion and `idClip::Init`. Unsupported brush
planes/materials fail in both configurations. The worldspawn uses the native collision
model name `worldMap`; no render or sound world is initialized.

Point and swept-box traces hit the floor at the native 0.25-unit clip margin. Tests
also require inside/outside contents, volume-mask rejection, entity identity and
pass-entity filtering, plus clip-model disable/enable. The logic probe owns a native
`idPhysics_Monster` and moves through `idEntity::RunPhysics` each Think. With zero
gravity and fly movement, it reaches x=8.192/16.896 on frames one/two, then stops
around x=21.741 before the wall. Native event/script/removal checks remain active.

Three cycles exercise brush conversion, text `.cm` loading and generated binary
collision-cache loading. The binary reload exposed stale serialized trace visitation:
its first point trace skipped the floor. Tagged portable loader fixes reset runtime
query fields and preserve the loaded counts/byte totals that allocation helpers had
double-counted. Missing `.proc` still emits its native optimization warning; portable
Common shutdown now releases its retained diagnostic lists before idlib teardown.

Every map shutdown must release all COLLISION/PHYSICS_CLIP allocations and empty the
trace-model cache. Warm reload ledgers and full game/decl/Common recovery remain exact.
This remains partial M3 acceptance. Gravity, floor contact and sliding are the next
bounded checks, before synthetic player commands and broader authored-map startup.
Player/AAS/PVS, retail content and sound/render integration remain pending.

### Acceptance

| Gate | Result |
| --- | --- |
| Debug/release `headless-game` and `headless-core` | EE compile and link passed; backend/tests retain strict `-Werror` |
| Retained resident link | 344 direct inputs, 11 registration roots, no GC, unresolved symbols or duplicates |
| Debug/release `make test-game` | All five fresh-process scenarios passed: collision/physics/events/reloads, missing map, script error, restricted geometry and material |
| Positive game classifier | Fifteen required checks, three event/collision traces and 24 each of tick, command, lifetime and physics traces passed |
| Host sanitizer fixtures and classifiers | All 80 Python regressions passed; the added check rejects incomplete traces, wrong collision results, movement, wall stops and physics frame coverage |
| Debug/release core and missing-fixture regressions | Passed with exact cleanup after the shared Common shutdown change |
| Source inventory / compile database | Source lists unchanged; existing 458-unit inventory and 363-entry database remain valid |

Each archive retains matching resident/core reports and image/map hashes. Generated
text/binary collision caches are hashed in `summary.json` after process cleanup.
The separate 66-process Common matrix was not rerun in this slice.

Matched run identities:

- Game debug/release: `20261010T084012Z_smoke_0ed7ec619587485f` /
  `20261010T084112Z_smoke_9a86e7970fab4a49`.
- Missing map debug/release: `20261010T084015Z_smoke_893421e6a00e4dd7` /
  `20261010T084115Z_smoke_a6bcb38e23d24b7b`.
- Script error debug/release: `20261010T084018Z_smoke_3580d4d1a7b14a02` /
  `20261010T084118Z_smoke_8d99e62b62a44e27`.
- Geometry debug/release: `20261010T084020Z_smoke_2cd931d9674348a7` /
  `20261010T084120Z_smoke_55cc6c533bc94c58`.
- Material debug/release: `20261010T084023Z_smoke_b8c54f2f33dc42cd` /
  `20261010T084123Z_smoke_0e84eafd564148bc`.
- Core debug/release: `20261010T084149Z_smoke_58acea26448a4c6c` /
  `20261010T084256Z_smoke_b2814fb4d0cc40f1`.
- Missing core fixture debug/release: `20261010T084647Z_smoke_9ebd381f6b53473e` /
  `20261010T084715Z_smoke_a4e24b2d4f934eee`.

### Measured memory

| Measurement | Debug | Release |
| --- | ---: | ---: |
| Resident fixed `PT_LOAD` residency | 10,881,894 bytes | 11,303,142 bytes |
| Resident BSS (included above) | 5,237,990 bytes | 5,237,990 bytes |
| Core fixed `PT_LOAD` residency | 1,881,520 bytes | 1,954,864 bytes |
| After native game Init: requested / backing / count | 1,396,264 / 1,488,684 / 2,261 | 1,396,264 / 1,488,684 / 2,261 |
| After eight ticks, conversion: requested / backing / count | 1,735,756 / 1,834,088 / 2,406 | 1,735,756 / 1,834,088 / 2,406 |
| After eight ticks, text cache: requested / backing / count | 1,735,772 / 1,833,700 / 2,395 | 1,735,772 / 1,833,700 / 2,395 |
| After eight ticks, binary cache: requested / backing / count | 1,733,524 / 1,831,432 / 2,394 | 1,733,524 / 1,831,432 / 2,394 |
| Peak requested / backing | 2,057,428 / 2,153,928 bytes | 2,057,428 / 2,153,928 bytes |
| Arena commitment after three cycles | 2,335,898 bytes | 2,332,442 bytes |
| Full game/decl/Common shutdown: requested / backing / count | 7,424 / 7,600 / 4 | 7,424 / 7,600 / 4 |

Cache paths have different live allocation layouts, while each map shutdown returns
to the same warm ledger. Core initialization, peak and final tagged ledgers remain
31,425 / 42,704, 315,905 / 327,668 and 1,024 / 1,068 / 1 respectively.
Script inline storage remains included in BSS; arena commitment already contains
tagged backing and freed capacity. These figures exclude kernel/stacks, players,
retail assets and real map transitions.

## M3 small step: gravity, floor rest and grounded sliding

Two additional instances of the native logic-probe class now run through the same
class factory, spawn chain and `idEntity::RunPhysics` as the wall-stop probe. Event,
script and removal checks remain active during all eight frames of each reload.
No engine implementation or source lists changed in this slice.

The falling probe starts at (-32,-32,4) with zero velocity and a fixture gravity of
512 units/s². It must reproduce native old-velocity integration through frame five,
land around z=2.251 on frame six and report ground/rest/zero velocity on frames seven
and eight. Contact checks require the native world floor and an upward solid contact.
Continued Think calls do not reactivate its resting physics.

The sliding probe starts on the floor at (0,32,2.25), with velocity movement and a
zero maximum step height. Its (512,64,0) input hits the wall on frame three, clipping
the inward velocity while preserving tangent speed and ground support. Every later
frame must report the world blocker and native `MM_SLIDING`, with bounded increasing
y and no floor/wall penetration. Native full-delta projection after partial collision
adds about 0.607 y units at first contact; this behavior is recorded in the engine rules.
Stairs, slopes and player/AI simulation remain later work.

### Acceptance

| Gate | Result |
| --- | --- |
| Debug/release `headless-game` | EE compile/link passed; new test code passes strict `-Werror` |
| Retained resident link | 344 inputs, 11 registration roots, no GC, unresolved symbols or duplicates |
| Debug/release `make test-game` | All five scenarios passed in each configuration; assertions-disabled checks remain active |
| Positive game classifier | Eighteen required checks; three event/collision traces and 24 each of tick, command, lifetime, wall-stop, falling and sliding traces |
| Host sanitizer fixtures and classifiers | All 81 Python regressions passed; the added regression rejects missing/incorrect falling, contacts, rest, tangent motion and blocker/movement states |
| Cleanup | All COLLISION/PHYSICS_CLIP allocations and trace-cache entries released at every map shutdown; warm reload and full pre-boot ledgers recover exactly |
| Core and source inventory | Core/source lists unchanged; existing 458-unit inventory and 363-entry compile database remain valid; core/Common emulator matrices not rerun |

Matched run identities:

- Game debug/release: `20261010T085720Z_smoke_1c420aeb364d413f` /
  `20261010T085758Z_smoke_dc2a46f8e6a443fe`.
- Missing map debug/release: `20261010T085723Z_smoke_e709633059484b61` /
  `20261010T085801Z_smoke_30367e322adc4f5f`.
- Script error debug/release: `20261010T085725Z_smoke_8ded7a04dd324a4b` /
  `20261010T085803Z_smoke_f9159322cb0d471a`.
- Geometry debug/release: `20261010T085728Z_smoke_3bcd9906301b4034` /
  `20261010T085805Z_smoke_7507adc3e3d140e8`.
- Material debug/release: `20261010T085730Z_smoke_42a209a3f9564d95` /
  `20261010T085808Z_smoke_9b0763dd73574c81`.

### Measured memory

| Measurement | Debug | Release |
| --- | ---: | ---: |
| Resident fixed `PT_LOAD` residency | 10,885,350 bytes | 11,306,086 bytes |
| Resident BSS (included above) | 5,237,990 bytes | 5,237,990 bytes |
| After native game Init: requested / backing / count | 1,396,264 / 1,488,684 / 2,261 | 1,396,264 / 1,488,684 / 2,261 |
| After eight ticks, conversion: requested / backing / count | 1,741,392 / 1,840,352 / 2,420 | 1,741,392 / 1,840,352 / 2,420 |
| After eight ticks, text cache: requested / backing / count | 1,741,408 / 1,839,964 / 2,409 | 1,741,408 / 1,839,964 / 2,409 |
| After eight ticks, binary cache: requested / backing / count | 1,739,160 / 1,837,696 / 2,408 | 1,739,160 / 1,837,696 / 2,408 |
| Peak requested / backing | 2,057,428 / 2,153,928 bytes | 2,057,428 / 2,153,928 bytes |
| Arena commitment after three cycles | 2,332,442 bytes | 2,333,594 bytes |
| Full game/decl/Common shutdown: requested / backing / count | 7,424 / 7,600 / 4 | 7,424 / 7,600 / 4 |

The extra entities/physics/contact storage raises live map memory while conversion
scratch still determines the peak. BSS and full shutdown remain unchanged. These
figures cover the fixed playerless fixture and exclude kernel/stacks, retail assets
and real map transitions. Sound/render integration remains deferred. The next candidate
is a bounded native player-physics probe driven by synthetic user commands, before
full player spawning.

## M3 small step: native player physics and synthetic commands

A second native fixture class now owns `idPhysics_Player`, spawns through the real
class factory and runs through `idEntity::RunPhysics`. It explicitly enables TH_PHYSICS;
player physics inherits the empty base `Activate`. The fixture still has zero clients
and null render/sound worlds. Native `idPlayer` spawning, presentation and device input
remain pending. No engine implementation, backend provider or source list changed.

The probe uses PM_NORMAL, speed 128, gravity 512, zero step height, a 4-unit box
footprint and the native standing height. One timestamped command enters native
`idUserCmdMgr` per frame and is consumed inside Think before `SetPlayerInput`.
Frames one through four request full forward movement; x velocity rises through
20.48/32.04/43.60 before the wall clips it on frame four around x=21.749. Frame five
requests reverse movement. Frames six through eight release input, and native ground
friction slows the probe to a complete stop around x=21.151.

Each frame requires the correct command, timestamps, read/write cursors and empty
pending queue. Floor checks require native world identity and an upward solid contact,
stable y/z, linked collision ownership and no unintended jump, crouch, step, ladder or
water state. The command buffer resets after each map shutdown, all probe entities
and clip allocations disappear, and warm/full-shutdown ledgers recover exactly.
All existing monster physics, script, activation and removal checks remain active.

### Acceptance

| Gate | Result |
| --- | --- |
| Debug/release `headless-game` | Strict EE compile and retained-object link passed |
| Resident link | 344 inputs, 11 registration roots, no GC, unresolved symbols or duplicates |
| Native registries | 535 event definitions, 160 classes, 496,480 bytes of event callbacks |
| Debug/release `make test-game` | All five fresh-process scenarios passed in each configuration |
| Positive game classifier | Twenty-two required checks; 24 player-input/physics traces in addition to the existing complete event/collision/tick/command/lifetime/monster traces |
| Host sanitizer fixtures and classifiers | All 82 Python regressions passed; the new regression rejects missing/incorrect commands, timestamps, cursors, pending state, acceleration, wall hits, reverse movement, release friction and floor support |
| Core and source inventory | Core/source lists unchanged; existing 458-unit inventory and 363-entry compile database remain valid; core/Common emulator matrices not rerun |

Matched run identities:

- Game debug/release: `20261010T092642Z_smoke_90149322372f403d` /
  `20261010T092730Z_smoke_4ee760c35f224525`.
- Missing map debug/release: `20261010T092645Z_smoke_406428ecaabc42c6` /
  `20261010T092733Z_smoke_129c00d6b1864024`.
- Script error debug/release: `20261010T092648Z_smoke_40b714bcc1ae4b48` /
  `20261010T092735Z_smoke_24b4c8a1fdb44c64`.
- Geometry debug/release: `20261010T092650Z_smoke_442c9d2296e143ee` /
  `20261010T092737Z_smoke_f7c8308f28a7453b`.
- Material debug/release: `20261010T092653Z_smoke_c50c122d8dc64ffa` /
  `20261010T092740Z_smoke_9312320d03884f1e`.

### Measured memory

| Measurement | Debug | Release |
| --- | ---: | ---: |
| Resident fixed `PT_LOAD` residency | 10,889,446 bytes | 11,309,670 bytes |
| Resident BSS (included above) | 5,238,118 bytes | 5,238,118 bytes |
| After native game Init: requested / backing / count | 1,396,272 / 1,488,684 / 2,261 | 1,396,272 / 1,488,684 / 2,261 |
| After eight ticks, conversion: requested / backing / count | 1,747,464 / 1,846,840 / 2,430 | 1,747,464 / 1,846,840 / 2,430 |
| After eight ticks, text cache: requested / backing / count | 1,747,480 / 1,846,452 / 2,419 | 1,747,480 / 1,846,452 / 2,419 |
| After eight ticks, binary cache: requested / backing / count | 1,745,232 / 1,844,184 / 2,418 | 1,745,232 / 1,844,184 / 2,418 |
| Peak requested / backing | 2,057,436 / 2,153,928 bytes | 2,057,436 / 2,153,928 bytes |
| Arena commitment after three cycles | 2,332,442 bytes | 2,334,106 bytes |
| Full game/decl/Common shutdown: requested / backing / count | 7,424 / 7,600 / 4 | 7,424 / 7,600 / 4 |

Conversion scratch still determines the peak; synthetic input reuses the existing
native command-manager buffer. The new class adds 128 BSS bytes and 8 requested bytes
at native Init. No full player entity, retail assets, device input or sound/render world
is initialized. Kernel/stacks and real map-transition peaks remain unmeasured.
Next candidates are native jump/crouch checks, then headless player entity startup.

## M3 small step: native player jumping and crouching

The existing native player-physics probe now continues through 88 `RunFrame` calls
per map. The initial eight movement/script/event checks remain unchanged. Afterward,
the monster probes become inactive so their sliding motion stays inside the bounded
world. Native player physics continues through entity Think, queued user commands and
`RunPhysics`; the authored script remains complete at eight increments, the removed
target stays absent, and frame returns contain no session command or vibration.
No engine/backend implementation, source list, movement cvar or declaration changed.

Jump input stays held on frames nine through 42. Native physics launches at 128
units/s, reaches the 16-unit apex on frame 23 and lands on frame 38. Holding jump
through landing prevents another takeoff. Releasing on frame 43 permits the second
jump on frame 44, which lands on frame 73. Position/vertical velocity follow native
averaged-velocity gravity integration, with fixed x/y and native floor identity on
landing. The fixture compares its airborne deadline in integer milliseconds to avoid
rounding the 500 ms boundary through float conversion.

Combined crouch/jump input on frames 76–84 changes native clip height from 74 to 38
units and cannot launch while crouched. Releasing both buttons restores standing
height on frames 85–88. This checks open headroom only. Low-ceiling rejection, crouch
speed, stairs/slopes, full `idPlayer` startup and device input remain pending. All three
raw/text/binary collision-cache reloads require identical behavior and exact teardown.

### Acceptance

| Gate | Result |
| --- | --- |
| Debug/release `headless-game` | Strict EE compile and retained-object link passed |
| Resident link | 344 inputs, 11 registration roots, no GC, unresolved symbols or duplicates |
| Native registries | Unchanged: 535 events, 160 classes, 496,480 bytes of event callbacks |
| Debug/release `make test-game` | All five fresh-process scenarios passed in each configuration |
| Positive game classifier | Twenty-seven required checks, all existing 24-row trace sequences and 240 additional posture traces |
| Host sanitizer fixtures and classifiers | All 83 Python regressions passed; the new regression rejects missing/duplicated traces, incorrect buttons/timing/cursors, jump/landing/held state, crouch height, standing restoration and completed script state |
| Core and source inventory | Core/source lists unchanged; existing 458-unit inventory and 363-entry compile database remain valid; core/Common emulator matrices not rerun |

Matched run identities:

- Game debug/release: `20261010T093805Z_smoke_96d8a96d73464c5a` /
  `20261010T093848Z_smoke_373417e50bbd4d76`.
- Missing map debug/release: `20261010T093809Z_smoke_7335dfebdc8d46e7` /
  `20261010T093851Z_smoke_c00758913ce44aad`.
- Script error debug/release: `20261010T093812Z_smoke_245a60bfa4414305` /
  `20261010T093854Z_smoke_d99739b13dea4130`.
- Geometry debug/release: `20261010T093815Z_smoke_94cb5947690a49ab` /
  `20261010T093856Z_smoke_562b3d8ea1f84450`.
- Material debug/release: `20261010T093817Z_smoke_6fae80a77f774034` /
  `20261010T093859Z_smoke_72a498aa77c14013`.

### Measured memory

| Measurement | Debug | Release |
| --- | ---: | ---: |
| Resident fixed `PT_LOAD` residency | 10,892,518 bytes | 11,312,486 bytes |
| Resident BSS (included above) | 5,238,118 bytes | 5,238,118 bytes |
| After native game Init: requested / backing / count | 1,396,272 / 1,488,684 / 2,261 | 1,396,272 / 1,488,684 / 2,261 |
| After 88 ticks, conversion: requested / backing / count | 1,747,756 / 1,848,712 / 2,466 | 1,747,756 / 1,848,712 / 2,466 |
| After 88 ticks, text cache: requested / backing / count | 1,747,772 / 1,848,324 / 2,455 | 1,747,772 / 1,848,324 / 2,455 |
| After 88 ticks, binary cache: requested / backing / count | 1,745,524 / 1,846,056 / 2,454 | 1,745,524 / 1,846,056 / 2,454 |
| Peak requested / backing | 2,057,436 / 2,153,928 bytes | 2,057,436 / 2,153,928 bytes |
| Arena commitment after three cycles | 2,333,466 bytes | 2,335,386 bytes |
| Full game/decl/Common shutdown: requested / backing / count | 7,424 / 7,600 / 4 | 7,424 / 7,600 / 4 |

Conversion scratch still determines the peak. Both standing/crouching trace-model
shapes are exercised; map shutdown releases all collision/clip allocations and cache
entries, warm reload ledgers match, and full shutdown recovers the pre-boot baseline.
The fixture keeps zero clients and null sound/render worlds. Kernel/stacks and real
campaign transitions remain unmeasured. The next candidate is bounded headless
`idPlayer` entity startup.

## M3 small step: bounded native player startup and ticks

The resident fixture now spawns a real `idPlayer` in client slot zero through the
native factory and entity/actor `CallSpawn` chain. A fixture-only setup installs
native player physics, empty inventory, linked AI script variables and manual actor
script state. Its constructor executes once per map. Native player and view constructors
skip HUD/PDA and material/fullscreen FX acquisition only when the fixture was explicitly
selected before Common startup. Regular campaign construction stays separate.

After the existing 88 frames, the physics probe stops and its command cursors reset.
Frames 89–99 run through native `RunAllUserCmdsForPlayer`, `RunSingleUserCmd`,
`HandleUserCmds` and the bounded player Think path. That path calls native speed
adjustment, movement, condition updates and actor script execution. Four forward
commands accelerate to 63.8 units/s; seven neutral commands stop at x=-26.192.
The authored idle script advances eleven times, with native floor support, health 100,
empty weapon/inventory state and linked movement conditions checked each frame.
Native PVS acquisition is skipped in fixture mode; no navigation/presentation data
is fabricated. Map shutdown invalidates the player handle, name and client-slot lookup.

The first target run exposed incomplete probe deactivation: native physics deactivation
sets TH_UPDATEVISUALS, which these probes have no Present call to clear. Their Think
methods now respect TH_THINK and their completed visual flags are cleared separately.
This fixes continued monster sliding/out-of-bounds warnings during posture tests and
prevents the old player-physics probe from consuming the native player's commands.

This is restricted headless player acceptance. Only the exact seven spawn keys and
neutral/full-forward commands are accepted. Extra model keys, missing `AI_ONGROUND`
script storage and attack commands are tested failures in fresh processes. Regular
player Init/SpawnToPoint, persistent inventory, weapons, models/joints, HUD/PDA,
achievements, view/campaign behavior and AAS/PVS remain later gates. No retail assets,
sound or render world are initialized.

### Acceptance

| Gate | Result |
| --- | --- |
| Debug/release `headless-game` | Strict EE compile and retained-object link passed |
| Resident link | 344 inputs, 11 registration roots, no GC, unresolved symbols or duplicates |
| Native registries | Unchanged: 535 events, 160 classes, 496,480 bytes of event callbacks |
| Debug/release `make test-game` | All eight fresh-process scenarios passed in each configuration |
| Positive classifier | Thirty-two required checks; all earlier traces plus 33 native-player command/movement/script traces |
| Host sanitizer fixtures and classifiers | All 84 Python regressions passed; native-player regression rejects missing/duplicate traces, command/cursor/timing, movement/floor/health and script constructor/tick errors |
| Separate core smoke | Debug/release core and expected missing-fixture tests passed; core memory unchanged; Common matrix not rerun |
| Source inventory | Unchanged 458-unit inventory and 363-entry compile database |

Matched run identities:

- Game debug/release: `20261010T100503Z_smoke_5161bbf2ce034262` /
  `20261010T100544Z_smoke_3e78bea4d69e4a21`.
- Missing map: `20261010T100507Z_smoke_ed9c7e8758714a3c` /
  `20261010T100548Z_smoke_0599c1638f194f75`.
- Script error: `20261010T100509Z_smoke_92b13dd5904d4501` /
  `20261010T100550Z_smoke_406a368a7a744de4`.
- Geometry: `20261010T100512Z_smoke_83305aea8988456c` /
  `20261010T100553Z_smoke_4608a53e8dfe44a4`.
- Material: `20261010T100514Z_smoke_688768a3c3904220` /
  `20261010T100555Z_smoke_c4e0ec6726064a39`.
- Player arguments: `20261010T100517Z_smoke_6b20bfaff07a42fb` /
  `20261010T100557Z_smoke_91c61cc2e77f44f7`.
- Player script linkage: `20261010T100520Z_smoke_38ab79c79f7b450d` /
  `20261010T100600Z_smoke_349c35d56414453b`.
- Player command: `20261010T100522Z_smoke_d14034e00cfa48e6` /
  `20261010T100602Z_smoke_5716c908ac494664`.
- Core: `20261010T100638Z_smoke_62efff2a732d4192` /
  `20261010T100643Z_smoke_ddc6c8c114384033`.
- Core missing fixture: `20261010T100641Z_smoke_969d0cc56fbb424a` /
  `20261010T100646Z_smoke_45c4659589974dac`.

### Measured memory

| Measurement | Debug | Release |
| --- | ---: | ---: |
| Resident fixed `PT_LOAD` residency | 10,898,406 bytes | 11,318,374 bytes |
| Resident BSS (included above) | 5,238,118 bytes | 5,238,118 bytes |
| After native game Init: requested / backing / count | 1,399,564 / 1,495,080 / 2,334 | 1,399,564 / 1,495,080 / 2,334 |
| After 99 ticks, conversion: requested / backing / count | 1,793,384 / 1,896,880 / 2,528 | 1,793,384 / 1,896,880 / 2,528 |
| After 99 ticks, text cache: requested / backing / count | 1,793,400 / 1,896,492 / 2,517 | 1,793,400 / 1,896,492 / 2,517 |
| After 99 ticks, binary cache: requested / backing / count | 1,791,152 / 1,894,224 / 2,516 | 1,791,152 / 1,894,224 / 2,516 |
| Peak requested / backing | 2,060,728 / 2,160,324 bytes | 2,060,728 / 2,160,324 bytes |
| Arena commitment after three cycles | 2,339,866 bytes | 2,341,786 bytes |
| Full game/decl/Common shutdown: requested / backing / count | 7,424 / 7,600 / 4 | 7,424 / 7,600 / 4 |

Warm map-shutdown ledgers match, and each shutdown frees all collision/clip allocations
and cache entries. Fixed ELF residency includes the script program's inline storage;
tagged backing is part of the arena, not an extra allocation above it. Kernel/stacks
and campaign transitions remain unmeasured. The next candidate is a small native
player script-state transition and jump/crouch command coverage before broader maps.

## M3 small step: native player posture and actor states

The native player fixture now accepts jump/crouch buttons alongside neutral/full-forward
movement. Frames 100–195 continue the same command queue and native player Think
pipeline. With native gravity 1066 and jump height 48, takeoff occurs on frames 100
and 142. Holding jump through frame 140 does not retrigger it after landing; release
on frame 141 rearms the second jump. Combined crouch/jump on frames 181–189 stays
grounded, shrinks clip height to 38 and restores height 74 on release. Four crouched
forward commands use native speed 80, then friction stops the player at x=-24.759.
Eye height follows native interpolation toward 32/68 rather than changing instantly:
42.280 at the last crouched tick and 56.847 after six released ticks.

Authored `FixtureIdle`, `FixtureAir` and `FixtureCrouch` states follow the native linked
conditions through the real actor `setState` event. The next state executes within
the same player tick, with one script increment per frame. The native `getState`
event confirms each function name alongside authored counters: seven state entries,
two jumps/landings, four soft-landing flag ticks, one constructor and 107 script ticks
per map. Health stays 100; inventory/resources and sound/render worlds stay empty.

The initial diagnostic run (`20261010T102342Z_smoke_bcc91c88f9774267`) failed the new
landing expectations. Native floor contact occurs on frames 135/177 at z≈0.310/0.370
while velocity remains downward. The following WalkMove overclips velocity to about
+0.289 before settling. `CrashLand` uses old velocity, so the soft-landing flag spans
two ticks per landing. The checks now preserve this native behavior and distinguish
ground transitions from flag ticks; no engine physics was changed. The first matrix
also caught the missing-field fixture failing during compilation after the new states
referenced that field. Its authored script now avoids that reference, preserving the
intended native `LinkScriptVariables` failure.

### Acceptance

| Gate | Result |
| --- | --- |
| Debug/release `headless-game` | Strict EE compile and retained-object link passed |
| Resident link | Unchanged 344 inputs, 11 registration roots; no GC, unresolved symbols or duplicates |
| Native registries | Unchanged 535 events, 160 classes and 496,480 bytes of event callbacks |
| Debug/release `make test-game` | All nine fresh-process scenarios passed in each configuration |
| Positive classifier | Thirty-seven required checks, all earlier traces and 288 additional player-state traces across three 195-frame reloads |
| Host sanitizer fixtures and classifiers | All 85 Python regressions passed, including missing/duplicate/reordered state traces, every command/physics/view/state field and both negative script shapes |
| Scope and source inventory | No native source or source-list changes; 458-unit inventory and 363-entry compile database remain valid; core/Common emulator matrices not rerun |

Matched run identities (debug / release):

- Game: `20261010T103135Z_smoke_f5fa4eed241e44f2` /
  `20261010T103231Z_smoke_a1d098add8394fdb`.
- Missing map: `20261010T103139Z_smoke_fa50c3adc8af4ff0` /
  `20261010T103235Z_smoke_ab78b21259ce469c`.
- Script error: `20261010T103141Z_smoke_fa50675e49814d98` /
  `20261010T103238Z_smoke_25b14f6c8f1545ea`.
- Geometry: `20261010T103144Z_smoke_33c90549233b48d5` /
  `20261010T103240Z_smoke_381bdd7e6a984fe3`.
- Material: `20261010T103147Z_smoke_b8741f571d8041fb` /
  `20261010T103243Z_smoke_b0161a3ac1d64510`.
- Player arguments: `20261010T103149Z_smoke_bd10ca2e61cd4906` /
  `20261010T103245Z_smoke_a48a0a3a83bc4f16`.
- Player script linkage: `20261010T103152Z_smoke_cfe5b076f54b46ec` /
  `20261010T103248Z_smoke_0eca9351b10147e6`.
- Player command: `20261010T103154Z_smoke_3c923163c3874dd4` /
  `20261010T103250Z_smoke_22c7a7750dd74edd`.
- Player actor state: `20261010T103157Z_smoke_c23bbc8c523c4b87` /
  `20261010T103253Z_smoke_5c5e9a278f304689`.

### Measured memory

| Measurement | Debug | Release |
| --- | ---: | ---: |
| Resident fixed `PT_LOAD` residency | 10,903,526 bytes | 11,323,622 bytes |
| Resident BSS (included above) | 5,238,118 bytes | 5,238,118 bytes |
| After native game Init: requested / backing / count | 1,401,332 / 1,499,096 / 2,386 | 1,401,332 / 1,499,096 / 2,386 |
| After 195 ticks, conversion: requested / backing / count | 1,797,968 / 1,903,668 / 2,579 | 1,797,968 / 1,903,668 / 2,579 |
| After 195 ticks, text cache: requested / backing / count | 1,797,984 / 1,903,280 / 2,568 | 1,797,984 / 1,903,280 / 2,568 |
| After 195 ticks, binary cache: requested / backing / count | 1,795,736 / 1,901,012 / 2,567 | 1,795,736 / 1,901,012 / 2,567 |
| Peak requested / backing | 2,062,496 / 2,164,340 bytes | 2,062,496 / 2,164,340 bytes |
| Arena commitment after three cycles | 2,347,034 bytes | 2,344,730 bytes |
| Full game/decl/Common shutdown: requested / backing / count | 7,424 / 7,600 / 4 | 7,424 / 7,600 / 4 |

The new script functions/counters account for additional native program allocations;
conversion scratch still determines the peak. Every map frees collision/clip ownership
and cache entries, warm shutdown ledgers match, and full shutdown recovers the exact
pre-boot baseline. Arena commitment includes backing and untagged libc allocations;
it is separate from fixed ELF residency. Kernel/stacks and campaign transitions remain
unmeasured. The next candidate is bounded low-ceiling crouch passage and blocked
standing restoration before stairs/slopes and broader campaign player behavior.

## M3 small step: low-ceiling passage and blocked standing

The authored world now contains exactly three axial brushes: the original floor/wall
and a roof from (-24,24,44) to (-16,56,52). The new lane is separate from the earlier
jump tests and preserves world bounds. The geometry gate still validates every plane,
side/material and brush count before native conversion; a roof moved down one unit
is a new expected-fatal scenario. No general geometry or retail map loading is enabled.

After frame 195, the completed probe collision bodies are disabled, and the same
native player is placed at (-48,40,0.25). This is explicit fixture setup, not campaign
spawn/teleport behavior. Frames 196–275 continue native command delivery, movement,
linked conditions and script execution. Standing forward movement stops at x≈-40.25
before the roof. Crouch on frames 212–231 permits passage, then releasing the button
underneath leaves height 38, crouch speed 80 and the authored crouch state active.
Neutral commands stop at x≈-20.812 on frame 239. Released forward input on 240–264
continues at crouch speed until the full shape leaves the roof. Post-move headroom
clears on 264; native CheckDuck restores height 74 on 265. Release friction stops the
player at x≈5.618 by 273. Eye height follows physical posture, remaining near 32
under the roof and interpolating toward 68 after standing. By 275 the actor has nine
state entries, 187 script ticks and one constructor; jump/landing counters stay unchanged.

Native standing/crouch swept-box traces prove the roof blocks only the taller shape,
and roof/gap contents are checked. The 36-unit upward headroom sweep is blocked at
fraction (44−0.25−38−0.25)/36≈0.15278 while the crouched shape overlaps the roof.
Every command cursor/time, position/velocity, height/eye value, floor identity,
AI/script state and native actor function name is checked across conversion, text-cache
and binary-cache loading. Health stays 100, and presentation/AAS/PVS stay absent.

The initial diagnostic run (`20261010T110003Z_smoke_a87313ab316c4a25`) passed the new
geometry/movement/release checks but failed its linked-condition expectation:
native `UpdateConditions` clears `AI_FORWARD` below speed 5 even when forward remains
held against the roof. The check now preserves that distinction. No native engine
physics or condition code was changed.

### Acceptance

| Gate | Result |
| --- | --- |
| Debug/release `headless-game` | Strict EE compile and retained-object link passed |
| Resident link | Unchanged 344 inputs, 11 registration roots; no GC, unresolved symbols or duplicates |
| Native registries | Unchanged 535 events, 160 classes and 496,480 bytes of event callbacks |
| Debug/release `make test-game` | All ten fresh-process scenarios passed in each configuration |
| Positive classifier | Forty-three required checks, all earlier traces, three headroom-world traces and 240 ceiling-lane traces across three 275-frame reloads |
| Host sanitizer fixtures and classifiers | All 86 Python regressions passed; missing/duplicate/reordered roof/lane traces and every input/physics/view/state field are rejected |
| Scope and source inventory | No native source or source-list changes; 458-unit inventory and 363-entry compile database remain valid; core/Common emulator matrices not rerun |

Matched run identities (debug / release):

- Game: `20261010T110257Z_smoke_7d4512610cd24cd8` /
  `20261010T110410Z_smoke_cfc8e4be92f54bb3`.
- Missing map: `20261010T110301Z_smoke_ea410cf999b34ee4` /
  `20261010T110414Z_smoke_001ce5d02eb0405d`.
- Script error: `20261010T110304Z_smoke_a53c235c05304b12` /
  `20261010T110417Z_smoke_bfc23e8e53c14dd1`.
- Floor geometry: `20261010T110306Z_smoke_41e1614a693c4641` /
  `20261010T110419Z_smoke_f602e13ddcbd4c74`.
- Material: `20261010T110309Z_smoke_1c4fea42b37b4a22` /
  `20261010T110422Z_smoke_287cfdd4fb6044db`.
- Player arguments: `20261010T110311Z_smoke_b37a2594d6ef4e14` /
  `20261010T110424Z_smoke_e22918dd875c44fa`.
- Player script linkage: `20261010T110314Z_smoke_2d8e93d206bc4c05` /
  `20261010T110427Z_smoke_7476a1e6339e4c82`.
- Player command: `20261010T110317Z_smoke_39463e3bbe8c45e7` /
  `20261010T110429Z_smoke_9ff847d56b8342af`.
- Player actor state: `20261010T110320Z_smoke_bd51b58229ba4248` /
  `20261010T110432Z_smoke_5fa50d9d9cb94e4f`.
- Roof geometry: `20261010T110322Z_smoke_6ce452b8be7c4a42` /
  `20261010T110435Z_smoke_3c5bff40949241c8`.

### Measured memory

| Measurement | Debug | Release |
| --- | ---: | ---: |
| Resident fixed `PT_LOAD` residency | 10,909,030 bytes | 11,328,614 bytes |
| Resident BSS (included above) | 5,238,118 bytes | 5,238,118 bytes |
| After native game Init: requested / backing / count | 1,401,332 / 1,499,096 / 2,386 | 1,401,332 / 1,499,096 / 2,386 |
| After 275 ticks, conversion: requested / backing / count | 1,799,972 / 1,906,528 / 2,600 | 1,799,972 / 1,906,528 / 2,600 |
| After 275 ticks, text cache: requested / backing / count | 1,799,988 / 1,905,880 / 2,582 | 1,799,988 / 1,905,880 / 2,582 |
| After 275 ticks, binary cache: requested / backing / count | 1,797,796 / 1,903,676 / 2,581 | 1,797,796 / 1,903,676 / 2,581 |
| Peak requested / backing | 2,065,148 / 2,167,856 bytes | 2,065,148 / 2,167,856 bytes |
| Arena commitment after three cycles | 2,349,722 bytes | 2,347,930 bytes |
| Full game/decl/Common shutdown: requested / backing / count | 7,424 / 7,600 / 4 | 7,424 / 7,600 / 4 |

The added roof and shape sweeps increase collision/cache allocations; native Init
and script storage remain unchanged. Conversion scratch still determines the peak.
Every map releases collision/clip ownership and trace-model cache entries, warm
shutdown ledgers match, and full shutdown returns exactly to the pre-boot baseline.
Fixed ELF residency and arena commitment are separate; tagged backing is already
inside the arena. Kernel/stacks and campaign transitions remain unmeasured. The next
candidate is native step-up coverage with a traversable step and an obstacle above
the configured step height, before slopes and broader campaign loading.

## M3 small step: native step-up and high-rise blocking

The authored world now contains exactly five axial brushes: the existing floor,
wall and roof, a 12-unit platform from (-24,-56,0) to (0,-24,12), and a higher box
from (0,-56,0) to (8,-24,36). The second rise is 24 units, exceeding the unchanged
native `pm_stepsize=16`. This lane at y=-40 is separate from the earlier movement
and ceiling tests. The geometry gate validates every plane/material and brush count;
raising the low platform by one unit is a new expected-fatal scenario before loading.

Frames 276–331 reposition the same native player at (-48,-40,0.25) and continue the
native command/Think/physics/script pipeline. Forward input through 315 climbs the
12-unit platform on frame 283 without a jump, then stops near x=-16.25 against the
high rise on 293. The climb reports one positive signed step of 12; native
`HasSteppedUp` alone also covers downward steps. Ground contacts and linked idle
state persist. Neutral input on 316–331 requires zero velocity at z=12.25. Eye
height continues toward 68; by frame 331 the actor has 243 script ticks, nine state
entries and one constructor. Health, empty resources and jump/landing counts stay
unchanged. General stairs, slopes and campaign player behavior remain outside this gate.

The first diagnostic run (`20261011T074006Z_smoke_12ea8accc8c24529`) passed all six
new step checks but failed the existing floor point-contents check on all three
reloads. Five brushes trigger native axial BSP construction. Its filtering retains
spanning brushes on ancestor nodes, while upstream `PointContents` visited only the
leaf. The tagged fix visits the leaf and each ancestor, preserving bounds/plane tests
and native unmasked point semantics. Existing floor inside/outside and volume-mask
checks cover conversion, text `.cm` and generated binary `.bcm` reloads.

### Acceptance

| Gate | Result |
| --- | --- |
| Debug/release `headless-game` | Strict EE compile and retained-object link passed |
| Resident link | Unchanged 344 inputs and 11 registration roots; no GC, unresolved symbols or duplicates |
| Native registries | Unchanged 535 events, 160 classes and 496,480 bytes of event callbacks |
| Host sanitizer fixtures and classifiers | All 87 Python regressions passed; 14 game classifier cases include missing/duplicate/reordered step traces and mutations of every trace field |
| Debug/release `make test-game` | All eleven fresh-process scenarios passed in each configuration after the point-contents fix |
| Positive contract | Forty-nine required checks, all earlier traces, three step-world traces and 168 step-lane traces across three 331-frame reloads |
| Scope and source inventory | One tagged native collision fix; source lists unchanged, 458-unit inventory and 363-entry compile database remain valid; core/Common emulator matrices not rerun |

Matched run identities (2026-10-11):

| Scenario | Debug | Release |
| --- | --- | --- |
| Game | `20261011T082250Z_smoke_29615e1b703f43a3` | `20261011T082337Z_smoke_a6edd35ee1894b2b` |
| Missing map | `20261011T082255Z_smoke_54884d01031642be` | `20261011T082341Z_smoke_5b2151dda7a54639` |
| Script error | `20261011T082257Z_smoke_5a3303429d174d4d` | `20261011T082343Z_smoke_04cf9a111e514923` |
| Floor geometry | `20261011T082300Z_smoke_b96a346a729b4d01` | `20261011T082346Z_smoke_f9a5197bdc6745e6` |
| Material | `20261011T082302Z_smoke_afca1fa853e34599` | `20261011T082349Z_smoke_612cd13b6bb84871` |
| Player arguments | `20261011T082305Z_smoke_2311766d19344206` | `20261011T082351Z_smoke_7b825066b76c4c8e` |
| Player script linkage | `20261011T082308Z_smoke_808f39c6a1544821` | `20261011T082354Z_smoke_36d7360a288b4667` |
| Player command | `20261011T082310Z_smoke_21c202ccaf284afc` | `20261011T082356Z_smoke_85ba9ffa32654d0f` |
| Player actor state | `20261011T082313Z_smoke_5a093bfbaf1046cb` | `20261011T082359Z_smoke_204871bd01384f01` |
| Roof geometry | `20261011T082316Z_smoke_8da5dd0e902f499e` | `20261011T082402Z_smoke_64aade9e97234950` |
| Step geometry | `20261011T082319Z_smoke_8891dbdc8e554470` | `20261011T082404Z_smoke_b8b510b1e0934009` |

### Measured memory

| Measurement | Debug | Release |
| --- | ---: | ---: |
| Resident fixed `PT_LOAD` residency | 10,913,894 bytes | 11,333,222 bytes |
| Resident BSS (included above) | 5,238,118 bytes | 5,238,118 bytes |
| After native game Init: requested / backing / count | 1,401,332 / 1,499,096 / 2,386 | 1,401,332 / 1,499,096 / 2,386 |
| After 331 ticks, conversion: requested / backing / count | 1,810,892 / 1,919,088 / 2,640 | 1,810,892 / 1,919,088 / 2,640 |
| After 331 ticks, text cache: requested / backing / count | 1,810,908 / 1,918,004 / 2,611 | 1,810,908 / 1,918,004 / 2,611 |
| After 331 ticks, binary cache: requested / backing / count | 1,801,868 / 1,908,908 / 2,609 | 1,801,868 / 1,908,908 / 2,609 |
| Peak requested / backing | 2,077,364 / 2,181,712 bytes | 2,077,364 / 2,181,712 bytes |
| Arena commitment after three cycles | 2,353,050 bytes | 2,351,514 bytes |
| Full game/decl/Common shutdown: requested / backing / count | 7,424 / 7,600 / 4 | 7,424 / 7,600 / 4 |

The additional brushes and BSP nodes raise collision/cache allocations and conversion
scratch, while native game Init and script storage remain unchanged. Every map releases
collision/clip ownership and trace-model cache entries, warm ledgers match and full
shutdown returns exactly to the pre-boot baseline. Fixed ELF residency and arena
commitment are separate; tagged backing is already inside the arena. Kernel/stacks,
full campaign state and transition peaks remain unmeasured.

The next candidate at this stage was bounded native slope coverage. The user
subsequently redirected work to the authored native map path recorded below.


## M3: native authored map, logical world/PVS and idle AI

The user's priority is now progressing toward campaign map startup headlessly,
without another series of movement fixtures. One integrated `game-map` scenario,
selected by `make test-map`, uses committed authored data in
`src/tests/data/headless_map/` and real `idGameLocal::InitFromNewMap` / `MapPopulate`.
It contains worldspawn, one solid floor, two CPU floor models, two logical render areas,
one portal, two hand-authored convex AAS cells and one native `idAI`.

Native declaration/model/world ownership, PVS construction, collision conversion,
AAS loading/routing and entity/actor/AI spawning run through retained engine code.
The AI has an explicit 16×16×32 collision box, no mesh/animation, an idle actor state
and `neverDormant` for the playerless run. Its script queries the opposite AAS cell
and an out-of-region goal; the latter must return -1, proving the actor retained its
AAS binding rather than using the no-AAS distance fallback. Both map and AI scripts
advance once on each of 60 native game frames, over three map reloads.

Logical renderer initialization owns CPU models/worlds without device/frame/texture
arenas. Its frame clock starts at one and advances with game ticks: native model
queries otherwise return an uninitialized null cache at frame zero. Native model
trace also needs the `collision` material flag on stage-free surfaces and clockwise
indices for the upward-facing floor. Point/bounds area queries, static model tracing,
portal state and open/closed/reopened PVS checks now pass. The native AAS reader accepts
the authored map's actual geometry CRC; routing crosses its two cells and rejects
out-of-region positions. A native clip trace verifies the floor and the actor retains
native ground contacts. The empty native sound world supplies required map-startup
bookkeeping; `s_noSound=1`, no playing sound world and no physical renderer are verified.

One tagged startup seam skips automatic lobby-player spawning for the selected headless
map mode. The old bounded player fixture keeps its separate startup and null worlds;
ordinary game startup retains its player synchronization. Loading pacifiers return
only when the initialized logical renderer explicitly supplies the headless loading
path. Presentation capabilities still fail explicitly. This does not execute Common's
campaign map-change pipeline or normal campaign player startup, animated AI/navigation/
combat, retail content, drawing or audible playback. M3 remains partial.

### Acceptance

| Gate | Result |
| --- | --- |
| Debug/release `headless-game` and `headless-core` | EE compile/link passed; new backend/test code passes strict `-Werror` |
| Resident link | 345 direct inputs, 11 registration roots; no GC, unresolved symbols or duplicates |
| Native authored map | Seventeen distinct required checks; 60 native frames on each of three reloads; warm map shutdown and exact full teardown pass |
| Existing game/core/renderer contracts | Eleven existing game scenarios, core positive and eight renderer negatives passed per configuration; final native map and existing positive game rerun after the silhouette scratch fix |
| Host sanitizer fixtures/classifiers | All 88 Python regressions passed; new classifier rejects missing query/tick/reload evidence, stale identities, crashes and timeouts |
| Source audit/editor tooling | 458-unit BFG inventory unchanged; one cold resident support unit added; compile database regenerated to 364 entries |

Final native map runs (2026-10-11): debug
`20261011T085646Z_smoke_00b8de4c38964b7c`, release
`20261011T085653Z_smoke_ff579fc1a6344ce1`. Final existing game positives:
`20261011T085649Z_smoke_410f8035a14340a8` /
`20261011T085657Z_smoke_be241da17ade4079`. Core positives:
`20261011T085424Z_smoke_2e17ae722a5d439f` /
`20261011T085519Z_smoke_fe797d4fb6474ae7`. All forty-two pre-scratch-fix contract runs
are listed locally in `build/map-target-tests.log`; final map/game runs are in
`build/map-final-target-tests.log`. Broader Common/emulator matrices were not rerun.

### Measured memory

| Measurement | Debug | Release |
| --- | ---: | ---: |
| Resident fixed `PT_LOAD` residency | 10,920,422 bytes | 11,338,214 bytes |
| Resident BSS (included above) | 5,238,118 bytes | 5,238,118 bytes |
| First 60-frame map: requested / backing / count | 2,220,520 / 2,330,300 / 2,681 | Same |
| Second 60-frame map | 2,220,536 / 2,330,316 / 2,681 | Same |
| Third 60-frame map | 2,220,256 / 2,330,060 / 2,681 | Same |
| Tagged peak requested / backing | 2,523,851 / 2,628,112 bytes | Same |
| dlmalloc arena commitment after map/game/logical teardown | 2,645,530 bytes | Same |
| Full Common/decl/game teardown requested / backing / count | 7,424 / 7,600 / 4 | Same |

The first measurement exposed native `R_IdentifySilEdges` reserving desktop-sized
scratch granularity for tiny models: peak requested/backing was 8,175,854 / 8,276,688
bytes, with approximately 12 MB arena commitment. The PS2 path now bounds silhouette
list/hash granularity by the surface's index count, capped at the existing native limit;
at most one edge can be created per index. It preserves edge matching and reduces
this map's tagged peak to 2.52 MB and arena commitment to 2.65 MB. Tagged backing is
already inside the arena, so do not add it again. These measurements exclude kernel
reservation and stacks, and establish no retail campaign or transition budget.

Next extend the same authored map with native AI movement and CPU model/joint behavior,
then campaign player startup and Common map transitions. Retail map dependency inventory,
conversion and residency remain subsequent gates. No additional standalone movement
fixtures are scheduled for this phase.


## Production loop and native test executable separation

Production `make` / `make release` now build `build/<config>/d3bfg.elf` with the
full retained campaign set and a dedicated `src/ps2/system/main.cpp`. The entry
preserves the loader's IOP/HostFs, initializes real Common services, repeatedly
calls `common->Frame()` with approximately 60 Hz idle pacing and shuts down after
a frame-boundary quit request. Common currently executes commands and offline
services only; it does not load a campaign map, call native game `RunFrame`, sample
physical input or submit presentation. Those stages remain the next production
integration work.

The production native filesystem uses `host:/base` and requires `default.cfg`;
it does not execute that desktop config. An optional `base/d3bfg.cfg` can queue
port settings and `quit` / `exit`. Production quit requests keep services alive
until the current frame returns. Native directory startup exposed a path-contract
mismatch: `AddGameDirectory` unconditionally removed the last character even when
`BuildOSPath` returned no trailing slash. The tagged portable path now strips only
an actual slash, and production logs confirm scanning `host:/base`.

Core/platform drivers moved to `src/tests/smoketests/main.cpp`, and the campaign
entry to `game_main.cpp`. Fixture helpers moved from `src/ps2/game/` into
`src/tests/smoketests/headless_fixture.*`. Tagged engine fixture hooks and fixture
method declarations compile only with `PS2_D3BFG_TESTS=1`; production uses `0`.
Separate campaign/test object and flag trees prevent either variant from reusing
the other's objects. Production source auditing and final link checks reject test
sources, transitive test headers and fixture symbols. No linker GC hides fixtures.

All native test variants are named `d3bfg_tests.elf`, with matching symbols/map:
`build/<config>/tests/core/`, `tests/platform/` and `tests/game/`; the compiler probe
uses `build/<config>-script/tests/script/`. Smoke commands and archived artifacts
now use the test name and select those paths explicitly. Production reports are
`build/<config>/report.{json,txt}`; campaign test reports live in `tests/game/`.

### Acceptance

- Debug/release production and campaign/core tests compile/link with the real EE
  compiler; strict backend/test sources pass `-Werror`.
- Production retains 342 direct inputs and all eleven registration roots with zero
  unresolved/duplicate symbols, zero test inputs and zero fixture symbols. Campaign
  tests retain 345 direct inputs; both links disable GC.
- Four production PCSX2 runs pass: sustained idle startup and config-driven delayed
  quit in each configuration, with completed reverse Common shutdown on quit.
- Eight relocated test runs pass: core, authored native map, bounded native player
  and expected missing-map failure in debug and release. The SDK-only platform probe
  and one positive isolated compiler probe also pass from their new locations.
- All 92 host regressions pass, including four new production-isolation checks and
  existing sanitizer harnesses. The 458-unit source inventory remains unchanged;
  the compile database now has 365 entries. Broader Common and negative-game matrices
  were not rerun for this separation.

Production fixed `PT_LOAD` residency is 10,858,662 bytes debug / 11,278,374 bytes
release. Campaign test residency is 10,920,422 / 11,338,342 bytes. These are fixed
ELF measurements, not total runtime or campaign memory budgets.

Production run archives (2026-10-11): debug idle
`20261011T101838Z_production_a442fd9c4e09`, debug quit
`20261011T101841Z_production_016323a91f60`, release idle
`20261011T101843Z_production_9577769389e5`, release quit
`20261011T101846Z_production_1108673fb5de`.
Relocated core/map/game/missing-map runs, in that order: debug
`20261011T102029Z_smoke_fe00815459934bbc`,
`20261011T102031Z_smoke_b987a6dfc4504e30`,
`20261011T102035Z_smoke_84464c3b71514e39`,
`20261011T102039Z_smoke_fff60da7f61d4dfe`; release
`20261011T102041Z_smoke_695beec02293409b`,
`20261011T102044Z_smoke_ad348b9eaed64489`,
`20261011T102047Z_smoke_41f038d00cdc4c98`,
`20261011T102051Z_smoke_93af0ff1591b4d40`.
Platform run: `20261011T101920Z_smoke_8375976291554570`.
Compiler probe: `20261011T102158Z_script_a88374ca45804d95`.

Next connect native headless map ownership/startup and game ticks to the production
Common loop, retaining the authored map test as the regression boundary. Full
campaign player startup, animated AI, map transitions and retail resource residency
remain pending. No additional gameplay fixtures were introduced by this split.

## Single test-enable define

`PS2_D3BFG_CORE_TESTS` has been removed. Native test code uses
`PS2_D3BFG_TESTS=1`, while production uses `0`. Make's `CORE_BOOT` selector now
chooses the core-stage implementation or a small SDK-only skip provider through
explicit source lists. The shared entry preserves filesystem initialization before
platform checks, core BEGIN/PASS/FAIL markers and the platform variant's core SKIP
result. `ID_PS2_CORE` and `PS2_D3BFG_FOUNDATION` remain unchanged.

Debug/release production, core, SDK-only platform and campaign test builds pass
with the real EE compiler and strict backend warnings. Production still retains
342 direct inputs with no test inputs or fixture symbols; campaign tests retain
345. The debug isolated compiler probe also builds. The four source-inventory
host regressions pass, and the compile database now includes 366 entries. No new
gameplay fixtures were added.

PCSX2 acceptance on 2026-10-11 covers both configurations' platform/core success,
expected missing-fixture failure and existing authored native map:

| Check | Debug archive | Release archive |
| --- | --- | --- |
| Platform | `20261011T105707Z_smoke_472bd63c2197403a` | `20261011T105728Z_smoke_bdcdf2f76e2a436b` |
| Core | `20261011T105758Z_smoke_3bcb05e339114732` | `20261011T110010Z_smoke_180d9f8a11f34092` |
| Missing fixture | `20261011T110007Z_smoke_fe86b2154b1b4912` | `20261011T110013Z_smoke_7c4fb37c2768436a` |
| Native map | `20261011T110015Z_smoke_baf0d7131c54463f` | `20261011T110018Z_smoke_2c0832cec851465f` |

The debug compiler probe's positive case also passes:
`20261011T110101Z_script_13d2f6493fb74675`.

The broader Common/player negative matrices and full host suite were not rerun
for this define/source-selection change. The next production integration work
remains native headless map startup and game ticks in the Common loop.

## Production native headless console startup

The production executable now continues past foundation startup into native
declarations, key/console/event services, static game API import and actual
`idGameLocal::Init`. It allocates CPU render-world and silent sound-world ownership
without a graphics/audio device, opens the retained native console and stays in
`GAMESTATE_NOMAP`. Common polls console input, runs native events/commands and offline
services, then calls native `RunFrame` synchronously with approximately 60 Hz pacing.
The native NOMAP return does not advance map frames. Menu SWF and campaign map/player
startup remain separate integration gates.

`make headless` stages a runnable production package at `build/<config>/headless/`,
using small authored scripts, `aas_types` and stage-free materials from
`src/tests/data/headless/`. It preserves packaged port settings and does not alter local
retail assets. `make run` / `make run-headless` launch it. Append complete command lines
to the package's `console.txt`; the backend delivers native `SE_CONSOLE` events,
echoes commands and retains native in-memory history. `quit` / `exit` finish the
current Common frame, then release game/world/console services before foundation
shutdown. `ps2_maxFrames` defaults to zero for continuous operation; a positive limit
requests the same ordered shutdown. `ps2_status` reports completed frames and state.

Input polling is bounded to 1 KiB per 100 ms, with 1,023-byte command lines. Partial
lines wait for newline; oversized lines and embedded control characters are rejected
as whole lines. Native command-buffer `wait` executes once per Common frame. Native
history is freed before idlib teardown. The NOMAP path exposed `gameReturn_t`'s
uninitialized session-command array; the backend clears it before dispatch rather
than treating untouched output as a transition request.

### Acceptance (2026-10-11 UTC)

- Debug/release production and core/campaign tests compile and link with the real EE
  compiler and strict backend warnings. Production retains 343 direct inputs, eleven
  registration roots and no test inputs or fixture symbols; campaign tests retain 345.
- Both production configurations pass a 120-frame limit and a continuous console run
  stopped by live `quit` after 152 debug / 153 release frames. Native console/NOMAP,
  disabled output devices, partial-line waiting, invalid-line rejection, command history,
  reverse shutdown and emulator exit status zero are required.
- Existing core, authored native map and bounded native-player smoke runs pass in both
  configurations. No new gameplay fixtures are linked into production or tests.
- All 99 host regressions pass, including seven new production-run classification checks
  for stale identities, incorrect state/counts, crashes, missing cleanup, leaked allocations,
  invalid-input acceptance and mirrored log streams. The compile database has 367 entries;
  the 458-unit native source inventory is unchanged. Broader Common/failure matrices were
  not rerun for this production-only integration.

| Measurement | Debug | Release |
| --- | ---: | ---: |
| Fixed production `PT_LOAD` | 10,863,078 bytes | 11,282,726 bytes |
| Ready tagged requested / backing / count | 1,845,319 / 1,942,752 / 2,384 | Same |
| Live-console peak requested / backing | 1,845,560 / 1,943,152 | Same |
| Arena peak commitment | 1,957,402 bytes | 1,959,642 bytes |
| Pre-init and full-shutdown requested / backing / count | 7,424 / 7,600 / 4 | Same |

Tagged backing is already inside the arena; do not add it twice. These measurements
exclude kernel reservation and stacks, and do not establish a campaign map budget.

| Production check | Debug archive | Release archive |
| --- | --- | --- |
| 120 frames | `20261011T125720Z_headless_adc21fbe417d` | `20261011T125733Z_headless_cfda241e40dc` |
| Live console / quit | `20261011T125724Z_headless_6a7d97b2ff52` | `20261011T125737Z_headless_6bbb3cb1c411` |

Core/map/player regression archives, respectively: debug
`20261011T125818Z_smoke_10403214d980462e`,
`20261011T125820Z_smoke_c0287bba6a864c9e`,
`20261011T125823Z_smoke_6cf23bd378f74e27`; release
`20261011T125828Z_smoke_893045d2ec674dae`,
`20261011T125830Z_smoke_e8d7f16e06c34f3f`,
`20261011T125833Z_smoke_b66eae4cc6b14fdf`.

Next connect native map loading, game time and simulation to this production Common
loop, preserving the existing authored map regression. Menu assets, full player startup,
animated AI and retail resource residency remain pending.

## CPU-only GS bring-up (2D-1)

The first approved renderer slice passes on 2026-10-14 with GCC 15.2 and PCSX2 2.6.3
Metal. It adapts Quake II GS/video/GIF/frame/command code into a CPU-only backend,
with owned tagged command storage, inline packet headers, release preflight and finite
DMA/FINISH/VSYNC deadlines. Device startup claims/resets VIF/GIF as well as GS and
restores the previous interrupt mask on teardown. This is the first M4 slice; it
does not establish native console/SWF/menu drawing, textures, 3D or physical-console
acceptance. The native render-system providers stay inactive and production still
boots the headless Common/game loop without fixtures.

One integrated core-test scene draws a 4×4 color grid, clipped screen-edge borders,
ordered solid alpha layers and a moving/final-frame marker. Each TV standard runs
120 presented frames, holds the last picture without another BeginFrame, then tests
pressure and reinitializes with smaller command halves. Native checks require both
contexts, exact frame counts, bounds, drains and baseline ledger recovery. Three
negative cases verify GIF preflight, oversized DIRECT rejection and a deliberately
stalled deadline with assertions enabled and disabled.

| Measurement | Debug | Release |
| --- | --- | --- |
| Production resident inputs / test inputs or symbols | 347 / none | 347 / none |
| Fixed production `PT_LOAD` | 10,875,686 bytes | 11,293,990 bytes |
| GS scene startup/final tagged requested / backing / count | 1,047 / 1,176 / 2 | Same |
| GS scene live/peak tagged requested / backing / count, 128 KiB halves | 263,191 / 263,412 / 3 | Same |
| Scene arena peak commitment | 280,656 bytes | 282,192 bytes |
| Command-half peak, 128 KiB / 1 KiB | 131,008 / 1,024 bytes | Same |
| Kicks / drains, primary run then small-half reload | 123 / 1; 230 / 224 | Same |
| Fixed VRAM: NTSC 512×448 / PAL 512×512 | 1,376,256 / 1,572,864 bytes | Same |

The scene's heap baseline belongs to its isolated core-stage process, not production's
game startup. Packet peak includes chain tags/terminators; VRAM is two PSMCT16 color
buffers plus Z16. Depth writes and dithering are disabled. No texture/GUI vertex pool
is allocated. These are functional/residency observations, not throughput measurements.

| Target gate | Debug archive | Release archive |
| --- | --- | --- |
| NTSC scene, pressure and reload | `20261014T055815Z_smoke_991bd72c0df3489b` | `20261014T055834Z_smoke_da4e14ee2be54fd3` |
| PAL scene, pressure and reload | `20261014T055819Z_smoke_7d614ac79ec34cdd` | `20261014T055839Z_smoke_71c6c2f172e549bc` |
| GIF overflow rejected | `20261014T055824Z_smoke_4212b37d772d43a9` | `20261014T055843Z_smoke_2d5cbbf742ac4016` |
| DIRECT overflow rejected | `20261014T055826Z_smoke_0c7b2184722941c4` | `20261014T055845Z_smoke_00035bc2facf4d67` |
| Injected wait timeout rejected | `20261014T055828Z_smoke_feaf7751d6ab4fd1` | `20261014T055847Z_smoke_c816276f8a654fe1` |
| Existing inactive core smoke | `20261014T055405Z_smoke_57c3c11006d54576` | `20261014T055439Z_smoke_a601c85d78d343a8` |
| Production 120-frame headless loop | `20261014T055412Z_headless_ff885f4b70b9` | `20261014T055444Z_headless_29b3fa0b4cb1` |
| Production live-console/quit | `20261014T055416Z_headless_ecfe8adc3236` | `20261014T055449Z_headless_a590b3b60f94` |

Production full shutdown again recovers 7,424 requested / 7,600 backing / four
allocations, with no GS initialization. All 105 Python regressions pass, including
SDK-library order, renderer result classification and rejection of GS startup in a
headless result. The regenerated compile database has 372 entries.

Visually inspected F8 captures are archived with matching ELFs/maps/flags/config and
`scene.png` / `visual-review.json`: debug PAL
`20261014T054644Z_smoke_43fa353523a047f7`, release NTSC
`20261014T054912Z_smoke_4f618ea62c834d5e`. Both held scenes complete and recover their
ledgers. The [release NTSC capture](images/renderer-2d-1-ntsc.png) is retained here as
review evidence; it is PCSX2's scaled 4:3 framebuffer output, not native GS readback.
An earlier debug NTSC inspection is also archived as
`20261014T054215Z_smoke_977bfcc22dc94297`. Pixel code is unchanged by subsequent
heap-telemetry logging. Automatic matrix summaries still mark visual inspection as
required; a log PASS is not a pixel check.

The initial submission stalled at VIF1 DIRECT/GS setup; resetting VIF/GIF state fixed
it. The first long inspection wait then exposed the SDK's 32-bit seconds conversion
overflow; widening the conversion fixed it. Failed runs remain archived as
`20261014T053254Z_smoke_8fdf15167e174a4a`,
`20261014T053739Z_smoke_ee882c8ce9d649de`, and
`20261014T053944Z_smoke_1bbcc16af3984aff`; the first two are pipeline-timeout failures,
the third a failed inspection-hold check. Neither failure was counted as acceptance.
Durable findings and recipes are in the GS/platform/testing rules.

Next is 2D-2: RGBA textures, sampler/stride/POT handling, smooth textured alpha,
residency hazards and bounded on-target capture. Sound, native UI and world rendering
remain subsequent gates in [RENDERER_2D_PLAN.md](RENDERER_2D_PLAN.md).

## RGBA textures and native capture (2D-2)

Accepted on 2026-10-14 with GCC 15.2 and PCSX2 2.6.3 in debug/release NTSC/PAL.
The CPU device supports owned straight RGBA8, nearest/linear filtering, logical region
clamp and repeat on POT axes. Logical dimensions are bounded to 512; TEX0 extents
are POT, upload stride is at least 64 and allocations cover whole PSMCT32 pages.
Padding duplicates image edges and alpha converts to GS 128 unity. NPOT repeat,
indexed/coverage formats and native image-manager/UI providers remain pending.

The 64-record registry caps owned pixels at 2 MiB and defaults to 2 MiB texture VRAM.
Synchronous uploads fence pending PATH2 before PATH3, write back chain and pixels, then
wait for DMA and GS FINISH. Dirty uploads, eviction and destruction retire queued TEX0
references before changing storage. No second Quake identity/cache or palette is imported.

The existing integrated scene now includes authored 37×19 images with padded source
stride, nearest/bilinear transitions, POT repeat, clipped sprites, texture/vertex alpha
and tint, ordered dirty updates and eviction/rebind. A 512 KiB quadrant upload crosses
the SDK IMAGE/REF split. Small VRAM and command halves force eviction and chunk drains.
Eleven native checks require exact ownership recovery; three expected-fatal probes
still pass with assertions disabled. No new standalone fixture is introduced.

Capture streams the most recently completed context through aligned 32-row strips
into checked top-down 24-bit TGAs. Finite waits cover request, receive and PATH3/FIFO
restoration. Injected receive timeout returns failure, restores owned pipeline state,
then captures the same framebuffer with an identical hash; fresh pressure draws and
reload follow. Failed output open is rejected and every write/close is checked. The
injected deadline validates restoration, not a physically wedged GS recovery claim.

The runner requires exact native dimensions/header/length, **24 scene pixel comparisons**,
split-upload quadrants and pressure pixels. NTSC and PAL release readbacks were visually
inspected; debug image hashes match release exactly. Converted unscaled PNG evidence:
[NTSC 512×448](images/renderer-2d-2-ntsc.png), [PAL 512×512](images/renderer-2d-2-pal.png).
Each run archives raw TGAs, checks/hashes, ELF/map/flags/config and inspection metadata.
No physical PS2 acceptance or EE cache/GS timing claim follows.

| Measurement | Debug | Release |
| --- | --- | --- |
| Production direct inputs / test inputs or symbols | 351 / none | Same |
| Production `PT_LOAD` | 10,978,470 bytes | 11,396,518 bytes |
| Scene baseline/final requested / backing / count | 1,047 / 1,176 / 2 | Same |
| Owned scene pixels / textures | 81,920 bytes / 8 | Same |
| Texture VRAM peak / evictions / uploads | 65,536 bytes / 3 / 9 | Same |
| Split-upload tagged peak requested / backing | 1,051,671 / 1,052,076 bytes | Same |
| Arena peak commitment | 1,067,984 bytes | 1,069,776 bytes |
| Command-half peak, primary / small reload | 131,008 / 1,024 bytes | Same |
| Kicks / drains, primary then small reload | 134 / 2; 559 / 547 | Same |
| Persistent aligned capture/control storage | Approximately 80 KiB | Same |

The split-upload peak includes caller source and owned copy. Capture BSS is included
in production `PT_LOAD`; tagged allocations are inside the arena, not additional to it.
Production continues the headless Common/game loop without initializing GS or allocating
device pools, and full shutdown recovers 7,424 / 7,600 bytes / four allocations.

| Target gate | Debug archive | Release archive |
| --- | --- | --- |
| NTSC textures/capture/recovery/reload | `20261014T094817Z_smoke_45502ce34b9444e4` | `20261014T094919Z_smoke_5950c84c799940a3` |
| PAL textures/capture/recovery/reload | `20261014T094913Z_smoke_9e4eccbb8658404e` | `20261014T094924Z_smoke_fdc2180c992148d7` |
| GIF overflow rejected | `20261014T093929Z_smoke_0d125f42f3a74c61` | `20261014T094027Z_smoke_031b08e4705242a6` |
| DIRECT overflow rejected | `20261014T093932Z_smoke_132d1946a7654767` | `20261014T094029Z_smoke_5310f1c0a8e34f3f` |
| Injected wait timeout rejected | `20261014T093934Z_smoke_ceeb7d4175104297` | `20261014T094031Z_smoke_f0cecbc97dbc43c2` |
| Production 120-frame loop | `20261014T094113Z_headless_aaa892d1f4b1` | `20261014T094218Z_headless_7a4ef4bd4542` |
| Production live commands/quit | `20261014T094117Z_headless_6ff17bf5b752` | `20261014T094222Z_headless_9d6b65e6dd6f` |

`make test-host` passes all 115 Python regressions and the existing native host harnesses.
Source audit and both strict EE builds pass; the compile database contains 376 entries.
Backend implementation commit: `4744e2d`. Next is **2D-3**, connecting display/intrinsic-image/draw
providers to the real native console while preserving the existing headless package.

## Native console drawing adapters (2D-3 first slice)

Implementation commit `e40a003` supplies the first 2D-3 slice. The native
`idRenderSystemLocal` now has an explicitly activated 2D capability, physical display
metrics/pixel aspect, byte-accurate color packing, supported state selection, fills,
axis-aligned quad methods and small/big character/string helpers. Activation is
independent of logical worlds and the low-level GS scene; OpenGL remains unavailable.
Common production presentation and the real `console->Draw` acceptance gate remain
pending, so **2D-3 is not complete**.

The two native image declarations own typed handles through existing opaque backend
state: white and a code-authored 256×256 ASCII atlas. Their 270,336 pixel bytes are
bounded by the existing renderer budget; generation's temporary 262,144 bytes are freed
before frames. Manager lookup admits only those names and sampler/usage profiles.
Charset glyphs occupy native 16×16 cells, with cursor bytes 10/11 and explicit box
fallbacks. No font dependency, Quake/retail artwork or native GUI vertex pool is added.

Geometry maps virtual 640×480 to GS 1/16 fixed point, retaining fractional positions
and interpolating clipped UVs. Portable input-bit checks reject nonfinite/subnormal
values before EE arithmetic. Text length/advance is checked before glyph emission.
`GLS_OVERRIDE|GLS_DEFAULT` performs replacement, including zero-alpha RGB writes;
ordinary intrinsic helpers use the declared straight-alpha profile. Arbitrary quads
require one constant ambient image stage; unsupported transforms, shaders, masks,
blends and geometry fail explicitly. Resident parsed-material equivalence and real
console drawing are subsequent verification, not claims made by the core scene.

The existing renderer scenario now calls native draw helpers for 120 frames, then four
after warm reload, and verifies startup rollback and exact cleanup. It displays authored
ASCII/cursor/color/input-line content; no real console state or input delivery is implied.
**26 native adapter pixel checks** cover glyph strokes/holes/transparency, cursors, color
escapes, replacement/alpha, clipping and fractional boundaries. Primary/reload hashes
match, as do debug/release images. Inspected unscaled release captures:
[NTSC](images/renderer-2d-3-adapters-ntsc.png), [PAL](images/renderer-2d-3-adapters-pal.png).

Both complete debug/release renderer matrices pass: twelve native checks, existing
24-pixel texture scene/upload/pressure checks, and seven expected-fatal probes.
New active UI probes reject unsupported state, negative rectangle width, skewed quads
and oversized strings before primitive emission. All **122 Python regressions** and
host geometry/charset ASan/UBSan checks pass. Source audit and strict EE builds pass;
the compile database has 380 entries.

Production retains 355 direct inputs with no test inputs/symbols or GC. `PT_LOAD` is
10,992,678 debug / 11,412,390 release bytes. Both headless modes still allocate no
device pools and recover 7,424 requested / 7,600 backing bytes / four allocations.
Isolated scene baseline/final remains 1,047 / 1,176 bytes / two allocations; tagged
peak is 1,051,671 / 1,052,076 bytes and arena peak 1,070,288 debug / 1,070,544 release.
Pressure chain peaks remain 131,008 / 1,024 bytes, with 135/3 primary kicks/drains
and 558/547 on the small-half reload. No physical PS2/cache/throughput claim is made.

| Target gate | Debug archive | Release archive |
| --- | --- | --- |
| NTSC native adapters/pixels/reload | `20261014T124628Z_smoke_fd61fd7f909f4ab1` | `20261014T124417Z_smoke_8a4f3f50454a432e` |
| PAL native adapters/pixels/reload | `20261014T124636Z_smoke_72e66181f9314bb0` | `20261014T124425Z_smoke_8f43d40415d241c4` |
| Unsupported UI state | `20261014T124316Z_smoke_c381efa6e75b45f4` | `20261014T124440Z_smoke_1be9fff239a340ce` |
| Invalid UI bounds | `20261014T124318Z_smoke_9ecf66b67ac94991` | `20261014T124442Z_smoke_8fc1871fbc03460b` |
| Unsupported skewed quad | `20261014T124320Z_smoke_21d42adadbbb40a7` | `20261014T124445Z_smoke_ed5f079df23f4772` |
| Oversized text | `20261014T124322Z_smoke_58e5f4ebc7ae4610` | `20261014T124447Z_smoke_b88ebe76df014378` |
| Production 120 frames | `20261014T124905Z_headless_89095fd79511` | `20261014T125054Z_headless_995bf3056f07` |
| Production live commands/quit | `20261014T124910Z_headless_0874c8ab138e` | `20261014T125059Z_headless_3440c62193b1` |

Next select visible versus headless policy before allocating display resources, call
the real native console from Common's frame lifecycle, and make `UpdateScreen` safe
without nesting frames or ticking the game twice. Real edit-field input/history and
capture-before-quit verification complete the remaining 2D-3 gate.

## Production native visible console (2D-3)

**2D-3 is accepted in PCSX2 on 2026-10-15 in debug and release.** The implementation
connects real `console->Draw(false)` to the production
Common frame loop. One production `d3bfg.elf` supports headless, NTSC 512×448 and PAL
512×512. Package-root `presentation.cfg` selects the policy before Common initializes;
a missing file means headless, and malformed/oversized content is fatal. `make console`
packages visible NTSC by default; `CONSOLE_STANDARD=pal` selects PAL. `make run-console`
launches it, and `make test-console` exercises both standards. Retail data and test
fixtures are not involved.

PCSX2 HostFs reports an absent policy as `-5`/`EIO`; the optional file therefore
warns and defaults to headless on either `ENOENT` or `EIO`. Missing policy cannot be
distinguished from another `EIO` here. Other open errors, read/close failures and
invalid present content remain fatal; required assets do not use this fallback.

Common processes console input and commands, advances its clock/logical stamp and
native `NOMAP` frame, then draws and immediately presents the real console once.
`UpdateScreen` calls made by native Enter/refresh coalesce into that final draw;
outside a Common frame they redraw without advancing clocks or game/logical ticks.
Visible frames use GS vsync without adding the headless 60 Hz sleep. Closing/reopening
the console tolerates the absence of a production mouse device. The native game still
stays in `NOMAP`; this does not load a campaign map or native BFG shell.

HostFs `console.txt` retains ordinary native command submission. Visible-only
`ps2_consoleText` and `ps2_consoleKey` additionally deliver validated native character
and key events to the actual edit field. Native Enter adds to RAM history; persistent
history writes remain unavailable, and `ps2_` host controls do not displace edit-field
history. `ps2_capture` queues a bounded HostFs TGA basename for the next completed
frame. A capture and quit arriving together still finish that frame and readback
before native UI images/device teardown. Full shutdown must recover the startup ledger.

The strict production-only `presentation.cpp` is cold-optimized. Tagged edits to native
Console/Common retain source boundaries, and test variants keep their unavailable
capability failures. Both production configurations link 356 direct objects with no
test inputs/symbols or linker GC. `PT_LOAD` is 10,997,606 debug / 11,417,190 release
bytes; the regenerated compile database has 381 entries.

The automated production runner waits at least 120 frames, types/submits an echo,
replays native up-arrow history, closes/reopens the console, edits an incomplete
`echo pending`, captures it, then requests history and native edit-field quit with a
final capture. It requires presentation count equal to completed Common frames,
redraw requests without extra ticks, exact startup-ledger recovery and two ordered
captures. Nineteen edit-capture pixel checks cover the translucent half-console,
cyan prompt/border, glyph strokes/holes and uncovered background. Final capture must
match the completed quit frame. Archives preserve matched ELF/map/reports, startup
policy/data/input hashes, flags, emulator config/logs and native TGAs.

Both configurations pass in PCSX2 2.6.3. Debug NTSC/PAL complete 199/187 Common frames
and presentations; release completes 199/177. Each run has three redraw requests,
coalesced into normal frames rather than additional simulation ticks or presentations.
This equality describes the tested in-frame path; an out-of-frame `UpdateScreen` is
allowed to redraw without a tick. Both captures close before teardown; the second
capture is from the final completed quit frame.

| Measured production visible loop | Debug | Release |
| --- | ---: | ---: |
| Tagged baseline/final requested / backing / allocations | 7,424 / 7,600 / 4 | Same |
| Peak requested / backing bytes | 2,639,911 / 2,737,664 | Same |
| Peak arena committed bytes | 2,752,666 | 2,750,874 |

These totals are separate from fixed ELF residency; do not add arena commitment to
its contained tagged backing a second time. Intrinsic image ownership and command
buffers recover before the final foundation shutdown.

The unscaled release captures were visually inspected: [NTSC 512×448](images/console-2d-3-ntsc.png)
and [PAL 512×512](images/console-2d-3-pal.png). They show native scrollback, the edited
`echo pending` input line, cyan prompt/border and version text over a translucent
half-console; the cursor follows the native blink phase. Original edit TGA SHA-256 is
`ee98ffbc082266dd6437cbfe0f49c7b68e1016fe36c52bddf47e45cccab74b8f` for NTSC and
`c90e3668d393c635b0af97dd84ba2a533d52e3bcebc8dbce4a35dbeddb817101` for PAL.
These are PCSX2 functional/readability observations, not physical-PS2 cache or GS-cost
acceptance.

All 135 Python host regressions pass, including thirteen new production-console
staging/classifier/capture checks, alongside the existing ASan/UBSan C++ harnesses.
Both configurations also pass the existing headless bounded/live-command checks, with
no GS startup/device pools and exact startup-ledger recovery.

| Production gate | Debug archive | Release archive |
| --- | --- | --- |
| Native visible NTSC | `20261015T000647Z_console_89637cdc87ec` | `20261015T000700Z_console_9049100ca398` |
| Native visible PAL | `20261015T000653Z_console_58d2cafed9a1` | `20261015T000705Z_console_d97eb136d785` |
| Headless 120 frames | `20261015T000711Z_headless_089b171561c0` | `20261015T000721Z_headless_79d4efab52e6` |
| Headless live commands/quit | `20261015T000716Z_headless_cf66dd06be66` | `20261015T000725Z_headless_9a9fc0827ea8` |

Four additional one-off release production-package probes pass. Missing policy
warns, completes three headless frames, initializes no GS device and fully shuts down.
The other probes require the intended first fatal diagnostic, with no unrelated
TLB/bus/assert failure. These are fresh isolated packages with matched production
images/reports and archived summaries/logs; they add no test fixture and are separate
from the positive `make test-console` runner.

| Release policy/input probe | Archive |
| --- | --- |
| Missing policy → headless | `20261015T000625Z_console-policy_missing-policy_cc59a8cc` |
| Invalid startup policy | `20261015T000628Z_console-policy_invalid-policy_94550d18` |
| Unknown named console key | `20261015T000630Z_console-policy_invalid-key_594400f2` |
| Capture basename with path traversal | `20261015T000632Z_console-policy_invalid-capture_473fade5` |

Indexed GUI/triangle storage, general image/material loading, native SWF/font execution
and the offline BFG main menu remain the following gates (2D-4 through 2D-7). Physical
pad/keyboard navigation and a physical-PS2 video/cache check remain 2D-8.

## Indexed native UI geometry (2D-4 first slice)

**This indexed-geometry slice is accepted in PCSX2 in debug and release on
2026-10-15.** All twenty-eight renderer sessions and eight production regressions pass.

The first 2D-4 slice adds bounded native GUI allocation and ordered CPU triangles.
It preserves the production console/headless split and extends the existing core GS
fixture; no new test fixture, test-enable define or retail asset is introduced. Native
SWF/font execution, the interactive authored SWF and general material/image loading
remain later work, so **2D-4 as a whole is not complete**.

`ui::Init` now allocates 4,096 native `idDrawVert` entries, 6,144 indices and 128 ordered
draw runs with tagged ownership. `AllocTris` copies indices and returns a write-only
CPU span; fill it before the next allocation or draw that can flush. Oversized requests
and indices outside the local vertex span fail before mutation. Pool pressure flushes
completed runs and recycles storage. Sprite interleaving and `EndFrame` flush queued
triangles in submission order, including a final draw with no following sprite/frame.
Texture `Update`/`Destroy` also flush queued UI references before changing or releasing
their pixel/VRAM ownership, so earlier triangles cannot observe a later mutation.
All spans expire at a flush or frame boundary. Native vertex-cache handles, static
buffers and joint buffers remain unavailable.

Native rotated/skewed quads and `DrawStretchTri` use the indexed path. The CPU mapper
clips either winding against the 640×480 canvas, interpolates UV/RGBA before fixed
rounding and rejects unsupported float representations before EE arithmetic. Native
half UVs are decoded from bits, with exponent 31 rejected explicitly. The initial
profile accepts zero depth and exact `color2={128,128,128,128}`; additive transforms
remain unavailable. Native SWF's `PackColor(0.5)` truncates to 127, which its future
zero-additive adapter must reconcile explicitly.

Every GS triangle reserves thirty qwords, declares its sampler/texture/clamp/blend/test
and Gouraud triangle state, then writes all three RGBQ/UV/XYZ2 vertices. RGB and alpha
map native 255 to GS unity 128. Degenerate geometry is skipped, and straight-alpha
triangles with three zero-alpha vertices can skip; replacement still writes RGB at
zero alpha. These packets fit the existing 1 KiB pressure halves and do not rely on
primitive state surviving a drain.

The native material entry points retain the existing one-constant-ambient-stage
profile. The core indexed scene uses the intrinsic-image API because core does not
link `DeclManager`/material implementations; this does not establish resident material
or native SWF acceptance. Its authored image contains a transformed white `A`, a
partially clipped cyan `A`, a three-color triangle, a yellow diamond, an alpha gradient
and sprite/triangle overlap. Native TGA checks distinguish geometry, glyph holes,
clipped UVs, interpolated colors/alpha and submission order. All three CPU pool limits
and command-half drains are exercised on the final frame, with exact warm/final ledger
checks and identical primary/reload captures required.

Both production configurations compile and link 357 direct inputs, with no test
inputs/symbols and no linker GC. Debug/release `PT_LOAD` is 11,009,702 / 11,434,406
bytes; the compile database has 382 entries. All 140 Python regressions and the host
ASan/UBSan harnesses pass, including the new triangle mapper's 20,000 generated cases.
The target matrix adds five release-safe failures—capacity, local indices, depth,
half UV and additive color—to the existing seven. Each renderer configuration runs
two positive standards and twelve isolated expected-fatal sessions.

All four debug/release NTSC/PAL positive renderer runs pass thirteen GS markers and
all twenty-seven indexed, twenty-six existing UI and twenty-four base scene pixel
comparisons, plus upload/pressure and recovery checks. Primary/reload captures have
identical hashes. Both cycles reach every CPU pool limit; the 1 KiB command halves
drain while retaining the image. Every warm frame preserves its tagged ledger, and
the integrated scene returns to its exact 1,047 requested / 1,176 backing bytes / two
allocation baseline.

| Indexed cycle | Frames | Vertex/index/run peaks | Flushes | Emitted triangles | Command drains |
| --- | ---: | --- | ---: | ---: | ---: |
| Primary | 120 | 4,096 / 6,144 / 128 | 366 | 4,647 | 6,927 |
| Reload | 4 | 4,096 / 6,144 / 128 | 18 | 3,255 | 3,331 |

| Positive renderer gate | Debug archive | Release archive |
| --- | --- | --- |
| NTSC | `20261015T045024Z_smoke_190499effc06446d` | `20261015T045208Z_smoke_3570041d53194caf` |
| PAL | `20261015T045034Z_smoke_103df0044cb8476e` | `20261015T045218Z_smoke_daa040892c0e4300` |

The release [NTSC](images/renderer-2d-4-ntsc.png) and
[PAL](images/renderer-2d-4-pal.png) captures were inspected at their original
512×448 / 512×512 dimensions. The transformed and clipped glyphs, interpolated RGB,
yellow diamond, alpha gradient, layered rectangles and final corner triangle are
visible without missing triangles or visible seams. A separate 3×3 readback around
the translucent quad's shared diagonal in both debug standards is uniformly
`(66,66,74)`, within the eight-level tolerance of `(68,70,74)`: no gap or doubled alpha
is present at that sample. Debug and release native indexed TGA hashes match per mode:

- NTSC: `1c0e7a04eb12b303062b988125bf982303ab5af7f1987db795519ed61b51ccde`.
- PAL: `c3bbb0254ccc0d04803d79f2f70762bfa9ff2d623a6f3f14a5d94176a67ac455`.

All twelve expected-fatal renderer cases pass in each configuration. Each run requires
its matching first fatal diagnostic; watchdogs, crashes and unrelated failures cannot
satisfy the gate. The final five cases cover the new indexed boundaries.

| Expected-fatal renderer case | Debug archive | Release archive |
| --- | --- | --- |
| GIF capacity | `20261015T045045Z_smoke_c7a0c946d4664cec` | `20261015T045229Z_smoke_79c3917506a4475d` |
| DIRECT capacity | `20261015T045047Z_smoke_f7ae4ec05c4e48c2` | `20261015T045231Z_smoke_d1508e5b8dc04321` |
| Wait deadline | `20261015T045049Z_smoke_b7b4ae1cb87a4a36` | `20261015T045233Z_smoke_995902ac3e6f4263` |
| Unsupported UI state | `20261015T045052Z_smoke_6e3fa194f1b94bf5` | `20261015T045236Z_smoke_9f68b9f5660e4933` |
| UI rectangle bounds | `20261015T045054Z_smoke_47fc79ff54244a2a` | `20261015T045238Z_smoke_9f3afbb446764ea6` |
| Invalid quad coordinate | `20261015T045056Z_smoke_483d1e8c2683462c` | `20261015T045240Z_smoke_129d702006394307` |
| Oversized text | `20261015T045058Z_smoke_82787c22eba04bb0` | `20261015T045242Z_smoke_9125e93c2b8e4f01` |
| Invalid local index | `20261015T045100Z_smoke_9afddbee26d24921` | `20261015T045244Z_smoke_b9c147185edc458a` |
| Oversized indexed request | `20261015T045102Z_smoke_f3917e804bf94623` | `20261015T045246Z_smoke_8fdf9d51d381493b` |
| Nonzero UI depth | `20261015T045104Z_smoke_ad408578076d450b` | `20261015T045248Z_smoke_2a41d160bca346db` |
| Nonfinite half UV | `20261015T045106Z_smoke_7f433fb329984eb5` | `20261015T045250Z_smoke_27f32668bcf145fc` |
| Additive vertex color | `20261015T045108Z_smoke_9330468fef184ccb` | `20261015T045252Z_smoke_7c3a9dfb9c9e4e44` |

The production console matrix still passes native edit/history, close/reopen and
capture-before-quit in NTSC/PAL. Both existing headless cases pass without GS startup
or UI pool allocation. Every production run recovers its exact 7,424 requested /
7,600 backing bytes / four allocation startup ledger.

| Production regression | Debug archive | Release archive |
| --- | --- | --- |
| Visible NTSC console | `20261015T045254Z_console_f2a3fc19dd2e` | `20261015T045306Z_console_9f9ac49f0ab9` |
| Visible PAL console | `20261015T045300Z_console_056a029ba7f6` | `20261015T045312Z_console_4cb27071d167` |
| Headless 120 frames | `20261015T045318Z_headless_9996905f06d0` | `20261015T045327Z_headless_7e562c48e31b` |
| Headless live commands/quit | `20261015T045322Z_headless_52c98bc449a5` | `20261015T045331Z_headless_9dd4d0d7c5d9` |

Visible production peak requested/backing memory is 2,786,855 / 2,884,740 bytes in both
configurations. Arena high-water is 2,900,314 debug / 2,897,498 release. Headless bounded
peak requested/backing memory remains 1,845,504 / 1,943,072 bytes; live-command peaks
are 1,845,528 / 1,943,120. These are measured authored console packages, not retail UI
budgets. No physical PS2 cache-coherency or GS-cost claim follows from PCSX2 pixels.

The next 2D-4 work is a scoped material/image/font path and one mask-free native SWF
with text, animation and focus/select input. Retail dependency inventory/cooking (2D-5)
can proceed alongside it; the native BFG main menu remains 2D-7.

## Authored native SWF and fonts (2D-4)

Accepted on **2026-10-15, Australia/Sydney**, with ps2dev GCC 15.2 and PCSX2 2.6.3.
The production ELF now loads one authored mask-free native BSWF, executes native
timeline/AVM1 actions, draws native `idFont` text and routes Tab/Enter through native
SWF events. This completes the authored 2D-4 interface gate. It does not load the
retail BFG shell, enable physical input, render the world or play sound.

`make ui` / `run-ui` stage and launch the production ELF with optional authored data;
new package configs load the scene automatically. `ps2_ui` controls loading, unloading,
reloading, status and pause/resume; `ps2_uiKey tab|enter` queues engine key events.
The real console retains input priority and can open over the movie. Movie/font/UI/device
shutdown follows ownership order, and headless startup creates none of these resources.

The reproducible [authored package](../src/tests/data/ui/README.md) contains a 2,364-byte BSWF,
1,342-byte font and 680-byte material file. Native unchecked BSWF/font loaders see an owned
snapshot only after its length and full contents match the generated authored bytes.
The 4,386 validation payload bytes are compiled into the backend; this narrow admission
path is not a general BSWF/font validator. Source-SWF expansion/cache writes, retail
cursor/controller preloads and font fallback probing stay unavailable.

Native font metrics use the EE's padded ten-byte glyph records. A separately generated
512×128 RGBA atlas adds 262,144 owned pixel bytes only when the font is requested;
gutters contain native half-texel UV expansion. Glyph/index arrays are explicitly freed.
The SWF draw scope accepts native normal blending and neutral `color2` bytes 127/128,
restores prior color/state afterward, and retains all other material/state/additive
rejections. Timeline time comes from the production owner so redraws preserve it.

| Native UI acceptance | Debug archive | Release archive |
| --- | --- | --- |
| NTSC, three loads | `20261015T072206Z_ui_e4a5793b3c00` | `20261015T072252Z_ui_9788e15effb5` |
| PAL, three loads | `20261015T072218Z_ui_21b4a8960e9f` | `20261015T072304Z_ui_538cf80e2101` |

Each load draws at least 120 normal frames. Two paused captures have different native
pulse frames, including fractional alpha. Native Tab/Enter changes focus from row zero
to one, runs the authored selection function and displays `SELECTED 1`. Opening the
console, typing/submitting `echo ui_console`, presenting it and closing it succeeds
before movie input resumes. Each run records three coalesced redraw requests and exactly
one presentation per normal frame: 559/595 debug NTSC/PAL and 571/601 release NTSC/PAL.
Capture queued with quit completes before shutdown.

All **24 UI captures pass 28 independent pixel checks each** (672 checks), covering
indexed shapes, transformed bitmap strokes/holes, native font strokes/holes, focus,
selection text and animated color/alpha. The unscaled third-cycle selected captures
were inspected in [NTSC](images/swf-2d-4-ntsc.png) and [PAL](images/swf-2d-4-pal.png).
Two original probes lay just outside PAL glyph strokes; moving them to stroke-row
centers made the same probes valid in both standards without changing tolerance.

| Expected asset rejection | Debug archive | Release archive |
| --- | --- | --- |
| Missing BSWF | `20261015T072233Z_ui_eb22d75de4a8` | `20261015T072318Z_ui_136765abc09a` |
| Truncated BSWF | `20261015T072235Z_ui_f6dab22d4c22` | `20261015T072320Z_ui_7942d00ed063` |
| Oversized BSWF | `20261015T072237Z_ui_b434a32b7dd2` | `20261015T072323Z_ui_a1514ec3024f` |
| Mutated BSWF | `20261015T072240Z_ui_8678ea98b5dd` | `20261015T072325Z_ui_ff1fdf6faded` |
| Missing font | `20261015T072242Z_ui_08b58fdb6140` | `20261015T072327Z_ui_d7c42def298c` |
| Mutated font | `20261015T072244Z_ui_40705a5b83d2` | `20261015T072330Z_ui_2dece6af9258` |

These twelve fresh processes stop at the expected asset diagnostic before a loaded
movie is published. Stale identities, skipped waits/actions, crashes, wrong diagnostics,
missing captures and watchdog expiry fail acceptance. HostFs logs submitted commands
before waits execute, so the driver uses native Echo output as phase barriers.

All warm unloads in all four positive runs return to **2,792,635 requested / 2,892,808
backing bytes / 2,446 allocations** after clearing native command history. The font and
material caches intentionally remain warm until UI shutdown. Every full shutdown
returns to **7,424 / 7,600 bytes / four allocations**. UI peak requested/backing is
3,065,630 / 3,167,572 bytes; arena high-water is 3,424,282 debug / 3,424,410 release.
Tagged backing is contained inside the arena, not added to it. These authored-package
measurements do not estimate retail shell/font residency or physical PS2 performance.

| Existing production/device regression | Debug archive | Release archive |
| --- | --- | --- |
| NTSC console | `20261015T072024Z_console_5f6b6c71da40` | `20261015T072332Z_console_b3e3ec21c77d` |
| PAL console | `20261015T072030Z_console_60f49af75e72` | `20261015T072338Z_console_2a34dd847c15` |
| Headless 120 frames | `20261015T072036Z_headless_dddf1a32b081` | `20261015T072343Z_headless_0ebf63a328a9` |
| Headless live input/quit | `20261015T072040Z_headless_e2f22264c897` | `20261015T072348Z_headless_78b6e5770a92` |
| NTSC integrated renderer | `20261015T072046Z_smoke_34f74eddfcff4a46` | `20261015T072354Z_smoke_2bb13a59701d47df` |
| PAL integrated renderer | `20261015T072057Z_smoke_a4a874b5aba94aa5` | `20261015T072404Z_smoke_277433cc81904be5` |

All twelve existing renderer bounds/state/deadline cases also pass in each configuration,
from debug `20261015T072108Z_smoke_eae667d4fe114557` through
`20261015T072131Z_smoke_fee23b3c59f4412f`, and release
`20261015T072415Z_smoke_2c3621cc3f2c4ab7` through
`20261015T072438Z_smoke_389186d16ebe476f`. No new native fixture executable was added.

Both production/core builds pass strict backend compilation. Production retains 360
direct inputs, zero test inputs/symbols and no linker GC; `PT_LOAD` is 11,018,214 debug /
11,435,878 release bytes. The compile database has 385 entries. All 160 Python host
regressions and the existing ASan/UBSan C++ harnesses pass, as does authored-data regeneration.

Next is **2D-5 retail UI dependency/feature inventory and cooking**: identify the bounded
shell/dialog/font/localization package and its actual mask/blend/atlas requirements.
Required feature closure is 2D-6, native offline menu integration is 2D-7, and physical
input/PS2 acceptance is 2D-8. Retail assets and converted output remain local and ignored.

## Standalone host resource extraction (2D-5 foundation)

On **2026-10-16, Australia/Sydney**, the standalone C++20 `resource_unpack` now expands
native BFG `.resources` containers into ordinary directory trees. `make resource-unpack`
builds `build/tools/resource_unpack`; the source also builds directly without ps2dev or
engine initialization. The format comes from native `File_Resource.{h,cpp}` and
`idFile::ReadString`: resource payloads are stored directly, so unpacking preserves
their bytes rather than decoding individual BSWF/BIMAGE files.

The tool preflights every input table and destination, checks mixed-endian fields and
file bounds, rejects traversal/symlinks/source aliases, and streams files through a
64 KiB buffer with checked reads/writes/close. Existing files require explicit
`--overwrite`. Later input containers and duplicate records win after path normalization.
Two retail sound entries contain `generated//sound/`; internal repeated separators
collapse to the same filesystem output path while leading/trailing separators remain
invalid. Completed files publish individually; a later I/O error is not a rollback of
the entire output tree. Use a destination tree without concurrent external mutation.

All **64 local retail containers** pass table/list validation. The sanitizer build
also lists their combined **23,500 normalized merged entries** successfully. Extracting
`_common.resources` followed by `_ordered.resources` produces **2,316 files / 80,053,423
payload bytes**; every extracted file was compared byte-for-byte with the independent
Python table reader. Output and `build/resource-unpack-validation.json` remain ignored.
Fourteen synthetic ASan/UBSan host tests cover malformed/truncated tables, unsafe paths,
overlap/duplicate semantics, streaming, symlinks, overwrite and no-write preflight failures.
No proprietary files were added to the repository, and no EE runtime capability changed.

Usage and limits are in [the unpacker guide](../src/tools/resource_unpack/README.md).
The UI dependency inventory and lossless host page cooker build on the same native
resource layout; target retail loading and rendering remain separate later gates.

## Retail UI host inventory and cooking (2D-5)

On **2026-10-16, Australia/Sydney**, `make ui-inventory` / `make ui-cook` now provide
the host foundation for a scoped native offline PressStart/Root/Quit-dialog package.
The checked-in `config/ui_retail.json` profile selects shell/dialog, English labels,
fonts, cursor/cover images and native source/path contracts. Independent checked
readers inspect BSWF16, modern/legacy font data, language, materials and BIMAGE v10;
the cooker decodes DXT1/DXT5 and native green-alpha without resampling.
Usage, ownership and limits are in [RETAIL_UI_PIPELINE.md](RETAIL_UI_PIPELINE.md).

The local `_common` / `_ordered` inventory resolves **2 SWFs, 5 font families,
12 images and 31/31 required localization IDs**, with **128 material files** and
**160 hashed inputs**. Five native menu/dialog source files are hashed separately.
Twelve of thirteen configured shell paths appear in static timeline expansion;
`menuStart/backing` is absent and is optional in native PressStart code.
The shell reports 54 mask placements, 29 additive-color placements, blend modes
0/4/8 and fifteen strokes; dialog adds ten masks and blend modes 0/8. Static script
inventory is conservative and does not establish dynamic resource/callback closure.

The host cook preserves original shell/dialog bytes, five complete modern fonts,
twelve legacy metric files and full English localization. Decoded artwork becomes
**143 logical 256×256 RGBA pages** with one-pixel gutters, source rectangles and
full-atlas-to-page UV transforms. Internal gutters sample neighboring original atlas
texels. The immutable package contains **145 unique asset files / 34,336,948 bytes**,
excluding the **10,173,242-byte manifest**. Repeated cooking is byte-identical:
manifest SHA-256 `6fd558c9b8ee7a05b7904b30ea49eef09cf039cdafafb44a2fb6762bf9b17b4a`.
Output remains local under ignored `build/`; no proprietary file is committed.

`make test-host` passes **264 Python regressions** and its ASan/UBSan C++ harnesses,
including fourteen unpacker and ninety new UI format/cook regressions. Tests cover
truncation/ranges, duplicate priority, aliases, native font behavior, localization,
DXT/swizzles, lossless pages/seams/UVs, immutable asset hashes and preservation of
prior output after failures. Required missing strings/glyphs refuse cooking before
staging; source/config publication aliases are rejected. All referenced cooked source
and page files pass size/hash validation on the real local package. All twelve decoded
images reconstruct byte-for-byte from page interiors, with every source pixel covered
exactly once. The decoded shell atlas and font alpha plane were visually inspected;
ignored evidence is in `build/ui-retail-validation.json`.

This is **partial 2D-5**, with `runtime_ready=false`. Native page binding and triangle
splitting, general bounded retail admission, masks/special blends and offline native
Shell startup remain open. The proposed eight-page / 2 MiB CPU pixel cache is not a
measured simultaneous working set; the twelve decoded base images total 26,220,672
bytes on the host. No EE/backend source or source list changed: production remains
360 retained objects and the compile database 385 entries. Existing authored native
UI admission and headless behavior are unchanged; target/emulator acceptance is not
claimed for this host-only step. `make`, `make release` and `make inventory` pass;
no emulator run is needed for these host-only changes.

## Streamed page images and triangle splits (2D-5 slice)

On **2026-10-16, Australia/Sydney**, the authored resource generator now emits the
approved C++ layout directly. Its exact-byte `--check` passes without requiring a
formatter, and authored SWF/font/material bytes remain unchanged. Target compilation
also exposed the preceding format pass's include-order regression in the engine
type-query test; `precompiled.h` now stays in a separate first include block.

The English-only retail cook now emits twelve compact **UIPG v1** image files in
addition to its source assets and inspectable RGBA pages. The runtime reads a 32-byte
little-endian header and eight-byte offset/checksum records rather than the large
JSON report. Every package has fixed 256×256 RGBA pages, one-pixel gutters, a bounded
inferred grid, exact length/offset validation and per-page FNV1a32 checks. Host SHA-256
and publication validation remain in place. The package has **157 unique asset files /
71,825,068 bytes**, plus a **10,180,729-byte manifest** with SHA-256
`c117443f418a0f8fdb264c755250cec891581e77f5a05f3fa04aafac1f7d7218`.
This includes duplicate disk storage for raw pages and indexed runtime files; it is
not simultaneous EE residency. Initial localization/fonts support English only.

The new strict `ui_page_format.*`, `ui_page_geometry.*` and `ui_page_image.*` backend
units validate metadata, stream one input page, split full-atlas triangles at page
and screen boundaries, and reuse a fixed set of texture slots through existing
Update/Bind/upload fences. Draw order, interpolation and queued texture ownership
survive CPU-cache and VRAM eviction. Invalid page data never replaces a valid slot.
Callers close images before shutting down their device; filtering is linear/clamp.
No native retail SWF/font provider is enabled by this slice.

Clipping reconstructs intersections from the original triangle and canonical boundary
constraints. This fixes a reproduced one-sixteenth-pixel adjacent-page disagreement
caused by chained interpolation. Constant components and exactly equivalent planes
preserve valid UV line/point mappings, including page corners. Arithmetic remains
single precision; the EE object imports no software-double helpers. Host sanitizer
checks cover malformed metadata, shared seams, reversed winding, color/alpha,
nonportable values, untouched rejected outputs and randomized rank-one mappings.

The existing renderer fixture now cooks an authored **600×320, six-page atlas**,
draws clipped/rotated triangles, zoomed bilinear seams and overlapping alpha layers,
and reloads it. It reserves **524,288 texture bytes + 262,144 input bytes + 48 table
bytes**, or **786,480 requested RENDER bytes**, with fixed object/file/allocator and
device overhead accounted separately. Two CPU slots compete for one VRAM page.
There are no draw-time allocations and warm ledgers remain exact over 120/four frames.
Periodic six-page sweeps test eviction; intervening frames keep a two-page working
set hot. The first deliberate all-pages-every-frame experiment exceeded its watchdog
under HostFs/hash thrashing and was not accepted as a rendering result.

The primary cycle records **170 loads, 1,870 hits, 168 evictions and 2,185 triangles**;
reload records **68 / 44 / 66 / 170**. Bad header/table/length/pixel files, invalid
paths/slot counts and insufficient cache budgets are rejected with exact cleanup.
Each native capture checks **45 pixels**, including **22 filtering/seam probes**, at
the existing tolerance of eight. Primary/reload hashes match per standard; inspected
unscaled captures are [NTSC](images/renderer-paged-2d-5-ntsc.png) and
[PAL](images/renderer-paged-2d-5-pal.png). Renderer shutdown returns exactly to its
**1,047 requested / 1,176 backing bytes / 2 allocations** baseline.

The production link now retains **363 objects**, zero test inputs/symbols and no GC;
the compile database contains **388 entries**. Production fixed `PT_LOAD` residency is
**11,034,470 debug / 11,455,974 release bytes**. `runtime_ready=false` remains correct:
native SWF/font page binding, English menu working-set measurements, masks/blends and
offline Shell integration remain subsequent gates. The authored cache exercise is
not a retail residency/performance or physical PS2 acceptance claim.

Debug/release production and core builds, **283 Python host regressions**, existing
ASan/UBSan harnesses and both new native format/geometry harnesses pass. Geometry
coverage includes 40,000 randomized cases and 1,200 rank-one profiles. Both complete
renderer matrices pass their two positive standards and twelve expected-fatal probes;
assertions are disabled in release. Production `test-headless` also passes bounded
frames and live command/quit runs in both configurations with exact shutdown recovery.
Authored generation and the compiler database are current, and no retail file is tracked.

| Configuration | NTSC acceptance archive | PAL acceptance archive |
| --- | --- | --- |
| Debug | `20261016T123532Z_smoke_c85948736c03477b` | `20261016T123556Z_smoke_ef0d53ec863c412e` |
| Release | `20261016T123712Z_smoke_7545305176dd4af1` | `20261016T123735Z_smoke_11870183015241c1` |

Both configurations have the same paged capture hashes: NTSC
`827db3465baa2d5c46481bec5ff4d55c6a3bf2152caf67de951c85c3739ddddb`, PAL
`f774a78722736c4cbdda72595bd98511d76606ec52830a558cf1606d1ab3f928`.
Each archive retains matching ELF/symbol/map/report, flags, authored package variants,
capture checks and logs. The subsequent slice below binds UIPG pages into authored
native SWF/font drawing. Retail English working-set measurement still precedes masks,
special blends and offline Shell integration.

## Native SWF and font page binding (2D-5 slice)

Accepted **2026-10-17, Australia/Sydney**, in production debug/release NTSC/PAL.
Native `idImage` now carries either direct intrinsic pixels or a checked `PageImage`.
Logical dimensions remain full-atlas dimensions for native SWF and font UV generation;
the existing draw adapters split paged indexed runs and glyph quads while preserving
submission order. The authored movie's **272×256** atlas places its 16×16 bitmap at
(246,64), across the x=254 page boundary. The **512×128** English ASCII font retains
its metrics, glyph coverage and half-texel UV expansion. No retail data or additional
target fixture is introduced.

Both images borrow one `PageCache`, keyed by image identity and page. Four nearest/
clamp texture slots reserve **1,048,576 pixel bytes**, one input page reserves
**262,144 bytes**, and seven records reserve **56 bytes**: **1,310,776 RENDER payload
bytes**, excluding objects, files, intrinsics and allocator overhead. Compiled authored
descriptors admit the exact package dimensions/counts/table checksums; each full page
is verified before updating a cache slot. Drawing allocates no storage. Image closure
flushes/fences queued references and invalidates its keys; cache shutdown rejects live
images in release too. Standalone renderer callers retain their private-cache API.

`make test-ui` and its release variant pass both video standards, **three loads of at
least 120 movie frames**, native timeline color/alpha, Tab/Enter AVM1 focus/select,
console overlay/input, final capture/quit and **30 pixel probes per movie capture**.
The new probes include the bitmap crossbar's seam center and either side. Captures
were visually inspected: continuous bitmap strokes, readable text, correct focus and
fractional alpha, with no visible gaps or tearing. Debug/release captures match within
each standard. See [NTSC](images/ui-paged-native-2d-5-ntsc.png) and
[PAL](images/ui-paged-native-2d-5-pal.png).

| Configuration | NTSC archive | PAL archive |
| --- | --- | --- |
| Debug | `20261017T001212Z_ui_4197b9778645` | `20261017T001232Z_ui_4d40d3228e81` |
| Release | `20261017T001349Z_ui_3e2328265446` | `20261017T001409Z_ui_c9c172245bd9` |

The selected `ui-c1-b.tga` hashes are NTSC
`d5cb35e1007c28262a2edbcace4be6c5e0085a0eab3cbad569c707b24f2acf0b`, PAL
`fd32e0ae3b7c81b6002695f5c369f4d477cb652a922869a366bcd3538e6f4297`.
Every captured cache state has **two images, four loads and zero evictions**;
hits/triangles increase across all reloads. Movie unload keeps image/font caches warm.
All four runs return to the same warm unload ledger, **3,842,041 requested /
3,942,880 backing bytes in 2,456 allocations**. Peak is **3,878,214 / 3,984,104 bytes**,
with arena commitment **4,210,330 debug / 4,213,402 release**. Full shutdown returns
exactly to **7,424 / 7,600 bytes in four allocations**.

Both builds also pass **twelve fresh-process invalid-data cases**: the prior six
BSWF/font guards, missing movie/font UIPG files, truncated movie pixels, invalid
header/table, and a deferred page checksum failure. Metadata failures precede load
publication; corrupt pixels fail on their first stream before a completed movie
presentation/capture. No generic allocation failure or EE crash is accepted. Archives
under `build/test-results/` retain matched ELF/symbols/map/reports, configuration,
authored inputs/mutations, logs and captures. The shared-cache ASan/UBSan harness passes
with assertions 0/1, covering cross-image keys, LRU pressure, corrupt-read preservation,
detach/reopen identity, owner bounds, sampler restrictions and partial cleanup.
All **290 Python host regressions** and the existing sanitizer harnesses pass.

The existing `test-renderer` matrices pass **fourteen isolated processes per build**:
NTSC/PAL scenes plus twelve release-preserved failure guards. Positive archives are
debug `20261017T001524Z_smoke_b513b8c381474b02` /
`20261017T001549Z_smoke_fbd72621ecc143ac` and release
`20261017T001643Z_smoke_0bce1dc0a96745b8` /
`20261017T001706Z_smoke_115b8815ce5a4ad4`. Production `test-headless` also passes
120-frame and live command/quit runs with no rendering device and exact cleanup:
debug `20261017T001804Z_headless_f624128d5b4c` /
`20261017T001809Z_headless_0f25486ce519`, release
`20261017T001818Z_headless_a20fb343749a` /
`20261017T001822Z_headless_e4b952125724`.

Production retains **363 direct objects**, zero test inputs/symbols and no GC. Fixed
`PT_LOAD` is **11,039,078 debug / 11,461,990 release bytes**; compile database entries
remain **388**. The earlier renderer page scene still supplies constrained eviction/
filtering coverage. These measurements establish authored native binding, not a retail
menu working set or physical PS2/cache/performance acceptance. Next admit the bounded
English retail font/image/material and BSWF profile, preserve native layout metrics,
and measure its active working set before mask/blend closure and offline Shell startup.

## Native English retail font resources (2D-5 slice)

Accepted **2026-10-17, Australia/Sydney**, in production debug/release NTSC/PAL.
The local retail cook now emits a bounded **UICT v1** resource catalog. Native adapters
admit five complete English font families, image descriptors, aliases and implicit
constant UI materials. The opt-in resource screen draws **Campaign** through native
`idFont` at 12/24/48-point scales. This is a font/resource proof: it does not load a
retail SWF, initialize Shell or complete milestone 2D-5. Production remains in NOMAP;
sound and world rendering remain inactive.

The **5,236-byte** immutable catalog contains twelve images, five fonts, nineteen
aliases, twelve material bindings and 1,528 bytes of UIPG descriptors. Validation
checks exact offsets, bounds, paths, canonical identities, references and English-only
flags before publication. Font length/checksum/layout and glyph/ASCII bounds are
checked before native parsing receives an immutable memory snapshot. Complete modern
font records preserve native duplicate-codepoint selection. Legacy 12/24/48-point
width/height pairs come from inspected source files; Sarasori retains the native
fallback when old files are absent. No glyph subsetting or modern-extrema substitution
changes those layout metrics. Only known implicit image-default materials are admitted;
their constant native stages are generated, without loading retail shader text.

Real font glyphs touch atlas edges. Optional outer half-texel admission preserves
native glyph geometry and UV expansion through replicated gutters; authored UV bounds
remain strict by default. Five open font atlases share **six linear/clamp slots**:
**1,572,864 pixel bytes + 262,144 scratch bytes + 384 record bytes = 1,835,392 requested
RENDER payload bytes**. Objects, catalog, glyph arrays, intrinsics and allocator
overhead are separate. Other catalog images stay unopened. Each selected word uses
at most four pages; sequential family changes and returning to the first yield
**22 loads / 16 evictions**. The subsequent warm reload adds no page loads. This does
not establish the complete retail menu's simultaneous working set or transition peak.

| Configuration | NTSC archive | PAL archive |
| --- | --- | --- |
| Debug | `20261017T072657Z_retail_dca885ee656e` | `20261017T072717Z_retail_623547b5418a` |
| Release | `20261017T072821Z_retail_3c6c488e9b07` | `20261017T072840Z_retail_15b5e6b8faac` |

Each run completes seven phases of at least 120 frames: five font families, return to
Arial Narrow, then its warm reload. Native metric checks cover all three scales;
source-derived stroke/hole probes and ink presence check each family. All five fonts
were visually inspected in both standards and configurations: readable, distinct
families with no visible clipping or page seams. First/return/reload captures match;
debug/release captures match per standard. Selected Arial Narrow hashes are NTSC
`e8e41c936e4b68541a48ca3cc6b73323915c2a15098cdd0e64d8c3ffdb0aca98`, PAL
`15c111ff1dfde7d26c3acf6aed23ed7c386e24829dcffe54c882e2b46a21efb0`.
Retail captures, contact sheets, cooked data and matched build evidence stay under
ignored `build/`; no proprietary asset is committed.

All four runs peak at **4,392,239 requested / 4,495,188 backing bytes**. Arena commitment
is **4,734,170 debug / 4,733,530 release**. Warm unload retains fonts/images at
**4,388,908 / 4,491,596 bytes in 2,497 allocations**; full shutdown returns exactly to
the startup baseline of **7,424 / 7,600 bytes in four allocations**. Each build also
passes five fresh-process failures: invalid catalog header, missing or mutated font,
invalid UIPG table and deferred pixel corruption. Every failure requires its specific
diagnostic before the first completed retail presentation; corrupt pixels fail before
cache replacement. Assertions are disabled in release.

Strict debug/release production and core builds pass, with **368 production direct
objects**, zero test inputs/symbols and no linker GC. Production fixed `PT_LOAD` is
**11,055,910 debug / 11,478,438 release bytes**; core is **2,093,296 / 2,177,264**.
The compiler database has **393 entries**. All **309 Python host regressions** and
ASan/UBSan harnesses pass, including new UICT/font readers, geometry and shared-cache
checks with assertions 0/1. Existing authored UI matrices (two standards plus twelve
failure cases per build), renderer matrices (two standards plus twelve failure cases)
and production headless bounded/live-command shutdown tests pass in debug/release.

Run `make ui-cook` then `make run-retail-ui`; select PAL with `CONSOLE_STANDARD=pal`
and release with `BUILD=release`. `make test-retail-ui` runs the resource gate for
both standards and all five failures. Append `ps2_uiRetail font 0` through `font 4`,
`ps2_uiRetail status` or `quit` to `build/<config>/ui-retail/console.txt` while running.
See [the pipeline guide](RETAIL_UI_PIPELINE.md) for staging and command details.

Next admit a bounded retail BSWF and bind its English labels/resources, then measure
the actual active-menu working set. Masks, additive/special blends and offline native
PressStart → Root → Quit-dialog integration follow. Neither these emulator results
nor the font screen establish physical PS2 cache/performance or full menu acceptance.

## Inactive retail dictionaries and English text (2D-5 slice)

Accepted **2026-10-17, Australia/Sydney**, in production debug/release NTSC/PAL.
The original retail shell/dialog BSWFs now load through native `idSWF::LoadBinary`
from checked immutable snapshots. A resource-only constructor retains dictionaries,
geometry, fonts and timeline storage while keeping imported placements and AVM1
inactive. Full movie Render, Activate, HandleEvent and timeline advancement fail
explicitly in this mode. This does not render an incomplete retail movie by dropping
its masks/blends, initialize Shell or complete 2D-5.

A strict shared storage reader checks every retained allocation/range, dictionary
kind, font identity/reference, finite geometry and triangle index before native
parsing. Bounds include a **1 MiB** movie, **1,024 dictionary records**, **512 sprites**,
**32,768 total commands/vertices**, **65,536 indices** and **eight font records**.
The actual shell contains **618 dictionary records, 256 sprites, 15,600 commands,
three fonts and 117 edit texts**; dialog contains **56, 26, 280, two and five**.
Initialization actions are rejected because native sprite Init would execute them
before the first frame. Other command buffers remain opaque; these checks do not
admit placement semantics, AVM1 execution or dynamic resource/callback closure.

Staging additionally checks source SHA-256 and complete host inventory, then emits a
fixed **64-byte UIMV** profile for two movie identities and English. Target checksum
validation precedes structural preflight; self-consistent corrupt metadata still
fails. The complete English dictionary loads through native localization:
**9,097 entries / 563,876 source bytes**. Native grammar/count/escape and UTF-8 checks
run in release too. Localization aliases and modern font bytes remain intact.

Three selected label IDs use native edit-text instances, native font lookup and the
original layout/drawing code. The original shell prototype **121** produces centered
white 36-point text; dialog prototype **29** produces left-aligned orange 34-point
text. Native wrapping, margins, alignment, source color, glyph sampling and integer
GetTextLength truncation are preserved. The screen positions these isolated records
for inspection; it is not the actual menu hierarchy or its timeline state.

| Configuration | NTSC archive | PAL archive |
| --- | --- | --- |
| Debug | `20261017T083403Z_movies_59f4572c89e8` | `20261017T083420Z_movies_ed2413998e17` |
| Release | `20261017T083516Z_movies_50e7912d36ff` | `20261017T083532Z_movies_67fd043cf0fb` |

Each run completes four phases of at least 120 frames: shell, dialog, shell return
and another shell load. Every capture checks source-derived label widths and **32
stable glyph/color probes**, using the existing channel tolerance of eight within one
projected pixel for raster rounding. All eight shell/dialog captures were visually
inspected: readable original styles, complete labels and correct alignment in both
standards/builds. Shell captures are byte-identical across reloads and configurations
within each standard. Their hashes are NTSC
`c6be866093fdb4eb773a7d518d03c5779545d4e4a0f3294ac5719c599d11719d`, PAL
`c39e14462dd34a710d56d5c4d3b0d3d2c50c83158ebce77ffcd90deaa8019c6b`.
NTSC dialog debug/release differ in four low-coverage pixels on the final T, with a
maximum channel difference of 25; source probes pass in both, and no bit-identical
cross-optimization dialog claim is made. Retail captures and original files stay
under ignored `build/`; none is committed.

Seven images share the existing six linear slots and scratch page. Five font tables
plus the two lazy movie atlas tables use **984 record bytes**, for **1,835,992 requested
RENDER payload bytes** including shared pixels/scratch. Only **three font pages** load
and remain warm without evictions; movie atlas pixels are not drawn. First use of
dialog adds **1,360 requested / 1,812 backing bytes in eleven allocations** of cached
image/material metadata. Once both bindings exist, repeated shell loads stay exactly
at **6,717,176 requested / 6,912,048 backing bytes in 4,704 allocations**.

All four runs peak at **7,362,559 / 7,555,996 bytes**. Arena commitment is **7,590,362
debug / 7,588,954 release**. The first shutdown check exposed native ClearDictionary's
intentional retention of keys, hash and allocator pools for language reloads. A
separate tagged PS2 full-release operation now frees them after all borrowers shut
down, preserving the existing Clear semantics. Full shutdown in all accepted runs
recovers **7,424 requested / 7,600 backing bytes in four allocations**, exactly baseline.
These are inactive dictionary/text measurements, not an active-menu working set.

Each build also passes six isolated failures: bad UIMV header, missing movie, corrupt
movie checksum, oversized count, initialization actions and corrupt English data.
Count/init cases recompute the profile FNV so the target structural reader must reject
them. Specific fatals precede any loaded movie resource or capture; EE crashes and
generic allocation failures do not pass. Strict production/core debug/release builds,
**316 Python host regressions**, sanitizer readers and existing cache/geometry harnesses
pass. The new reader also passes sampled truncations/mutations of both real local
movies and the full real English grammar check. No new target fixture is introduced.

Production retains **369 direct objects**, zero test inputs/symbols and no GC. Fixed
`PT_LOAD` is **11,066,918 debug / 11,490,086 release bytes**; core is **2,101,488 /
2,185,328**. The compiler database contains **394 entries**. All affected regression
matrices also pass: authored SWF has fourteen isolated processes per build, retail font
has seven per build, and production headless has bounded/live-command runs per build.
Positive authored UI archives are debug `20261017T083710Z_ui_61997ac73924` /
`20261017T083730Z_ui_3e3a96991995`, release `20261017T083827Z_ui_f6f95ca9defb` /
`20261017T083846Z_ui_08ce5a85c594`. Retail font archives are debug
`20261017T083942Z_retail_d6f3859ebfac` / `20261017T084002Z_retail_41b7925cb42a`, release
`20261017T084040Z_retail_cd3d10413611` / `20261017T084059Z_retail_03ab136c56df`.
Headless archives are debug `20261017T084139Z_headless_df0f114fa6e2` /
`20261017T084143Z_headless_01292b1c0dbb`, release `20261017T084152Z_headless_d4eff2eff2da` /
`20261017T084156Z_headless_2a301b24df26`. No physical PS2 acceptance is claimed.

Run `make ui-cook` then `make run-retail-movies`; select `BUILD=release` and/or
`CONSOLE_STANDARD=pal` as needed. `make test-retail-movies` runs both standards and the
six failure cases; use `BUILD=release` for the assertions-off gate. Append
`ps2_uiRetail movie shell`, `ps2_uiRetail movie dialog`, `ps2_uiRetail movie none`,
`ps2_uiRetail status` or `quit` to `build/<config>/ui-retail-movies/console.txt`.
The [pipeline guide](RETAIL_UI_PIPELINE.md#inactive-retail-dictionaries-and-english-text)
contains complete examples and ownership details.

Next close bounded placement/AVM1 execution and dynamic resource/path contracts,
measure the actual menu bitmap/font working set, and implement required masks/blends
before accepting full retail movie rendering and offline PressStart → Root → Quit.

## Scoped retail timelines and native widget owners (2D-5 slice)

Accepted **2026-10-18, Australia/Sydney**, in production debug/release NTSC/PAL.
Original shell/dialog timelines now execute through the native AVM1 interpreter in an
explicit production execution mode. Construction remains inactive, and full movie
drawing/event routing still fail explicitly. The inspection screen continues to draw
three isolated English text records. This establishes timeline and widget ownership;
it does not complete retail menu rendering, navigation or milestone 2D-5.

A second target preflight validates placements, dictionary references, display changes,
sprite graphs, branches, constant pools, registers and function bodies. The execution
profile admits the **32 action families** used by these movies and the authored demo.
Release checks bound instruction/command/call/allocation work, recursion, stacks,
strings, properties and live instances. Native services require the actual resolved
function object, correct owner and supported signature. A bounded origin stack protects
every active script source and receiver against destructive ancestor seeks, including
outer callers hidden by nested functions and native widget callbacks. This selected
profile does not establish general AVM1 lifetime safety.

The shell borrows a minimal native menu owner. The original carousel retains five
visible slots with **one Doom 3 option** and its admitted cover material; the original
Credits screen installs its real callback and retains its idle-state gate. These are
native owners rather than placeholder callbacks. Callback references stay pinned until
the owner detaches from its borrowed movie, before movie destruction. Complete Shell
startup, screen transitions, Credits navigation, saves, online services, sound and
Classic Doom remain unavailable.

The first shell frame contains **1,855 sprites / 485 texts / nine levels**; animation
adds instances within the **2,048 / 512 / 16** live limits. Dialog contains **49 / 11 /
five**. Native movie rate and bounded catch-up advance before GS submission, once per
Common frame; synchronous redraws do not run scripts. The root can remain on frame
one while descendant timelines continue to tick. Execution counters distinguish Common
ticks from actual native timeline steps.

The Doom 3 cover adds only cached image/material metadata: **eight images / 1,032 page
record bytes** after both movie bindings exist. Six shared linear slots and one scratch
page still request **1,836,040 RENDER payload bytes** including records. Only three font
pages load, remain warm and incur no eviction. Movie/cover bitmap pixels are not drawn,
so these figures do not measure an active menu's artwork working set.

Loaded heap usage varies with clock-driven animation. The acceptance gate keeps a
**16 MiB** loaded-heap ceiling and compares exact ownership after movie/widget teardown.
Warm release retains **5,446,190 requested / 5,551,040 backing bytes in 2,548
allocations** after both lazy image bindings exist. Console history is cleared before
the final unload: HostFs queues the entire batch into history first, so a future long
echo identity would otherwise add a transient 64-byte string at that checkpoint.

| Configuration | Executable NTSC archive | Executable PAL archive |
| --- | --- | --- |
| Debug | `20261017T232203Z_movies_7ce187b923fe` | `20261017T232254Z_movies_aa53c53ff00a` |
| Release | `20261017T232515Z_movies_a2f665139d77` | `20261017T232600Z_movies_76f2b46935c9` |

Each run completes four phases of at least 120 Common frames: shell, dialog and two
shell returns. Native timeline-step counters advance beyond activation; each load must
produce fresh real callback traces and the shell's exact carousel/Credits owner state.
All **32 source-derived text probes** pass. Shell reload captures match; all shell/dialog
styles were visually inspected in both standards/builds for complete, readable labels
and correct alignment. Final capture is readable, newer than the last movie capture and
drained before shutdown. Retail captures and cooked data remain under ignored `build/`.

Across these runs, peak heap usage is at most **11,137,065 requested / 11,838,584 backing
bytes**. Arena commitment is **11,986,395 debug / 11,984,219 release**. Animation phase
changes the loaded peak; these figures are measured maxima, not deterministic live
ledgers or campaign budgets. All runs recover exactly **7,424 / 7,600 bytes in four
allocations**, matching startup. Fixed `PT_LOAD` is **11,139,621 debug / 11,575,973
release bytes**; the separate core test images are **2,118,896 / 2,203,120**. Production
retains **373 direct objects**, zero test inputs/symbols and no GC. The compiler database
contains **398 entries**; the source audit classifies all **458** upstream units.

Both complete retail matrices pass **sixteen isolated processes per build**: inactive
and executable NTSC/PAL plus twelve negative cases. Four new malformed placement/action
cases reach execution preflight after recomputing the profile checksum. A valid infinite
loop reaches the instruction budget; an alias to a different real native service fails
function identity before dispatch. Specific diagnostics are required before movie
publication or any capture; crashes, unrelated fatals and generic allocation failures
do not pass. Strict production/core builds and **329 Python host regressions** pass.
ASan/UBSan harnesses cover the shared action reader and runtime limits, including all
active caller origins with assertions enabled/disabled. No new native fixture is added.

Three consecutive dialog executions also pass in both standards/builds: debug
`20261017T232743Z_movies_da67a30ed913` / `20261017T232754Z_movies_e1a32ca6fce6`, release
`20261017T232954Z_movies_806575a71be9` / `20261017T233006Z_movies_b84fb42cb04f`.
Warm dialog captures match. The cold NTSC final-T edge reproduces the previously
measured four-pixel / 25-channel variance at source glyph row 332, x=81–96; all 32
source probes remain required. PAL captures are identical across all three loads.
The latest classifier rechecks all eight executable archives and rejects a counter
that never advances beyond activation's single step.

Existing authored UI positive animation/input/reload checks pass in NTSC/PAL:
debug `20261017T232807Z_ui_face18f8ed5a` / `20261017T232827Z_ui_3ad388c073be`, release
`20261017T233018Z_ui_4f0f4d69839b` / `20261017T233038Z_ui_60a7c32420e7`.
Production headless bounded/live-command checks pass with exact cleanup: debug
`20261017T232851Z_headless_0044931d4f6c` / `20261017T232949Z_headless_3499b4999c28`, release
`20261017T233102Z_headless_c89c9d5f4151` / `20261017T233106Z_headless_d1eb1cb7a842`.
One prior debug console attempt remains failed: PCSX2 trapped in its host SIGINT/Qt
shutdown handler after native shutdown completed. The macOS report identifies a
recursive allocator lock; the unchanged fresh retry passes. Its archive and triage
are recorded in [the emulator rules](../.claude/rules/testing-pcsx2.md). No process-exit
or crash check was relaxed.

Run `make ui-cook` and `make run-retail-movies`, then append
`ps2_uiRetail execute shell` or `ps2_uiRetail execute dialog` to
`build/<config>/ui-retail-movies/console.txt`. `movie shell|dialog` keeps inactive
inspection available. `make test-retail-timelines` checks executable NTSC/PAL reloads;
`make test-retail-movies` also checks inactive loads and twelve expected failures.
Select `BUILD=release` and/or `CONSOLE_STANDARD=pal` when staging. See
[the execution guide](RETAIL_UI_PIPELINE.md#scoped-retail-timeline-execution).

Next close the retail mask/additive/special-blend requirements before full movie
drawing, then resolve dynamic screen resources and measure simultaneous artwork/font
residency. Offline native PressStart → Root → Quit-dialog navigation follows those
gates. No physical PS2 cache, throughput or complete menu acceptance is claimed.

## Native SWF additive blending (2D-6 first slice)

Accepted **2026-10-18, Australia/Sydney**, in debug/release NTSC/PAL. Native SWF blend
mode **8** now draws solids, textured sprites, indexed geometry and streamed pages
through one GS pass. The native GUI shader premultiplies its straight source before
ONE/ONE blending; the GS receives straight RGBA and computes **destination + source ×
alpha** instead. Texture and vertex alpha multiply once, and overflowing RGB saturates.
Ordinary ONE/ONE requests outside the SWF scope still fail. Nonzero additive color
transforms, screen mode 4, masks and full retail movie drawing remain unavailable.

The existing authored movie adds six native PlaceObject3 records, reusing its original
sixteen-entry dictionary and two image atlases. It checks zero/half/full alpha,
overlapping sums, saturation, transparent bitmap holes and the x=254 page seam.
All **42 source/reference pixel checks per capture** pass across three animation/input/
reload cycles. The unscaled [NTSC](images/swf-additive-2d-6-ntsc.png) and
[PAL](images/swf-additive-2d-6-pal.png) captures were inspected for readable original
text, intact status/console interaction, complete glyphs and clean additive regions.
Expected additive RGB includes each five-bit framebuffer storage round; the existing
eight-channel tolerance is unchanged. See [the GS arithmetic notes](../.claude/rules/gs-renderer.md#doom-native-swf-additive-blending).

| Configuration | Authored UI NTSC archive | Authored UI PAL archive |
| --- | --- | --- |
| Debug | `20261017T235351Z_ui_0d615bfe1e40` | `20261017T235411Z_ui_690d066b4452` |
| Release | `20261017T235508Z_ui_0ddb82878dfc` | `20261017T235528Z_ui_1e56c01627e9` |

Both complete authored UI matrices also pass all twelve malformed-resource cases.
The existing core renderer scene now checks **27 base pixels / 33 native adapter
pixels**, including quarter weight from half texture × half vertex alpha, black-source
preservation, additive sprites, overlap, saturation and restoration of ordinary alpha.
Indexed and paged scene checks, timeout recovery, context flips, 1 KiB command halves,
pool/residency pressure and cleanup continue to pass. Both renderer matrices retain
twelve release-safe failure probes, including unscoped ONE/ONE and nonzero color2.
Positive renderer archives are debug `20261017T234749Z_smoke_cf29e40f910f4177` /
`20261017T234813Z_smoke_4e581908e8464b4a` and release
`20261017T235057Z_smoke_34ce414ec3d74f1d` / `20261017T235120Z_smoke_70451ccf274d4fe3`.

No new native fixture, pixel buffer, surface or composition pass is introduced.
Sprites/triangles retain **22/30 qword** packets and write ALPHA/PRIM state explicitly
after drains. The authored profile still shares four nearest slots (**1 MiB**), one
**256 KiB** scratch page and **56 record bytes**; four warm page loads incur no eviction.
Across all four production runs, peak heap usage is **3,879,948 requested / 3,985,980
backing bytes**; arena commitment is **4,212,059 debug / 4,213,595 release**. Warm unload
retains **3,842,175 / 3,943,140 bytes in 2,459 allocations**. Full shutdown recovers
**7,424 / 7,600 bytes in four allocations**, exactly baseline.

Strict production/core builds and **330 Python host regressions**, sanitizer readers,
runtime and shared-cache harnesses pass. The generator's exact admission profile is
reproducible with `build_authored_ui.py --check`. Production retains **373 direct objects**,
zero test inputs/symbols and no GC; fixed PT_LOAD is **11,139,749 debug / 11,576,485
release bytes**. Source groups and the **398-entry** compile database are unchanged.
Production bounded/live-command headless checks pass: debug
`20261017T235219Z_headless_57a5e62667e3` / `20261017T235224Z_headless_2bc22c3131dd`, release
`20261017T235234Z_headless_3c5830d70d04` / `20261017T235238Z_headless_97f3e85d5deb`.

Run `make run-ui` to inspect this scene, or select `BUILD=release CONSOLE_STANDARD=pal`.
`make test-ui` and `make test-renderer` exercise the corresponding production/core
gates; use `BUILD=release` for assertions-off checks. Next implement the required mask
and remaining color/blend contracts before enabling full retail movie rendering and
offline Shell navigation. No physical PS2 throughput or full menu acceptance is claimed.

## Native convex SWF masks (2D-6 second slice)

Accepted **2026-10-18, Australia/Sydney**, in debug/release NTSC/PAL.
The first mask profile captures the native `RenderMask` fill triangles and clips
content on the CPU, without a GS stencil buffer or mask pixel surfaces. Captures
admit **64 input triangles**, **16 simplified convex boundary points** and **four
active masks**. Reverse interior edges cancel; a single closed convex boundary
proves exact coverage. Empty masks clip everything. Concave, disconnected, holed,
overlapping, nonconforming and stroked geometry remains explicitly unavailable.

Mask ownership uses native display-entry identities, so crossing clip ranges can
expire in either order. Every mutation drains completed write-only spans. The PS2
expiry loop revisits entries swapped by `RemoveIndexFast`, fixing simultaneous
expiry. Native state must agree with the inherited/local active count; arbitrary
stencil bits remain unsupported. Scope, frame and shutdown boundaries reject leaks
in release too. Both indexed content and direct rectangle/font draws share clipping.
Original triangle-edge/mask-plane constraints reconstruct intersections, preserving
full-atlas UVs and float color interpolation before final byte rounding. Unmasked
geometry keeps its existing direct path.

The existing authored movie now has **20 dictionary entries**, **eight convex mask
placements** and a peak of **three active masks**. Its original bitmap/font atlases
are unchanged. All **77 source/reference pixels per capture** pass through three
load/input/reload cycles in debug/release NTSC/PAL. They cover oblique trapezoid
boundaries, nested/crossing intersections, simultaneous expiry, restored parent
clipping, solid/half-alpha/additive strips, bitmap page seams/holes and native text.
The [NTSC](images/swf-masks-2d-6-ntsc.png) and
[PAL](images/swf-masks-2d-6-pal.png) captures were inspected for intact text, clean
clip edges, transparent glyph holes and preserved original UI regions. Pixel
colors and the existing eight-channel tolerance remain unchanged.

| Configuration | Authored UI NTSC archive | Authored UI PAL archive |
| --- | --- | --- |
| Debug | `20261018T002132Z_ui_22f4f68962b5` | `20261018T002208Z_ui_5ea98e6dd667` |
| Release | `20261018T002318Z_ui_f34a5871030b` | `20261018T002355Z_ui_97e19a6f293e` |

Both matrices also pass all twelve malformed-resource cases. Two early runs
(`20261018T001704Z_ui_8c27feda4750`, `20261018T001910Z_ui_c5d77f9620aa`) were rejected
because frame-count animation probes repeatedly sampled the same pulse phases.
The runner now uses twelve distinct bounded waits, including short intervals,
while still requiring a different fractional-alpha phase, complete shutdown and
the original watchdog. No crash, animation or pixel criterion was relaxed.

The host sanitizer suite passes **334 Python regressions** plus its C++ harnesses.
The existing triangle harness now covers topology rejection, capacity limits,
reflected meshes, interpolation, hostile floats, fractional shared edges and
3,000 deterministic four-mask intersections under reversed winding/mask order.
No new native fixture or executable is added.

The retail inventory remains a separate gate. Dialog has ten placements using two
rectangular shapes. Shell has 54 placements referencing 29 mask shapes, including
a 185-point outline, disconnected regions, concave text outlines and holes; two
masks also have opaque boundary strokes. Its largest fill vertex count is 198; the largest triangle count is 183. Conservative all-frame display graphs show at most one inherited/
local active mask, but fourteen shell placements rotate/shear the local rectangle.
A bounded triangulated representation and explicit stroke coverage are the next
mask candidates. Full retail `Render` remains gated, as do screen blend 4, nonzero
additive color transforms, active-artwork residency and offline Shell navigation.

Both existing renderer matrices pass **19 isolated cases**: NTSC/PAL positive
scenes and seventeen expected-fatal probes. The five new mask probes require
concave coverage, scope leaks, unknown removal keys, active-count mismatch and
stroke masks to fail with assertions off. Existing geometry/page pixels, timeout
recovery, context flips, pool/residency pressure and cleanup remain accepted.
Positive renderer archives are debug `20261018T002507Z_smoke_e8e38fb31db7409e` /
`20261018T002531Z_smoke_79ad3d3e48d94129` and release
`20261018T002635Z_smoke_9b0a680b808a416b` / `20261018T002658Z_smoke_5d09e03e510346ab`.
Production headless bounded/live-quit regressions also pass in both builds:
debug `20261018T002805Z_headless_0396520fe5b4` /
`20261018T002809Z_headless_8d27771683a3`, release
`20261018T002819Z_headless_92fbf113bdc5` / `20261018T002824Z_headless_7a2c585e6adb`.

The CPU mask state occupies **4,404 fixed resident bytes**, with no new pixel/VRAM
allocation. GCC reports 7,440 bytes of clipping scratch and a 1,888-byte dispatcher
frame; individual stack frames and the 128 KiB reserve are recorded in the
[memory notes](../.claude/rules/memory-budget.md#doom-convex-ui-mask-budget-2026-10-18).
The twenty-entry movie peaks at **3,885,301 requested / 3,992,336 backing bytes**;
arena commitment is **4,212,891 debug / 4,214,043 release**. Warm unload remains
**3,842,175 / 3,943,140 / 2,459**, and full shutdown **7,424 / 7,600 / 4**.
Production `PT_LOAD` is **11,159,397 debug / 11,596,517 release bytes**; core is
**2,139,184 / 2,223,920 bytes**. Production retains **374 direct objects**, zero
test inputs/symbols and no GC; the compile database has **399 entries**, and the
audit still classifies 458 upstream units. These are functional emulator checks,
not physical PS2 cache, throughput or frame-rate acceptance.

Run the latest authored scene with `make run-ui`; use `make test-ui`,
`make test-renderer`, `make test-headless` and `make test-host` for the associated
gates, adding `BUILD=release` for assertions-off target checks. Next address the
remaining complex shell mask geometry and strokes before the other retail drawing
and native offline menu gates.


## Documentation audit and agent handover (2026-10-19)

Reviewed project plans, developer controls, agent rules, provenance and fixture/tool
notes against cleanup through `6843948` and the current source manifest. Current
summaries distinguish retail loading, scoped timeline execution and full drawing.
The convex-mask slice and fixture relocation are reflected in the roadmap, and
links/claims about the deleted copied Quake II Makefile have been removed. Dated
acceptance measurements remain historical evidence.

[AGENT_HANDOVER.md](AGENT_HANDOVER.md) records the continuation sequence, source
entry points, profile/ownership invariants, local data policy, commands and relevant
existing verification gates. README remains focused on future players and links
to that developer entry point.

Documentation checks pass for local Markdown paths/heading fragments and current
guide Make targets. Fresh `make inventory` confirms 458 classified upstream units,
282 campaign sources, 48 core backend and 34 resident support sources;
`make compiledb` emits 399 entries, and authored generator `--check` passes.
This is a documentation-only update. No new emulator, gameplay, menu, hardware or
performance acceptance is claimed.

## GitHub Actions CI (2026-10-19)

GitHub Actions now builds the port. `ps2.yml` runs `make` and `make release` on Linux in
the ps2dev project's `ghcr.io/ps2dev/ps2sdk` image, pinned by digest to its 2026-10-13
build: ps2sdk `329e2bb`, GCC 15.2.0 and binutils 2.45.1, matching the local toolchain. It
covers the source audit, every production object and the resident link checks, and keeps
no ELF. `tools.yml` builds the C++ host tools with GCC, Apple clang and MSVC through the
new `src/tools/CMakeLists.txt`, which `make resource-unpack` now drives as the single
owner of their flags; the MSVC executable links its runtime statically and is kept as a
run artifact. Neither workflow runs host tests, test executables or PCSX2 gates. Details
and traps are in [ci.md](../.claude/rules/ci.md).

Linux file names are case-sensitive. Thirty-three quoted includes in compiled id units
spelled `idLib/`, `snapshot_jobs.h` or `image.h`, which the Mac's volume resolved. Each now
matches the disk and carries a `[PS2_D3BFG]` tag: 30 `d3xp/menus` units,
`sys/LightweightCompression.cpp`, `sys/Snapshot.h` and `swf/SWF_Main.cpp`. Thirty-one more
remain in deferred or excluded sources that no target compiles.

| Local check | Result |
| --- | --- |
| Case-sensitive APFS volume with the image's SDK, the local EE toolchain and GNU make 4.4.1 | `make` and `make release` link 374 objects with zero unresolved or duplicate symbols |
| Same volume without the include fixes | Fails at the first wrong-case include |
| Same volume, other targets | `compile-core`, `compile-game`, `headless-core`, `platform-probe` and `headless-game` build |
| Working tree, macOS make 3.81 | Debug/release pass; `make compiledb` unchanged at 399 entries |
| Host tools through CMake | Apple clang build passes the 14 unpacker regressions; the EE g++ in LP64 mode compiles the source cleanly with the GCC warning set at `-O2`/`-O3` |
| `make test-host` | 334 Python regressions and the sanitizer harnesses pass |

Production `PT_LOAD` is 11,159,013 debug / 11,596,133 release bytes, before and after the
include fixes. That is 384 bytes below the convex-mask record in both configurations, with
no source change since: the local ps2sdk and newlib were rebuilt on 2026-10-19 (ps2sdk
`329e2bb`). No emulator gate has been rerun against the rebuilt toolchain.

`make script-probe` fails on both hosts, independently of these changes: since `7d629da`
sorted `script_boot.cpp`'s includes, `d3xp/Game_local.h` precedes `idlib/precompiled.h`.
CI does not build that probe. The first hosted runs followed the push of `232d758`: Host
tools passed with GCC, Apple clang and MSVC. PS2 game stopped at its first compile, because
the image's EE compiler loads GMP, MPFR and MPC libraries the image lacks; `34d3161`
installs them, and both configurations then linked in 568 s. No emulator, runtime or
hardware acceptance is claimed.

## Script probe repair and the rebuilt toolchain (2026-10-19)

`script_boot.cpp` again includes `idlib/precompiled.h` in its own commented first block.
The 2026-10-16 formatting pass had sorted `d3xp/Game_local.h` ahead of it, so the isolated
compiler probe no longer compiled. All fifteen compiler cases pass again in debug
(`20261019T121308Z_script_0c1dedcef5734fac` to `20261019T121336Z_script_e79e7f532c044568`)
and release (`20261019T121343Z_script_130ca5ff06b143ed` to
`20261019T121409Z_script_43d1798e46a54c76`).

This pass is also the first emulator run on the local toolchain rebuilt on 2026-10-19
(ps2sdk `329e2bb`, GCC 15.2.0, newlib built with `HAVE_BLKSIZE`, so stdio sizes its
buffers from `st_blksize`). Every regular target gate passes in both configurations,
each with its runner's complete case set:

| Gate | Cases per configuration |
| --- | --- |
| `smoke-platform`, `smoke`, `smoke-negative` | SDK probe, core foundation, expected missing fixture |
| `test-renderer` | NTSC/PAL scenes and 17 expected fatals |
| `test-common` | 7 lifecycle stops and 76 negative probes |
| `test-headless`, `test-console` | Bounded and live-quit runs; NTSC/PAL console |
| `test-ui` | NTSC/PAL authored movie and 12 invalid resources |
| `test-retail-ui` | NTSC/PAL English fonts and 5 invalid resources |
| `test-retail-movies` | Inactive and executable NTSC/PAL movies and 12 guards |
| `test-game`, `test-map` | Game fixture and 10 expected fatals; authored map |

Debug archives run from `20261019T121431Z_smoke_a25a3d839dd24e65` to
`20261019T123056Z_smoke_49d2036617f644ea`, release from
`20261019T123309Z_smoke_3b935fed6480463f` to `20261019T124825Z_smoke_89fd025e5f3a4e5c`.
The new authored NTSC capture matches the accepted 2D-6 image, and the executed retail
dialog screen draws its three English labels.

Production code is unchanged since the 2026-10-18 runs, so they compare the two
toolchains directly. Every tagged ledger matches: headless ready 1,845,287 / 1,942,720
bytes in 2,384 allocations, the authored UI peak of 3,885,301 / 3,992,336, warm unloads
and the exact 7,424 / 7,600 / 4 recovery. Only allocator arena commitment moves: +12,672
(debug) and +16,768 (release) bytes in the bounded headless run, consistent with larger
stdio buffers, and +384 / −3,712 bytes in the NTSC authored UI run. Production `PT_LOAD`
is 384 bytes smaller, as recorded above. These are PCSX2 functional results, not
hardware or I/O timing measurements.

## Original dialog drawn in full (2D-6 third slice)

Accepted **2026-10-20, Australia/Sydney**, in debug/release NTSC/PAL. Production now
draws the original `dialog.bswf` through native `idSWF::Render`, showing the root
menu's quit confirmation: the "EXIT GAME" title, "Are you sure you want to exit
DOOM 3?" and ACCEPT / CANCEL / CHANGE GAME, with the original textured band, masks,
additive glows and text strokes. `ps2_uiRetail dialog quit` selects it in the
retail-movies package. Captures were inspected; like every retail capture they stay
under ignored `build/` and are not reproduced in `docs/images`.

Full drawing is a third admission after storage and execution. The target preflight
counts shape outline records, screen-blend placements and nonzero additive color
terms, and admits drawing only when all three are zero. The dialog qualifies with ten
masks, each still checked by the convex CPU bridge at draw time; the shell's 15
outlines, five screen-blend and 29 additive placements keep it blocked. Both movies'
target counts match the independent host inventory exactly. The timeline still
advances once per Common frame before the GS frame; `Render` only draws, inside the
SWF draw scope and an execution scope. Event routing stays unavailable.

The dialog owner mirrors `idCommonDialog::Init` and `ShowDialog` for GDM_QUIT_GAME as
DIALOG_DYNAMIC: title/text keys come from native `GetDialogMsg` (on a zero-initialized
local manager, since Common's dialog manager stays uninitialized), options from the
English dictionary. The three choices are native callback objects that fail
explicitly if called. Retail `guiSolid` and `_white` now resolve to the single
vertex-colored stage native GUI surfaces evaluate, replacing the earlier `_white`
substitution for resource-only movies. No new GS path, pixel surface or fixture was
needed.

| Configuration | NTSC archive | PAL archive |
| --- | --- | --- |
| Debug | `20261020T014227Z_movies_ba3bde02d7c6` | `20261020T024117Z_movies_d7d8fec11046` |
| Release | `20261020T024748Z_movies_2b4029dffa3a` | `20261020T025327Z_movies_38c63907875a` |

A second debug NTSC session, `20261020T021239Z_movies_108e40a5711c`, reproduced every
counter, ledger and capture comparison exactly. PAL runs draw the same layout at
512×512; the inspected captures show intact text, masks, glows and textures in both
standards.

Each run loads the dialog three times: 120 frames, then two 30-frame warm reloads. Every
load logs one admission matching the inventory, the native quit owner state, the
dialog's script work (`showMessage`, `updatePrompts`, `updateCmds`, `calcNumLines`,
platform/localization callbacks and the `rollOn` frame) and rising draw and mask
counters. Two captures ten frames apart locate the only animation at rest, the selected
option's pulse (about 57×20 pixels); every other pixel matches across all six captures,
about 1.37 million pixels per NTSC run and 1.57 million per PAL run. The `dialog-blend`
negative re-encodes a root placement as screen blend and stops at the specific admission
fatal (debug `20261020T024745Z_movies_1be40ed68186`, release `20261020T025948Z_movies_51df9ee94762`).

The rest of `test-retail-movies` passes in both configurations too: inactive and
executable NTSC/PAL cases (debug `20261020T021021Z_movies_02f7d68f8f24` to
`20261020T021143Z_movies_c2c91195aa4a`, release `20261020T021945Z_movies_5ada1edd86e9`
to `20261020T022058Z_movies_727945080ead`) and all 13 negative cases (debug
`20261020T024700Z_movies_4132ffb544a1` to `20261020T024745Z_movies_1be40ed68186`, release
`20261020T025904Z_movies_5f4716b1232b` to `20261020T025948Z_movies_51df9ee94762`).

Every other gate that runs changed code passes with its complete case set in both
configurations: `test-retail-ui` (NTSC/PAL and five invalid resources), `test-ui`
(NTSC/PAL and twelve invalid resources), `test-renderer` (19 cases), `test-console`
and `test-headless`, plus `make test-host` (334 Python regressions and the C++
sanitizer harnesses, with new synthetic draw-feature counter checks). Debug archives
run from `20261020T015631Z_retail_7ef04cf4adb7` to `20261020T020114Z_headless_1265880d1424`,
release from `20261020T020515Z_retail_d6c8e3f82b75` to `20261020T020943Z_headless_b959ebb73261`.

Five slow dialog sessions ended early because their emulator window was closed during
the run (confirmed by the user): PCSX2 shut down cleanly with exit status 0 and no
target fault after 79–264 s. The archives are kept: debug NTSC
`20261020T015200Z_movies_d68994c46880`, release NTSC `20261020T020325Z_movies_09765a4a87a7`
and `20261020T022148Z_movies_66f75d9a7bd5`, debug PAL `20261020T021821Z_movies_71ffe2bb99e4`,
plus one exploratory run. Shutdown confirmation is disabled in the local settings, so a
closed window stops the VM at once. The runner now records why each session ended, and
the accepted runs above are fresh sessions, not retries of a crashed run.

**Page streaming dominates.** The 1152×596 dialog atlas spans 15 RGBA32 pages and two
fonts add theirs, while the retail profile shares six linear slots. Each frame draws
about 890 triangles and misses about 51 pages; every miss reads and checks 256 KiB
from HostFs, about 31 ms in PCSX2, so a frame takes about 1.6 s. More slots cannot fix
it: the GS memory left beside the framebuffers holds about ten such pages, below the
dialog's working set of about 20; the shell atlas alone spans 60 pages, of 143 in the
cooked profile. This is the measured
reason for the asset recompression work selected next. These are PCSX2 HostFs costs,
not hardware storage or GS fill measurements.

| Measurement (debug/release, NTSC/PAL identical unless noted) | Value |
| --- | ---: |
| Loaded dialog, first frame | 5,673,819 requested / 5,807,980 backing / 3,229 allocations |
| Loaded dialog at rest, every load | 5,690,707 / 5,827,180 / 3,281 |
| Every release, cold and warm | 5,443,407 / 5,547,524 / 2,531 |
| Heap peak / arena commitment | 6,079,651 / 6,183,080; 6,399,643 (release 6,400,923) |
| Full shutdown | 7,424 / 7,600 / 4 (exact baseline) |

Production `PT_LOAD` is 11,164,005 debug / 11,600,997 release bytes (+4,992 / +4,864),
with 374 objects, no unresolved symbols and no fixtures.

## Memory budget investigation: UI formats and the retail boot (2026-10-20)

Measured **2026-10-20, Australia/Sydney**, as the first step of the selected asset
recompression and 32 MiB work. Nothing was pruned or re-encoded yet: this section
records what the retail UI art and the retail declarations and scripts cost, and
which levers the numbers point to.

### Retail UI texture formats (host survey)

A scratch survey decoded the original atlases (dialog, shell, HUD, PDA and pause
movies, the Doom 3 carousel cover and the five fonts) and compared candidate GS
formats with the lossless cook. Its tools, reports and previews stay local under
ignored `build/analysis/ui-formats/`. Texel error is premultiplied RGB plus alpha, in 8-bit
units. A display simulation draws each atlas image at its stage scale with GS
bilinear sampling, straight alpha and PSMCT16 truncation, over black, gray and white,
and compares it with an area-filtered ideal. Every retail movie's stage is 1280×960,
so art drawn at its authored size appears at 0.4× horizontally and 0.47× (NTSC) or
0.53× (PAL) vertically.

| Screen | Now: RGBA32 pages | PSMT8, one CLUT per image | Halved where safe |
| --- | ---: | ---: | ---: |
| Dialog | 3,932,160 (15 pages) | 626,960 | 183,032 |
| Shell | 15,728,640 (60) | 3,075,792 | 1,176,528 |
| HUD | 1,572,864 (6) | 230,352 | 103,668 |
| PDA | 2,359,296 (9) | 405,104 | 335,168 |
| Pause | 6,553,600 (25) | 1,323,072 | 700,908 |
| Five fonts | 12,582,912 (48) | 901,120 (PSMT4 alpha) | 201,492 (English glyphs) |

Byte counts are texel storage plus 1 KiB per image CLUT, without page padding.

- **PSMT8 with one palette per atlas image is near-lossless.** Texel PSNR is 52.3 dB
  (dialog), 47.6 (shell), 55.6 (HUD), 51.3 (PDA) and 47.5 (pause); at most 0.43% of
  texels differ by more than 8/255. At display scale the error against the ideal stays
  within 0.1 percentage points of the current RGBA32 path. One palette per 512×512
  page scores 43–51 dB and leaves visible rings in the dialog's glow; the GS can bind a
  different CLUT per draw, so per-image palettes need no extra texture storage.
- **Halving is free where the art is not magnified.** id's atlas packer puts each image
  in a 4-texel-aligned block with at least one clamped border texel, so a 2×2
  premultiplied downsample never mixes images, and bilinear footprints at image edges
  stay inside the block. At authored size, halved art is still minified on NTSC and
  lowers the display error against the ideal (shell 2.70% → 1.97% of pixels two or
  more 5-bit levels off; HUD 14.9% → 10.3%), and it is neutral on PAL. Static fill
  and placement matrices bound how far each image is ever enlarged. Texels never drawn
  above 1.25×: dialog 96%, shell 85%, HUD 90%, pause 66%, PDA 25%. The PDA and pause
  menu enlarge pictures 2–4×, and those images keep full resolution.
- **PSMT4 is too lossy for art** (4–17% of texels more than 8/255 off) but suits the
  fonts, whose atlases are white RGB with DXT1-green alpha: 16 alpha levels display
  identically to 8-bit alpha. English uses 98 glyphs per font, 16–20% of each atlas.
- Every screen's candidate set fits the roughly 2.5 MiB of GS memory beside the
  framebuffers, so pages would stay resident instead of streaming each frame. The
  in-game set (HUD, PDA, pause and fonts) falls from about 23 MB of RGBA32 pages to
  about 1.3 MB; the menus (shell, dialog and fonts) from about 32 MB to 1.6 MB.

### Retail declarations and scripts on the EE

`make test-retail-boot` boots production headless with the retail `_common` and
`_ordered` containers mounted: every retail declaration folder loads, the complete
retail `doom_main.script` compiles, the game reaches the `NOMAP` console for 120
frames and shuts down. Production now prints tagged ledgers after the foundation,
declaration and game startup stages.

The first attempt ran out of memory 9.3 s into startup, while compiling scripts:
`idResourceContainer::Init` reads `_ordered.resources` wholesale into RAM (11.3 MB
`IDFILE`), and the arena reached 21.2 MB with 14 KB free. A tagged change now streams
it like `_common.resources`. A failed allocation now reports the request, the live
totals and every live tag before the fatal diagnostic.

| Startup checkpoint (debug) | Requested | Backing | Allocations |
| --- | ---: | ---: | ---: |
| Foundation, containers mounted | 1,250,136 | 1,302,080 | 1,248 |
| Declarations (materials, skins, sound shaders) | 4,204,461 | 5,313,644 | 24,885 |
| Game (its declaration folders, events, scripts) | 10,148,418 | 13,473,216 | 75,748 |
| Peak / arena commitment | 10,582,854 / 13,910,756 | 14,523,035 | |
| Full shutdown | 7,424 | 7,600 | 4 |

At game startup the largest owners (backing bytes) are compressed declaration text
`DECLTEXT` 4,759,828 in 11,947 allocations, `SCRIPT` 2,828,664 in 33,710, `DECL`
1,779,856, `STRING` 1,227,552, the two containers' file tables `RESOURCE` 1,079,480,
the smoke-particle pool `PARTICLE` 680,300 and event callbacks in `SYSTEM` 635,524.
Backing exceeds requested bytes by 3.32 MB (25%): every allocation pays the tagged
header and alignment slack plus dlmalloc rounding. The compiled program has 95,386
statements (1,907,720 bytes of the 3,515,932-byte static arrays already in BSS) and
3,070 functions; compilation takes 7.8 s in PCSX2 and the whole run 18.5 s. Archive:
`20261020T050009Z_headless_1a4fb98b209d`.

Before any map, the retail boot therefore commits 26,738,688 bytes debug (27,175,808
release) of the 33,554,432: the kernel's first MiB, 11.17 / 11.60 MB of `PT_LOAD` and
the 14.52 MB arena. That leaves 6.8 MB debug and 6.4 MB release for stacks, a level's
logic state, the in-game UI and load peaks. For scale, mars_city1's container holds
5.4 MB of binary collision and 2 MB of AAS, and its 5.48 MB `.map` is 4.85 MB of
brush/patch text the game parses at load, beside 592 KB of entity text (2,026 entities).

| Lever | Measured size |
| --- | ---: |
| Heap per-allocation overhead at the retail boot | 3.32 MB |
| Resident compressed declaration text | 4.76 MB |
| Container file tables (`idStrStatic<256>` name per entry, 3,598 entries) | 1.08 MB |
| Preallocated smoke particles | 0.68 MB |
| Script static arrays beyond this program's use (statements, globals, functions) | ~0.9 MB |
| mars_city1 `.map` brush/patch text | 4.85 MB on disc |
| UI textures, RGBA32 pages to the candidates above | ~22 MB of pages |

### Gates

Debug only, by the user's direction for this change: `make test-host` (336 Python
regressions, two of them new retail-runner checks, plus the C++ harnesses), core smoke
`20261020T050740Z_smoke_674d5629fe9b4eb8`, `test-headless`
(`20261020T050748Z_headless_3ae23afe53ba`, `20261020T050752Z_headless_31d7a03b1358`),
`test-console` (`20261020T050835Z_console_085074c2eae3`,
`20261020T050841Z_console_e01f246ac5c1`) and `test-retail-boot`
(`20261020T050802Z_headless_d0f6c5e7f049`, with the same ledgers as the measured run
above). Release and the core and game test ELFs build; release gates were not rerun.
Production `PT_LOAD` is 11,167,077 debug / 11,604,197 release bytes (+3,072 / +3,200).
