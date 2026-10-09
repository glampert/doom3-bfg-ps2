# EE build inventory

The build uses explicit lists in [`config/sources.mk`](../config/sources.mk).
[`config/source_inventory.json`](../config/source_inventory.json) records the disposition
of every shipped BFG C/C++ unit. `make inventory` fails when a source is added, removed,
duplicated, left unclassified, or enabled despite an exclusion. Source discovery is used
for auditing only; it never selects files for compilation.

## Reviewed upstream projects

| Project under `src/neo/` | Source units |
| --- | ---: |
| `idlib.vcxproj` | 55 |
| `doomexe.vcxproj` | 199 |
| `game-d3xp.vcxproj` | 130 |
| `external.vcxproj` | 56 |
| Total project units | 440 |

There are 458 shipped C/C++ source files under `src/neo/`: the 440 project units plus
18 source files outside those manifests. The obsolete `game.vcxproj` names the absent
`game/` tree and is excluded from this audit. `d3xp/` is the BFG campaign game source.
The Windows project spells `renderer/OpenGL/gl_Image.cpp` as `gl_image.cpp`; the audit
matches project paths without case and records the actual on-disk filename so the
inventory works on hosts with either filesystem behavior.

Third-party dependencies imported for the port live under `src/external/`.
The current core uses `src/external/dlmalloc/`; its source is explicitly listed
in `CORE_C_SRC`, and the heap bridge imports its header through a system include path.

## Source dispositions

| Disposition | Units | EE policy |
| --- | ---: | --- |
| Campaign runtime | 281 | Debug/release compile gates pass; resident linking and runtime adaptation remain required |
| PS2 replacement | 62 | Desktop platform, GPU/device, input and BFG service implementations need adapters |
| Deferred runtime | 33 | BFG shell, multiplayer, online services and demo paths; callers still need explicit adapters |
| Optional vendor | 56 | zlib/JPEG; include only when a format, license and target configuration are audited |
| Host conversion | 5 | Desktop model import and texture encoding candidates |
| PCH excluded | 3 | Visual Studio precompiled-header translation units |
| Outside project manifests | 18 | Kept for inspection; never silently enabled |

These are source inclusion decisions, not claims that replacement APIs exist. Essential
HUD/PDA menus, GUI/SWF, campaign physics/scripts/AI, collision, renderer frontend and
logical sound remain in the intended runtime list. Original multiplayer and BFG launcher
files are excluded; shared callers will be adapted before the campaign resident link.
`doomclassic/` is excluded as an entire tree from every new target and remains pending
its separate deletion gate. Retail assets do not participate in these build targets.

## Build gates and artifacts

`make` / `make release` link the asset-free **headless core** into
`build/<config>/d3bfg.elf`. `make platform-probe` selects the separate SDK-only probe.
The program and build report identify the milestone explicitly. The core initializes
Doom's retained foundation services; it does not initialize the campaign game.

`make compile-core` compiles the scalar idlib subset and the initial command/CVar/file
foundation. Its upstream list contains 52 idlib units: all 55 from `idlib.vcxproj` except
`Simd_SSE.cpp`, `win_thread.cpp`, and the precompiled-header placeholder. The root build
also declares ten framework/session support units and their heap/platform support separately.
`Common.cpp` now provides the real Common constructor and staged Init/Shutdown. Native
`PlayerProfile.cpp`, `sys_localuser.cpp` and `sys_signin.cpp` supply transient offline
profiles and lookup helpers. Common embeds snapshot values, so `Snapshot.cpp`,
`LightweightCompression.cpp` and `Snapshot_Jobs.cpp` retain their constructors and whole-
object link dependencies. This does not enable networking. These six support units moved
from PS2 replacement to campaign runtime; no previously selected runtime unit was removed.
`build/<config>/libd3bfg_core.a` is a compilation artifact, not a boot proof.
It also includes the shared logger, which both the core adapters and SDK-only probe use.
The game class allocator is linked into the core for focused adapter tests; the
campaign's `idClass` hierarchy and `idGameLocal` are not yet part of that executable.

