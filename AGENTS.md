# Doom 3 BFG Edition for the PlayStation 2

id's Doom 3 BFG (forked from https://github.com/id-Software/DOOM-3-BFG)
is being ported to the PS2 with the free ps2dev SDK. The source split is:

- **`src/neo/`**: id's original C++ code, kept as close to unmodified as possible.
  Every change is tagged `// [PS2_D3BFG]: <why>`.
- **`src/ps2/`**: the console backend, all new C++20 (no exceptions, no RTTI,
  strict `-Werror`). The mechanical M0 move is committed separately from portability edits.

File-scoped backend variables and constants with internal linkage always use an explicit
`static`, including inside anonymous namespaces (e.g. `static constexpr` for constants).
Backend diagnostics go through `ps2::Log` / `LogV`; fatal failures use `FatalError` /
`FatalErrorV`. `src/ps2/common.h` provides `PS2_Assert` / `PS2_AssertMsg` for new
preconditions, `ArrayLength`, and portable format/cold function macros. Preserve runtime
validation in release builds. Reused Quake II backend code is attributed in README.md;
new file banners use `This source code is released under the GNU GPL-3.0-or-later license.`

Backend namespaces follow `ps2::module::subsystem`, the module being the `src/ps2/` folder
(`ps2::renderer::gs`, `ps2::system::heap`, `ps2::ui::swf`). A subsystem spread over several
files keeps one namespace; merge related code rather than adding a namespace per file.
Code shared within a module, or without a clear subsystem, lives at `ps2::module`, and code
every module shares at `ps2`. UI code lives in `src/ps2/ui/`. Tests use `ps2::tests`. See
[ps2-backend-cpp.md](.claude/rules/ps2-backend-cpp.md#naming).

New C++ code, including host tools, places block braces on their own lines. Always
brace conditional bodies and expand blocks across multiple lines, including short
functions. Leave a blank line after a completed block before the next independent
statement or block; keep the code visually spaced rather than densely packed.

[IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md) describes the proposed milestones and
source changes. [README.md](README.md) records current project status; expand it as the
port develops. [AGENT_HANDOVER.md](docs/AGENT_HANDOVER.md) is the current continuation guide.
[CVARS.md](docs/CVARS.md) lists implemented backend cvars; add each one with
its debug/release defaults and flags when it exists in code. The files in
[.claude/rules/](.claude/rules/) include Quake II PS2 reference notes. Their Quake source
paths, test results and runtime behavior are not yet Doom 3 implementation facts.

## Toolchain

- EE compiler: `mips64r5900el-ps2-elf-gcc`/`g++` (GCC 15). There is no `ee-gcc`/`ee-g++`, so
  don't go looking for them. VU tools: `openvcl`, `dvp-as`. Also `bin2c`.
- `$PS2DEV` = `~/ps2dev`, `$PS2SDK` = `~/ps2dev/ps2sdk` (EE headers in `ee/include`). The
  SDK's C sources, for checking what a library really does, are under
  `~/ps2dev/src/ps2dev/build/ps2sdk/ee/<lib>/src/`. gsKit is at `~/ps2dev/gsKit`.
- Imported third-party dependencies live in `src/external/`, currently `dlmalloc`, the bundled engine-modified JPEG 6, and zlib 1.2.3.
  Codec sources use separate explicit vendor groups and tagged backend allocator hooks.
  Planned VU/tool dependencies from the reference are `src/external/vclpp` (and its
  nested `external/parse-utils`), `src/external/vu-checker`, and possibly
  `src/external/miniz`.
  These VU/tool dependencies are not imported or pinned yet. When VU code is added,
  build the pinned vclpp into `build/tools/vclpp` instead of relying on a copy on `PATH`.

## Build and verify

- `make` compiles and links the production native console with the real EE compiler.
  `make release` selects release; the artifacts are `build/<config>/d3bfg.elf` and
  matching `d3bfg_unstripped.elf`, map and resident `report.{json,txt}`. The active Makefile owns the Doom source lists; the historical copied Quake II
  Makefile was removed. Use the external Quake II checkout for build comparisons.
- `make headless` packages production with authored data under `build/<config>/headless/`;
  `make run` / `make run-headless` launch it. The native game stays in `NOMAP` while Common
  pumps console commands and native frames. `console.txt` accepts append-only lines via
  HostFs; `ps2_maxFrames=0` runs until quit. `make test-headless` checks 120 frames,
  live command input and full shutdown without linking fixtures into production.
  `make retail-boot` / `make run-retail-boot` stage/run the same ELF with the local retail
  `_common`/`_ordered` containers hard-linked from `RETAIL_DATA_ROOT` (symlinks are not
  listed). `make test-retail-boot` checks every retail declaration and the full
  `doom_main.script` reaching `NOMAP`, and records the tagged `[D3BFG] MEMORY startup-*`
  ledgers production prints after foundation, declaration and game startup.
  `make console` / `make run-console` stage/run the same ELF visibly under
  `build/<config>/console/`; `CONSOLE_STANDARD=ntsc` (default) or `pal` selects the mode.
  Every visible package also stages the cooked retail catalog and assets from
  `UI_COOK_OUTPUT` (run `make ui-cook` first): the presentation opens the catalog at
  startup and the console draws the original `textures/bigchars` charset from it.
  Package-root `presentation.cfg` selects `headless`/`ntsc`/`pal` before Common startup;
  a missing file defaults to headless, and `ntsc menu`/`pal menu` also open the native
  menu at startup. `make test-console` checks native console drawing,
  edit/history events, close/reopen and final capture/quit in both standards.
  HostFs `ps2_consoleText`, `ps2_consoleKey` and `ps2_capture` are visible-mode controls.
  Common frames own exactly one native console presentation; `UpdateScreen` coalesces
  redraw requests during a frame and never advances simulation. Finish queued capture
  and final presentation before UI/device shutdown; visible vsync adds no headless sleep.
  `make ui` / `make run-ui` stage/run the same ELF with the authored native SWF under
  `build/<config>/ui/`, using `CONSOLE_STANDARD` for NTSC/PAL. `make test-ui` checks native
  loading, ASCII `idFont`, timeline color/alpha, AVM1 focus/select, capture and reloads.
  The authored page-binding gate passes debug/release NTSC/PAL: it checks 94 pixels
  (outlines, color terms, screen blending and depth masks among them), shared cache
  budgets and twelve invalid-resource cases, including deferred pixel corruption. A
  sanitizer cache harness also exercises assertions 0/1.
  `ps2_ui` controls load/unload/reload/status/pause/resume; `ps2_uiKey tab`/`enter` supply
  native SWF key events while the console is closed. Console input has priority; its
  toggle opens/closes it over the movie. The game stays in `NOMAP`. Authored assets and
  their compiled exact-byte admission profile come from `build_authored_ui.py`; use
  `--check` to detect stale outputs before staging. Its UIPG header/table descriptors
  are compiled too, and streamed page pixels are checked before cache replacement.
  `make retail-ui` / `make run-retail-ui` stage/run the opt-in English retail font
  screen from the local `make ui-cook` output, using `CONSOLE_STANDARD` and `BUILD`.
  `make test-retail-ui` checks five real native fonts at 12/24/48-point scales, full
  legacy layout metrics, source-derived pixels, 120 frames per family, return/reload,
  shutdown and five corrupt-resource cases. HostFs `ps2_uiRetail` accepts load/unload/
  reload/status or `font INDEX` (0–4). Start in a fresh visible process; the retail
  screens and the authored movie are separate screens of one catalog-backed image
  profile. No retail SWF or Shell is
  loaded by that font-only mode. The separate movie profile below admits bounded
  dictionaries, English text and timeline execution; full drawing and offline Shell
  integration remain pending.
  `make retail-movies` / `make run-retail-movies` stage/run original shell/dialog BSWFs
  and the complete English dictionary under `build/<config>/ui-retail-movies/`.
  `ps2_uiRetail movie shell|dialog|none` selects inactive native dictionaries and
  isolated English text layout. `make test-retail-movies` checks native storage/layout,
  warm reloads, resident textures, full cleanup and malformed resources in both standards.
  Resource-only constructors never run imported AVM1/placements; inactive inspection
  rejects Activate and timeline advancement; only the native menu admits key events. Native
  dictionary shutdown releases retained localization keys/pools after movies/fonts.
  `ps2_uiRetail execute shell|dialog` separately admits placement/AVM1 operands and
  executes native timelines before presentation, without full movie drawing or event routing.
  The shell borrows a minimal native widget owner: the original carousel has five slots
  with only the admitted Doom 3 cover, and the original Credits screen owns its callback.
  Callback identity, signatures and destructive ancestor seeks are checked in release too.
  `make test-retail-timelines` checks executable movies; `make test-retail-movies` also
  checks inactive inspection and malformed resources. `ps2_uiRetail dialog quit` draws the
  original dialog in full through native `Render`, showing Common's quit message, and
  `ps2_uiRetail draw shell` draws the original shell's start screen; full drawing is a
  third admission, refusing only additive alpha terms. `ps2_uiRetail logo on|off` plays the
  native shell's logo transition and `ps2_uiRetail clock fixed` advances executing movies
  1/60 s per Common frame. `make test-retail-dialog` and `make test-retail-shell` check
  them. `ps2_uiRetail menu` opens the menu through the native `idMenuHandler_Shell`
  (start, root and credits screens bound; PS3 platform layout). Common routes events the
  console and authored movie leave to it, as `MenuEvent` does; key events are a fourth
  admission, and each native handler a key reaches must be a widget event of the menu's
  own live widgets. `ps2_uiRetail key NAME` posts a keyboard key (enter, escape, arrows)
  or pad button (cross, circle, dpad_*) through the event loop: accept signs in and shows
  the root screen, the arrows move its focus and back returns to press-start. The
  Campaign (New Game, Load Game; no save to continue) and Settings (Controls, Game
  Options, System) screens open from the root and return to it; a move into any screen
  below them fails by name. The menu also initializes Common's
  dialog manager, which opens the admitted dialog movie inactive beside the shell (retail
  admission is per movie): the root's Quit shows the native confirmation, an active dialog
  takes events first, its choices are pinned by identity, and accept quits through
  Common. `make retail-menu` / `make run-retail-menu` stage/run a package whose policy
  boots into the menu, before the startup commands, on real time. `make test-retail-menu`
  checks all four (`menu-render`, `menu-quit`, `menu-boot`, `menu-screens`).
  Retail text draws pad button tags (`<JOY1>`) as the cooked PS3 prompt art, which a
  drawn movie loads when admitted. Converted pages stay resident: a debug PCSX2 frame
  takes about 60 ms for the dialog and 100–140 ms for the shell on the fixed clock. The
  other screens, physical keyboard/pad input, dynamic resources and artwork residency
  remain pending.
- `make test-renderer` checks the CPU-only GS device in the separate core test ELF:
  NTSC/PAL solid/textured alpha frames, native TGA pixels and timeout recovery,
  native drawing adapters/ASCII glyphs, indexed rotated/clipped geometry and per-vertex
  RGB/alpha, additive color, screen and line passes and the depth mask, streamed UIPG pages
  with triangle splits and bounded cache pressure, context flips, residency/buffer pressure,
  reload/cleanup and seventeen bounds/state/deadline failures. `make run-renderer` captures
  and holds the NTSC scene for inspection.
  Production optionally presents the real native console and authored native SWF through
  Common; retail menu/Shell providers remain pending. `ui::Init/Shutdown` explicitly own
  native 2D capability and the white intrinsic after/before the caller-owned `renderer`
  device; the console charset comes from the retail catalog in production and from the
  authored fixture (`tests/smoketests/console_charset.*`) in the core test image. Low-level GS
  readiness alone never changes `R_IsInitialized`, and OpenGL stays unavailable.
  UI collection owns 4,096 vertices, 6,144 indices and 128 ordered draw runs. Fill returned
  write-only spans before the next allocation/draw; flushing or ending a frame recycles
  them. Native vertex-cache/static/joint buffers remain unavailable. Half UVs are decoded
  from checked bits, and `color2` requires exactly 128 in all four channels outside the
  scoped native SWF path, which decodes RGB color terms (127 and 128 are zero) into a
  fog-colored second pass; additive alpha terms stay unavailable. Native SWF blend 8 maps
  to alpha-weighted GS addition and blend 4 to the PC's `Cd + Cd × S`, one channel per pass
  with channel CLUTs; ordinary ONE/ONE state outside that scope stays unavailable. Line
  records draw as GS lines. Native SWF masks collect at most 64 fill triangles into one
  convex boundary of at most 16 points, with four active keyed masks clipped on the CPU;
  any other mask (concave, holed, disconnected, stroked, up to 256 triangles) is written to
  the depth buffer, one at a time. Flush before every mask mutation; empty masks clip all
  content. Every primitive writes TEST with ZTE 1, as the GS manual requires.
  Native `idImage` keeps logical atlas dimensions while SWF bitmap and
  ASCII font triangles use shared page dispatch. The authored 272×256 movie bitmap
  crosses x=254; the font retains its 512×128 atlas. Both images share one `PageCache`
  with four nearest/clamp slots (1 MiB), one 256 KiB scratch page and 56 bytes of page
  records. Images/cache remain warm across movie unload/reload. Shut down movies before
  fonts, close every page image before its cache, and release images/cache before the device.
  The device owns tagged command halves/pixels and uses `draw`, `graph`, `packet2`, `dma`.
  Preserve release preflight/deadlines, VIF/GIF ownership and immediate final presentation.
- `make compile-core` checks the explicit scalar idlib/framework source set.
  `make headless-core` links staged foundation startup/tests as
  `build/<config>/tests/core/d3bfg_tests.elf`; production links no test code.
  `make test-common` checks partial shutdown and offline capability failures in fresh
  emulator processes; Common startup is one-shot after static CVar registration.
  `make compile-game` is the separate M2b campaign compile gate. `make link-game`
  validates the retained-object resident link, registrations and map coverage under
  `build/<config>/`. `make headless-game` separately builds
  `build/<config>/tests/game/d3bfg_tests.elf`;
  `make test-game` checks native startup, entity/script ticks, bounded native `idPlayer`
  startup/commands and reloads, plus missing-map, script-error, restricted geometry/material
  and player spawn/script/command/state failures in fresh emulator processes.
  Fixed floor/wall/low-roof/step collision and native physics wall-stop, gravity, floor-rest and
  grounded-sliding probes are covered, along with native player physics driven by
  explicitly injected user commands, including jumping/landing, jump release and
  crouch/standing collision shapes. The native `idPlayer` also covers jump/crouch
  commands, crouch speed/view interpolation, authored actor-state transitions,
  low-ceiling passage, blocked standing restoration and bounded step-up/high-rise blocking;
  `make test-map` uses native `InitFromNewMap` with one authored floor, logical
  world/model/PVS queries, AAS routing and one idle native AI over three 60-frame
  reloads. Rendering and playback stay inactive; a native empty sound world provides
  startup bookkeeping. Full campaign player startup, animated AI and general campaign
  maps remain later gates. Preserve headless simulation while the selected 2D renderer/
  console/menu roadmap progresses; sound and world rendering follow later.
- New backend/test sources pass strict warnings and `-Werror`; legacy warning policy is
  documented in [docs/BUILD_INVENTORY.md](docs/BUILD_INVENTORY.md). Host runtime tests
  cannot replace target compilation. Check Make's exit status, including VU tool failures.
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
  host (contraction off).
- `make ui-inventory` / `make ui-cook` inspect/cook the local retail shell/dialog profile
  in `config/ui_retail.json`; override `UI_DATA_ROOT` and `UI_COOK_OUTPUT` as needed.
  Output must stay under `build/`. The manifest records hashes, dependencies, feature
  gaps, full English font/language data and the converted GSTX runtime images; the
  profile's conversion policy (first pass: everything halved, 16-level English-only
  font glyphs; `texture_policies` keeps the console charset at full resolution) drives
  `asset_reexport`, and `UI_PREVIEWS=DIR` adds original/converted
  PNGs for a visual review. Initial UI localization supports English only. Native
  authored SWF/font images share a fixed `PageCache` of UIPG pages; standalone
  `PageImage` callers may own a private cache. The cook also emits a bounded UICT4
  resource index carrying each texture's GSTX header and tables and each glyph set's
  table. Native snapshots validate its offsets/paths/English records and font counts
  before legacy parsing; constant UI material stages are generated from admitted
  bindings. A retail `PalettedImage` admits exactly the cataloged file, checks every
  CLUT and page while loading and keeps them resident in the texture owner; a triangle
  samples the placement holding its UV centroid, region-clamped. The profile's
  `glyph_sets` list sizes (pixels per atlas texel, outline weight) whose glyphs and
  outlines are pre-rendered; a tagged `RenderEditText` hook draws them texel for texel
  at the native layout position, keeps native quads for any other size and logs each
  such size once (`UI glyph size ... prerendered=no`). The dialog's and the shell start
  screen's sizes are cooked (Sarasori and Arial Narrow, in each standard). A set image
  holds one 1:1 band placement per page; each catalog entry carries its glyph's rectangle.
  `runtime_ready=false` is intentional: the shell's later screens, additive alpha terms,
  active artwork residency and offline Shell integration remain later gates. Scoped
  dictionary/text/timeline admission exists. See
  [docs/RETAIL_UI_PIPELINE.md](docs/RETAIL_UI_PIPELINE.md).
