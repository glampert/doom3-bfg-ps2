# id's Doom 3 engine C++

The engine source is under `src/neo/`. The initial EE core uses the generic scalar
math path; PS2-specific math replacement headers are a later milestone.

## Editing rules

- Keep id's code as close to unchanged as possible. Platform work belongs in `ps2`
  behind a seam. When an engine change is unavoidable, keep it minimal and tag it:
  `// [PS2_D3BFG]: <why>`.
- **No double FPU on the EE.** Unsuffixed constants in the legacy target are made float by
  `-fsingle-precision-constant`. Explicit upstream double arithmetic remains software
  floating point; its current units are listed in README.md. Don't introduce new double
  math in hot paths, or change precision without a target regression test.

## Core portability findings

- `idCVar`'s old default-constructor RTTI assertion compared a pointer type with a value
  type and was always true. `idInternalCVar` needs this constructor. Portable builds keep
  it without RTTI; deleting the constructor would break real cvar registration.
- `FS_WriteFloatString` accepts no `l` length modifier. `%d`/`%c` consume promoted `int`
  and `%u`/`%x` consume `unsigned int`; reading `long` happens to share width on EE/Win32
  but is wrong on LP64 hosts. Parser integer directives actually store `long`, so they
  use `%ld` and `labs`.
- Upstream `jobs_numThreads=0` did not prevent worker creation in `Init`. The portable
  manager explicitly creates zero workers and uses the original `RunJobs` scalar path
  even for explicit parallelism requests. Preserve syncpoint bookkeeping and predecessor
  completion; a replacement that just calls registered functions misses this behavior.
- A failed plane/ray intersection leaves its scale output untouched. Surface ray tests
  must check the return value before reading scale. Empty trace models have zero silhouette
  edges; do not read `unsortedSilEdges[0]` in that case.
- The scalar SIMD processor and PS2 system adapter have virtual destructors on portable
  builds. Deleting a derived object through an abstract base with a nonvirtual destructor
  is not made safe by disabling RTTI.
- `idPolynomial` originally allocated coefficients without a destructor and used a
  shallow implicit copy constructor. Debug `idLib::Init`'s polynomial tests leaked
  eight allocations (160 requested bytes). The portable value type now has deep-copy
  construction and frees its coefficients; the smoke test verifies independent copies
  and ledger recovery after scope exit.
- `idFile_Memory::Seek(distance, FS_SEEK_END)` subtracts a positive distance from the
  end, unlike stdio's signed offset. Preserve this existing API in memory-file tests.
  The foundation's ordinary opens now return permanent streams; explicitly request
  `OpenFileReadMemory` for memory-file semantics. Metadata/stream opens do not inherit
  the fixture bulk-read size limit. Campaign bulk reads reject negative/overflow lengths
  and short reads before publishing ownership or incrementing their load ledger.
  Cvar direct commands enforce ROM/INIT flags; `set` and programmatic setters explicitly
  force writes. Test both paths instead of treating the forced setter as a restriction.

## Campaign portability findings

- `Game_local.h` includes `MenuScreen.h`, which previously pulled in `tr_local.h`
  and all its OpenGL state. Display mode declarations belong in public `RenderSystem.h`.
  The campaign target uses the full precompiled header; the reduced core header is
  not a substitute for resolving these dependencies.
- Texture/shader metadata headers now hide GL handles behind `ID_OPENGL`. The portable
  backend state and residency/shader query implementations still need to be supplied
  before a campaign link; no successful resource-load behavior is implied by the headers.
- C++20 reserves `requires`. Door/trigger members and requirement parameters use
  `requiredItem`, while the map key remains `"requires"` and save-field order is unchanged.
- Missing generated `gamesys/TypeInfo.h` is variable/state-dump introspection, separate
  from the retained `idClass`/`idTypeInfo` hierarchy. Include it only for the corresponding
  `ID_DEBUG_MEMORY` / `ID_DEBUG_UNINITIALIZED_MEMORY` features, which are not enabled
  or supplied by this port.
- `idClass::operator new`'s four-byte size prefix lost the heap's 16-byte alignment.
  Portable class allocation now returns the shared heap pointer directly and reads its
  requested size for unsized delete. `memused` counts object bytes; backing/metadata
  remains in the heap ledger. Both signed class counters are checked before allocation.
  Explicit aligned new/delete overloads preserve over-aligned derived game classes.
- Portable class factories use required/fatal allocation and run `FindUninitializedMemory`
  after construction. `SpawnEntityType` preserves normal-return spawn-argument cleanup;
  allocation/spawn errors terminate at this stage. Script errors also use explicit fatal
  cleanup; recoverable game loading remains pending. Desktop factory exception handling
  remains in its own branch.
