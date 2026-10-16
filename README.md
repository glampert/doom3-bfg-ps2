# Doom 3 BFG Edition for the PlayStation 2

A PlayStation 2 port of [id's Doom 3 BFG Edition](https://github.com/id-Software/DOOM-3-BFG)
using the free ps2dev SDK. The production executable boots the native game into a
headless or GS-rendered native console and runs the Common/game loop until `quit` or
a frame limit. The same executable can load an authored native SWF with text, timeline
animation and keyboard focus/select events. NTSC/PAL presentation is an explicit
package startup choice.
Separate native test executables exercise a scalar EE core and an authored headless
game fixture that runs native initialization, entity thinking, script events, bounded collision/physics
and a restricted native `idPlayer`. An authored map also boots through native `InitFromNewMap`
with logical world/model/PVS queries, collision, AAS and one idle native AI. Full campaign player startup, retail maps,
retail SWF/menu integration and world rendering, SPU2 output, physical input and saves
remain later milestones; the port is not playable yet.

The engine is under `src/neo/`, moved intact in commit `4e5f082`. New console code is
under `src/ps2/`, and host/target regression tests are under `src/tests/`. Upstream
changes carry `// [PS2_D3BFG]` annotations. [IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md)
contains the campaign milestones; [docs/BUILD_INVENTORY.md](docs/BUILD_INVENTORY.md)
records the explicit source dispositions and deferred replacements. Third-party
dependencies imported for the port live under `src/external/`.

[RENDERER_2D_PLAN.md](docs/RENDERER_2D_PLAN.md) inventories the Doom UI and Quake II
PS2 renderer, and proposes the next rendering work: GS primitives, a visible native
console, then the native offline main menu. It records reuse boundaries, asset and
memory requirements, and acceptance gates. The CPU-only GS device layer now draws
solid colors and RGBA textures with alpha in NTSC/PAL. It owns bounded pixel/VRAM
storage, fences dirty uploads and eviction, and streams native framebuffer captures
with checked writes and recoverable readback deadlines. Production can now call the
real native `console->Draw` after each Common frame and draw an authored native SWF
beneath it.
Debug/release NTSC/PAL console acceptance and inspected captures are recorded in
[PORT_STATUS.md](docs/PORT_STATUS.md#production-native-visible-console-2d-3).

Accepted 2D-3 supplies native display/color/quad/text adapters, owned white
and authored ASCII charset images, and production console presentation. Fractional
640×480 coordinates map to GS fixed point. Native edit-field input, command history,
console closing/reopening and capture-before-quit use the same production executable
as headless startup. `UpdateScreen` requests during a Common frame coalesce into its
final draw, without nesting frames or advancing the game clock again.

The first 2D-4 slice adds bounded native `AllocTris` collection, rotated/skewed quads
and textured triangles with per-vertex RGB/alpha. CPU spans hold at most 4,096 vertices,
6,144 indices and 128 ordered draws; a flush consumes and recycles them. Geometry is
clipped on the 640×480 canvas before fixed-point GS emission, and mixed sprite/triangle
calls preserve submission order. This uses the existing intrinsic images and restricted
constant-material profile. General material/image loading and additive vertex color
remain pending.
Debug/release NTSC/PAL captures, bounds failures and production regressions pass;
see the [indexed UI acceptance record](docs/PORT_STATUS.md#indexed-native-ui-geometry-2d-4-first-slice).

The authored 2D-4 slice now connects the native SWF loader, timeline, AVM1 interpreter and
`idFont` to this geometry path. [Authored UI data](assets/ui/README.md) supplies a
mask-free scene with transformed bitmap geometry, solid fills, ASCII text, color/alpha
animation and Tab/Enter focus/select functions. Its font uses a separate procedural
atlas with UV gutters. BSWF/font loading admits exact authored snapshots before the
legacy binary parsers run. Retail main-menu/Shell integration, general asset conversion,
masks, gradients, additive color and broader material profiles remain pending.
Debug/release NTSC/PAL acceptance, native captures and stable reload/shutdown ledgers
are recorded in [PORT_STATUS](docs/PORT_STATUS.md#authored-native-swf-and-fonts-2d-4).

## Build

Install ps2dev with `mips64r5900el-ps2-elf-g++` and ps2sdk. The defaults are
`PS2DEV=$HOME/ps2dev` and `PS2SDK=$PS2DEV/ps2sdk`. This checkout was tested with GCC 15.2.
The Doom Makefile uses explicit source lists in `config/sources.mk`; the unchanged
Quake II reference is retained in `docs/reference/quake2.Makefile`.

```sh
make                         # production d3bfg.elf, debug
make release                 # production d3bfg.elf, release
make headless                # package production with authored console boot data
make run-headless            # run the headless package in PCSX2, until quit
make console                 # package the same production ELF with visible NTSC console
make CONSOLE_STANDARD=pal console # select PAL presentation
make run-console             # run the visible console package in PCSX2
make test-console            # native console/edit/history/capture/quit checks in NTSC/PAL
make BUILD=release test-console # same production presentation checks without assertions
make ui                      # package the authored native SWF with visible NTSC presentation
make CONSOLE_STANDARD=pal ui  # select PAL for the authored UI package
make run-ui                  # run the authored native SWF in PCSX2
make test-ui                 # native SWF/font/timeline/focus/reload/capture checks in NTSC/PAL
make BUILD=release test-ui    # same authored UI checks without assertions
make test-headless           # 120 frames, live console commands and shutdown in PCSX2
make BUILD=release test-headless # same production checks without assertions
make test-renderer           # NTSC/PAL sprites/indexed UI, native pixels, reload and failure probes
make BUILD=release test-renderer # same device checks without assertions
make run-renderer            # native capture plus a 60-second inspection hold; F8 also saves a capture
make platform-probe          # SDK/timer/alignment probe
make headless-core           # scalar Doom foundation and integration tests
make compile-core            # 52 scalar idlib units and 10 framework/session support units
make compile-game            # all 281 retained campaign units; compile gate only
make link-game               # production campaign link, registrations and test-isolation gate
make headless-game           # native campaign test executable
make test-game               # boot/player/ticks/reloads and ten expected failures in PCSX2
make BUILD=release test-game  # same game checks with assertions disabled
make test-map                # native authored map/world/PVS/AAS/AI startup and ticks
make BUILD=release test-map   # same map checks with assertions disabled
make test-host               # shared heap tests with ASan/UBSan, runner tests
make smoke                   # core smoke run in PCSX2
make BUILD=release smoke     # release core smoke run
make smoke-negative          # expected missing-fixture failure
make test-common             # seven partial-startup and sixty-two expected-fatal probes
make compiledb               # compile_commands.json from real Make rules
make resource-unpack         # standalone C++20 host resource reader/extractor
```

The [host resource unpacker](src/tools/resource_unpack/README.md) expands native BFG
`.resources` containers into ordinary files without starting the engine. It validates
tables and paths, streams payloads and refuses existing outputs by default. For example:

```sh
build/tools/resource_unpack --output build/unpacked-bfg \
    gamedata/d3_bfg/_common.resources gamedata/d3_bfg/_ordered.resources
```

Extracted BSWF/BIMAGE files keep their native formats. Retail extraction belongs in
ignored local directories; it is separate from authored UI assets and target startup.

Production artifacts are `build/<config>/d3bfg.elf`, `d3bfg_unstripped.elf`,
`d3bfg.map` and `report.{json,txt}`. This executable links the full retained campaign
without sources or headers under `src/tests/`, fixture symbols or linker GC. Its
`main()` preserves the loader's IOP and initializes Common, native declarations,
key/console events, the static game API and `idGameLocal::Init`, plus CPU render-world
and silent sound bookkeeping. Common processes console events, commands, offline
services and native `RunFrame` on the calling thread. Headless mode retains approximately
60 Hz idle pacing; visible mode presents once per completed Common frame and uses GS
vsync without an additional sleep. The game remains in `NOMAP`, whose native frame
returns without advancing map simulation. Retail main-menu SWF, map transitions and physical
input remain pending. Startup checks `base/default.cfg` for existence and optionally executes
`base/d3bfg.cfg`; desktop bindings are not executed. `quit` / `exit` request ordered
game/world/console shutdown followed by Common shutdown at the frame boundary.

`make headless` packages the production ELF and small authored boot data from
`assets/headless/` under `build/<config>/headless/`. It preserves an existing packaged
`base/d3bfg.cfg` and leaves local `base/` assets alone. The package root has
`presentation.cfg`, read before Common initialization: `headless`, `ntsc` or `pal`,
with an optional final newline. A missing file selects headless; invalid content is
fatal. The current PCSX2 HostFs maps a missing file to `EIO`, so policy open failure
with `ENOENT` or `EIO` warns and defaults to headless; other errors remain fatal.
Staging always replaces this policy, so repackaging cannot retain a stale mode.
`make run` / `make run-headless` launch this package. The native console is active with stdout output and no drawing.
Append newline-terminated commands to its `console.txt` from another terminal:

```sh
printf 'ps2_status\nhistory\n' >> build/debug/headless/console.txt
printf 'quit\n' >> build/debug/headless/console.txt
```

Input is read from the beginning on each boot; clear old commands before restarting.
The backend polls at 100 ms intervals, reads at most 1 KiB per poll, and submits complete
lines through native `SE_CONSOLE` events. Lines of at most 1,023 bytes are accepted;
oversized lines and embedded control characters are discarded through the newline.
History stays in memory. Set `ps2_maxFrames 120` in the packaged `base/d3bfg.cfg` for a
bounded run; its default `0` keeps running until a quit command. `ps2_status` reports
completed frames, native `NOMAP`/console state and the tagged heap ledger.

`make console` stages the same ELF and authored boot data under
`build/<config>/console/`; `CONSOLE_STANDARD` defaults to `ntsc`. It opens the native
half-height console over a dark background. Use its `console.txt` for commands, or
send ASCII and named keys to the native edit field:

```sh
printf 'ps2_consoleText "echo hello"\nps2_consoleKey enter\n' >> build/debug/console/console.txt
printf 'ps2_consoleKey up\nps2_consoleKey enter\n' >> build/debug/console/console.txt
printf 'ps2_capture console.tga\nquit\n' >> build/debug/console/console.txt
```

`ps2_consoleText` requires an active console and one quoted argument of at most 255
printable ASCII characters, excluding grave and tilde. `ps2_consoleKey` accepts
`enter`, `up`, `down`, `left`, `right`, `toggle`, `escape`, `home`, `end`, `delete` and
`backspace`. These deliver native character/key events; physical keyboard/pad polling
is not enabled. Native Enter adds to RAM history; persistent history writes remain
disabled. Host `ps2_` control messages do not replace edit-field history.
`ps2_capture` accepts one pending ASCII alphanumeric/dash/underscore basename ending
in `.tga` (at most 63 bytes including the suffix). Quote names containing a dash,
for example `ps2_capture "console-edit.tga"`, because the native command lexer splits
unquoted dashes into separate tokens. It writes the next completed frame to the
package root. A capture queued with `quit` completes after the final native
draw and before UI/device shutdown. Visible `ps2_status` also reports presentation
and redraw-request counts. These three controls are available only in visible mode;
`ps2_maxFrames` and the ordinary command stream work in either mode.

`make ui` stages the same production ELF with the resources from `assets/ui/base/`
under `build/<config>/ui/`. It uses `CONSOLE_STANDARD=ntsc` or `pal` and adds
`ps2_ui load` to a newly created packaged `base/d3bfg.cfg`. Existing packaged settings
are preserved; append that command manually if an older configuration does not load
the scene. The native game remains in `NOMAP` while the authored movie runs.

```sh
printf 'ps2_uiKey tab\nps2_uiKey enter\nps2_ui status\n' >> build/debug/ui/console.txt
printf 'ps2_capture "ui-selected.tga"\n' >> build/debug/ui/console.txt
printf 'ps2_consoleKey toggle\n' >> build/debug/ui/console.txt
```

`ps2_ui` takes one of `load`, `unload`, `reload`, `status`, `pause` or `resume`.
Loading closes the console; unloading reopens it. Reloading resets the authored
focus and selection state. `ps2_uiKey tab` and `ps2_uiKey enter` deliver native key
events to the loaded movie while the console is closed. The authored AVM1 functions
move focus, select an item and update the status text. Native console input has
priority: `ps2_consoleKey toggle` opens it over the movie, and toggling again restores
movie input. UI commands are visible-mode controls registered as ordinary system
commands; no physical input device is enabled. `ps2_capture` and queued final capture
before `quit` work with either console or movie drawing.

Native tests use `d3bfg_tests.elf`, matching `d3bfg_tests_unstripped.elf` and
`d3bfg_tests.map`. `make headless-core` writes `build/<config>/tests/core/`,
`make platform-probe` writes `build/<config>/tests/platform/`, and
`make headless-game` writes `build/<config>/tests/game/`. The isolated compiler probe
uses `build/<config>-script/tests/script/`. Core/platform/script reports are named
`build-report.{json,txt}`; campaign test reports are `report.{json,txt}`. All smoke
commands select these test images explicitly. Campaign tests have their own object
and flag trees with `PS2_D3BFG_TESTS=1`; production uses `0`. This is the only test
enable define. Make selects the core stage or SDK-only skip provider through source
lists; `CORE_BOOT` remains a Make selector, not a compiler define.

Reports distinguish fixed ELF load-segment residency from runtime allocation.
Flag/compiler/source-list stamps invalidate affected objects automatically. Debug
uses `-O2` with asserts; release uses `-O3` without debug-only checks. Explicit cold
backend code uses `-Os`.

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
the default executable runs the production headless game/console loop. Six native profile, user and
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
tested on the EE; native loose-file startup and ZIP codec linking pass, while retail
archive/game-resource use remains pending.
Retained menu, GUI-variable, file and model casts use checked portable hierarchy queries.
Shared host/EE tests cover null/sibling rejection, inherited/const types and separate-unit
identity; actual file objects and engine hierarchy declarations are checked on the EE.

`make link-game` performs the production whole-object campaign link under
`build/<config>/`, with strict support objects and the native filesystem. All 343 inputs
must link without unresolved symbols or duplicate definitions. JSON/text reports
retain object/source hashes, flags and the real linker status, verify registration
roots and every map input, and reject test sources, transitive test headers and
fixture symbols. Failed attempts remove stale images. `make headless-game` separately
links the 345-input campaign test image with explicit authored-fixture selection.

`make test-game` boots native `idGameLocal`, parses a fixed floor/wall/low-roof/step worldspawn `.map` through
the native loose-file filesystem, and spawns a logic entity through the real class
factory. Each of three map reloads runs 331 native `RunFrame` calls. The first eight
check logic-entity Think and script increments scheduled through `sys.waitFrame`;
frames 9–88 exercise native player-physics posture changes, then frames 89–99 tick a
real `idPlayer` in client slot zero through the native command/Think pipeline.
Frames 100–195 add two jumps, held-jump suppression/rearming, crouch walking and
eye-height interpolation, with authored idle/air/crouch actor-state transitions.
Frames 196–275 place the same player in a separate ceiling lane, then use native
movement to test a standing block, crouched passage, blocked crouch release and
standing restoration after the full clip shape clears the roof.
Frames 276–331 place the same player in a separate step lane. With the native step
limit at 16, forward movement climbs a 12-unit platform without jumping and stops
against the following 24-unit rise. Signed step height, ground contacts, commands,
release stopping, eye interpolation and the continuing idle script are checked.
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
with the expected diagnostic in debug and release. Changed roof or step planes also fail
the exact five-brush geometry gate before collision loading.
This is partial M3 game acceptance. That original fixture keeps null render/sound worlds
and separate bounded map/player setup. `make test-map` now exercises native
`InitFromNewMap` with an authored floor, two logical areas and a portal, two static
CPU models, two AAS cells and one native `idAI` with a collision box and actor script.
It checks world/model traces, portal close/reopen PVS, collision, walk routing and
actor-bound reachability, then runs 60 native frames across each of three reloads.
Warm map shutdown and full teardown recover exact ledgers. The empty native sound
world supplies startup bookkeeping with `s_noSound=1`; no playback or drawing is requested.
The AI is unanimated and idle, with `neverDormant` for the playerless run. Common map
transitions, normal campaign player startup, animated AI navigation/combat and retail
content remain pending. Headless gameplay work takes priority.

The first M3 service slice supplies logical sound samples from bounded, extensionless
fixture paths backed by loose 16-bit PCM WAVs. Mono/stereo data at 8–48 kHz provides
real frame counts, integer millisecond duration and 60 Hz peak amplitude queries.
Generated defaults own real PCM too. Streams close before load failures are reported;
reload, purge, rename and destruction recover exact tagged heap ledgers. Missing,
malformed, compressed or oversized input fails explicitly. The initial 256 KiB payload
limit is for authored fixtures; retail `.idwav`/ADPCM conversion, populated sound-world/channel integration
and SPU2 playback remain pending. The native map mode uses an empty sound world only.

Explicit headless initialization selects a monotonic fixture clock and a bounded pool
of 48 logical voices. Playback supports lead-in/loop transitions, seeking, fractional
pitch, pause/resume, stop and completion. Allocated voices pin their samples until
freed, and pool reuse/shutdown recover exact heap ledgers. Amplitude follows the active
sample's pre-gain peak envelope; no output mixer or physical device is initialized.

Typed renderer providers now bind the native screen, image, shader, cinematic and
vertex-buffer interfaces, resolving the renderer link group. They expose inactive
state and empty metadata cleanup until explicit native 2D activation. The 2D profile
supports display metrics, color/state, fills, constant-material quads, bounded indexed
triangles with per-vertex RGB/alpha and authored ASCII text; unsupported state,
geometry, images, shaders and demos fail explicitly.
The 42 frontend cvars retain native defaults and flags. Production activates the device
and intrinsic images only when its startup policy requests visible presentation. The
authored SWF uses native material declarations, font metrics and timeline/scripts;
general SWF assets, retail menu integration and world drawing remain pending.

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
all forty-nine `game/` checks, three event-posting and collision traces, all 24 frame/entity/script,
command, entity-lifetime, wall-stop, falling, grounded-sliding and player-input/physics traces,
240 player-physics posture traces, 33 native-player startup/walk traces and 288 native-player
posture/state traces, plus three headroom-world traces and 240 ceiling-lane traces,
and three step-world traces and 168 step-lane traces.
Ten expected failures cover missing maps, malformed scripts, restricted floor/roof/step
geometry/materials and player spawn/script/command/state errors. Resident images, map, flags and authored
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
archived `d3bfg_tests_unstripped.elf` (or production `d3bfg_unstripped.elf`) with `mips64r5900el-ps2-elf-addr2line`. The standalone
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

The standalone host resource unpacker follows id Software's
`framework/File_Resource.{h,cpp}` and `File.cpp` binary layouts and lookup order.
Its bounded table reader, streaming output and filesystem validation are new host code.

The PS2 backend draws on Guilherme Lampert's [quake2-ps2](https://github.com/glampert/quake2-ps2).
The shared `src/ps2/common.h` assertion macros and `ArrayLength` helper are adapted
from that project. The author authorizes reuse of their code under this project's
GPL v3 or later terms. Third-party notices retain their original terms; the import
inventory is recorded in [docs/REUSE.md](docs/REUSE.md).

The first GS device layer adapts its video-mode, GS, GIF writer, command-chain and
frame code from reference commit `e64493c1bbadd84b2ab5d52a128ef25905236510`. Doom owns
the command storage and adds release-safe packet bounds and hardware timeouts; no
Quake game artwork, palette, model cache or VU programs are imported.
Texture metadata, residency fences and strip capture also adapt that reference's
mechanisms, with Doom-owned pixels, conservative RGBA pages and checked recovery.
Native character layout and color-escape helpers follow id Software's
`renderer/RenderSystem.cpp`. The bootstrap ASCII bitmap charset is authored for this port;
it contains no retail or Quake artwork and uses the project's GPL v3 or later license.