- `PS2_D3BFG_DEBUG`, `PS2_D3BFG_ASSERTS`, `PS2_D3BFG_PROFILE` and `PS2_D3BFG_TESTS` are always 0 or 1.
  Test them with `#if`, never `#ifdef` (`-Wundef` is on).
- Source lists in `config/sources.mk` are explicit and audited. Register new backend
  files in `PS2_CXX_SRC` or the relevant core group; cold sources also go in
  `SIZE_OPT_CXX_SRC`. Run `make compiledb` after changing source lists. Flag stamps
  invalidate affected object groups automatically.
- `GAME_BACKEND_CXX_SRC` contains required campaign adapters compiled strictly by
  `compile-game`; `RESIDENT_SUPPORT_CXX_SRC` supplies strict shared providers;
  `PS2_CXX_SRC` owns production entry, `CORE_TEST_DRIVER_CXX_SRC` and `GAME_TEST_CXX_SRC`
  own test entries/adapters. `PLATFORM_TEST_CXX_SRC` supplies the SDK-only core-stage
  skip provider. Make selects test stages through source lists; `PS2_D3BFG_TESTS` is
  the sole test-enable define. Keep fixture hooks behind it and separate
  campaign/test object trees. The production link rejects test inputs and symbols. `VENDOR_C_SRC` / `VENDOR_CXX_SRC` enable only audited relocated codec units. The source
  inventory maps their original upstream project paths. Host fixture lists cannot enable
  additional vendor sources in an EE runtime target.
