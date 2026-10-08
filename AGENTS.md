# Doom 3 BFG Edition for the PlayStation 2

id's Doom 3 BFG (forked from https://github.com/id-Software/DOOM-3-BFG)
is being ported to the PS2 with the free ps2dev SDK. The source split is:

- **`src/neo/`**: id's original C++ code, kept as close to unmodified as possible.
  Every change is tagged `// [PS2_D3BFG]: <why>`.
- **`src/ps2/`**: the console backend, all new C++20 (no exceptions, no RTTI,
  strict `-Werror`). The mechanical M0 move is committed separately from portability edits.

File-scoped backend variables and constants with internal linkage always use an explicit
`static`, including inside anonymous namespaces (e.g. `static constexpr` for constants).

[IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md) describes the proposed milestones and
source changes. [README.md](README.md) records current project status; expand it as the
port develops. [CVARS.md](docs/CVARS.md) lists implemented backend cvars; add each one with
its debug/release defaults and flags when it exists in code. The files in
[.claude/rules/](.claude/rules/) include Quake II PS2 reference notes. Their Quake source
paths, test results and runtime behavior are not yet Doom 3 implementation facts.

## Toolchain

- EE compiler: `mips64r5900el-ps2-elf-gcc`/`g++` (GCC 15). There is no `ee-gcc`/`ee-g++`, so
  don't go looking for them. VU tools: `openvcl`, `dvp-as`. Also `bin2c`.
- `$PS2DEV` = `~/ps2dev`, `$PS2SDK` = `~/ps2dev/ps2sdk` (EE headers in `ee/include`). The
  SDK's C sources, for checking what a library really does, are under
  `~/ps2dev/src/ps2dev/build/ps2sdk/ee/<lib>/src/`. gsKit is at `~/ps2dev/gsKit`.
- Imported third-party dependencies live in `src/external/`, currently `dlmalloc`.
  Planned VU/tool dependencies from the reference are `src/external/vclpp` (and its
  nested `external/parse-utils`), `src/external/vu-checker`, and possibly
  `src/external/miniz`.
  These VU/tool dependencies are not imported or pinned yet. When VU code is added,
  build the pinned vclpp into `build/tools/vclpp` instead of relying on a copy on `PATH`.

## Build and verify

- `make` compiles and links the current debug milestone with the real EE compiler.
  `make release` selects release; the artifacts are `build/<config>/d3bfg.elf` and
  matching `d3bfg_unstripped.elf`, map and build report. The verbatim reference Makefile
  is preserved in `docs/reference/quake2.Makefile`.
- `make compile-core` checks the explicit scalar idlib/framework source set.
  `make headless-core` links its real foundation bootstrap and tests. `make compile-game`
  is the separate M2b campaign gate and exposes remaining portability blockers.
- New backend/test sources pass strict warnings and `-Werror`; legacy warning policy is
  documented in [docs/BUILD_INVENTORY.md](docs/BUILD_INVENTORY.md). Host runtime tests
  cannot replace target compilation. Check Make's exit status, including VU tool failures.
- `PS2_D3BFG_DEBUG`, `PS2_D3BFG_ASSERTS` and `PS2_D3BFG_PROFILE` are always 0 or 1.
  Test them with `#if`, never `#ifdef` (`-Wundef` is on).
- Source lists in `config/sources.mk` are explicit and audited. Register new backend
  files in `PS2_CXX_SRC` or the relevant core group; cold sources also go in
  `SIZE_OPT_CXX_SRC`. Run `make compiledb` after changing source lists. Flag stamps
  invalidate affected object groups automatically.
- Doom smoke procedures and the separately labeled Quake reference are in
  [.claude/rules/testing-pcsx2.md](.claude/rules/testing-pcsx2.md).

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