`make headless-core` explicitly selects the same core bootstrap/test sources and links
all core objects directly, without garbage collection. Debug/release core and expected
missing-fixture runs have passed in PCSX2; see [PORT_STATUS.md](PORT_STATUS.md).

`make compile-game` independently compiles all 281 intended runtime units with the
campaign header boundary. Both configurations pass. It does not select the reduced
core precompiled header or establish a resident game link; replacement services and
registrations still need that separate gate.

Each successful ELF link writes:

- `d3bfg_unstripped.elf`, the matching symbols for crash analysis;
- `d3bfg.elf`, stripped by default (`STRIP_ELF=0` copies the unstripped artifact);
- `d3bfg.map`, a linker map without garbage collection;
- `build-report.json` and `build-report.txt`, compiler version, source/config identity,
  source hashes, ELF hash, ELF load-segment memory total, section sizes and largest symbols.

The load-segment total measures fixed ELF residency. Runtime heap allocations and
thread stacks need separate measurements before drawing a total EE memory budget.

Debug uses `-O2` and symbols; release uses `-O3` and no debug symbols. Explicit cold
backend sources use `-Os` in both ordinary core and campaign-adapter object trees.
Both use C++20 with exceptions, RTTI and thread-safe local
static initialization disabled. New sources use the full GCC warning set and `-Werror`;
upstream sources keep `-Wall -Wextra` visible and use single-precision literals to avoid
EE software-double helpers. Explicit upstream object groups suppress the observed
`unknown-pragmas`, `unused-parameter`, `deprecated-copy`, `class-memaccess`,
`ignored-qualifiers` and `write-strings` diagnostics from legacy header/interface styles.
Format, array-bound, uninitialized-value and nonvirtual-destructor diagnostics remain
visible. The checked backend bridge treats upstream `neo` contracts as system headers;
new backend/test headers remain checked. SDK and dlmalloc headers are system headers.
No project-wide `-w`, VU dependency or retail-data
dependency is introduced by this foundation.

Compiler/flag/source-list stamps rebuild affected object groups when configuration
changes; changing flags no longer requires manually deleting the object tree. Header
dependencies use `-MD -MP` so changes to system-treated SDK/vendor/engine bridge
headers also invalidate objects. `make compiledb` dry-runs the real rules, checks Make's exit
status, and writes argument-array entries only after a successful dry run. An existing
database survives a failed command. Core and campaign objects live in separate trees
because their precompiled-header boundaries differ.

The verbatim Quake II Makefile remains at
[`docs/reference/quake2.Makefile`](reference/quake2.Makefile) for inspection. The active
Makefile borrows its compiler/configuration and warning policies, but has independent
Doom source lists and no dependency on another checkout.


## Isolated script compiler probe

`make script-probe` builds the real `Script_Compiler.cpp` and `Script_Program.cpp`
against foundation services, into `build/<config>-script/`. `make test-script` runs
its positive/expected-fatal PCSX2 matrix; select release with `BUILD=release`.
The probe provides an explicitly empty native-event registry and can omit the retail
script-defines include while exercising authored include files. It does not run the
interpreter or `idGameLocal`. Only this isolated probe uses function/data sections and
linker garbage collection to exclude unrelated game/save methods. The resident campaign
link must still supply those services and retain all required registrations.

Probe/test and compiler source groups remain explicit and audited. Build reports include
its compiler flags and source identity; the compile database adds the probe dry run while
preserving the ordinary core/campaign flags for sources shared with those targets.

## Checked type-query tests

The regular core directly links three additional test units for portable type queries.
Shared probes build on host and EE; a separate strict EE unit includes full campaign
declarations with `ID_PS2_CORE` undefined while linking only inline hierarchy queries
and real foundation file objects. It does not link a menu/model runtime or discard
selected core objects. These test sources remain explicit in `CORE_BOOT_CXX_SRC`.

The campaign manifest retains 281 units; all compile in both configurations.
[PORT_STATUS.md](PORT_STATUS.md) records the sound boundary and resident-link work.

## Campaign diagnostics and JPEG adapter

