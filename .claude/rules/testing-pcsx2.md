# Testing: Doom core and Quake II PS2 reference procedures

## Doom foundation procedures

- Build the current milestone with `make`, or select `make platform-probe` /
  `make headless-core` explicitly. `make release` chooses the release configuration.
  Match `--elf` and `--symbols` when passing a nondefault configuration to the runner.
- `python3 src/tools/scripts/run_pcsx2_test.py --scenario platform` stages an isolated
  manifest, ELF/symbols, logs and result under `build/test-results/<UTC-run-id>/`.
  Use `--scenario core` for the foundation bootstrap and `core-missing-fixture` for
  its negative fixture test. Build the matching ELF before each scenario.
- The runner refuses an already-running PCSX2, verifies HostFs and IOP/file logging
  read-only, uses `-logfile` to avoid the normal emulog path, and stops its own process
  after completion or timeout. A pass requires matching run identity, stage markers
  and result JSON. Crashes and watchdog expiration fail even if a PASS was printed.
- PCSX2 2.6.3 prints a valid `PCSX2 v2.6.3` line for `-version` but exits with status 1;
  `-help` also exits 1. The runner recognizes the version line and accepts status 0/1
  for that query only. It does not treat a test process exit as a pass.
- The platform probe passed on 2026-10-08; archived run
  `20261008T070310Z_smoke_88ff01f3485548c6` checks timer progress/conversion,
  64-byte static/stack buffer alignment, scalar vector math and the EE FPU's finite
  divide-by-zero behavior. This does not establish EE cache coherency or GS timing.
- Latest debug/release core and missing-fixture runs passed on 2026-10-10. Run identities,
  memory measurements and the precise foundation boundary are recorded in
  [PORT_STATUS.md](../../docs/PORT_STATUS.md). `make test-host` also runs 49 runner
  regressions, including synthetic watchdog failures whose printed FAIL is expected.
- Shared diagnostics checks add five host regressions for channel/variadic forwarding,
  long output, assertion evaluation/disable behavior, source-location diagnostics and
  fatal/heap-failure stdout flushing. `make test-host` builds both assertion settings
  under the strict warning set and ASan/UBSan, alongside the shared heap checks.
- Shared host/EE class-allocation checks cover ordinary/over-aligned objects, exact
  accounting and out-of-order free, signed counter limits, rejected requests without
  mutation, and factory constructor/diagnostic/virtual-delete ordering. Host subprocess
  checks require allocation rejection and counter underflow to remain fatal with
  assertions disabled. These test the adapter, not a linked `idGameLocal` fixture.

- `make script-probe` / `make test-script` select the isolated real script compiler and
  program, with an empty native-event registry. `make BUILD=release test-script` verifies
  the same matrix with assertions disabled. This target has its own `-script` artifact
  directory and linker-GC boundary; it is not a resident game-link gate.
- Script runs archive authored inputs, matched images/symbols/map, flags and config.
  Positive cases require the run's completion identity; negative cases require program
  cleanup before the expected fatal diagnostic with the correct source filename.
  A watchdog, TLB error, missing cleanup or unrelated fatal cannot pass. Host classifier
  regressions verify these distinctions.
- Core smoke completion requires all seven `types/` markers: inherited/const queries,
  null/sibling rejection, separate-unit identity, actual menu/GUI/model declarations,
  and real file objects. Shared query tests also run with host ASan/UBSan. Four host
  compiler regressions reject unsafe queries and verify the desktop RTTI fallback.
  The complete host suite currently has 81 Python regressions, including shared PCM/voice timing, codec, source
  relocation audit, real JPEG
  and filesystem sanitizer fixtures.

- Core smoke now also requires six filesystem-service markers. The runner stages
  authored directory/text/70 KiB binary fixtures and archives their hashes even in
  the missing-main-fixture scenario. Checks exercise real permanent streams, memory
  bounds and cursor/EOF, directory/extension filters, device/path rejection, write/
  append/short reads, parent directories and native driver errors, and ZIP timestamps.
  Removal's observed PCSX2 error is documented in [ps2-platform.md](ps2-platform.md);
  the test verifies error propagation and side effects, not successful driver removal.

