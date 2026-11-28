# Developer guide

The [project README](../README.md) provides a short overview and toolchain setup.
The command reference is in the header of the [Makefile](../Makefile). Run commands
from the repository root. This guide covers packaging, runtime controls, build
boundaries and diagnostics; it does not imply that the campaign is playable.

Current implementation and acceptance details live in:

- [Port status](PORT_STATUS.md): verified capabilities, limitations, captures and measurements.
- [Implementation plan](plans/IMPLEMENTATION_PLAN.md): campaign milestones.
- [2D renderer roadmap](plans/RENDERER_2D_PLAN.md): UI/menu bring-up and remaining rendering work.
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
hosts, Windows/MSVC included, build it with `cmake` directly. The VU compiler, oxide-vusl, comes
from oxide-vcl, a pinned submodule under `src/external/` (`git submodule update --init`); `make`
builds it with `cargo` (a recent stable Rust) into `build/tools/oxide-vcl/` and compiles the
`VUSL_FILES` programs into `build/vu/` with it, each with its generated C++ header, then
assembles them with the toolchain's `dvp-as`. Writing VU programs for it: [vu-microprograms.md](../.claude/rules/vu-microprograms.md).

GitHub Actions runs `make` and `make release` on Linux in the pinned ps2dev `ps2sdk` image,
and builds the host tools with GCC, Apple clang and MSVC, then requires the three script cooks to
write the same images of the authored script fixtures. Otherwise these are compile and link checks;
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

## Asset re-export

The [asset re-export tool](../src/tools/asset_reexport/README.md) converts retail
textures into paletted GSTX files the GS samples directly, from a JSON job. Its `world`
command cooks a map's `.proc` into a D3WD world file; `make world-cook` cooks the authored
graybox map (`src/tests/data/graybox_map/`) into `build/world/`, and `make world-cook-retail`
cooks mars_city1's binary `.bproc` from the retail installation (`config/world_mars_city1.json`,
`RETAIL_DATA_ROOT`) there too, with its diffuse textures and the lightmap pages of its class A
lights as `mars_city1.d3tx` (`mars_city1.lights.json` classifies every light; `WORLD_TOPOLOGY=strips`
cooks plain strips instead, drawn without a lightmap pass; give it its own
`WORLD_COOK_OUTPUT`). Its `map` command cooks the same job's `.map` into a D3ME file of its entities
(`make map-cook-retail`: `build/map/mars_city1.d3me`), with the geometry CRC its `.bcm` and `.aas48`
were built against, and each of the map's AAS files into a D3AS (`mars_city1.aas48.d3as`); packages
that mount the map's container put them at `base/maps/game/`, where production's `idMapFile` and
`idAASFileLocal` read them instead of the text. The debug `ps2_mapdata MAP` command loads the map's
game data step by step with a memory checkpoint after each, and `ps2_verifyAAS MAP` compares each
cooked AAS with id's text parse of its source.
`src/tools/scripts/vu_cost.py` counts the world programs' VU1 cycles over
cooked worlds. `src/tools/scripts/texture_census.py --data <root> [--map NAME]` counts
the retail BIMAGE textures by usage, format and colour format (headers only, about 4 s for
every container). `make ui-cook`
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

## Script cook

Production never compiles scripts. `build/tools/script_cook` (CMake target `script_cook`, on
GCC, Clang and MSVC) builds id's own compiler, idlib's lexer and parser and the program from
`src/neo` for the host, and writes the compiled program as a D3SP image
(`src/ps2/game/script_image_format.h`) that `idProgram::Startup` loads:

```sh
make script-cook          # src/tests/data/headless/base -> build/script/headless/doom_main.d3sp
make script-cook-retail   # RETAIL_DATA_ROOT's containers -> build/script/retail/doom_main.d3sp
```

