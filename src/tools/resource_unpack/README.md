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

The source has no SDK or engine dependencies. It can also be built directly with a
C++20 compiler, without the PS2 toolchain:

```sh
clang++ -std=c++20 -O2 -fno-exceptions -fno-rtti \
    src/tools/resource_unpack/main.cpp -o /tmp/resource_unpack
```

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
python3 -m unittest discover -s src/tests/host -p test_resource_unpack.py
```
