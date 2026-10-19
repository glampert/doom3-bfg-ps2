---
paths:
  - ".github/**"
---

# CI (GitHub Actions)

- **`ps2.yml`** runs `make`, then `make release`, on Linux inside the ps2dev project's
  `ghcr.io/ps2dev/ps2sdk` image, for every push or PR that changes more than the docs or
  editor settings: the source audit, every production object and the resident link with its
  registration, map and test-isolation checks. It is a compile and link check. It keeps no
  ELF and builds no test executable; host tests and PCSX2 gates stay local.
- **`tools.yml`** builds the C++ host tools (`src/tools/CMakeLists.txt`) alone, with GCC,
  Apple clang and MSVC, when they or their CMake project change. It keeps the Windows `.exe`
  files as an artifact, `host-tools-windows-x64`, for 90 days. Its first run (2026-10-19,
  `232d758`) passed on all three: GCC 13.3 in 12 s, MSVC 19.51 in 34 s, and Apple clang 21
  in 20 s after ten minutes waiting for a macOS runner.
- The jobs see different hosts. `tools.yml` gets whatever GCC `ubuntu-latest` ships, on glibc
  with `_FORTIFY_SOURCE`, which makes an ignored `fread` result an error. `ps2.yml` gets the
  image's EE GCC 15.2 with Alpine 3.24's GNU make 4.4.1 (the Mac has 3.81), Python 3.14,
  git 2.54 and BusyBox `ash` for recipes. Linux file names are case-sensitive and this Mac's
  APFS volume isn't, so an include or Makefile path in the wrong case builds locally and
  fails CI.
- Each configuration logs about 16 MB, mostly some 34,000 legacy warnings from id's code
  that the warning policy keeps visible (`-Wreorder` alone is half, repeated per unit from
  shared headers). To triage a failure, search the step's log for `error:` and `Error`.

## The ps2sdk image

- Alpine 3.24's minimal root filesystem plus `/usr/local/ps2dev`: the EE, IOP and dvp
  toolchains and the PS2SDK, without ps2sdk-ports or gsKit. Its environment sets `PS2DEV`,
  `PS2SDK` and `PATH`, and the Makefile's `?=` defaults yield to them. It has no make,
  Python or git, and lacks libraries its own compiler needs; the job adds
  `make python3 git gmp mpfr4 mpc1` with apk (see the traps).
- **Pinned by digest** (the multi-arch index), not `:latest`, so a new upstream GCC or SDK
  can't turn CI red with nothing changed here. quake2-ps2 pins the same digest; both match
  the shared local `~/ps2dev` install.
- The pin, as of 2026-10-19 still `latest`: built 2026-10-13, ps2sdk `329e2bb` (the config's
  `org.opencontainers.image.revision` label), GCC 15.2.0, binutils 2.45.1, newlib 4.6.0, on
  Alpine 3.24.2. The local install has the same versions. Its SDK headers,
  `samples/Makefile.pref` and `ee/startup/linkfile` are byte-identical to the local ones, as
  are the EE compiler's newlib and GCC headers. The six libraries the link uses have identical
  code and data sections in all 393 members; only debug sections and archive dates differ.
- **Moving the pin:** read the new digest, check that it builds (below), then update the
  digest and the comment above it in `ps2.yml`. The registry answers without Docker or a
  GitHub login:
  ```sh
  TOK=$(curl -s "https://ghcr.io/token?scope=repository:ps2dev/ps2sdk:pull" |
        python3 -c 'import sys, json; print(json.load(sys.stdin)["token"])')
  curl -sI -H "Authorization: Bearer $TOK" -H "Accept: application/vnd.oci.image.index.v1+json" \
       https://ghcr.io/v2/ps2dev/ps2sdk/manifests/latest | grep -i docker-content-digest
  ```
  `manifests/<digest>` then lists the per-platform manifests. The amd64 one names two
  layers (the Alpine root and the ~190 MB ps2dev tree) and a config blob (`blobs/<digest>`)
  that carries the labels.

## Checking a Linux build without Docker

