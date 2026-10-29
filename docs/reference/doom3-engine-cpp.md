# id's Doom 3 engine C++: detailed findings

Subsystem-specific portability findings, fixture traces and retail UI/SWF notes moved
verbatim from the always-loaded rule [doom3-engine-cpp.md](../../.claude/rules/doom3-engine-cpp.md),
which keeps the editing rules and the broadly applicable traps. Read the matching section
before touching that subsystem.

## Core portability findings

- Engine headers such as `d3xp/Game_local.h` depend on type/platform declarations
  from `idlib/precompiled.h`. Keep that setup include in a separate first block;
  alphabetical include sorting otherwise moves Game_local ahead of it and breaks
  the EE type-query test build. The 2026-10-16 formatting pass broke the script
  probe's `script_boot.cpp` this way until 2026-10-19; CI builds neither test
  target, so build them after a formatter pass.
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

## Retail UI format findings

- Native binary fonts have a big-endian header, little-endian ten-byte glyph records
  and a little-endian codepoint array. Sorted duplicate codepoints occur in retail
  fonts; native lookup selects the last duplicate. Preserve the old 12/24/48-point
  layout metrics independently of modern glyph extrema or any future subset.
- The bounded English resource provider validates exact font length, checksum, glyph
  count/atlas bounds and ASCII indices before publishing an immutable memory file to
  native parsing. It supplies cooked oldInfo width/height triples without loading the
  legacy blobs on the target stack. Microgramma's modern advance differs from its
  legacy values; substituting modern extrema changes layout. Sarasori has no old
  files in the inspected package and retains native modern-extrema fallback.
- RegisterFont removes literal `fonts/` before each constructor/remap. Font aliases
  use the registered name before spaces/hyphens normalize, then retry registration.
  The default-font short circuit is exact `Arial_Narrow`. Case-folding it changes
  alias behavior. Native language indirection has a two-lookup limit.
- DeclManager::MakeNameCanonical lowercases, slash-converts and removes the last
  dot suffix, but preserves repeated internal separators. Keep material identities
  separate from normalized extraction filesystem paths (`gui//x` differs from `gui/x`).
- Resource container payloads are stored directly. Their header/table integers are
  big-endian; ReadString filename lengths are little-endian. Two retail sound names
  contain `generated//sound/`; extraction merges that filesystem alias deliberately.
  Host format validation/cooking is not native retail admission; see
  docs/RETAIL_UI_PIPELINE.md and the checked host readers for bounds.

## Inactive retail SWF resources

- Bounded retail resource snapshots use `idSWF(..., nullptr, true)`. Native dictionary
  allocation/loading is retained, but the constructor skips Run/RunActions/RunTo. Reject
  any doInitActions before construction: sprite Init otherwise executes them even before
  the first frame. Inactive inspection performs storage preflight only. `execute
  shell|dialog` adds placement/AVM1 preflight and explicit `EnableResourceExecution`
  permission under a checked execution scope, allowing native timeline advancement and
  minimal widget callbacks. Full Render needs the separate draw admission below, and
  HandleEvent the native menu's event admission. Never clear `resourceOnly` to bypass missing
  drawing/resource admission. See the scoped execution rules later in this file.
- The original BSWF timestamp refers to an unstaged source SWF. Resource-only loading
  uses its preflighted immutable identity, without requiring that source timestamp.
  PS2 admits no other movie: `LoadBinary` refuses a non-resource movie. Isolated text reuses the
  native edit-text record's metrics, alignment and color via RenderResourceText.
  GetTextLength caches an int even though it returns float; source oracles must retain
  native truncation rather than compare an unrounded floating glyph sum.
- idLangDict::Clear frees values but deliberately retains keys, indices and pools so
  existing idStrIds survive language reloads. It is not full shutdown. PS2
  ReleaseResources additionally clears key storage/hash and shuts down blockAlloc;
  call it only after every borrowed string/idStrId user and native movie/font is gone.
  Target English loading checks grammar, escapes/counts and UTF-8 in release too.

## Retail containers and declaration residency

- The native filesystem mounts every `.resources` file in the base directory, but
  the port's directory listing skips symlinks: stage containers as hard links or
  copies. `fs_resourceLoadPriority` 1 serves container entries before loose files.
- `idResourceContainer::Init` reads `_ordered.resources` wholesale through
  `OpenFileReadMemory` (11.3 MB); on PS2 a tagged change opens it as a stream like
  the others. Each `idResourceCacheEntry` holds an `idStrStatic<256>` name, so the
  3,598 retail entries cost 1.08 MB of tables.
- `USE_COMPRESSED_DECLS` keeps every declaration's Huffman-compressed text resident
  from `declManager->Init` onward, parsed or not: 4.25 MB requested for 11,947 retail
  declarations. `idGameLocal::Init` registers the `def`, `fx`, `particles`, `af` and
  `newpdas` folders, so most declaration text arrives during game startup.
- Declarations with one name keep the first definition in folder listing order and
  warn for later ones. A loose authored `materials/*.mtr` beside the containers can
  silently replace a retail material (`_tracemodel`, `itemHighlightShell`); the retail
  boot package stages only the uniquely named `_headless_default`.
