# VU microprograms and VU0 inline asm

Doom's VU1 programs are VUSL, a GLSL-like language for VU1, since 2026-11-21 (VCL before):
`passthrough.vusl`, `world_lm.vusl` (cooked D3WD world batches, indexed strips) and
`world_strips.vusl` (plain-strips cooks, for the topology comparison), and the clipper
library `clipper.vusl` both world programs call. They share `vu_memory.vush` (the data memory
map), `world.vush` (the output windows) and `clipper.vush` (the library's interface). The world
programs and the clipper came from quake2-ps2's by way of this port's VCL versions; the "VU
memory layout (vu1.h)" section and the named VU0 asm sites below are quake2-ps2's, and the
hardware/toolchain behaviour transfers.

## Pipeline (Makefile rule `build/vu/%.o`)

`oxide-vusl --warnings-as-errors --header=build/vu/<name>.vusl.h` → `dvp-as`, then a `.d` for the
link reports. oxide-vusl comes from the `src/external/oxide-vcl` submodule, built by cargo into
`build/tools/oxide-vcl/`; it compiles VUSL to VCL and hands that to oxide-vcl in oxide mode with
every optimization. Its docs, there: `docs/VUSL.md` (the language, the cost of each construct,
libraries, the header) and `docs/VUSL_COMPILER.md`. The output is config-independent and lives in
`build/vu/`: the `.vsm`, the `.o` and the header; `make vu` builds it alone. The programs are
listed in `config/sources.mk` (`VUSL_FILES`). The code lands in `.vutext` (the `.vu` directive's
section); `.vudata` is empty.

- **Check `make`'s exit status.** oxide-vusl writes all its files or none, but a failed VU
  build leaves the previous `.vsm`/`.o` in place. Messages read `file(line): error: ...` with
  the source line and a caret.
- **The header is the binding.** `ps2/renderer/vu.h` includes the four `<name>.vusl.h` and
  takes VU1 data memory's addresses from them: the frame constants, the double buffer (BASE,
  OFFSET), the batch state's offsets, the windows, and the clipper's micro address.
  `vu::FrameConstants` is the generated `Frame`, so `vu_memory.vush` names its members; the
  `static_assert`s there check that every program agrees on each address. A layout changed in a
  `.vush` fails the build, not the screen. `-Ibuild/vu` is on every EE compile, and the EE
  objects wait for the headers on a clean build (an order-only prerequisite); `-MD` tracks them
  after.
- `--warnings-as-errors` covers the preprocessor and the compiler alike, an included function no
  program calls among them (hence the three includes: each file holds what its includers call).
  The notes oxide-vusl prints for values kept in the work area (`'ixtop_0' does not fit in the
  VI registers`) are not warnings.
- `--emit-vcl=FILE` writes the generated VCL, for reading what a construct became:
  `build/tools/oxide-vcl/release/oxide-vusl --emit-vcl=/tmp/t.vcl -o /tmp/t.vsm src/ps2/renderer/vu1progs/world_lm.vusl`.
  world_lm takes about 10 seconds to compile, the others a second or less.
- dvp-as's **"operand out of range" warning on a branch is harmless**: offsets are 11 bits and
  dvp-as keeps the low ones, which reach the same instruction because VU1 micro memory is exactly
  2048 instructions and the PC wraps.

## The clipper library

`clipper.vusl` is compiled once (433 instructions, its entry table included) and resident at
micro address 1536, the `#pragma library` address in `clipper.vush`; the world programs reach it
with `iaddiu` and `jalr`, so they stay relocatable in the swap region. `vu::Init` measures it and
builds its MPG chain; `vu::Bind` returns that chain (`Binding::library`) with the first program
that calls it, after Init or `vu::Invalidate`, and `DisplayList::SetProgram` CALLs it before the
program's own. `vu::Stats::libraryUploads` counts those uploads apart from the programs'. The swap
region ends at 1536 (2010-2047 stay unused).

- **The calling convention:** arguments and results travel in VF24-VF31 and VI10-VI14, and a
  call may change those, VI15 (the link) and VI01. The clipper also works in VF16-VF23 and
  VI06-VI09, which it saves at 1008-1016 and restores, and spills to 1017-1023.