- `make test-common` / `make BUILD=release test-common` run 66 isolated probes. Seven
  stop real Common after system, idlib, commands, cvars, filesystem, jobs or session;
  acceptance requires exactly those startup stages, reverse shutdown, idempotent cleanup
  and exact tagged-ledger recovery. Static CVar registration is one-shot, so each stop
  uses a fresh process. Fifty-nine negative probes require matching run/begin identities and
  the expected fatal diagnostic: online flags, absent user, loading out of order, network
  matchmaking, Classic switching, absent save manager, unsupported input device, audio
  missing WAVs, unloaded duration queries, audio device initialization, invalid WAV
  formats/truncation/chunk bounds/payload budgets; absent/backward voice clocks, unloaded
  voice samples, negative seeks, invalid pitch, pinned sample mutation, double/foreign
  free, channel mismatch and unknown sound flags; process launch,
  negative duration input, ASE/LWO/Maya source-model import, renderer initialization,
  dimensions, draw submission, image/vertex/shader loading, cinematics and render demos,
  multiplayer ticks/chat/snapshot output/scoreboard activation/mode enumeration and
  save-manager access after forcing the disabled save cvar on; input initialization/map
  preparation, command building/access, inhibition, mouse/button/key sampling, null
  command strings and forced controller/rumble values.
  The first fatal line must match the expected diagnostic with a name boundary;
  InitForNewMap cannot satisfy an Init probe, and later messages cannot hide a wrong
  initial fatal. A watchdog, TLB/bus error, unexpected return or unrelated fatal fails.
- Regular core smoke requires nine `offline/` markers covering the real Common
  identity and `com_smp=0`/ROM policy, local user/profile/achievement state, unavailable
  persistence, copied match parameters, explicit loading completion, stable reload
  accounting and sign-out/routing/stale handles. Profile save failure must preserve
  transient stats/bits. These checks do not execute game ticks or load a map. Core smoke
  also requires seven `audio/` checks: native string/unloaded metadata ownership, real
  mono/stereo PCM timing/amplitude, generated-default/reload ownership, shared timeline
  behavior, voice playback/loop envelopes and pool reuse. Tests select `InitHeadless`
  with a manually advanced monotonic clock; no physical device or native sound world
  is initialized. All scopes
  recover exact requested/backing/count ledgers. The runner stages and hashes authored
  WAVs separately from the filesystem directory fixtures. A stereo fixture exceeds the
  core's 64 KiB memory-file limit and must load through a permanent stream. Host ASan/UBSan
  checks exercise all truncated prefixes, format/layout errors, read failures, legal odd
  chunk padding/reordering and 8,192 deterministic corruptions. Duration uses actual
  frame count/rate; 11,025 Hz must yield 1,000 ms for 11,025 frames rather than the native
  helper's rounded-rate result. Samples shorter than 1 ms fail before game looping can
  divide by a zero duration. Shared host/EE voice tests exercise fractional-frame completion,
  lead-in/loop boundaries, seeking, pause/resume, pitch changes/freezing, maximum clock
  jumps and update-cadence independence. Host sanitizer tests require fatal runtime
  validation with assertions enabled and disabled. Native pool checks fill/exhaust/reuse
  all 48 slots, reset controls, retain samples until free and recover the exact ledger on
  repeated shutdown/restart and destruction. The added
  `offline/common-idle-demo-ledger` covers repeated inactive-demo cleanup and offline
  queries; `core/platform-language-duration-utc` checks language bounds, maximum signed
  durations, leap days and the 2038 boundary. It establishes formatting, not clock accuracy.

- Three required `renderer/` markers check inactive native interface identity, unloaded
  image/buffer construction and exact scoped ledger recovery, and disabled resolution
  policy/cvar registration. No world is allocated or rendered. Dimensions, loading and
  submission fail by method name; the eight renderer probes verify those failures in
  fresh processes with assertions enabled and disabled.

- Three required `deferred/` markers check empty native multiplayer lifecycle and ledger
  recovery, save-description copy/clear ownership with warmed native dictionary pools,
  and the read-only disabled save cvar. These do not execute a game, shell or save
  pipeline. Shell vtable and retry-dialog providers currently have compile/link acceptance.

