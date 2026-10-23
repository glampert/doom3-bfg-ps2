# Doom 3 BFG Edition for the PlayStation 2

An in-progress PlayStation 2 port of [id Software's Doom 3 BFG Edition](https://github.com/id-Software/DOOM-3-BFG),
built with the free ps2dev toolchain and SDK. The goal is a playable single-player
Doom 3 campaign on the PS2.

**The port is not playable yet.** It currently boots into a developer console, loads
the original game's definitions and scripts, and can display test interfaces, text
from the original game, the original quit dialog and the main menu's start and root
screens, drawn and driven by the game's own interface code; the menu can be navigated
with key presses sent from the development computer. A host tool converts the original interface art into compact paletted
textures that stay in the PS2's graphics memory. Gameplay systems are being brought up
with small test maps before moving on to the full campaign.

- [x] PS2 startup, a sustained game loop and developer console.
- [x] Basic 2D graphics, fonts and interface foundations.
- [x] Initial map loading, movement, collision and AI tests.
- [ ] Main menu and complete single-player campaign loading.
- [ ] 3D graphics, lighting, animation and effects.
- [ ] Controller input, sound, and saving/loading progress.

See the [roadmap](docs/IMPLEMENTATION_PLAN.md) and [port status](docs/PORT_STATUS.md)
for detailed progress and current limitations. Original game data is not included;
retail-data tools require your own Doom 3 BFG Edition installation.

## Build

**The ps2dev toolchain and PS2SDK.** Build/install from
[ps2dev](https://github.com/ps2dev) (`ps2toolchain` + `ps2sdk`). These tools should be
on `PATH`; the current build uses the EE compiler/SDK, while VU tools are for later
rendering work:

| Tool | Comes from | Typical location |
| --- | --- | --- |
| `mips64r5900el-ps2-elf-gcc` / `mips64r5900el-ps2-elf-g++` | ps2toolchain | `$PS2DEV/ee/bin` |
| `openvcl` | openvcl | `$PS2DEV/bin` |
| `dvp-as` | ps2toolchain (dvp) | `$PS2DEV/dvp/bin` |
| `bin2c` | ps2sdk | `$PS2SDK/bin` |

Set up `~/.zshenv` (or your shell's equivalent) along these lines:

```sh
export PS2DEV=$HOME/ps2dev
export PS2SDK=$PS2DEV/ps2sdk
export PATH=$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/dvp/bin:$PS2SDK/bin:$PATH
```

Python 3.10+ is required for build scripts. Host resource tools and tests also need
a C++20 compiler (default: `clang++`), and the host tools need CMake 3.24 or newer. The
native build is currently checked with GCC 15.2.

From the repository root:

```sh
make                  # debug build: build/debug/d3bfg.elf
make release          # release build: build/release/d3bfg.elf
```

[CI](.github/workflows/ps2.yml) runs `make` and `make release` on Linux for every push and
pull request that changes more than the documentation, inside the ps2dev project's
[`ps2sdk` container image](https://github.com/ps2dev/ps2sdk/pkgs/container/ps2sdk), pinned
to one build of it. It checks that both configurations compile and link, and keeps no ELF.

The C++ host tools are a [CMake project](src/tools/CMakeLists.txt), which
`make resource-unpack` and `make asset-reexport` drive: the first extracts the original
resource containers, the second [converts textures](src/tools/asset_reexport/README.md)
into the paletted formats the port loads. They also build on Windows with MSVC:

```sh
cmake -S src/tools -B build/tools
cmake --build build/tools --config Release
```

That leaves the `.exe` files in `build/tools/Release/`, linked with the C/C++ runtime
statically. [CI](.github/workflows/tools.yml) builds the tools with GCC, Apple clang and
MSVC whenever they change, and keeps the Windows executables as the `host-tools-windows-x64`
artifact on each run's page under the repository's Actions tab, for 90 days.

See the [Makefile header](Makefile) for packaging, run and test targets, and the
[developer guide](docs/DEVELOPMENT.md) for runtime controls and diagnostics.
Contributors continuing the port should start with the [handover notes](docs/AGENT_HANDOVER.md).

## Running in PCSX2

Stage a package before launching so the ELF has its data alongside it. For the
current visible developer console:

```sh
make console
PCSX2 -batch -elf "$PWD/build/debug/console/d3bfg.elf"
```

Replace `PCSX2` with the path to your emulator executable if it is not on `PATH`.
On macOS, this is typically `/Applications/PCSX2.app/Contents/MacOS/PCSX2`.
`make run-console` stages and launches the same package; set `PCSX2=/path/to/PCSX2`
to override the Makefile's emulator path.

While PCSX2 is closed, set these development options in `PCSX2.ini`:

```ini
[EmuCore]
HostFs = true             ; host: maps to the ELF's directory; without it there is no game data

[Logging]
EnableIOPConsole = true   ; the game's stdout goes through the IOP
EnableFileLogging = true  ; ...and into logs/emulog.txt

[USB1]
Type = hidkbd             ; optional: a USB keyboard that passes host keystrokes through
```

On macOS the configuration is normally at
`~/Library/Application Support/PCSX2/inis/PCSX2.ini`, with logs in the sibling `logs/`
directory. Game-side keyboard/controller polling is still pending; attaching an
emulated keyboard alone does not enable input in this port. Current console controls
and command-file input are described in the [developer guide](docs/DEVELOPMENT.md#production-packages-and-controls).

## Source layout

```text
.
|-- base/               # Upstream configs and render programs; local retail files ignored
|-- build/              # Generated executables, packages, tools and test results
|-- config/             # Build source lists and asset profiles
|-- docs/               # Developer guides, roadmaps and technical records
|-- src/
|   |-- external/       # Imported third-party dependencies
|   |-- neo/            # Original Doom 3 BFG engine and game code
|   |-- ps2/            # PlayStation 2 backend
|   |-- tests/          # Host tests and separate native test executables
|   |   `-- data/       # Authored boot, map and interface fixtures
|   `-- tools/          # Host resource tools and build/test scripts
`-- Makefile
```

## Attributions

Thanks to id Software for the original game source code and the ps2dev community for the toolchain and SDK.

This project derives its PS2 backend code from [Quake II PS2](https://github.com/glampert/quake2-ps2).

Bundled dependencies also include zlib, jpeg-6 and Doug Lea's allocator (dlmalloc);
see the [reuse inventory](docs/REUSE.md) for provenance and individual notices.

## License

The game and new PS2 backend source are released under the **GNU GPL-3.0-or-later**
license. See [LICENSE.txt](LICENSE.txt). Third-party components and tools retain their
own license notices. Original game assets are proprietary and are not included here.