- **A call costs a caller its registers.** What a loop keeps live across the call must fit
  VI02-VI09. oxide-vcl gives those values their registers first, and saves the ones that do not
  fit only across the calls: stored when written and loaded back after each call, in their
  registers everywhere else. `world_lm` saves the XTOP base, its list walk's counters and its
  window state, `world_strips` the XTOP base and its flag masks (0x8000, 0x4000); the strip paths,
  which never call, keep them all in registers. The build notes each one ("crosses a library
  call"); check a new program's notes after adding a call.
- **The value cache forgets memory at a call**, `uniform` memory apart: a value read before the
  call is read again after it.
- Without the library each world program inlines its own clipper: `world_lm` would be 1,520
  instructions, which leaves the swap region no room for `world_strips`.

## Writing VUSL for these programs

Measured with `vu_cost.py` over mars_city1 (2026-11-21), against the hand-written VCL these
programs replaced: the same kicks and vertices; `world_lm` 63.7 cycles a triangle for the base
pass (VCL 64.2, −0.8%) and 90.3 with the lightmap pass (92.4, −2.3%), `world_strips` 60.2
(57.1, +5.3%). oxide-vcl's programs suite ran both on its scenes in its VU model, with the
packets of the VCL programs and the interpreter's memory. What moved them, each from its own
A/B:

- **Keep a hot loop one block.** oxide-vcl's `loops` pipelines only a loop that is one block. A
  room test per vertex splits the strip copy: `world_strips` computes each window's run,
  `min(left, room)`, and copies that many with no test (74.8 to 72.1 cycles a triangle).
- **Not every rewrite pays.** The same run-length window in `world_lm`'s strip pass cost its
  lightmap pass 104.3 cycles a triangle against 95.4. Measure each one.
- The pass is a run-time argument of the clipper (`float pass`), where an inlined clipper would
  fold it.
- **Flags and Q:** `clipw()`'s flags land four cycles after it; `adc()` and `clipped()` read
  them, and the compiler requires the `clipw`s to be the last ones in the same block. A Q read
  after a divide needs no padding.

To A/B a program change, compile both versions and run `vu_cost.py --vu <dir>` over the cooked
worlds (`build/world/mars_city1.d3wd`, `build/world-strips/mars_city1.d3wd`; `--no-lightmaps`
for the base pass alone). oxide-vcl's programs suite (`crates/vusl/tests/programs.rs`, whose
`report` test prints cycles per scene) holds copies of these programs ported from this port's
VCL at `adc427d`. PCSX2 shows none of it (see [performance.md](performance.md)).

## Linking

- `#pragma program(NAME)` (or `#pragma library(NAME, ...)`) gives `NAME_CodeStart`/`NAME_CodeEnd`.
  The generated header expands `VUSL_DECLARE_PROGRAM(NAME)`, which `vu.h` defines before
  including the headers: the symbols at global scope (an `extern "C"` inside an anonymous
  namespace gets internal linkage and won't resolve against the VU object) and
  `ps2::renderer::vu::NAME_Code()`. Nothing keeps `.vutext` through `--gc-sections` but a
  reference from linked code: the core test reads the symbols, production through `vu::Init`.
- A library's entry table must branch to a local label: dvp-as leaves a branch to a global
  symbol as a relocation ld rejects ("unsupported relocation"). oxide-vusl does so.
- Draw code reaches VU1 only through `dma::DisplayList` (`dma_list.h`): `SetFrameConstants`
  writes the frame constants and the VIF1 double buffer once per frame, `SetProgram` CALLs the
  prebuilt MPG chains only when `vu::Bind` finds the program (or the clipper) unloaded, and
  `DrawBatch`/`UnpackBatch`/`EndBatches` place each MSCAL in the next tag. Every command reserves
  its own ring room. Call `vu::Invalidate` after anything else touches micro memory.
- A program that kicks its input half directly must not end before the kick drains:
  MSCAL waits for the program's E bit, not for PATH1, and the unpack after next overwrites
  the half under the GIF. `passthrough.vusl` kicks a null GIF tag (NLOOP 0, EOP 1, which
  outputs nothing) second, since an XGKICK stalls until the previous PATH1 transfer ends.

## Doom's world batches (world_lm.vusl, world_strips.vusl)

- A cooked packet ends with `ITOP N` and starts nothing: `world::Draw` puts `MSCAL` in the next
  tag's VIFcode (the next batch's REF runs it before its data reaches the other half). Keep
  the program single-exit (no `yield`); there is no MSCNT entry.
