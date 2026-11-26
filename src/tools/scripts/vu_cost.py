#!/usr/bin/env python3
# ================================================================================================
# File: vu_cost.py
# Brief: Count the VU1 cycles a world program spends on each cooked batch, for gate R1's topology
# comparison (world_lm.vusl over indexed strips, world_strips.vusl over plain strips). PCSX2 reports
# no VU1 cycles, but oxide-vcl's output is scheduled: every instruction pair is one cycle, and a
# pair the VU stalls on (a result not yet landed, the divider busy) carries the stall in its
# listing comment, 'STALL_LATENCY ?N'. This script unpacks each batch's VIF packet into data
# memory as VIF1 would and runs the program's integer side exactly (loads, stores, counters,
# branches and their delay slots), charging one cycle a pair, plus its marked stall, or the DIV
# wait at a WAITQ. Float
# results are not computed: every vertex is taken to be inside the guard band (the clip flags read
# zero), which is the strip path nearly every batch takes, so the clipper library is never called.
# With --list-path, the first judgement of each batch reads one vertex outside instead: the batch
# takes the list path, its triangles with that vertex call the clipper, which costs nothing and
# sends nothing here, and the rest go out whole. That prices the list path's own walk for the
# console's fit (frame_log.py --vu-fit), which prices the clipper. XGKICK stalls depend on the GS and
# are not charged; both topologies send the same vertices. A batch with a lightmap page runs the
# lightmap pass too (world_lm.vusl; world_strips.vusl draws none).
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""Count VU1 cycles of the world programs over cooked D3WD batches (the strip path)."""

import argparse
import json
from pathlib import Path
import re
import struct
import sys

import d3wd

REPO_ROOT = Path(__file__).resolve().parents[3]
VU_BUILD = REPO_ROOT / "build/vu"
# vu1progs/vu_memory.vush (vu::kInputBase, vu::kWindowA and vu::kWindowQwords in vu.h)
INPUT_BASE = 40
WINDOW_STATE = 7
WINDOW_A = 652
WINDOW_QWORDS = 151
# The lightmap page's A+D packet: its second qword's word 3 asks for the lightmap pass.
BATCH_LIGHTMAP_FLAG = 296
# The VU1 DIV unit's latency in cycles (EE Core User's Manual, VU instruction timings).
DIV_LATENCY = 7
MAX_CYCLES = 1_000_000

LABEL_RE = re.compile(r"^(\w+):\s*$")
STALL_RE = re.compile(r"STALL_LATENCY \?(\d+)")
MEMORY_RE = re.compile(r"^(-?\w+)\((VI\d\d)\)$")


class Program:
    """One assembled program: its instruction pairs (upper, lower, stall cycles) and labels.

    A row of oxide-vcl's listing is the upper instruction and its operands (none for NOP), then the
    lower one and its operands, then an optional comment.
    """

    def __init__(self, name: str, text: str):
        self.name = name
        self.pairs = []
        self.labels = {}
        inside = False
        for line in text.splitlines():
            if line.startswith(f"{name}_CodeStart:"):
                inside = True
                continue
            if line.startswith(f"{name}_CodeEnd:"):
                break
            code, _, comment = line.partition(";")
            if not inside or not code.strip() or code.strip().startswith("."):
                continue
            label = LABEL_RE.match(code)
            if label:
                self.labels[label.group(1)] = len(self.pairs)
                continue
            tokens = code.split()
            upper_size = 1 if tokens[0].upper().startswith("NOP") else 2
            if len(tokens) < upper_size + 1:
                raise ValueError(f"{name}: unexpected line {line!r}")
            stall = STALL_RE.search(comment)
            self.pairs.append((" ".join(tokens[:upper_size]), " ".join(tokens[upper_size:]),
                               int(stall.group(1)) if stall else 0))


def load_program(name: str, build: Path = VU_BUILD) -> Program:
    return Program(name, (build / f"{name}.vsm").read_text())


def unpack(packet: bytes, memory: dict) -> int:
    """VIF1's view of a cooked batch packet: STCYCL, UNPACK (TOPS-relative), ITOP; returns ITOP."""
    words = struct.unpack(f"<{len(packet) // 4}I", packet)
    at, cycle, write, itop = 0, 1, 1, None
    while at < len(words):
        code = words[at]
        at += 1
        command, num, immediate = code >> 24, (code >> 16) & 0xFF, code & 0xFFFF
        if command == 0x00:
            continue
        if command == 0x01:
            cycle, write = immediate & 0xFF, immediate >> 8
            continue
        if command == 0x04:
            itop = immediate & 0x3FF
            continue
        if command & 0x60 != 0x60:
            raise ValueError(f"unexpected VIFcode {code:#010x}")
        layout, signed = command & 0xF, not immediate & (1 << 14)
        address = (immediate & 0x3FF) + (INPUT_BASE if immediate & (1 << 15) else 0)
        if write != 1:
            raise ValueError("only WL = 1 is cooked")
        for index in range(num):
            if layout == 0xC:
                lanes = list(words[at:at + 4])
                at += 4
            elif layout == 0xD:
                halves = struct.unpack("<4H", struct.pack("<2I", *words[at:at + 2]))
                at += 2
                lanes = [(value | 0xFFFF0000) if signed and value & 0x8000 else value for value in halves]
            elif layout == 0xE:
                lanes = list(struct.pack("<I", words[at]))
                at += 1
                lanes = [(value | 0xFFFFFF00) if signed and value & 0x80 else value for value in lanes]
            else:
                raise ValueError(f"unexpected unpack format {layout:#x}")
            memory[(address + index * cycle) & 0x3FF] = lanes
    if itop is None:
        raise ValueError("packet without ITOP")
    return itop


