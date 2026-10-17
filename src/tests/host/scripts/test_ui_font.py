#!/usr/bin/env python3
# ================================================================================================
# File: test_ui_font.py
# Brief: Check font ABI bounds and localization coverage using synthetic data without retail text.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import json
from pathlib import Path
import struct
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/scripts"))
import ui_font


def font(codepoints=(32, 65, 9731), *, glyph=None, ascender=14, descender=-2):
    record = glyph if glyph is not None else struct.pack("<BBbbBBHH", 8, 12, 10, -1, 9, 0, 1, 1)
    return (struct.pack(">Ihhhh", ui_font.FONT_MAGIC, 48, ascender, descender, len(codepoints)) +
            record * len(codepoints) + struct.pack("<" + "I" * len(codepoints), *codepoints))


def old_font(*, material=b"fonts/test\0", trailer=False):
    glyph = struct.pack("<7i4fi32s", 12, 10, -2, 8, 9, 8, 12, 0.0, 0.0, 8 / 256, 12 / 256, 0, material)
    result = glyph * 256
    return result + struct.pack("<f64s", 1.0, b"test\0") if trailer else result


class NativeFont(unittest.TestCase):
    def test_native_endian_layout_and_signed_bearings(self):
        result = ui_font.inspect_font(font(), atlas_width=64, atlas_height=32)
        self.assertEqual(result["bytes"], 12 + 3 * 14)
        self.assertEqual(result["glyph_record_bytes"], 10)
        self.assertEqual(result["codepoints"], [32, 65, 9731])
        self.assertEqual(result["required_atlas_extent"], [9, 13])
        self.assertEqual(result["sampling_bounds"], [0.5, 0.5, 9.5, 13.5])
        self.assertEqual(result["glyphs"][0]["left"], -1)
        self.assertEqual(result["descender"], -2)
        self.assertEqual(result["needs_sampling_gutter"], [])
        json.dumps(result, allow_nan=False)

    def test_empty_negative_and_inconsistent_counts_are_rejected(self):
        for data in (b"", b"idf*", font()[:-1], font() + b"\0",
                     struct.pack(">Ihhhh", ui_font.FONT_MAGIC, 48, 14, 0, 0),
                     struct.pack(">Ihhhh", ui_font.FONT_MAGIC, 48, 14, 0, -1)):
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                ui_font.inspect_font(data)

    def test_wrong_magic_point_size_and_header_metrics_are_rejected(self):
        for offset, data in ((0, b"bad!"), (4, struct.pack(">h", 24)),
                             (6, struct.pack(">h", -1)), (8, struct.pack(">h", 4097))):
            modified = bytearray(font())
            modified[offset:offset + len(data)] = data
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                ui_font.inspect_font(bytes(modified))

    def test_non_scalar_or_descending_character_indices_are_rejected(self):
        for indices in ((65, 32), (0xD800,), (0x110000,), (0xFFFFFFFF,)):
            with self.subTest(indices=indices), self.assertRaises(ValueError):
                ui_font.inspect_font(font(indices))

    def test_retail_compatible_duplicate_indices_keep_native_last_selection(self):
        result = ui_font.inspect_font(font((32, 65, 65, 169, 169)))
        self.assertEqual(result["glyph_count"], 5)
        self.assertEqual(result["unique_glyph_count"], 3)
        self.assertEqual(result["duplicate_codepoints"], [65, 169])
        self.assertEqual(result["character_indices"], [32, 65, 65, 169, 169])
        self.assertEqual([glyph["selected_for_codepoint"] for glyph in result["glyphs"]],
                         [True, False, True, False, True])

    def test_unused_native_padding_does_not_change_metrics(self):
        glyph = struct.pack("<BBbbBBHH", 8, 12, 10, 0, 9, 255, 1, 1)
        result = ui_font.inspect_font(font(glyph=glyph))
        self.assertEqual(result["padding_nonzero_count"], 3)
        self.assertEqual(result["glyphs"][0]["s"], 1)

    def test_duplicate_ascii_records_cannot_overflow_the_native_signed_byte_cache(self):
        with self.assertRaisesRegex(ValueError, "ASCII cache index"):
            ui_font.inspect_font(font((32,) * 129))

    def test_atlas_rectangles_and_dimensions_are_bounded(self):
        for width, height in ((8, 32), (64, 12), (0, 32), (65536, 32), (64, None), (True, 32)):
            with self.subTest(dimensions=(width, height)), self.assertRaises(ValueError):
                ui_font.inspect_font(font(), atlas_width=width, atlas_height=height)
        glyph = struct.pack("<BBbbBBHH", 8, 12, 10, 0, 9, 0, 65535, 1)
        with self.assertRaisesRegex(ValueError, "coordinate bound"):
            ui_font.inspect_font(font(glyph=glyph))

    def test_half_texel_sampling_gutter_is_reported_without_coordinate_clamping(self):
        glyph = struct.pack("<BBbbBBHH", 8, 12, 10, 0, 9, 0, 0, 0)
        result = ui_font.inspect_font(font(glyph=glyph), atlas_width=8, atlas_height=12)
        self.assertEqual(result["glyphs"][0]["s"], 0)
        self.assertEqual(result["sampling_bounds"], [-0.5, -0.5, 8.5, 12.5])
        self.assertEqual(result["needs_sampling_gutter"], [32, 65, 9731])


