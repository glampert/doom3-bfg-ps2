# Performance: Quake II PS2 codegen and PCSX2 reference

The named functions, object paths and measurements below come from `quake2-ps2`.
Treat them as test ideas and hardware clues; this port has no renderer or timing baseline yet.

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
  `SIZE_OPT_CXX_SRC` objects get **`-Os` per object**, and an ad-hoc `-O3` shows dozens of
  spurious diffs. Always build a **control** set with unmodified headers first and require
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
