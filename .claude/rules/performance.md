# Performance: Quake II PS2 codegen and PCSX2 reference

The named functions, object paths and measurements below come from `quake2-ps2`.
Treat them as test ideas and hardware clues. Doom's first hardware frame log is the mars_city1
tour (below); there is no campaign baseline yet. Quake timings below are not Doom measurements.

## EE codegen facts

- **Count memory ops, not instructions.** ALU is the cheap half of this machine. A version
  that ran 9 *more* instructions per triangle but did fewer loads and stores was faster.
- **`-fno-strict-aliasing` makes gcc spill and reload pointers around stores.** A local
  `__restrict` copy *at the use site* fixes it
  (`vu1::LerpVertexBytes * const __restrict p = tri.pos;`). `__restrict` on struct/class
  members produces byte-identical code, so don't bother. It isn't a blanket sweep: check the
  disassembly per site, since it pays off where register pressure is high. Declare restrict
  locals *after* any flush call in the loop body. A cursor passed by reference has its
  address taken and stays in memory, so copy it into a local first.
- **ps2sdk `packet2_add_*` costs ~5 memory ops per word** (it reloads `packet->next` around
  every store). In hot emission, take a local `qword_t * __restrict q = pkt->next`, write whole
  qwords, and store `next` back once.
- **gcc never forms `lq`/`sq` itself.** Struct copies of `alignas(16)` types, `__int128`,
  `vector_size(16)`, `mode(TI)` and aligned `__builtin_memcpy` all lower to `ld`/`sd` pairs (a
  Mat4 is a two-trip loop). Use `ps2/qwords.h` (`CopyQwords`, `StoreQword`, `CopyDrawVertex`)
  at memory-to-memory sites. A transparent lq/sq `operator=` on Vec4/Mat4 was measured and
  rejected, because asm memory operands push register-promoted locals back to memory (+56%
  instructions in `DrawAliasMD2Entity`). The reasons are recorded in `vec_mat.h`'s header.
- `__builtin_ctz/clz` are libgcc calls on the EE. A per-batch ctz cost TexChains +9%.
- **Most of `-ffast-math` is already the target's default.** The r5900 GCC inlines `sqrtf`,
  `fabsf` and `1/sqrtf` as `sqrt.s`/`abs.s`/`rsqrt.s`, and folds `x != x` to false, under plain
  `-O2` (no NaN on this FPU). `floorf`, `ceilf`, `sinf`, `cosf`, `powf` stay libm calls either way.
  Measured on the release ELF (2026-11-03): `-ffast-math` is -0.46% static instructions, -3.2% FPU
  instructions, -11% `div.s`, -14 KB `.text`; `-fno-math-errno -fno-trapping-math` alone is -0.07%.
  The win is **`-fno-signed-zeros`**, which lets GCC emit the R5900's `min.s`/`max.s` (about 440
  sites, replacing `c.lt.s`/`bc1t`/`mov.s` triples in bounds, projection and UI clipping code:
  `idRenderMatrix::ProjectedBounds` 317→151, `ps2::ui::MapRect` 222→133). `-fassociative-math`
  and `-freciprocal-math` add the remaining 0.04%. The Makefile's `MATHFLAGS` adopts
  `-fno-math-errno -fno-trapping-math -fno-signed-zeros` in both configurations (2026-11-03);
  reassociation and reciprocal math stay off so scalar references remain bit-exact against the
  VU0 paths and the UI's 1/16 fixed-point mapping keeps its rounding. Static counts only: no
  hardware or dynamic measurement yet.

## Doom's frame log (2026-11-25)

- `ps2_framelog start [FRAMES]` records a row per Common frame (every build) and writes the run once
  to the package's `framelog.txt`; `ps2_world path FILE [FRAMES]` plays a camera path one step a
  frame, so runs compare frame for frame. `src/tools/scripts/frame_log.py log [--compare base]`
  summarises one. Columns and design: RENDERING_ARCHITECTURE.md 17.2.
- Hardware timing comes from `make BUILD=release stage-perf-world-retail` on a console (the whole
  mars_city1 tour, 7,244 frames); the gate's 240-frame tour in PCSX2 checks function only.
- Frames pipeline (2026-11-26, `ps2_pipelineFrames`): the kick that sends frame N+1 waits for chain N
  (`gpu_us`, what the EE did not cover building N+1) and for the vsync that shows N (`vsync_us`).
  `ps2_pipelineFrames 0` gives the serial frames of the first console run, for an A/B on hardware.
- **VU1 against the GS (2026-11-26):** the GPU sampler splits `gpu_us` into `vu_us`, `gs_us`,
  `direct_us`, `vif_us` and `tail_us`, from VPU-STAT, VIF1_STAT and GIF_STAT read every pass of
  `dma::WaitIdle` (`dma::GpuState`). It sees whole chains only with serial frames. `ps2_world fill off`
  scissors every pass to one pixel: a serial run's chain time against it is the GS's pixel work.
  The perf package plays the tour pipelined, serial and serial without fill in one boot. PCSX2
  puts everything in `vif_us`.
- **Instrumentation defines:** `PS2_D3BFG_FRAME_LOG` and `PS2_D3BFG_GPU_SAMPLER`, on in every build
  for now (`INSTRUMENT_DEFS`). New timing or profiling code goes behind them, with no-op stubs where
  callers would need `#if`, so a planned Profile build keeps it and Release drops it.
