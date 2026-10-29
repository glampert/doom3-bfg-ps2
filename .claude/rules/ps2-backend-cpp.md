# PS2 C++ backend conventions (`src/ps2/`)

The backend follows the conventions established by `quake2-ps2`. Reference-only APIs
in other rule files still need verification when imported. Current backend code spans
`src/ps2/`; target fixtures live in `src/tests/smoketests/`, host fixtures in
`src/tests/host/`, and redistributable fixture assets in `src/tests/data/`.

## Language and structure

- `-std=gnu++20 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-strict-aliasing`,
  with the `-Werror` set in the Makefile's `EE_CXX_WARNFLAGS`. ps2sdk headers are
  `-isystem`, so only our code is held to it. id's C++ is exempt from strict warnings and `-Werror`.
- The C++ host tools are held to the same set plus `-Wpedantic`, with no exceptions, RTTI
  or thread-safe statics, by `src/tools/CMakeLists.txt`, and MSVC to `/W4`, all as errors.
  `make tools` checks only the local compiler; CI checks GCC on glibc, Apple clang and MSVC
  (see [ci.md](ci.md)). For GCC-only warnings before pushing, the EE compiler in its LP64
  ABI stands in for a host GCC, `<filesystem>` included:
  `mips64r5900el-ps2-elf-g++ -mabi=64 -std=c++20 -O3 <flags> -c -o /dev/null x.cpp`.
  Without `-mabi=64` it is a 32-bit target, where `size_t` is 32 bits. Its libstdc++ is
  GCC 15's, so it misses the false positives CI's GCC 13 (and GCC 14) report inside the STL,
  and no local compiler shows MSVC's warnings there: check both on Compiler Explorer
  ([ci.md](ci.md#traps)).
- Allocation goes through `ps2::system::heap` with Doom's `memTag_t` (stored as a checked 16-bit tag).
  Required allocation failure is fatal; `TryAlloc` returns null for checked callers.
- `alignas(N)` is the alignment idiom, not `__attribute__((aligned))`.
- Backend/stdout diagnostics use `ps2::Log` / `LogV` from `system/log.h`, with info,
  warning, error and fatal levels. Required failures use `ps2::FatalError` / `FatalErrorV`;
  `system::heap::Fail` delegates there. The sink currently uses synchronous stdout; on-screen
  fatal reporting remains a TODO. Data-file writes are separate from diagnostics.
- `common.h` provides `ArrayLength`, portable `PS2_PRINTF_FUNC` / `PS2_COLD_FUNC`, and
  `PS2_Assert` / `PS2_AssertMsg` for preconditions. Assertions follow
  `PS2_D3BFG_ASSERTS`; disabled assertions do not evaluate conditions or messages.
  Which checks survive release builds is set by [Validation](#validation-and-error-handling).

## Validation and error handling

- **Load and init time** (boot, subsystem `Init`, catalog/image/font/movie loads, console
  commands): validate fully in every build. Handle failures gracefully (return false or null and
  release partial ownership), or fail by name with `FatalError` when nothing can recover.
- **Render and frame time** (draws, GS packets, texture binds and residency uploads, geometry
  mapping and clipping, frame/scope ownership, per-frame menu and presentation state): checks for
  unexpected states, invalid data and caller bugs are assertions only. Use `PS2_Assert` /
  `PS2_AssertMsg`, or a `#if PS2_D3BFG_ASSERTS` block for formatted details or extra work,
  including whole validation-only passes such as the UI flush preflight. Release builds accept
  crashes and UB there to keep the code slim; debug builds carry the checks. Assert liberally:
  preconditions, invalid arguments, counters, ownership.
- Control flow is not validation. A clipped primitive, a non-convex mask taking the depth path or
  `tex::Bind` returning false for a texture that cannot become resident stays an ordinary branch
  in every build (the UI then asserts that its draw succeeded).
- Keep a call whose result is only checked outside the assertion:
  `const bool drawn = Draw(...); PS2_AssertMsg(drawn, "...");`. A variable only an assertion
  reads belongs inside `#if PS2_D3BFG_ASSERTS` (the disabled macro still names its condition).
- Render-time helpers that report `false` or `Result::Invalid` (`MapRect`/`MapTriangle`/`MapLine`,
  mask clipping, `ValidateTriangle`) check their input limits only in assertion builds; the
  limits are caller preconditions, documented in the header.
- `gs::WaitUntil` deadlines exist only in assertion builds; a hung release build spins.
- Expected-fatal emulator probes of render-time checks run only in assertion builds
  (`make BUILD=release test-renderer` skips them).
- Every branch that takes a fatal error or an unexpected failure is `[[unlikely]]`. Error
  handlers and functions reached only at load, boot, shutdown or from console commands are
  `PS2_COLD_FUNC`, on the header declaration too. It optimizes for size and forbids inlining:
  never put it on per-frame code.
- Retail SWF execution follows the same split: the load-time preflight (`swf_execution.cpp`)
  admits operands and opcodes in every build, while per-frame budgets, live ledgers, callback
  identity, seek and event-handler checks and dependency traces are assertion-only. Release keeps
  the execution scope and its instruction/call/command counters. Gate checks in id's SWF code
  with `#if defined( ID_PS2 ) && PS2_D3BFG_ASSERTS` when their arguments do work (`va`,
  `ToString`, out-of-line calls); the release `ui::swf` hooks are inline no-ops.
- Audio and session API checks still enforce their limits in release.

## Naming

- Types and **all** functions, including file-local `static`/anonymous-namespace helpers:
  PascalCase (`ps2::renderer::gs::Init`, `QwordCount`).
- Variables: camelCase. Private members: `m_`. Public struct members: no prefix.
- File/local statics: `s_` (`s_frame`). Exported globals: `g_`.
- File-scoped variables and constants with internal linkage always spell out `static`,
  even inside an anonymous namespace. File-scoped `constexpr` constants use `static constexpr`.
- Namespaces are lowercase and follow `ps2::module::subsystem`. The module is the `src/ps2/`
  folder (`audio`, `game`, `input`, `renderer`, `system`, `ui`); the subsystem is a cohesive
  unit inside it (`ps2::renderer::gs`, `ps2::system::heap`, `ps2::ui::swf`). A subsystem
  spread over several files keeps one namespace: don't open a namespace per file, and merge
  related code into an existing subsystem before adding one. Code shared within a module,
  or without a clear subsystem, lives at `ps2::module` (`ps2::renderer::BeginFrame`,
  `ps2::audio::SoundSample`); code every module shares lives at `ps2` (`ps2::Log`,
  `ps2::DynCast`). Test code uses `ps2::tests` and `ps2::tests::fixture`. UI code lives
  in `src/ps2/ui/` even when it implements renderer interfaces.
- Merging namespaces can hide or rebind names. A subsystem named like an engine global
  hides it inside the module (`session` in `ps2::system::session`: write `::session`), and
  an unqualified `Shutdown()` silently binds to another subsystem function of that name.
  Give merged functions distinct names (`gs::InitCommandBuffer`, `gs::InitVram`).
- Constants are `k`-prefixed, enum class members take no prefix and use CamelCase.

## File style

- Every file opens with the banner block (`File:`, `Brief:` explaining what and *why*, the GPL
  license sentence: `This source code is released under the GNU GPL-3.0-or-later license.`).
  Copy it from a neighbour such as `system/heap.h`. Group code under
  `// ----...` section headers like the existing files.
- Comments explain decisions and hardware reasons, the way the existing code does.
  Keep header comments brief (1-2 lines). Avoid adding history to comments.
- Formatting follows the root `_clang-format`.
- New C++ code, including host tools, uses braces on their own lines for functions,
  namespaces, lambdas and control-flow blocks. Always brace conditional bodies;
  do not put bodies or complete blocks on a single line, even for short functions.
- Give the code breathing room: generally leave a blank line after a completed block
  before the next independent statement or block.

## Sized types

- Use ps2sdk's `u8/u16/u32/u64` and `s8..s64` from `tamtypes.h` in backend code.
- `<cstdint>` is fine in headers shared with host tools, where `tamtypes.h` doesn't exist.
- **Never mix the two at a ps2sdk pointer boundary.** On the EE ABI `std::uint32_t` is
  `unsigned long` while `u32` is `unsigned int`: same width, incompatible pointer types.

## Passing the strict `-Werror` set

Only *implicit* conversions warn. An explicit `static_cast` is the fix almost everywhere.

- `-Wcast-align`: byte buffer → struct pointer must go through `void*`:
  `static_cast<const T *>(static_cast<const void *>(ptr))` (as image_load.cpp does).
- `-Wconversion`/`-Wsign-conversion`: `int / sizeof(X)` promotes the int to `size_t`, so
  write `static_cast<int>(sizeof(X))`. Size arguments to `memcpy`/`Alloc` need explicit
  `static_cast<size_t>`/`<u32>`.
- `-Wdouble-promotion`: float literals take the `f` suffix. Cast int operands to float in
  float math (`static_cast<float>(n)`), and keep constants float (`constexpr float`).
  `std::floor/ceil` on a float pick the float overload.
- `-Wnull-dereference`: Doom's portable `Sys_Error` is marked `NO_RETURN`, and
  `ps2::system::heap::Fail` is `[[noreturn]]`. Preserve those contracts. In the Quake reference,
  `Sys_Error` lacked that annotation and needed an explicit unreachable return.
  If a helper has any path returning null (e.g. a "measure" mode), every dereference of its
  result warns. Remove that path, e.g. by splitting a sizer type from the allocator. This can
  surface when an int index becomes a pointer.
- `-Wdeprecated-copy`: a user-provided `operator=` on an aggregate errors at every copy site.
  Wrap the *class definition* in `#pragma GCC diagnostic push/ignored "-Wdeprecated-copy"/pop`.
  GCC checks the state at the class, not at the use. Don't declare a copy ctor instead: any
  user-declared constructor ends aggregate init in C++20. A union holding such a member loses
  its implicit copy assignment.
- `-Wsign-conversion` on a template array bound: `T a[N]` with `template<int N>` warns. Write
  `T a[static_cast<u32>(N)]` and keep the parameter `int`, so comparisons against int counters
  stay clean.
- `-Wclass-memaccess`: `memset` of a struct with default member initializers needs
  `static_cast<void *>(&obj)`.
- `-Wduplicated-branches`: `GS_PSM_32 == 0`, so `pal ? GS_PSM_32 : 0` trips it.
- Don't include `io_common.h` in C++: it trips the "fio/fileXio directly" `#error` in the
  clangd view. Define the few `FIO_O_*` constants you need.
- A backend unit that includes `<idlib/precompiled.h>` needs the `ENGINE_BRIDGE_INCS`
  override in the Makefile's per-object list (beside `ui/page_image.o`), which makes the
  engine headers system headers. Without it the core test build compiles id's headers
  under `-Werror` and fails in `sys_types.h`/`sys_public.h`, although production builds,
  whose resident-support group already has the seam, pass.
