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
preconditions, `ArrayLength`, and portable format/cold function macros. Validate and fail
gracefully at load/init time in every build; render-time checks are assertions only
(`PS2_Assert`/`PS2_AssertMsg` or `#if PS2_D3BFG_ASSERTS`), so release builds accept crashes/UB
there. Fatal branches are `[[unlikely]]`; error handlers and load/boot-only functions are
`PS2_COLD_FUNC`. See [Validation](.claude/rules/ps2-backend-cpp.md#validation-and-error-handling).
Reused Quake II backend code is attributed in README.md;
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

## Where things are written down

- [README.md](README.md): current project status; expand it as the port develops.
- [docs/AGENT_HANDOVER.md](docs/AGENT_HANDOVER.md): the current continuation guide.
- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md): toolchain, host tools, packages and their
  HostFs controls, source groups and link policy.
- [docs/testing/](docs/testing/): each gate's acceptance ([core.md](docs/testing/core.md),
  [production.md](docs/testing/production.md), [game.md](docs/testing/game.md)).
- [docs/RETAIL_UI_PIPELINE.md](docs/RETAIL_UI_PIPELINE.md): the retail UI cook, catalog,
  glyph sets and the movie admissions.
- [docs/plans/](docs/plans/): [IMPLEMENTATION_PLAN.md](docs/plans/IMPLEMENTATION_PLAN.md)
  (milestones), [RENDERING_ARCHITECTURE.md](docs/plans/RENDERING_ARCHITECTURE.md) (3D
  rendering) and its step-by-step [RENDERING_TODO.md](docs/plans/RENDERING_TODO.md).
- [docs/CVARS.md](docs/CVARS.md): implemented backend cvars; add each one with its
  debug/release defaults and flags when it exists in code.
- [.claude/rules/](.claude/rules/) (index below) includes Quake II PS2 reference notes. Their
  Quake source paths, test results and runtime behavior are not yet Doom 3 implementation facts.

## Toolchain

- EE compiler: `mips64r5900el-ps2-elf-gcc`/`g++` (GCC 15). There is no `ee-gcc`/`ee-g++`, so
  don't go looking for them. VU tools: `openvcl`, `dvp-as`. Also `bin2c`.
- `$PS2DEV` = `~/ps2dev`, `$PS2SDK` = `~/ps2dev/ps2sdk` (EE headers in `ee/include`). The
  SDK's C sources, for checking what a library really does, are under
  `~/ps2dev/src/ps2dev/build/ps2sdk/ee/<lib>/src/`. gsKit is at `~/ps2dev/gsKit`.
