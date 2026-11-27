# M3 implementation TODO: a game-loaded map in production

Companion to [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)'s M3 (section 12) and the
prerequisites at the top of [RENDERING_TODO.md](RENDERING_TODO.md). Opened **2026-11-27** from a
survey of the tree; nothing below is implemented yet. The goal: production loads
`game/mars_city1` through Common's own map change, the game spawns it from the retail data, draws
its player view, takes the pad's input, saves and loads, and unloads back to the ledger. It also
closes R2's remaining gate items (load-game, the game's door closing its portal for sound).

Tick items as they land and record their acceptance in [PORT_STATUS.md](../PORT_STATUS.md). Every
step keeps `test-headless`, `test-headless-retail`, `test-map`, `test-world`, `test-world-retail`
and `test-ui` green and the production link free of test inputs. AI animation and skinning (R4),
sound output, and everything the renderer does not draw yet (lights, particles, world GUIs) stay
out of scope: the game must run with them present and undrawn.

## Where production stands (2026-11-27)

- **Production never leaves `NOMAP`.** `idCommonLocal::Frame` calls `FrameProduction`
  (`common_frame.cpp:406-415`, `common_production.cpp:276`) instead of id's frame body, so the
  session's `LOADING` never reaches `ExecuteMapChange` (`common_frame.cpp:617-622`). A
  `sessionCommand` from the game is fatal (`common_production.cpp:291`). The `map` command calls
  `StartNewGame`, which puts the offline session in `LOADING` for good: only `ExecuteMapChange`
  calls `LoadingFinished` (`Common_load.cpp:556`).
- **`ExecuteMapChange` would stop at the renderer.** In call order (`Common_load.cpp:394-666`):
  `StartWipe` (`CaptureRenderToImage`), `renderSystem->BeginLevelLoad`, `LoadLoadingGui`
  (`new idSWF("loading/default")`, `LoadLevelImages`), `Preload`, then the render world and the
  game load, `usercmdGen->InitForNewMap`, `EndLevelLoad`, the autosave, and
  `BeginAutomaticBackgroundSwaps`. The renderer calls fail by method name
  (`logical_renderer.cpp:231-244`); `usercmdGen` is unsupported (`input/usercmd_stub.cpp`); the
  autosave reaches the save pipeline, whose every member is fatal (`system/filesystem.cpp:614-655`).
- **Only the test image has loaded a map in the game**, and only the authored `headless_map`:
  `map_boot.cpp:66-82` calls `InitFromMap` and `game->InitFromNewMap` directly, bypassing Common
  and the session, with no player, one idle AI without a mesh, and no models, sounds, images or
  GUIs. Its fixture branches are `PS2_D3BFG_TESTS` code in id's game (`Game_local.cpp:1248-1252`,
  `2358-2368`, `Player.cpp:1318-1830`).
- **The retail map's game data is in id's own forms**, in `maps/mars_city1.resources`: the text
  `.map` (5,475,467 bytes: 4.85 MB brushes and patches, 592 KB entities), `generated/maps/game/
  mars_city1.bcm` (5,378,192, 425 models, CRC `0x8B89B51C`), the text `mars_city1.aas48`
  (1,961,240, the same CRC) and `generated/guis/game/mars_city1.bgui`. The map scripts are in the
  cooked program (`map_marscity1::*`). None of these has been read on the PS2. Opening a container
  entry under 8 MB reads it whole, so the `.map` and `.bcm` each sit in memory while they parse.
- **Models:** static binaries (`.blwo`, `.base`) load without the vertex cache; every `.bmd5mesh`
  reaches `vertexCache.AllocStaticVertex` (`Model_md5.cpp:760-762`, `tr_trisurf.cpp:1836-1838`),
  and `idVertexCache::ActuallyAlloc` fails by name (`logical_renderer.cpp:440`). Source model
  import (ASE, LWO, MA) is fatal (`model_import.cpp`). The cooked render world registers only the
  `_areaN` models, not the map's inline `func_static_N` brush models the game looks up.
- **Sounds:** `SoundSample::LoadResource` reads `<name>.wav` (`audio/sound_backend.cpp:73-100`)
  whatever `s_noSound` says, and the retail data has no `.wav`.
- **Cinematics:** a material with a `videomap` fails at load (`image_backend.cpp`, `idCinematic`).
- **Drawing:** `Present` draws only the `ps2_world` viewer's camera (`presentation.cpp:1239`);
  nothing calls `game->Draw`. `scene::RenderView` draws the `_areaN` entities alone
  (`scene.cpp:153-234`), so game render entities are linked into areas but never drawn. The D3WD v5
  entity records are validated and unused (`world_format.h:126-149`).