- `idParser` tries an include relative to the including file first, so the retail
  script compile logs failed HostFs opens such as `script/script/doom_defs.script`
  before the container lookup succeeds. They are expected.
- The retail `doom_main.script` compiles in 7.8 s in PCSX2: 95,386 statements and 3,070
  functions, within the 131,072 / 3,584 static limits. `idGameLocal::Init` also
  preallocates `idSmokeParticles` (680 KB) before any map.

## Campaign portability findings

- `gameReturn_t`'s native constructor leaves `sessionCommand` uninitialized. Native
  `idGameLocal::RunFrame` returns immediately at `NOMAP` without filling it; production
  headless frames explicitly clear it before dispatch. The native script compiler also
  includes `script/doom_defs.script` before `doom_main.script`, even for a minimal program.
- `Game_local.h` includes `MenuScreen.h`, which previously pulled in `tr_local.h`
  and all its OpenGL state. Display mode declarations belong in public `RenderSystem.h`.
  The campaign target uses the full precompiled header; the reduced core header is
  not a substitute for resolving these dependencies.
- Texture/shader metadata headers now hide GL handles behind `ID_OPENGL`. The portable
  backend now supplies the retained state/residency/shader query contracts needed
  for the campaign link. Only scoped 2D image/material/font loading is implemented;
  the header split and successful link do not enable general resource loading.
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
- Retained menu/GUI/file/model casts use `ps2::DynCast` on public single-inheritance
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

## Native SWF and font loading

The first visible movie was a placeholder authored SWF/ASCII font (removed 2026-10-29;
its BSWF and font remain host-test stand-ins). These findings carry over to retail:

- Font and BSWF parsing consume an owned read-only `idFile_Memory` snapshot of the
  checked bytes, so a second mutable HostFs open cannot replace checked counts or strings.
- PS2 SWF construction has no source-SWF inflate/cache-write fallback and does not
  preload unused cursor media. `_white` remains the owned RGBA intrinsic:
  `R_ParseImageProgram_r` otherwise rewrites it to a desktop YCoCg disk path
  (`guis/assets/white`), outside this image profile. Do not add a permissive image alias
  to hide that remap or a missing material declaration.
- Call `idSWF::Activate(true)` before querying frame-one children or binding functions to
  shortcut keys. Activation frees/rebuilds the display list and runs its initialization
  actions; child pointers obtained beforehand can become invalid.
- Native `idSWF::Render` uses its caller's time on PS2. A Common frame advances the
  movie clock; a redraw uses the previous value and cannot tick the timeline again.
  Native `Pause`/`Resume` retain their timeline behavior.
- The English retail profile admits eight canonical font families at most, which bounds
  the font cache; its selected package has five. Literal `fonts/` stripping and catalog
  aliases precede canonical cache lookup. General localization/fallback remains
  unavailable. Native glyph records are 10 bytes with a zero byte before the
  little-endian 16-bit atlas coordinates. Validate the complete font before native
  allocation and the material's loaded first image before native glyph scaling
  dereferences it.
- Native `idFont` separately allocates `glyphData` and `charIndex`; deleting only its
  `fontInfo` leaks both. The PS2 destructor frees those arrays. Movie dictionaries
  borrow cached fonts, and fonts borrow material/image declarations: destroy the movie,
  then the font cache, then UI images/geometry and the GS device, before declarations
  and idlib disappear. A movie reload may retain the warm font/image cache; full
  Common shutdown must recover the pre-boot ledger.

## Staged Common and offline profiles

- The foundation instantiates real `idCommonLocal`. Portable Init/Shutdown track
  system, idlib, commands, cvars, fixture filesystem, jobs and offline session, and
  unwind only completed stages in reverse order. Filesystem initialization depends on
  registered commands/cvars. Static CVar registration cannot be rerun after shutdown;
  deliberately partial boot tests use separate processes.
- `PS2_D3BFG_FOUNDATION` selects Common method providers, never a different class
  layout. Full-header bridge units must include `idlib/precompiled.h` before
  `Common_local.h` or `UsercmdGen.h`. Use a separate commented include group so
  clang-format cannot move the dependent native header ahead of the precompiled header.
  All portable objects omit the Classic framebuffer/material; the physical Classic tree remains excluded, pending its separate deletion gate.
- Portable `com_smp` defaults to zero with ROM flags. Init creates no game worker and
  `RunGameAndDraw` calls `Run` synchronously regardless of forced cvar writes. The
  foundation test frame pumps commands and offline users only. Static game API
  import uses C linkage and validates `GAME_API_VERSION`/both exported interfaces, but
  is exercised by native `idGameLocal::Init` in the resident logic fixture and production
  headless console. Production adds native declarations, console/key/event services,
  CPU worlds and game startup after the foundation. Tests keep separate startup ownership.
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
- The backend classes are `ps2::audio::SoundSample`, `SoundVoice` and `SoundHardware`.
  The engine keeps its names as aliases: `sound.h` aliases `idSoundSample` (its plain
  `class idSoundSample;` forward declaration would conflict, so that line is tagged) and
  the backend header aliases `idSoundVoice` and `idSoundHardware`. Engine sources stay unchanged.
