#!/usr/bin/env python3
# ================================================================================================
# File: test_vu_cost.py
# Brief: Pin vu_cost.py's VU1 model: delay slots, the DIV wait at WAITQ, the stalls oxide-vcl's
# listing marks, the E bit, VIF1's unpack
# with skipping writes and sign extension, both world programs' strip paths sending every vertex
# of a batch across window kicks, the lightmap pass sending them again, and d3wd.py's decoding of
# a batch's vertices and strips.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/scripts"))
import d3wd
import vu_cost

TOY = """
\t\t.vu
toy_CodeStart:
                    nop                             xitop VI01
lLoop:
                    nop                             iaddi VI01, VI01, -1
                    nop                             div q, VF00w, VF01w
                    nop                             waitq
                    nop                             ibgtz VI01, lLoop
                    nop                             iaddiu VI02, VI02, 1
                    nop[E]                          nop
                    nop                             isw.x VI02, 0(VI00)
toy_CodeEnd:
"""

STALLS = """
\t\t.vu
stall_CodeStart:
; _LNOPT_w=[ normal2 ] 4 [8 0] 8   [stall]
         NOP                                                        div           Q,VF00w,VF01w
         mulq.xyz      VF02,VF01,Q                                  iaddiu        VI02,VI00,0x00000003 ;\tSTALL_LATENCY ?4
         NOP[E]                                                     NOP
         NOP                                                        NOP
stall_CodeEnd:
"""


def vif(command, num=0, immediate=0):
    return (command << 24) | (num << 16) | immediate


def unpack_code(layout, count, address, unsigned=False):
    return vif(0x60 | layout, count, address | (1 << 14 if unsigned else 0) | (1 << 15))


def batch_packet(strip, indexed):
    """A cooked WA packet: 'strip' lists (vertex, adc) in strip order."""
    vertices = sorted({vertex for vertex, _ in strip}) if indexed else list(range(len(strip)))
    quads = (len(strip) + 3) // 4 if indexed else 0
    words = [vif(0x01, 0, 1 | 1 << 8), unpack_code(0xC, 2, 0), 0, 0, 0, quads, 0x3F800000, 0x3F800000, 0, 0]
    if indexed:
        entries = [2 + quads + 4 * vertex | (0x8000 if adc else 0) for vertex, adc in strip]
        entries += [0x8000 | (2 + quads)] * (4 * quads - len(entries))
        words.append(unpack_code(0xD, quads, 2, unsigned=True))
        words += [entries[index] | entries[index + 1] << 16 for index in range(0, len(entries), 2)]
    flags = [0] * len(vertices) if indexed else [0x8000 if adc else 0 for _, adc in strip]
    words += [vif(0x01, 0, 4 | 1 << 8), unpack_code(0xD, len(vertices), 2 + quads)]
    for flag in flags:
        words += [0, flag << 16]
    words.append(unpack_code(0xD, len(vertices), 3 + quads))
    words += [0, 0] * len(vertices)
    words.append(unpack_code(0xE, len(vertices), 4 + quads, unsigned=True))
    words += [0x80808080] * len(vertices)
    words.append(vif(0x04, 0, len(vertices)))
    while len(words) % 4:
        words.append(0)
    return struct.pack(f"<{len(words)}I", *words)


def strip_of(length):
    """One strip over 'length' distinct vertices."""
    return [(vertex, vertex < 2) for vertex in range(length)]


class MachineTests(unittest.TestCase):
    def test_delay_slots_waitq_and_end_bit(self):
        program = vu_cost.Program("toy", TOY)
        memory = {}
        machine = vu_cost.Machine(program, memory, itop=3)
        # xitop, then per pass: iaddi, div, waitq (7 cycles after the div), ibgtz, its delay slot;
        # then the E pair and the one after it.
        self.assertEqual(machine.run(), 1 + 3 * 12 + 2)
        # The delay slot ran on every pass, the pair after the E bit once.
        self.assertEqual(memory[0][0], 3)

    def test_marked_stalls_are_charged(self):
        # oxide-vcl's layout: an upper instruction with operands, a lower one, and a pair the VU
        # stalls on, which the listing marks with the stall's cycles.
        program = vu_cost.Program("stall", STALLS)
        self.assertEqual(program.pairs[1], ("mulq.xyz VF02,VF01,Q", "iaddiu VI02,VI00,0x00000003", 4))
        machine = vu_cost.Machine(program, {}, itop=0)
        self.assertEqual(machine.run(), 4 + 4)
        self.assertEqual(machine.vi[2], 3)

    def test_unpack_skips_and_extends(self):
        memory = {}
        words = [vif(0x01, 0, 3 | 1 << 8), unpack_code(0xD, 2, 2), 0x00010002, 0x80000003, 0x0000FFFF, 0x7FFF0000,
                 unpack_code(0xE, 1, 3, unsigned=True), 0x80FF0102, vif(0x04, 0, 2)]
        packet = struct.pack(f"<{len(words)}I", *words)
        self.assertEqual(vu_cost.unpack(packet, memory), 2)
        base = vu_cost.INPUT_BASE
        self.assertEqual(memory[base + 2], [2, 1, 3, 0xFFFF8000])
        self.assertEqual(memory[base + 5], [0xFFFFFFFF, 0, 0, 0x7FFF])
        self.assertEqual(memory[base + 3], [2, 1, 0xFF, 0x80])
        self.assertNotIn(base + 4, memory)


