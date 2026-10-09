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
| Campaign runtime | 274 | Intended retained units; compilation and adaptation remain required |
| PS2 replacement | 68 | Desktop platform, GPU/device, input and BFG service implementations need adapters |
| Deferred runtime | 34 | BFG shell, multiplayer, online services and demo paths; callers still need explicit adapters |
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
also declares the three framework units and their new heap/platform support separately.
`build/<config>/libd3bfg_core.a` is a compilation artifact, not a boot proof.
It also includes the shared logger, which both the core adapters and SDK-only probe use.
The game class allocator is linked into the core for focused adapter tests; the
campaign's `idClass` hierarchy and `idGameLocal` are not yet part of that executable.

`make headless-core` explicitly selects the same core bootstrap/test sources and links
all core objects directly, without garbage collection. Debug/release core and expected
missing-fixture runs have passed in PCSX2; see [PORT_STATUS.md](PORT_STATUS.md).

`make compile-game` independently compiles all 274 intended runtime units with the
campaign header boundary. It intentionally exposes remaining M2b portability failures;
it does not select the reduced core precompiled header or claim that replacement
implementations have been supplied.

Each successful ELF link writes:

- `d3bfg_unstripped.elf`, the matching symbols for crash analysis;
- `d3bfg.elf`, stripped by default (`STRIP_ELF=0` copies the unstripped artifact);
- `d3bfg.map`, a linker map without garbage collection;
- `build-report.json` and `build-report.txt`, compiler version, source/config identity,
  source hashes, ELF hash, ELF load-segment memory total, section sizes and largest symbols.

The load-segment total measures fixed ELF residency. Runtime heap allocations and
thread stacks need separate measurements before drawing a total EE memory budget.

Debug uses `-O2` and symbols; release uses `-O3` and no debug symbols. Explicit cold
backend sources use `-Os`. Both use C++20 with exceptions, RTTI and thread-safe local
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

The campaign manifest retains all 274 units; both configurations now compile 268.
The six remaining common/Classic/sound failures are recorded in
[PORT_STATUS.md](PORT_STATUS.md), and the campaign gate remains unsuccessful.

## Campaign diagnostics and JPEG adapter

`GAME_BACKEND_CXX_SRC` lists required campaign-only backend objects. `compile-game`
compiles them with strict backend warnings and their own flag stamp, alongside the
unchanged 274-unit campaign manifest. They are not linked into the foundation to
manufacture runtime acceptance. The first adapter supplies bounded SWF JPEG decoding.

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
