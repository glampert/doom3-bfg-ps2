# Agent handover

Reviewed **2026-10-23, Australia/Sydney**, after the full-dialog slice, the first
memory budget measurements, the paletted UI conversion, the pre-rendered menu glyphs,
the fully drawn shell and the native shell menu with its key input, each with its debug
emulator acceptance, against the current source manifests.
This is a continuation guide; [AGENTS.md](../AGENTS.md) supplies the coding, build and
Git rules. Runtime evidence remains in [PORT_STATUS.md](PORT_STATUS.md).

## Read first

1. [Current status](PORT_STATUS.md#current-snapshot--2026-10-22), the latest
   [console charset acceptance](PORT_STATUS.md#retail-console-charset-2026-10-22), the
   [pre-rendered glyph acceptance](PORT_STATUS.md#pre-rendered-menu-glyphs-2026-10-22), the
   [paletted UI acceptance](PORT_STATUS.md#paletted-retail-ui-textures-2026-10-21) and the
   [full-dialog acceptance](PORT_STATUS.md#original-dialog-drawn-in-full-2d-6-third-slice).
2. [2D renderer roadmap](RENDERER_2D_PLAN.md), especially implementation progress,
   next selected work and required mask/blend gates.
3. [Retail UI pipeline](RETAIL_UI_PIPELINE.md#scoped-retail-timeline-execution) and
   [developer controls](DEVELOPMENT.md#production-packages-and-controls).
4. Applicable [.claude rules](../AGENTS.md#rules-index-clauderules),
   [build inventory](BUILD_INVENTORY.md) and [reuse inventory](REUSE.md).

Dated sections in PORT_STATUS, BUILD_INVENTORY and the original inspection tables
are historical records. Do not repeat completed header/RTTI/exception/link work or
use an old source count/limitation as current guidance. Quake measurements are
reference observations until reproduced here. CLAUDE.md redirects to AGENTS.md.

## Starting state

The port is not playable. M0–M2 and the retained campaign compile/link gate M2b are
complete. M3 is partial: the separate game fixture exercises native initialization,
scripts/events and a restricted player over 331-frame reloads; the authored map
fixture covers logical models/world/PVS, collision, AAS and one idle native AI.
Production initializes the game but stays in `NOMAP`; it does not run that map.

The user's milestones are: main menu with full 2D drawing, then pad/keyboard input,
then a level boot without the 3D view but with in-game UI, then 3D rendering. The
original dialog and the shell's start screen now draw in full. Before the shell, the user selected asset
recompression/re-export and fitting the game into 32 MiB; its first measurements are
recorded, and its first lever, paletted UI textures, is implemented (see below). Preserve the
headless/game fixtures. English is the only initial UI language. Sound playback, world
rendering, save/load and animated campaign AI remain later work. Extend existing native
renderer/UI fixtures; the user has asked to avoid adding standalone fixture programs
for each feature.

| Profile | Working behavior | Explicit limit |
| --- | --- | --- |
| Production headless | Common, native console commands, NOMAP ticks, bounded/live quit and complete shutdown | No production map simulation |
| Visible console | Native console edit/history, close/reopen, capture/final presentation in NTSC/PAL, drawn with the original `textures/bigchars` charset from the cooked catalog | HostFs injection; no physical input polling; visible packages need `make ui-cook` |
| Authored SWF | Native loading, fonts, timeline/AVM1 focus/select, additive blend 8, screen blend 4, color terms, outlines, convex and depth masks, shared pages | Exact generated asset admission; no retail-menu claim |
| Retail fonts | Five English families, native legacy metrics and resident paletted textures | Font/resource screen only |
| Retail movie inspection | Original shell/dialog dictionaries, full English dictionary and isolated native edit-text layout | Inactive imported timelines |
| Retail timeline execution | Checked placement/AVM1 work; minimal carousel/Credits owners | Draws isolated labels; no events |
| Retail dialog drawing | Native `Render` of the original dialog with Common's quit message from resident paletted textures, its text from pre-rendered glyphs | About 40 ms per debug PCSX2 frame; choices fail explicitly |
| Retail shell drawing | Native `Render` of the original shell's start screen: outlines, color terms, screen blends, depth masks; the native logo transition on request | 100–140 ms per debug PCSX2 frame on the fixed clock; placeholder prompts and labels; no navigation |
| Native shell menu | `idMenuHandler_Shell` with its start, root and credits screens, driven by HostFs keyboard keys and pad buttons through Common's event loop: sign-in, root focus, back to press-start; Quit opens Common's native confirmation, whose Cancel returns and Accept quits; pad button tags draw as PS3 prompt art; a `menu` package policy boots into it | Campaign/Settings screens, physical keyboard/pad |
| Retail data boot | Production mounts `_common`/`_ordered`, loads every retail declaration and compiles the full `doom_main.script` into `NOMAP` | 13.5 MB of heap backing before any map; no retail map or in-game UI yet |

Recent implementation commits are the full-dialog slice (2026-10-20), `dd04ab0` (scoped
native timelines/widget owners), `ec0df7a` (additive blending), `ac9de81` (convex masks)
and `1f045d8` (fixture consolidation). Later README cleanup is preserved. `6843948`
removed the copied Quake II Makefile; do not restore or link to
`docs/reference/quake2.Makefile`. The build does not require a sibling checkout. The
external Quake II repository remains a reuse/reference source when available.

## Next implementation sequence

### 1. Asset recompression and the 32 MiB budget

Selected by the user on 2026-10-20, after the first full UI picture. The first
measurements are in
[PORT_STATUS](PORT_STATUS.md#memory-budget-investigation-ui-formats-and-the-retail-boot-2026-10-20):

- **UI pages.** PSMT8 with one CLUT per atlas image is near-lossless (47.5–55.6 dB),
  while per-page palettes band smooth gradients. Halving images never drawn above
  about 1.25× is free at the 1280×960 stage's on-screen scale (85–96% of menu and HUD
  texels). PSMT4 suits only the alpha-only fonts, which need 98 English glyphs each.
  Candidate per-screen sets are 0.1–1.2 MB and fit GS memory beside the framebuffers,
  against 1.6–15.7 MB of RGBA32 pages now. Facts and method:
  [gs-renderer.md](../.claude/rules/gs-renderer.md#retail-ui-page-format-survey-2026-10-20).
- **Retail boot.** `make test-retail-boot` reaches `NOMAP` with every retail
  declaration and the full `doom_main.script`: 13.5 MB of heap backing (14.5 MB arena)
  before any map, leaving 6.4–6.8 MB. The owners are declaration text (4.76 MB), script
  (2.83 MB), declaration objects and names (3.0 MB), container tables (1.08 MB) and
  smoke particles (0.68 MB). Per-allocation overhead accounts for 3.32 MB of the backing.

Proposed order; the user chose lever 1 first:

1. **Paletted UI textures — accepted in debug on 2026-10-21.** The user asked
   for a host C++ re-export suite with a shared texture library, everything halved for
   a first pass (PDA and pause included), English-only fonts, aggressively shared
   palettes, PSMT4 where it holds and RLE on disc. `asset_reexport` and
   `src/tools/assets/` convert the UI to GSTX ([pipeline](RETAIL_UI_PIPELINE.md#converted-textures-gstx)):
   763 KB on disc, 1.98 MB decoded for all 16 textures. The target's `PalettedImage`
   keeps pages resident. The debug emulator gates pass in NTSC and PAL; the dialog
   draws a frame in about 60 ms in debug PCSX2 (1.6 s before) with a 1.46 MB smaller
   loaded heap. Next: the user's visual pass over `build/ui-previews` (halved thin
   fonts lost their solid stroke interior), and the release gates when asked.
   **The console** draws the original `textures/bigchars` charset from the catalog since
   2026-10-22 (full resolution through `texture_policies`); the 5×7 generator is now only
   the core renderer test's fixture, and every visible package stages the cook.
   **Menu text** draws from pre-rendered glyphs since 2026-10-22, at the user's choice
   after the dialog's labels aliased ([pipeline](RETAIL_UI_PIPELINE.md#pre-rendered-glyphs)):
   `conversion.glyph_sets` lists Sarasori's six dialog sizes and four start-screen sizes,
   and four Arial Narrow sizes for the start screen. Each screen that draws more text
   logs its sizes once (`UI glyph size ... prerendered=no`); add them to the profile as
   the shell's screens come up. Since 2026-10-23 a set image holds one 1:1 band
   placement per page and each catalog entry carries its glyph's rectangle (UICT4), so a
   set grows by 20 catalog bytes per glyph and size, up to sixteen 512×512 pages.
2. Cheap heap wins, measured by `test-retail-boot`: a smaller tagged header or
   small-block pools, heap-string container names, a PS2-sized smoke pool and script
   limits sized to the retail program.
3. Declaration text on demand from the containers (keep an index), or a pruned
   declaration set: up to 4.76 MB.
4. A retail level's logic state (entities, collision, AAS, PVS topology) beside the
   in-game movies, with `.map` brush/patch text stripped at cook time.

Record measurements for each lever before and after; prune content only with the
user's agreement.

### 2. Shell drawing: start screen drawn in full (2026-10-23)

The shell's draw features have GS paths since 2026-10-23
([GS notes](../.claude/rules/gs-renderer.md#doom-swf-outlines-color-terms-screen-and-depth-masks-2026-10-23)):
line records draw as GS lines; RGB additive color terms in a fog-colored second pass;
screen (mode 4, the PC's `Cd + Cd × S`) one channel per pass with channel CLUTs; masks the
convex CPU bridge refuses go into the depth buffer, one at a time. Every primitive writes
TEST with ZTE 1. `ps2_uiRetail draw shell` draws the original shell's start screen in full
and `make test-retail-shell` accepts it in NTSC and PAL
([record](PORT_STATUS.md#original-shell-drawn-in-full-2d-6-fourth-slice)).

Open items, roughly in order:

- **The native screens.** Done for the start and root screens on 2026-10-23: the native
  handler fills the prompts and the carousel (item 3 below). The minimal owner's
  `draw shell` mode still shows the authored placeholders.
- **Text sizes.** The start and root screens' text draws from pre-rendered glyphs since
  2026-10-23 (Sarasori at heights 10, 13 and, outlined, 10 and 18; Arial Narrow at 2.5
  and 3; per standard). Later screens log their missing sizes once
  (`UI glyph size ... prerendered=no`); add them to `conversion.glyph_sets` as they come
  up. Sarasori has 14 of a set's 16 sizes: raise the bound (converter, cook and catalog)
  before the next screens.
- **Frame cost.** A debug PCSX2 shell frame takes 100–140 ms with one timeline step per
  frame and about 330 ms on the real-time clock (up to ten steps per frame); it draws about
  1,070 triangles and a dozen masks. Every triangle writes its full state and CLUT load,
  masked content clips on the EE, and outlines draw twice as GL does. Measure release,
  then batch state.
- **Limits kept.** Two non-convex masks at once, additive alpha terms and blend modes
  other than add and screen stay refused; `runtime_ready=false` until the complete
  feature/resource contract holds.

### 3. Connect the offline native menu

Started 2026-10-23: the native `idMenuHandler_Shell`, with its press-start, root and
credits screens, owns the admitted shell (`ps2_uiRetail menu`, `make test-retail-menu`;
[record](PORT_STATUS.md#native-shell-menu-start-and-root-screens-2026-10-23)). Handlers
and movies use platform 1, the PS3 layout. Key events reach it through Common's event
loop (`ps2_uiRetail key NAME`; [record](PORT_STATUS.md#native-shell-menu-key-input-2026-10-23)):
accept signs in, the arrows move the root's focus and back returns to press-start.
The root's Quit opens Common's native confirmation through its dialog manager
([record](PORT_STATUS.md#native-quit-dialog-through-commons-dialog-manager-2026-10-24)),
and its pad button tags draw as the PS3 prompt art
([record](PORT_STATUS.md#pad-prompt-icons-in-retail-text-2026-10-24)). A `menu` package
policy boots into the menu (`make retail-menu`;
[record](PORT_STATUS.md#booting-into-the-native-menu-2026-10-24)). Next, in order:

- **Other screens.** Credits is already bound; Campaign and Settings come next. Enable
  each deferred screen as a reviewed subset, with its text sizes cooked. A move into an
  unbound screen fails by name until then.
- **Keyboard and pad.** Post the same events from the devices. For the debug USB keyboard
  follow `quake2-ps2`'s `src/ps2/input/keyboard.cpp`: ps2kbd raw non-blocking reads, polled
  every 250 ms while idle and every frame only while a key is held or within 10 s of the
  last key, with capped reads per frame. Each read is an IOP file round trip, so polling
  every frame has a real cost on hardware.

The minimal owner of the older harness modes borrows a five-slot carousel with only the
admitted Doom 3 cover and the native Credits callback. Avoid enabling all deferred
menu/online/save/video paths to satisfy one screen.

2D-7 requires a recognizable original root, at least 600 responsive frames, menu and
console transitions, quit/confirmation, stable warm ledgers and full shutdown.
Unavailable campaign/network/save/video actions must be explicit. Physical
keyboard/pad integration and real PS2 acceptance remain 2D-8.

## Code entry points and ownership

| Work | Entry points |
| --- | --- |
| Production loop/presentation | `src/ps2/system/main.cpp`, `headless.cpp`, `presentation.cpp`, `common_campaign.cpp` |
| Authored movie | `src/ps2/ui/movie.cpp`, `authored_assets.cpp`, `authored_ui_data.h` |
| Retail resources and execution | `src/ps2/ui/retail_movie.cpp`, `retail_assets.cpp`, `retail_materials.cpp`, `retail_catalog.cpp`, `retail_owner.cpp` |
| Storage/script guards | `src/ps2/ui/swf_format.*`, `swf_execution.*`, `swf_runtime.*` |
| Native SWF integration | `src/neo/swf/SWF_Main.cpp`, `SWF_Render.cpp`, `SWF_SpriteInstance.cpp`, native menu/widget sources |
| CPU geometry/masks | `src/ps2/ui/draw.*`, `masks.*`, `triangles.*`, `geometry.*` |
| Images/fonts/page cache | `src/ps2/renderer/image_backend.cpp`; `src/ps2/ui/font_backend.cpp`, `page_image.*`, `page_geometry.*`, `page_format.*`, `paletted_image.*`, `gstx_format.*` |
| GS device and fences | `src/ps2/renderer/gs.*`, `render_system.*`, `cmd_buffer.*`, `texture.*`, `vram.*`, `capture.*` |
| Authored reference/oracles | `src/tools/scripts/build_authored_ui.py`, `ui_capture.py`, `renderer_capture.py` |
| Native/host regressions | `src/tests/smoketests/renderer_gs_tests.cpp`, `src/tests/host/ui_triangles_tests.cpp`, `src/tests/host/scripts/` |
| Build manifests | `config/sources.mk`, `config/source_inventory.json`, root Makefile |

Backend namespaces follow `ps2::module::subsystem` (for example `ps2::renderer::gs`,
`ps2::ui::swf`); the [backend rule](../.claude/rules/ps2-backend-cpp.md#naming) has the details.

Native SWF files retain upstream style around minimal tagged PS2 edits. New backend
and host C++ uses Allman braces, spaced blocks, explicit `static` file variables,
shared logging and runtime release validation. Register new units explicitly and
regenerate the compile database. Third-party code belongs in `src/external/`.

## Invariants to preserve

- One Common frame owns one presentation. `UpdateScreen` coalesces redraws and never
  advances simulation or scripts. Complete queued capture/final draw before teardown.
- Fill returned GUI spans before another allocation/draw. Flush before mask changes,
  image/cache mutation or span recycling. Scope/frame/shutdown ownership checks are
  required with assertions disabled too.
- Masks currently admit 64 input triangles, sixteen convex boundary points, four
  keyed active masks and 64 clip planes. Empty masks clip everything. Removal is not
  stack-only; simultaneous expiry revisits swapped entries. Direct glyphs clip too.
- Preserve original triangle/clip-plane provenance and interpolate UV/RGBA before
  rounding. A page boundary and a rounded interior UV are not equivalent constraints.
  Native image dimensions remain logical atlas dimensions, not tile dimensions.
- SWF zero-additive color accepts biased 127/128 only in its scope. Mode 8 uses
  straight RGBA with alpha-weighted GS addition. Nonzero color transforms still fail.
  Expected pixels include each PSMCT16 storage round; do not widen tolerances to hide errors.
- Scripts/callbacks need storage, operand, work, hierarchy and destructive-ancestor
  checks. `resourceExecution` is a separate permission from `resourceOnly`, and
  `resourceRender` a third one: drawing requires no outlines, screen blends or additive
  color in the preflight counts, and retail `Render` never runs frames. Event routing
  stays unavailable. Shutdown widget borrowers before movies,
  movies before fonts/localization, images before caches, caches/UI before the device.
- Authored images share four nearest slots (1 MiB), a 256 KiB scratch page and 56-byte
  records. Retail images are converted GSTX textures whose pages stay resident once
  opened (the shell atlas is 860 KB of texels). Do not merge these profiles implicitly.
- EE float math has no IEEE Inf/NaN and no hardware double. Preserve checked float
  encodings and single-precision geometry. PCSX2 does not establish EE cache or GS cost.

## Data and commands

All commands run from the repository root. Tracked authored inputs are
`src/tests/data/headless/`, `headless_map/` and `ui/`; Python tests are under
`src/tests/host/scripts/`. Update the generator, never only its generated BSWF/header.
Retail data is local/ignored under `base/` or `gamedata/`. Make defaults to
`UI_DATA_ROOT=gamedata/d3_bfg`; override it to the directory containing the expected
containers and English language file. Cooked output must stay under `build/`.
Do not commit proprietary data, captures or extraction output; public captures must
contain only redistributable authored assets.

```sh
make
make release
make inventory
make compiledb
python3 src/tools/scripts/build_authored_ui.py --check
make test-host
make run-ui
make ui-cook
make run-retail-movies
make test-retail-boot
```

GitHub Actions builds production debug/release on Linux in the pinned ps2sdk image
(`.github/workflows/ps2.yml`) and the C++ host tools with GCC, Apple clang and MSVC
(`tools.yml`). Both are compile/link checks; host tests and emulator gates stay local.
Linux file names are case-sensitive: [ci.md](../.claude/rules/ci.md) has the Mac reproduction.

`run-ui` shows the authored scene. To draw the original quit dialog in full in the
separate retail package, while it is running with the resource screen loaded:

```sh
printf 'ps2_uiRetail dialog quit\n' >> build/debug/ui-retail-movies/console.txt
```

A new retail-movies package auto-loads the screen. Existing packaged configs are
preserved: check status and issue `ps2_uiRetail load` only if not loaded; a duplicate
load is fatal. Use `execute shell|dialog` for scoped timelines with label inspection,
`movie shell|dialog|none` for inactive inspection, and `ps2_uiRetail status` for native
counters. Only the dialog draws in full, at about 60 ms per debug PCSX2 frame. HostFs
commands provide input; physical keyboard attachment alone will not drive the game.

## Verification and recorded baseline

Run emulator gates sequentially with no unrelated PCSX2 session. The runner checks
configuration and owns only the process it starts. Do not change the user's ini or
terminate their unrelated emulator. Preserve failed archives and triage EE versus
host-emulator crashes; a fresh retry is new evidence, not a passing crashed run.

| Change | Relevant existing gates |
| --- | --- |
| Geometry/GS/mask work | `make test-host`, `make test-renderer`, `make test-ui`; repeat target gates with `BUILD=release` |
| Retail resources/timelines/menu | Host guards plus `make test-retail-ui`, `make test-retail-movies` or scoped `make test-retail-timelines` / `make test-retail-dialog`, as affected, in debug/release |
| Common/frame/presentation ownership | `make test-headless`, `make test-console`, `make test-ui`, debug/release |
| Heap, declarations, scripts or containers | `make test-retail-boot` (its startup ledgers), plus `make test-headless` |
| Game/map behavior | Existing `make test-game` / `make test-map`, debug/release; not a reason to resume standalone movement-fixture expansion |
| Documentation/path-only changes | Local links/targets, source inventory, authored `--check`; fixture moves also use host tests and package staging |
| Include/Makefile paths, toolchain or CI | `make`, `make release`, plus the case-sensitive Linux check in [ci.md](../.claude/rules/ci.md) |

Latest (2026-10-20, debug only by the user's direction): the retail boot and its
startup ledgers, with host, core smoke, headless and console gates. Last feature
acceptance (2026-10-20): the original dialog drawn in full in debug/release NTSC/PAL
(`test-retail-dialog`, also inside `test-retail-movies`), plus retail fonts, authored
UI, renderer, console, headless and host regressions. Previous (2026-10-18): 77
authored pixel checks, three reload/input cycles, twelve malformed-resource cases,
nineteen core renderer cases per configuration and two headless cases per configuration.
On 2026-10-19 every regular target gate (smoke variants, renderer, Common, headless,
console, UI, retail fonts/movies, game, map and the script probe) passed again in both
configurations on the rebuilt toolchain, with unchanged tagged ledgers. Host regression
count is 354 plus C++ sanitizer harnesses (2026-10-21). Run IDs and screenshots are
linked from PORT_STATUS; ignored `build/test-results/` archives may not exist on another
machine.

Production has 376 direct objects, no fixture inputs/symbols and no GC. The source audit
has 458 upstream units / 282 campaign units; compile database 401 entries. Production
PT_LOAD is 11,177,957 debug / 11,614,821 release bytes after the paletted UI change
(11,167,077 / 11,604,197 after the retail boot change, 11,164,005 / 11,600,997 after
the full-dialog slice, 11,159,013 / 11,596,133 on the SDK rebuilt on 2026-10-19,
11,159,397 / 11,596,517 before it); core is 2,150,896 / 2,235,504 (2,139,184 /
2,223,920 before the rebuild). Authored UI peak is 3,885,330 requested / 3,992,352
backing; full shutdown recovers 7,424 / 7,600 bytes / four allocations. These are
separate fixed/runtime measurements; do not sum tagged backing and committed arena
twice. Retail timeline heap acceptance uses its own 16 MiB loaded ceiling and exact warm
release baselines. The fully drawn dialog peaks at 4,412,335 requested / 4,516,500
backing bytes (debug; 6,079,651 / 6,183,080 with streamed pages); the shell's drawn
working set is not measured yet. The retail boot peaks at 10,582,854 / 13,910,756 with a
14,523,035-byte arena before any map.

No native target or hardware test is newly claimed by the 2026-10-19 docs audit.
Before another implementation commit, fetch, inspect the worktree and check divergence.
Commit directly on `main`, use a sentence ending in a period, and push only when asked.