class Machine:
    """The integer side of VU1: VI registers, data memory, and VF registers as copied words."""

    LANES = "xyzw"

    def __init__(self, program: Program, memory: dict, itop: int, list_path: bool = False):
        self.program = program
        # --list-path: the next clip judgement reads a vertex outside.
        self.outside = list_path
        self.memory = memory
        self.itop = itop
        self.vi = [0] * 16
        self.vf = [[0, 0, 0, 0] for _ in range(32)]
        self.cycles = 0
        self.div_ready = 0
        self.kicks = []

    def word(self, address: int) -> list:
        return self.memory.setdefault(address & 0x3FF, [0, 0, 0, 0])

    @staticmethod
    def register(token: str) -> int:
        return int(token.rstrip(",").lstrip("VIF")[0:2])

    def address(self, operand: str) -> int:
        match = MEMORY_RE.match(operand)
        return (int(match.group(1), 0) + self.vi[self.register(match.group(2))]) & 0x3FF

    def set_vi(self, index: int, value: int):
        if index != 0:
            self.vi[index] = value & 0xFFFF

    def signed(self, index: int) -> int:
        value = self.vi[index]
        return value - 0x10000 if value & 0x8000 else value

    def lower(self, text: str):
        """Executes one lower instruction; returns a branch target pair index or None."""
        parts = text.replace(",", " ").split()
        op, args = parts[0].lower(), parts[1:]
        base, _, dest = op.partition(".")
        lanes = [self.LANES.index(lane) for lane in (dest or "xyzw")]
        # loi is the pair's I-bit immediate, a float for the upper half: nothing integer happens.
        if base in ("nop", "fcset", "waitq", "div", "sqrt", "rsqrt", "loi"):
            if base == "div":
                self.div_ready = self.cycles + DIV_LATENCY
            return None
        if base == "lq":
            source = self.word(self.address(args[1]))
            target = self.vf[self.register(args[0])]
            for lane in lanes:
                target[lane] = source[lane]
        elif base == "sq":
            target = self.word(self.address(args[1]))
            source = self.vf[self.register(args[0])]
            for lane in lanes:
                target[lane] = source[lane]
        elif base == "sqi":
            pointer = self.register(args[1].strip("()+"))
            target = self.word(self.vi[pointer])
            source = self.vf[self.register(args[0])]
            for lane in lanes:
                target[lane] = source[lane]
            self.set_vi(pointer, self.vi[pointer] + 1)
        elif base == "ilw":
            self.set_vi(self.register(args[0]), self.word(self.address(args[1]))[lanes[0]])
        elif base == "isw":
            target = self.word(self.address(args[1]))
            for lane in lanes:
                target[lane] = self.vi[self.register(args[0])]
        elif base in ("iaddiu", "iaddi"):
            self.set_vi(self.register(args[0]), self.vi[self.register(args[1])] + int(args[2], 0))
        elif base == "isubiu":
            self.set_vi(self.register(args[0]), self.vi[self.register(args[1])] - int(args[2], 0))
        elif base in ("iadd", "isub", "iand", "ior"):
            left, right = self.vi[self.register(args[1])], self.vi[self.register(args[2])]
            value = {"iadd": left + right, "isub": left - right, "iand": left & right, "ior": left | right}[base]
            self.set_vi(self.register(args[0]), value)
        elif base == "xtop":
            self.set_vi(self.register(args[0]), INPUT_BASE)
        elif base == "xitop":
            self.set_vi(self.register(args[0]), self.itop)
        elif base == "xgkick":
            # NLOOP as the GIF reads it now; a later packet reuses the window. The batch's state
            # packet and the null tag are kicked too but send no vertex.
            address = self.vi[self.register(args[0])]
            if WINDOW_A <= address < WINDOW_A + 2 * WINDOW_QWORDS:
                self.kicks.append(self.word(address)[0] & 0x7FFF)
        elif base == "fcand":
            # Every vertex inside the guard band: no clip flag is set (but one, with --list-path).
            self.set_vi(1, 1 if self.outside else 0)
            self.outside = False
        elif base == "jalr":
            # A call to the clipper (--list-path): free, and it sends nothing.
            self.set_vi(self.register(args[0]), 0)
        elif base == "mfir":
            # A VI register into VF lanes, sign-extended (the counts' float conversion follows).
            target = self.vf[self.register(args[0])]
            for lane in lanes:
                target[lane] = self.signed(self.register(args[1]))
        elif base == "mtir":
            lane = self.LANES.index(args[1][-1])
            self.set_vi(self.register(args[0]), self.vf[self.register(args[1][:-1])][lane])
        elif base == "move":
            source = self.vf[self.register(args[1])]
            target = self.vf[self.register(args[0])]
            for lane in lanes:
                target[lane] = source[lane]
        elif base == "mr32":
            source = list(self.vf[self.register(args[1])])
            target = self.vf[self.register(args[0])]
            rotated = source[1:] + source[:1]
            for lane in lanes:
                target[lane] = rotated[lane]
        elif base == "b":
            return self.program.labels[args[0]]
        elif base in ("ibeq", "ibne"):
            equal = self.vi[self.register(args[0])] == self.vi[self.register(args[1])]
            return self.program.labels[args[2]] if equal == (base == "ibeq") else None
        elif base in ("ibgtz", "ibgez", "ibltz", "iblez"):
            value = self.signed(self.register(args[0]))
            taken = {"ibgtz": value > 0, "ibgez": value >= 0, "ibltz": value < 0, "iblez": value <= 0}[base]
            return self.program.labels[args[1]] if taken else None
        else:
            raise ValueError(f"{self.program.name}: unsupported lower instruction {text!r}")
        return None

    def run(self) -> int:
        pc, pending, ending = 0, None, False
        while True:
            upper, lower, stall = self.program.pairs[pc]
            if lower.startswith("waitq"):
                self.cycles = max(self.cycles, self.div_ready)
            else:
                self.cycles += stall
            self.cycles += 1
            target = self.lower(lower)
            if self.cycles > MAX_CYCLES:
                raise RuntimeError(f"{self.program.name}: runaway program")
            if ending:
                return self.cycles
            if "[E]" in upper:
                ending = True
            # A branch's delay slot runs before the jump lands.
            pc = pending if pending is not None else pc + 1
            pending = target