- Doom smoke procedures and the separately labeled Quake reference are in
  [.claude/rules/testing-pcsx2.md](.claude/rules/testing-pcsx2.md).
- CI: `.github/workflows/ps2.yml` runs `make` and `make release` on Linux, in the ps2dev
  `ps2sdk` image pinned by digest, for every change but docs. It keeps no ELF and runs no
  test executable, host test or emulator gate. `tools.yml` builds the host tools with GCC,
  Apple clang and MSVC. See [.claude/rules/ci.md](.claude/rules/ci.md).

## Git workflow

- One branch: commit directly on `main`. Don't create feature branches unless asked. Push
  only when asked.
- `git fetch` first. The user sometimes commits on GitHub directly; check for divergence
  before committing and fast-forward only when the working tree can be preserved safely.
- If the planned vclpp submodules are imported and changed, publish nested submodule
  commits before a parent gitlink. This repository has no such gitlinks yet.
- Commit subjects are one sentence ending in a period, often prefixed with the area:
  `Sound: stream the soundtrack from loose SPU2 ADPCM files.`,
  `vclpp: back to C++17, so GCC 9 builds it; CI on GitHub.`

## Things that bite (details in the rules)

- The EE FPU has no Inf/NaN (1/0 = FLT_MAX), and double is soft-float. Host and target
  silently disagree on degenerate math.