- Third-party code lives in `src/external/`: `dlmalloc` and the pinned VU tool submodules
  `vclpp` (with nested `parse-utils`) and `vu-checker` (`git submodule update --init
  --recursive` after cloning). `make` builds vclpp into `build/tools/vclpp` with `HOST_CXX`
  (never a copy on `PATH`) and the `VCL_FILES` programs into `build/vu/`. No JPEG or zlib is
  linked; see [DEVELOPMENT.md](docs/DEVELOPMENT.md#third-party-dependencies).

## Build and verify

- `make` / `make release` build production: `build/<config>/d3bfg.elf`, matching
  `d3bfg_unstripped.elf`, map and resident `report.{json,txt}`. Production links no test code.
  Check Make's exit status, including VU tool failures.
- Test images are built explicitly (`make build-core`, `build-platform`, `build-game`,
  `build-script`). Gates: `test-host`, `test-renderer`, `test-common`, `test-game`,
  `test-map`, `test-headless`, `test-headless-retail`, `test-ui` (about 8 minutes per
  standard) and `test-world`. Their acceptance is in `docs/testing/`.
- `make stage-*` / `run-*` package the same ELF under `build/<config>/<package>/`.
  Package-root `presentation.cfg` selects `headless`/`ntsc`/`pal` (`menu` also opens the
  native menu); HostFs `console.txt` takes appended commands. Visible packages need
  `make ui-cook` first. Retail data stays under ignored `build/`.
- Host tools are the CMake project `src/tools/CMakeLists.txt` (`make resource-unpack`,
  `asset-reexport`, `world-cook`, `ui-inventory`, `ui-cook`). Regenerate authored fixtures
  with their generator scripts, never by editing outputs.
- `PS2_D3BFG_DEBUG`, `PS2_D3BFG_ASSERTS`, `PS2_D3BFG_PROFILE` and `PS2_D3BFG_TESTS` are always 0 or 1.
  Test them with `#if`, never `#ifdef` (`-Wundef` is on). `PS2_D3BFG_TESTS` is the sole
  test-enable define; keep fixture hooks behind it.
- Source lists in `config/sources.mk` are explicit and audited. Register new backend
  files in `PS2_CXX_SRC` or the relevant core group; cold sources also go in
  `SIZE_OPT_CXX_SRC`. Run `make compiledb` after changing source lists.
- New backend/test sources pass strict warnings and `-Werror`; legacy warning policy is
  in [docs/BUILD_INVENTORY.md](docs/BUILD_INVENTORY.md). Host runtime tests cannot replace
  target compilation.
- Preserve headless simulation while the 2D renderer/console/menu roadmap progresses;
  sound and world rendering follow later.
- CI (`.github/workflows/ps2.yml`, `tools.yml`) only compiles and links; it runs no test
  executable or emulator gate. See [.claude/rules/ci.md](.claude/rules/ci.md).

## Git workflow

- One branch: commit directly on `main`. Don't create feature branches unless asked. Push
  only when asked.
- `git fetch` first. The user sometimes commits on GitHub directly; check for divergence
  before committing and fast-forward only when the working tree can be preserved safely.
- The `src/external/vclpp` (with nested `parse-utils`) and `src/external/vu-checker`
  gitlinks point at published upstream commits. When a submodule changes, publish the
  nested commit first (parse-utils, then vclpp), then bump the gitlink here.
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
test recipe), record it in the matching rule file below. Rule files load into every session,
so keep them to what most tasks need: put a broadly useful rule there and its details,
dated measurements and acceptance history in the matching `docs/reference/` or
`docs/testing/` file it links to. Preserve Quake measurements as reference observations
until reproduced on this port.

| File | Loaded for | Covers |
| --- | --- | --- |
| [testing-pcsx2.md](.claude/rules/testing-pcsx2.md) | always | PCSX2 setup and logs, runner pass rules, crash triage, the known TLB flake, host harnesses; per-gate detail in `docs/testing/` |
| [ps2-backend-cpp.md](.claude/rules/ps2-backend-cpp.md) | `ps2`, host tools | naming, types, file style, load-time vs render-time validation, passing the strict `-Werror` set |
| [ps2-platform.md](.claude/rules/ps2-platform.md) | `ps2` | ps2sdk traps, EE FPU, SIF DMA, IOP modules, ROM FILEIO, memory card |
| [gs-renderer.md](.claude/rules/gs-renderer.md) | `ps2/renderer` | GS/libdraw traps, Doom 2D path invariants, paletted pages, SWF draw scope; history in `docs/reference/gs-renderer.md` |
| [vu-microprograms.md](.claude/rules/vu-microprograms.md) | VU sources, vu-checker | openvcl/dvp-as/vclpp traps, VU0 inline asm, VCL comment style, runtime probes |
| [performance.md](.claude/rules/performance.md) | `ps2`, frame-log scripts | EE codegen facts, what PCSX2 can measure, capture/A-B/asm-test recipes |
| [doom3-engine-cpp.md](.claude/rules/doom3-engine-cpp.md) | id's C++ | editing rules, portability traps, retail SWF admission model; subsystem findings in `docs/reference/doom3-engine-cpp.md` |
| [memory-budget.md](.claude/rules/memory-budget.md) | heap, VRAM, assets, MapCycle | how to measure, the current 32 MB picture, recurring lessons; dated budgets in `docs/reference/memory-budget.md` |
| [vclpp-submodule.md](.claude/rules/vclpp-submodule.md) | `external/vclpp` | vclpp/parse-utils conventions, verification recipes, CI, MASP mode, tyra |
| [ci.md](.claude/rules/ci.md) | `.github` | the two workflows, the pinned ps2sdk image and moving it, checking a Linux build without Docker, runner traps |