- Unloaded samples can retain names, reference/purge flags and last-played metadata;
  that does not establish resource loading. Do not invent zero durations or amplitudes
  to make gameplay proceed. Unloaded queries terminate with the method name. The
  M3 prerequisites supply actual PCM fixture data/timing/amplitude and explicitly
  selected headless voices. Native sound-world/channel integration and hardware output
  remain later work; direct adapter tests do not establish native game sound behavior.
- `snd_local.h::SamplesToMsec` divides the rate by 100 before converting: 11,025 frames
  at 11,025 Hz yield 1,002 ms. The backend derives duration from 64-bit `frames * 1000 / rate`
  instead. `NumSamples` is frames per channel, not interleaved scalar sample count.
  Native emitter looping takes modulo sample duration, so fixture WAVs shorter than
  1 ms must be rejected rather than exposing a zero divisor. Peak amplitude scans each
  60 Hz PCM window over all channels and handles -32768 without signed overflow.
- The fixture loader uses `OpenFileRead` and bounded header/payload reads, not whole-file
  `ReadFile`. Keep stream destruction outside the fatal call path and free any acquired
  payload before reporting a read/allocation failure: fatal logging does not unwind C++
  objects. No `.idwav`, ADPCM decoder or retail sample-cache budget is established yet.
- `InitHeadless` requires an explicit monotonic microsecond clock. Unconfigured native
  hardware `Init` still fails rather than selecting silence implicitly. Shutdown releases
  the pool synchronously without querying the clock; the selected clock persists for
  a later `Init`, which resets its monotonic epoch. Its context must remain valid while
  voices are used. No asynchronous DMA/zombie interval exists in this logical mode.
- Voice phase is bounded within its active segment in Q16 microseconds, with Q16 pitch
  in [0, 8]. Reduce large clock deltas before multiplying to avoid 64-bit overflow.
  Segment durations derive from frames/rate, not truncated integer milliseconds. Seek
  subtracts the lead-in before loop modulo; desktop `RestartAt` does not do this.
  Derived `SetPitch` charges the old rate up to the change instant; native callers use
  `idSoundVoice*` (the backend `SoundVoice`), avoiding the base class's inline
  metadata-only setter.
- Allocated voices retain their lead-in and loop samples, including while idle, stopped
  or complete, until `FreeVoice` or hardware shutdown. Purge/reload/rename/destruction
  rejects pinned samples. Native `StopVoicesWithSample` must free voices before sample
  mutation; completion alone does not release the slot or prevent a later restart.
  `GetAmplitude` supplies the active sample's pre-gain peak, zero while paused/stopped/
  complete and one for active `SSF_NO_FLICKER`; native callers apply gain separately.
  Surround output remains unavailable and native `SoundVoice.cpp`/`s_subFraction` are
  not imported by this slice.


## Resident link findings

- No JPEG or zlib is linked (removed 2026-10-29, saving 158,592 bytes of release
  `PT_LOAD`). PS2 loads only cooked BSWF movies and converted images, so the source-SWF
  JPEG tags, `idSWF::Inflate` and `LoadJPG` fail by name, and `idDecompressJPEG` holds no
  decoder state. `Zip.cpp`, `Unzip.cpp` and `idFile_InZip` are excluded (nothing opens
  ZIP files), and `ps2/system/save_metadata.cpp` stubs the `idFile_SaveGamePipelined`
  members campaign code names; `File_SaveGame.cpp` and its `sgf_*` cvars are not built.
- id's project files still name the deleted `renderer/jpeg-6` and `framework/zlib`
  units; the inventory's `removed_upstream_trees` counts them without resolving them.
- Compressed resource packs will import miniz. Its zlib-compatible API (`z_stream`,
  `deflateInit2`/`inflateInit2` with raw `-15` window bits, `Z_SYNC_FLUSH`) covers what
  `File_SaveGame.cpp` and `SWF_Zlib.cpp` call, so both can return close to unchanged;
  check the allocator callback types against miniz's `size_t` signatures.
- The removed JPEG 6 was engine-modified (four-byte RGB rows, float DCT only, a C++
  ABI); a stock libjpeg is not a drop-in replacement for id's desktop decoder code.

- Campaign code embeds `idAchievementManager`; it is not solely an online service.
  Retain native `d3xp/Achievements.cpp` and gate only Classic header/evaluation on
  portable builds. Compilation does not establish player achievement execution.
- Whole-object campaign linking exposes excluded shell/multiplayer/save, renderer-demo
  and source-import calls even before game initialization. Keep the source manifest
  intact and report requestors; do not use GC or generic linker-symbol shims to hide them.
- Common's offline query providers are shared between foundation and campaign. Its
  private reset must clear native queued snapshots, interpolation values and usercmd
  storage despite having no network peers. Populated-state runtime acceptance remains
  an M3 check. Inactive demo cleanup can return safely; active demos fail explicitly.
