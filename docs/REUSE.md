# Reused source and licensing

The reference checkout is `glampert/quake2-ps2` at
`71902966e2b3bb21b6e09a33cacc9b97107ce573`. The Doom port does not depend on that
checkout at build time.

| Destination | Source and adaptation | License |
| --- | --- | --- |
| `docs/reference/quake2.Makefile` | Verbatim reference retained when the Doom Makefile was introduced; excluded from the build | Original reference notice retained |
| `src/tools/scripts/symbolize.py` | Standalone Quake symbolization helper with Doom defaults | Original GPL v2 notice and `GPL-2.0.txt` retained |
| `src/external/dlmalloc/malloc.c`, `malloc.h` | Doug Lea malloc 2.7.2, including the reference's PS2 configuration; normalize line endings/trailing whitespace, reject calloc multiplication overflow and correct the prefixed `dlmallinfo` header declaration | Original public-domain notices retained |
| `src/external/dlmalloc/dlmalloc.c` | PS2/newlib integration wrapper; adds a checked `size_t` to `ptrdiff_t` boundary | User authorized reuse of their own code under GPL v3 in this implementation session |
| `src/external/jpeg-6/` | Engine-modified IJG JPEG 6 moved from `src/neo/renderer/jpeg-6/`; retains RGBA output, float DCT and the prior tagged Huffman-copy fix. The original distribution terms are restored as `README.ijg` from the [IJG 6a archive](https://www.ijg.org/files/jpegsrc.v6a.tar.gz); this does not upgrade the codec | Original IJG notices and distribution terms retained; port allocator hooks are separate GPL v3 or later backend code |
| `src/external/zlib/` | Bundled zlib 1.2.3 moved unchanged from `src/neo/framework/zlib/`; nine streaming/CRC sources selected, gzip file I/O and other optional units excluded | Original zlib notices and README retained |
| `src/ps2/input/usercmd_stub.cpp` | Native `userCmdStrings` table from `src/neo/framework/UsercmdGen.cpp`; preserves all 47 action names, order and terminator. The provider and disabled controls are new backend code | Original id Software copyright and GPL v3 or later terms retained |
| `src/ps2/common.h` | Quake II backend assertion macros and `ArrayLength`; removes engine dependencies and uses the shared Doom fatal handler | User-owned reference code reused under GPL v3 or later; attribution in README.md |

New Doom backend code uses `GPL-3.0-or-later`. The authorization above applies to
user-owned reference code; it does not change third-party notices or the standalone
helper's license. The tagged heap wrapper is new code. It records each allocation's
requested size instead of subtracting allocator-rounded sizes on unsized delete.
