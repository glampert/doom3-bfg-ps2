#!/usr/bin/env python3
# ================================================================================================
# File: test_ui_swf.py
# Brief: Synthetic native BSWF16 structure, feature inventory and malformed-input checks.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import json
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/scripts"))
import build_authored_ui as authored
from ui_swf import Inspector, Reader, SwfError, inspect_swf


def be(fmt, *values):
    return struct.pack(">" + fmt, *values)


def movie(dictionary, frames=None):
    return be("IqffH", 0x42535710, -1, 640, 480, 1536) + authored.sprite(frames or [[]]) + be("i", len(dictionary)) + b"".join(dictionary)


def image(name="."):
    return be("i", 1) + authored.string(name) + be("4i4f", 16, 0, 16, 0, 1, 1, 1, 1)


def font():
    return be("i", 5) + authored.string("synthetic_ascii") + be("3hiHhi6fi3H", 14, 0, 0, 1, 65, 16, 3, 0, 0, 10, 0, 10, 10, 3, 0, 1, 2)


def shape(morph=False, gradient=False, stroke=False):
    output = be("i8fi", 3 if morph else 2, 0, 0, 10, 10, 0, 0, 10, 10, 1)
    output += bytes((1 if gradient else 4, 0)) + b"\xff" * 8 + be("6f", 1, 1, 0, 0, 0, 0) * 2
    output += bytes((1 if gradient else 0,)) + (bytes((0, 255)) + b"\xff" * 8 if gradient else b"")
    output += be("fHi6f", 0, 1, 3, 0, 0, 10, 0, 10, 10)
    output += be("i6f", 3, 0, 0, 10, 0, 10, 10) if morph else be("i", 0)
    output += be("i3Hi", 3, 0, 1, 2, int(stroke))
    if stroke:
        output += be("HH", 20, 20) + b"\xff" * 8 + be("i6f", 3, 0, 0, 10, 0, 10, 10)
        output += be("i6f", 3, 0, 0, 10, 0, 10, 10) if morph else be("i", 0)
        output += be("i3H", 3, 0, 1, 2)
    return output


def text():
    return (be("i4f6fiH", 6, 0, 0, 10, 10, 1, 1, 0, 0, 0, 0, 1, 5) + b"\xff" * 4 +
            be("hhHHBiIi", 0, 0, 960, 0, 1, 1, 0, 16))


def edittext():
    return (be("i4fIHH", 7, 0, 0, 10, 10, 8, 5, 960) + b"\xff" * 4 +
            be("HiHHHh", 96, 0, 0, 0, 0, 0) + authored.string("variable") + authored.string("#str_12345"))