- `renderSystem` and direct retained frontend callers must share native `tr` identity.
  Bind the full `idRenderSystemLocal` vtable and native image/shader/cache globals with
  typed definitions; no generic symbol shims or desktop arenas. Inactive state is a
  valid query, while live dimensions, allocation and submission fail until implemented.
  Preserve referenced frontend cvar defaults/flags/bounds. Unloaded image metadata and
  empty buffer cleanup must not claim a loaded resource or mutate the heap ledger.

## Initial native game fixture

- Explicitly select `ps2::tests::fixture::Enable` before Common startup. Native filesystem
  Init then selects only `host:game-fixture`, with matching `fs_game`/search gamedir.
  This preserves native listing/permanent streams and correct relative script paths,
  without desktop default.cfg or retail container initialization. It is separate from
  the core fixture filesystem and from ordinary campaign startup.
- Native game Init needs an `aas_types` declaration even when no AAS instances are
  requested. `SCRIPT_DEFAULT` implicitly includes `SCRIPT_DEFAULTDEFS`; authored
  fixtures must provide both `script/doom_main.script` and `script/doom_defs.script`.
  Unlike the isolated compiler probe, game initialization installs the real event and
  class registries (535 event definitions and 160 classes, including the logic and
  player-physics probes).
- `InitHeadlessFixture` accepts only the bounded worldspawn map with five fixed axial
  collision brushes, validates every plane/material before collision conversion,
  rejects resource/physics keys and runs native `InitScriptForMap`/`SpawnMapEntities`.
  It does not call regular `InitFromNewMap`, whose AAS/PVS/player/resource setup
  remains pending. Only explicit fixture mode permits native `RunFrame` with a null
  render world and initially no clients, suppressing player synchronization/debug drawing while
  retaining native clocks, entity Think, interpreter and event service loops.
- Worldspawn's thread `DelayedStart(0)` at game time zero schedules execution for at
  least 1 ms, so map `main` starts on the first native frame. A script that increments
  before `sys.waitFrame` advances once per frame; placing the increment after the wait
  shifts its first effect to the second frame. Native wait events resume on later
  game frames, not wall-clock sleeps.
- Native `MapShutdown` cancels pending script/event work and resets map definitions.
  Three logic-map reloads recover the same warm ledger; full game/decl/Common shutdown
  recovers the pre-boot baseline. That initial slice leaves sound/render worlds,
  AAS/PVS and players uninitialized. Extend headless simulation before sound/render integration.
- `$name` script references bind when native `idEntity::SetName` calls `program.SetEntity`.
  If the reference is compiled after the entity was named, it stays null until rebound.
  In this fixture, worldspawn compiles the map script after its own base Spawn named it;
  the logic probe and native command target spawn afterward and bind their references.
  Use the probe as the `activate` argument: native `EV_Activate` requires a non-null
  entity (`e`), and the interpreter terminates the thread if that argument is missing.
- Native `idTarget_SessionCommand::Event_Activate` copies the `command` spawn key into
  `gameLocal.sessionCommand`. `BuildReturnValue` copies it into the frame return and
  clears the pending string in the same frame. The fixture requires returns on frame
  four (script activation) and six (queued activation), then checks target/map-binding
  removal and reload accounting. It does not dispatch that token through Common or
  load a new campaign map.
- Native `PostEventMS` schedules relative to the entity's time group. In the fixture,
  both clocks advance at 60 Hz: a 90 ms deadline is serviced on frame six at 100 ms,
  never frame five at 83 ms. `ServiceEvents` dispatches events whose deadline is at
  or before the current game time.
- Script `remove()` dispatches `EV_SafeRemove`, which queues `EV_Remove` at zero delay.
  The service loop removes the current event before invoking it, allowing deletion
  without double-free; `idClass` destruction cancels its remaining queued events.
  Frame-seven removal (116 ms) invalidates the cached generation-checked `idEntityPtr`
  and cancels the target's 120 ms activation, proven by the empty frame-eight return.
  Native entity teardown removes the name hash and invalidates the spawn slot. The
  script `$name` definition is a numeric entity slot, not an `idEntityPtr`; map program
  reset removes that definition. Do not infer generation safety for script references
  from the cached native handle check.

## Native collision fixture

- Native `idClip::Init` loads the model named `worldMap`. Collision conversion takes
  an entity's `model` or `name` before falling back to that name; the authored
  worldspawn uses `worldMap` to avoid an unintended model-loading fallback.
  `_tracemodel` must be explicitly declared. Both it and the fixed brush material
  use a stage-free `solid` definition, preserving collision bits without images.
- Native point `Contents` takes the fast point path and returns unmasked brush bits.
  A nonempty trace-model volume applies `contentMask`. Use volume queries to test
  contents filtering; retain the upstream distinction instead of assuming identical
  point/volume semantics.
