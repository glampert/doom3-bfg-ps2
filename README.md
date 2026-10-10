# Doom 3 BFG Edition for the PlayStation 2

A PlayStation 2 port of [id's Doom 3 BFG Edition](https://github.com/id-Software/DOOM-3-BFG)
using the free ps2dev SDK. The port builds a scalar EE core and an authored headless
game fixture that runs native initialization, entity thinking, script events, bounded collision/physics
and a restricted native `idPlayer`. Full campaign player/AAS/PVS startup, retail maps,
rendering, SPU2 output, input and saves remain later milestones; the port is not playable yet.

The engine is under `src/neo/`, moved intact in commit `4e5f082`. New console code is
under `src/ps2/`, and host/target regression tests are under `src/tests/`. Upstream
changes carry `// [PS2_D3BFG]` annotations. [IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md)
contains the campaign milestones; [docs/BUILD_INVENTORY.md](docs/BUILD_INVENTORY.md)
records the explicit source dispositions and deferred replacements. Third-party
dependencies imported for the port live under `src/external/`.

## Build

Install ps2dev with `mips64r5900el-ps2-elf-g++` and ps2sdk. The defaults are
`PS2DEV=$HOME/ps2dev` and `PS2SDK=$PS2DEV/ps2sdk`. This checkout was tested with GCC 15.2.
The Doom Makefile uses explicit source lists in `config/sources.mk`; the unchanged
Quake II reference is retained in `docs/reference/quake2.Makefile`.

```sh
make                         # current debug milestone
make release                 # release configuration
make platform-probe          # SDK/timer/alignment probe
make headless-core           # scalar Doom foundation and integration tests
make compile-core            # 52 scalar idlib units and 10 framework/session support units
make compile-game            # all 281 retained campaign units; compile gate only
make link-game               # retained campaign link and registration/map gate
make headless-game           # resident native game fixture ELF
make test-game               # boot/player/ticks/reloads and eight expected failures in PCSX2
make BUILD=release test-game  # same game checks with assertions disabled
make test-host               # shared heap tests with ASan/UBSan, runner tests
make smoke                   # core smoke run in PCSX2
make release smoke           # release core smoke run
make smoke-negative          # expected missing-fixture failure
make test-common             # seven partial-startup and fifty-nine expected-fatal probes
make compiledb               # compile_commands.json from real Make rules
```

Each configuration produces `build/<config>/d3bfg.elf`, matching
`d3bfg_unstripped.elf`, `d3bfg.map`, and `build-report.{json,txt}`. Reports distinguish
fixed ELF load-segment residency from runtime allocation. Flag/compiler/source-list
stamps invalidate affected objects automatically. Debug uses `-O2` with asserts;
release uses `-O3` without debug-only checks. Explicit cold backend code uses `-Os`.

All target C++ uses C++20 without exceptions, RTTI or thread-safe local statics. New
backend/tests pass the full strict GCC warning set with `-Werror`. Legacy style warning
suppressions and the narrow system-header treatment of upstream contracts are listed
in the build inventory. Format, array bounds and uninitialized-value warnings remain
visible. A host runtime test does not replace the EE compile/link check.

Debug/release core runs and the expected missing-fixture failure pass in PCSX2.
[docs/PORT_STATUS.md](docs/PORT_STATUS.md) records the checks, archived run identities,
ELF/startup memory measurements and resident-link acceptance.

M2b compile/link acceptance is complete: campaign headers separate OpenGL state from shared
renderer/menu contracts, and initial C++20/source portability fixes are in place.
All 281 retained campaign units now compile in both configurations. The four logical-
sound units use a portable sample/voice/device boundary without DirectX SDK headers;
the default executable remains the headless core. Six native profile, user and
snapshot/compression support units have been retained for the real Common layout and
offline services. Campaign achievements are retained too; their Classic evaluation
entry is explicitly unavailable.
Game class allocation now preserves heap alignment and checks signed memory counters;
factories use explicit fatal allocation without exception handling. Shared host/EE tests
cover this allocation boundary; the resident game fixture now tests native class startup and spawning.
The script compiler/program also compile without exceptions; an isolated EE probe tests
compilation, limits and explicit cleanup before fatal errors. `make test-script` runs it.
The resident fixture now runs the native interpreter and event registry; recoverable
script loading remains a later gate.
Portable common errors now terminate through the shared logging sink without desktop
dialogs or renderer/session calls. SWF JPEG errors explicitly release decoder/output
state; bounded input, dimensions and repeated/table-based decoding pass host sanitizer
and EE tests. JPEG and zlib live under `src/external/`, with explicit decompression/stream
source lists, original notices and tagged heap hooks.
Filesystem/ZIP/key units now compile with bounded device paths, stdio handles, an explicit
ZIP timestamp policy and the engine's fixed key-label table. Shared directory and file services are
tested on the EE; ZIP codec linking passes, while full campaign filesystem startup and
actual archive/game-resource use remain pending.
Retained menu, GUI-variable, file and model casts use checked portable hierarchy queries.
Shared host/EE tests cover null/sibling rejection, inherited/const types and separate-unit
identity; actual file objects and engine hierarchy declarations are checked on the EE.

`make link-game` attempts a real whole-object campaign link under
`build/<config>/resident/`, with its own strict support objects and no core filesystem
substitute. Debug and release now link all 344 inputs without unresolved symbols or
duplicate definitions. JSON/text reports retain object/source hashes, flags and the real
linker exit status, and verify the game/class/cvar registration roots and every input in
the map. Failed attempts remove stale resident ELFs. The resident entry requires the
runner's explicit game manifest before selecting the authored fixture.

`make test-game` boots native `idGameLocal`, parses a fixed floor/wall worldspawn `.map` through
the native loose-file filesystem, and spawns a logic entity through the real class
factory. Each of three map reloads runs 195 native `RunFrame` calls. The first eight
check logic-entity Think and script increments scheduled through `sys.waitFrame`;
frames 9–88 exercise native player-physics posture changes, then frames 89–99 tick a
real `idPlayer` in client slot zero through the native command/Think pipeline.
Frames 100–195 add two jumps, held-jump suppression/rearming, crouch walking and
eye-height interpolation, with authored idle/air/crouch actor-state transitions.
The script activates a native `idTarget_SessionCommand` on frame four. A queued 90 ms
activation fires on frame six (100 ms). Each command is returned and cleared in the
same frame. Script removal on frame seven (116 ms) invalidates the cached `idEntityPtr`
and cancels another activation queued for 120 ms; frame eight returns no command.
Map shutdown removes the map's script bindings.
Native collision tests cover point/swept-box traces, contents and masks, entity
filtering and clip-model disable/enable. The probe runs native `idPhysics_Monster`
through `RunPhysics` during its Think calls, moving toward the wall and stopping
before penetration. Two additional native probes verify gravity-driven falling,
floor contacts and rest, and grounded diagonal sliding along the wall with stepping
disabled. A fixture entity owns native `idPhysics_Player`, consumes synthetic commands
from `idUserCmdMgr`, accelerates into the wall, reverses and stops through native
friction after input release. Floor support and exact command consumption are checked
each frame. The same probe jumps and lands, stays grounded while jump remains held,
then jumps again after release. Crouch shrinks the native clip height from 74 to 38
units, prevents takeoff with jump also pressed, and restores standing height when
released in open space. The completed script stays at eight increments and the
monster probes become inactive during these additional player frames.
The player factory retains native entity/actor spawn and script threads, with a
fixture-only player setup and Think path that calls native speed adjustment, movement,
condition updates and actor script execution. Its constructor runs once per map and
the authored state advances once per tick. Forward/neutral, jump and crouch commands
exercise walking, friction and posture; native `getState` confirms script transitions.
Health, empty inventory, floor identity, linked
conditions, command cursors and native handle invalidation are checked. HUD/PDA,
models, weapons, view effects and sound/render worlds are not acquired.
Three reloads cover brush conversion, text `.cm` loading and
generated binary collision-cache loading. Clip/collision ownership returns to zero
at map shutdown.
Warm reload ledgers stay exact; full shutdown returns to the pre-boot ledger. Missing
maps, script errors, unsupported brush planes/materials, extra player spawn keys,
missing player script fields, unsupported commands and missing actor states must fail
with the expected diagnostic in debug and release.
This is partial M3 game acceptance. The fixture omits AAS/PVS,
sound and render worlds and uses separate bounded map/player setup methods; regular
`InitFromNewMap` and retail content remain pending. Headless gameplay work takes
priority, with sound and rendering integration deferred.

The first M3 service slice supplies logical sound samples from bounded, extensionless
fixture paths backed by loose 16-bit PCM WAVs. Mono/stereo data at 8–48 kHz provides
real frame counts, integer millisecond duration and 60 Hz peak amplitude queries.
Generated defaults own real PCM too. Streams close before load failures are reported;
reload, purge, rename and destruction recover exact tagged heap ledgers. Missing,
malformed, compressed or oversized input fails explicitly. The initial 256 KiB payload
limit is for authored fixtures; retail `.idwav`/ADPCM conversion, native sound-world
initialization and SPU2 playback remain pending.

Explicit headless initialization selects a monotonic fixture clock and a bounded pool
of 48 logical voices. Playback supports lead-in/loop transitions, seeking, fractional
pitch, pause/resume, stop and completion. Allocated voices pin their samples until
freed, and pool reuse/shutdown recover exact heap ledgers. Amplitude follows the active
sample's pre-gain peak envelope; no output mixer or physical device is initialized.

Typed renderer providers now bind the native screen, image, shader, cinematic and
vertex-buffer interfaces, resolving the renderer link group. They expose inactive
state and empty metadata cleanup. Initialization, display dimensions, resource loading,
draw submission and active demos fail with the method name. The 42 frontend cvars retain
native defaults and flags. No frame arenas, texture/vertex payloads or GS device are
created; meaningful world/material/model queries remain required for general map loading.

The deferred game/UI providers now close the multiplayer, leaderboard, shell and save
metadata link group. Single-player multiplayer reset/precache and empty scoreboard
cleanup are usable; match operations terminate explicitly. Offline leaderboard lifecycle
creates no online definitions. Native save descriptions retain copy/clear ownership,
while `saveGame_enable=0` is read-only and forced writes cannot enable the save manager.
Shell presentation, enumeration and retry dialogs remain unavailable. Shell providers
have compile/link acceptance; their runtime checks await the resident fixture.

The input provider now supplies the native `idUsercmdGen` interface, all 47 action
strings plus their terminator, and case-insensitive command lookup. Empty cleanup and
native invalid-index results are usable. Initialization, map preparation, inhibition,
mouse/button/key sampling and command generation fail explicitly. Controller and rumble
preferences are archived in both builds with zero defaults and ROM flags; forced writes
cannot enable their unavailable services. Physical device polling and controller mapping
remain later work. The game fixture explicitly injects synthetic commands into the
native command manager and player-physics probe.

## Core boundary

The foundation links every selected scalar idlib object directly and runs the real
`idCommonLocal::Init` / `Shutdown` from `Common.cpp`. Startup completes system, idlib,
command, cvar, filesystem, synchronous-job and offline-session stages; shutdown unwinds
only completed stages in reverse order. Startup is one-shot because Doom's static cvar
registration cannot be repeated after shutdown. Game, presentation, dialogs and saves
remain deferred capabilities with explicit failures. The filesystem still uses the
bounded loose-file fixture adapter; full campaign container initialization is pending.
The portable Common layout excludes the 2,304,000-byte Classic framebuffer. No Classic
source or desktop GPU/audio implementation is linked.

The offline session owns one local user on input device zero, with the native transient
profile's stats and achievement bits. Match parameters are copied, stale user handles
are rejected, and loading completes only on `LoadingFinished`. Online requests are
unsupported; profile persistence reports failure without discarding transient data.
The static versioned `GetGameAPI` import path now compiles, but is not invoked by the
foundation. The resident fixture invokes it during native game initialization and
executes authored scripts and a logic-only map; general campaign map loading remains pending.

The portable audio boundary preserves sample names, reference/purge flags and last-played
bookkeeping while owning loaded fixture PCM. Resource reload and generated defaults use
the same timing/amplitude queries. Logical voices require the explicit headless clock;
unconfigured device initialization still fails. Native sound-world integration and
audsrv/SPU2 output remain later work.

Backend and smoke-test diagnostics use the shared `ps2::Log` / `LogV` sink with info,
warning, error and fatal levels. It currently writes synchronously to stdout. System,
core, heap and assertion failures terminate through `FatalError` / `FatalErrorV`;
on-screen fatal reporting remains a TODO. `src/ps2/common.h` provides `PS2_Assert`,
`PS2_AssertMsg`, `ArrayLength` and portable format/cold attributes for new backend code.

The fixture adapter streams loose files through real `idFile_Permanent` objects;
explicit bulk/memory reads stay bounded to 64 KiB. It rejects parent, absolute and
other-device paths. Shared system services supply directory filters, parent creation,
write/append and ZIP timestamp packing. The one-time ROM FILEIO patch protects
getstat/dread DMA and removal fallthrough without resetting the loader's IOP. Native
rename reports `ENOSYS`; PCSX2 removal reports `ENODEV` despite deleting the file,
and the wrapper preserves that error. Foundation-level listing/write/resource APIs
remain unsupported until the campaign filesystem is linked. Unimplemented game/UI/network calls also terminate with a
diagnostic. Core `Error` is fatal; lexer fixtures that need to reject malformed input
without terminating use the engine's `LEXFL_NOERRORS` flag.

`idLib::Init` initializes strings, dictionaries, math and the generic SIMD processor.
The initial job manager creates no workers. Dependencies complete before their dependent
list executes, and the original scalar executor handles synchronization points and
completion bookkeeping. Explicit parallelism requests still run on the caller.
[CVARS.md](docs/CVARS.md) documents `jobs_numThreads`, `com_smp` and later backend controls.

Scalar compilation retains intentional double arithmetic in `Parser`, `Timer`, `Token`,
`bv/Sphere`, `geometry/RenderMatrix`, `math/MatX`, `math/Matrix`, `math/Ode` and
`math/Plane`. Those operations use software helpers on the EE. Target precision and
performance tests must precede replacing them with single-precision implementations.

## Heap and memory

Doug Lea's allocator supplies the EE's sole C/newlib arena; standard C allocation entry
points and their reentrant variants route to it. Doom's aligned/tagged APIs and all
standard, sized, nothrow and over-aligned C++ allocation forms use `ps2::heap` metadata.
The zero-initialized ledger supports registration allocations before `main`.

Each allocation stores its requested size, backing size, tag and original base pointer.
An unsized free subtracts exactly that allocation's sizes. Invalid alignment, size
arithmetic overflow and invalid tags are rejected; required allocations terminate on
failure. Alignment is at least 16 bytes, with larger powers of two supported for DMA.
The current implementation is for a single calling thread.

The shutdown tests require current requested/backing bytes and allocation count to
return to the baseline taken before initialization. The core's process-lifetime
baseline is 1,024 requested bytes in one allocation; the whole resident fixture starts
with 7,424 bytes in four allocations. Both must recover their own exact baseline.

Per-tag and total reports track current and peak requested/backing bytes. On the EE,
backing size is the allocator's usable chunk size; host tests report the reserved request.
`GetArenaStats` includes untagged C/newlib allocations and allocator overhead on the EE.
The ELF report, arena commitment, stack usage and later GS VRAM usage are separate parts
of the memory budget. Stripping symbols does not reduce loaded code/data/BSS.

## Smoke tests and diagnostics

The runner stages a unique manifest, authored fixture and matching ELF/symbols under
`build/test-results/<run-id>/`. It uses the installed PCSX2 configuration, requires
HostFs and IOP/file logging to be enabled already, and refuses to launch while another
PCSX2 session is running. It selects a separate log with `-logfile` and stops only the
process it started. It does not edit emulator settings.

```sh
python3 src/tools/scripts/run_pcsx2_test.py --scenario platform
python3 src/tools/scripts/run_pcsx2_test.py --scenario core
python3 src/tools/scripts/run_pcsx2_test.py --scenario core-missing-fixture
make test-common
make BUILD=release test-common
make test-game
make BUILD=release test-game
```

Build the corresponding probe, core or resident ELF first. A pass requires the fresh run identity,
scenario-specific stage markers and a closed structured result; process exit alone cannot
pass. A watchdog or crash diagnostic fails the run. The negative scenario requires the
core to reject a missing fixture. Tests cover heap alignment/accounting, scalar matrix
and vertex formats, lexer/string behavior and synchronous job ordering. Core services
also exercise command/cvar registration and fixture I/O.
Nine offline checks cover Common identity, local users, transient profiles/achievements,
unavailable persistence, match transitions, copied parameters, reload accounting and
sign-out/input routing, plus inactive-demo cleanup. Common probes launch a fresh process
for each of seven deliberate startup stops and fifty-nine invalid or unsupported requests;
each must match its own run
identity and expected cleanup or fatal diagnostic. The separate game matrix requires
all thirty-seven `game/` checks, three event-posting and collision traces, all 24 frame/entity/script,
command, entity-lifetime, wall-stop, falling, grounded-sliding and player-input/physics traces,
240 player-physics posture traces, 33 native-player startup/walk traces and 288 native-player
posture/state traces. Eight expected failures cover missing maps, malformed scripts,
restricted geometry/materials and player spawn/script/command/state errors. Resident images, map, flags and authored
game files are hashed in each archive; generated collision caches are hashed after completion. Seventeen audio failures cover missing
files, unloaded duration, unsupported formats, truncation, chunk bounds, payload budget
and device initialization, plus invalid clocks, voice inputs, sample lifetimes and pool
ownership. Seven audio checks cover metadata, PCM timing/amplitude, generated defaults,
deterministic playback, looping and pool reuse with exact heap recovery. Additional probes
reject process launching,
negative durations and ASE/LWO/Maya source-model imports. Language and UTC/duration
formatting are checked on the EE.
Three renderer checks cover native interface identity, empty metadata/ledger recovery,
and disabled resolution policy/cvars. Eight renderer probes reject initialization,
dimensions, drawing, image/vertex/shader loading, cinematics and demos. Three deferred
checks cover inactive multiplayer lifecycle, copied save descriptions and disabled save
policy. Six probes reject multiplayer ticks/chat/snapshots/scoreboards/mode enumeration
and forced-save manager access. Three input checks cover native action lookup, empty
cleanup/invalid-index behavior and disabled controller preferences. Eleven probes cover
input initialization, map preparation, sampling/generation/inhibition, null command
strings and forced controller/rumble settings.

This milestone preserves the loader's IOP/`host:` filesystem and initializes SIF RPC.
Physical USB/HDD bring-up, the GS/VU1 path, SPU2 audio, controller input, save games and
a custom EE exception handler are not implemented. For an emulator-reported PC, use the
archived `d3bfg_unstripped.elf` with `mips64r5900el-ps2-elf-addr2line`. The standalone
symbolization helper is in `src/tools/scripts/`.

## Local data and reference knowledge

Retail data under `gamedata/d3_bfg/` and `gamedata/d3_roe/` remains local and ignored.
The root `base/` retains tracked source-release configuration/render programs; new
retail files there are ignored. Synthetic tests use authored data and do not depend on
either game installation.

[AGENTS.md](AGENTS.md) describes working conventions. [docs/REUSE.md](docs/REUSE.md)
records source provenance and licensing. The imported allocator retains its original
public-domain notices. The standalone `symbolize.py` retains GPL v2; new backend code
uses GPL v3 or later. Quake II measurements in `.claude/rules/` remain reference evidence
until independently reproduced with this port.

## Attributions

This software is based in part on the work of the Independent JPEG Group. The
engine-modified JPEG 6 and Jean-loup Gailly/Mark Adler’s zlib 1.2.3 retain their original
notices under `src/external/`; the target builds only the audited codec source lists.

The input action table preserves id Software's native `framework/UsercmdGen.cpp` names,
order and terminator; the portable provider is new backend code.

The PS2 backend draws on Guilherme Lampert's [quake2-ps2](https://github.com/glampert/quake2-ps2).
The shared `src/ps2/common.h` assertion macros and `ArrayLength` helper are adapted
from that project. The author authorizes reuse of their code under this project's
GPL v3 or later terms. Third-party notices retain their original terms; the import
inventory is recorded in [docs/REUSE.md](docs/REUSE.md).