- Index entries are u16 qword offsets with ADC in bit 15, because VU1 has no integer multiply
  or shift. A vertex is four qwords in VU memory (`STCYCL 4,1`): phase 1 overwrites it in place
  (`Projected`: clip position, projected XYZ2 with the outside flag in .w, colour bytes kept,
  STQ into the fourth), and goes out as ST, RGBAQ, XYZ2 (50 vertices a window). The clipper's
  corners carry the UV before the divide (S times w), three qwords each.
- Each batch's input half ends with 11 qwords of state (`vu::kBatchLightmapOffset`, 295): the
  lightmap page's A+D packet (word 3 of 296 positive asks for the lightmap pass), or for a
  material pass's batch its colour at 295 with word 3 of 296 negative (every vertex takes that
  colour, no lightmap pass); then the material's 9 qwords (`vu::kBatchStateOffset`, 297): the
  program kicks the material's A+D packet first and opens its windows with the batch's own
  drawing tags. A list batch may kick
  no window, so it kicks the null tag before it ends: the state packets must leave the half
  first. A batch fits 295 qwords.
- **Sixteen lanes, both passes:** phase 1 stores the lightmap S x Q in qword 3 `.w` and T x Q in
  qword 0 `.w` (the clip position's w); the clipper's seed rebuilds w as 1/Q (`div` per corner) and
  blends the pass's UV by the pass float. A lit batch walks its stream again for the lightmap
  pass (ST, RGBAQ and XYZ2 like the base pass: the RGBAQ write is what stores a PACKED ST's Q).
  `world_lm` is 648 instructions and `world_strips` 370 (2026-11-21, `.vutext` of their objects),
  both in the swap region; passthrough is resident and the clipper fixed at 1536.
- The two output windows are absolute (652, 803) and keep alternating across batches through
  `Frame.window` (qword 7); `DisplayList::SetFrameConstants` resets them per frame. Never reopen
  window A per batch: the window a batch writes first must be the one drained by the previous
  kick's stall.
- XYZ2 is `ivec3 xyz` plus a separate `int16 adc` member, which compile to `sq.xyz` and `isw.w`:
  two writes to the same field may be reordered, disjoint fields cannot collide (quake2-ps2's
  EmitVertex does the same).
- **A clip w of exactly 0 loses the clipper's UVs:** the clipper rebuilds w as 1/Q, and with
  Q = 1/0 at the largest float that flushes to zero (the VU has no denormals), taking the corner's
  UV (S x Q x w) with it. A vertex in the eye's plane does this (grid-aligned cameras put whole
  wall edges there). Both world programs add 1e-20 to w after the matrix; keep it.
- **Data memory beyond the EE's:** world_lm's work area at 15-39 (qwords 13-14 are the frame
  constants' `ListStats`, the list path's counts in GPU sampler builds, `-DPS2_D3BFG_GPU_SAMPLER`
  from the Makefile's `INSTRUMENT_DEFS`; the clipper may not write its callers' memory, so world_lm
  counts around the call); the
  world programs' scratch at 954-956, right after window B; the clipper's buffers at 957-1007,
  A of 8 corners and B of 9, three qwords each; its saves and work area at 1008-1023. The buffers
  hold the true worst case only while a corner is kept exactly when its distance is >= 0: every
  distance is 0 or at least one ulp of the near distance times `kClipDistanceScale`, which is the
  1/16 the clip passes compare against from a near plane of 1/8 up (`vu::kClipMinNear`).
  `scene::ViewProjection` clamps the near plane there (assertion builds stop).
- Plain strips keep ADC (0x8000) and the outside flag (0x4000) apart in XYZ2's `.w`; the list
  path clears the word for its triangles.
- **world_lm keeps each vertex's six clip flags** in XYZ2's `.w` (`clipbits()`, oxide-vcl `dca192e`:
  FCGET masked to the newest judgement; 0 inside the guard band). The list path sends a triangle
  whole when the OR of its corners' flags is 0 and drops it when their AND is not: corners sharing
  a flag are all outside that plane (+z for everything behind the eye, since z_c is about z_e + 2n
  there while |w| = z_e), and the clipper would send nothing. Only the rest call the clipper.
- **Count VU1 cycles with `src/tools/scripts/vu_cost.py`** (PCSX2 shows none): it runs a
  program's `.vsm` on the integer side over cooked batches, one cycle per pair plus the stall the
  listing marks on it (`STALL_LATENCY ?N`), or the DIV wait at `waitq`, clip flags taken as zero
  (the strip path, which never calls the clipper). Over mars_city1 (2026-11-21): `world_lm` 63.7
  cycles a triangle for the base pass (`--no-lightmaps`) and 90.3 with the lightmap pass, plain
  strips 60.2. Only kicks of an output window count as packets. `--list-path` sends every batch
  down the list path instead (one vertex outside, the clipper call free): 33.7 cycles more an entry
  walked, which the console's fits confirm (35.8, then 31.3 with the reject). The clipper's own cost
  comes only from the fit (`frame_log.py --vu-fit`, performance.md): before the reject 1,232 cycles a
  call that sends a fan and 194 one wholly outside a plane, after it (2026-11-27) 1,008 a call and a
  rejected triangle nothing beyond its list entries.

## Writing VUSL

- Files follow the port's C++ layout: the `/* === File: Brief: === */` banner with the license
  line, braces on their own lines, every conditional body braced, aligned `=` in a block of
  assignments, and a blank line after a completed block. Each struct and function gets a comment
  saying what it is and where its memory lives.

## VU memory layout (vu1.h)

Chunks are up to 90 vertices (60 for MD2) in a double buffer, `XTOP` flips per `MSCAL`, and
there are two output windows XGKICKed alternately. The clip scratch is 956..1009, and the
light block starts at 1010. The batch tag block is 7 qwords with no room to grow (see
[gs-renderer.md](gs-renderer.md)).

## Debugging a microprogram

Read the generated `.vsm` as a hypothesis, not as evidence. Four disassembly theories failed
in a row on one bug, and two probes settled it:

- **Readout channel:** paint a value into the vertex colour (`addw.xyz vCol, vf00, <reg>`) and
  read the grey level on screen. A value that saturates the blend hides what's behind it.
- **Byte dump:** store the value to a scratch qword nothing else writes (a member of a struct
  bound at one of the free qwords), then print it from the EE with
  `ps2::debug::DumpVu1DataMemory(what, addrQw, count)` (`debug/pipeline_dump.h`, debug only),
  gated to every N frames. A probe inside a function lands in *every* inlined call, so check
  the shape of the data before trusting a sample.

## VU0 macro-mode inline asm (GCC 15)

- Syntax: `$vf4`/`$ACC` register prefixes and **no** `.xyzw` dest suffix
  (`vmulax $ACC, $vf5, $vf1`). See the `__GNUC__ > 3` branch of ps2sdk `ee/math3d/src/math3d.c`.
- GAS inserts a `nop` between `mfc1` and a reader of its GPR. Put independent work there.
- Passing a float as `"r"(bits_to_u32(t))` makes GCC `mfc1` right after the `div.s` and stall
  for the whole divide. Pass `"f"(t)` and `mfc1` inside the asm after the loads.
- One LerpTo-style asm per qword serializes on FMAC latency. Fusing N qwords into one block
  lets the subtracts issue back to back, and the results stay bit-identical.
- `ps2/math/vec_mat.h` and `ps2/qwords.h` (imported 2026-11-03) have no host fallback. The
  core gate's `math/qword-copies` and `math/vu0-*` checks (`tests/smoketests/math_tests.cpp`)
  compare them with scalar references on target; keep them passing after any edit to an asm
  block or constraint list. For a rewrite's codegen, use the test-ELF recipe in
  [performance.md](performance.md).