Staging copies the retail image into packages that mount the retail containers and the
headless one into every package on the authored data (headless, console, world and UI); the
targets that stage depend on the cook. The cook has no game classes: it reads the game's event
registry from `src/tools/script_cook/events.txt`, which `python3 src/tools/scripts/script_events.py`
regenerates from a bounded debug run (`ps2_scriptEvents`). Regenerate it after changing an
`idEventDef`; the PS2 refuses a program whose events differ from its own. `make
test-script-image` builds the `SCRIPT_CHECK=1` production variant (`build/<config>-scriptcheck`),
which compiles the retail script at boot as production used to, and requires the image the
cook's writer makes of that EE compile to equal the host cook's, byte for byte. CI cannot cook
the retail program, so `tools.yml` cooks `src/tools/script_cook/tests` (an authored script
covering folds, vectors, strings, objects, namespaces and events) and the two headless
fixtures on Linux, macOS and Windows and requires identical images; cook it by hand with
`build/tools/script_cook --events src/tools/script_cook/events.txt --source-dir
src/tools/script_cook/tests --output <file>`. The engine
reference's "Cooked script program" has the format and the host/EE traps.

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
message. `ps2_uiRetail draw shell` draws the original shell's start screen, and
`ps2_uiRetail menu` / `key NAME` open and drive the native menu (`make stage-ui-menu`);
screens below Campaign and Settings remain pending. To inspect the current
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
from its resident converted textures and pre-rendered glyphs, at about 60 ms per debug
PCSX2 frame; the earlier RGBA page slice took about 1.6 seconds per frame while its
pages streamed from HostFs.
`execute shell` selects the minimal carousel/Credits owner; `movie shell|dialog|none`
selects inactive inspection. `make test-ui` checks both and the rest of the UI (console,
fonts, drawn dialog and shell, menu, Campaign and Settings, quit dialog) in one session per
standard (`run_ui_session.py`), plus the invalid-resource cases in fresh processes
(`run_retail_ui.py`, `run_retail_movies.py`). All authored startup/map/UI
fixtures live under `src/tests/data/`; generated packages and captures stay in `build/`.