@unittest.skipUnless((vu_cost.VU_BUILD / "world_lm.vsm").is_file() and (vu_cost.VU_BUILD / "world_strips.vsm").is_file(),
                     "make vu builds the programs")
class WorldProgramTests(unittest.TestCase):
    def test_every_vertex_is_sent_across_kicks(self):
        # Short batches fit one window; long ones kick and resend two vertices per extra window.
        for name, indexed, length in (("world_lm", True, 6), ("world_lm", True, 72), ("world_strips", False, 6),
                                      ("world_strips", False, 70)):
            with self.subTest(program=name, vertices=length):
                cycles, kicks, sent = vu_cost.batch_cycles(vu_cost.load_program(name), batch_packet(strip_of(length), indexed))
                padded = 4 * ((length + 3) // 4) if indexed else length
                self.assertEqual(sent, padded + 2 * (kicks - 1))
                self.assertEqual(kicks, 1 if length <= 48 else 2)
                # A floor, not a pin: each vertex is transformed and sent.
                self.assertGreater(cycles, 24 * length)

    def test_lightmap_pass_sends_every_vertex_again(self):
        # Its windows are the base pass's (ST, RGBAQ and XYZ2, 50 vertices), so it kicks and resends
        # exactly as the base pass does.
        program = vu_cost.load_program("world_lm")
        for length, base_kicks, base_sent in ((6, 1, 8), (68, 2, 70)):
            with self.subTest(vertices=length):
                unlit = vu_cost.batch_cycles(program, batch_packet(strip_of(length), True))
                lit = vu_cost.batch_cycles(program, batch_packet(strip_of(length), True), lit=True)
                self.assertEqual(unlit[1:], (base_kicks, base_sent))
                self.assertEqual(lit[1:], (2 * base_kicks, 2 * base_sent))
                self.assertGreater(lit[0], unlit[0] + 8 * length)
        # world_strips draws no lightmap pass.
        strips = vu_cost.load_program("world_strips")
        self.assertEqual(vu_cost.batch_cycles(strips, batch_packet(strip_of(6), False), lit=True)[1:],
                         vu_cost.batch_cycles(strips, batch_packet(strip_of(6), False))[1:])

    def test_plain_strips_transform_every_repeat(self):
        # Two strips sharing two vertices, against two of the same length over distinct ones: the
        # indexed program transforms each shared vertex once, so the repeats cost it less; the plain
        # one transforms every entry, so they cost it the same.
        shared = [(0, True), (1, True), (2, False), (3, False), (2, True), (3, True), (4, False), (5, False)]
        distinct = [(0, True), (1, True), (2, False), (3, False), (4, True), (5, True), (6, False), (7, False)]
        lm, strips = vu_cost.load_program("world_lm"), vu_cost.load_program("world_strips")
        cost = lambda program, strip, indexed: vu_cost.batch_cycles(program, batch_packet(strip, indexed))[0]
        self.assertLess(cost(lm, shared, True), cost(lm, distinct, True))
        self.assertEqual(cost(strips, shared, False), cost(strips, distinct, False))


class DecoderTests(unittest.TestCase):
    def batch(self, packet, indexed, vertices, indices):
        world = d3wd.World.__new__(d3wd.World)
        world.packets = packet
        flags = d3wd.FLAG_INDEXED_STRIPS if indexed else 0
        return world, d3wd.Batch(flags, vertices, 0, len(packet) // 16, indices, 0, d3wd.NO_LIGHTMAP)

    def test_strips_decode_in_id_winding(self):
        # Two strips: triangle k of each is (k, k+1, k+2) for even k, (k+1, k, k+2) for odd k.
        strip = [(0, True), (1, True), (2, False), (3, False), (4, False), (2, True), (3, True), (5, False)]
        expected = [(0, 1, 2), (2, 1, 3), (2, 3, 4), (2, 3, 5)]
        world, batch = self.batch(batch_packet(strip, True), True, 6, len(strip))
        self.assertEqual(world.triangles(batch), expected)
        plain, plain_batch = self.batch(batch_packet(strip, False), False, len(strip), 0)
        self.assertEqual(plain.triangles(plain_batch), [(0, 1, 2), (2, 1, 3), (2, 3, 4), (5, 6, 7)])

    def test_vertices_decode_the_streams(self):
        world, batch = self.batch(batch_packet(strip_of(6), True), True, 6, 6)
        vertices = world.vertices(batch)
        self.assertEqual(len(vertices), 6)
        self.assertEqual(vertices[0].color, (0x80, 0x80, 0x80, 0x80))
        self.assertEqual(vertices[5].lightmap, (0.0, 0.0))


if __name__ == "__main__":
    unittest.main()