- **First console run (2026-11-26, NTSC 640 32/32, PORT_STATUS "First console frame log"):** the tour
  took 199 s from the HDD and 193 s from USB (36 to 37 fps mean, 62% of frames at one field, 12% at
  three or more). The HDD and USB logs (`build/framelog_{hdd,usb}.txt`) are the baselines for
  `--compare`.
  - A mid-pass eviction cost about 285 µs of EE time (`FurthestUse`'s rescans), about half the
    view time; since fixed (the next uses are kept as the cursor advances: PCSX2's PAL gate tour
    builds its views in 28% less time with the same evictions).
  - One-field frames waited 6.5 ms for VU1 and the GS.
  - The loader read 706 KiB/s from the HDD and 248 KiB/s from USB, in 8.4 KB reads.
- **Second console run (2026-11-26, PORT_STATUS "Second console frame log"):** with the four fixes the
  pipelined tour takes 149 s on either drive (79% of frames at one field, 2% at three or more). Its
  logs, copied to `build/console-2026-11-26/` (restaging the package clears them), are the new
  baselines.
  - The chain is VU1's: 75.5% of serial chain time. The GS's pixel work is 1.5 ms a frame.
  - VU1 runs 179 cycles a vertex drawn, against `vu_cost.py`'s 52 to 59 on the strip path; the gap
    depends on the view (the list path and the clipper, which the model never runs).
  - The worst frames are EE-bound: about 10 µs a batch and 105 µs a mid-pass eviction.
  - Pair a serial log with the pipelined one of the same build (the tour plays frame for frame) to
    tell a chain-bound frame from an EE-bound one.
- **Fit a stage's column against the counters** (least squares over the rows) to price a cost:
  it found the eviction cost at R² 0.989. Stalls spaced at a fixed interval point at a periodic
  call: the keyboard's 250 ms poll waits for the loader's read in flight, since both are fileXio
  calls.

## What a PCSX2 capture can and can't show

- **No EE cache emulation** (`EnableEECache = false`). Cache-locality work (prefetch, slimmer
  structs, UCAB/uncached buffers, reordering) reads as zero. Don't conclude it's worthless.
  To test it, turn the ini flag on (slow, edit with PCSX2 closed) or use hardware.
- **No GS-internal cost.** CLUT loads, fill rate, texture cache and overdraw are free. Only
  data moved (DMA/GIF qwords) and EE/VU1 instructions are charged. For GS-side changes,
  promise "no regression", never a measurable win.
- **PCSX2 charges roughly per instruction** (~2.5-3 cycles per EE instruction seemed to
  apply). Only a net instruction-count cut shows up. An MMI change that swapped 5 memory ops
  for 1 extra instruction measured *slower* (+3.1%) and was reverted, even though it should
  win on hardware.
- Same build, same capture: rendering stages reproduce within ~0.2 µs/frame, and demo1 ends on
  frame 4183 of 7911 every run. SndMix (~1 µs), Frame (~1 µs) and VSync (~6 µs) wander between
  identical runs. **Code layout alone shifts stages by ~1-2 µs** (0.5 KB of init-only code
  moved Particles by -1.9%). To attribute a delta under ~2%, compare against a same-build
  re-run, and if needed against a variant with HEAD's section sizes (`objdump -h`).
- Quote per-item ratios (e.g. EntGeom per triangle), not the EE mean. Run-to-run spread on
  the EE mean is about ±2%.
- PCSX2 timing ≠ hardware for I/O and FPU latency either.

## Codegen A/B across the backend (no tree edits)

- Baseline: `make release`, then snapshot `build/release/src/ps2/**/*.o` to the scratchpad.
- Take the exact compile line from `make -n <obj>` (or `make -n -W <file>`).
  `SIZE_OPT_CXX_SRC` and `ENGINE_SIZE_OPT_CXX_SRC` objects get **`-Os` per object**, so
  one ad-hoc `-O` level for every object shows dozens of spurious diffs. Always build a **control** set with unmodified headers first and require
  zero diffs against the baseline.
- To shadow a header, put the modified copy at `<dir>/ps2/math/vec_mat.h` and pass `-I<dir>`
  *before* `-Isrc`.
- To list every copy/assign site of a type, temporarily `T & operator=(const T &) = delete;` in
  the shadow and compile everything with `-fmax-errors=0`.
- Compare per function (instructions, loads, stores, lq/sq), normalizing branch targets.
  Static counts understate loops, so check loop bodies by hand.

## Proving an EE/VU0 asm rewrite on target (~6 s)

All of this happens in the scratchpad, with nothing added to the repo.

- The test `.cpp` includes the real header plus the HEAD version of the changed functions
  (`git show HEAD:<file>`) inside `namespace ref { ... }`. Qualify ref-internal calls
  (`ref::Foo`), since ADL on shared argument types makes them ambiguous.
- Use deterministic xorshift inputs, `memcmp` every output byte, and print `Tag: PASS/FAIL`.
  Call `SifInitRpc(0)` before `printf`, then `SleepThread()`.
- Build with the release flags. Link:
  `mips64r5900el-ps2-elf-g++ -T$PS2SDK/ee/startup/linkfile -O3 -o t.elf t.o <objs> -L$PS2SDK/ee/lib -Wl,-zmax-page-size=128 -lkernel`
  (add `-lgraph -ldma` if needed).
- Back up emulog, run `PCSX2 -batch -elf t.elf` in a background shell with an `until grep`
  loop on the log (the verdict, or `TLB Miss|EXCEPTION`, plus a deadline), then `kill` the PID
  you started, since SleepThread never exits. Restore emulog.
- The same harness measured vsync timing: an NTSC field is 9609.6 T2 ticks = 59.94 Hz
  (`gs::PresentClock`).
