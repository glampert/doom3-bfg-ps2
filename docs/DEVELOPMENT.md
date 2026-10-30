# Developer guide

The [project README](../README.md) provides a short overview and toolchain setup.
The command reference is in the header of the [Makefile](../Makefile). Run commands
from the repository root. This guide covers packaging, runtime controls, build
boundaries and diagnostics; it does not imply that the campaign is playable.

Current implementation and acceptance details live in:

- [Port status](PORT_STATUS.md): verified capabilities, limitations, captures and measurements.
- [Implementation plan](IMPLEMENTATION_PLAN.md): campaign milestones.
- [2D renderer roadmap](RENDERER_2D_PLAN.md): UI/menu bring-up and remaining rendering work.
- [Retail UI pipeline](RETAIL_UI_PIPELINE.md): local cooking, fonts, dictionary/text inspection and scoped timeline execution.
- [Agent handover](AGENT_HANDOVER.md): current boundaries, next work and verification recipes.
- [Build inventory](BUILD_INVENTORY.md): explicit sources, portability changes and warning policy.
- [Backend cvars and commands](CVARS.md): defaults, flags and supported controls.
- [Reuse inventory](REUSE.md): provenance and third-party notices.

## Build prerequisites and policy

The native build uses the ps2dev EE compiler and PS2SDK, with GCC 15.2 used for current
verification. Python 3.10 or later runs the build/report/cook scripts. Host tools and
sanitizer tests use a C++20 host compiler (`HOST_CXX`, default `clang++`). The C++ host
tools are a CMake 3.24+ project, `src/tools/CMakeLists.txt`: `make resource-unpack` and
`make asset-reexport` configure it into `build/tools/cmake/` with `HOST_CXX`, and other
hosts, Windows/MSVC included, build it with `cmake` directly. VU tooling is reserved for later renderer work;
no VU program or vclpp dependency is built yet.

GitHub Actions runs `make` and `make release` on Linux in the pinned ps2dev `ps2sdk` image,
and builds the host tools with GCC, Apple clang and MSVC. These are compile and link checks;
host tests and PCSX2 gates still run locally. Linux file names are case-sensitive, unlike
the default macOS volume; [ci.md](../.claude/rules/ci.md) describes reproducing the Linux
build on a Mac.

Native source lists are explicit in `config/sources.mk`. Run `make audit-sources` to audit
them and `make compiledb` after source-list changes. No source discovery enables
extra engine/vendor files. The copied Quake II reference Makefile has been removed;
use the external `quake2-ps2` checkout when comparing build policies.

## Resource extraction

The [host resource unpacker](../src/tools/resource_unpack/README.md) expands native BFG
`.resources` containers into ordinary files without starting the engine. It validates
tables and paths, streams payloads and refuses existing outputs by default. For example:

```sh
make resource-unpack
build/tools/resource_unpack --output build/unpacked-bfg \
    gamedata/d3_bfg/_common.resources gamedata/d3_bfg/_ordered.resources
```

Extracted BSWF/BIMAGE files keep their native formats. Retail extraction belongs in
ignored local directories; it is separate from the host SWF fixtures and target startup.

## Texture re-export

The [texture re-export tool](../src/tools/asset_reexport/README.md) converts retail
textures into paletted GSTX files the GS samples directly, from a JSON job. `make ui-cook`
writes that job for the retail UI and runs it; `UI_PREVIEWS=build/ui-previews` also writes
an original and a converted PNG of every texture for a visual review:

```sh
make ui-cook UI_PREVIEWS=build/ui-previews
```