`GAME_BACKEND_CXX_SRC` lists required campaign backend objects. `compile-game`
compiles them with strict backend warnings and their own flag stamp, alongside the
281-unit campaign manifest. The adapters supply bounded SWF JPEG decoding, lifecycle
tracking, offline session services and the portable audio boundary. Lifecycle, offline
session and audio support also link into the foundation; the JPEG decoder remains
campaign-only and has host runtime acceptance so far.

`JPEG_TEST_CXX_SRC` is an explicit host-only list of the shipped legacy JPEG sources.
It does not change their runtime dispositions or import a new dependency. Host codec
objects retain visible legacy warnings without `-Werror`; Clang's removed `register`
and writable-string diagnostics are suppressed only there. The new decoder/test units
retain strict warnings and ASan/UBSan. The source audit allows this host fixture list
without allowing excluded vendor/editor code into target groups.

The codec's target dependency still needs selection/import under `src/external/` and
target decode/runtime acceptance before a resident game link. A tagged fix bounds the
shipped encoder's Huffman-value copy; the authored fixture generator exposed its
256-byte read from 12/162-byte standard tables under ASan.

## Portable filesystem services

`CORE_BACKEND_CXX_SRC` includes strict shared `filesystem.cpp` and the engine-facing
`sys_filesystem.cpp`; both cold units use `-Os`. The foundation opens real permanent
streams, while explicit whole-file fixture reads keep their 64 KiB limit. The full
campaign `FileSystem.cpp` remains a separately compiled unit, not an initialized or
linked filesystem claim. It uses checked stdio handles, paths and directory services;
`Zip.cpp` uses a documented timestamp policy and `KeyInput.cpp` keeps the engine label table.
Removed `std::auto_ptr` owners use `std::unique_ptr`.

Core links ps2sdk's `libpatches` for one-time ROM FILEIO repair; the platform-only
probe does not. No IOP reset or storage-driver import is added. Failed patching disables
removal; native rename errors remain visible. The host filesystem harness uses real
isolated files and directories under strict warnings and ASan/UBSan. It tests signed
length overflow using a sparse file, cursor preservation, short reads, suffix filters,
symlink exclusion, path bounds and error returns. Source-list audit and the compile
database include the new EE units; host fixtures are not campaign runtime acceptance.

## Staged Common and offline services

`Common.cpp` uses the full campaign header with `PS2_D3BFG_FOUNDATION=1` in the core.
That define selects foundation method providers, without changing the portable Common
layout. `common_foundation.cpp` supplies diagnostics and explicit unavailable methods
on the real class. `offline_session.cpp`, native profile/user/snapshot support and the
offline smoke unit also compile against full headers with `ID_PS2_CORE` undefined.
New backend objects retain strict warnings; upstream units retain the documented legacy
policy. Header choices are recorded in flag stamps and the real compilation database.

The ordinary foundation links every selected object without GC. The portable Common
class omits Classic material/framebuffer storage and creates no game worker. Its static
`GetGameAPI` import compiles separately in the campaign object, but the foundation has
no game object or game initialization stage. Full container filesystem, render/sound,
UI/dialog and save services still need their resident-link implementations.

`make test-common` runs seven partial-startup cleanup probes and forty expected-fatal
capability/precondition probes, each in a fresh process. `BUILD=release` selects the
assertions-disabled matrix. The regular core smoke also checks copied match parameters,
explicit loading completion, transient stats/achievement bits, user-handle invalidation
and stable accounting over three reloads after warming native string-pool capacity.


## Logical sound boundary

The retained `snd_emitter.cpp`, `snd_shader.cpp`, `snd_system.cpp` and `snd_world.cpp`
compile through `ps2/audio/sound_backend.h`, selected by `snd_local.h` on portable
builds. Stream buffer ownership uses the portable sample/voice types too. The desktop
branch keeps its original SDK headers and XA2 classes; the optional `GetIXAudio2`
query returns null on portable builds. Native logical sound bodies remain selected.

