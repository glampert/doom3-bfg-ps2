# Testing: PCSX2 sessions, runners and triage

The rules every emulator or host test task needs. Each gate's exact acceptance (markers,
pixel probes, counters, ledgers, failure cases) is in `docs/testing/`; read the file for
the gate you are changing or diagnosing:

| Gate | Detail |
| --- | --- |
| `test-renderer`, `run-renderer`, `test-host`, `test-platform`, `test-core`, `test-script`, `test-common` | [core.md](../../docs/testing/core.md) |
| `test-headless`, `test-headless-retail`, `test-ui` (every step), input devices | [production.md](../../docs/testing/production.md) |
| `test-game`, `test-map` | [game.md](../../docs/testing/game.md) |
| Quake II reference: PCSX2 storage (USB/HDD images), full crash triage, host harness recipes | [quake2-reference.md](../../docs/testing/quake2-reference.md) |

Run IDs, captures and measured budgets are in [PORT_STATUS](../../docs/PORT_STATUS.md);
memory figures in [memory-budget.md](memory-budget.md).

## Images and runs

- `make` / `make release` build production, without test code. Test images are built
  explicitly (`make build-core`, `build-platform`, `build-game`, `build-script`), all named
  `d3bfg_tests.elf` under `build/<config>/tests/<image>/`. Match `--elf` and `--symbols`
  when passing a nondefault configuration to `run_pcsx2_test.py`.
- `PS2_D3BFG_TESTS` is the sole test-enable define. Make's `CORE_BOOT` selector picks the
  real core stage or the SDK-only skip provider through source lists; build and run
  platform/core variants separately so archived flag stamps match.
- Run emulator gates sequentially, one PCSX2 at a time, in debug and, when asked, with
  `BUILD=release` (assertions off: load-time validation must still fail; render-time checks are
  assertions, so their expected-fatal probes run in debug only). `make test-ui`
  takes about 8 minutes per standard: say so before starting a long session.
- Runners stage an isolated archive under `build/test-results/<UTC-run-id>/` (manifest,
  matched ELF/symbols/map, inputs and their hashes, both PCSX2 streams, `summary.json`).
  They refuse an already-running PCSX2, check HostFs and IOP/file logging read-only, use
  `-logfile` instead of the normal emulog, and stop their own process at completion or
  timeout.
- A pass needs the matching run identity, every required marker and the result/summary.
  Crashes, TLB/bus errors, an unrelated first fatal, watchdog expiry or a nonzero emulator
  exit fail even if PASS was printed. Negative cases require their specific first fatal.
  Count native markers from one stream (PCSX2 mirrors IOP lines into stdout and emulog)
  while checking both for crashes.
- HostFs `console.txt` lines are logged on submission, before `wait` delays execute: only
  actual native `echo` output is a barrier. A key or command issued in the frame of a
  `ps2_capture` runs before that frame draws; put `wait 1` between them.
- Inspect captures as well as logs. Pixel checks model the PSMCT16 frame (five bits per
  channel) and allow eight levels per channel. PCSX2 models neither the EE cache nor GS
  internal cost: captures prove function, not hardware timing or coherency.
- Closing the PCSX2 window ends the VM with status 0; the runner reports "emulator exited
  with status 0 after N s". That is an interrupted run, not a target fault.

## PCSX2 setup and logs

- App: `/Applications/PCSX2.app/Contents/MacOS/PCSX2`, launched as `PCSX2 -batch -elf <elf>`;
  `host:` maps to the ELF's directory. Override it with `PCSX2=`.
- `~/Library/Application Support/PCSX2/inis/PCSX2.ini` needs `[EmuCore] HostFs = true`,
  `[Logging] EnableIOPConsole = true` (EE stdout goes through IOP fio) and
  `EnableFileLogging = true`. **Edit it only while PCSX2 is closed**; it rewrites the file
  on exit. Every launch overwrites `logs/emulog.txt`.
- `[USB1] Type = hidkbd` attaches the host keyboard (reported as JIS; it sends HID `0x34`
  for `` ` ``). Its `Missing host mapping for QKey` warnings are harmless.
- PCSX2 2.6.3 exits with status 1 for `-version` and `-help`; the runner accepts that for
  the version query only.
- On 2026-10-18 PCSX2 trapped with host SIGILL while handling the runner's SIGINT after a
  clean shutdown (recursive `os_unfair_lock` in malloc from Qt's signal handler). That
  host fault is separate from the EE flake below. Keep strict exit checks and triage
  before retrying; completion markers cannot turn a crashed process into a pass.

## Crash triage

- Resolve addresses against the same build's unstripped ELF:
  `mips64r5900el-ps2-elf-addr2line -f -C -e build/<config>/d3bfg_unstripped.elf <addr>`
  (or the test image's `d3bfg_tests_unstripped.elf`).
- **Known flake, never game code:** one to three `TLB Miss, pc=<same> addr=0x10|0x18`
  lines in `_request_end` (ps2sdk `sifrpc.c`) during `host:` I/O, or a wild `pc` below
  `.text`. One HostFs RPC completed twice. **Re-run before investigating.** Details and
  the disassembly recipe are in [quake2-reference.md](../../docs/testing/quake2-reference.md#crash-triage).
- A null load/store in PCSX2 logs `TLB Miss ... [store]` and skips the access without
  raising an exception; use `teq $zero, $zero` to test an exception path.

## Host harnesses

- Host tests check runtime logic only; target compilation (`make`) stays the compile check.
- Emulate the EE FPU in math harnesses: divide by zero and `1/sqrt(0)` give `FLT_MAX`, not
  infinity; there is no NaN. Otherwise target-only bugs do not reproduce.
- ASan cannot see an overrun that stays inside one object. Heap-allocate the object under
  test, poison what must not be read, give outputs exact-size buffers, and prove the
  harness by seeding defects.
- Code with EE/VU0 inline asm cannot run on the host; use the standalone test-ELF recipe
  in [performance.md](performance.md).
