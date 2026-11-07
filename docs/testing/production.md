# Production package procedures

Detailed acceptance for the production ELF's packages: `make test-headless`,
`test-headless-retail`, `test-ui` and `test-world`. The shared rules are in
[testing-pcsx2.md](../../.claude/rules/testing-pcsx2.md).

## Headless and retail boot

- `make test-headless` / `make BUILD=release test-headless` exercise the production ELF
  with authored `src/tests/data/headless/` data. One run completes exactly 120 frames; another
  continues past 120 until live HostFs commands request quit. Require active native
  console/NOMAP state, command history, rejection of partial/oversized/control-character
  input and exact startup ledger recovery after ordered shutdown. Archives include
  production link evidence, matched images, config/input hashes and both PCSX2 streams.
  PCSX2 mirrors IOP lines into stdout and emulog; count native markers from one stream
  while checking both for crashes. Host regressions in `test_headless_runner.py` reject
  false acceptance.
- `make test-headless-retail` runs the production ELF headless with the local retail
  `_common`/`_ordered` containers hard-linked into its package (`RETAIL_DATA_ROOT`,
  default `gamedata/d3_bfg`; symlinks are skipped by the native listing). It requires
  both container mounts, the full `doom_main.script` compile, the three startup
  memory checkpoints in order, 120 `NOMAP` frames and exact startup-ledger recovery.
  `summary.json` records each checkpoint, the largest tags and the compiled script
  size. The run takes about 20 s (7.8 s of script compilation); `make run-headless-retail`
  stages and launches the same package until quit. Retail data never enters the repo.

## Input devices

- Every visible package brings up the pad and the debug USB keyboard at presentation
  startup and logs `[D3BFG] INPUT devices pad=N keyboard=N`, then an `INPUT` status line
  with `ps2_status`. In PCSX2 the emulated DualShock2 connects in analog mode and the
  `hidkbd` USB keyboard opens; the gates press nothing, so no device event reaches them,
  and the idle keyboard is read about every 250 ms. The keyboard's modules load from the
  package's `irx/` (staged from `$PS2SDK/iop/irx`); a module that fails only disables
  that device.

## UI session (`make test-ui`)

