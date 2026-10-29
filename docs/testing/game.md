# Game test image procedures

Detailed acceptance for `make test-game` and `make test-map` on the game test image
(`make build-game`). The shared rules are in
[testing-pcsx2.md](../../.claude/rules/testing-pcsx2.md).

## Game fixture

- `make build-game` builds `build/<config>/tests/game/d3bfg_tests.elf`. `make test-game`
  runs `game`, `game-missing-map`, `game-syntax`, `game-geometry`, `game-material`,
  `game-player-args`, `game-player-script`, `game-player-command`, `game-player-state`,
  `game-ceiling-geometry` and `game-step-geometry`
  sequentially in fresh processes;
  `make BUILD=release test-game` selects assertions-disabled validation. To run one
  scenario directly, pass the resident ELF with `--elf` to `run_pcsx2_test.py`.
  Core scenarios still require the separate core ELF.
- Game runs stage `game.manifest`, minimal declarations, default script/defines and
  a worldspawn floor/wall/low-roof/step map/script and stage-free collision materials under `game-fixture/`. The native filesystem needs
  both `fs_game` and its search-path gamedir set to that root for relative script paths.
  No core filesystem substitute or retail containers are used. Resident report/image/map
  hashes and flags must match before launch; authored file hashes are archived too.
  Generated text/binary collision caches remain in the archive and are hashed in
  `summary.json` after process cleanup.
- A game pass requires the matching closed JSON, begin/final markers, all forty-nine `game/`
  checks and exactly three cycles of 331 native frames. Each cycle starts with eight
  frame/entity/script, command, lifetime, wall-stop, falling, grounded-sliding and
  player-input/physics traces at native 60 Hz.
  Checks cover native initialization, logic world, ticks/script events, map shutdown,
  native script target activation, successful delayed-event posting, removal/cancellation,
  stable warm reload accounting and full pre-boot
  ledger recovery.
  Each `GAME_EVENTS` trace must confirm two posted events with 90/120 ms deadlines.
  `GAME_COMMAND` must return `fixture-activated` only on frames four and six, empty on every
  other frame, and report no pending native command after each return. The 90 ms event
  first becomes due on frame six (100 ms). Script `remove` runs on frame seven (116 ms);
  `GAME_LIFETIME` must show the cached native handle, resolution and name lookup present
  through frame six and invalid/absent on frames seven and eight. The empty return at
  133 ms verifies cancellation of the 120 ms event. Early/late,
  missing, repeated or unconsumed commands fail even with PASS check markers.
  Three `GAME_COLLISION` traces require matching point/box fractions and z endpoints,
  inside/outside contents and volume-mask rejection. Four collision checks also require
  fixed world bounds, entity identity/pass-entity filtering and clip disable/enable.
  `GAME_PHYSICS` must move x to 8.192/16.896 on the first two frames and stay within
  0.05 of 21.75 from frame three, with y=0/z=16 and wall-clipped velocity. These
  checks must pass on conversion, text-cache and binary-cache loads. Map shutdown
  requires zero COLLISION/PHYSICS_CLIP/PHYSICS_CLIP_ENTITY allocations and an empty trace-model cache.
  `GAME_FALL` checks old-velocity integration with a fixture gravity of 512 units/s²,
  landing around z=2.251 on frame six and floor contact/rest/zero velocity on frames
  seven/eight. `GAME_SLIDE` requires ground/floor support throughout, wall blocking
  and native `MM_SLIDING` from frame three, clipped inward velocity and continued
  bounded tangential motion. Missing/incorrect traces fail despite PASS markers.
  `GAME_PLAYER` requires forward input 127 on frames one through four, reverse -127
  on frame five and neutral input afterward. Native command cursors, timestamps and
  empty pending state must match each frame. Player physics accelerates into the wall
  on frame four, moves away on frame five and stops through release friction on frame
  eight, with native floor support throughout. Position/velocity traces are checked
  numerically; the command buffer resets after every map shutdown.
  `GAME_POSTURE` covers frames nine through 88, with exact queue cursors, button bits,
  timestamps and completed script state (eight increments). Jump stays held through
  frame 42; takeoff occurs only on frame nine, the 16-unit apex at frame 23 and landing
  at frame 38. Release on frame 43 permits a second takeoff on frame 44 and landing
  on frame 73. Every vertical position/velocity must match the gravity-512 parabola,
  with stationary x/y and native floor contacts on landing. Combined crouch/jump
  input on frames 76–84 must shrink height from 74 to 38 without takeoff; release
  restores standing height on frames 85–88. The completed monster probes are inactive
  during this phase. Missing, duplicated, reordered or incorrect posture traces fail
  despite PASS markers. Standing restoration is tested only with unobstructed headroom;
  the native player ceiling and step phases below add blocked restoration and bounded stepping.
  `GAME_NATIVE_PLAYER` covers frames 89–99 with one native player in slot zero. The
  earlier physics probe is inactive, and its command cursors reset before player
  spawn. Forward commands on frames 89–92 accelerate using native walk speed 140;
  neutral commands apply native ground friction and stop by frame 99 at x=-26.192.
  Every trace requires native floor contact, health 100, exact timestamps/cursors,
  no pending command, one constructor execution and script increments 1–11. Five
  checks require real player identity/clip shape, native command delivery, script
  execution, movement and linked conditions/empty resources. Map shutdown invalidates
  the player handle and name/client-slot lookup. This uses bounded player setup and
  Think methods; regular campaign Init/SpawnToPoint, weapons, models, HUD and AAS/PVS
  remain pending. Missing, duplicate, incorrect or unconsumed traces fail even with PASS markers.
  `GAME_PLAYER_STATE` adds 96 native-player traces per cycle on frames 100–195.
  Jump stays held through frame 140, releases on 141 and takes off again on 142.
  Floor contacts on 135/177 precede velocity settling; soft-landing flags span
  135–136/177–178. Combined crouch/jump on 181–189 cannot take off; forward input
  on 181–184 follows native crouch speed 80 and friction. Eye height interpolates
  toward 32 while crouched and 68 after release. Require the native actor-state
  names and authored counters: seven state entries, two jumps/landings, four soft-flag
  ticks, one constructor and 107 script ticks. Exact queue/time/conditions/health
  and numeric movement/clip/view traces are required alongside five new checks.
  `GAME_HEADROOM_WORLD` adds standing/crouch swept-box traces and roof/gap contents.
  `GAME_HEADROOM` adds 80 traces per cycle, frames 196–275. The same native player
  is placed at (-48,40,0.25), and completed probe bodies are disabled. Standing
  forward input blocks around x=-40.25; crouch on 212–231 permits passage. Release
  on 232 stays crouched, including a stationary tick on 239. Released forward input
  on 240–264 uses crouch speed while native headroom remains blocked. Post-move
  headroom becomes clear on 264, and native CheckDuck restores height 74 on 265.
  Require exact queue/time/health/AI/state fields, movement/eye interpolation and the
  upward sweep fraction 0.15278 on 213–263, then 1.0. The actor has nine state entries
  and 187 ticks by frame 275, while jump/landing counters stay unchanged. Six new
  markers and every numeric trace are required; duplicated/reordered/missing rows fail.
  `GAME_STEP_WORLD` adds low/high swept-box collision and surface-height traces.
  `GAME_STEP` adds 56 traces per cycle, frames 276–331. The same player starts at
  (-48,-40,0.25), climbs the 12-unit platform on frame 283 with signed step height 12,
  and blocks against a further 24-unit rise on 293. Native step limit stays 16; no
  jump is injected. Forward stays held through 315, then neutral commands require
  zero velocity at (-16.25,-40,12.25). Ground/AI/script state stays idle, with 243
  script ticks and nine state entries by 331. Eye height continues toward 68.
  Six added markers and every queue/time/position/velocity/step/contact/view/state
  field are required. Missing, duplicate or reordered rows fail despite PASS markers.
  `core SKIP` means the core test suite was not run, although staged Common foundation
  startup executes.
  The entry's platform marker covers manifest/host I/O, not the SDK-only platform suite.
