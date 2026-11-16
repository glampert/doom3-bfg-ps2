# VU microprograms and VU0 inline asm

Doom's VU build (the rule, `vu.h`, `vu1progs/`) is in place since 2026-11-03: `passthrough.vcl`
and, since 2026-11-04, `world_lm.vcl` (cooked D3WD world batches) with `vu_window.i` and
`vu_clip.i` adapted from quake2-ps2; `world_strips.vcl` (2026-11-07, swap region) draws plain-strips
cooks for the topology comparison. Doom's data memory map is `vu1progs/vu_memory.h`; the
"VU memory layout (vu1.h)" section and the named VU0 asm sites below are quake2-ps2's, and the
hardware/toolchain behaviour transfers.

## Pipeline (Makefile rule `build/vu/%.o`)

`oxide-vcl -G -I vu1progs --vcl-boilerplate --warn-undef --mode=oxide --opt=all
--warnings-as-errors` → `dvp-as`, then a `.d` for the link reports. oxide-vcl is the
`src/external/oxide-vcl` submodule, built by cargo into `build/tools/oxide-vcl/`; its
`docs/VCL_REFERENCE.md` covers the language, the preprocessor and the optimizations. The output is
config-independent and lives in `build/vu/`, the `.vsm` listing beside each `.o`; `make vu` builds
it alone. Doom's programs are listed in `config/sources.mk` (`VCL_FILES`) under
`src/ps2/renderer/vu1progs/`, with the data memory map in `vu_memory.h`, shared with C++ through
`ps2/renderer/vu.h` (plain `#define`s, `#if` checks and `//` comments only, names prefixed `kVu`).
The code lands in `.vutext` (the `.vu` directive's section); `.vudata` is empty.

- **Check `make`'s exit status.** A failed VU build leaves the previous `.vsm`/`.o` in place.
  Messages read `file(line): error: ...` with the source line and a caret.
- `--warnings-as-errors` covers the preprocessor (`--warn-undef`: an `#if` on an undefined name)
  and the compiler alike.
- dvp-as's **"operand out of range" warning on a branch is harmless**: offsets are 11 bits and
  dvp-as keeps the low ones, which reach the same instruction because VU1 micro memory is exactly
  2048 instructions and the PC wraps.
- **Immediates:** oxide mode's `imm` fix rewrites or rejects an immediate that does not fit its
  field, where dvp-as would truncate it silently.
- `oxide-vcl -G -I vu1progs --vcl-boilerplate --preprocess-only prog.vcl` prints the expanded
  source; `-c` puts the source lines in the listing as comments.

## Writing fast VCL for oxide-vcl

The scheduler packs the instructions of one **block** (the code between labels, branches and
`--barrier`s) into upper/lower pairs, filling latencies with independent work. Three oxide-mode
optimizations work on whole loops (`--opt=all`): `loops` software-pipelines a loop that is one
block (iteration *k+1*'s loads and math run beside iteration *k*'s stores), `unswitch` copies a
loop once per outcome of a branch on a register the loop never writes (two such registers give
four copies), and `exit` ends a program on its last instructions. Write for those:

- **Read the listing.** Each block's header, `_LNOPT_w=[ ... ] N [...] C`, gives its
  instructions and its cycles. A pipelined loop shows `__MAIN_LOOP`/`__EPI0` labels, an unswitched
  one `__u0`/`__u1` (`__u0__u1` for the second level), and `STALL_LATENCY ?N` marks a pair the VU
  stalls on. `src/tools/scripts/vu_cost.py` counts a program's cycles over cooked batches.
- **Keep a hot loop one block.** A branch inside the body splits it and stops `loops`. Turn a test
  per iteration into a run length computed outside the loop: the strip paths compute how many
  vertices (or index qwords) the window takes, `min(left, room)`, and run that many without a
  room test. A per-element decision with a cheap slow path can often be made after the work: the
  quake2-ps2 triangle loop writes each triangle into the window before it knows whether it is
  clipped, and the clip path writes over the same slots.
- **Watch the compile time.** `loops`' schedule search grows quickly with the body: a loop of
  about 50 instructions costs seconds, a branch-free clipper pass of 70 about four minutes
  (`time make vu`). When a big loop gains little (world_lm's index-qword loops are bound by their
  11 lower-pipe instructions an entry, under 1% from pipelining), a `--barrier` inside it makes it
  two blocks, which `loops` leaves alone; say why in a comment. `--LoopCS 0,0` does not do it: it
  warns, and `--warnings-as-errors` fails the build.
- **Hold batch state in VI registers.** The allocator gives a value its real live range across
  branches and loops, and has VI01-VI15 (`--vcl-boilerplate` declares `.init_vi_all`; VI01 is
  `fcand`'s destination and stays free across it). A mode read once per batch belongs in a register:
  the world list path's pass (`iPass`) is never written by the walk, so unswitch gives each pass its
  own copy. Reloading such a value inside the loop, even from a constant header, is a write and
  keeps the branch.
- **When the integer pool runs out** (`AllocationPoolFull` on a VI web), shorten lives rather than
  spill: rebuild a constant where it is used (`iaddiu iMask, vi00, 0x7FFF`), re-read `xtop` instead
  of holding the base, derive a counter from another (world_lm's lanes left are `iLeft & 3`), and
  keep a value read once per window or pass in memory (the drawing tag's address).
- **Every register read must be written on every path** before it, even one that never runs:
  `'x.y' is read before anything writes it`. Initialise it before the loop; the message names the
  register and lane.
- **Memory order:** oxide mode keeps two accesses in order when their addresses are provably the
  same qword, the same base register (or VI00) plus the same constant through IADDIU, ISUBIU,
  IADDI or LQI/SQI steps, in a pipelined loop too; XGKICK follows every earlier store. So a load and
  a store of one address, a spill and its reload, or a window patch and its kick need no
  `--barrier`. Keep one only between two accesses to one qword through registers the compiler
  cannot relate (an `iadd` of a variable, or two separately loaded pointers).
- **No float immediates.** Build a constant once with `loi` and `addi` on a zeroed register
  (`sub fK, vf00, vf00`), never on `vf00` itself: its w is 1.0, so `addi fK, vf00, i` gives w = 1 + I,
  and `add.yw fX, vf00, fY[x]` gives w = 1 + y. Both bit this port once.
- **Flags and Q:** `fcand`/`fmand` in the block of the instruction that set the flags; the
  scheduler places the read when they land (four cycles after a `clipw`) and keeps other
  flag-setting instructions out of the way. A Q read after `div` needs no hand padding.
- **Integer immediates:** `iaddi` takes 5 bits signed, `iaddiu`/`isubiu` 15 bits unsigned; 0x8000
  takes two steps (`iaddiu r, vi00, 0x7FFF` then `+1`).
- **Before keeping a rewrite**, compare its GIF packets with the version before: the same input
  data, a PCSX2 capture or `ps2::debug::DumpVu1DataMemory` of the windows. oxide-vcl's own test
  suite runs the corpus versions of `world_lm.vcl` and `world_strips.vcl` (at `adc427d`) on scenes
  in a timed VU model (`crates/oxide-vcl/tests/scenes.rs` there), which is how this port's
  rewrites were checked: the same packets as the original vcl's, and their cycles.

## Linking

- `#vuprog NAME` → `.name NAME` → oxide-vcl emits `NAME_CodeStart`/`NAME_CodeEnd`. Declare
  those with `PS2_DECLARE_VU_MICROPROGRAM` (`ps2/renderer/vu.h`) at global scope. An
  `extern "C"` inside an anonymous namespace gets internal linkage and won't resolve against
  the VU object. Use `u32`/`unsigned int` for the symbol types, not `uint32_t`. Nothing keeps
  `.vutext` through `--gc-sections` but a reference from linked code: the core test reads
  the symbols, production will through `vu::Init`.
- Draw code reaches VU1 only through `dma::DisplayList` (`dma_list.h`): `SetFrameConstants`
  writes the shared data memory block and the VIF1 double buffer once per frame,
  `SetProgram` CALLs the prebuilt MPG chain only when `vu::Bind` (`ps2/renderer/vu.{h,cpp}`)
  finds the program unloaded, and `DrawBatch`/`UnpackBatch`/`EndBatches` place each MSCAL in
  the next tag. Every command reserves its own ring room. Call `vu::Invalidate` after
  anything else touches micro memory.
- A program that kicks its input half directly must not end before the kick drains:
  MSCAL waits for the program's E bit, not for PATH1, and the unpack after next overwrites
  the half under the GIF. `passthrough.vcl` kicks a null GIF tag (NLOOP 0, EOP 1, which
  outputs nothing) second, since an XGKICK stalls until the previous PATH1 transfer ends.
- `#define`s nest and `#macro`s may invoke each other (recursion is an error). Label-placing
  macros take one prefix and paste it (`lbl##Loop`, `lbl##NoWarp`). Constant expressions are
  folded only with `--fold-constants`, which the Makefile doesn't pass, so the compiler gets
  operands like `1010 + 0(vi00)` as written and evaluates them itself.

## Doom's world batches (world_lm.vcl, world_strips.vcl)

- A cooked packet ends with `ITOP N` and starts nothing: `world::Draw` puts `MSCAL` in the next
  tag's VIFcode (the next batch's REF runs it before its data reaches the other half). Keep
  the program single-exit; there is no MSCNT entry.
- Index entries are u16 qword offsets with ADC in bit 15, because VU1 has no integer multiply
  or shift. A vertex is four qwords in VU memory (`STCYCL 4,1`): phase 1 overwrites it in place
  (clip position, projected XYZ2 with the outside flag in .w, colour bytes kept, STQ into the
  fourth), and goes out as ST, RGBAQ, XYZ2 (50 vertices a window). The clipper's corners carry
  the UV before the divide (S times w), three qwords each.
- Each batch's input half ends with 11 qwords of state: the lightmap page's A+D packet at 295-296
  (word 3 of 296 non-zero asks for the lightmap pass), then the material's 9 qwords
  (`kVuBatchStateOffset` 297): the program kicks the material's A+D packet first and opens its
  windows with the batch's own drawing tags (`OpenBatchWindow`). A list batch may kick no window,
  so it kicks the null tag before it ends: the state packets must leave the half first. A batch
  fits 295 qwords.
- **Sixteen lanes, both passes:** phase 1 stores the lightmap S x Q in qword 3 `.w` and T x Q in
  qword 0 `.w` (the clip position's w); the clipper's seed rebuilds w as 1/Q (`div` per corner) and
  blends the pass's UV by the pass float in the walk state. A lit batch walks its stream again for
  the lightmap pass (ST, RGBAQ and XYZ2 like the base pass: the RGBAQ write is what stores a
  PACKED ST's Q). `world_lm` is 1,060 instructions and `world_strips` 782 (no lightmap pass; it
  zeroes the pass word); both world programs swap and only passthrough is resident.
- The two output windows are absolute (652, 803) and keep alternating across batches through
  qword 7; `DisplayList::SetFrameConstants` resets them per frame. Never reopen window A per batch: the
  window a batch writes first must be the one drained by the previous kick's stall.
- Store XYZ2 as `sq.xyz` plus a separate `isw.w` for ADC: two writes to the same field may be
  reordered, disjoint fields cannot collide (quake2-ps2's EmitVertex does the same).
- **A clip w of exactly 0 loses the clipper's UVs:** the clipper rebuilds w as 1/Q, and with
  Q = 1/0 at the largest float that flushes to zero (the VU has no denormals), taking the corner's
  UV (S x Q x w) with it. A vertex in the eye's plane does this (grid-aligned cameras put whole
  wall edges there). Both world programs add 1e-20 to w after the matrix (`fWBias`); keep it.
- The list path's walk state is in registers, the clipper's survivor count too (`iCount`); VU
  memory holds only what is read where it is used: the pass's drawing tag address and the pass
  (`kVuWalkPrevAddr` .y, .z), for a window open and the clipper's seed. The clip scratch is 954-1005, right
  after window B (that qword, then buffers A and B of 24 and 27 qwords, pinned by `#if`s in
  `vu_memory.h`); 1006-1023 are free. With the clipper's
  temporaries that fills the VI registers: add walk state by shortening another value's life.
- `world_strips.vcl` reads the position's ADC word with `ilw.w` before the in-place `sq` of the
  same qword; oxide mode keeps the two in order, also in the pipelined transform. Plain strips
  keep ADC (0x8000) and the outside flag (0x4000) apart in XYZ2's `.w`; the list path clears the
  word for its triangles.
- **Count VU1 cycles with `src/tools/scripts/vu_cost.py`** (PCSX2 shows none): it runs a
  program's `.vsm` on the integer side over cooked batches, one cycle per pair plus the stall the
  listing marks on it (`STALL_LATENCY ?N`), or the DIV wait at `waitq`, clip flags taken as zero
  (the strip path). Over mars_city1 (2026-11-16): `world_lm` 64.6 cycles a triangle for the base
  pass (`--no-lightmaps`) and 93.3 with the lightmap pass, plain strips 55.5; with openvcl they
  were 117.6, 154.7 and 122.5, when the transform cost 51 cycles a vertex unpipelined. Only kicks
  of an output window count as packets.

## VU memory layout (vu1.h)

Chunks are up to 90 vertices (60 for MD2) in a double buffer, `XTOP` flips per `MSCAL`, and
there are two output windows XGKICKed alternately. The clip scratch is 956..1009, and the
light block starts at 1010. The batch tag block is 7 qwords with no room to grow (see
[gs-renderer.md](gs-renderer.md)).

## Writing VCL

- Above every non-trivial `#macro` and `#vuprog`, after the prose, add an indented
  `;`-commented **C-like pseudo-code** block: `vec4`/`mat4`/`qword` types, `//` comments, and
  control flow that mirrors the real code (`do/while` for a bottom-tested `ibne` loop).
  `textured_triangles.vcl` sets the format. Comment-only top-level `;` blocks pass through
  the preprocessor and the compiler safely.

## Debugging a microprogram

Read the generated `.vsm` as a hypothesis, not as evidence. Four disassembly theories failed
in a row on one bug, and two probes settled it:

- **Readout channel:** paint a value into the vertex colour (`addw.xyz vCol, vf00, <reg>`) and
  read the grey level on screen. A value that saturates the blend hides what's behind it.
- **Byte dump:** `sq` the register to a scratch qword nothing else writes (the spare qword of
  clip scratch), then print it from the EE with
  `ps2::debug::DumpVu1DataMemory(what, addrQw, count)` (`debug/pipeline_dump.h`, debug only),
  gated to every N frames. A probe inside a macro lands in *every* expansion, so check the
  shape of the data before trusting a sample.

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