`conversion.texture_policies` overrides policy fields for one image (the console charset
keeps `scale` 1.0, so its 16×16 cells stay whole). The same tool pre-renders menu fonts at
the exact sizes a screen draws them, listed under
`conversion.glyph_sets` in `config/ui_retail.json`. A retail font drawn at a size it has
no bitmaps for logs `[D3BFG] UI glyph size font=NAME scale=X,Y stroke=W prerendered=no`
once; add those values to the list and cook again. See the
[retail pipeline guide](RETAIL_UI_PIPELINE.md#pre-rendered-glyphs).

Both tools share the `src/tools/assets/` library, meant for every later texture
re-export.

## Production packages and controls

Every visible package, `make stage-console` included, stages the cooked retail
catalog and assets from `UI_COOK_OUTPUT` (default `build/ui-retail`) into its `base/`:
the presentation opens the catalog at startup and draws the console with the original
`textures/bigchars` charset. Run `make ui-cook` before staging or testing a visible
package; headless packages need no cook.

Retail UI commands and staging are described in the
[retail pipeline guide](RETAIL_UI_PIPELINE.md#scoped-retail-timeline-execution).
`ps2_uiRetail movie shell|dialog` inspects inactive resources;
`ps2_uiRetail execute shell|dialog` runs admitted native timelines and the minimal
widget owners while retaining the separate text inspection screen.
`ps2_uiRetail dialog quit` draws the original dialog in full with Common's quit
message. Shell drawing and menu navigation remain pending. To inspect the current
original-game resources:

```sh
make ui-cook
make run-ui-movies
```

A newly created retail-movies package automatically loads its resource screen
and selects inactive shell inspection. With that package running, append commands
from another terminal:

```sh
printf 'ps2_uiRetail execute dialog\n' >> build/debug/ui-movies/console.txt
```

Existing packaged settings are preserved. If an older package did not load the
resource screen, check `ps2_uiRetail status` and send `ps2_uiRetail load` once before
execution; repeating `load` while already loaded is fatal.

This executes the original dialog timeline while drawing isolated native English
labels. `dialog quit` instead draws the complete dialog through native `idSWF::Render`
from its resident converted textures and pre-rendered glyphs, at about 40 ms per debug
PCSX2 frame; the earlier RGBA page slice took about 1.6 seconds per frame while its
pages streamed from HostFs.
`execute shell` selects the minimal carousel/Credits owner; `movie shell|dialog|none`
selects inactive inspection. `make test-ui` checks both, with the full dialog, in its
merged retail session (`run_ui_session.py`). All authored startup/map/UI
fixtures live under `src/tests/data/`; generated packages and captures stay in `build/`.

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

`make stage-headless` packages the production ELF and small authored boot data from
`src/tests/data/headless/` under `build/<config>/headless/`. It preserves an existing packaged
`base/d3bfg.cfg` and leaves local `base/` assets alone. The package root has
`presentation.cfg`, read before Common initialization: `headless`, `ntsc` or `pal`,
with an optional final newline. `ntsc menu` or `pal menu` also opens the native shell
menu at startup (`make stage-ui-menu`). A missing file selects headless; invalid content is
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

`make stage-headless-retail` stages the same ELF under `build/<config>/headless-retail/` with the
retail `_common.resources` and `_ordered.resources` of `RETAIL_DATA_ROOT` (default
`gamedata/d3_bfg`) hard-linked into its `base/` (copied across volumes; the native
listing skips symlinks). Only the authored configuration and the port's
`_headless_default` material come from `src/tests/data/`; every retail declaration and
the full retail `doom_main.script` load from the containers. `make run-headless-retail`
launches it with the same `console.txt` controls. Every production startup prints
`[D3BFG] MEMORY`, `ARENA` and per-tag `TAG` ledgers after `startup-foundation`,
`startup-decls` and `startup-game`; `make test-headless-retail` checks and records them.
Retail packages stay under ignored `build/`.

`make stage-console` stages the same ELF and authored boot data under
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

Native tests use `d3bfg_tests.elf`, matching `d3bfg_tests_unstripped.elf` and
`d3bfg_tests.map`. `make build-core` writes `build/<config>/tests/core/`,
`make build-platform` writes `build/<config>/tests/platform/`, and
`make build-game` writes `build/<config>/tests/game/`. The isolated compiler probe
uses `build/<config>-script/tests/script/`. Core/platform/script reports are named
`build-report.{json,txt}`; campaign test reports are `report.{json,txt}`. All smoke
commands select these test images explicitly. Campaign tests have their own object
and flag trees with `PS2_D3BFG_TESTS=1`; production uses `0`. This is the only test
enable define. Make selects the core stage or SDK-only skip provider through source
lists; `CORE_BOOT` remains a Make selector, not a compiler define.

Reports distinguish fixed ELF load-segment residency from runtime allocation.
Flag/compiler/source-list stamps invalidate affected objects automatically. Debug
uses `-O2` with asserts; release uses `-O2` without debug-only checks. Explicit cold
backend code (`SIZE_OPT_CXX_SRC`) and id's load/init/command-time sources
(`ENGINE_SIZE_OPT_CXX_SRC`) use `-Os`. The resident links gate on a whole-object link
and run a `--gc-sections` relink of the same objects; see `link_resident.py`.

All target C++ uses C++20 without exceptions, RTTI or thread-safe local statics. New
backend/tests pass the full strict GCC warning set with `-Werror`. Legacy style warning
suppressions and the narrow system-header treatment of upstream contracts are listed
in the build inventory. Format, array bounds and uninitialized-value warnings remain
visible. A host runtime test does not replace the EE compile/link check.

## Foundation test boundary

The separate foundation test executable links every selected scalar idlib object
directly and runs the real
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
unconfigured device initialization still fails. An empty native sound world now supplies game/map startup bookkeeping; audible
voice/device integration and audsrv/SPU2 output remain later work.

Backend and smoke-test diagnostics use the shared `ps2::Log` / `LogV` sink with info,
warning, error and fatal levels. It currently writes synchronously to stdout. System,
core, heap and assertion failures terminate through `FatalError` / `FatalErrorV`;
on-screen fatal reporting remains a TODO. `src/ps2/common.h` provides `PS2_Assert`,
`PS2_AssertMsg`, `ArrayLength` and portable format/cold attributes for new backend code.

The fixture adapter streams loose files through real `idFile_Permanent` objects;
explicit bulk/memory reads stay bounded to 64 KiB. It rejects parent, absolute and
other-device paths. Shared system services supply directory filters, parent creation,
and write/append. The one-time ROM FILEIO patch protects
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
[CVARS.md](CVARS.md) documents `jobs_numThreads`, `com_smp` and later backend controls.

Scalar compilation retains intentional double arithmetic in `Parser`, `Timer`, `Token`,
`bv/Sphere`, `geometry/RenderMatrix`, `math/MatX`, `math/Matrix`, `math/Ode` and
`math/Plane`. Those operations use software helpers on the EE. Target precision and
performance tests must precede replacing them with single-precision implementations.

## Heap and memory

Doug Lea's allocator supplies the EE's sole C/newlib arena; standard C allocation entry
points and their reentrant variants route to it. Doom's aligned/tagged APIs and all
standard, sized, nothrow and over-aligned C++ allocation forms use `ps2::system::heap` metadata.
The zero-initialized ledger supports registration allocations before `main`.

Each allocation stores its requested size, backing size, tag and original base pointer.
An unsized free subtracts exactly that allocation's sizes. Invalid alignment, size
arithmetic overflow and invalid tags are rejected; required allocations terminate on
failure. Alignment is at least 16 bytes, with larger powers of two supported for DMA.
The current implementation is for a single calling thread.

The shutdown tests require current requested/backing bytes and allocation count to
return to the baseline taken before initialization. Each executable records its own
pre-initialization baseline; current measurements are recorded in [PORT_STATUS](PORT_STATUS.md).

Per-tag and total reports track current and peak requested/backing bytes. On the EE,
backing size is the allocator's usable chunk size; host tests report the reserved request.
`GetArenaStats` includes untagged C/newlib allocations and allocator overhead on the EE.
The ELF report, arena commitment, stack usage and GS VRAM usage are separate parts
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

The full scenario lists and accepted debug/release runs are maintained in
[PORT_STATUS](PORT_STATUS.md), the [PCSX2 testing rules](../.claude/rules/testing-pcsx2.md) and the
[per-gate procedures](testing/).
The runner checks the installed HostFs/logging configuration; optional USB keyboard
attachment does not enable game-side keyboard polling, which remains pending.

For a reported EE address, use the symbols from the exact production or test build:

```sh
mips64r5900el-ps2-elf-addr2line -f -C -e build/debug/d3bfg_unstripped.elf 0xADDRESS
```

Use the archived `d3bfg_tests_unstripped.elf` for a test executable. The standalone
symbolization helper lives in `src/tools/scripts/`. A custom EE crash screen and
exception handler remain pending.

## Local data and reference knowledge

Retail data under `gamedata/d3_bfg/` and `gamedata/d3_roe/` remains local and ignored.
The root `base/` retains tracked source-release configuration/render programs; new
retail files there are ignored. Synthetic tests use authored data and do not depend on
either game installation.

[AGENTS.md](../AGENTS.md) describes working conventions. [reuse inventory](REUSE.md)
records source provenance and licensing. The imported allocator retains its original
public-domain notices. The standalone `symbolize.py` retains GPL v2; new backend code
uses GPL v3 or later. Quake II measurements in `.claude/rules/` remain reference evidence
until independently reproduced with this port.