- Game negatives must observe native initialization and the correct first fatal after
  game startup: the missing-map diagnostic or a source-located `maps/logic.script`
  error, or the geometry/material restriction diagnostic before collision loading;
  extra player spawn keys before factory construction, missing `AI_ONGROUND` script
  storage during native linkage, attack input before movement/script execution,
  a missing `FixtureAir` function during native actor `setState`, and a changed roof
  plane or step height before collision loading.
  A result file, wrong source, unrelated first fatal, bus/TLB error, watchdog
  or nonzero emulator status fails. Fourteen host regressions verify these classifications
  and resident archive identity; target debug/release runs are required too.
- This is bounded logic-map/player acceptance: native entity thinking, script `waitFrame`
  scheduling, monster/player physics, restricted native player ticks with injected commands and map teardown run,
  while AAS/PVS, full campaign player behavior, device input, sound/
  render worlds and regular campaign map loading remain pending. Sound and rendering
  integration are deferred while the headless gameplay fixture grows.

## Doom authored native map procedure

- `make test-map` / `make BUILD=release test-map` run one integrated `game-map`
  scenario against the resident ELF. The runner copies only committed authored data
  from `src/tests/data/headless_map/`; it hashes the map/proc/AAS/decl/scripts and
  archives generated collision/render-world caches with matched ELF/symbols/map.
- Seventeen distinct required checks cover native startup, world/model/trace queries,
  portal open/close/reopen PVS, native floor collision, AAS routing and actor binding,
  inactive presentation, native AI/map script ticks, map shutdown, warm ledger stability
  and full teardown. Fifteen checks repeat once per reload, with three 60-frame summaries;
  missing checks, stale identities, crashes and watchdogs fail.
- This runs native `InitFromNewMap` / `MapPopulate`, with automatic lobby-player spawning
  omitted only in the explicit map mode. One idle unanimated native AI uses an explicit
  collision box and `neverDormant` because no player supplies visibility. The AAS is
  hand-authored with the actual geometry CRC. An empty native sound world supplies
  bookkeeping while playback/drawing stay inactive. Normal campaign player startup,
  animated AI/gameplay, Common transitions and retail maps remain later gates.
- Debug/release final map runs on 2026-10-11:
  `20261011T085646Z_smoke_00b8de4c38964b7c` /
  `20261011T085653Z_smoke_ff579fc1a6344ce1`. Current memory evidence is in PORT_STATUS.