`ps2/audio/sound_backend.cpp` is in both core and campaign backend lists, uses full
campaign headers and strict warnings, and is cold (`-Os`). It supplies unloaded sample
metadata and explicit method-named fatal failures for unavailable resource, timing,
amplitude and device operations. It never reports a fake loaded sample or successful
playback. The foundation directly links this boundary and its sample-ownership checks;
it does not link or initialize native sound worlds. Native `SoundVoice.cpp`, wave-file
loading and XA2 implementations remain replacement sources, pending real sample/voice
semantics before the M3 fixture. No vendor disposition changes or audio cvars are added.

Core smoke requires the sample metadata/ledger marker. The three new expected-fatal
probes cover resource loading, duration queries and device initialization in fresh
processes with assertions enabled and disabled. See the latest acceptance evidence in
[PORT_STATUS.md](PORT_STATUS.md).


## Resident campaign link gate

`make link-game` / `make BUILD=release link-game` attempt the actual EE link with
all 281 campaign objects, fifteen campaign backend objects, eight strict resident support
objects and dlmalloc: 305 direct inputs, without GC or archives hiding undefined
references. `RESIDENT_SUPPORT_CXX_SRC` uses its own full-header object tree and flag
stamp. It excludes `core.cpp`'s fixture filesystem and `common_foundation.cpp`'s method
substitutes. `SCRIPT_PROBE=1 link-game` is rejected explicitly.

`link_resident.py` records the compiler/link command, flags, source/dependency/object
hashes, actual linker status, unique demangled symbols and requesting objects in
`build/<config>/resident/report.{json,txt}`, beside the raw log, response file and map.
The current gate fails with 17 codec symbols in both configurations; no resident ELF is
emitted. Compiler nonzero exits stay failures even without a recognized diagnostic.
Successful links also require a MIPS executable/load segment, every input in the map,
static game interfaces, three native class registration roots and the static CVar
registry. Stale ELF/report files are cleared before attempts. The resident entry is
explicitly unavailable until M3 startup; a future linked probe is not game boot.

Native `d3xp/Achievements.cpp` moves from deferred to campaign runtime. Its campaign
logic and cvars remain intact; portable builds gate the Classic header and terminate
Classic evaluation. Every original campaign unit stays selected. Achievement-manager
runtime execution remains pending the resident link and actual player fixture.

The shared `common_campaign.cpp` supplies offline Common queries, native snapshot/
interpolation/usercmd state reset, inactive-demo cleanup, and explicit failures for
network/demo/menu capabilities. State reset is compile-accepted; populated snapshot
cleanup still requires the game fixture. Foundation tests cover inactive demos and
offline query/ledger stability. New `sys_services.cpp` supplies language identifiers,
nonnegative duration formatting and UTC formatting of supplied timestamps; it does not
validate a wall clock or storage timestamps. Unavailable OS services fail by method.
`model_import.cpp` rejects the three host source formats instead of enabling their
conversion parsers or pretending resources loaded. Those source dispositions stay
unchanged. All three backend units compile strictly and cold in core and campaign
object trees. No vendor/data import is added.

## Renderer interface stubs

Four explicit `RENDER_STUB_CXX_SRC` units also appear in the core/campaign backend
lists and cold policy. They compile against full native headers under strict warnings;
no upstream renderer sources or layouts change. `null_render.cpp` binds `renderSystem`
to the native `tr`, provides zero device/backend state, the complete native render-system
vtable, debug entry points and demo boundaries. `image_stubs.cpp` binds the native
image/shader/resolution/cinematic interfaces; `vertex_stubs.cpp` supplies empty native
buffer construction/destruction and cache cleanup. `render_cvars.cpp` preserves the
42 referenced frontend controls with native defaults, flags, bounds and completions.

Only inactive queries, unloaded image metadata, empty-resource cleanup and a disabled
full-resolution policy are supported. Display dimensions, initialization, loading,
allocation, shader/timing queries and draw submission fail explicitly. Demo writes
can return when no recorder exists; active demo operations fail. These providers close
all 87 renderer symbols and two cinematic sound-window symbols from the preceding link.
They do not implement the meaningful logical renderer required by M3, allocate stock
frame/vertex arenas, or start a graphics device.

