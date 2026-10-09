# Doom 3 BFG for PlayStation 2: implementation plan

## 1. Objective and scope

Port the Doom 3 campaign from the BFG source release to the PS2 using the installed free EE/IOP toolchain. Preserve game rules, scripts, collision, AI, level progression, and essential in-world interfaces while substantially reducing rendering and asset costs.

The first deliverables are an EE build and a reproducible headless engine boot. These are separate from loading a retail map or running the campaign. A platform test executable alone does not count as a headless Doom 3 boot.

Scope decisions for this plan:

- Use the Quake II PS2 repository as the platform, organization, style, and tooling reference, reusing its implementation wherever suitable (https://github.com/glampert/quake2-ps2).
- Keep the BFG game implementation in `neo/d3xp`; despite the directory name, this is the relevant game source. Do not substitute the obsolete `game.vcxproj` manifest, which refers to a missing `neo/game` tree.
- Target the original Doom 3 campaign first. Expansion campaigns, multiplayer, online services, stereoscopic rendering, and the BFG multi-game launcher are deferred/not in scope.
- Exclude `doomclassic` from every new build. Record it as pending deletion, then delete it in a separate implementation commit after removing its engine references and verifying the link.
- Keep CPU/game simulation behavior initially. Optimize measured bottlenecks after correctness tests exist; do not silently lower physics or script tick rates to improve frame rate.
- Use C++20 for new C++ code, with exceptions, RTTI, and thread-safe local-static initialization disabled. The target engine must also work with these features disabled; suppressing legacy warnings cannot make exception or RTTI expressions compile.
- Treat a 30 fps rendered frame rate as an eventual tuning objective, not a feasibility claim or an initial acceptance condition.

## 2. Evidence from this checkout and machine

The initial M0–M2 foundation is implemented. Current acceptance results, memory
measurements and the next campaign compile blocker are recorded in
[PORT_STATUS.md](PORT_STATUS.md). The inspection below predates implementation;
its `neo/` paths now live under `src/neo/`, and the reference Makefile is preserved at
`docs/reference/quake2.Makefile`.

Inspection baseline:

| Item | Observed state |
| --- | --- |
| Doom repository | `/Users/guilherme/Repos/GitHub/doom3-bfg-ps2`, HEAD `1caba1979589971b5ed44e315d9ead30b278d8b4` |
| Reference repository | `/Users/guilherme/Repos/GitHub/quake2-ps2`, HEAD `71902966e2b3bb21b6e09a33cacc9b97107ce573` |
| Existing build | Visual Studio solution/projects under `neo`; root Makefile is an unmodified Quake II reference and does not build Doom yet |
| EE compiler | `/Users/guilherme/ps2dev/ee/bin/mips64r5900el-ps2-elf-g++`, GCC 15.2.0 |
| SDK | `/Users/guilherme/ps2dev/ps2sdk`; SDK source also installed below `/Users/guilherme/ps2dev/src/ps2dev/build/ps2sdk` |
| Other tools | gsKit installed; host Clang, Python 3, GNU Make and CMake available |
| Emulator | `/Applications/PCSX2.app`, version 2.6.3 |
| Emulator configuration | HostFs, file logging and IOP console logging enabled; screenshot hotkey is F8 |
| Game data in this checkout | Local `gamedata/d3_bfg/` contains BFG retail data; `gamedata/d3_roe/` contains Resurrection of Evil reference data. Both must remain outside commits |

These observations establish available tools and inputs, not a verified working Doom build. Initial compile and synthetic headless tests must not depend on the retail files.

### Supplied asset inventory and compatibility gate

The BFG installation has `.resources` containers, Bink videos and other shipped data. The
RoE reference data is grouped under `gamedata/d3_roe/{dds,models,sound,textures,misc,game00}`.
Inspection of RoE found 36 `.map` and 36 `.proc` files, 83 `.cm` files, 41 AAS files
(`.aas48`/`.aas96`), 86 scripts, 124 entity declaration files, 191 MD5 meshes, 1,959 MD5
animations, 8,715 TGA images, 8,116 DDS images, 3,606 OGG files, 482 WAV files and 42
RoQ videos. These are RoE reference counts, not a BFG content or runtime inventory.

`gamedata/d3_roe/misc/script/doom_main.script`, `gamedata/d3_roe/misc/def/aas.def`
(including `aas_types`), and `gamedata/d3_roe/misc/maps/game/mars_city1.{map,proc,cm}`
are present for format comparisons. The largest RoE text `.proc` is about 28.96 MiB;
RoE `.proc` files total about 512 MiB and collision files about 330 MiB on disk. These
sizes are not BFG or runtime allocation estimates.

RoE contains `game00/gamex86.dll`, loose OGG/RoQ and original-style GUI files, but no
`.resources`, `.preload`, `.bproc`, `.bimage` or `.swf` files. Do not use this Windows DLL or
assume that RoE files can stand in for BFG data. Validate the BFG container layout, resource
manifests, declarations and map dependencies from `gamedata/d3_bfg/` first. RoE remains a
reference for older text and media formats only.

The primary converter input is `gamedata/d3_bfg/`; its logical resource paths and map
dependencies need an inventory before choosing the target package layout. An optional
RoE import path would need its own mapping from `misc/script/...` to `script/...`,
`misc/maps/...` to `maps/...`, and so on. Build a separate normalized/cooked output tree
without moving or overwriting either installation. For local runs, stage the appropriate
`base/` view under `build/<config>/` beside the ELF. The repository's root `base/` is a
tracked directory with source-release files, so replacing it with a symlink would remove
tracked content. Both retail trees are ignored; never stage them wholesale.

### Major blockers found in source

| Area | Concrete evidence | Consequence |
| --- | --- | --- |
| Platform selection | `neo/idlib/sys/sys_defines.h` reports `Unknown Platform` outside Windows and defines `ID_OPENGL` unconditionally | Add explicit PS2 and host-test configurations instead of pretending the EE is Windows |
| Shared headers | `sys_includes.h`, `sys_intrinsics.h`, `sys_threading.h` pull in Windows, DirectX, SSE and Win32 handles; `precompiled.h` pulls in OpenGL and most subsystems | Establish portable public header boundaries before attempting bulk compilation |
| Modern C++ | `sys_types.h` defines a `nullptr` macro; alignment/assert macros use `UINT_PTR` and MSVC extensions | Remove obsolete keyword emulation and port platform macros deliberately |
| Error handling | `Common.cpp`, `common_frame.cpp`, `Common_printf.cpp`, `Thread.cpp`, script compiler/program and game class allocation contain throws/catches | Replace error control flow; do not erase `try`/`throw` with macros |
| RTTI | Casts occur in model/file code, `ui/*`, and many `d3xp/menus/*` files; `CVarSystem.h` uses `typeid` | Add checked type queries or remove genuinely excluded callers |
| Thread startup | `Common.cpp` starts the game worker; `ParallelJobList.cpp::Init` creates workers even when job submission is configured synchronous | Setting `com_smp=0` and `jobs_numThreads=0` alone is insufficient; First approach will be single-threaded, stub these out and execute code in-place/serially. |
| Renderer memory | `tr_frontend_main.cpp` allocates two 64 MiB frame arenas | Stock renderer initialization cannot run within EE RAM |
| Vertex storage | `VertexCache.h` requests 186.5 MiB across static and double-buffered vertex/index/joint GPU buffers | Replace the desktop buffer architecture; reducing texture sizes alone will not solve memory use |
| Classic Doom | `Common_local.h` embeds a 2,304,000-byte classic image buffer; common, menu, achievement and sound code reference Classic Doom | Removing the classic library from the link is insufficient |
| Save preload | `Common.cpp::Init` reserves save buffers; `sys_savegame.h` specifies 4 MiB plus a 400 KiB string table | Defer these allocations. The nearby “20 MB” comment is stale; save games come later, disable these for now. |
| Script storage | `Script_Program.h` contains 296,608-byte globals/defaults and space for 131,072 statements inside the game object | Inspect actual EE `.bss`/layout, not just allocator totals |
| Sound | `sound/snd_local.h` includes DirectX/XAudio2 headers and concrete XA2 classes | Separate logical sound interfaces from device implementation |
| World loading | `RenderWorld_load.cpp::InitFromMap` reads binary world data into memory and retains area models | Portal visibility is useful, but does not provide asset streaming by itself |
| Container loading | `File_Resource.cpp::idResourceContainer::Init` loads all of `_ordered.resources` into memory, in addition to resource index structures | Use bounded reads and compact indexes when supporting BFG containers |
| Game initialization | `d3xp/Game_local.cpp::Init` starts scripts and requires `aas_types` declarations | Asset-free core boot and game initialization require different tests |

The project manifests are useful source inventories, not portable build definitions. The initial audit found 55 idlib C++ units, 199 executable units, 130 game-d3xp units, and 56 external units. Reconcile these counts into explicit include/exclude/replacement lists during implementation; never use a recursive “compile everything” wildcard.

## 3. Repository structure and conventions

Mirror the reference project's root Makefile, `src/ps2`, `src/tools`, data directory, and per-configuration build tree. Move `neo` intact beneath `src` to preserve Doom's own subsystem names and most relative includes. Do not rename its C++ engine into Quake's C subsystem layout.

```text
Makefile
_clang-format
README.md
AGENTS.md
CLAUDE.md
D3BFG_README.txt
LICENSE.txt
base/                           # keep tracked source content; generated/retail files ignored
gamedata/                       # Doom 3 BFG and RoE game data; stays local, never committed
src/
  external/                     # third-party dependencies: dlmalloc now; pinned VU tools later
  neo/                          # existing neo/ moved intact
    idlib/ framework/ d3xp/ cm/ aas/ ui/ swf/ renderer/ sound/ sys/
  ps2/
    common.h                    # controlled bridge to Doom's public interfaces
    system/                     # entry, sys, IOP/storage, heap, threading
    debug/                      # logs, assertions, exceptions, frame metrics
    input/ audio/ save/
    math/                       # Single-precision optimized EE math functions and VU0-macro mode vector math
    renderer/                   # GS, VRAM, textures, packets, VU, Doom adapter
  tools/
    host/                       # converters, validators
    scripts/                    # build metadata, smoke runner, comparisons
  tests/
    unittests/                  # small, redistributable inputs; no retail assets, host unit tests and small PS2 tests
    smoketests/                 # larger integration tests designed to run on the PS2 - target boot/math/render/game scenarios
build/
  debug/ release/ tools/ tests/
  test-results/<run-id>/
docs/                           # asset format, budgets, port status, reuse inventory
  IMPLEMENTATION_PLAN.md
  CVARS.md
.claude/rules                   # agent's documentation, skills and knowledge about the project and PS2 hardware/SDK
```

The engine now lives under `src/neo/`. Commit `4e5f082` moved the original blobs intact,
without source edits. File tables below retain the original `neo/...` inspection paths;
their implementation destinations are `src/neo/...`. The backend and test scaffolding
implements M0–M2; later subsystem entries remain proposals. Third-party dependencies
imported for this port live in `src/external/`, including future vclpp/vu-checker
tool dependencies.

Perform the `neo` move as a mechanical commit. Audit project-relative paths, includes outside `neo`, debugger configuration, and data lookup immediately afterward. Keep runtime paths independent of the source tree. Leave `doomclassic/` in place and excluded until its separate deletion gate.

New backend/tool code follows the reference's PascalCase types and functions, camelCase variables, `m_` members, `s_` local statics, `g_` exported globals, lowercase namespaces, `alignas`, file banners and `_clang-format`. Preserve its four-space/Allman layout, updating obsolete formatter options before making formatting mandatory. Use PS2SDK sized types at SDK interfaces and `<cstdint>` in shared host/target formats; equal-width typedefs are not necessarily pointer-compatible on the EE ABI. Define debug/assert/profile switches to `0` or `1` and test them with `#if`.

Preserve original Doom formatting in modified upstream files and mark changes with `// [PS2_D3BFG]: <reason>`, analogous to `PS2_QUAKE`. Preserve copyright notices; new Doom-specific file banners must reflect this project's licensing, not blindly copy the reference's GPL version line.

## 4. Reuse strategy

Copy narrowly selected components into this repository with source commit and adaptation notes. Do not make the build depend on the absolute path of the Quake repository. Do not modify that repository to serve this port.

| Reference component | Intended reuse | Doom-specific work |
| --- | --- | --- |
| Root `Makefile`, `_clang-format`, build scripts | Target/compiler discovery, strict warnings, debug/release split, stripped/unstripped artifacts, compile database | Replace explicit manifests, names, config defines and data paths; track flag changes in dependencies |
| `src/ps2/system/main.cpp`, `iop_boot.*` | IOP bring-up and host/HDD/USB selection | Call Doom bootstrap; detect Doom fixture/package paths; start with host storage, add physical storage after boot |
| `system/heap.*`, `system/dlmalloc/` | One heap, tagged accounting and peak windows | Keep the Doom heap adapter in `system/` and import the allocator into `src/external/dlmalloc/`; map Doom tags and audit allocation-before-main and unsized-delete accounting |
| `debug/exception_handler.*`, `log_file.*`, `scr_print.*` | Fatal/crash diagnostics, persistent logs, emergency screen | Doom prefixes, matching ELF symbols, heap-independent early/fatal logging |
| `input/pad.*`, `input/keyboard.*` | Hardware polling and controller initialization | Convert events into Doom key/usercmd semantics; preserve menu and PDA navigation |
| `renderer/gs.*`, `vram.*`, `gif_writer.h` and packet/VU support | GS setup, local-memory allocator, packet safety, synchronization patterns | Doom materials, coordinate conventions, per-texture CLUTs, draw submission and lifetime rules |
| `math/vec_mat.*` and VU examples | EE/VU implementation patterns | Validate against Doom conventions and scalar results before replacing math |
| `audio/audsrv_device.*`, `mix_ring.*`, `music_stream.*`, `spu_adpcm.h` | Device output, bounded buffering, ADPCM encoding/stream patterns | Adapt Doom sound voices, shader semantics, timing and positional mixing; Quake sound API is not reusable as-is |
| `save/memcard.*`, storage/device support | Low-level card/storage operations | Doom serializer and larger save format, failure recovery, versioning and temporary-file policy |
| `src/tools/host/musenc.cpp` | Audio conversion algorithms and tool organization | Doom sample metadata, loop points and channel policy |
| `src/tools/scripts/symbolize.py`, `gen_compile_commands.py`, `frame_log/*` | Symbolization, build tooling, packaging patterns, performance reports | Doom names, schema and output roots; isolated test data/configuration |
| `src/ps2/tests/*` | In-game test state-machine and pass/fail patterns | New Doom scenarios; do not transplant Quake command aliases or map assumptions |

Quake BSP/MD2 loaders, its `refexport_t` seam, Quake gameplay, and Quake-specific save tables do not fit Doom. Reuse the GS machinery beneath those interfaces. The reference uses a shared palette for indexed textures; Doom materials will need their own palette/residency policy.

## 5. Build and portability design

### Build targets and policies

Base the root Makefile on the working Quake configuration. Provide separate source groups for legacy engine C++, third-party C/C++, new backend C++, host tools and target tests.

- `make` and `make release`: initially the current validated PS2 milestone, ultimately `build/<config>/d3bfg.elf` and `d3bfg_unstripped.elf`.
- `make compile-core`: compile the portable idlib/framework subset.
- `make compile-game`: compile the intended campaign engine/game source set, even while only a smaller boot executable is runnable. Report exactly which subsystems remain replaced or excluded.
- `make test-host`, `make smoke`, `make smoke-render`, `make tools`, `make compiledb`: tests and tooling with explicit outputs and exit status.
- Always emit a link map, section totals, largest symbols, compiler version and source/config identity. Stripping debug symbols reduces file size, not loaded code/data/BSS memory.
- Use C++20 with `-fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-strict-aliasing` for target C++.
- Reuse the reference's `-Wall -Wextra -Werror` plus shadow, conversion/sign-conversion, format, undef, pointer, alignment, virtual, VLA and GCC logic/duplication diagnostics for new code. Apply compiler-supported equivalents on host Clang.
- Permit documented, source-specific suppressions for legacy code and system-header treatment for SDK/vendor headers. Keep new project headers checked. Do not use project-wide `-w` to hide problems introduced by the port.
- Begin with debug `-O2` and release `-O3` as in Quake; use `-Os` for measured cold paths. Evaluate function/data sections and linker garbage collection with checks for static game-class/CVar registration. Do not let dead stripping manufacture a misleading “game compiled” claim.
- Add configuration/flag stamps so changing flags rebuilds affected objects; the reference warns that its existing build does not track these changes.
- Use a host compiler without EE SDK include paths for host logic tests. EE builds remain the authority for target ABI, warnings, alignment, intrinsics and linkability.

### File/function change map

Paths in this table are relative to the current `neo/` unless they explicitly name the new `ps2/` backend.

| Current file(s) / function(s) | Planned change |
| --- | --- |
| `idlib/sys/sys_defines.h`, `sys_builddefines.h` | Add `ID_PS2` and a separate host-test platform; define endian, path, format, noreturn, inlining and alignment explicitly; select GS instead of unconditional OpenGL |
| `idlib/sys/sys_includes.h`, `sys_types.h`, `sys_assert.h` | Split Windows includes; remove `nullptr` emulation for modern C++; use pointer-width-safe arithmetic and portable assertions; audit layout/ABI assumptions |
| `idlib/sys/sys_intrinsics.h`, `idlib/math/Simd.cpp`, `Simd_Generic.cpp`, geometry/math inline headers | Select scalar code, remove unconditional x86 dependencies, provide valid cache/prefetch helpers; validate generic fallbacks actually compile |
| `idlib/precompiled.h`, renderer public headers, `sound/sound.h` / `snd_local.h` | Isolate GL/DirectX concrete types from engine/game contracts; avoid broad SDK include leakage into new code |
| `idlib/Heap.cpp` and allocation declarations in `Heap.h` | Route `Mem_Alloc`, `Mem_Alloc16`, frees and relevant new/delete forms through one tagged heap; enforce alignment and overflow checks |
| `sys/sys_public.h`, new `ps2/system/sys.*` | Implement print/error/quit, monotonic time, filesystem enumeration, paths, event queue and platform services; explicitly reject unsupported DLL/process operations |
| `framework/File.cpp`, `FileSystem.cpp`, `idlib/sys/sys_filesystem.h` | Replace Windows file APIs and removed `std::auto_ptr`; handle short reads, seeks, path bounds, enumeration and real timestamps or a documented target policy; preserve logical resource paths; fix vararg promotion assumptions in `FS_WriteFloatString` for LP64 host tests |
| `idlib/sys/sys_threading.h`, `Thread.cpp`, `ParallelJobList.cpp` | Add PS2 primitives where necessary; disable worker creation for initial synchronous scheduling; preserve job dependency/completion semantics |
| `framework/Common.cpp::Init`, `Shutdown`, `LoadGameDLL`, `common_frame.cpp::Frame` | Introduce explicit startup stages/capabilities; static game linkage; synchronous frame execution; defer UI, saves, renderer/audio device startup |
| `framework/Common_printf.cpp::Error`, `FatalError`, Common/Thread try/catch blocks | Initial target errors become explicit fatal termination with diagnostics; introduce recoverable status propagation where needed later |
| `d3xp/script/Script_Compiler.cpp`, `Script_Program.cpp`, `d3xp/gamesys/Class.h`, `Game_local.cpp` | Convert exception paths; retain useful source-location errors; bound compiler storage and class allocation; restore cleanup semantics explicitly |
| `d3xp/Game_local.cpp::TestGameAPI` | Initialize `gameImport_t`, including `version = GAME_API_VERSION`, before the current API self-test reads it; do not suppress the uninitialized-data issue |
| `framework/CVarSystem.h`, `File_Resource.cpp`, `sys/sys_savegame.cpp`, `renderer/ModelManager.cpp`, `Model_md5.cpp`, `ui/*`, `d3xp/menus/*` | Replace RTTI with existing engine type information or small explicit checked type queries; remove casts only when their excluded subsystem is truly absent |
| `framework/Common*.{h,cpp}`, `common_frame.cpp`, `d3xp/Achievements.cpp`, classic-facing menus, XA2 sound files | Remove classic includes/calls/embedded image storage and classic launcher paths; preserve single-game behavior |
| `sys/sys_session*.{h,cpp}`, `sys_localuser`, `sys_signin`, `sys_profile`, achievements/stats | Implement a minimal offline local-user/session path sufficient for campaign state changes; excluded online operations report unsupported behavior |

This is an initial dependency map. The first real compiler pass will produce an additional reviewed blocker list, especially for MSVC template lookup, pointer conversions, CRT functions and implicit integer-size assumptions. Do not enable RTTI/exceptions merely to bypass that work.

The EE floating-point behavior also needs explicit target tests: host IEEE Inf/NaN and double-precision assumptions do not transfer directly. Audit zero normalization, half-float conversion, sentinel values and precision-sensitive collision/math paths. Keep scalar reference fixtures for packed vertices and joints before adopting VU optimizations. Retain `_D3XP` and static game linkage; do not import or call the supplied x86 game DLL.

### Error, threading and allocation trade-offs

The first executable may terminate on recoverable desktop errors. That is acceptable only if the limitation is documented and failures produce a useful reason. Before normal gameplay/save loading, restore recoverable failures with explicit results and ownership cleanup. `longjmp` across C++ objects, unchecked `static_cast` substitution, and macro-eliminated exceptions are not acceptable shortcuts.

Start with one engine thread. `ParallelJobList::Init` must not create workers; job submission runs inline with tested ordering. Storage/audio drivers may still have asynchronous hardware behavior, so single-threaded gameplay does not eliminate DMA cache or buffer-lifetime requirements. If later audio/IO work introduces another EE thread, revisit allocator locking and all local-static initialization first; the reference dlmalloc is configured without locks.

Preserve normal upstream allocations initially but account for every byte. Use bounded frame scratch, pools and stable level allocations for new hot paths; no per-draw heap churn. Verify sized/unsized and aligned delete accounting: the reference allocates by requested size but can free by allocator usable size. Also account for constructors running before `main`; measuring system memory from the current break alone can double count their allocations.

## 6. Headless boot architecture

Headless means no GS renderer or audible output is needed to execute the selected engine stage. A diagnostic emergency screen may be available, but must not be required for success.

Use explicit bootstrap modes rather than dozens of ad hoc null checks:

1. **Platform boot:** IOP/stdout, timer, heap, exception report and a bounded test loop. Confirms ELF loading and SDK integration only.
2. **Engine core boot:** initialize real `idLib`, command/CVar systems, console/logging, filesystem, event processing and synchronous job services; execute a test command and exit/idle with a machine-readable result. No game initialization claim.
3. **Game fixture boot:** initialize declarations, logical models/materials/world, collision/AAS, script and actual `idGameLocal` against a minimal authored fixture. Advance game frames and verify state. A renderer/sound substitute must preserve the queries gameplay depends on.
4. **Retail map headless boot:** run actual campaign initialization and a selected converted map, including script, collision and AI events, within measured memory limits.

Stage 2 should branch deliberately within the Common startup lifecycle after the required services, with matching partial-shutdown bookkeeping. It must not simply return from the platform entry point before invoking Doom code. Stage 3 must satisfy `program.Startup(SCRIPT_DEFAULT)` and `aas_types`; tiny checked-in fixtures or locally generated test content supply these dependencies. Inventory additional startup declarations as the tests progress.

For headless stages, exclude the desktop backend and prevent its allocations, not just `r_skipBackEnd`. New `ps2/renderer/null_render.*` and `ps2/audio/null_sound.*` provide narrowly defined contracts. They may discard output, but material lookup, model bounds/joints, render-world portals/traces, sound duration and trigger timing require meaningful behavior by the game-fixture stage. Unexpected calls fail with the method name instead of silently succeeding.

Make the distinction visible in test markers, for example `D3PS2:CORE_BOOT:PASS` versus `D3PS2:GAME_FIXTURE:PASS`. Reserve a campaign-map marker for successful real map load plus simulation ticks. Build-only archives and a tiny linked executable do not establish campaign feasibility.

## 7. Memory feasibility and budgets

Treat the 32 MiB EE, 4 MiB GS and IOP/SPU2 resources as separate budgets. Measure loaded image+BSS, stacks, heap arenas, live allocations, fragmentation and transition peaks; do not report only stripped ELF size or requested heap bytes.

The following is an initial allocation envelope to test, not a prediction of what Doom will fit into:

| EE use | Initial envelope (MiB) |
| --- | ---: |
| Loaded program, static data/BSS, kernel/system reserve and stacks | 8.0 |
| Additional game/script/entity/AI/physics allocations | 6.0 |
| World topology, collision and navigation | 4.0 |
| Resident model/animation data | 3.0 |
| CPU texture cache and staging | 3.0 |
| Audio samples/stream buffers in EE RAM | 1.0 |
| Renderer frame scratch, geometry staging and DMA packets | 2.0 |
| Shared file/decompression/save scratch | 1.5 |
| Allocator overhead and transition allowance | 1.5 |
| Required uncommitted safety margin | 2.0 |
| **Total** | **32.0** |

Inline game/script storage belongs to the BSS row, not also to the dynamic game row. Scratch reuse requires proven non-overlapping lifetimes. Rebalance these envelopes after the first linked image and startup measurement; if code/static state alone consumes too much, reduce optional retained code and data before trying maps.

Propose 512×448 with two 16-bit framebuffers and one 16-bit depth buffer for the first GS prototype. Their raw footprint is 1.3125 MiB, leaving 2.6875 MiB of the 4 MiB GS space before page alignment, palettes and other allocations. Validate the real VRAM layout using the reused allocator. Video-mode alternatives come after a captured baseline.

Mandatory memory gates:

- Core boot has a recorded stable baseline and preferably uses no more than half EE RAM in total; exceeding that target triggers investigation before game startup.
- Campaign work retains at least 2 MiB measured headroom through the worst tested load, save and transition, including allocator overhead. This is a proposed acceptance threshold, subject to hardware findings.
- Remove desktop renderer arenas, eager save buffers, classic image state and unused thread stacks before interpreting a game-load failure as an asset-size problem.
- Free previous-level residency before loading the next level where semantics permit. Capture a fresh peak window for every transition, not only after load completion.
- Do not arbitrarily lower script/entity/physics limits until representative campaign data is measured. Oversize content must fail with a report identifying the resource and budget.
- Test repeated load/unload cycles and ensure retained state is stable, with allowances explicitly listed for intentional caches.

If resident collision, navigation, scripts and necessary game state exceed the envelope after conversion, pause graphics optimization and reassess world partitioning/content requirements. Streaming render assets does not automatically make the full simulation fit.

## 8. Renderer adaptation

Keep Doom's scene/entity/light/material contracts and progressively port the useful parts of the front end. Introduce a PS2 submission layer below those contracts; do not emulate OpenGL or preserve the desktop GPU-buffer architecture.

| Doom area | Plan |
| --- | --- |
| `RenderSystem.h`, `RenderSystem_init.cpp`, `RenderSystem.cpp`, `tr_local.h` | Separate frontend/world responsibilities from GL initialization/backend; connect PS2 begin/end/present and capability reporting |
| `tr_frontend_main.cpp::R_InitFrameData` and frame allocation helpers | Bounded arena(s), measured peak, explicit lifetime before DMA completion |
| `tr_frontend_main.cpp::R_RenderView`, `R_AddLights`, `R_AddModels` in frontend files | Preserve scene extraction/visibility while suppressing desktop interaction and shadow work that the PS2 backend will not consume |
| `VertexCache.*`, `BufferObject.*`, `GuiModel.*` | CPU/EE-compatible handles and bounded transient geometry; no GL buffers, GPU skinning buffers or desktop GUI capacities |
| `RenderWorld_load.cpp`, `RenderWorld_portals.cpp`, frontend scene submission | Preserve portal topology/visibility; add converted geometry residency without invalidating world/entity pointers |
| `Material.cpp::Parse`, `EvaluateRegisters` | Retain time/entity-driven behavior; compile a supported subset to fixed-function passes; log unsupported stages deterministically |
| `Image_load.cpp`, `BinaryImage.*`, `Image.*` | Load converted GS-friendly formats and palettes; bounded upload/cache policy; explicit alpha and power-of-two texture semantics |
| `Model_md5.cpp`, `neo/d3xp/anim/*` | Start with scalar CPU skinning (`r_useGPUSkinning=0` path); later batch/quantize and move measured transform work to VU |
| `OpenGL/gl_backend.cpp::RB_ExecuteBackEndCommands`, `tr_backend_draw.cpp::{RB_DrawViewInternal,RB_DrawInteractions,RB_StencilShadowPass}`, `RenderProgs*`, desktop shadow jobs | Exclude desktop backend, shader pipeline and stencil-volume path from PS2; replace only required draw behavior |
| `Cinematic.cpp` | Existing released implementation does not supply working Bink playback; add an explicit converted-video path later if campaign content needs it |

Lighting progression:

1. Unlit textured surfaces, correct depth/culling/clipping, alpha tests, transparency, GUI and animated model poses.
2. Offline static illumination represented by vertex colors or lightmaps. Compare memory and visual results in a small room before choosing the dominant format.
3. Bounded dynamic vertex lighting and/or a limited additive light pass for moving lights, muzzle flashes and the flashlight. Preserve light activation and movement semantics, even when visual fidelity is reduced.
4. Cheap contact/blob shadows and selective special effects if the frame and VRAM budgets allow them.

Static baking alone is insufficient: Doom has switchable lights, moving doors, scripted darkness and a gameplay-critical flashlight. The converter must classify bakeable contributions, retain dynamic controls, and avoid leaving illumination baked on after a scripted switch-off. Full per-pixel normal/specular lighting and stencil shadow volumes are outside the initial target.

GS blending cannot directly multiply arbitrary RGB lightmap color by diffuse color. Evaluate Quake's lightmap-intensity-in-alpha plus vertex-chroma approach as a concrete candidate. Doom's `.proc` data does not contain ready-made Quake lightmaps: producing lightmap UVs and baking illumination are new host-tool work. Capture tests must assess the resulting color and darkness trade-offs.

Start with scalar EE transformations and a simple GS packet path that can be compared against reference math. Introduce Quake's VU tooling only after that path produces deterministic captures. Verify triangle orientation, clip-space depth, near-plane clipping, texture addressing, 0x80 GS alpha conventions, synchronization and per-texture palette changes independently.

## 9. Asset conversion and loading

Create host tools under `src/tools/host` and shared fixed-width disk-format definitions.
Conversion consumes `gamedata/d3_bfg/`, writes to a separate output directory, and never
edits the source installation. Keep proprietary input/output assets out of commits.

Begin with a BFG container and dependency inventory before designing every final format.
Use `framework/File_Resource.*`, `File_Manifest.*`, renderer binary/text loaders, MD5
loaders, collision and AAS readers as format references. Report per-map dependencies,
dimensions, counts, animation/joint sizes, material features, sound durations and
estimated runtime residency. The installed BFG `.resources` are the primary input;
support loose RoE source formats only as a separate, justified path. Quake PAK, original
Doom 3 PK4 and BFG resource layouts are not interchangeable.

Proposed converter stages:

- Extract/index resources and declarations with normalized logical paths and deterministic ordering.
- Reduce diffuse/emissive textures by policy; evaluate indexed 4/8-bit and 16-bit formats, mip chains, alpha masks and per-material palettes. Decode desktop compression on the host rather than retaining a target DXT pipeline.
- Convert static geometry to bounded batches with local indices and quantified position/UV error. Preserve portal boundaries, seams, material divisions, entity origins and trigger/collision relationships.
- Convert MD5 meshes/animations with stable joints, animation names, frame commands and attachment semantics. Begin with unmodified joint/weight behavior; reduce influences, precision and frame data only against a pose-error test.
- Preserve map entities, scripts, collision and AAS first. Lower visual detail without changing walkable surfaces, door collision, trigger volumes or AI connectivity. Navigation regeneration is a separate validated step if geometry eventually requires it.
- Decode the BFG sound resources on the host and convert sound to bounded sample/stream formats using the reference ADPCM tooling where appropriate. Preserve loops, duration, cue timing and shader metadata. Plan an explicit Bink replacement/transcode path for video materials and PDA content; the released cinematic implementation does not supply Bink playback. RoQ is relevant only to the optional RoE import path.
- Package level/area resources in independently readable chunks with bounded decompression scratch. Avoid whole-package decompression into RAM.

Every format needs magic/version, explicit endian and integer widths, checked counts/offsets/lengths, alignment, checksum and converter version. Never serialize native C++ structs or host pointers. Reject overflow, truncated blocks, unsupported versions and invalid asset references before allocation.

Preserve a dependency manifest and measured size report for each converted level. A first version can use whole-level residency for small fixtures. Introduce area-based render streaming only when that baseline works: retain world/portal identity, prefetch adjacent areas, pin assets used by active entities and in-flight DMA, and apply bounded eviction. Gameplay/collision/AI streaming requires separate lifetime/state design and is not implied by render streaming.

At the loader boundary, replace `_ordered.resources` eager buffering in `idResourceContainer::Init` and `.bproc` whole-file buffering in `InitFromMap` with bounded file/chunk readers. Audit `Common_load.cpp::ExecuteMapChange` ordering: it begins filesystem level work before `UnloadMap`, and later preloads assets before world/game initialization. Measure and eliminate avoidable old/new overlap there.

Offline script compilation is a later option if runtime compiler memory proves material. It requires a versioned bytecode/global/event format and validation against interpreter behavior; it is not a shortcut for skipping the script port.

## 10. Audio, input, UI, saves and campaign completeness

Silent headless boot is an early milestone, not the final sound design. Separate logical sound worlds/emitters from XAudio2 hardware. Preserve playback state, duration, amplitude queries used by materials, and completion-driven behavior before replacing the output device. Then bring up audsrv/mixing/streaming using bounded buffers; measure EE cost before expanding voice count.

Map controller state into `framework/UsercmdGen.cpp`/key input and the game's expected commands. Test dead zones, simultaneous actions, hot-plug/reconnect, UI focus, PDA selection and world GUIs. A basic launcher/menu may replace the BFG shell, but interactive screens, inventory/PDA information and script-required UI behavior must remain usable for campaign progression. `ui` and `swf` cannot simply be declared out of scope forever.

For saves, inspect `framework/Common_load.cpp::{SaveGame,LoadGame}`, `File_SaveGame.*`, `sys/sys_savegame.*` and `d3xp/gamesys/SaveGame.*`. Replace eager multi-megabyte reservations and desktop threaded save transport with bounded, versioned serialization. Start on host files, then test memory-card/device support using the reference's low-level code. Preserve recoverable handling of insufficient space, absent devices, interrupted writes and incompatible saves; never corrupt the previous valid slot on a failed write.

Campaign validation must eventually include level transitions, death/reload, checkpoints/manual saves, scripted scenes, elevators/doors, AI navigation, weapons, flashlight, interactive GUIs and the ending. Simplified visuals must not silently break those systems.

## 11. Automated testing and screenshots

### Host tests

Keep tests in the repository instead of relying on temporary harnesses. Test pure logic against real implementation with only hardware boundaries substituted. Run ASan/UBSan where supported, while recognizing that EE float behavior and DMA require target coverage.

- Heap alignment, requested/usable-size accounting, allocate-before-main bookkeeping, tag totals, overflow and failure paths.
- Path normalization, enumeration, storage selection, bounded reads/seeks and missing/truncated files.
- Converter parsing, explicit-endian round trips, quantization error, malformed counts/offsets and package dependency validation.
- Scalar vector/matrix/skinning/clipping behavior and synthetic geometry with known answers.
- Script error reporting/cleanup, tiny interpreter fixtures, checked runtime type queries and synchronous job ordering.
- Capture pixel-format/orientation conversion and image comparison on known patterns.
- Test-runner classification of timeout, crash, stale log, missing marker, missing image and expected negative-test failure.

A host stub compile is never a substitute for building the same subsystem with the actual EE compiler. Avoid importing every engine header into a host harness merely to test a small parser.

### PCSX2 runner

Add `src/tools/scripts/run_pcsx2_test.py` and structured test scenarios. Follow the verified reference launch form `PCSX2 -batch -elf <elf>`; verify any additional CLI switches against the installed version before relying on them.

Each run receives a unique fixture/data directory beside its ELF, a controlled configuration, a test identifier, fixed seed and bounded frame/tick count. Use an explicit test manifest or boot argument read by the port. Do not patch source, queue Quake aliases, or rely on `autoexec.cfg` to script a run.

Capture stdout/emulator logs and engine result files under `build/test-results/<run-id>/`, together with ELF hash, unstripped symbols, build flags, fixture/package hash, emulator version/settings and timing/memory summaries. Start with a fresh log or seek from a known start position; PCSX2 overwrites its log on launch. Success requires the expected stage marker and structured result, not just a process exit code.

Use a watchdog with explicit timeout and capture failure context before terminating only the process started by the runner. Do not close an unrelated emulator session. Prefer an isolated emulator profile if the installed CLI supports it; otherwise serialize runs and back up/restore touched settings while PCSX2 is closed. Do not alter the user's existing Quake configuration or memory card for tests.

Tests cover both debug and release at milestone boundaries. First cover host storage; later test missing data, USB/HDD fallback, no card/full card, short reads and real storage images. Reference-port HostFs flakes must be diagnosed by matching stack/symbol evidence; never blanket-ignore all TLB errors or turn a failed run green by retrying.

### Capturing the renderer without desktop screenshot permissions

The installed SDK supplies `ee/include/screenshot.h` and source `ee/debug/src/screenshot.c` with `ps2_screenshot` and `ps2_screenshot_file`. It reads GS memory through reverse VIF1 DMA and can write TGA. The Quake port does not already contain a framebuffer capture feature.

Use this as a source reference for a debug-only `ps2/renderer/capture.*` implementation and a deterministic `ps2_capture`/test capture request. The stock helper is not robust enough to call blindly: its file function returns zero for success and failure, ignores write results, omits truncation, assumes width-derived pitch, and its readback has unbounded waits and an early-exit state-restoration hazard.

Required capture behavior:

1. Capture a named completed frame at a fixed simulation tick/camera/seed, with the correct front/back buffer and field convention recorded.
2. Drain/fence GS, GIF and VIF work before reversing the bus; prevent any concurrent packet submission.
3. Use explicit GS address/pitch units and supported pixel format; validate DMA qword sizes and use cache-safe aligned, bounded strips.
4. Save/restore affected GS/VIF/DMA state on every path and bound waits. Route a timeout through pipeline diagnostics.
5. Write a checked, truncated output file under the run's writable host path. Use strip buffers rather than another full framebuffer allocation.
6. Convert to PNG on the host, retaining the raw capture and metadata. Verify orientation, channels, alpha, dimensions and content against a color-grid fixture.
7. Compare exact expected regions for primitive tests and documented pixel tolerances for scene regressions. Produce a difference image and statistics, then inspect the actual capture visually.

PCSX2's configured F8 screenshot is a fallback for early manual checks. The engine readback path is the automation target and also supports eventual hardware captures. Capture runs are excluded from performance measurements because readback/IO stalls the pipeline.

PCSX2 results validate functional rendering and regressions under a recorded configuration. They do not establish real EE cache correctness or GS throughput. Hardware runs remain required before performance and stability claims.

## 12. Milestones, acceptance gates and commits

Work in small commits; each implementation commit states its behavior change and validation. Do not push without express authorization. Implementation is now authorized. Inspect the current branch/worktree before implementing and preserve unrelated user work.

| Milestone | Work and likely commit boundaries | Acceptance gate |
| --- | --- | --- |
| **M0: structure and build inventory** | Mechanical `neo` move; root Makefile/tool discovery; style/config docs; source inclusion/replacement/deletion inventory; reuse provenance | Reproducible source lists; correct tool/version report; no classic source in any new target; no implementation hidden in the move commit |
| **M1: core EE portability and compilation** | Header/platform split; scalar idlib; heap/error/RTTI changes required by core; platform services; explicit campaign compile manifest | Core EE objects compile with required language restrictions; new code passes strict warnings; linkable platform probe plus explicit remaining game blockers; this is partial engine compilation |
| **M2: platform and core headless boot** | Runtime entry/IOP/logging; timer/heap tests; synchronous jobs; staged Common init; test runner and fixture root | PCSX2 emits distinct platform/core PASS results; real Doom command/CVar/filesystem services exercised; bounded run and useful negative-test failures; recorded image/startup memory |
| **M2b: campaign source compile and resident link** | Complete no-exception/no-RTTI conversion for the intended runtime manifest, including game/script and required UI interfaces; supply declared subsystem replacements | `make compile-game` passes for every intended runtime unit; link the game-fixture ELF with no unresolved symbols or desktop/classic dependencies; verify class/CVar registrations and retained code in its map; explicitly list replacements and deferred source-import/editor features |
| **M3: headless game fixture** | Decl/script/error propagation; logical render/sound contracts; offline session; collision/AAS/game registration; tiny authored map and scripts | `idGameLocal` initializes and advances deterministic game ticks; entity/script/collision assertions pass; malformed input fails usefully; memory stabilizes on reload |
| **M4: GS primitives and capture** | Reused GS/VRAM/packet layer; CPU triangle path; image readback and host comparison | Captured color grid, textured/depth-tested geometry, clipping/alpha/CLUT fixtures pass; readback failure restores state; captures are visually inspected |
| **M5: inventory/converter and one campaign map** | Asset inventory, first versioned formats, bounded resource loader; first real map headless, then static textured world | Conversion report and hashes reproducible; map scripts/collision/portals load within budget; captured fixed-camera views have correct geometry/material placement |
| **M6: gameplay rendering and presentation** | MD5 animation/skinning, simplified lighting/flashlight, input, essential HUD/PDA/world GUI, sound | Playable scripted section with enemy/weapon/door/light/UI interactions; deterministic captures and state checks; no missing essential UI or silent timing breakage |
| **M7: residency and optimization** | Texture/model streaming; transition ownership; VU paths; measured batching and LOD | Stress maps/transitions retain headroom; scalar/VU tests agree within bounds; hardware measurements guide performance work; no stale-DMA asset eviction |
| **M8: saves, campaign sweep and release work** | Bounded saves/card devices; campaign regression matrix; physical storage; packaging and docs | Save/load and progression tests pass; debug/release and hardware validation recorded; classic tree deleted once uncoupled; reproducible local release package |

M2b is required before M3. M4 and the host asset inventory can proceed independently after M2 while M2b/M3 mature. The first retail-map trial requires both game initialization and compatible converted content. Optimization should not conceal unresolved loading/gameplay failures.

Suggested first implementation commits after approval:

1. `Build: establish the PS2 layout and explicit source manifests.`
2. `Platform: make idlib headers and scalar math compile for the EE.`
3. `Memory: connect Doom allocations to the tagged PS2 heap.`
4. `Platform: add PS2 services and remove classic Doom build dependencies.`
5. `Framework: add synchronous headless core startup.`
6. `Tests: automate PCSX2 boot checks and preserve diagnostics.`

Split each further if it mixes independent fixes. Classic deletion and changes to game error/RTTI semantics deserve separate reviewable commits. A passing M2 concludes the initial core compile/headless slice; full intended campaign-source compilation and resident linking are the explicit M2b gate. Neither establishes campaign playability.

## 13. Open inputs and decision points

- **Approval:** M0–M2 passed; the user has asked to continue the plan. M2b campaign portability is now in progress.
- **Retail data:** the user supplied BFG assets in `gamedata/d3_bfg/` and separate RoE reference assets in `gamedata/d3_roe/`. Inventory the BFG resource containers and resolve logical runtime paths before a campaign initialization claim. Core and synthetic tests remain independent of them.
- **Reuse notices:** the user authorizes GPL v3 reuse of reference code they own. Preserve third-party notices and record each import in `docs/REUSE.md`.
- **Hardware:** establish access and the preferred transfer/storage method before requiring physical-console acceptance. PCSX2 can support the initial milestones.
- **Campaign scope assumption:** original Doom 3 campaign first; the BFG expansion code may remain where shared, but expansion content is not the first completion target.
- **Feasibility gate:** after M2/M3 memory measurements and a representative asset inventory, revise the quantitative budgets and streaming plan. If core game state cannot fit, report that evidence and the concrete fidelity/content trade-offs before expanding the implementation.

The initial M0–M2 scope (core compilation and headless boot) is complete. The current
implementation work is M2b, beginning with the full campaign header boundary and
continuing through explicit error/type handling and subsystem replacements. Progress
and remaining resident-link work are recorded in [PORT_STATUS.md](PORT_STATUS.md).
The current core runs real `idCommonLocal::Init` / `Shutdown` through completed-stage
tracking for system, idlib, commands, cvars, fixture filesystem, synchronous jobs and
one offline user/session. Game/presentation/save stages are deliberately deferred.
Classic framebuffer residency and portable startup/frame references are removed; the
static game API import path compiles but is not exercised until the resident game link.
The campaign gate now compiles all 280 retained units in debug and release. The logical
sound header uses `ps2/audio/sound_backend.*` for portable sample/voice/device contracts.
Only unloaded sample bookkeeping is implemented; resource, timing/amplitude and device
operations fail explicitly. Meaningful sound semantics remain required before M3.
Resident linking is the remaining M2b gate. Acceptance evidence must retain these
boundaries: no game ticks, interpreter execution or map loading yet.