- **Saves:** `File_SaveGame.cpp` is not built (it needs zlib; the plan is miniz,
  [doom3-engine-cpp.md](../reference/doom3-engine-cpp.md)), the session's save and load are
  `OFFLINE_UNSUPPORTED` (`offline_session.cpp:729-737`), `GetSaveGameManager` fails by name, there
  is no memory card code, and `Sys_DefaultSavePath` is the package root (`sys.cpp:765-774`).
- **The menu:** `StartMenu`, `ExitMenu` and `MenuEvent` are fatal (`common_campaign.cpp:63-65`),
  so `LeaveGame` and `Stop` are too. The shell's new game issues `map game/mars_city1 0`
  (`MenuHandler_Shell.cpp:1227`).
- **Memory:** retail `startup-game` is 5,389,144 bytes of backing (debug), with a 21.6 MB heap gap.
  The D3WD is 5.78 MB whole; texture copies stream within 2 MiB. No figure exists for the game's
  side of a retail map (the parsed `.map`, collision, AAS, spawned entities, models).

## Decisions

- [ ] **The `.map` on the PS2** (after M3.1's measurement): id's text `.map` parsed whole, or a
      host-cooked entity-only `.map` that keeps id's parser. The geometry CRC that the `.bcm` and
      `.aas48` check is computed from the brushes and patches as they parse (`MapFile.cpp:258`,
      `521`; checked at `Game_local.cpp:1026` and `CollisionModel_files.cpp:587`), so an entity-only
      file must carry it. `idGameLocal::GetLevelMap` parses the `.map` again with primitives
      (`Game_local.cpp:2892-2905`): find its callers first.
- [ ] **The loading screen:** id's `loading/<map>` SWF and its images, or a PS2 screen (the console
      backdrop and a progress line). id's wipes capture the frame (`CaptureRenderToImage`), which
      the PS2 skips either way.
- [ ] **Where saves go first:** the package folder (host, USB, HDD: works in every gate) before the
      memory card (libmc, rom0 MCMAN/MCSERV, [ps2-platform.md](../../.claude/rules/ps2-platform.md)).
- [ ] **What `s_noSound` loads:** the default sample for every sound shader (no data read), until
      the sound milestone cooks the retail `.idwav` data.

## M3.1: the retail map's game data, measured (game test image)

Before Common is involved: what mars_city1's game side costs and whether id's loaders accept its
files. Extend `map_boot.cpp` with a retail case (`make test-map-retail`, the retail containers
staged like `test-headless-retail`), loading in steps with a `[D3BFG] MEMORY` checkpoint after each.

- [ ] Parse `maps/game/mars_city1.map` with `idMapFile`: the peak while parsing, the ledger with
      primitives and after `RemovePrimitiveData`, the parse time, and the geometry CRC (must equal
      `0x8B89B51C`).
- [ ] `collisionModelManager->LoadMap` from the retail `.bcm` (no rebuild: assert no `.cm` is
      written), its ledger, and the 152 `generated/collision/*.bcmodel` the entities' clip models use.
- [ ] The text AAS (`mars_city1.aas48`) through `idAASFileLocal`: the parse peak and its resident
      size; a cooked binary AAS only if the text parse costs too much.
- [ ] Record the figures in [memory-budget.md](../reference/memory-budget.md) and decide the
      `.map` question above.

## M3.2: mars_city1 spawned (game test image)

`InitFromNewMap` with the retail map and the cooked script program, still without Common: every
entity spawns and the game runs frames with an empty usercmd. Fix what the spawn reaches.

- [ ] Static vertex and index caches that need no GPU: `idVertexCache::AllocStatic*` returns a
      valid handle and keeps nothing (nothing draws md5 meshes before R4, which gives them VU1
      buffers), so `.bmd5mesh` models load. The frame caches stay unavailable.
- [ ] `s_noSound` makes every sample the default one (decision above), without reading a file.
- [ ] Cinematic materials load a static stand-in (one black frame) instead of failing; the
      material keeps its stage.
- [ ] The cooked render world registers the map's inline brush models (`func_static_N` and the
      other `*_N` names the `.proc` defined) from the D3WD, so `FindModel` finds them: as empty
      models for the merged entities, with their own surfaces for the rigid ones (M3.5 draws them).
- [ ] Every model, skin, particle, animation and GUI the spawn asks for is found in the retail
      binaries; list any that falls back to source import and cook or refuse it by name.
- [ ] The full player: `SyncPlayersWithLobbyUsers` spawns the real `idPlayer` (not the fixture's
      restricted one) at the map's `info_player_start`, with its weapons and HUD GUI loaded.
