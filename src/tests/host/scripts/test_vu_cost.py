#!/usr/bin/env python3
# ================================================================================================
# File: test_vu_cost.py
# Brief: Pin vu_cost.py's VU1 model: delay slots, the DIV wait at WAITQ, the E bit, VIF1's unpack
# with skipping writes and sign extension, and both world programs' strip paths sending every
# vertex of a batch across window kicks.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/scripts"))
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
        entries = [2 + quads + 3 * vertex | (0x8000 if adc else 0) for vertex, adc in strip]
        entries += [0x8000 | (2 + quads)] * (4 * quads - len(entries))
        words.append(unpack_code(0xD, quads, 2, unsigned=True))
        words += [entries[index] | entries[index + 1] << 16 for index in range(0, len(entries), 2)]
    flags = [0] * len(vertices) if indexed else [0x8000 if adc else 0 for _, adc in strip]
    words += [vif(0x01, 0, 3 | 1 << 8), unpack_code(0xD, len(vertices), 2 + quads)]
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
        for name, indexed, length in (("world_lm", True, 6), ("world_lm", True, 88), ("world_strips", False, 6),
                                      ("world_strips", False, 101)):
            with self.subTest(program=name, vertices=length):
                cycles, kicks, sent = vu_cost.batch_cycles(vu_cost.load_program(name), batch_packet(strip_of(length), indexed))
                padded = 4 * ((length + 3) // 4) if indexed else length
                self.assertEqual(sent, padded + 2 * (kicks - 1))
                self.assertEqual(kicks, 1 if length <= 70 else 2)
                self.assertGreater(cycles, 48 * length)

    def test_plain_strips_transform_every_repeat(self):
        # Two strips sharing two vertices: the indexed program transforms each vertex once.
        strip = [(0, True), (1, True), (2, False), (3, False), (2, True), (3, True), (4, False), (5, False)]
        indexed = vu_cost.batch_cycles(vu_cost.load_program("world_lm"), batch_packet(strip, True))[0]
        plain = vu_cost.batch_cycles(vu_cost.load_program("world_strips"), batch_packet(strip, False))[0]
        self.assertGreater(plain - indexed, 2 * 40)


if __name__ == "__main__":
    unittest.main()