- More than four brushes triggers native axial BSP construction. `R_FilterBrushIntoTree`
  keeps brushes spanning all descendants on ancestor nodes; upstream `PointContents`
  checked only the leaf, missing the floor after the step fixture added its fifth
  brush. The tagged fix checks leaf-to-root brush lists and preserves native bounds,
  plane tests and unmasked return semantics. Floor inside/outside and volume-mask
  checks run on conversion, text-cache and binary-cache reloads.
- Three reloads exercise brush conversion and `.cm` writing, text `.cm` parsing and
  generated `.bcm` writing, then binary loading. The binary loader used to retain
  serialized visitation/sidedness from another trace epoch: the first point trace
  on the third load missed the floor. Portable loading now resets vertex/edge,
  polygon and brush runtime query state. Allocation helpers also increment already
  restored counters; preserve the serialized counts/byte totals for correct statistics.
- Missing `.proc` is a native collision-optimization warning; brush conversion still
  succeeds without render-world topology. Portable Common shutdown must clear native
  warning/error lists before idlib teardown. The first fixture run exposed 608 requested
  bytes retained by its warning list/string; shutdown now recovers the exact baseline
  while preserving the diagnostic.
- Native `CM_CLIP_EPSILON` is 0.25 units. Downward point/2-unit box traces stop at
  z=0.25/2.25. The 2-unit physics probe runs `idEntity::RunPhysics` with native
  `idPhysics_Monster`, zero gravity and fly movement. It reaches x=8.192/16.896 on
  frames one/two and stops around x=21.741 before the wall at x=24. The small offset
  below 21.75 comes from native overclip; this establishes neither grounded movement
  nor AI/player simulation.
- The additional falling probe uses native monster gravity of 512 units/s², starting
  at z=4 with zero velocity. Airborne evaluation moves with the old velocity before
  adding gravity. It lands around z=2.251 on frame six; `CheckGround` runs before
  movement, so ground/rest begin on frame seven. `Rest` clears velocity and TH_PHYSICS,
  while the fixture's TH_THINK remains active. Frames seven/eight require native
  world floor identity, an upward solid contact and zero velocity.
- Grounded diagonal sliding uses velocity movement, zero maximum step height and
  velocity (512,64,0). Ground contacts persist, the world blocks from frame three,
  and `MM_SLIDING` preserves tangent velocity 64 while overclip leaves x velocity
  about -0.512. Native `SlideMove` projects the full delta after a partial collision,
  adding about 0.607 extra y units at the first wall contact; the fixture bounds this
  behavior rather than assuming displacement equals tangent speed times total time.
  This covers flat-floor sliding, not stairs, slopes or player/AI simulation.

## Native player-physics fixture

- A native fixture entity can own `idPhysics_Player` and call `SetPlayerInput` then
  `idEntity::RunPhysics` without spawning an `idPlayer`. Enable TH_PHYSICS explicitly:
  player physics inherits the empty base `Activate`, unlike monster physics. The
  fixture retains zero clients and null sound/render worlds, so normal player frame
  synchronization and presentation remain outside this acceptance.
- `idUserCmdMgr::GetUserCmdForPlayer` increments its read cursor only when an unread
  command exists, otherwise returning the previous command. Before any command was
  written its index is -1; guard `HasUserCmdForPlayer` before reading. The fixture
  queues one timestamped command per native frame, verifies exact cursors/consumption,
  then calls `ResetPlayer` between maps. This is explicit injection, not input sampling.
- The PM_NORMAL fixture uses speed 128, gravity 512, zero step height, a 4-unit box
  footprint and the native standing height. Forward input accelerates x velocity to
  20.48/32.04/43.60 on the first three frames. Frame four hits the wall around x=21.749,
  with overclip leaving about -0.054 x velocity. Reverse input gives -21.76 on frame
  five; neutral input then slows to -11.56/-1.96/0 through native ground friction.
  An upward solid world contact persists at z=0.25. Those initial movement checks
  do not establish stairs, full player entity or controller-input acceptance.
- The extended fixture runs 88 native frames per reload. `CheckJump` launches at
  sqrt(2 * 16 * 512) = 128 units/s; player `SlideMove` integrates the average old/new
  vertical velocity, unlike the monster probe's old-velocity integration. The apex is
  16 units above the floor and landing takes 500 ms. Compare the fixture's airborne
  deadline in integer milliseconds before converting to float; the boundary must not
  depend on rounding `elapsedMs * 0.001f` around 0.5.
- `PMF_JUMP_HELD` prevents another takeoff until a command releases jump, including
  after landing. `CheckDuck` runs before `WalkMove`/`CheckJump`; simultaneous crouch
  and jump therefore shrink the clip shape and reject takeoff. Native default heights
  are 74 standing / 38 crouched. Releasing crouch traces available headroom before
  restoring standing height. The fixture proves only the unobstructed restoration
  case and leaves movement cvars unchanged. Its script stops after eight increments;
  later frames verify that completed state, rather than inventing further script work.

## Bounded native player fixture

- `idPlayer` construction itself acquires HUD/PDA handlers and `idPlayerView` materials/
  fullscreen FX. The explicit process-wide fixture selection must precede factory
  construction, with those acquisitions gated only in that mode. Native campaign
  construction remains unchanged; a null render world alone is not fixture selection.
