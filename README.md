# Doom 3 BFG Edition for the PlayStation 2

A PlayStation 2 port of [id's Doom 3 BFG Edition](https://github.com/id-Software/DOOM-3-BFG)
using the free ps2dev SDK. The initial implementation builds a scalar EE core and an
asset-free smoke executable. Campaign initialization, map loading, rendering, audio,
input and saves are later milestones; the port is not playable yet.

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
make link-game               # resident link gate; reports the remaining unresolved symbols
make test-host               # shared heap tests with ASan/UBSan, runner tests
make smoke                   # core smoke run in PCSX2
make release smoke           # release core smoke run
make smoke-negative          # expected missing-fixture failure
make test-common             # seven partial-startup and twenty-nine expected-fatal probes
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
ELF/startup memory measurements and the remaining resident-link work.

M2b is in progress: the campaign headers now separate OpenGL state from shared
renderer/menu contracts, and initial C++20/source portability fixes are in place.
All 281 retained campaign units now compile in both configurations. The four logical-
sound units use a portable sample/voice/device boundary without DirectX SDK headers;
the default executable remains the headless core. Six native profile, user and
snapshot/compression support units have been retained for the real Common layout and
offline services. Campaign achievements are retained too; their Classic evaluation
entry is explicitly unavailable.
Game class allocation now preserves heap alignment and checks signed memory counters;
factories use explicit fatal allocation without exception handling. Shared host/EE tests
cover this allocation boundary, while actual game initialization remains a later gate.
The script compiler/program also compile without exceptions; an isolated EE probe tests
compilation, limits and explicit cleanup before fatal errors. `make test-script` runs it.
Recoverable script loading and actual interpreter/game execution remain later gates.
Portable common errors now terminate through the shared logging sink without desktop
dialogs or renderer/session calls. SWF JPEG errors explicitly release decoder/output
state; bounded input, dimensions and repeated/table-based decoding have sanitizer tests.
Filesystem/ZIP/key units now compile with bounded device paths, stdio handles, an explicit
ZIP timestamp policy and the engine's fixed key-label table. Shared directory and file services are
tested on the EE; full campaign filesystem initialization and ZIP runtime linking remain pending.
Retained menu, GUI-variable, file and model casts use checked portable hierarchy queries.
Shared host/EE tests cover null/sibling rejection, inherited/const types and separate-unit
identity; actual file objects and engine hierarchy declarations are checked on the EE.

`make link-game` attempts a real whole-object campaign link under
`build/<config>/resident/`, with its own strict support objects and no core filesystem
substitute. It currently fails on 21 unresolved symbols: four input/usercmd symbols and
seventeen JPEG/zlib symbols. JSON/text reports retain object/source hashes,
flags, requestors and the real linker exit status. Failed attempts remove stale resident
ELFs. A successful gate must retain game/class/cvar registration roots and every input
object in its map. Its eventual entry still requires M3 game-fixture startup.

Typed renderer providers now bind the native screen, image, shader, cinematic and
vertex-buffer interfaces, resolving the renderer link group. They expose inactive
state and empty metadata cleanup. Initialization, display dimensions, resource loading,
draw submission and active demos fail with the method name. The 42 frontend cvars retain
native defaults and flags. No frame arenas, texture/vertex payloads or GS device are
created; meaningful world/material/model queries remain required before M3.

The deferred game/UI providers now close the multiplayer, leaderboard, shell and save
metadata link group. Single-player multiplayer reset/precache and empty scoreboard
cleanup are usable; match operations terminate explicitly. Offline leaderboard lifecycle
creates no online definitions. Native save descriptions retain copy/clear ownership,
while `saveGame_enable=0` is read-only and forced writes cannot enable the save manager.
Shell presentation, enumeration and retry dialogs remain unavailable. Shell providers
have compile/link acceptance; their runtime checks await the resident fixture.

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
foundation. Resident game linking, interpreter execution and map loading remain pending.

The portable audio boundary preserves sample names, reference/purge flags and last-played
bookkeeping. Samples remain unloaded; resource loading, duration/amplitude queries and
device operations fail explicitly with the method name. No voices or audio device are
created. Meaningful sample timing and playback state are required before the M3 game
fixture; audsrv/SPU2 output remains later work.

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

The shutdown test requires current requested/backing bytes and allocation count to
return to the baseline taken before core initialization. A process-lifetime hash table
created before `main` remains in that baseline (1,024 requested bytes); test/core
allocations must not remain after shutdown.

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
```

Build the corresponding probe or core ELF first. A pass requires the fresh run identity,
platform/core stage markers and a closed structured result; process exit alone cannot
pass. A watchdog or crash diagnostic fails the run. The negative scenario requires the
core to reject a missing fixture. Tests cover heap alignment/accounting, scalar matrix
and vertex formats, lexer/string behavior and synchronous job ordering. Core services
also exercise command/cvar registration and fixture I/O.
Nine offline checks cover Common identity, local users, transient profiles/achievements,
unavailable persistence, match transitions, copied parameters, reload accounting and
sign-out/input routing, plus inactive-demo cleanup. Common probes launch a fresh process
for each of seven deliberate startup stops and twenty-nine invalid or unsupported requests;
each must match its own run
identity and expected cleanup or fatal diagnostic. The three audio failures cover resource
loading, duration queries and device initialization; core smoke also checks sample
metadata ownership and exact heap recovery. Additional probes reject process launching,
negative durations and ASE/LWO/Maya source-model imports. Language and UTC/duration
formatting are checked on the EE.
Three renderer checks cover native interface identity, empty metadata/ledger recovery,
and disabled resolution policy/cvars. Eight renderer probes reject initialization,
dimensions, drawing, image/vertex/shader loading, cinematics and demos. Three deferred
checks cover inactive multiplayer lifecycle, copied save descriptions and disabled save
policy. Six probes reject multiplayer ticks/chat/snapshots/scoreboards/mode enumeration
and forced-save manager access.

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

The PS2 backend draws on Guilherme Lampert's [quake2-ps2](https://github.com/glampert/quake2-ps2).
The shared `src/ps2/common.h` assertion macros and `ArrayLength` helper are adapted
from that project. The author authorizes reuse of their code under this project's
GPL v3 or later terms. Third-party notices retain their original terms; the import
inventory is recorded in [docs/REUSE.md](docs/REUSE.md).