- `make test-ui` / `make BUILD=release test-ui` run every UI screen in two emulator
  sessions plus the invalid-resource processes. `run_ui_session.py` launches one
  production session per standard from a package whose `presentation.cfg` reads `ntsc menu`
  or `pal menu`, and drives ten steps through HostFs: `boot` (the menu the policy opened),
  `console`, `fonts`, `inactive` and `execution` (the movies), `dialog`, `shell`, `menu`
  (three handler loads), `screens` (Campaign and Settings) and `quit`, whose dialog accept
  ends the session right after the final capture. Each step opens with an
  `echo <id>_<step>_begin` barrier and ends with `_end`; its checks read only its slice of
  the log (boot from the log's start, quit to its end), and each step has its own watchdog.
  Images a step opens stay warm for the rest of the session, so later steps'
  `RETAIL textures` counters include them: after the boot, every step sees the menu's
  images. Session checks span the slices: the boot policy, barrier order, one presentation
  per Common frame, no unreviewed screen, no unrendered glyph size outside the
  `inactive`/`execution` steps (their isolated English labels draw at the movie's largest
  text height through native quads, and a size logs once per session), the final capture
  drained by the dialog's quit and exact startup-ledger recovery. Sessions run in the front end's
  display mode: 640 wide, FIELD, the default 32/32 preference drawn as 32/16 (`GS ready NTSC
  640x448 FIELD PSMCT32+PSMCT16S/Z16S fixed_vram=2293760`), with captures 640 wide and the menu's
  glyph sets cooked for that width. The texture range the first `GS ready` line leaves decides the
  eviction policy (`run_ui_session.texture_range`): below 1,900,544 bytes, the smallest measured to
  hold the whole session (PAL's 1,572,864), screens evict and upload textures again when they
  change, and the step classifiers accept evictions and uploads across screens and reloads (the
  held dialog between its two captures still uploads nothing), and the session allows at most
  `EVICTION_LIMIT` (32) evictions in all (PAL 23 at 640, 2026-11-06); NTSC must evict nothing. A debug NTSC session
  takes about 470 s: boot 93, console 5, fonts 15, inactive 11, execution 62, dialog 8,
  shell 40, menu 107, screens 63 and quit 53. `summary.json` records
  each step's verdict, captures and duration. Then `run_retail_ui.py` and
  `run_retail_movies.py` run their 5 and 13 invalid-resource cases, each ending in its
  expected fatal in a fresh process. Release runs 11 movie cases: `movie-budget` and
  `movie-native-alias` fail through run-time script checks, which only assertion builds
  have, and release logs no `SWF dependency` traces, so the steps skip those checks. (The placeholder authored SWF session and its 12
  cases were removed with the authored UI on 2026-10-29.)
- The console step exercises the production ELF visibly in NTSC and PAL. Startup policy is selected by package-root
  `presentation.cfg`; `make stage-console` defaults to NTSC and `CONSOLE_STANDARD=pal`
  stages PAL (`ntsc menu`/`pal menu` boot into the native menu; `make stage-ui-menu`). Every visible package stages the cooked retail catalog from
  `UI_COOK_OUTPUT` (`make ui-cook` first); the console draws the original charset from
  it. Once the boot step has released the menu, the step submits and replays native edit
  history, closes/reopens the real console and checks an edited input line, with
  redraw-only requests for each Enter. The edit capture checks the translucent/uncovered
  background, the cyan border and 24 input-line probes derived from the cooked charset
  (`console_capture.expected_probes`): the strongest ink and one hole per character of
  `]echo pending`, each accepting the colors the converted texture can produce within
  half a texel of its projection, plus the gap between the words. The TGA,
  input/config/data hashes and matched production evidence are archived. Inspect
  images as well as logs; this proves console presentation, not SWF/menu or hardware
  acceptance. Run all emulator targets sequentially in isolated sessions.
- The `inactive` and `execution` steps check inactive dictionaries and executable
  native shell/dialog timelines. Each has four phases, which each run at least 120
  frames: shell, dialog, shell return and another shell load; captures are named
  `movie-<step>-<phase>.tga`. Check native dictionary/
  font/text counts, all 9,097 English entries, source-derived integer text widths and
  at least two glyph/color probes per label. Each probe accepts the colors the converted
  font can produce within one texel of its projected position (sound bilinear bounds,
  strongest first), plus the existing channel tolerance of eight within one projected
  pixel for GS raster rounding. Inspect both styles and
  standards. Shell reload captures must match; warm dialog captures must match. Only
  the documented NTSC cold/warm final-T bottom edge admits four changed pixels with
  channel delta at most 25, within the source-derived glyph row/bounds. Source probes
  stay unchanged. Inactive loaded ledgers remain exact after lazy binding. Executable
  movie ledgers can vary with animation frames: compare exact post-release warm ledgers,
  retain the 16 MiB loaded-heap ceiling and require full startup recovery at shutdown.
  HostFs adds every non-ps2 command to history before executing the batch: clear history
  before the final unload, or the future long echo identity contributes a transient
  64-byte string to its release checkpoint.
  The `RETAIL textures` counters must equal the open converted images exactly (images,
  pages, CLUTs, pixel and metadata bytes, computed from the cooked GSTX files): fonts
  and their glyph sets (a font's set opens with it), plus each movie atlas loaded so
  far, plus the Doom 3 cover once the shell executes, plus what earlier steps left warm.
  Triangles rise, evictions stay zero and uploads stay fixed after the first phase. Executable
  phases require fresh real callback/owner traces and bounded hierarchy/work counters.
  Twelve fresh processes reject bad profile, missing movie, corrupt movie/English bytes,
  oversized counts and init actions. Count/init cases recompute FNV to reach storage
  preflight. Placement/opcode/branch/operand cases reach execution preflight; a valid
  infinite loop and an alias to a different native service reach their runtime guards.
  The minimal owner uses
  the real carousel/Credits callbacks and only one Doom 3 option; shell navigation is
  covered by the `menu`, `screens` and `quit` steps below, while the screens below
  Campaign and Settings and physical PS2 acceptance remain pending.
- The `dialog` step draws the original dialog in full with Common's quit message: one 120-frame load and
  two 30-frame warm reloads. Each load needs exactly one draw admission matching the
  inventory (ten masks; no outlines, screen blends or additive color), the native quit
  owner state and English options, dialog script dependencies, rising draw/mask/texture
  triangle counters, texture counters equal to the fonts, their glyph sets and the
  dialog atlas (with what earlier steps left warm), zero
  evictions, the same upload count at every capture of all three loads,
  identical warm release ledgers. Two captures ten frames apart locate the looping focus highlight;
  every other pixel must match across all loads, and frames must show textured detail.
  The `dialog-alpha-add` negative re-places the first root character with an additive
  alpha term, the one color term without a draw path, and requires the specific
  draw-admission fatal. Resident paletted textures draw a frame in about
  55–60 ms in debug PCSX2 (streamed RGBA pages took about 1.6 s); since the dialog's text
  draws from pre-rendered glyphs, frames take about 40 ms. Closing the window (local settings skip the
  shutdown confirmation) ends the VM cleanly; the runner reports "emulator exited with
  status 0 after N s" as the session end. That is an interrupted run, not a target fault.
- The `shell` step draws the original shell in full through `ps2_uiRetail draw shell`: one 120-frame load and two
  30-frame warm reloads on the fixed-step clock (`ps2_uiRetail clock fixed`: 1/60 s per
  Common frame from the native activation time), so both warm reloads reach identical
  frames and their captures must match exactly. Each load needs a draw admission equal
  to the inventory (masks, outlines, screen placements, additive placements), the minimal
  carousel/Credits owner, rising triangle, mask, depth-mask, line, screen and color-term
  counters, textures equal to the fonts, glyph sets, charset, shell atlas and Doom 3 cover,
  three channel CLUTs per atlas CLUT, zero evictions, fixed uploads and identical warm
  release ledgers. A debug PCSX2 frame takes 100–140 ms on the fixed clock. Every text size the
  start screen draws is cooked, so its runs log no `UI glyph size` line. The owner does not run the native
  screens, so placeholder prompts and labels show: compare captures with the menu's
  expected state, not the PC's interactive one.
- The `menu` step opens the menu through the native shell handler (`ps2_uiRetail menu`) on the fixed clock and drives
  it with key events (`ps2_uiRetail key NAME`): one 120-frame load and two 30-frame warm
  reloads. Each load captures the start screen (a), presses accept and captures the root
  screen 90 frames later (b), presses down twice and captures Credits focused 30 frames
  later (c), then presses back and captures the start screen 90 frames later (d). The
  first load uses keyboard keys (enter, down, escape), the warm reloads pad buttons
  (cross, dpad_down, circle). Each capture needs the handler's screen, session and root
  focus (0/PRESS_START/0, 1/IDLE/0, 1/IDLE/2, 0/PRESS_START/2), platform 1, five admitted
  callbacks and exact menu event counts (0/0, 2/1, 6/5, 8/6 received/handled: a released
  button has no handler, a scroll handles both halves), plus the shell's draw admission,
  textures and channel CLUTs, rising draw counters, zero evictions and fixed uploads. No
  `UI glyph size` line may appear. Warm releases must match and both warm reloads'
  captures must match pixel for pixel. A key issued in the frame of a `ps2_capture` runs
  before that frame draws, so `wait 1` separates each capture from the next key. The
  retail carousel items lack a `sel_up` frame, so native code warns on each load and focus
  change; the warnings are expected. The last step, `quit`, is one load that ends the
  session through the native quit dialog: accept (cross) signs in, dpad_up wraps the
  root's focus to Quit, cross opens Common's confirmation (d), dpad_right focuses
  Cancel (e) and cross closes it (f), cross reopens it (g) and a final cross accepts,
  quitting the game right after the queued final capture; the runner sends no `quit`.
  Each capture needs the screen, session and focus plus the event counts and dialog
  state (`dialog=1 quit=2` while shown); a dialog takes both halves of every key until a
  choice closes it. Require one shell and one dialog draw admission, the shell, dialog
  atlas, fonts, glyph sets, charset and cover resident with the channel CLUTs, the
  dialog's layout services after it opens, one copied choice call each for Cancel and
  Accept and one release after the final capture. The dynamic
  dialog binds neither Escape nor circle to cancel; its options take focus left to
  right. Its `<JOY1>` prompt draws as the PS3 cross art, and no `UI glyph size` line may
  appear; the dialog and shell steps require none either, and their textures
  include the eight prompt images a drawn movie loads. The first step, `boot`, drives the
  menu the package's `menu` policy opened at startup on real time, with no load or menu
  command sent: it captures the start screen 120 frames after the startup echo, the root
  90 frames after enter and the start screen 90 frames after escape, then unloads. It
  requires `BOOT presentation=<std> menu=1`, both draw admissions before the startup
  echo, the startup status with the console closed, `clock=real`, the handler's screen,
  session, focus and event counts, the resident textures, no unrendered size and one
  release after its last capture. The `screens` step opens the menu on the fixed clock
  and, from the root, enters Campaign (c),
  moves its focus (d), returns (e), moves the root's focus to Settings (f), enters it (g),
  moves its focus (h) and returns (i), capturing each: it requires screens 3 and 13 with the
  expected focus and event counts, no `shell capability unavailable` line, no unrendered
  size, rising draw counters, the resident textures and one release. The menu is warm by
  the `screens` and `quit` steps, so their start screens wait 30 frames, not 120.
- The `fonts` step (after `make ui-cook`) and five fresh-process resource failures
  check the retail fonts. Seven phases draw five native English fonts, return to the first and reload it, each for
  at least 120 frames. Check all three legacy metric pairs, glyph ink/hole probes bounded
  by what the converted font can produce within one texel, stable reload captures, the
  `RETAIL textures` counters (the five converted fonts with their glyph sets plus what
  earlier steps left warm, rising triangles, zero evictions, no uploads on the warm
  reload); the retail cycle counts every load of the session, so the step's are relative
  to its first. Reject bad
  catalog headers, missing/mutated font data, a texture whose tables differ from the
  catalog and a corrupt page block; paletted images verify every page at load, so all
  five fail before any loaded state or presentation. A corrupt catalog header fails at
  presentation startup, when the catalog opens for the console charset, before the
  package config echoes the run identity.
  Inspect all five fonts in both standards; proprietary captures and cooked files
  remain under ignored `build/`. This proves the resource screen, not a retail SWF,
  Shell navigation or physical PS2 performance. `make run-ui-fonts` launches it;
  append `ps2_uiRetail font 0` through `font 4`, `status` or `quit` to its console.txt.

## World session (`make test-world`)

- `make test-world` / `make BUILD=release test-world` (after `make world-cook` and
  `make ui-cook`, which the target runs or needs) run `run_world.py`: one production
  session per standard from a visible console package on the authored headless data, with
  `src/tests/data/graybox_map/materials/graybox.mtr` and the cooked
  `build/world/graybox.d3wd` staged under `base/`. No `.proc` is staged: the render world
  must take the D3WD.
- HostFs commands close the console, set `r_znear 4` (the GS gate's near plane), run
  `ps2_world load maps/graybox` (the `[D3BFG] world maps/graybox.d3wd: areas=2 surfaces=18
  batches=26 portals=1 nodes=11` line and `WORLD loaded areas=2 portals=1`), then three
  `ps2_world trace`s through the area models' s16 mirror (room 0's floor at fraction 0.5
  facing up, the divider's post at 0.46 facing -x with `trim`, the open doorway a miss,
  each within 0.002).
- Six views, each `ps2_world portal 1 open|closed`, `ps2_world view EYE TARGET`, a capture
  two frames later and a `ps2_world status` two frames after that, between echo barriers.
  The status must report the view still drawing, the start area and the areas and batches
  the portal flow let it draw: `room0`, `room1`, `doorway` and `clip` (the doorway at the
  screen's right edge, partly behind the camera) 2 areas and 26 batches, `away` (facing room
  0's far wall) and `closed` (the doorway camera with the portal closed) 1 area and 14 batches.
- Captures: `room0`, `room1`, `doorway` and `clip` are the GS gate's cameras and must
  show its reference pixels at the capture's width (`renderer_capture.WORLD_SCENE_PIXELS_640`
  for production's default, `WORLD_SCENE_PIXELS` at 512, both written by
  `src/tools/scripts/world_reference.py` and 8-bit colours stored as each profile keeps them;
  its `--compare` matches a whole capture, which the point checks cannot, and a capture with
  room 1 missing behind the doorway scores 98.7 %, good ones 99.5 to 99.9 %); `closed` must equal `doorway` everywhere
  outside the doorway's screen rectangle (the opening seen from 436 units plus 3 pixels,
  centred on the frame), differ on at least half of it and show the clear colour (16, 24, 40)
  at the centre.
- Frame-buffer profiles: the package boots in the front end, whose `GS ready ... 640x448 FIELD
  PSMCT32+PSMCT16S/Z16S fixed_vram=2293760` (PAL `640x512`, 2,621,440) is the default 32/32
  preference drawn as 32/16. The load switches to the preference by itself: the `ingame` step must
  log one `[D3BFG] GS display 640x448 FIELD bits=32/32 PSMCT32/Z16S fixed_vram=2867200
  texture_vram=1327104` (PAL 3,276,800 and 917,504). The six views run there, then
  `ps2_frameBufferBits 16/16` switches live (`bits=16/16 PSMCT16/Z16`, NTSC 1,720,320, PAL
  1,966,080) and they run again as `<view>-16`; `ps2_frameBufferBits 32/16` (`bits=32/16`) draws
  `doorway-32-16`, and `ps2_frameBufferBits 32/32` back draws `doorway-32`. Every capture line
  carries the draw buffer's `bits=` (32 in 32/16, whose capture reads the 32-bit draw buffer).
  32-bit captures are checked against the exact reference colours within 2 levels (PSMCT16's model
  allows 8), the closed doorway against the exact clear colour; `doorway-32-16` and `doorway-32`
  must be byte-identical to the first `doorway`.
- Display modes: `ps2_resolution 0` must log one `GS display 512x448 FIELD` line (`512x512`
  PAL) and `doorway-512` must show the GS gate's 512-wide reference pixels; `ps2_resolution 1`
  back, and `doorway-640` must be byte-identical to the first `doorway`. Then `ps2_interlace
  frame` (`GS display 640x224 FRAME`, `640x256` PAL; fixed VRAM 1,474,560 / 1,638,400 at 32/32)
  and `doorway-frame` is captured at the half height and must show the 640-wide reference pixels,
  each at half its row (the camera's 4:3 field of view makes it the same picture at half the
  lines); `ps2_interlace field` back, and `doorway-field` must match the first `doorway`. Each
  step changes one cvar (`run_world.switch_command`); the expected line comes from
  `renderer_capture.frame_size` and `fixed_vram_bytes` for the session's standard. Every view
  keeps its culling counts. After `ps2_world unload` the session is back in the front end: the
  `frontend` barrier's span must hold one switch back to 32/16, and no display change may happen
  outside these steps.
- `ps2_world unload` must leave the empty single-area world (`areas=1 portals=0`), and the
  session quits with the exact startup ledger. A session takes about 9 s of emulation in
  debug. `test_world_runner.py` rejects a missing or refused cooked load, wrong traces,
  broken culling counts, missing, stale or misnamed captures and statuses, a capture in the
  wrong profile, a profile change that is missing, doubled, outside its step or with the
  wrong buffers, out-of-order barriers, an unfinished unload or ledger, and a closed doorway
  that changes pixels outside the opening or none.

## Package and UI gate overview

Summary of the production packages, their controls and the UI screens `make test-ui` covers.

- `make stage-headless` packages production with authored data under `build/<config>/headless/`;
  `make run` / `make run-headless` launch it. The native game stays in `NOMAP` while Common
  pumps console commands and native frames. `console.txt` accepts append-only lines via
  HostFs; `ps2_maxFrames=0` runs until quit. `make test-headless` checks 120 frames,
  live command input and full shutdown without linking fixtures into production.
  `make stage-headless-retail` / `make run-headless-retail` stage/run the same ELF with the local retail
  `_common`/`_ordered` containers hard-linked from `RETAIL_DATA_ROOT` (symlinks are not
  listed). `make test-headless-retail` checks every retail declaration and the full
  `doom_main.script` reaching `NOMAP`, and records the tagged `[D3BFG] MEMORY startup-*`
  ledgers production prints after foundation, declaration and game startup.
  `make stage-console` / `make run-console` stage/run the same ELF visibly under
  `build/<config>/console/`; `CONSOLE_STANDARD=ntsc` (default) or `pal` selects the mode. A PAL
  presentation sets `com_engineHz` 50 at startup (two ticks per 40 ms frame).
  Every visible package also stages the cooked retail catalog and assets from
  `UI_COOK_OUTPUT` (run `make ui-cook` first): the presentation opens the catalog at
  startup and the console draws the original `textures/bigchars` charset from it.
  Package-root `presentation.cfg` selects `headless`/`ntsc`/`pal` before Common startup;
  a missing file defaults to headless, and `ntsc menu`/`pal menu` also open the native
  menu at startup. The console step of `make test-ui` checks native console drawing,
  edit/history events and close/reopen in both standards.
  HostFs `ps2_consoleText`, `ps2_consoleKey` and `ps2_capture` are visible-mode controls.
  Common frames own exactly one native console presentation; `UpdateScreen` coalesces
  redraw requests during a frame and never advances simulation. Finish queued capture
  and final presentation before UI/device shutdown; visible vsync adds no headless sleep.
  Every native movie is a retail resource: `idSWF::LoadBinary` refuses any other movie
  by name. The placeholder authored SWF and its streamed UIPG pages that brought the
  native SWF up were removed on 2026-10-29; their BSWF and font remain host-test
  stand-ins only (`src/tests/data/ui/`, generated by `src/tests/host/scripts/swf_fixture.py`).
  `make stage-ui-fonts` / `make run-ui-fonts` stage/run the opt-in English retail font
  screen from the local `make ui-cook` output, using `CONSOLE_STANDARD` and `BUILD`.
  The font step of `make test-ui` checks five real native fonts at 12/24/48-point scales,
  full legacy layout metrics, source-derived pixels, 120 frames per family and
  return/reload; five corrupt-resource cases run in fresh processes. HostFs `ps2_uiRetail`
  accepts load/unload/reload/status or `font INDEX` (0–4). Retail images stay warm after
  their screens unload, under one 2 MiB texture budget. No retail SWF or Shell is
  loaded by that font-only mode. The separate movie profile below admits bounded
  dictionaries, English text and timeline execution; full drawing and offline Shell
  integration remain pending.
  `make stage-ui-movies` / `make run-ui-movies` stage/run original shell/dialog BSWFs
  and the complete English dictionary under `build/<config>/ui-movies/`.
  `ps2_uiRetail movie shell|dialog|none` selects inactive native dictionaries and
  isolated English text layout. The session's inactive step checks native storage/layout,
  warm reloads and resident textures; malformed resources fail in fresh processes.
  Resource-only constructors never run imported AVM1/placements; inactive inspection
  rejects Activate (and, in assertion builds, timeline advancement); only the native menu
  admits key events. Native
  dictionary shutdown releases retained localization keys/pools after movies/fonts.
  `ps2_uiRetail execute shell|dialog` separately admits placement/AVM1 operands and
  executes native timelines before presentation, without full movie drawing or event routing.
  The shell borrows a minimal native widget owner: the original carousel has five slots
  with only the admitted Doom 3 cover, and the original Credits screen owns its callback.
  Assertion builds check callback identity, signatures, destructive ancestor seeks and the
  script work budgets; release builds trust the load-time preflight and only count work.
  The session's execution step checks executable movies. `ps2_uiRetail dialog quit` draws the
  original dialog in full through native `Render`, showing Common's quit message, and
  `ps2_uiRetail draw shell` draws the original shell's start screen; full drawing is a
  third admission, refusing only additive alpha terms. `ps2_uiRetail logo on|off` plays the
  native shell's logo transition and `ps2_uiRetail clock fixed` advances executing movies
  1/60 s per Common frame. The session's dialog and shell steps check them.
  `ps2_uiRetail menu` opens the menu through the native `idMenuHandler_Shell`
  (start, root and credits screens bound; PS3 platform layout). Common routes events the
  console leaves to it, as `MenuEvent` does; key events are a fourth
  admission, and each native handler a key reaches must be a widget event of the menu's
  own live widgets. `ps2_uiRetail key NAME` posts a keyboard key (enter, escape, arrows)
  or pad button (cross, circle, dpad_*) through the event loop: accept signs in and shows
  the root screen, the arrows move its focus and back returns to press-start. The
  Campaign (New Game, Load Game; no save to continue) and Settings (Controls, Game
  Options, System) screens open from the root and return to it; a move into any screen
  below them fails by name. The menu also initializes Common's
  dialog manager, which opens the admitted dialog movie inactive beside the shell (retail
  admission is per movie): the root's Quit shows the native confirmation, an active dialog
  takes events first, its choices are pinned by identity, and accept quits through
  Common. `make stage-ui-menu` / `make run-ui-menu` stage/run a package whose policy
  boots into the menu, before the startup commands, on real time. `make test-ui` runs
  every UI screen in few launches: one session per standard boots such a package into
  the menu and drives, step by step, the console, the fonts, inactive and executing
  movies, the drawn dialog and shell, the menu's reloads, the Campaign and Settings
  screens and last the quit dialog, whose accept ends the session. Each step is checked on
  its slice of the log between echo barriers; images earlier steps opened stay warm in
  later steps' counters. Each invalid-resource case (5 font, 13 movie) runs in its own
  process; release skips the two movie cases that fail at run time (budget, native alias).
  Retail text draws pad button tags (`<JOY1>`) as the cooked PS3 prompt art, which a
  drawn movie loads when admitted. A visible package also brings up the DualShock pad
  (the ROM's SIO2MAN/PADMAN through libpad) and a debug USB keyboard (usbd and ps2kbd,
  staged from the SDK under `irx/`), both posting key events into Common's queue: cross
  is `K_JOY1`, the keyboard's grave key toggles the console and typed characters reach its
  edit field. The idle keyboard is read every 250 ms, every frame only while in use. Converted pages stay resident: a debug PCSX2 frame
  takes about 60 ms for the dialog and 100–140 ms for the shell on the fixed clock. The
  screens below Campaign and Settings, dynamic resources and artwork residency remain
  pending.
