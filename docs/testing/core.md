# Core test image procedures

Detailed acceptance for the core and platform test images (`make build-core`, `build-platform`,
`build-script`) and `make test-host`. The shared rules are in
[testing-pcsx2.md](../../.claude/rules/testing-pcsx2.md).

## Renderer

- `make test-renderer` / `make BUILD=release test-renderer` use the existing core test
  executable for the integrated GS/native-adapter scenes in NTSC/PAL, then (debug only) seventeen expected-fatal probes.
  They do not initialize real Console/Common presentation or link fixtures into production. Fourteen GS
  markers require 120 presented frames, alternating contexts, a reload with smaller
  command halves, texture dirty/eviction hazards, capture recovery, pressure/drains and
  exact tagged cleanup. GIF/oversized-DIRECT bounds, a deliberately stalled deadline,
  and active unsupported UI state, rectangle, invalid quad and oversized text are render-time
  assertions: fatal in assertion builds, compiled out of release, which runs only the two scenes. Five mask probes reject a second non-convex (depth) mask,
  scope leaks, unknown keys, active-count disagreement and a mask beyond the 256 replayed
  triangles. Adapter startup also rejects insufficient VRAM/pixel budgets
  without retaining partial ownership.
  Indexed geometry adds oversized single-request, invalid local-index, nonzero-depth,
  nonfinite half-UV and additive-color failures. Run both positive standards and all
  seventeen failures sequentially: nineteen isolated emulator sessions in debug, two in release.
- `make run-renderer` holds the completed NTSC picture for 60 seconds; PCSX2's configured
  F8 saves a framebuffer screenshot. `--scenario renderer-pal --visual-wait 60 --timeout 90`
  selects PAL when invoking the runner directly with the matching `--elf`. The native
  ready file is consumed only after its closed-file log marker, and the runner releases
  the hold through `renderer.resume`. Archives flag pixel inspection as required even
  when all native/log checks pass. Native `renderer*.tga` files now stream completed
  framebuffers as checked top-down 24-bit data. The runner rejects malformed dimensions,
  header/length, 27 authored scene pixels (including bilinear interpolation and additive
  texture/vertex alpha multiplication), split-upload
  quadrants and pressure pixels; captures before/after recovery must have identical hashes.
  `renderer-ui*.tga` adds 33 glyph/cursor/color/alpha/fractional/clip comparisons, including
  additive sprites, overlapping sums, saturation, zero alpha and restored normal blending, with
  identical primary/reload images and 120/four native adapter frames.
  `renderer-indexed*.tga` adds 27 rotated/clipped glyph, solid-shape, Gouraud RGB/alpha,
  submission-order and pool-pressure pixel checks. Require 120/four frames, identical
  primary/reload captures, all three CPU pool limits reached, command drains and exact
  ledger cleanup. This extends the existing integrated fixture; no new fixture is added.
  Its pass band adds 21 more: fog-colored additive terms (one pass and two), half alpha,
  subtraction and mode 8; per-channel screen at full and half alpha; a concave L in the
  depth mask, its notch, the cleared mask and a one-line mask; replace, vertical,
  half-alpha, mode-8 and Gouraud lines. Expected values model every pass's five-bit
  storage (gs-renderer.md); x multiples of 5 and y multiples of 15 are whole pixels in
  both standards.
  A timeout-injected partial file is never accepted. Preserve images/hashes and record inspection.
  Acceptance run identities and measured budgets are in PORT_STATUS; no physical PS2
  or cache-coherency claim.

## Host tests

- `make test-host` builds the texture converter and runs its library tests (glyph sets
  included), the target GSTX admission against converter output, the catalog reader
  against valid and mutated glyph tables and Python tests that drive the converter on
  synthetic containers. It also runs the real shared page-cache implementation under ASan/UBSan
  with assertions enabled and disabled. Only GS/texture submission is stubbed. Keep
  cross-image keys, LRU pressure, bad-read preservation, detach/reopen identity, fixed
  allocation ledgers, partial-init cleanup and live-image shutdown guards covered.
  These checks supplement strict EE compilation and native captures.

## Platform, core smoke, script and Common