Production artifacts are `build/<config>/d3bfg.elf`, `d3bfg_unstripped.elf`,
`d3bfg.map` and `report.{json,txt}`. This executable links the full retained campaign
without sources or headers under `src/tests/` or fixture symbols; the gate link keeps every
object and the runnable is relinked with `--gc-sections`. Its
`main()` first finds the package through the [IOP boot](#console-boot-usb-and-hdd), keeping
the loader's IOP on `host:`, then initializes Common, native declarations,
key/console events, the static game API and `idGameLocal::Init`, plus CPU render-world
and silent sound bookkeeping. Common processes console events, commands, offline
services and native `RunFrame` on the calling thread. Headless mode retains approximately
60 Hz idle pacing; visible mode presents once per completed Common frame and uses GS
vsync without an additional sleep. The game remains in `NOMAP`, whose native frame
returns without advancing map simulation. Visible packages bring up the native shell menu, the DualShock pad and a debug USB
keyboard; map transitions remain pending. Startup checks `base/default.cfg` for existence and optionally executes
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
`_headless_default` material come from `src/tests/data/`; every retail declaration loads
from the containers, and the retail script program from the cooked
`base/script/doom_main.d3sp` (`make script-cook-retail`). `make run-headless-retail`
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
`backspace`. These deliver native character/key events; visible packages also poll the
DualShock pad and, when `ps2_keyboard` is set (off by default; put `set ps2_keyboard 1` in a
package's `d3bfg.cfg`), a debug USB keyboard, which post the same native events. Native Enter adds to RAM history; persistent history writes remain
disabled. Host `ps2_` control messages do not replace edit-field history.
`ps2_capture` accepts one pending ASCII alphanumeric/dash/underscore basename ending
in `.tga` (at most 63 bytes including the suffix). Quote names containing a dash,
for example `ps2_capture "console-edit.tga"`, because the native command lexer splits
unquoted dashes into separate tokens. It writes the next completed frame to the
package root. A capture queued with `quit` completes after the final native
draw and before UI/device shutdown. Visible `ps2_status` also reports presentation
and redraw-request counts. These three controls are available only in visible mode;
`ps2_maxFrames` and the ordinary command stream work in either mode.

`ps2_world` (visible mode too) drives Common's render world without a game map:
`load maps/NAME` takes `base/maps/NAME.d3wd` (from `make world-cook` or `world-cook-retail`, with
`base/maps/NAME.d3tx` beside it drawing its textures when there is one;
its materials must be declared in `base/materials/`, or id's declaration manager loads images
the PS2 refuses; `run_world_retail.py` declares the retail ones collision-only from the cook
report), `view EX EY EZ TX TY TZ` draws it every frame from the eye towards the
target under the console and screens (90-degree vertical field of view on the 4:3 picture, the
same framing in every display mode), `trace SX SY SZ EX
EY EZ` logs the first world surface between two points, `decal OX OY OZ DX DY DZ SIZE DEPTH
MATERIAL` projects a SIZE square along D onto the surfaces around O as the game's `ProjectDecal`
does (built when a view next sees them; materials without stages need `forceOverlays`), `decals`
logs what the entities hold (count, triangles, area, bounds), `portal HANDLE open|closed` opens a
portal or closes it as a door does (`PS_BLOCK_ALL`: view, locations and air), `off` stops drawing,
`unload` empties the world and `status` logs the areas, portals and the last view's area, entities,
drawn areas, batches and frame arena use (`load` also logs its time, `[D3BFG] WORLD load_time`);
three queries answer as the game's code would: `area X Y Z [X Y Z ...]` (up to 16 points) logs
`PointInArea` for each, `connected` logs `AreasAreConnected` as each area's group (the lowest area it
joins) for the view, location and air attributes, and `sound LX LY LZ SX SY SZ MAX` logs the distance
the sound world gives a sound at S heard at L (`idSoundWorldLocal::ResolveOrigin`'s walk through the
portals, a closed one adding `s_doorDistanceAdd`); `loader` logs the background loader's totals
since boot (bytes, reads, seeks, the time inside them and the rate) and the package's device;
`path FILE [FRAMES]` plays a camera path from `base/FILE` (lines of `FRAMES X Y Z YAW PITCH`, each
keyframe reached FRAMES frames after the one before, one step a Common frame whatever it took, so
every run draws the same frames; FRAMES limits the frames played) and logs `[D3BFG] WORLD path
done` at its end;
`textures on|off` draws the world textured (the default) or untextured, `lightmaps on|off` its
lightmap pass (the default) or the vertex chroma alone, and `textures status` logs the last pass's
texture arena, residents, uploads, evictions, untextured fallbacks, resident lightmap pages and
batches drawn without their page. `fill off|on|status` (builds with `PS2_D3BFG_GPU_SAMPLER`) is
the GS fill A/B: off, every world pass draws as usual into a one-pixel scissor (its sky box too), so
VU1 and the GIF do the pass's work while the GS fills next to nothing; the chain time that leaves
is the GS's pixel work.
Debug builds add a free-flight camera: `fly on` starts from the current view, then each frame the
pad's left stick moves along the view and sideways, the right stick turns, R1 and L1 rise and sink,
cross holds 4x speed and square a quarter (256 units a second at full deflection, 120 degrees a
second of turn, through walls: it moves the camera, not a player). Triangle logs the pose with a
`view=` line `ps2_world view` takes, and `fly status` logs it too. While flying, the pad posts no
key events, so it never reaches the console or a screen. `fly off` stops; `view`, `off`, `load`
and `unload` end or move the flight. `fly input LX LY RX RY BUTTONS FRAMES` scripts the sticks
(-128 to 127) and a libpad button mask for FRAMES frames of 1/30 s each, for the gate.
`make run-world-retail` (debug, after `make ui-cook`; `CONSOLE_STANDARD=pal` for PAL) stages mars_city1
under `build/debug/world-retail/` with a `console.txt` that closes the console, loads the map and
starts flying from the player start.

`ps2_framelog start [FRAMES] [FILE] | stop | status` (visible mode, builds with
`PS2_D3BFG_FRAME_LOG`) records one row per Common frame in EE memory (TAG_DEBUG, about 190 bytes a
row, 3,600 rows when FRAMES is left out, 16,384 at most) and writes them once to the package's FILE
(`framelog.txt`; a `.txt` name of letters, digits, `_`, `-` and `.`, quoted when it holds a dash)
when it stops, fills, or the camera path it was started with ends (`src/ps2/system/frame_log.cpp`).
The label notes the map, its load time, the standard, `pipeline=` and `fill=`. A row holds the
frame's EE time and its parts (game step, 3D views, screens, submit), the time spent waiting for
VU1 and the GS and for the vertical blank, the loader's time in reads, and the frame's batches,
vertices, chain bytes, texture uploads, streaming reads and misses and the eye. Builds with
`PS2_D3BFG_GPU_SAMPLER` split the GPU wait into what held the chain up while the EE waited
(`dma::GpuState`, read from VPU-STAT, VIF1_STAT and GIF_STAT each pass of the wait): `vu_us` (VU1
running), `gs_us` (VU1 stalled on an XGKICK, VIF1 waiting for the GIF, or PATH1 still feeding the GS),
`direct_us` (PATH2: texture uploads, the sky box, state), `vif_us` (VIF1 decoding or unpacking, or
waiting for the DMA) and `tail_us` (the GS's last primitives). With serial frames
(`ps2_pipelineFrames 0`) that is the whole chain; pipelined, only the part the EE's own work did not
cover. PCSX2 classifies everything as `vif_us` (its VU1 finishes at once, and its GIF reports no
path), so the split means something on a console only. The sampler also counts world_lm's list path,
which `dma::WaitIdle` reads out of VU1 memory (the frame constants' last qword) after each chain:
`list_batches` (batches with a vertex outside the guard band), `list_entries` (the stream entries
its passes walked, a lit batch's twice), `clips` (triangles sent to the clipper), `empty_clips`
(calls that sent nothing) and `rejects` (triangles dropped before the clipper, their corners all
outside one plane). These are exact in PCSX2 too, which runs the programs as the console does.
`src/tools/scripts/frame_log.py framelog.txt [--compare baseline.txt] [--vu-fit counts.txt]`
summarises a log: each stage's mean and percentiles, frames over the budgets and by whole fields,
the GPU split with VU1's cycles a vertex drawn (against `vu_cost.py`'s `cycles_per_vertex`), the
list path's totals, the slowest frames with where the camera was. `--vu-fit` takes a console's
serial log and a log of the same tour with the list path counts (PCSX2's), pairs the frames that
drew the same batches and vertices, and fits VU1's cycles a frame against the vertices, batches, lit
batches, list entries, clipper calls and empty calls. `ps2_memory LABEL` logs the allocation
ledgers under LABEL (MEMORY, ARENA, HEAP, and in tracking builds STACK and TAG lines); run
`clearHistory` first, since the console history keeps a buffer per HostFs line.

**Console perf run.** `make BUILD=release stage-perf-world-retail` (after `make ui-cook`;
`CONSOLE_STANDARD=pal` for PAL) stages `build/release/perf-world-retail/`, whose `base/d3bfg.cfg`
closes the console and plays the whole camera tour (`config/world_mars_city1_tour.path`, 7,244
frames, generated from the `.bproc` by `src/tools/scripts/world_tour.py`) three times, loading
mars_city1 afresh for each (`PERF_RUNS` in `run_world_retail.py`):
- `framelog.txt`: pipelined frames, what a frame costs as the game runs;
- `framelog_serial.txt`: serial frames (`ps2_pipelineFrames 0`), whose GPU split covers every chain
  whole;
- `framelog_nofill.txt`: serial with `ps2_world fill off`, whose chain time against the serial run's
  is the GS's pixel work.

Each ends on a console line `Camera path done after 7244 frames; frame log written to <file> in
<device>`. Copy the package folder to a USB drive or the hard disk as for the console boot (below),
boot its `d3bfg.elf`, wait for the third line (about twelve minutes), and read the three logs back
from the package folder. Compare them with `src/tools/scripts/frame_log.py framelog_serial.txt
--compare framelog_nofill.txt`, and a run with an earlier one's log the same way. The same package
runs in PCSX2 (`make BUILD=release run-perf-world-retail`), whose timings say nothing about a
console's.
Close the console (`ps2_consoleKey toggle`) to see the whole view; `make test-world` stages
the graybox and runs it. Production draws 640 wide by default. `ps2_frameBufferBits
16/16|32/16|32/32` sets the in-game frame-buffer profile (draw/display bits; 32/16 draws in
32 bits and shows a dithered 16-bit copy), and the front end, with no map loaded, draws a 32/32
preference as 32/16; `ps2_resolution 0|1` switches between 512 and 640 wide and `ps2_interlace
field|frame` between the full-height FIELD buffer and a half-height FRAME one, each before the
next frame by restarting the GS (one blank frame). Loading a world with `ps2_world` counts as a
map. Capture lines report the captured draw buffer's bits as `bits=` and the TGA carries the
size ([CVARS.md](CVARS.md)).

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

## Console boot (USB and HDD)

`ps2::system::iop::Boot` (`src/ps2/system/iop_boot.cpp`, adapted from `quake2-ps2`) runs
first in `main` and returns the package root, the folder holding `base/default.cfg`, which
`ps2::system::runtime::PackageRoot()` then gives to `fs_basepath`, `presentation.cfg`, captures
and `Sys_LaunchPath`:

- **`host:`** (the emulator's HostFs) is probed first and keeps the IOP as the loader left it,
  with the ROM's FILEIO serving files. Every production gate takes this path.
- **Any other boot resets the IOP**, since a launcher's modules are no base to build on, then
  starts iomanX and fileXio, the hard disk's DEV9/ATAD/APA/PFS drivers (skipped when the ELF
  was launched from `mass:`), then BDM, FatFs, usbd and usbmass_bd, and waits up to 10 s for
  a USB volume. The ELF's folder (from the loader's `argv[0]`, at most 64 characters) is
  searched before the drive's root: on the launch partition, the other main PFS partitions,
  then `mass0:` to `mass9:`. A found partition stays mounted at `pfs0:`. fileXio then serves
  the C library's files, with a 64 KB read buffer.
- **The storage drivers come from EE memory**: after the reset nothing can read the drive
  until they run. `iop_modules.S` embeds the SDK's ten IRX files (190,784 bytes) in the
  `.bootdata` section, which `config/linkfile.ld` places in its own segment at the first
  page past `_end`. The heap's arena starts on it and holds it as its first block until the
  boot calls `heap::ReleaseBootData()`, on `host:` too. Drivers started later are loose
  files in the package's `irx/`: `usbd` (unless the USB boot started it) and `ps2kbd`. The EE
  reads each one and starts it from memory (`SifExecModuleBuffer`, LOADFILE patched first on
  `host:`): LOADFILE reading a USB drive itself overflowed its thread's 2 KB stack. The pad uses
  the ROM's SIO2MAN and PADMAN; the boot waits at most 3 s for PADMAN's RPC server before libpad's
  `padInit`, whose own bind never gives up. Each module start and device open logs a line first.
- What the boot did is logged as `[D3BFG] IOP` lines and, after a reset, printed into the
  engine console (`IOP boot:`), a console's only record. No package found is fatal with the
  launch path, the folders searched, the hard disk's state and the mounted USB volumes, which a
  debug build shows on its crash screen (below); a release build only logs it. `console.txt` is
  polled on `host:` only, so a console package puts its commands in `base/d3bfg.cfg`.
- **A debug build's log on a console** (`ps2/debug/log_mirror`): every `ps2::LogV` line also goes
  to the screen from the first line of `main` until `dma::Init` (the renderer) or the main loop
  starts, white, warnings yellow, errors red, so a hang leaves its last line on the TV; and to
  `d3bfg.log` in the package folder, emptied at each boot, with the lines logged before the
  drive was found written first. Each line opens, appends and closes the file (the only flush
  FatFs has on USB; a package on `pfs0:` also syncs), about 2.5 s more to the main loop in
  PCSX2's emulated USB. `host:` boots write no file. Release builds have neither.

The USB path runs on a console and in PCSX2's emulated USB storage; the HDD path is untested
([production.md](testing/production.md#console-storage-boot)). Writes to a PFS partition
are synced (`fileXioSync`) only by a debug build's log file.

## Third-party dependencies

- Imported third-party dependencies live in `src/external/`: the VU compiler submodule below.
  The EE heap is the port's own (`src/ps2/system/heap*`, which replaced dlmalloc).
  No JPEG or zlib is linked: images arrive converted by the asset pipeline, ZIP files are
  unsupported, and source-SWF inflation and pipelined save compression fail by name.
  Compressed resource packs will import `src/external/miniz`, whose zlib-compatible API
  can then restore save compression.
  The VU compiler is a pinned submodule: `src/external/oxide-vcl`
  (`git submodule update --init` after cloning). `make` builds its oxide-vusl with cargo into
  `build/tools/oxide-vcl/` (never a copy on `PATH`) and compiles the VUSL programs in
  `config/sources.mk`'s `VUSL_FILES` (`src/ps2/renderer/vu1progs/`) into `build/vu/`, each with
  a `<name>.vusl.h` header; `ps2/renderer/vu.h` includes those, declares the programs' symbols
  and checks the EE's layouts against them.
- IOP modules come from the installed SDK's `$PS2SDK/iop/irx`, none of them vendored: the
  storage drivers are `.incbin`'d into production's boot data (`IOP_BOOT_MODULES` in the
  Makefile, built once into `build/iop/`), and visible packages copy `usbd.irx` and
  `ps2kbd.irx` into their `irx/`. Every EE image links with `config/linkfile.ld`, the SDK's
  linkfile plus the boot data segment; keep it in step when the pinned SDK moves.

## Host tools, source groups and link policy

- `make resource-unpack` builds the standalone C++20 host extractor at
  `build/tools/resource_unpack`, using the native resource layout without engine/SDK
  linkage. The host tools are a CMake 3.24+ project, `src/tools/CMakeLists.txt`, which owns
  their flags on every host (MSVC included); make configures it into `build/tools/cmake/`
  and starts that tree afresh when `HOST_CXX` changes. `--list` validates metadata;
  `--output DIR` streams a merged directory tree, with later input containers/records
  winning. Existing files require `--overwrite`. Keep retail output ignored.
  `make test-host` exercises its separate sanitizer build; this host-only tool does not
  change native asset providers or boot capability.
- `make asset-reexport` builds `build/tools/asset_reexport`, the C++20 converter of retail
  textures to paletted GSTX (`src/tools/assets/gstx.h`): PSMT8/PSMT4 pages, shared CLUTs,
  RLE and placements mapping each image's logical texels. It also renders a font's glyphs
  at exact on-screen pixel sizes into a glyph-set texture (`assets/glyph_set.*`). It and
  `resource_unpack` share the `src/tools/assets/` library (resources, BIMAGE/DXT, palettes,
  packing, GSTX), meant for every later texture re-export. Output is byte-identical on any
  host (contraction off). Its `world` command cooks a text `.proc` into a D3WD world file
  (`assets/world.h`: areas, portals, nodes, VU1 batch packets, CPU mirror); `make
  world-cook` cooks the authored graybox map, generated with its `.map`/`.proc` by
  `src/tests/host/scripts/graybox_fixture.py` (update the generator, never the outputs).
  The world gates' reference pixels (`graybox_map/scene_pixels.json`) come from
  `src/tools/scripts/world_reference.py`, which rasterizes the `.proc` from the gates' cameras;
  run it after changing the map, the cook job's colours or a camera (`make test-host` fails
  on a stale digest), and use its `--compare` to match a whole capture with the reference
  (`--scene mars_city1` rasterizes the retail `.bproc`, `--camera` takes any view).
- `make script-cook` / `script-cook-retail` build `build/tools/script_cook` ("Script cook"
  above). It compiles id's sources with their warnings off, as they are exempt from the strict
  set on the EE, and holds its own to the tools' set.
- `PS2_D3BFG_DEBUG`, `PS2_D3BFG_ASSERTS`, `PS2_D3BFG_PROFILE` and `PS2_D3BFG_TESTS` are always 0 or 1.
  Test them with `#if`, never `#ifdef` (`-Wundef` is on). So are the instrumentation defines in the
  Makefile's `INSTRUMENT_DEFS`, on in both configurations for now: `PS2_D3BFG_FRAME_LOG` (the frame
  log and the counters only it reads) and `PS2_D3BFG_GPU_SAMPLER` (the GPU sampler and
  `ps2_world fill`). A Profile build that keeps them and a Release without them are planned
  ([IMPLEMENTATION_PLAN.md](plans/IMPLEMENTATION_PLAN.md#profile-build-todo-2026-11-26)). Build a
  combination with `make BUILD=release OUTPUT_DIR=build/<dir> INSTRUMENT_DEFS="-DPS2_D3BFG_FRAME_LOG=0
  -DPS2_D3BFG_GPU_SAMPLER=0"`.
- Source lists in `config/sources.mk` are explicit and audited. Register new backend
  files in `PS2_CXX_SRC` or the relevant core group; cold sources also go in
  `SIZE_OPT_CXX_SRC`. Both configurations build at `-O2`; id's load/init/command-time
  sources listed in `ENGINE_SIZE_OPT_CXX_SRC` build at `-Os` (`-Oz` is identical on the EE). Run `make compiledb` after changing source lists. Flag stamps
  invalidate affected object groups automatically.
- `GAME_BACKEND_CXX_SRC` contains required campaign adapters compiled strictly into
  production; `RESIDENT_SUPPORT_CXX_SRC` supplies strict shared providers;
  `PS2_CXX_SRC` owns production entry, `CORE_TEST_DRIVER_CXX_SRC` and `GAME_TEST_CXX_SRC`
  own test entries/adapters. `PLATFORM_TEST_CXX_SRC` supplies the SDK-only core-stage
  skip provider. Make selects test stages through source lists; `PS2_D3BFG_TESTS` is
  the sole test-enable define. Keep fixture hooks behind it and separate
  campaign/test object trees. The production link rejects test inputs and symbols. The source
  inventory records the deleted upstream JPEG/zlib trees, whose units id's project files
  still name.

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
core, heap and assertion failures terminate through `FatalError` / `FatalErrorV`; assertion
builds then report on screen ([crash reports](#smoke-tests-and-diagnostics)), release builds
`Exit(1)`. `src/ps2/common.h` provides `PS2_Assert`,
`PS2_AssertMsg`, `ArrayLength` and portable format/cold attributes for new backend code.

The fixture adapter streams loose files through real `idFile_Permanent` objects;
explicit bulk/memory reads stay bounded to 64 KiB. It rejects parent, absolute and
other-device paths. Shared system services supply directory filters, parent creation,
and write/append. On `host:` the one-time ROM FILEIO patch protects
getstat/dread DMA and removal fallthrough without resetting the loader's IOP; a console
boot's files go through fileXio instead. Native
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

`ps2::system::heap` is the EE's only allocator ([HEAP_ALLOCATOR.md](plans/HEAP_ALLOCATOR.md)).
crt0's `_ps2sdk_memory_init` hook hands it all of sbrk's range before libcglue or any
constructor runs. In production the arena starts a page lower, on the linker's boot data,
held as its first large block until the IOP boot releases it (`heap::ReleaseBootData`); it
never enters the ledger. Requests up to 256 bytes take headerless slots in 4 KiB pages of 48 size
classes, carved down from the arena's top; larger ones take TLSF blocks with an 8-byte
header, carved up from its bottom. Doom's tagged APIs, every C++ allocation form and the
C/newlib entry points (including the reentrant `_r` variants) share it; the C library's
allocations are counted in their own ledger (`GetLibcStats`).

Alignment is the size's natural one (its lowest set bit after rounding to 4, capped at 16)
for `new` and tagged `new`, 16 for `Mem_Alloc`, at least 8 for `malloc`, and any power of two
on request. Tracking builds (`PS2_D3BFG_ASSERTS`) keep each allocation's tag and requested
size in the tiers' own records, poison freed memory (`0xDEADBEEF`) and fresh memory
(`0xCDCDCDCD`), and stop on a write after free, a double or foreign free, a zero-size `new`,
a tagged allocation before `heap::EnterMain()` (every EE main calls it first; a global whose
constructor allocates needs `ps2::DeferredObject` storage) or use from another thread.
Release builds check nothing, keep no per-tag ledger and count backing bytes as requested.
The heap serves a single calling thread.

The main stack sits right above the arena, under the heap's first small pages. `heap.cpp`
sets crt0's `_stack_size` to 256 KiB (`heap::kMainStackBytes`; the retail menus peak at about
154 KB). Tracking builds paint the stack in `EnterMain`, stop at its bottom 1 KiB on every
Common frame (`heap::CheckStack`) and print the untouched headroom in `STACK` lines.

Nothing allocates before `main`, so every executable starts and ends at a 0 / 0 / 0
tagged ledger and the shutdown tests require exactly that.

Per-tag (tracking builds) and total reports track current and peak requested/backing bytes.
Backing is the slot's class size or the block's size, header included. `GetArenaStats` adds
the tiers: commitment (the arena less the gap between them), free pages, borrowed pages, the
largest free block and tracking metadata. The ELF report, arena commitment, stack usage and
GS VRAM usage are separate parts of the memory budget. Stripping symbols does not reduce
loaded code/data/BSS.

## Smoke tests and diagnostics

The runner stages a unique manifest, authored fixture and matching ELF/symbols under
`build/test-results/<run-id>/`. It uses the installed PCSX2 configuration, requires
HostFs and IOP/file logging to be enabled already, and refuses to launch while another
PCSX2 session is running. It selects a separate log with `-logfile` and stops only the
process it started. It does not edit emulator settings.

```sh
make test-platform
make test-core
make test-common
make BUILD=release test-common
make test-game
make BUILD=release test-game
```

Build the corresponding test image (`make build-platform`, `build-core` or `build-game`) first. A pass requires the fresh run identity,
scenario-specific stage markers and a closed structured result; process exit alone cannot
pass. A watchdog or crash diagnostic fails the run. Negative probes require their specific first fatal. Tests cover heap alignment/accounting, scalar matrix
and vertex formats, lexer/string behavior and synchronous job ordering. Core services
also exercise command/cvar registration and fixture I/O.

The full scenario lists and accepted debug/release runs are maintained in
[PORT_STATUS](PORT_STATUS.md), the [PCSX2 testing rules](../.claude/rules/testing-pcsx2.md) and the
[per-gate procedures](testing/).
The runner checks the installed HostFs/logging configuration; optional USB keyboard
attachment (`[USB1] Type = hidkbd`) is read by visible packages only.

For a reported EE address, use the symbols from the exact production or test build:

```sh
mips64r5900el-ps2-elf-addr2line -f -C -e build/debug/d3bfg_unstripped.elf 0xADDRESS
```

Use the archived `d3bfg_tests_unstripped.elf` for a test executable.

**Crash reports (assertion builds only, `src/ps2/debug/`, from `quake2-ps2`).** A fatal error
(`FatalErrorV`: assertions, `Sys_Error`, heap failures) or an EE CPU exception logs the message,
then the unwound call stack as `#N 0xADDRESS` lines between `STACK TRACE` banners, and draws the
message and the stack in red on a debug screen that takes the GS over by hand (`scr_print`), in the
standard `gs::Init` last ran, or the ROM's. The program then halts there; the runners stop the
emulator at the `[D3BFG] FATAL` line, which an exception report starts with too. Every EE `main`
installs libeedebug's level 1 exception handlers first (`[D3BFG] DEBUG EE exception handlers
installed`). Resolve a log, or the addresses read off the screen, with
`python3 src/tools/scripts/symbolize.py --elf build/debug/d3bfg_unstripped.elf < log`. The
walker (`stack_trace`) reads each frame from its function's prologue, decoding a caller's frame
from its call, since a `[[noreturn]]` function's return address is the next function's first
instruction. Production's `ps2_crash fatal|assert|trap|null` raises each kind; PCSX2 raises the
trap but skips the null store, which faults on a console. Release builds compile none of it and
keep `Exit(1)`; their link is unchanged.

**The HostFs flake (PCSX2, seen in `quake2-ps2`).** A `TLB Miss` in `_request_end` (ps2sdk
`sifrpc.c`, `SIF_CMD_RPC_END`) during `host:` file I/O is never game code: one to three
`TLB Miss, pc=<same> addr=0x10|0x18 [load|store]` lines, `cd->hdr.pkt_addr` already null because
one RPC completed twice under PCSX2's HostFs. Its other face is a wild `pc` below `.text`
(0x100000), such as `pc=0x82000 addr=0x0`, usually fatal. The `pc` moves per build: resolve it
with `mips64r5900el-ps2-elf-addr2line -f -e <unstripped elf> <pc>` before deciding. Re-run
before investigating: across three identical Quake II 39-map cycles, one died, one was clean and
one logged it and still finished. VIF1 chains only read RAM, so the renderer cannot cause it, and
a capture from a run that logged it stays valid.

The standalone symbolization helper lives in `src/tools/scripts/`.

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