This Mac has no Docker. The image's target files build with the local toolchain while its
versions match the image's, so the case-sensitivity check needs only a case-sensitive volume.
It runs the Mac's toolchain binaries, not the image's Linux ones, so it can't catch a
missing runtime library (see the traps). On 2026-10-19 this reproduced the CI build of both
configurations:

1. `hdiutil create -size 3g -fs 'Case-sensitive APFS' -type SPARSE -volname D3CS <scratch>/d3cs`,
   then `hdiutil attach -nobrowse -mountpoint <scratch>/d3cs_mnt <scratch>/d3cs.sparseimage`.
2. Download the ps2dev layer through the registry, as above, and extract its SDK onto the
   volume: `tar -xzf <layer> --strip-components 3 usr/local/ps2dev/ps2sdk` inside
   `<vol>/ps2dev`. Copy the local `~/ps2dev/ee` beside it: GCC finds its own headers relative
   to its binary, so the copy is relocatable and its system headers become case-sensitive too.
3. Optionally build GNU make 4.4.1 from source in the scratchpad (verify the tarball against
   the `sha512sums` in Alpine's `main/make/APKBUILD`), to run the image's make version.
4. `git clone --depth 1 file://$PWD <vol>/repo`, apply uncommitted changes with
   `git diff | git -C <vol>/repo apply`, then run `make -j8` and `make release -j8` there with
   `PS2DEV=<vol>/ps2dev PS2SDK=<vol>/ps2dev/ps2sdk` and a `PATH` without `~/ps2dev`.
   Check the exit status. Finish with `hdiutil detach <scratch>/d3cs_mnt`.

Release ELFs built this way have the local build's `PT_LOAD` size exactly. Debug ones differ
by the SDK path's length, which ps2sdk's inline asserts embed (see
[memory-budget.md](memory-budget.md)), and every ELF embeds its build time, so compare sizes
and reports, not ELF bytes.

A quicker screen needs no new build: every `.d` file under `build/` records each header path
as spelled, so walking those paths and comparing each component with its directory listing
finds wrong-case includes in seconds. It can't see Makefile paths of files never compiled.

## Traps

- **JavaScript actions run inside an Alpine container only on x64 runners.** The runner ships
  `node20_alpine`/`node24_alpine` for linux-x64 alone, and on arm64 it fails with "JavaScript
  Actions in Alpine containers are only supported on x64 Linux runners".
- **The image's EE compiler can't start without three host libraries.** Its `cc1`,
  `cc1plus` and `lto1` link GMP, MPFR and MPC dynamically, and the image doesn't ship them.
  The first `ps2.yml` run (2026-10-19, `232d758`) stopped at its first compile with
  `Error loading shared library libmpc.so.3`. Install `gmp mpfr4 mpc1` (Alpine 3.24 names).
  quake2-ps2 gets them through `build-base`, whose host gcc depends on them. The EE
  binutils and driver need only musl; the `libstdc++`/`libgcc_s` users are gdb, openvcl
  and the libcc1 plugins. On a new pin, list each image binary's needs with
  `llvm-readelf -d` and map the sonames through Alpine's `APKINDEX` `p:so:` entries.
- **Install git before `actions/checkout`.** Without git 2.18 or newer on `PATH`, it downloads
  a tarball through the REST API instead, with no `.git`, and the link report records an
  unknown commit.
- **Mark the workspace safe for git after checkout.** The container runs as root while the
  runner's user owns the workspace, so git stops with "detected dubious ownership". Checkout
  adds `safe.directory` only to its own temporary global configuration. `build_metadata.py`
  survives the error, but records an unknown commit and prints git's message at every link.
- **Wrong-case includes still sit in sources no target compiles.** On 2026-10-19 the 33 in
  compiled units were fixed and tagged (`idLib/`, `snapshot_jobs.h`, `image.h`). Deferred
  `d3xp/menus` screens and `sys/PacketProcessor.cpp` still include `idLib/precompiled.h`
  (29 files), and `renderer/BinaryImage.cpp` spells `dxt/` and `color/` for `DXT/` and
  `Color/`. Fix the case, tagged, when enabling one of them.
