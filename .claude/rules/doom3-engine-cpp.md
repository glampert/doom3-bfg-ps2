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
  allocation/spawn errors terminate at this stage. Script errors/recoverable game loading
  are still pending. Desktop factory exception handling remains in its own branch.
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
