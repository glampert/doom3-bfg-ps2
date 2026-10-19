# Agent handover

Reviewed **2026-10-19, Australia/Sydney**, against `6843948` and the current source
manifests. This is a continuation guide; [AGENTS.md](../AGENTS.md) supplies the coding,
build and Git rules. Runtime evidence remains in [PORT_STATUS.md](PORT_STATUS.md).

## Read first

1. [Current status](PORT_STATUS.md#current-snapshot--2026-10-19) and the latest
   [convex-mask acceptance](PORT_STATUS.md#native-convex-swf-masks-2d-6-second-slice).
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

The selected work is the approved 2D/menu roadmap. Preserve the headless/game
fixtures while advancing the actual menu. English is the only initial UI language.
Sound playback, world rendering, physical input, save/load, retail campaign maps
and animated campaign AI remain later work. Extend existing native renderer/UI
fixtures; the user has asked to avoid adding standalone fixture programs for each
feature. The real-dialog → shell-frame → interactive-root sequence is the current
recommended path, not a claim that those milestones are already accepted.

| Profile | Working behavior | Explicit limit |
| --- | --- | --- |
| Production headless | Common, native console commands, NOMAP ticks, bounded/live quit and complete shutdown | No production map simulation |
| Visible console | Native console edit/history, close/reopen, capture/final presentation in NTSC/PAL | HostFs injection; no physical input polling |
| Authored SWF | Native loading, fonts, timeline/AVM1 focus/select, additive blend 8, convex masks, shared pages | Exact generated asset admission; no retail-menu claim |
| Retail fonts | Five English families, native legacy metrics and shared linear pages | Font/resource screen only |
| Retail movie inspection | Original shell/dialog dictionaries, full English dictionary and isolated native edit-text layout | Inactive imported timelines |
| Retail timeline execution | Checked placement/AVM1 work; minimal carousel/Credits owners | Still draws isolated labels; full movie Render/HandleEvent remain fatal |

Recent implementation commits are `dd04ab0` (scoped native timelines/widget owners),
`ec0df7a` (additive blending), `ac9de81` (convex masks), and `1f045d8` (fixture
consolidation). Later README cleanup is preserved. `6843948` removed the copied
Quake II Makefile; do not restore or link to `docs/reference/quake2.Makefile`.
The build does not require a sibling checkout. The external Quake II repository
remains a reuse/reference source when available.

## Next implementation sequence

### 1. Reach a fully rendered original dialog

Audit the actual executed dialog's complete shape/text/material/image/state demands.
Its ten mask placements use two rectangular shapes, which fit the convex profile;
that does not prove all remaining dialog draws are admitted. Identify native mask
material override, blend/color transforms, text strokes and dynamic resource paths
before allowing a full draw. The resource-only constructor currently substitutes
`_white` for `guiSolid`; review the full-movie solid/atlas material contract instead
of assuming the inspection bindings are render-ready. Finish any required gap, then introduce
an explicit checked full-dialog capability and exercise the real native Render path.

Keep inactive inspection and scoped execution usable. Do not clear `resourceOnly`,
discard unsupported GLS bits, substitute normal blends or globally enable full retail
rendering to get a picture. Capture source/reference pixels and native state together,
measure page/heap ownership, and require repeated load/unload plus full cleanup in
both configurations and video standards.

### 2. Close shell drawing requirements and capture a full shell frame

The local inventory reports 54 mask placements referencing 29 shell shapes: a
185-point convex outline, disconnected regions, concave text outlines and holes.
The largest fill has 198 vertices; the largest tessellation has 183 triangles.
Masks 185 and 189 also contain opaque boundary strokes. Fourteen placements
rotate/shear a local rectangle. The conservative all-frame display graph found at
most one inherited/local active mask, but the authored test intentionally covers three.

Evaluate a bounded triangulated-mask representation for the remaining fill regions;
a 256-triangle envelope is a candidate to validate, not an implemented capability.
Prove coverage/topology and canonical intersections across mask pieces before
blending translucent/additive content. Merely increasing the convex capacity cannot
handle holes or disconnected regions. Stroke coverage needs its own explicit policy.

Also close screen blend 4, nonzero additive color transforms, dynamic image/material
paths and active artwork residency. The existing six-slot retail cache has not been
accepted against the fully drawn menu. `runtime_ready=false` must remain until its
complete required feature/resource contract is satisfied.

### 3. Connect the offline native menu

Bring up bounded native PressStart/Root/Quit-dialog ownership and Common startup,
level-load and focus/select/back routing. Current owners borrow a five-slot native
carousel containing only the admitted Doom 3 cover and the native Credits callback;
they are not the complete `idMenuHandler_Shell` screen hierarchy. Avoid enabling all
deferred menu/online/save/video paths to satisfy one screen.

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
| CPU geometry/masks | `src/ps2/renderer/ui_draw.*`, `ui_masks.*`, `ui_triangles.*`, `ui_geometry.*` |
| Images/fonts/page cache | `src/ps2/renderer/image_backend.cpp`, `font_backend.cpp`, `ui_page_image.*`, `ui_page_geometry.*`, `ui_page_format.*` |
| GS device and fences | `src/ps2/renderer/gs.*`, `render_system.*`, `cmd_buffer.*`, `texture.*`, `vram.*`, `capture.*` |
| Authored reference/oracles | `src/tools/scripts/build_authored_ui.py`, `ui_capture.py`, `renderer_capture.py` |
| Native/host regressions | `src/tests/smoketests/renderer_gs_tests.cpp`, `src/tests/host/ui_triangles_tests.cpp`, `src/tests/host/scripts/` |
| Build manifests | `config/sources.mk`, `config/source_inventory.json`, root Makefile |

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
  checks. `resourceExecution` is a separate permission from `resourceOnly`; it does
  not authorize full Render or event routing. Shutdown widget borrowers before movies,
  movies before fonts/localization, images before caches, caches/UI before the device.
- Authored images share four nearest slots (1 MiB), a 256 KiB scratch page and 56-byte
  records. Retail resources share six linear slots (1.5 MiB), a scratch page and lazy
  image tables. Do not merge these profiles implicitly or eagerly decode the shell atlas.
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
```

GitHub Actions builds production debug/release on Linux in the pinned ps2sdk image
(`.github/workflows/ps2.yml`) and the C++ host tools with GCC, Apple clang and MSVC
(`tools.yml`). Both are compile/link checks; host tests and emulator gates stay local.
Linux file names are case-sensitive: [ci.md](../.claude/rules/ci.md) has the Mac reproduction.

`run-ui` shows the authored scene. For original dialog execution in the separate
retail package, while it is running with the resource screen loaded:

```sh
printf 'ps2_uiRetail execute dialog\n' >> build/debug/ui-retail-movies/console.txt
```

A new retail-movies package auto-loads the screen. Existing packaged configs are
preserved: check status and issue `ps2_uiRetail load` only if not loaded; a duplicate
load is fatal. Use `execute shell` for the scoped widget owner, `movie shell|dialog|none` for inactive
inspection, and `ps2_uiRetail status` for native counters. Full retail movies are not
currently visible. HostFs commands provide input; physical keyboard attachment alone
will not drive the game.

## Verification and recorded baseline

Run emulator gates sequentially with no unrelated PCSX2 session. The runner checks
configuration and owns only the process it starts. Do not change the user's ini or
terminate their unrelated emulator. Preserve failed archives and triage EE versus
host-emulator crashes; a fresh retry is new evidence, not a passing crashed run.

| Change | Relevant existing gates |
| --- | --- |
| Geometry/GS/mask work | `make test-host`, `make test-renderer`, `make test-ui`; repeat target gates with `BUILD=release` |
| Retail resources/timelines/menu | Host guards plus `make test-retail-ui`, `make test-retail-movies` or scoped `make test-retail-timelines`, as affected, in debug/release |
| Common/frame/presentation ownership | `make test-headless`, `make test-console`, `make test-ui`, debug/release |
| Game/map behavior | Existing `make test-game` / `make test-map`, debug/release; not a reason to resume standalone movement-fixture expansion |
| Documentation/path-only changes | Local links/targets, source inventory, authored `--check`; fixture moves also use host tests and package staging |
| Include/Makefile paths, toolchain or CI | `make`, `make release`, plus the case-sensitive Linux check in [ci.md](../.claude/rules/ci.md) |

Last feature acceptance (2026-10-18): debug/release NTSC/PAL, 77 authored pixel checks,
three reload/input cycles, twelve malformed-resource cases, nineteen core renderer
cases per configuration and two headless cases per configuration. On 2026-10-19 every
regular target gate (smoke variants, renderer, Common, headless, console, UI, retail
fonts/movies, game, map and the script probe) passed again in both configurations on
the rebuilt toolchain, with unchanged tagged ledgers. Host regression count is 334 plus
C++ sanitizer harnesses. Run IDs and screenshots are linked from PORT_STATUS; ignored
`build/test-results/` archives may not exist on another machine.

Production has 374 direct objects, no fixture inputs/symbols and no GC. The source
audit has 458 upstream units / 282 campaign units; compile database 399 entries.
Production PT_LOAD is 11,159,013 debug / 11,596,133 release bytes with the SDK rebuilt
on 2026-10-19 (11,159,397 / 11,596,517 before it); core was 2,139,184 / 2,223,920
before the rebuild. Authored UI peak is 3,885,301 requested / 3,992,336 backing;
full shutdown recovers 7,424 / 7,600 bytes / four allocations. These are separate
fixed/runtime measurements; do not sum tagged backing and committed arena twice.
Retail timeline heap acceptance uses its own 16 MiB loaded ceiling and exact warm
release baselines. It is not the fully rendered menu's memory measurement.

No native target or hardware test is newly claimed by the 2026-10-19 docs audit.
Before another implementation commit, fetch, inspect the worktree and check divergence.
Commit directly on `main`, use a sentence ending in a period, and push only when asked.