- Validate exact player type, all seven fixture keys, empty slot zero and zero clients
  before `SpawnEntityType` constructs/registers a player. Native entity registration
  trusts `spawn_entnum`; checking after base Spawn cannot protect an existing slot.
  The authored API uses `SpawnEntityType`, not general entityDef/map player loading.
- Native `CallSpawn` runs entity and actor setup before player Spawn. Actor setup
  allocates script storage and manual actor/animation threads, but queues its script
  constructor without executing it. The bounded player setup links all native AI
  variables, executes the constructor once, sets an authored `FixtureIdle` state and
  installs native player physics/empty inventory. Regular `Init`/`SpawnToPoint`,
  persistent inventory, weapons, model joints, HUD and achievements remain deferred.
- With a native `idPlayer` in slot zero and `numClients=1`, `RunEntityThink` selects
  `RunAllUserCmdsForPlayer` → `RunSingleUserCmd` → `HandleUserCmds` → player Think.
  The fixture retains that delivery and native speed adjustment, `Move`, condition
  updates and actor interpreter state. Neutral/full-forward, jump and crouch commands
  are accepted; other buttons, strafing, view-angle changes and impulses fail before simulation. AAS remains
  empty, and fixture-only `SetupPlayerPVS` acquires no render-world topology.
- `BecomeInactive(TH_PHYSICS)` reactivates `TH_UPDATEVISUALS`. Custom headless probes
  must respect TH_THINK and clear the pending visual flag separately, since they have
  no `Present` call to do so. Native active-list removal occurs at the end of the next
  frame. The initial extended posture probe did not fully deactivate the monster
  probes; the native-player slice fixes their continued Think/out-of-bounds warnings.
- The native-player checks run frames 89–99 after the earlier physics probe stops and
  its command cursors reset. Native walk speed 140 with ground acceleration/friction
  reaches 63.8 units/s after four commands, then stops at x=-26.192 on frame 99.
  Script globals reset per map: one constructor and eleven state increments. Three
  reloads invalidate the player handle and recover exact warm/full ledgers. This
  establishes restricted player simulation, not full campaign player behavior.
- Frames 100–195 add two jumps using native gravity 1066 and jump height 48, then
  crouch walking at speed 80 and native eye-height interpolation (rate 0.87, targets
  68/32). Native contacts mark the floor at z≈0.310/0.370 on frames 135/177 while
  velocity is still downward; the next WalkMove overclips it to about +0.289 before
  settling. `CrashLand` uses old vertical velocity, so `AI_SOFTLANDING` is true for
  two ticks per landing. Count grounded transitions separately from soft-flag ticks.
- Actor `setState` ends current script processing; native `UpdateScript` switches to
  the requested function within the same player tick. Authored Idle→Air→Idle→Air→
  Idle→Crouch→Idle states therefore increment the tick counter once per frame across
  seven state entries, two jumps and two landings. Query the native `getState` event
  as well as authored counters. A missing function must fail at `Event_SetState`;
  missing AI storage must fail at `LinkScriptVariables`, so that negative script
  deliberately avoids referencing the omitted field during compilation.
- The fixed roof box (-24,24,44)→(-16,56,52) is in a separate lane. At y=40 a
  standing 32×32×74 shape stops near x=-40.25, while height 38 passes beneath it.
  `CheckDuck` runs before movement: the upward crouched-shape sweep is blocked at
  fraction (44−0.25−38−0.25)/36≈0.15278 while the shape overlaps the roof. Releasing
  crouch keeps physics/AI/script crouch state and speed 80 until headroom clears.
  The post-move trace clears on frame 264; standing restores on frame 265. Eye
  interpolation follows physics posture rather than the crouch button alone.
- Native `UpdateConditions` clears `AI_FORWARD` when `xyspeed <= MIN_BOB_SPEED` (5),
  even with forward held against a collision. Headless checks must distinguish input
  from linked movement conditions. Completed probes still own linked collision bodies
  after Think stops; disable those bodies explicitly when isolating the ceiling lane.
- The step lane uses a 12-unit platform (-24,-56,0)→(0,-24,12) followed by a high
  box (0,-56,0)→(8,-24,36): the second rise is 24, above native `pm_stepsize=16`.
  `HasSteppedUp` includes both PMF_STEPPED_UP and PMF_STEPPED_DOWN. Check signed
  `GetStepUp` as well as the flag to distinguish climbing from stepping down.
  This fixture climbs at frame 283 and blocks near x=-16.25 on 293 while grounded;
  no jump or crouch is needed, and `PM_STEPSCALE=1` preserves horizontal velocity.

## Scoped retail execution

- Retail origin and permission to execute are separate: a resource-only `idSWF` still
  rejects HandleEvent until event admission, and Render until draw admission.
  Construction stays inactive; real widget callbacks must be installed before explicit
  activation. Advance once per Common frame before GS submission; synchronous redraws
  preserve movie time.