- [ ] 60 frames of `RunFrame`, then `MapShutdown`; three cycles return the ledger exactly after a
      warm one. Record the spawn peak and the in-map ledger.

## M3.3: Common's map change in production

The same load through Common, from the console and from the menu's New Game.

- [ ] `FrameProduction` runs the session's map change: a `LOADING` session reaches
      `ExecuteMapChange`, and the game's `sessionCommand`s (map, `died`, disconnect) are handled as
      id's frame does instead of failing.
- [ ] The logical renderer's level-load calls (`BeginLevelLoad`, `EndLevelLoad`,
      `LoadLevelImages`, `Preload`, `BeginAutomaticBackgroundSwaps`) do what the PS2 needs: free
      and reload the UI's cached images around the map, and keep presenting the loading screen
      while the load runs. `StartWipe` skips the capture.
- [ ] The loading screen (decision above), drawn by `UpdateScreen` during the load.
- [ ] `usercmdGen->InitForNewMap` and the rest of the usercmd interface exist (empty commands
      until M3.4).
- [ ] The autosave is off until M3.6 (`com_disableAutoSaves` exists in debug only; give release
      the same behaviour).
- [ ] The `ps2_world` viewer and Common share Common's render world: the viewer refuses `load`
      while the game holds a map, and `Present` takes the game's state for `InGame`.
- [ ] Gate: `make test-headless-retail` grows a map step: `map game/mars_city1` from
      `console.txt`, `[D3BFG] MEMORY` after the load, N in-game frames, the map unloaded, three
      cycles exact. A visible run (`stage-world-retail` with the game data) loads it from the
      menu's New Game.

## M3.4: the player's view and input

- [ ] `Present` calls `game->Draw` while a map is loaded (`idGameLocal::Draw`, `RenderPlayerView`,
      `RenderScene`), in the bracket's place of the viewer's camera. `SingleView`'s capture for a
      portal sky and the fullscreen effects that capture are skipped (`CaptureRenderToImage`).
- [ ] The player view draws the world as the viewer does from the same pose: a capture at the
      player start against the viewer's start view (`world_reference.py --compare`).
- [ ] The HUD: what the player's HUD SWF needs from the 2D path, drawn over the view, or left out
      by name until a later step.
- [ ] `usercmdGen` from the pad (move, look, jump, crouch, attack, use) and the debug keyboard
      when `ps2_keyboard` is set, through `input/pad.cpp` and `input/keyboard.cpp`.
- [ ] Gate: a scripted usercmd path walks the player from the start through the first door; the
      captures along it match the viewer's at the same poses.

## M3.5: entities in the world

- [ ] The D3WD entity records drive the game's render entities: a merged entity's model draws
      nothing (its triangles are area geometry), and a rigid one (doors, movers, items at rest)
      draws its own surface range with its entity's transform, the rest pose undone, instead of as
      area geometry.
- [ ] A door the game opens and closes moves on screen and sets its portal: `idMover_Binary`'s
      `FindPortal` and `SetPortalState` (`Mover.cpp:276`, `366`) against the cooked portals, and the
      sound world's walk through it (R2's "a door portal toggle still gates sound", now from the
      game).
- [ ] Entities the cook does not place yet (moveables and items whose model comes from their
      entityDef) draw through the same rigid path, from their spawned model.

## M3.6: saves and load-game

- [ ] miniz behind `idFile_SaveGamePipelined` (`File_SaveGame.cpp` built, its zlib calls mapped),
      on the background loader or synchronously with a bounded buffer: measure the save's size first.
- [ ] The offline session's `SaveGameSync`, `LoadGameSync`, enumeration and delete over the save
      folder (decision above), with `saveGame_enable` on.
- [ ] `idCommonLocal::SaveGame` and `LoadGame` round trip mars_city1: the restored game's entities,
      player and script state equal the saved ones (a checksum over the save stream read back).
- [ ] Gate: R2's "one load-game returns the ledger to baseline": save, unload, load the save, and
      the ledger after a warm cycle is exact. The autosave turns back on.

## M3.7: leaving and changing maps

- [ ] `StartMenu`, `ExitMenu` and `LeaveGame`: back to the native menu from a loaded map, the map
      unloaded and the UI's images back.
- [ ] Map transitions: mars_city1's exit to the next map through `ExecuteMapChange`, the previous
      map unloaded first (cooked worlds for the next map: `world-cook-retail`'s list grows).
- [ ] The player's death and the restart.
- [ ] Gate (M3): from the menu's New Game, mars_city1 loads, the player walks the start area,
      saves, loads the save, leaves to the menu and starts again; every ledger checkpoint holds.
