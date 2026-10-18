# Doom 3 BFG Edition for the PlayStation 2

An in-progress PlayStation 2 port of [id Software's Doom 3 BFG Edition](https://github.com/id-Software/DOOM-3-BFG),
built with the free ps2dev toolchain and SDK. The goal is a playable single-player
Doom 3 campaign on the PS2.

**The port is not playable yet.** It currently boots into a developer console and
can display test interfaces and text from the original game. Gameplay systems are
being brought up with small test maps before moving on to the full campaign.

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
a C++20 compiler (default: `clang++`). The native build is currently checked with GCC 15.2.

From the repository root:

```sh
make                  # debug build: build/debug/d3bfg.elf
make release          # release build: build/release/d3bfg.elf
```

See the [Makefile header](Makefile) for packaging, run and test targets, and the
[developer guide](docs/DEVELOPMENT.md) for runtime controls and diagnostics.

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
|-- gamedata/           # Local retail game installations (ignored)
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

Thanks to id Software for the original game source, the ps2dev community for the
toolchain and SDK, and Guilherme Lampert's [quake2-ps2](https://github.com/glampert/quake2-ps2)
for PS2 backend code and reference work.

This software is based in part on the work of the Independent JPEG Group. Bundled
dependencies also include zlib and Doug Lea's allocator; see the
[reuse inventory](docs/REUSE.md) for provenance and individual notices.

## License

The game and new PS2 backend source are released under the **GNU GPL-3.0-or-later**
license. See [LICENSE.txt](LICENSE.txt). Third-party components and tools retain their
own license notices. Original game assets are proprietary and are not included.