- Three required `input/` markers check the full native action table and exact lookup
  (including case and impulse-prefix collisions), repeated empty cleanup and native
  invalid-index results, and read-only controller/rumble preferences. The input provider
  acquires no devices or sampling buffers and publishes no player commands. Eleven
  input probes verify unavailable methods, null-string validation and forced preferences;
  changing an archived cvar cannot turn the boundary into an input source.

- Four required `codecs/` markers check the known CRC vector, independent wrapped/raw
  streams with chunked input/output and compression round trips, checksum/truncation and
  partial allocator failures, and RGBA JPEG/full/table/abbreviated reuse. All scopes
  recover exact requested/backing/count ledgers. Five JPEG failure probes reject invalid
  input, an overlong marker, truncation, an invalid signature and dimensions over 1024
  with the expected fatal on the EE. Host sanitizer checks also verify full decoder/output
  cleanup before fatal. Codec memory uses the same heap through native JPG/ZIP tags.

- `make link-game` passes the retained compile/link gate in debug/release: all 344 inputs,
  every required game/class/cvar registration root and map input are retained without
  unresolved symbols, duplicates, Classic imports or GC. Its resident entry requires
  an explicit authored fixture manifest. Do not reuse an older resident ELF after a failed attempt.
  The default runnable ELF remains the validated core/codec/audio fixture.
  Six host regressions reject stale images, nonzero links, missing registrations/map
  inputs, non-executables and ignored undefined diagnostics.

- `make headless-game` builds `build/<config>/resident/d3bfg.elf`. `make test-game`
  runs `game`, `game-missing-map`, `game-syntax`, `game-geometry` and `game-material`
  sequentially in fresh processes;
  `make BUILD=release test-game` selects assertions-disabled validation. To run one
  scenario directly, pass the resident ELF with `--elf` to `run_pcsx2_test.py`.
  Core scenarios still require the separate core ELF.
- Game runs stage `game.manifest`, minimal declarations, default script/defines and
  a worldspawn floor/wall map/script and stage-free collision materials under `game-fixture/`. The native filesystem needs
  both `fs_game` and its search-path gamedir set to that root for relative script paths.
  No core filesystem substitute or retail containers are used. Resident report/image/map
  hashes and flags must match before launch; authored file hashes are archived too.
  Generated text/binary collision caches remain in the archive and are hashed in
  `summary.json` after process cleanup.
- A game pass requires the matching closed JSON, begin/final markers, all eighteen `game/`
  checks and exactly three cycles of eight frame/entity/script, command, lifetime,
  wall-stop, falling and grounded-sliding traces at
  native 60 Hz.
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
  requires zero COLLISION/PHYSICS_CLIP allocations and an empty trace-model cache.
  `GAME_FALL` checks old-velocity integration with a fixture gravity of 512 units/s²,
  landing around z=2.251 on frame six and floor contact/rest/zero velocity on frames
  seven/eight. `GAME_SLIDE` requires ground/floor support throughout, wall blocking
  and native `MM_SLIDING` from frame three, clipped inward velocity and continued
  bounded tangential motion. Missing/incorrect traces fail despite PASS markers.
  `core SKIP` means the core test suite was not run, although staged Common foundation
  startup executes.
  The entry's platform marker covers manifest/host I/O, not the SDK-only platform suite.
- Game negatives must observe native initialization and the correct first fatal after
  game startup: the missing-map diagnostic or a source-located `maps/logic.script`
  error, or the geometry/material restriction diagnostic before collision loading.
  A result file, wrong source, unrelated first fatal, bus/TLB error, watchdog
  or nonzero emulator status fails. Eight host regressions verify these classifications
  and resident archive identity; target debug/release runs are required too.
- This is playerless logic-map acceptance: native entity thinking, script `waitFrame`
  scheduling, bounded collision/physics and map teardown run, while AAS/PVS, player commands, sound/
  render worlds and regular campaign map loading remain pending. Sound and rendering
  integration are deferred while the headless gameplay fixture grows.

## Quake II reference procedures

All commands, file paths, test counts, performance numbers and emulator results below
describe `quake2-ps2`. Apply them to Doom only after the required subsystem and data
exist; they are not Doom validation evidence.

## PCSX2 setup and logs

