# Native resource unpacker

`resource_unpack` is a standalone C++20 host tool for Doom 3 BFG `.resources` files.
It follows `src/neo/framework/File_Resource.{h,cpp}` and `idFile::ReadString`: a
big-endian header/table and little-endian filename lengths. Containers store their
payload bytes directly. Extracted BSWF, BIMAGE, font and other files retain their
native contents; image decoding and UI cooking are separate tools.

Build and run from the repository root:

```sh
make resource-unpack
build/tools/resource_unpack --list gamedata/d3_bfg/_common.resources
build/tools/resource_unpack --output build/unpacked-bfg \
    gamedata/d3_bfg/_common.resources gamedata/d3_bfg/_ordered.resources
```

The source has no SDK or engine dependencies, and needs no PS2 toolchain. `make` drives the
host-tool CMake project, [`src/tools/CMakeLists.txt`](../CMakeLists.txt), which owns the
flags. Any host with CMake 3.24 and a C++20 compiler, Windows/MSVC included, can build it
directly:

```sh
cmake -S src/tools -B build/tools
cmake --build build/tools --config Release
```

That leaves `build/tools/resource_unpack`, or `build/tools/Release/resource_unpack.exe`
under Visual Studio. The `host-tools-windows-x64` artifact of each
[Host tools](../../../.github/workflows/tools.yml) CI run holds the Windows executable.

Multiple inputs use native reverse lookup priority: later containers and later entries
win after ASCII lowercase/backslash normalization. Redundant internal separators
also alias one output path: the retail sound container contains `generated//sound/`
names. Leading/trailing separators and traversal remain invalid. `--list` validates all tables and
prints the merged logical name, byte count and selected container. Extraction validates
all input tables and destinations before creating payload files. Existing files are
refused by default; `--overwrite` explicitly enables replacement. Each file streams
through a 64 KiB buffer into an exclusive temporary file, checks read/write/close and
source stability, then publishes the completed file. A later I/O failure can leave
earlier completed files; extraction is not a whole-directory transaction.

The reader checks the resource magic, signed field ranges, file/table bounds, entry
counts, string lengths, exact table consumption and overlapping payload ranges.
Identical shared payload spans are permitted. The limits are 255 input containers,
one million entries per container and 32 MiB per table. It rejects unsafe relative
paths, file/directory collisions, output symlinks, paths that could overwrite source
containers and Windows device aliases. Use an output tree that is not concurrently
modified; filesystem checks do not provide an adversarial directory sandbox.

Keep extracted game data under ignored `build/`, `gamedata/` or repository-root `base/`.
Do not commit retail files. `make test-host` builds a separate ASan/UBSan version and
runs synthetic extraction, malformed-input, overwrite and filesystem tests. To run
only those tests after building it:

```sh
make build/tests/resource_unpack
python3 -m unittest discover -s src/tests/host/scripts -p test_resource_unpack.py
```