- Full drawing is a third admission, `EnableResourceRender`, after execution. The target
  preflight refuses only additive alpha terms now (outlines, screen blends, RGB color
  terms and masks have draw paths; see the GS notes). Native `Render` then only draws a
  retail movie, without running frames, inside a draw scope plus an execution scope that
  bounds any reachable script work. Both the dialog and the shell qualify.
- Key events are a fourth admission, `EnableResourceEvents`, after drawing; only the
  native menu enables it. `HandleEvent` then takes `SE_KEY` keyboard and pad keys inside
  the movie's execution scope and refuses pointer, text and axis events and any
  `useFunction`, `waitInput` or `focusWindow` global. Shortcut keys and the objects they
  name are script data, so a native handler a key reaches must be a `WrapWidgetSWFEvent`
  (checked query on the `idSWFScriptFunction` root) whose target is one of the menu
  handler's live widgets, compared as pointers before anything dereferences it; script
  handlers run under the scope's budgets. Native `idMenuHandler::GetChildFromIndex`
  accepts `Num()` as an index, so the walk uses a tagged child count.
- Retail `guis.mtr` defines `guiSolid` as blend/colored/map `_white`/clamp, identical
  to the implicit `_white` definition. GUI surfaces evaluate `colored` with all-one
  shader parameters (`GuiModel.cpp`) and force vertex-color modulation
  (`tr_backend_draw.cpp`), so both reduce to one constant vertex-colored stage over the
  owned white image. The retail material hook supplies that text; no `_white` substitution.
- Outside the native menu, Common's dialog manager has no dialog movie: `common->Dialog()`
  returns it so queries and clears of its empty queue work, and adding a dialog fails
  explicitly instead of the native silent return, which would drop its callbacks and the
  player's choice. The `dialog quit` test owner reproduces `idCommonDialog::Init`
  constants and `ShowDialog` globals for GDM_QUIT_GAME / DIALOG_DYNAMIC. `GetDialogMsg`
  reads no manager state, so a zero-initialized local manager supplies the native
  title/text keys; that owner's choices fail explicitly. The movie positions itself
  through `calcNumLines`, `_y` writes and text `_stroke*` properties.
- The shell's first frame needs native `refreshCarousel` and `updateCredits` owners.
  Use their actual widgets, retain callback identity and detach the borrowed movie before
  the base menu handler cleans up. The port's minimal carousel selects Doom 3 only. The
  native shell handler (below) now provides these owners itself; the minimal one remains
  only for the timeline and full-draw test modes.
- Script lookup names are mutable. Native service authorization must compare the
  resolved function object and owner, not only its name/signature. Native widget updates
  can seek too; reject callbacks/seeks that could destroy an executing descendant.
  Check every active script source and receiver, not just the innermost caller: a
  descendant can call a function on root while its outer action is still running.
- Static command/action validation does not bound dynamic work. Keep release checks for
  instruction/call/stack/allocation work, and cap native argument lists at
  `SWF_MAX_PARMS` (16). This selected profile is not general SWF execution support;
  copied text aliases and raw default-sprite references retain upstream lifetime limits.

## Native shell menu (reviewed subset)

- `idMenuHandler_Shell` and the press-start, root, campaign (`Singleplayer`) and
  settings screens are campaign units now; the other shell screens stay deferred. On PS2
  the handler's `Initialize` binds the start, root, campaign, settings and credits
  screens only, and skips the intro movie build touches, intro and Mars cinematics and
  menu sounds (empty sound names make `PlaySound` return early). The offline session has
  no save game manager (`GetSaveGameManager` fails by name), so the campaign screen's
  tags report no save to continue instead of enumerating, and `ContinueGame` fails by
  name; New Game and Load Game move into screens that are still unbound.
  The in-game menu fails explicitly. Handler paths that reach unlinked screens (lobbies,
  leaderboards, continue, key bindings, save enumeration) are compiled out or fail by
  method name. `shell_stub.cpp` keeps only the load and save screens' enumeration calls,
  which the retained save-information widget references.
- `idMenuHandler::Initialize` opens the movie through `ps2::ui::CreateMenuMovie`,
  which admits only the shell, and `ActivateMenu` starts it through `ActivateMenuMovie`
  once the handler installed `updateMenuDisplay` and `activateMenus`: the backend pins
  those, `closeMenu`, `refreshCarousel` and `updateCredits` by identity, then enables
  execution and drawing. `idMenuHandler::Update` skips `Render` on PS2: the presentation
  updates the handler, then advances the timeline, then draws in the GS frame.
- `ps2::ui::OpenMenu` follows `idCommonLocal::CreateMainMenu`, after native
  `PerformGameSwitch`'s move to press-start (the offline session signs in user 0 at
  startup, as the PC session does). A package whose policy says `menu` opens it from
  `InitPresentation`, at the end of `InitHeadless` and before the first frame executes
  `d3bfg.cfg`, as `Init` creates and starts the menu before `AddStartupCommands`; the
  menu closes the console, as `StartMenu` does, and runs on real time. Every frame mirrors `GuiFrameEvents`: sync with the
  session, then the handler's update. Signing in moves the session to IDLE and the next
  sync shows the root screen.