- App: `/Applications/PCSX2.app/Contents/MacOS/PCSX2`, launched as `PCSX2 -batch -elf <elf>`
  (`make run`). `host:` maps to the ELF's directory, and `make run` symlinks
  `build/<config>/baseq2` → the repo's `baseq2/`.
- Development through `host:` needs `[EmuCore] HostFs = true` in
  `~/Library/Application Support/PCSX2/inis/PCSX2.ini`. Disable it to test HDD/USB loading.
  Game stdout needs `[Logging] EnableIOPConsole = true`,
  because ps2sdk stdout goes through IOP fio, plus `EnableFileLogging = true` to land in
  `~/Library/Application Support/PCSX2/logs/emulog.txt`. Engine lines carry a `[Q2]` prefix.
- **Edit PCSX2.ini only while PCSX2 is closed.** It rewrites the file on exit.
- Every launch overwrites `emulog.txt`. Copy it out before the next launch if you need it.
- PCSX2 memory card slot 1: `~/Library/Application Support/PCSX2/memcards/Mcd001.ps2`.
  `Slot1_Enable = false` in the ini simulates "no card". Format notes are in the
  Quake II reference repository's `save-games.md` rule.
- `[USB1] Type = hidkbd` attaches a host-passthrough USB keyboard. It reports itself as JIS,
  and its boot `Missing host mapping for QKey` warnings are harmless. It sends HID usage
  `0x34` for the host's `` ` `` key, never `0x35`.
- `Pad: DS2 Config Finished ... VS: Normal - VL: Normal` in the log means the vibration motors
  were mapped (`padSetActAlign`).
- **PCSX2 emulated USB storage:**
  `[USB2] Type = Msd`, `Msd_subtype = 0` (Iomega Zip-100 / Generic), and
  `Msd_ImagePathMsd = /absolute/path/usb.img` attach a file-backed mass-storage device;
  USB1 can remain `hidkbd`. PCSX2 uses raw 512-byte sectors and opens the image `r+b`,
  so use a writable raw disk image; an MBR/FAT32 layout is a conservative test choice.
  Put `baseq2/` at its root for a directly launched host ELF. Disable `[EmuCore] HostFs`
  or launch from a folder without host game data, since the successful host probe skips
  storage bring-up. Also disable HDD if it has a matching installation: HDD takes
  precedence over USB. The embedded BDM modules probe `mass0:` through `mass9:`.
  Attaching the image doesn't launch its ELF; loading the ELF itself from `mass:` needs
  a homebrew launcher such as wLaunchELF. See PCSX2's
  [usb-msd.cpp](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/USB/usb-msd/usb-msd.cpp)
  and [USB.cpp](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/USB/USB.cpp).
- **Creating a USB image on macOS:** `hdiutil create -size 256m -fs 'MS-DOS FAT32'
  -layout MBRSPUD -volname Q2PS2DEBUG -srcfolder <package-folder> -format UDTO
  -nospotlight <output>.cdr`, then rename `.cdr` to `.img`. Despite the UDTO format's
  "DVD/CD master" name, the explicit filesystem/layout produce raw MBR/FAT32 sectors,
  without a DMG trailer or an ISO9660 filesystem. The 3.20 debug package produced a
  256 MiB image with `quake2.elf` and `baseq2/` at the root; its partition/boot signatures,
  FAT copies, and SHA-256 hashes of all four packaged files were verified by reading the
  FAT32 image directly. Launch `mass:/quake2.elf` in uLaunchELF. The user verified this
  on 2026-10-07; the log reports `mass0:/baseq2` ready after 600 ms and reads from its pak.
  Initial debug-run loading-screen summaries report 120-131 seconds. These are instrumented
  observations from one session, not a storage-throughput benchmark. Its emulator log was
  preserved as `github_rel_3.20/q2ps2_debug_3.20_usb-emulog.txt` before another launch could
  overwrite it. Keep build, map, config, and logging settings matched in comparisons.