class LegacyFont(unittest.TestCase):
    def test_old_metrics_with_or_without_exporter_trailer(self):
        for trailer in (False, True):
            result = ui_font.inspect_old_font(old_font(trailer=trailer))
            self.assertEqual(result["glyph_record_bytes"], 80)
            self.assertEqual(result["glyph_count"], 256)
            self.assertEqual(result["max_advance"], 9)
            self.assertEqual(result["max_height"], 12)
            self.assertEqual(result["glyphs"][0]["material"], "fonts/test")
            self.assertEqual(result["trailer"], {"glyph_scale": 1.0, "name": "test"} if trailer else None)

    def test_full_width_material_field_is_bounded_and_not_treated_as_c_string(self):
        result = ui_font.inspect_old_font(old_font(material=b"a" * 32))
        self.assertEqual(result["glyphs"][0]["material"], "a" * 32)
        self.assertFalse(result["glyphs"][0]["material_name_terminated"])

    def test_malformed_legacy_length_metrics_floats_and_trailer_are_rejected(self):
        cases = [old_font()[:-1], old_font() + b"\0"]
        for offset, replacement in ((12, struct.pack("<i", 7)), (28, struct.pack("<f", float("nan"))),
                                    (44, struct.pack("<i", 1))):
            data = bytearray(old_font())
            data[offset:offset + 4] = replacement
            cases.append(bytes(data))
        data = bytearray(old_font(trailer=True))
        struct.pack_into("<f", data, 256 * 80, float("inf"))
        cases.append(bytes(data))
        for data in cases:
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                ui_font.inspect_old_font(data)


class LanguageDictionary(unittest.TestCase):
    def test_ascii_lexer_comments_escapes_and_case_insensitive_native_keys(self):
        result = ui_font.inspect_language(b'// sample\n{\n"#str_Test" "A\\nB\\t\\\"C\\\\"\n"#font_Arial" "Test_Font"\n}\n// end')
        self.assertEqual(result["encoding"], "ascii")
        self.assertEqual(result["entries"]["#str_test"], 'A\nB\t"C\\')
        self.assertEqual(result["font_aliases"], {"#font_arial": "Test_Font"})
        self.assertNotIn(ord("F"), result["codepoints"])
        self.assertEqual(result["entry_count"], 2)

    def test_unicode_requires_native_bom_and_valid_encoding(self):
        body = '{\n"#str_test" "A\u2603"\n}'.encode("utf-8")
        result = ui_font.inspect_language(b"\xef\xbb\xbf" + body)
        self.assertEqual(result["encoding"], "utf-8-bom")
        self.assertEqual(result["codepoints"], [65, 9731])
        for data in (body, b"\xef\xbb\xbf\xff", b"\xff\xfe{}", b'{}\0'):
            with self.subTest(data=data), self.assertRaises(ValueError):
                ui_font.inspect_language(data)

    def test_native_duplicate_dictionary_keys_replace_the_earlier_value(self):
        result = ui_font.inspect_language(b'{"#str_Test" "old"\n"#STR_TEST" "new"}')
        self.assertEqual(result["parsed_entry_count"], 2)
        self.assertEqual(result["entry_count"], 1)
        self.assertEqual(result["duplicate_keys"], ["#str_test"])
        self.assertEqual(result["entries"], {"#str_test": "new"})

    def test_invalid_dictionary_structure_and_escapes_are_rejected(self):
        for data in (b"", b'"a" "b"', b'{"a"}', b'{"a" "b"', b'{"a" "b"}{}',
                     b'{a "b"}', b'{"a" "b\n"}', b'{"a" "b\\r"}', b'{"" "b"}',
                     b'{"a b" "c"}', b'{"a" "\x01"}', b'{/* block */}',
                     b'{"#font_test" ""}', b'{"#font_test" "A\\nB"}'):
            with self.subTest(data=data), self.assertRaises(ValueError):
                ui_font.inspect_language(data)

    def test_dictionary_file_string_and_entry_limits_are_enforced(self):
        with mock.patch.object(ui_font, "MAX_LANGUAGE_BYTES", 2), self.assertRaises(ValueError):
            ui_font.inspect_language(b"{ }")
        with mock.patch.object(ui_font, "MAX_LANGUAGE_STRING", 2), self.assertRaises(ValueError):
            ui_font.inspect_language(b'{"a" "abc"}')
        with mock.patch.object(ui_font, "MAX_LANGUAGE_ENTRIES", 1), self.assertRaises(ValueError):
            ui_font.inspect_language(b'{"a" "a"\n"b" "b"}')

    def test_selected_coverage_resolves_native_redirect_and_omits_retail_text(self):
        language = ui_font.inspect_language(b'{"#str_root" "#STR_LABEL"\n"#str_label" "A B\\nC"}')
        result = ui_font.language_coverage(language, ["#STR_ROOT", "#str_root"], [32, 65, 66])
        self.assertEqual(result["requested_count"], 1)
        self.assertEqual(result["resolved_count"], 1)
        self.assertEqual(result["codepoints"], [32, 65, 66, 67])
        self.assertEqual(result["missing_glyphs"], [67])
        self.assertEqual(result["records"][0]["chain"], ["#str_root", "#str_label"])
        self.assertEqual(result["records"][0]["codepoints"], [10, 32, 65, 66, 67])
        self.assertNotIn("A B", json.dumps(result))

    def test_missing_cycles_and_native_two_lookup_depth_are_reported(self):
        language = ui_font.inspect_language(b'{"#str_a" "#str_b"\n"#str_b" "#str_c"\n'
                                            b'"#str_c" "C"\n"#str_cycle" "#str_cycle"}')
        result = ui_font.language_coverage(language, ["#str_a", "#str_cycle", "#str_absent"])
        self.assertEqual(result["resolved_count"], 0)
        self.assertEqual([record["reason"] for record in result["records"]],
                         ["redirect-depth", "missing-key", "redirect-cycle"])
        self.assertEqual(result["glyph_count"], 0)


if __name__ == "__main__":
    unittest.main()