- `make test-platform` stages an isolated
  manifest, ELF/symbols, logs and result under `build/test-results/<UTC-run-id>/`.
  Use `--scenario core` for the foundation bootstrap. Build the matching ELF before
  each scenario.
- The platform probe passed on 2026-10-08; archived run
  `20261008T070310Z_smoke_88ff01f3485548c6` checks timer progress/conversion,
  64-byte static/stack buffer alignment, scalar vector math and the EE FPU's finite
  divide-by-zero behavior. This does not establish EE cache coherency or GS timing.
- Latest debug/release core runs passed on 2026-10-10. Run identities,
  memory measurements and the precise foundation boundary are recorded in
  [PORT_STATUS.md](../PORT_STATUS.md). `make test-host` also runs runner
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

- `make build-script` / `make test-script` select the isolated real script compiler and
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
  The host suite includes shared PCM/voice timing, the source inventory audit and
  filesystem sanitizer fixtures.

- Core smoke now also requires five filesystem-service markers. The runner stages
  authored directory/text/70 KiB binary fixtures and archives their hashes even in
  the missing-main-fixture scenario. Checks exercise real permanent streams, memory
  bounds and cursor/EOF, directory/extension filters, device/path rejection, write/
  append/short reads, parent directories and native driver errors.
  Removal's observed PCSX2 error is documented in [ps2-platform.md](../../.claude/rules/ps2-platform.md);
  the test verifies error propagation and side effects, not successful driver removal.

- `make test-common` / `make BUILD=release test-common` run 28 isolated probes. Seven
  stop real Common after system, idlib, commands, cvars, filesystem, jobs or session;
  acceptance requires exactly those startup stages, reverse shutdown, idempotent cleanup
  and exact tagged-ledger recovery. Static CVar registration is one-shot, so each stop
  uses a fresh process. Twenty-one negative probes (the `NEGATIVE_PROBES` entries but the
  seventeen GS failures, which `test-renderer` runs) require matching run/begin identities
  and the expected fatal diagnostic: online flags, absent user, loading out of order,
  unsupported input device, audio missing WAVs, unloaded duration queries, invalid WAV
  formats/truncation/chunk bounds/payload budgets; absent/backward voice clocks, unloaded
  voice samples, negative seeks, invalid pitch, pinned sample mutation, double/foreign
  free, channel mismatch and unknown sound flags; and negative duration input. The probes that only confirmed a deferred capability fails by name (input
  sampling, multiplayer, save manager, network matchmaking, Classic switching, audio
  device, process launch, source-model import and the renderer's world/image/vertex/
  shader/cinematic/demo entry points) were retired on 2026-10-26; those stubs still fail
  by method name.
  The first fatal line must match the expected diagnostic with a name boundary, and later
  messages cannot hide a wrong initial fatal. A watchdog, TLB/bus error, unexpected return
  or unrelated fatal fails.
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
  submission fail by method name.

- Three required `deferred/` markers check empty native multiplayer lifecycle and ledger
  recovery, save-description copy/clear ownership with warmed native dictionary pools,
  and the read-only disabled save cvar. These do not execute a game, shell or save
  pipeline. Shell vtable and retry-dialog providers currently have compile/link acceptance.

- Three required `input/` markers check the full native action table and exact lookup
  (including case and impulse-prefix collisions), repeated empty cleanup and native
  invalid-index results, and read-only controller/rumble preferences. The input provider
  acquires no devices or sampling buffers and publishes no player commands.

- The additional GS failure probes cover assertion-build GIF bounds, oversized DIRECT
  reservations and a stalled deadline; they do not claim Common or UI startup.

- The production link (`make`, `make release`) is the retained compile/link gate: all 341 production inputs,
  every required game/class/cvar registration root and map input are retained without
  unresolved symbols, duplicates, Classic imports or GC. The campaign test entry separately requires
  an explicit authored fixture manifest. Do not reuse an older resident ELF after a failed attempt.
  The default runnable ELF is the production Common loop; the runner uses only test images.
  Nine host regressions reject test inputs/headers/symbols, stale images, nonzero links, missing registrations/map
  inputs, non-executables and ignored undefined diagnostics.