- ps2sdk stubs some libc calls to fail (`sysconf` → -1), and some of its register macros
  don't parenthesize their arguments. Verify before trusting either.
- SIF DMA target buffers need `alignas(64)`.
- The VU toolchain miscompiles silently. Only `check_vu_code.py` and the screen tell you.
- GS alpha 1.0 is `0x80`. Normalized ST spans the power-of-two TEX0 extent, not the image.
- PCSX2 models neither the EE cache nor GS-internal cost. A capture only gates regressions
  for those.
- CI's Linux file names are case-sensitive and this Mac's APFS volume isn't. An include or
  Makefile path in the wrong case builds locally and fails CI.

## Rules index (`.claude/rules/`)

When a finding is durable (a hardware fact, a toolchain trap, a measured Doom baseline, a
test recipe), record it in the matching rule file below. Preserve Quake measurements as
reference observations until reproduced on this port.

| File | Loaded for | Covers |
| --- | --- | --- |
| [testing-pcsx2.md](.claude/rules/testing-pcsx2.md) | always | PCSX2 setup and logs, scripted sessions, built-in tests, crash triage, the known TLB flake, host harnesses |
| [ps2-backend-cpp.md](.claude/rules/ps2-backend-cpp.md) | `ps2`, host tools | naming, types, file style, passing the strict `-Werror` set |
| [ps2-platform.md](.claude/rules/ps2-platform.md) | `ps2` | ps2sdk traps, EE FPU, SIF DMA, IOP modules, ROM FILEIO, memory card |
| [gs-renderer.md](.claude/rules/gs-renderer.md) | `ps2/renderer` | GS/libdraw facts, frame model, CLUTs, VRAM blocks, mipmaps, ST scaling |
| [vu-microprograms.md](.claude/rules/vu-microprograms.md) | VU sources, vu-checker | openvcl/dvp-as/vclpp traps, VU0 inline asm, VCL comment style, runtime probes |
| [performance.md](.claude/rules/performance.md) | `ps2`, frame-log scripts | EE codegen facts, what PCSX2 can measure, capture/A-B/asm-test recipes |
| [doom3-engine-cpp.md](.claude/rules/doom3-engine-cpp.md) | id's C++ | editing rules, engine quirks and bugs |
| [memory-budget.md](.claude/rules/memory-budget.md) | heap, VRAM, assets, MapCycle | the 32 MB picture, map-transition peak, measured budgets |
| [vclpp-submodule.md](.claude/rules/vclpp-submodule.md) | `external/vclpp` | vclpp/parse-utils conventions, verification recipes, CI, MASP mode, tyra |
| [ci.md](.claude/rules/ci.md) | `.github` | the two workflows, the pinned ps2sdk image and moving it, checking a Linux build without Docker, runner traps |
