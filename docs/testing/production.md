# Production package procedures

Detailed acceptance for the production ELF's packages: `make test-headless`,
`test-headless-retail` and `test-ui`. The shared rules are in
[testing-pcsx2.md](../../.claude/rules/testing-pcsx2.md).

## Headless and retail boot

- `make test-headless` / `make BUILD=release test-headless` exercise the production ELF
  with authored `src/tests/data/headless/` data. One run completes exactly 120 frames; another
  continues past 120 until live HostFs commands request quit. Require active native
  console/NOMAP state, command history, rejection of partial/oversized/control-character
  input and exact startup ledger recovery after ordered shutdown. Archives include
  production link evidence, matched images, config/input hashes and both PCSX2 streams.
  PCSX2 mirrors IOP lines into stdout and emulog; count native markers from one stream
  while checking both for crashes. Seven host regressions reject false acceptance.
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
  drained by the dialog's quit and exact startup-ledger recovery. A debug NTSC session
  takes about 470 s: boot 93, console 5, fonts 15, inactive 11, execution 62, dialog 8,
  shell 40, menu 107, screens 63 and quit 53. `summary.json` records
  each step's verdict, captures and duration. Then `run_retail_ui.py` and
  `run_retail_movies.py` run their 5 and 13 invalid-resource cases, each ending in its
  expected fatal in a fresh process. (The placeholder authored SWF session and its 12
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
  the real carousel/Credits callbacks and only one Doom 3 option; Shell navigation, the
  shell's later screens and physical PS2 acceptance remain pending.
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
