# EE build inventory

The build uses explicit lists in [`config/sources.mk`](../config/sources.mk).
[`config/source_inventory.json`](../config/source_inventory.json) records the disposition
of every shipped BFG C/C++ unit. `make inventory` fails when a source is added, removed,
duplicated, left unclassified, or enabled despite an exclusion. Source discovery is used
for auditing only; it never selects files for compilation.

Current source audit, reviewed **2026-10-19**: 458 upstream units, 282 retained
campaign units, 48 core backend sources, 34 resident support sources and 399 compile
database entries. The last runtime acceptance is the 2026-10-18 convex-mask slice in
[PORT_STATUS.md](PORT_STATUS.md#native-convex-swf-masks-2d-6-second-slice). Later
sections retain dated source/size snapshots; their earlier counts are historical.

## Reviewed upstream projects

| Project under `src/neo/` | Source units |
| --- | ---: |
| `idlib.vcxproj` | 55 |
| `doomexe.vcxproj` | 199 |
| `game-d3xp.vcxproj` | 130 |
| `external.vcxproj` | 56 |
| Total project units | 440 |

There are 458 shipped C/C++ source files across `src/neo/` and the relocated codec
trees in `src/external/`: the 440 project units plus 18 files outside those manifests.
The audit maps the original codec paths in the unchanged Windows reference projects
through `source_relocations`; compilation uses only current explicit paths. The obsolete `game.vcxproj` names the absent
`game/` tree and is excluded from this audit. `d3xp/` is the BFG campaign game source.
The Windows project spells `renderer/OpenGL/gl_Image.cpp` as `gl_image.cpp`; the audit
matches project paths without case and records the actual on-disk filename so the
inventory works on hosts with either filesystem behavior.

Third-party dependencies imported for the port live under `src/external/`.
The current core uses `src/external/dlmalloc/`, JPEG 6 and zlib 1.2.3. dlmalloc is listed
in `CORE_C_SRC`, and the heap bridge imports its header through a system include path.

## Source dispositions

| Disposition | Units | EE policy |
| --- | ---: | --- |
| Campaign runtime | 285 | Includes the native shell handler with its press-start, root and credits screens; M3 runtime adaptation remains required |
| PS2 replacement | 62 | Desktop platform, GPU/device, input and BFG service implementations need adapters |
| Deferred runtime | 29 | Other BFG shell screens, multiplayer, online services and demo paths; callers still need explicit adapters |
| Vendor runtime | 34 | Nine zlib streaming/CRC and 25 JPEG decompression sources; separate vendor flags, strict tagged heap hooks |
| Optional vendor | 22 | Remaining shipped zlib/JPEG units excluded from the EE |
| Host conversion | 5 | Desktop model import and texture encoding candidates |
| PCH excluded | 3 | Visual Studio precompiled-header translation units |
| Outside project manifests | 18 | Kept for inspection; never silently enabled |

These are source inclusion decisions, not claims that replacement APIs exist. Essential
HUD/PDA menus, GUI/SWF, campaign physics/scripts/AI, collision, renderer frontend and
logical sound remain in the intended runtime list. Original multiplayer and BFG launcher
files are excluded; typed providers close their retained link dependencies and reject
unavailable operations explicitly.
The `doomclassic/` source tree has been removed; source-list and resident-link guards
continue to reject Classic Doom inputs. Retail assets do not participate in these build targets.

## Build gates and artifacts

Host-only `make resource-unpack`, `make asset-reexport`, `make ui-inventory` and
`make ui-cook` read local retail containers and publish ignored extraction/inventory/
cooking output. The C++ unpacker and texture converter are built by the host-tool CMake
project, `src/tools/CMakeLists.txt`, with the host compiler and without engine linkage,
on the shared `src/tools/assets` library; the UI pipeline uses Python's standard library
and runs the converter. None adds an EE source, vendor dependency or target capability.
The [pipeline guide](RETAIL_UI_PIPELINE.md) describes bounds and the candidate manifest.
The native UIPG reader, page splitter and fixed texture cache bind the authored pages;
the GSTX reader and paletted image bind the converted retail textures.
The English resource catalog, inactive retail dictionaries and opt-in bounded timeline
execution now build on that layer. Production retains **379 direct inputs** and the
compile database has **405 entries**. The native shell handler, its press-start and root
screens and the original Credits unit drive the shell menu; the minimal retail owner
remains for the timeline and full-draw test modes. The original dialog and the shell's
start and root screens draw in full, key events drive the menu and its Quit opens
Common's native confirmation; the other shell screens remain pending. See [the execution contract](RETAIL_UI_PIPELINE.md#scoped-retail-timeline-execution).

`make` / `make release` link the production native console loop as
`build/<config>/d3bfg.elf`, with the full retained campaign source set and no test inputs.
`make headless-core` links asset-free foundation tests into
`build/<config>/tests/core/d3bfg_tests.elf`; `make platform-probe` selects the separate
SDK-only test variant. Both use `PS2_D3BFG_TESTS=1`; Make source selection supplies
the core stage or the `PLATFORM_TEST_CXX_SRC` skip provider. The platform link has no
Doom core services. The core test initializes retained foundation services and
does not initialize the campaign game. `CORE_BOOT` controls this selection only in
Make; `ID_PS2_CORE` and `PS2_D3BFG_FOUNDATION` still control engine header boundaries.

`PS2_CXX_SRC` owns production entry, the Common loop provider and the strict optional
presentation and movie providers. Production links 374 direct objects, retaining all eleven
registration roots with no test inputs or fixture symbols. Native game initialization,
declarations, key/console and event services, CPU worlds and silent sound bookkeeping
precede the console loop.
`make headless`, `make console` and `make ui` stage the same ELF with authored data separately
from retail assets. Package-root `presentation.cfg` selects headless/NTSC/PAL before
Common startup. `make test-headless`, `make test-console` and `make test-ui` exercise production itself.
Visible frames call real native `console->Draw(false)` after the native `NOMAP` tick;
`UpdateScreen` coalesces in-frame requests without advancing clocks. Native `RunFrame`
returns at `NOMAP` until map integration exists.

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

`make compile-game` independently compiles all 282 intended runtime units with the
campaign header boundary. Both configurations pass. It does not select the reduced
core precompiled header or establish a resident game link; replacement services and
registrations still need that separate gate.

Each successful core/platform/script test link writes:

- `d3bfg_tests_unstripped.elf`, the matching symbols for crash analysis;
- `d3bfg_tests.elf`, stripped by default (`STRIP_ELF=0` copies the unstripped artifact);
- `d3bfg_tests.map`, a linker map without garbage collection;
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
No project-wide `-w` or VU dependency is introduced. Core/platform tests are asset-free;
production native filesystem startup requires `base/default.cfg` in HostFs.

Compiler/flag/source-list stamps rebuild affected object groups when configuration
changes; changing flags no longer requires manually deleting the object tree. Header
dependencies use `-MD -MP` so changes to system-treated SDK/vendor/engine bridge
headers also invalidate objects. `make compiledb` dry-runs the real rules, checks Make's exit
status, and writes argument-array entries only after a successful dry run. An existing
database survives a failed command. Core and campaign objects live in separate trees
because their precompiled-header boundaries differ.

The active Makefile borrows Quake II compiler/configuration and warning policies,
but has independent Doom source lists and no dependency on another checkout. The
historical copied reference Makefile was removed; compare with the external
`quake2-ps2` checkout when needed.


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
also linked in the foundation for target decoding and cleanup probes.

`JPEG_TEST_CXX_SRC` is an explicit host-only list of the shipped legacy JPEG sources.
The separate target lists select only audited decompression/streaming units. Host codec
objects retain visible legacy warnings without `-Werror`; Clang's removed `register`
and writable-string diagnostics are suppressed only there. The new decoder/test units
retain strict warnings and ASan/UBSan. The source audit allows this host fixture list
without allowing additional excluded vendor/editor code into target groups.

The codecs now live under `src/external/`, with target decode/runtime acceptance and
a passing resident link. The original IJG terms are restored as `README.ijg`. A tagged fix bounds the
shipped encoder's Huffman-value copy; the authored fixture generator exposed its
256-byte read from 12/162-byte standard tables under ASan.

## Portable filesystem services

`CORE_BACKEND_CXX_SRC` includes strict shared `filesystem.cpp` and the engine-facing
`sys_filesystem.cpp`; both cold units use `-Os`. The foundation opens real permanent
streams, while explicit whole-file fixture reads keep their 64 KiB limit. The full
campaign `FileSystem.cpp` is retained in the resident link and now initializes an
explicit native loose-file search root for the authored game fixture. Retail container
startup remains pending. It uses checked stdio handles, paths and directory services;
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
no game object or game initialization stage. The resident link supplies typed subsystem
boundaries; full container filesystem, active render/sound, UI/dialog and save behavior
remain pending.

`make test-common` runs seven partial-startup cleanup probes and fifty-nine expected-fatal
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
campaign headers and strict warnings, and is cold (`-Os`). It now owns real logical
fixture PCM with loaded/default/timestamp state, frame counts, exact rate-based duration
and peak amplitude. `ps2/audio/pcm_wave.cpp` validates RIFF headers through a bounded
stream callback and supplies shared host/EE timing math. It has the regular optimization
flags because amplitude queries are used during gameplay. The shared `voice_timeline.cpp`
also uses regular optimization flags in both backend groups. It advances bounded playback
phase with an explicit monotonic clock, fractional pitch and lead-in/loop transitions.
The native adapter owns a 48-slot pool and pins each allocated voice's samples through
stop/completion until free or shutdown. The foundation tests these services directly but
does not initialize native sound worlds. Native `SoundVoice.cpp`, `WaveFile.cpp` and
XA2 implementations remain replacement sources. The base voice constructor is supplied
with native defaults; surround calculation and unconfigured physical device initialization
fail explicitly. No `s_subFraction` or other audio cvars or vendor disposition changes are added.

Core smoke requires seven audio markers for metadata, PCM timing/amplitude, generated
defaults, shared timeline behavior, voice playback/loop envelopes and pool reuse.
Seventeen expected-fatal probes cover missing files, unloaded
duration, unsupported format, truncation, chunk bounds, payload budget and device
initialization, plus invalid clocks/voice inputs, pinned-sample mutation and pool ownership,
in fresh processes with assertions enabled and disabled. See acceptance in
[PORT_STATUS.md](PORT_STATUS.md).


## Resident campaign link gate

`make link-game` / `make BUILD=release link-game` attempt the actual EE link with
all 281 campaign objects, eighteen campaign backend objects, seven shared support
objects, two production entry/lifecycle units, dlmalloc and 34 codec objects: 343 direct inputs, without GC or archives hiding undefined
references. `RESIDENT_SUPPORT_CXX_SRC` uses its own full-header object tree and flag
stamp. It excludes `core.cpp`'s fixture filesystem and `common_foundation.cpp`'s method
substitutes. `SCRIPT_PROBE=1 link-game` is rejected explicitly.

`link_resident.py` records the compiler/link command, flags, source/dependency/object
hashes, actual linker status, unique demangled symbols and requesting objects in
`build/<config>/report.{json,txt}`, beside the raw log, response file and map.
The current gate passes in both configurations without unresolved symbols or duplicate
definitions; each emits matched stripped/unstripped resident ELFs. Compiler nonzero exits stay failures even without a recognized diagnostic.
Successful links also require a MIPS executable/load segment, every input in the map,
static game interfaces, three native class registration roots and the static CVar
registry. Stale ELF/report files are cleared before attempts. The production entry now pumps Common frames and shuts down at a quit boundary.
`make headless-game` separately links 345 inputs under `build/<config>/tests/game/`,
including four test entries/adapters, with `PS2_D3BFG_TESTS=1` in isolated object trees.
Production uses `0`; its gate rejects test sources, transitive headers and fixture
symbols. Test runtime gates remain `make test-game` and `make test-map`.

Native `d3xp/Achievements.cpp` moves from deferred to campaign runtime. Its campaign
logic and cvars remain intact; portable builds gate the Classic header and terminate
Classic evaluation. Every original campaign unit stays selected. Achievement-manager
runtime execution remains pending the actual resident player fixture.

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

The active 2D bring-up proposal and Quake II reuse inventory are in
[RENDERER_2D_PLAN.md](RENDERER_2D_PLAN.md). Its destinations and budgets are proposed;
the renderer slices below record the existing implementation history.

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
They do not implement a logical world renderer for broader map startup, allocate stock
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
Campaign-only `shell_stub.cpp` supplied the native shell vtable and required save/load
screen enumeration calls until 2026-10-23. `MenuHandler_Shell.cpp` and the press-start
and root screens are now campaign units (the native shell menu); the stub keeps only the
load and save screens' enumeration calls, which the retained save-information widget
references, and they fail explicitly. The other shell, lobby, leaderboard and scoreboard
screens stay deferred. Resident-support `retail_menu.cpp` supplies the handler's movie
hooks to production and the game test image alike.

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
poller, device buffer or additional upstream unit is enabled. The resident game fixture
now injects synthetic commands directly into native `idUserCmdMgr` for its player-physics
probe; physical controls remain a later runtime gate.

At the input-interface slice, the core contained 21 backend and twelve smoke units. Three required `input/`
markers verify native table order/extent and independent command names/prefix handling,
repeated empty cleanup and invalid indices with exact ledger recovery, and disabled
controller cvar policy. Eleven fresh-process probes cover every unavailable input method,
null command validation and forced controller/rumble values with asserts enabled and
disabled. That slice retained all 305 inputs and left only 17 codec symbols unresolved.
Source dispositions stayed unchanged; source audit and the compile database covered the new
strict full-header units. The input link group is closed; physical input acceptance remains a later gate.

The negative-probe classifier matches only the first fatal line and requires a name
boundary after the expected text. A new host regression rejects InitForNewMap under
an Init probe and a later matching message after an unrelated failure. The current
host suite at that slice contained 62 Python regressions plus the shared ASan/UBSan fixtures.


## Codec integration and M2b link acceptance

`VENDOR_C_SRC` selects nine zlib 1.2.3 C sources with C11 vendor flags and
`MY_ZCALLOC`; `VENDOR_CXX_SRC` selects 25 engine-modified JPEG 6 decompression sources
with the visible legacy C++20 warning policy. Both have separate compiler/flag/source
stamps and explicit object trees. Vendor format/bounds warnings remain visible. Observed
legacy warnings include zlib state-machine fallthroughs and K&R declarations, and JPEG
`register`/unused declarations; no blanket suppression was added for them.

The strict `codec_memory.cpp` provides typed JPEG no-backing-store allocation hooks,
checked C-linkage zlib products and shared JPEG diagnostic hooks. Explicit stream
allocator callbacks retain priority over defaults. Codec memory uses native JPG/ZIP
heap tags and returns to the exact ledger on successful and partial-failure cleanup.
No temporary-file memory manager, JPEG encoder, raw JPEG memory reader or gzip file I/O
is linked on the EE. Portable `LoadJPG` fails explicitly; bounded SWF decoding is usable.

The source inventory moves 34 bundled sources from optional vendor to vendor runtime.
No campaign units were dropped. Original codec paths are remapped for reference-project
coverage, and host regressions reject lost relocations or excluded codec additions.
Codec integration recorded 358 compile-database entries, 23 foundation backend units
and 13 smoke units. The first M3 PCM slice recorded 359 entries and 24 foundation backend
units. The voice slice added shared timeline and smoke units: 361 entries,
25 foundation backend units and 14 core smoke units. The initial game fixture adds
two resident-only units, bringing the compile database to 363 entries. `compile-game`
also builds the 34 audited codec units. The core test ELF runs core/codec/audio fixtures;
the separate resident ELF runs the authored game fixture.

## Initial headless game fixture

`make headless-game` selects the whole-object resident link above. `make test-game`
runs positive native startup/ticks/reloads, missing-map and malformed-script scenarios
in fresh PCSX2 processes; `BUILD=release` verifies the same behavior without asserts.
The strict, cold `tests/smoketests/headless_fixture.cpp` is a campaign test unit;
`tests/smoketests/game_boot.cpp` is strict, cold resident support. Neither changes the
281-unit native campaign manifest or enables additional vendor code.

The resident entry uses real staged Common services, native declaration management,
native game/class/event/script startup and native filesystem streams/listing. Tagged
engine edits select the isolated loose-file root and permit playerless `RunFrame`
calls without a render world. The explicit startup method accepts only the bounded
worldspawn logic map; regular `InitFromNewMap` remains separate. The real class factory,
entity Think loop, interpreter and `waitFrame` event scheduling execute over three
reloads. The script also dispatches native `activate` to `idTarget_SessionCommand` on
frame four; a queued 90 ms activation fires on frame six. The native return path
consumes both commands in their frames. Script removal on frame seven invalidates
the cached `idEntityPtr` and cancels a queued 120 ms activation, checked after its
deadline on frame eight. The fixture validates two fixed axial brushes before native
collision conversion and clip initialization. Point/box traces, contents/masks and
entity filtering are checked; native monster physics runs through the probe's
`RunPhysics` calls and stops at the wall. Raw, text-cache and binary-cache reloads
also verify gravity, floor contacts/rest and grounded diagonal wall sliding through
two additional instances of the same native probe class. Stepping is disabled for
the sliding case. A second native fixture class owns `idPhysics_Player`, consumes
explicitly injected commands through `idUserCmdMgr` and runs native acceleration,
wall collision, reverse movement and release friction through `RunPhysics`. Its
floor support, command cursor/timing and teardown are checked. Additional native frames
verify two jumps/landings, held-jump suppression/release, crouch collision resizing and
unobstructed standing restoration; no new source units or providers are enabled.
Full player spawning
remains pending. All map shutdowns recover exact ledgers. Tagged engine fixes reset
binary-cache trace visitation and
preserve loaded statistics; portable Common shutdown frees retained diagnostics before
idlib teardown. Map shutdown removes script bindings and all collision/clip allocations.
AAS/PVS, players, render/sound worlds, shell resources and retail data are not initialized. Native sound/render integration follows
later headless gameplay work.

Resident reports include matched runnable/unstripped ELF and map hashes. The runner
checks them and the flag stamps before archiving the image, symbols, map, reports,
response file, settings and authored fixture hashes. Host regressions reject stale
images/flags, incomplete link evidence, partial tick traces and unrelated fatal errors;
only the target runs establish native game execution.


## Native authored map startup

`tests/smoketests/map_boot.cpp` is one additional strict, cold resident support unit.
The resident link now has 345 direct inputs, and the regenerated compile database has
364 entries; the 281-unit campaign manifest and 458-unit source inventory are unchanged.
`make test-map` selects the single `game-map` scenario with authored inputs in
`src/tests/data/headless_map/`. Native `InitFromNewMap`, worldspawn/AI factories, PVS,
collision and AAS run. CPU models/worlds use explicit logical renderer initialization;
physical renderer initialization and the core target's world allocation remain unavailable.
A native empty sound world supplies bookkeeping only. Tagged engine changes skip
automatic player spawning for this mode and bound silhouette scratch by the surface's
index count on PS2. Full campaign player startup, animated models/AI, Common transitions
and retail assets remain later gates.

## CPU-only GS device foundation (2D-1)

Four strict backend sources now supply `video_mode`, `gs`, `cmd_buffer` and the
backend `render_system`. `gif_writer.h` reserves before SDK environment writes;
`pipeline_wait.h` uses wrap-safe COP0 Count deadlines for DMA, FINISH and VSYNC
in both configurations. The command recorder owns tagged, 64-byte-aligned halves
and inline packet2 headers; it does not borrow map/parser scratch or initialize VU.
The native Doom renderer providers remain inactive. Device allocation is opt-in
through the backend API; the production Common loop still runs headlessly.

The first profile is 512×448 NTSC or 512×512 PAL, two PSMCT16 framebuffers and Z16
with masked depth writes. A frame alternates contexts, clears, emits clipped solid
GS sprites, fences and presents immediately. Partial/repeated shutdown drains
references before releasing storage. Textures, native GUI/SWF and menu drawing
remain subsequent roadmap gates. At the foundation commit, visual acceptance was
pending the integrated GS run; the accepted follow-up is recorded below.

Make and the resident linker now pass the same explicit `draw graph packet2 dma
kernel m patches` library order, recorded in the resident SDK flag stamp and link
command/report. Production retains 347 direct inputs without GC or test symbols.
Debug/release fixed `PT_LOAD` is 10,875,174 / 11,293,734 bytes. Both strict EE builds
and core test links pass; eleven resident-link host regressions pass. The compile
database contains 371 entries.

The existing production 120-frame and live-console/quit regressions passed in both
configurations on 2026-10-14, with exact startup ledger recovery (7,424 requested /
7,600 backing / four allocations) and no GS initialization. Archives: debug
`20261014T052340Z_headless_b23efba852d1`, `20261014T052345Z_headless_4836b1b75218`;
release `20261014T052523Z_headless_9cbf51804843`, `20261014T052527Z_headless_0621a07f9946`.

The completed 2D-1 follow-up claims/resets the VIF/GIF interfaces before the first
DIRECT and restores the original GS interrupt mask on teardown. The existing core
test driver selects one new integrated `renderer_gs_tests.cpp` by manifest; production
source lists do not include it. `test-renderer` runs NTSC/PAL plus three release-safe
failure cases, and `run-renderer` holds the final scene for F8 inspection. Native
console/SWF/menu stubs remain unchanged. Final production `PT_LOAD` is 10,875,686 /
11,293,990 bytes with 347 inputs and no test symbols; the compile database has 372
entries. Measured scene budgets, inspected captures and final debug/release regression
identities are in [PORT_STATUS.md](PORT_STATUS.md#cpu-only-gs-bring-up-2d-1).

The accepted 2D-2 source lists add strict `texture_layout.cpp`, `vram.cpp`, `texture.cpp`
and cold `capture.cpp` to both core and resident support. Production has 351 direct
inputs, no test inputs/symbols and no GC; debug/release `PT_LOAD` is 10,978,470 /
11,396,518 bytes. The compile database has 376 entries. Native image/GUI method providers
stay inactive. The existing scene now verifies textures, dirty/eviction fences and
native TGA pixels/recovery; capture injection compiles only under `PS2_D3BFG_TESTS`.
Both production headless checks and all 115 Python host regressions pass; see
[2D-2 acceptance](PORT_STATUS.md#rgba-textures-and-native-capture-2d-2) for budgets and artifacts.

The first 2D-3 slice adds explicit strict `ui_geometry.cpp`, `ui_draw.cpp`, `charset.cpp`
and cold `image_backend.cpp` to core/resident support. Native display/color/quad/text
methods move out of `null_render.cpp`; implemented intrinsic-image methods move out
of `image_stubs.cpp`. A tagged `Image.h` friend returns the typed handle from the
existing opaque backend state. There is still one provider per native method, with no
desktop `RenderSystem.cpp`/`Image_GL.cpp` added. Core adapters and the GS test use the
audited full-header foundation seam; resident adapters use full campaign headers.

At the 2026-10-14 first-slice checkpoint, production had 355 direct inputs, no test
inputs/symbols and no GC. Debug/release `PT_LOAD` was 10,992,678 / 11,412,390 bytes;
the compile database had 380 entries.
`Console.cpp`, `ConsoleHistory.cpp`, `EditField.cpp`, `DebugGraph.cpp`, `Material.cpp`
and `DeclManager.cpp` remain campaign-only. The adapter scene calls native draw helpers
with authored content; it does not import the real console into the core test target.
At that checkpoint, both headless modes and all 122 Python regressions passed, plus
host geometry/charset ASan/UBSan harnesses. Production Common presentation and real
console acceptance were still the remaining 2D-3 gate.

The remaining 2D-3 implementation adds strict, cold `system/presentation.cpp` only to
`PS2_CXX_SRC`/`SIZE_OPT_CXX_SRC`. Native `Console.cpp` retains RAM history on Enter,
while persistent history writes remain unavailable. The tagged PS2 `UpdateScreen`
branch forwards to this production provider; native frame and shutdown ownership stay
with Common. Production mouse-capture requests are harmless without a mouse; test
variants retain their unavailable-capability checks. No test fixture or new upstream
source is enabled, and the core test still does not link Console/Material/DeclManager.

At the 2D-3 checkpoint, production had 356 direct inputs, no test inputs/symbols and no GC. Debug/release
`PT_LOAD` was 10,997,606 / 11,417,190 bytes; the compile database had 381 entries.
`stage_headless.py` packages explicit startup policy for either mode; `run_console.py`
checks the real native edit field, history replay, close/reopen and capture-before-quit
in NTSC/PAL. Host capture checks distinguish native glyph strokes/holes and translucent
background from missing/opaque text. All four debug/release NTSC/PAL production
console runs and both configurations of the existing headless matrix pass, along with
135 Python regressions and existing ASan/UBSan C++ harnesses. The inspected captures
and acceptance evidence are recorded in [PORT_STATUS.md](PORT_STATUS.md#production-native-visible-console-2d-3);
indexed GUI/SWF, general materials and the native menu remain later gates.

The first 2D-4 slice adds explicit strict `ui_triangles.cpp` to core/resident support.
`ui_draw.cpp` now owns bounded tagged CPU vertex/index/run storage and implements
native `AllocTris`, `DrawStretchTri` and transformed `DrawStretchPic` collection.
`render_system.cpp` flushes ordered collection at sprite/frame boundaries, and `gs.cpp`
emits affine UV triangles with Gouraud RGB/alpha. This adds no upstream source, no
desktop `GuiModel.cpp`/vertex-cache allocation path and no additional test fixture.
The existing GS test scene now covers indexed drawing and five more failure cases.

At the indexed-geometry checkpoint, production had 357 direct inputs, no test inputs/symbols and no GC.
Debug/release `PT_LOAD` is 11,009,702 / 11,434,406 bytes, and the compile database has
382 entries. Campaign source dispositions remain unchanged. Both production builds
pass; target acceptance for indexed geometry is recorded in PORT_STATUS. Native SWF,
fonts, general materials and the offline menu remained later gates at that checkpoint.

The authored SWF implementation adds strict, cold `ui/authored_assets.cpp` and
`renderer/font_backend.cpp` to resident support, plus production-only `ui/movie.cpp`.
No upstream unit is newly enabled: native SWF, Font, Material and DeclManager code was
already retained. Tagged native seams admit checked binary snapshots, skip unused
retail cursor/controller preloads, retain the white RGBA intrinsic, free font arrays
and use caller-owned timeline time for redraws. The core target retains inactive font
providers and does not link the movie, native Font/SWF or authored resource snapshot.

At the authored 2D-4 checkpoint production had 360 direct inputs, zero test inputs/symbols and no GC. Debug/release
`PT_LOAD` is 11,018,214 / 11,435,878 bytes; the compile database has 385 entries.
`make ui` packages authored data separately from the existing console/headless packages;
`make test-ui` exercises the production movie and its failure boundaries. Retail menu
dependencies, general material/image/font loading and the offline shell remain pending.

The 2D-5 page foundation adds strict `ui_page_format.cpp`, `ui_page_geometry.cpp` and
`ui_page_image.cpp` to core/resident support. The native binding reuses those existing
units and the existing image/UI/movie adapters: it enables no additional upstream
source or target fixture. Tagged `Image.h` declarations expose checked direct/paged
access and a private backend loader. Production still has **363 direct inputs**, zero
test inputs/symbols and no GC; debug/release `PT_LOAD` is **11,039,078 / 11,461,990 bytes**.
The compile database has **388 entries**. A separate ASan/UBSan host cache harness uses
the real cache/format/geometry/heap code with assertions 0/1 and stubbed GS submission;
it adds no vendor source to an EE list. Header dependencies keep that harness current.
The authored native SWF/font binding and remaining retail admission gates are recorded
in [PORT_STATUS](PORT_STATUS.md#native-swf-and-font-page-binding-2d-5-slice).

The English retail resource slice adds strict `ui/retail_catalog.cpp`,
`ui/retail_assets.cpp` and `renderer/ui_font_format.cpp` to core and resident support;
resident-only `ui/retail_materials.cpp` generates the admitted constant stages.
Production-only `ui/retail_movie.cpp` owns the opt-in resource screen/command. No
additional upstream unit, vendor unit or target fixture is enabled. Tagged native
Font/Material/Image seams retain the original glyph/index parser, lookup and UVs
behind bounded snapshots; the core test still does not link native Font/SWF/Material.
Production now retains **368 direct inputs**, zero test inputs/symbols and no GC.
Debug/release `PT_LOAD` is **11,055,910 / 11,478,438 bytes**; the compile database
contains **393 entries**. The core ELF retains **2,093,296 / 2,177,264 bytes** at this
checkpoint. Separate host sanitizer readers validate UICT and native font metadata;
they do not replace strict EE compilation or native pixel checks.

The inactive retail dictionary/text slice adds strict shared `ui/swf_format.cpp` to
`CORE_BACKEND_CXX_SRC` (45) and `RESIDENT_SUPPORT_CXX_SRC` (30), with size optimization
for cold validation. It adds no upstream/vendor compilation unit or target fixture.
Production retains **369 direct objects**, zero test inputs/symbols and no GC. Fixed
`PT_LOAD` is **11,066,918 debug / 11,490,086 release bytes**; core is **2,101,488 /
2,185,328**. The compiler database has **394 entries**. Tagged native SWF constructor,
load/presentation gates and isolated text entry retain the original parsers/layout;
a PS2-only LangDict release operation frees localization keys/pools at shutdown.
The new host sanitizer reader checks retained BSWF storage and English grammar; it
cannot authorize native AVM1/placement execution or replace target validation.

The paletted retail texture slice adds strict `renderer/gstx_format.cpp` and
`renderer/ui_paletted_image.cpp` to core and resident support, size-optimized like the
other cold UI units; the paletted image takes the engine-header seam of its renderer
neighbors in the core build. Indexed PSMT8/PSMT4 layouts, paired page/CLUT binding and
region-clamped draws extend the existing texture owner and GS units. No upstream or
vendor unit or target fixture is added; the tagged `Image.h` friend declaration names
the common `PagedImage` interface. Production retains **376 direct objects**, zero test
inputs/symbols and no GC. Fixed `PT_LOAD` is **11,177,957 debug / 11,614,821 release
bytes**; core is **2,150,896 / 2,235,504**. The compiler database has **401 entries**.
Host sanitizer tests check the target's GSTX admission against converter output.

The namespace cleanup (2026-10-21) moves the UI units from `src/ps2/renderer/` to
`src/ps2/ui/` and drops their `ui_` prefix: `draw`, `geometry`, `triangles`, `masks`,
`page_format`, `page_geometry`, `page_image`, `paletted_image` and `font_format`, plus
`gstx_format`, `font_backend` and `charset`. Each keeps its position, group and flags in
the source lists, so link order is unchanged. Production retains **376 direct objects**;
fixed `PT_LOAD` is **11,177,829 debug / 11,614,693 release bytes** (128 fewer, from
shorter diagnostics), and core stays **2,150,896 / 2,235,504**. The compiler database
keeps **401 entries**.