- `TestGameAPI` now zero-initializes its imports and sets `GAME_API_VERSION` before
  `GetGameAPI` checks it. Compilation alone does not validate game initialization.

- Portable script compilation uses the initial fatal-error policy, including console
  snippets. Error scopes explicitly release parser sources, partial program state and
  owned source files before termination; they do not unwind other C++ locals or return
  a recovered program. Transactional rollback remains required before gameplay/save use.
- `idCompiler` now receives its program and native-event queries explicitly. The isolated
  EE compiler fixture uses an empty event registry; it proves compilation/storage/reset,
  not native event execution, bytecode interpretation or game initialization.
- Reject global-storage overflow before pointer/counter mutation and bytecode source
  location truncation. Recursive descent currently permits 64 guarded calls; calibrate
  this bound against campaign scripts before treating it as a campaign acceptance limit.
- EOF inside an unfinished block and constant integer remainder by zero must report
  script errors. Parser-to-lexer diagnostics pass formatted strings via `"%s"`, with
  bounded buffers, so token percent signs cannot be interpreted a second time.
- Retained menu/GUI/file/model casts use `ps2::CheckedCast` on public single-inheritance
  interfaces. Register each queried derived type with its own `PS2_TYPE_DERIVED` hook;
  inheriting a parent's alias is intentionally insufficient. Null/rejected queries remain
  null, and const removal/cross casts are compile errors. Writable zero-initialized type
  tokens have one address across translation units without a static-init guard. Desktop
  builds keep `dynamic_cast`; the existing game `idTypeInfo` system is separate.
- Cached model type checks run in release before downcasts. A failed memory-file query
  in resource packaging closes the opened file before skipping it. Static hierarchy tests
  do not establish menu/model execution or renderer acceptance.
- Portable `idCommonLocal::Error`/`FatalError` use the primitive terminating log sink,
  including before CVar initialization. They do not run desktop dialogs, `Stop`,
  renderer/session shutdown or exception recovery. Recoverable loading remains pending.
- The shipped JPEG source has four-byte RGB rows and supports only the float DCT
  configuration. Its encoder copied 256 Huffman values from shorter standard arrays;
  copy the symbol count instead. Portable SWF decoding preserves tables across tags,
  rejects source exhaustion/oversized images, and frees owned output and codec state
  before a fatal diagnostic. Host codec tests do not establish target JPEG execution.

## Staged Common and offline profiles

- The foundation instantiates real `idCommonLocal`. Portable Init/Shutdown track
  system, idlib, commands, cvars, fixture filesystem, jobs and offline session, and
  unwind only completed stages in reverse order. Filesystem initialization depends on
  registered commands/cvars. Static CVar registration cannot be rerun after shutdown;
  deliberately partial boot tests use separate processes.
- `PS2_D3BFG_FOUNDATION` selects Common method providers, never a different class
  layout. Full-header bridge units must include `idlib/precompiled.h` before
  `Common_local.h`. All portable objects omit the Classic framebuffer/material; the
  physical Classic tree remains excluded, pending its separate deletion gate.
- Portable `com_smp` defaults to zero with ROM flags. Init creates no game worker and
  `RunGameAndDraw` calls `Run` synchronously regardless of forced cvar writes. The
  foundation frame currently pumps commands and offline users only. Static game API
  import uses C linkage and validates `GAME_API_VERSION`/both exported interfaces, but
  has compile acceptance only until resident game initialization exists.
- Offline profiles use native engine stats/128 achievement bits with no save processors
  or persistence. Default profile lookup shares the active profile without resetting it.
  Re-registration resets the transient lifetime; storage queries fail,
  profile serialization returns false and requested saves become ERR while preserving
  current data. Validate stat indices and both achievement bounds; use unsigned 64-bit
  shifts so IDs 63 and 127 do not invoke signed-shift undefined behavior.
- Native dictionary string pools retain capacity until idLib shutdown. Warm them before
  comparing repeated-match heap ledgers; then require exact requested/backing/count
  stability on each reload and exact pre-init baseline recovery at full shutdown.
  The offline session must stay LOADING until explicit `LoadingFinished`, never infer
  map completion from Pump/Frame. Native game achievement manager execution is deferred.


## Portable logical sound

- `snd_local.h` must gate both the SDK includes and `bufferContext_t`'s concrete XA2
  pointer types. Portable logical sound uses `ps2/audio/sound_backend.h`; desktop
  declarations remain in their original branch. The optional video-device query
  `GetIXAudio2` returns null on portable builds without importing an XAudio type.
- Unloaded samples can retain names, reference/purge flags and last-played metadata;
  that does not establish resource loading. Do not invent zero durations or amplitudes
  to make gameplay proceed. The initial backend terminates unavailable queries with
  the method name. Meaningful timing, completion, amplitude and voice state remain
  required before a game fixture; hardware output is a later boundary.
