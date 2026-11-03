# vclpp and parse-utils (submodules shared with the Quake II reference)

Doom imports the same submodules quake2-ps2 uses, at the same pins (2026-11-03):
`src/external/vclpp` at `95911cd` (vclpp 2 with MASP mode), its nested
`external/parse-utils` at `4fd3fcd`, and `src/external/vu-checker` at `3917e1b`. The
reference keeps them under `src/tools/`, so the `src/tools/vclpp` paths below describe
the reference checkout; read `src/external/vclpp` here. Doom's Makefile builds vclpp
into `build/tools/vclpp` with the same out-of-tree `BIN_TARGET` rule; the byte-identical
verification recipe below is still quake2-ps2's (its VU programs are the larger corpus).

vclpp (`src/tools/vclpp`, github.com/glampert/vclpp) preprocesses the VU programs. vclpp 2
is token-based, built on the lexer of parse-utils (nested submodule
`src/tools/vclpp/external/parse-utils`). The engine lives in vclpp, not in parse-utils'
`preprocessor`, which minifies and expands into text. vclpp only borrows its `eval()` for
`#if` and `-x`. The README is the syntax reference, including "MASP Mode" and "Upgrading
from VCLPP 1".

## Conventions (different from `ps2`)

- parse-utils style in both repos: **snake_case** types and functions, `m_` private members.
  vclpp code lives in `namespace vclpp`. Don't apply the backend's PascalCase rule here.
- **C++17, and it must build with GCC 9**, because tyra builds vclpp with GCC 9 on Ubuntu
  20.04. That rules out C++20 library features (`std::span`, `starts_with`/`ends_with`,
  `contains`, `std::bit_cast`, `string_view` lookups in unordered maps; use `std::map` with
  `std::less<>`) and the C++17 pieces GCC 9's libstdc++ lacks (floating-point
  `from_chars`/`to_chars`).
- Check with `-std=c++17 -pedantic-errors` under clang and the EE GCC. Compile with
  `-c -o /dev/null`, not `-fsyntax-only`, which skips GCC's `-O2`-only warnings.
- **Only Linux CI catches LP64 format mismatches.** `std::int64_t` is `long` on 64-bit Linux
  but `long long` on macOS and the EE. Use `PRId64` and friends.
- GCC 9 can't be installed on this Mac (arm64; Homebrew disabled `gcc@9`/`gcc@10`). Rely on
  CI, or use an Ubuntu 20.04 container.

## Publishing

- Push order: **parse-utils → vclpp → quake2-ps2**, each bumping the next one's gitlink.
  Before assuming any of them is published, run `git fetch` + `git status -sb` in each.
- CI is `.github/workflows/ci.yml` in vclpp, with three jobs (GCC 9 in an `ubuntu:20.04`
  container, GCC on ubuntu-latest, Apple clang on macos-latest). Each builds with `-Werror`
  and runs both test suites. Run and job status need no auth:
  `api.github.com/repos/glampert/vclpp/actions/runs`, then `.../runs/<id>/jobs`. Poll at most
  every 60 s (the unauthenticated limit is 60 requests/hour). Job *logs* need repo admin
  rights.

## Verifying a change

- Tests: `make test` in `src/tools/vclpp` (golden cases under `tests/`: features, errors,
  warnings, compat, masp) and in parse-utils (its tests use `-Werror`).
- **A change that could affect VU output** must leave quake2-ps2's VU build byte-identical:
  - build vclpp 1 from `git -C src/tools/vclpp show 00e44ec:vclpp_main.cpp` (`-std=c++14`) and
    save `build/vu/*` from it;
  - `diff -w -B` the `.pp.vcl` files, `cmp` the `.vsm`/`.o`, and `cmp` the `.c.vsm` with its
    `^ ; Line N:` lines dropped.
  - The Makefile runs vclpp with `-Wundef -Werror`, so a new warning fails the VU build.