The renderer slice brought the core to 18 backend units and ten smoke units. Three
required renderer markers verify native binding/inactive state, metadata scope cleanup
and ledger recovery, and
resolution/cvar policy. Eight new expected-fatal probes run with assertions on and off.
That slice retained 301 resident inputs and left 82 unresolved symbols in deferred
shell/network/save (61), codecs (17), and input (4). Source dispositions stayed unchanged.

## Deferred game and shell boundary

Three cold, strict campaign providers close the 61-symbol deferred group. Shared
`multiplayer_stub.cpp` uses the native class layout, initializes every state field
without accessing other globals before main, and permits offline reset/precache, empty
scoreboard cleanup and inactive queries. Match ticks, snapshots, chat, team/flag
operations, scoreboard activation and mode enumeration fail by native method name.
Offline leaderboard init/shutdown create no online definitions or requests.

Shared `save_metadata.cpp` implements native description construction/clear; normal
copy assignment remains upstream. `saveGame_enable` defaults to zero with BOOL/ROM
flags. A forced internal write cannot enable the session save manager. Retry dialogs
fail explicitly; no save manager, file pipeline, storage or enumeration is constructed.
Campaign-only `shell_stub.cpp` supplies the native shell vtable and required save/load
screen enumeration calls. Empty shell cleanup releases list metadata; active resources,
presentation, continuation, save enumeration and leaderboard callbacks fail explicitly.
Shell and retry-dialog paths have compile/link acceptance, pending resident runtime tests.

The deferred game/UI slice brought the core to 20 backend and eleven smoke units.
Three required `deferred/` markers cover inactive multiplayer lifecycle/ledger recovery, independently copied save
descriptions and repeated cleanup after warming native string pools, and read-only save
policy. Six fresh-process probes cover match ticks, variadic chat, snapshot writes,
scoreboard activation, mode enumeration and forced save enablement, with assertions on
and off. That slice retained 304 resident inputs and left input (4) and codecs (17)
unresolved. No upstream source dispositions changed or game initialization was claimed.


## Input/usercmd interface provider

Shared, cold `ps2/input/usercmd_stub.cpp` compiles strictly with the native headers in
core and campaign object trees. It binds `usercmdGen` to a typed, allocation-free
implementation and preserves the exported native `userCmdStrings` table: 47 commands
and a final sentinel, with exactly `UB_MAX_BUTTONS` entries for SWF's indexed traversal.
Case-insensitive lookup returns the native enum for exact names or `UB_NONE` for an
unrecognized command. Null command strings fail explicitly in both configurations.

Clear, ClearAngles and Shutdown operate on an empty provider; invalid ButtonState and
KeyState indices retain the native minus-one result. Valid live queries, initialization,
map preparation, inhibition, mouse state and command building/access fail by method name.
No neutral commands or connected device are fabricated. `in_useJoystick` and
`in_joystickRumble` retain native archive flags, add ROM and default to zero in both
builds. Forced controller writes cannot enable sampling; forced rumble writes still
encounter the existing unavailable `Sys_SetRumble` provider. No SDK input module, event
poller, device buffer or additional upstream unit is enabled. Physical controls and
explicit synthetic command injection remain required for their later runtime gates.

The core now contains 21 backend and twelve smoke units. Three required `input/`
markers verify native table order/extent and independent command names/prefix handling,
repeated empty cleanup and invalid indices with exact ledger recovery, and disabled
controller cvar policy. Eleven fresh-process probes cover every unavailable input method,
null command validation and forced controller/rumble values with asserts enabled and
disabled. All 305 resident inputs remain retained; only 17 codec symbols are unresolved.
Source dispositions stay unchanged; source audit and the compile database cover the new
strict full-header units. The input link group is closed; physical input acceptance remains a later gate.

The negative-probe classifier matches only the first fatal line and requires a name
boundary after the expected text. A new host regression rejects InitForNewMap under
an Init probe and a later matching message after an unrelated failure. The current
host suite contains 62 Python regressions plus the shared ASan/UBSan fixtures.
