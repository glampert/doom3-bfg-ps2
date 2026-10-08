# Initial EE foundation acceptance

M0–M2 passed on 2026-10-08 with ps2dev GCC 15.2.0 and PCSX2 2.6.3. This is the
scalar Doom foundation: real idlib, command/CVar/file services, synchronous jobs and
PS2 system/heap adapters. It does not run `idCommonLocal::Init` or initialize the game.

## Build and runtime evidence

| Gate | Result |
| --- | --- |
| `make` and `make release` | Compile and link passed; strict new-code warnings remain errors |
| `make compile-core` | 52 idlib and 3 framework units compiled; archive also contains 4 backend/vendor support units |
| `make inventory` | 458 BFG units classified; 88 Classic Doom units excluded from every target |
| `make compiledb` | 283 entries generated from successful real Make dry runs |
| `make test-host` | Shared heap tests passed under ASan/UBSan; 27 runner regression tests passed |
| SDK-only platform probe | Passed: `20261008T070310Z_smoke_88ff01f3485548c6` |
| Final debug core smoke | Passed: `20261008T073710Z_smoke_88a676a12f834106` |
| Final release core smoke | Passed: `20261008T073729Z_smoke_daeb6577543542f2` |
| Final debug missing-fixture scenario | Expected failure accepted: `20261008T073822Z_smoke_b5b081a1d81c4b8a` |

Each emulator run is archived locally under `build/test-results/<run-id>/`, with its
manifest, authored inputs, result JSON, logs, matched ELF/symbols/map, configuration
snapshot and build reports. The negative scenario fails only the fixture-read check;
the remaining checks, including shutdown, must pass. Retail assets are not used.

The core checks registration and command buffering, `exec` and cvar rules, parser
macros and malformed input, bounded fixture I/O and path rejection, scalar matrix and
packed vertex formats, geometry edge cases, polynomial lifetime, owned event queues,
single-thread primitives and job dependencies/synchronization. Allocator checks cover
pre-main allocation, alignment, tagged over-aligned `new`, all global allocation/delete
forms, overflow and exact accounting on unsized free.

## Measured memory

| Measurement | Debug | Release |
| --- | ---: | ---: |
| Fixed ELF residency (`PT_LOAD` memory size) | 1,560,800 bytes | 1,617,760 bytes |
| ELF `.bss` (included above) | 392,288 bytes | 392,288 bytes |
| Initialized core requested / backing | 13,231 / 17,716 bytes | 13,231 / 17,716 bytes |
| Smoke peak requested / backing | 22,597 / 27,544 bytes | 22,597 / 27,544 bytes |
| dlmalloc arena commitment after tests | 44,832 bytes | 45,216 bytes |
| After shutdown requested / backing / count | 1,024 / 1,068 bytes / 1 | 1,024 / 1,068 bytes / 1 |

The retained allocation is a process-lifetime idlib hash table created before `main`.
Shutdown must return exactly to this baseline. Arena commitment includes untagged
C/newlib allocations, allocator overhead and freed space; it must not be added to the
tagged backing total again. ELF residency includes SDK/library support and is separate
from the arena. These figures exclude kernel reservation and stack usage, and establish
no campaign or map-transition budget. PCSX2 does not prove hardware cache correctness
or throughput.

## Next gate: M2b

The explicit campaign list contains 274 retained source units. `make compile-game`
currently fails at its first unit, `aas/AASFile.cpp`: the full precompiled header reaches
`renderer/OpenGL/qgl.h:36`, which requires unavailable `gl/gl.h`. Separate portable
renderer/sound/frontend contracts before continuing that gate; do not use the reduced
core header to disguise campaign dependencies. Campaign exceptions/RTTI, offline
session services and subsystem replacements remain work for M2b and subsequent gates.

There is no map loader boot, GS/VU renderer, audible output, controller input, save
support, physical storage bring-up or custom EE exception handler in this slice.
Classic Doom is excluded but its tree has not been deleted. Imported third-party
notices remain intact; see [REUSE.md](REUSE.md).