- **Reusable test USB image:** `python3 src/tools/scripts/make_usb_image.py` packages the
  existing stripped debug/release ELFs at the volume root as `quake2_debug.elf` and
  `quake2_release.elf`, beside `baseq2/`; unstripped ELFs are omitted. It copies the working
  data/config except `baseq2/pak0/` when `pak0.pak` is present, and includes only `.adp`
  files under `baseq2/music/` (case-insensitive, including subdirectories). macOS `hdiutil`
  produces an auto-sized writable MBR/FAT32 image with volume name `Q2PS2` at
  `build/quake2-usb.img`; `--output`, `--size-mib`, and `--force` override defaults.
  The initial full-data run produced an 832 MiB image: raw FAT32 verification checked all
  96 included files by SHA-256, with no missing/extra files or directories. Sixteen fixture
  cases cover selection, sizing, errors and safe replacement. Both ELFs now sit next to
  the shared data, requiring no folder fallback. Close PCSX2 before replacing an image.
- **HDD boot:** the current source embeds DEV9/ATAD/APA/PFS and probes HDD between `host:`
  and USB. ELFs packaged before HDD support need rebuilding.
  Enable `[DEV9/Hdd] HddEnable = true` and set `HddFile` to a PCSX2 HDD image. Create and
  format a new blank image with uLaunchELF's HDD Manager, create a main PFS partition
  (e.g. `+Q2PS2`, 512 MiB), and copy the new ELF and `baseq2/` side by side into it.
  With `HostFs = false`, launch it through uLaunchELF. The game resets the IOP and mounts
  PFS itself: the launcher mount does not survive. Successful bring-up logs
  `game data on pfs0:/.../baseq2 (HDD partition hdd0:+Q2PS2)` and keeps that mount alive.
  Canonical `hdd0:+Q2PS2:pfs:/dir/quake2.elf` and browser `hdd0:/+Q2PS2/dir/quake2.elf`
  paths retain the launch partition; bare `pfsN:` paths fall back to enumeration.
  The launch partition's folder/root comes first; other main PFS partitions are searched
  for the same folder before any roots. Missing HDD/drivers or no matching data falls
  through to USB. The user verified emulated PFS boot/reads on 2026-10-07 and observed
  noticeably faster loading than USB. The missing HDD log still needs session-specific
  verification after adding HDD-only `fileXioSync` after log closes; see the PFS
  metadata-flush trap in [ps2-platform.md](ps2-platform.md).
- **Storage selection runtime harness:** include `iop_boot.cpp` with stub SDK headers
  and IOP/mount calls, then run from a directory holding folders literally named `pfs0:`,
  `mass0:`, `mass1:`... (macOS allows the colon). `fopen` then takes the probe paths as
  relative ones. The HDD implementation passed 84 cases under ASan/UBSan: launch-path
  parsing/bounds, host/HDD/USB precedence, partition filtering and mount lifetime,
  optional HDD failure fallback, mandatory initialization failures, and keyboard USB
  startup after HDD boot. `SyncGameDataDevice()` tests cover no-op before boot/host/USB/
  failed HDD, selected-mount sync and raw errors, and clearing stale state on another
  detection. This verifies selection logic, not the real drivers or DMA;
  `make` and `make release` remain the target compile/link checks.
- **HDD log sync runtime harness:** include `debug/log_file.cpp` with fake SDK headers,
  real host-backed open/seek/write/close wrappers and a mocked `SyncGameDataDevice()`;
  the boot harness checks its real fileXio implementation. Twenty-four ASan/UBSan cases
  verify header/append/fatal persistence ordering, no sync for host/USB or disabled
  logging, and disabling after open/seek/write/
  partial-write/close/sync failures. Every opened descriptor closes before HDD sync, even
  after a failed operation; an open failure never syncs. Real PFS persistence still needs
  an emulator run with the new ELF, checking `quake2.log` beside `baseq2/` after shutdown.

## Crash triage

- Debug builds print an EE exception report (cause, EPC, BadVAddr, stack). Resolve addresses
  against **the same build's** `quake2_unstripped.elf`:
  `mips64r5900el-ps2-elf-addr2line -f -C -e build/debug/quake2_unstripped.elf <addr>` or
  `build/tools/symbolize < emulog.txt`.