- Handlers and movies report platform 1, the PS3 face-button layout and prompt art (the
  prompt image sprites carry Xbox, PlayStation and PC frames). The root menu omits the
  development map list and multiplayer; the start screen's carousel holds Doom 3 alone,
  and pressing it signs in or opens the root screen.
- Common's PS2 `ProcessEvent` gives the console, then the menu each event; `ProcessMenuEvent` mirrors `MenuEvent` (the sign-in manager, then
  `Shell_HandleGuiEvent`) inside the shell's execution scope. The command bar binds
  ENTER/JOY1 to accept and ESCAPE/BACKSPACE/JOY2 to back; `UpdateCmds` binds the arrows
  and the native `shortcutKeys.clear` table aliases the D-pad to them. A released command
  bar button has no `onRelease`, so accept and back handle only their press; scroll
  buttons handle both halves. The root's back moves the session to press-start, which
  signs the user out; the next accept signs in again. `idMenuHandler_Shell::Update` fails
  `shell capability unavailable: screen N` for a transition into an unbound screen
  (Campaign 3, Settings 13) instead of leaving the menu empty.
- The menu initializes Common's dialog manager after the shell, as `idCommonLocal::Init`
  does, and shuts it down first. Retail admission is per movie, so the shell and the
  dialog are resident together; each preparation admits one construction. `Init` opens
  the dialog through `CreateDialogMovie`, with execution, drawing and events admitted
  but inactive (`EnableResourceExecution( false )`); showing a dialog activates it. No
  save indicator movie is opened: nothing in BFG activates it. The manager's `Render`
  keeps its state half (drop cleared dialogs, show the next) before the GS frame and
  returns before drawing; the presentation advances the dialog's timeline and draws it
  over the shell. Resource movies activate on the presentation's movie clock
  (`MovieClock`), so a dialog shown mid-run follows the fixed test step.
- An active dialog takes every event before the menu, inside its own execution scope.
  `ShowDialog` pins the choices it installs; the movie copies them into button
  properties (`func0`, `func1`) and calls them as methods without arguments, so the
  pinned object, not the name, authorizes the call. The root's Quit dialog is
  DIALOG_DYNAMIC: its three options take focus left to right and accept selects the
  focused one; neither Escape nor circle cancels it. Accept quits through Common at the
  frame's end. Its `<JOY1>` prompt is a button tag: native `FindTooltipIcons` replaces
  tags with controller art from `tooltipButtonImage` while drawing.
- On PS2 only retail movies get that table, with the PS3 art (platform 1), and the lookup
  takes `psImage`. A material found while drawing would load
  its image inside the GS frame, which the paletted loader refuses, so
  `EnableResourceRender` loads the eight prompt materials first (native PC preloads them in
  the constructor). The profile's `material_names` admit them; no pointer art is loaded.
- Native widget callbacks, carousel and credits refreshes included, run under the same
  execution scope and seek checks as timeline scripts. The carousel's buttons ask for a
  `sel_up` frame that the retail movie's carousel items do not have, so native code
  warns on each load and focus change; the warning comes from the original data, not the port.

## Native edit-text drawing

- `RenderEditText` scales glyphs by `glyphScale` = post-transform font height / 48:
  screen units per texel of the 48-point atlas. `GetScaledGlyph` grows the UVs by half a
  texel per side, but the quad grows by a fixed 1.0 unit (`glyph.width + 1.0f`), so at
  the PS2 dialog's 0.22–0.34 units per texel a small glyph is stretched noticeably.
- A stroke is four black copies at (±1, ±1) × `strokeWeight` × `glyphScale`, with alpha
  `strokeStrength` × the text alpha, drawn before the glyph. Layout adds
  `swf_textStrokeSizeGlyphSpacer` (1.5) × weight × `glyphScale` to each advance.
  `swf_textStrokeSize` defaults to 1.65; the dialog's scripts set 1.75 for the body and
  options and 2.0 for the title through `_stroke*` properties. A drop shadow is one black
  copy offset by 2 × `glyphScale`.
- The PS2 tag in `RenderEditText` hands a glyph to `ps2::ui::fonts::FindPrerenderedGlyph`
  when the text has no drop shadow and its matrix neither rotates nor shears. On a match,
  `DrawPrerenderedGlyph` draws the baked outline in the stroke color and then the glyph,
  replacing the four stroke copies and the quad. The pen, advances (stroke spacer
  included), alignment, selection highlight and cursor stay native.

## Native BSWF atlas records

BSWF16 image records interleave **width, atlas X offset, height, atlas Y offset**.
The writer/loader loops over axes; four consecutive integers are not a size pair
followed by an offset pair. The host SWF fixture's seam probe uses `(16,246,16,64)` for a
16×16 bitmap at (246,64). A wrong tuple can still render plausible pixels because
native size factors cancel, while never crossing the intended page boundary.
Check semantic dimensions/offsets in host readers and require actual page-load
counters plus seam pixels on EE; a visually familiar letter alone missed this error.