def batch_cycles(program: Program, packet: bytes, lit: bool = False, list_path: bool = False) -> tuple[int, int, int]:
    """Cycles, GS packets kicked and vertices sent for one batch, with its lightmap pass when 'lit'."""
    memory = {WINDOW_STATE: [WINDOW_A, WINDOW_QWORDS, 0, 0], INPUT_BASE + BATCH_LIGHTMAP_FLAG: [0, 0, 0, 1 if lit else 0]}
    itop = unpack(packet, memory)
    machine = Machine(program, memory, itop, list_path)
    cycles = machine.run()
    return cycles, len(machine.kicks), sum(machine.kicks)


def measure(world: d3wd.World, program: Program, lightmaps: bool = True, list_path: bool = False) -> list:
    return [batch_cycles(program, world.packet(batch), lightmaps and batch.lightmap_page != d3wd.NO_LIGHTMAP, list_path)
            for batch in world.batches]


def summary(world: d3wd.World, report: dict, costs: list, areas=None) -> dict:
    chosen = range(len(world.areas)) if areas is None else areas
    batches = [index for area in chosen for index in range(world.areas[area].first_batch,
                                                          world.areas[area].first_batch + world.areas[area].batch_count)]
    triangles = sum(report["area_list"][area]["triangles"] for area in chosen)
    vertices = sum(world.batches[index].vertex_count for index in batches)
    cycles = sum(costs[index][0] for index in batches)
    # Per vertex too: the frame log counts the vertices it draws, not triangles (frame_log.py's VU1
    # cycles a vertex, from the GPU sampler over serial frames).
    return {"batches": len(batches), "triangles": triangles, "vertices": vertices, "cycles": cycles,
            "cycles_per_triangle": round(cycles / triangles, 2), "cycles_per_vertex": round(cycles / vertices, 2) if vertices else 0.0,
            "kicks": sum(costs[index][1] for index in batches), "vertices_sent": sum(costs[index][2] for index in batches),
            "packet_bytes": sum(16 * world.batches[index].packet_qwords for index in batches)}


