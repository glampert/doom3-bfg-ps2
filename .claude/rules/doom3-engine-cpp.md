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
  `Common_local.h` or `UsercmdGen.h`. Use a separate commented include group so
  clang-format cannot move the dependent native header ahead of the precompiled header.
  All portable objects omit the Classic framebuffer/material; the physical Classic tree remains excluded, pending its separate deletion gate.
- Portable `com_smp` defaults to zero with ROM flags. Init creates no game worker and
  `RunGameAndDraw` calls `Run` synchronously regardless of forced cvar writes. The
  foundation frame currently pumps commands and offline users only. Static game API
  import uses C linkage and validates `GAME_API_VERSION`/both exported interfaces, but
  is exercised by native `idGameLocal::Init` in the resident logic fixture. The
  foundation still does not initialize the game.
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
  `idSoundVoice*`, avoiding the base class's inline metadata-only setter.
- Allocated voices retain their lead-in and loop samples, including while idle, stopped
  or complete, until `FreeVoice` or hardware shutdown. Purge/reload/rename/destruction
  rejects pinned samples. Native `StopVoicesWithSample` must free voices before sample
  mutation; completion alone does not release the slot or prevent a later restart.
  `GetAmplitude` supplies the active sample's pre-gain peak, zero while paused/stopped/
  complete and one for active `SSF_NO_FLICKER`; native callers apply gain separately.
  Surround output remains unavailable and native `SoundVoice.cpp`/`s_subFraction` are
  not imported by this slice.


## Resident link findings

- Bundled JPEG 6/zlib 1.2.3 live in `src/external/`; the inventory remaps original
  Windows reference-project paths without editing those manifests. Target vendor lists
  are explicit and audited separately from campaign sources. JPEG's RGBA layout and
  C++ ABI are engine modifications; a stock libjpeg is not a drop-in replacement.
- JPEG's `jdatasrc.cpp` has no encoded length and blindly refills 4096 bytes. Exclude
  that source on the EE and fail portable source `LoadJPG` explicitly. The bounded SWF
  adapter supplies its own complete-memory source, limits dimensions/output, and keeps
  table-only/abbreviated images within a decoder lifetime. `jmemsys.h` requires the
  private declarations established by `jpeglib.h`; preserve their include order.
- `MY_ZCALLOC` excludes zutil.c's unchecked malloc/free product. Supply typed C-linkage
  defaults through the tagged heap and preserve caller-provided stream allocators.
  `deflate.c` references `compressBound`, so `compress.c` is required even without
  application calls to the one-shot compress API. Gzip stdio is not a dependency.

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

- Explicitly select `ps2::gamefixture::Enable` before Common startup. Native filesystem
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
- `InitHeadlessFixture` accepts only the bounded worldspawn map with two fixed axial
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
  updates and actor interpreter state. Only neutral/full-forward commands are accepted;
  buttons, strafing, view-angle changes and impulses fail before simulation. AAS remains
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
