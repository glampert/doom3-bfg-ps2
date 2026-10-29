# id's Doom 3 engine C++

The engine source is under `src/neo/`. The initial EE core uses the generic scalar
math path; PS2-specific math replacement headers are a later milestone. Detailed,
subsystem-specific findings live in
[docs/reference/doom3-engine-cpp.md](../../docs/reference/doom3-engine-cpp.md); this
file keeps only what most engine edits need.

## Editing rules

- Keep id's code as close to unchanged as possible. Platform work belongs in `ps2`
  behind a seam. When an engine change is unavoidable, keep it minimal and tag it:
  `// [PS2_D3BFG]: <why>`.
- id's code builds without warnings in every configuration. `LEGACY_SHARED_WARNFLAGS`
  turns off only reviewed categories (listed by reason in the Makefile, engine objects
  only). Treat any new engine warning as a possible defect: fix it in the source, and
  extend the list only for a category reviewed as style or a false positive.
- **No double FPU on the EE.** Unsuffixed constants in the legacy target are made float by
  `-fsingle-precision-constant`. Explicit upstream double arithmetic remains software
  floating point; its current units are listed in the
  [developer guide](../../docs/DEVELOPMENT.md#foundation-test-boundary). Don't introduce new double
  math in hot paths, or change precision without a target regression test.

## Portability traps

- Include `idlib/precompiled.h` in its own first block before `Game_local.h`,
  `Common_local.h`, `UsercmdGen.h` and similar headers, with a comment so clang-format
  cannot reorder them. A formatter pass broke the script probe this way; CI builds neither
  test target, so build them after formatting.
- C++20 reserves `requires`: members and parameters use `requiredItem`; the map key stays
  `"requires"` and save-field order is unchanged.
- Casts of menu/GUI/file/model interfaces use `ps2::DynCast`; register each queried derived
  type with its own `PS2_TYPE_DERIVED` hook (a parent's alias is not enough).
- `idClass::operator new` returns the shared heap pointer directly (16-byte alignment kept,
  no size prefix); over-aligned classes use the explicit aligned overloads.
- Fatal-error policy: `Error`/`FatalError`, script errors and allocation failures
  terminate via the primitive log sink; no desktop dialogs, no unwinding of C++ locals,
  no recoverable loading yet. Release owned resources before the fatal call.
- Common is staged and one-shot: static CVar registration cannot be rerun after shutdown,
  so partial-boot tests use fresh processes. `PS2_D3BFG_FOUNDATION` selects method
  providers, never a different class layout.
- The offline session stays LOADING until an explicit `LoadingFinished`; never infer map
  completion from Pump/Frame.
- Failed plane/ray intersections leave the scale output untouched: check the return value.
  `FS_WriteFloatString` takes no `l` modifier (parser integers are `long`: `%ld`, `labs`).
- Abstract bases with virtual destructors only; disabling RTTI does not make deleting
  through a nonvirtual base safe.

## Retail SWF admission model

- Retail movies construct resource-only: `idSWF(..., nullptr, true)` loads the dictionary
  but skips Run/RunActions/RunTo, and `LoadBinary` refuses any other movie. Never clear
  `resourceOnly` to bypass a missing admission.
- Admissions are separate and cumulative: `EnableResourceExecution` (placement/AVM1
  preflight, native timeline advance and minimal widget callbacks under a checked scope),
  `EnableResourceRender` (native `Render` draws without running frames, inside draw plus
  execution scopes), `EnableResourceEvents` (`SE_KEY` keyboard/pad only; pointer, text,
  axis, `useFunction`, `waitInput`, `focusWindow` refused; only the native menu enables it).
- Assertion builds authorize callbacks and seeks by identity: compare the resolved function
  object and owner, not a mutable name. A handler a key reaches must be a `WrapWidgetSWFEvent`
  whose target is one of the menu handler's live widgets. Reject callbacks/seeks that could
  destroy an executing descendant; check every active script source and receiver.
- Static validation does not bound dynamic work: assertion builds keep instruction, call,
  stack and allocation budgets and cap argument lists at `SWF_MAX_PARMS` (16). Release builds
  rely on the load-time preflight and debug runs of the same retail scripts, and accept UB.
- Install real widget callbacks before activation; call `Activate(true)` before querying
  frame-one children. Advance once per Common frame before GS submission; redraws reuse
  the previous movie time.
- Teardown order: movies, then the font cache, then UI images/geometry, then the GS device,
  before declarations and idlib. Fonts borrow materials/images and movies borrow fonts;
  a movie reload may keep the warm font/image cache, full shutdown must recover the ledger.
- `idLangDict::Clear` retains keys; PS2 `ReleaseResources` frees them only after every
  borrowed string/`idStrId` user, movie and font is gone.

## Reference index

[docs/reference/doom3-engine-cpp.md](../../docs/reference/doom3-engine-cpp.md) sections:
Core portability findings (cvars, jobs, polynomial, memory files); Retail UI format findings
(fonts, DeclManager names, containers); Inactive retail SWF resources; Retail containers and
declaration residency; Campaign portability findings (headers, class factories, script
compiler, casts); Native SWF and font loading; Staged Common and offline profiles;
Portable logical sound; Resident link findings (removed JPEG/zlib, renderSystem binding); Initial
native game fixture; Native collision fixture; Native player-physics fixture; Bounded native
player fixture; Scoped retail execution; Native shell menu; Native edit-text drawing; Native
BSWF atlas records.