def list_path_summary(world: d3wd.World, strip: list, listed: list, lightmaps: bool) -> dict:
    """The whole map down the list path against the strip path: the cycles it adds an entry walked,
    the unit the GPU sampler counts (list_entries: a pass walks four a qword of the stream, a lit
    batch's twice)."""
    entries = sum(4 * ((batch.index_count + 3) // 4) * (2 if lightmaps and batch.lightmap_page != d3wd.NO_LIGHTMAP else 1)
                  for batch in world.batches)
    strip_cycles = sum(cost[0] for cost in strip)
    list_cycles = sum(cost[0] for cost in listed)
    return {"entries": entries, "strip_cycles": strip_cycles, "list_cycles": list_cycles,
            "cycles_per_entry": round(list_cycles / entries, 2), "added_per_entry": round((list_cycles - strip_cycles) / entries, 2),
            "vertices_sent": sum(cost[2] for cost in listed)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worlds", type=Path, nargs="+", help="cooked .d3wd files, each beside its .report.json")
    parser.add_argument("--areas", type=int, nargs="*", action="append", default=None,
                        help="also sum these areas (repeatable: one view's areas each)")
    parser.add_argument("--vu", type=Path, default=VU_BUILD, help="where the assembled .vsm files are")
    parser.add_argument("--no-lightmaps", action="store_true", help="count the base pass alone")
    parser.add_argument("--list-path", action="store_true",
                        help="also run every batch down the list path (one vertex outside, the clipper free): its cycles an entry walked")
    args = parser.parse_args()
    results = {}
    for path in args.worlds:
        world = d3wd.load(path)
        report = json.loads(path.with_name(path.stem + ".report.json").read_text())
        program = load_program("world_lm" if world.indexed() else "world_strips", args.vu)
        costs = measure(world, program, not args.no_lightmaps)
        entry = {"program": program.name, "topology": report["topology"], "map": summary(world, report, costs)}
        for areas in args.areas or []:
            entry["areas " + " ".join(str(area) for area in areas)] = summary(world, report, costs, areas)
        if args.list_path:
            entry["list_path"] = list_path_summary(world, costs, measure(world, program, not args.no_lightmaps, True), not args.no_lightmaps)
        results[str(path)] = entry
    json.dump(results, sys.stdout, indent=2)
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