class NativeSwfInventory(unittest.TestCase):
    def test_authored_native_profile_and_deterministic_sanitized_report(self):
        output = inspect_swf(authored.build_swf())
        self.assertEqual(output["format"], "BSWF16")
        self.assertEqual(output["dictionary_counts"], {"null": 1, "image": 1, "shape": 5, "sprite": 2, "font": 1, "edittext": 6})
        self.assertEqual(output["features"]["bitmap_fills"], 1)
        self.assertEqual(output["scripts"]["defined_functions"], ["focusNext", "select"])
        self.assertEqual(output["dependencies"]["fonts"], ["ps2_ascii"])
        self.assertEqual(output["dictionary"][3]["size"], [16, 16])
        self.assertEqual(output["dictionary"][3]["atlas_offset"], [246, 64])
        self.assertFalse(output["dynamic_dependency_closure"])
        encoded = json.dumps(output, sort_keys=True, allow_nan=False)
        self.assertNotIn("TAB TO FOCUS", encoded)
        self.assertNotIn("DOOM 3 BFG / PS2", encoded)
        self.assertEqual(encoded, json.dumps(inspect_swf(authored.build_swf()), sort_keys=True, allow_nan=False))
        self.assertIn("_root.pulse.@1", [row["path"] for row in output["instance_paths"]])
        bitmap = output["dictionary"][4]["fills"][0]
        self.assertIn("bitmap_pre_atlas_uv_bounds_conservative", bitmap)

    def test_every_truncated_authored_prefix_is_rejected(self):
        source = authored.build_swf()
        for end in range(len(source)):
            with self.subTest(end=end), self.assertRaises(SwfError):
                inspect_swf(source[:end])

    def test_all_native_dictionary_variants_and_forward_references(self):
        dictionary = [be("i", 0), image("synthetic/material"), shape(stroke=True), shape(morph=True, gradient=True),
                      be("i", 4) + authored.sprite([[]]), font(), text(), edittext()]
        output = inspect_swf(movie(dictionary, [[authored.place(1, 4, name="child")]]))
        self.assertEqual(set(output["dictionary_counts"]), {"null", "image", "shape", "morph", "sprite", "font", "text", "edittext"})
        self.assertEqual(output["features"]["morphs"], 1)
        self.assertEqual(output["features"]["gradient_fills"], 1)
        self.assertEqual(output["features"]["strokes"], 1)
        self.assertEqual(output["dependencies"]["localization_ids"], ["#str_12345"])
        self.assertEqual(output["dependencies"]["images"], ["synthetic/material"])

    def test_unknown_magic_dictionary_and_retained_tag_are_rejected(self):
        cases = [b"WRNG" + authored.build_swf()[4:], movie([be("i", 99)]), movie([be("i", 0)], [[(999, b"")]])]
        for source in cases:
            with self.subTest(source=source[:30]), self.assertRaises(SwfError):
                inspect_swf(source)

    def test_frame_dimensions_rate_offsets_and_counts_are_validated(self):
        source = movie([be("i", 0)])
        mutations = [(12, ">f", float("nan")), (16, ">f", 0), (20, ">H", 0),
                     (22, ">H", 0), (24, ">i", -1), (24, ">i", 1_000_001), (28, ">I", 1), (32, ">I", 1)]
        for offset, fmt, value in mutations:
            changed = bytearray(source)
            struct.pack_into(fmt, changed, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(SwfError):
                inspect_swf(bytes(changed))

    def test_dictionary_references_and_triangle_indices_are_validated(self):
        for character in (0, 9):
            with self.subTest(character=character), self.assertRaises(SwfError):
                inspect_swf(movie([be("i", 0)], [[authored.place(1, character)]]))
        invalid = bytearray(shape())
        struct.pack_into(">H", invalid, len(invalid) - 6, 99)
        with self.assertRaisesRegex(SwfError, "vertex index"):
            inspect_swf(movie([be("i", 0), image(), bytes(invalid)]))
        with self.assertRaisesRegex(SwfError, "wrong type"):
            inspect_swf(movie([be("i", 0), be("i", 0), shape()]))

    def test_nonfinite_shape_coordinates_and_inverted_bounds_are_rejected(self):
        for offset, value in ((4, float("inf")), (12, -1)):
            invalid = bytearray(shape())
            struct.pack_into(">f", invalid, offset, value)
            with self.subTest(offset=offset), self.assertRaises(SwfError):
                inspect_swf(movie([be("i", 0), image(), bytes(invalid)]))

    def test_placeobject3_masks_blends_cache_filters_and_color_add(self):
        color = authored.Bits()
        color.put(1, 1)
        color.put(0, 1)
        color.signed_values((0, 32, 0, 0), 4)
        # Flags: character, color, clip-depth. Flags2: blend, cache, filter-list.
        body = bytes((2 | 8 | 64, 7)) + struct.pack("<HH", 1, 2) + color.data() + struct.pack("<H", 3)
        body += bytes((1, 1)) + bytes(9) + bytes((8, 1))
        output = inspect_swf(movie([be("i", 0), image(), shape()], [[(70, body)]]))
        self.assertEqual(output["features"]["masks"], 1)
        self.assertEqual(output["features"]["color_add"], 1)
        self.assertEqual(output["features"]["blend_modes"], [8])
        self.assertEqual(output["features"]["filter_types"], [1])
        self.assertTrue(any("skips filter" in value for value in output["limitations"]))

    def test_invalid_placeobject_and_filter_payloads_are_rejected(self):
        cases = [bytes((0,)) + struct.pack("<H", 1), bytes((2,)) + struct.pack("<HH", 0, 2),
                 bytes((2, 1)) + struct.pack("<HH", 1, 2) + bytes((1, 99)),
                 bytes((2, 2)) + struct.pack("<HH", 1, 2) + bytes((15,))]
        for index, payload in enumerate(cases):
            tag = 26 if index < 2 else 70
            with self.subTest(index=index), self.assertRaises(SwfError):
                inspect_swf(movie([be("i", 0), image(), shape()], [[(tag, payload)]]))

    def test_avm1_literal_resources_localization_and_payload_privacy(self):
        code = authored.push("private caption body", "#str_456", "generated/swf/sample.bswf") + authored.action(0)
        output = inspect_swf(movie([be("i", 0)], [[(12, code)]]))
        self.assertEqual(output["dependencies"]["localization_ids"], ["#str_456"])
        self.assertEqual(output["dependencies"]["script_resource_candidates"], ["generated/swf/sample.bswf"])
        self.assertNotIn("private caption body", json.dumps(output))
        self.assertEqual(output["counts"]["script_strings"], 3)

    def test_named_localization_ids_and_conservative_literal_codepoints(self):
        # The authored helper uses ASCII by design; create the UTF-8 Push directly.
        code = authored.action(0x96, b"\0#STR_MENU_CONTINUE #str_3Suffix #str_012\0\0\xc3\xa9\0") + b"\0"
        output = inspect_swf(movie([be("i", 0)], [[(12, code)]]))
        self.assertEqual(output["dependencies"]["localization_ids"], ["#STR_MENU_CONTINUE", "#str_012", "#str_3Suffix"])
        self.assertIn(233, output["text"]["literal_codepoints"])
        self.assertIn("includes identifiers and paths", output["text"]["coverage_scope"])

    def test_action_lengths_branches_strings_push_types_and_floats_are_checked(self):
        cases = [b"\x96\x04\x00\x00x", authored.action(0x96, b"\x00unterminated"),
                 authored.action(0x96, b"\xff"), authored.action(0x96, b"\x01" + struct.pack("<f", float("nan"))),
                 authored.action(0x99, struct.pack("<h", -1)) + b"\0", b"\0\x04"]
        for index, code in enumerate(cases):
            with self.subTest(index=index), self.assertRaises(SwfError):
                inspect_swf(movie([be("i", 0)], [[(12, code)]]))

    def test_constant_pool_functions_with_and_init_actions_are_scanned(self):
        pool = authored.action(0x88, struct.pack("<H", 1) + b"#str_789\0")
        pushed = authored.action(0x96, b"\x08\x00")
        code = pool + authored.function("testFunction", pushed)
        with_code = pushed + authored.action(0)
        code += authored.action(0x94, struct.pack("<H", len(with_code)) + with_code) + authored.action(0)
        output = inspect_swf(movie([be("i", 0)], [[(12, code)]]))
        self.assertEqual(output["scripts"]["defined_functions"], ["testFunction"])
        self.assertEqual(output["dependencies"]["localization_ids"], ["#str_789"])
        self.assertEqual(output["scripts"]["opcodes"]["Push"], 2)
        inspector = Inspector()
        # A zero-command sprite with a separately stored DoInitAction block.
        sprite = be("Hi2IiI", 1, 2, 0, 0, 0, len(pool + b"\0")) + pool + b"\0" + be("iiI", 0, 1, len(pool + b"\0"))
        inspector.sprite(Reader(sprite), None)
        self.assertEqual(inspector.counts["init_actions"], 1)
        self.assertEqual(inspector.dependencies["localization_ids"], {"#str_789"})

    def test_unknown_actions_and_unresolved_pool_references_stay_explicit(self):
        code = authored.action(0x96, b"\x08\x04") + authored.action(0xFE, b"opaque") + b"\0"
        output = inspect_swf(movie([be("i", 0)], [[(12, code)]]))
        self.assertEqual(output["features"]["unresolved_constant_references"], 1)
        self.assertEqual(output["scripts"]["opcodes"]["unknown_0xfe"], 1)
        self.assertTrue(any("Unknown AVM1" in value for value in output["limitations"]))

    def test_clip_actions_are_bounded_and_marked_unsupported_by_native(self):
        code = authored.push("#str_88") + b"\0"
        clip = struct.pack("<HIII", 0, 1, 1, len(code)) + code + struct.pack("<I", 0)
        place = bytes((2 | 128,)) + struct.pack("<HH", 1, 2) + clip
        output = inspect_swf(movie([be("i", 0), image(), shape()], [[(26, place)]]))
        self.assertEqual(output["features"]["clip_action_records"], 1)
        self.assertEqual(output["dependencies"]["localization_ids"], ["#str_88"])

    def test_recursive_sprite_graph_is_reported_without_recursive_expansion(self):
        recursive = be("i", 4) + authored.sprite([[authored.place(1, 1, name="again")]])
        output = inspect_swf(movie([be("i", 0), recursive], [[authored.place(1, 1, name="child")]]))
        self.assertEqual(len(output["instance_paths"]), 2)
        self.assertTrue(any("Recursive" in value for value in output["limitations"]))

    def test_unconsumed_bytes_and_non_bytes_input_are_rejected(self):
        with self.assertRaisesRegex(SwfError, "unconsumed"):
            inspect_swf(movie([be("i", 0)]) + b"extra")
        with self.assertRaises(TypeError):
            inspect_swf(bytearray(movie([be("i", 0)])))


if __name__ == "__main__":
    unittest.main()