- **The handler can't print from where it runs.** libeedebug calls it at exception level (EXL
  set, its own stack). No SIF RPC can complete there, and newlib's `printf` faults: its state
  pointer reads null. The old handler died that way: a burst of `TLB Miss` at 0x0-0x68 inside
  `printf`, `_vfprintf_r` and `__retarget_lock_acquire_recursive`, and no report. Now the
  handler only copies the frame's registers. It then points the frame's EPC and `$sp` at
  `ReportCrash`, which has its own 16 KB stack, and returns. libeedebug's `_ee_load_frame` +
  `eret` resumes the faulting thread there as ordinary thread code, which writes the log, the
  screen and stdout. Keep anything that needs the IOP out of `OnException`.
- To test the handler, add a temporary `teq $zero, $zero` (an unconditional trap, cause 13).
  `move $sp, $zero` before it gives a smashed stack. A null store or load won't do: PCSX2 logs
  `TLB Miss ... [store]` and skips the access without raising the exception. For the same
  reason the reporter's retry path (a fault while it walks the stack) can't be exercised in
  PCSX2.
- **Known flake, never game code:** a `TLB Miss` in `_request_end` (ps2sdk `sifrpc.c`,
  `SIF_CMD_RPC_END`) during `host:` file I/O, usually right after a
  `PackFile: host:/baseq2/pak0.pak` line. The signature is one to three
  `TLB Miss, pc=<same> addr=0x10|0x18 [load|store]` lines: `cd->hdr.pkt_addr` is already null,
  one RPC completed twice under PCSX2's faked IOP HostFs. A second face of it is a wild `pc`
  below `.text` (0x100000), e.g. `pc=0x82000 addr=0x0`, which is usually fatal. The pc moves
  per build, so resolve it:
  ```sh
  mips64r5900el-ps2-elf-objdump -d build/debug/quake2_unstripped.elf > q2.dis
  L=$(grep -n "^  1ba87c:" q2.dis | cut -d: -f1)
  awk -v n="$L" 'NR<=n && /^[0-9a-f]+ </ {f=$0} NR==n {print f}' q2.dis | c++filt
  ```
  **Re-run before investigating.** Across three identical 39-map cycles, one died, one was
  clean, and one logged it and still finished. The renderer can't cause it: VIF1 chains only
  read RAM. A capture from a run that logged it is still valid.

## Host harnesses (runtime logic only; `make` stays the compile check)

- **Renderer sources:** in the scratchpad, make a `fakeinc/` with four headers and compile
  with `-Ifakeinc -I<repo>/src` (quoted includes miss the source's own dir, so `-I` order
  decides): `ps2/common.h` (`MAX_QPATH`, `PS2_QUAKE_DEBUG`, `Com_Printf`/`Com_DPrintf`/
  `Sys_Error` decls, `PS2_Assert`/`PS2_AssertMsg`), `tamtypes.h`, `gs_psm.h` (copy the
  `GS_PSM_*` values), `draw_buffers.h` (a `texbuffer_t`). Don't fake `texture.h`/`vram.h`.
  `#include` the **.cpp** so the test can walk anonymous-namespace statics. Stub `Sys_Error`
  as a counter (fatal paths `return` after it) and make the assert stub `exit(1)`. Use clang
  with ASan/UBSan.
- **Client-side sources** (e.g. input/rumble.cpp) compile against the *real* `client/client.h`
  chain on host clang in C++ mode. Make the fake `ps2/common.h` wrap
  `#include "common/q_common.h"` in `extern "C"`, define the globals the source touches, and
  fake only the hardware class.
- **Emulate the EE FPU** in any math harness: `1/sqrt(0)` must give FLT_MAX, not inf (see
  [ps2-platform.md](ps2-platform.md)). Otherwise target-only bugs won't reproduce.
- **ASan can't see an overrun that stays inside one object** (a member array reading into
  the next member). Heap-allocate the object under test, so writes past its last member hit
  the redzone, and poison what must not be read with `ASAN_POISON_MEMORY_REGION` from
  `<sanitizer/asan_interface.h>` (it handles a partial first granule), unpoisoning after the
  call. Give outputs exact-size heap buffers with canaries. Then prove the harness by seeding
  defects into a shadow copy of the header (`-I<mutant dir>` first): the `half_band.h`
  harness caught 7 of 7 (off-by-ones, an over-long memmove, a wrong tap, a 32-bit overflow).
- Code with EE/VU0 inline asm can't run on the host. Use the standalone test ELF recipe in
  [performance.md](performance.md).
