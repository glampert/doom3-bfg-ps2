#!/usr/bin/env python3
# ================================================================================================
# File: test_ui_retail_catalog.py
# Brief: Check the bounded deterministic English catalog using only authored source assets.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import copy
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "src/tools/scripts"))
import gstx_fixture
import ui_retail_catalog as catalog
from ui_font import inspect_font


class RetailCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.directory = Path(cls.temporary.name)
        cls.font_raw = (ROOT / "src/tests/data/ui/base/newfonts/ps2_ascii/48.dat").read_bytes()
        font_hash = hashlib.sha256(cls.font_raw).hexdigest()
        cls.font_asset = {"path": "assets/" + font_hash + ".dat", "bytes": len(cls.font_raw), "sha256": font_hash}
        (cls.directory / "assets").mkdir()
        (cls.directory / cls.font_asset["path"]).write_bytes(cls.font_raw)
        cls.image_raw = gstx_fixture.alpha_texture(512, 128, bytes([255]) * 512 * 128)
        cls.image_asset = gstx_fixture.write_asset(cls.directory, cls.image_raw)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        details = inspect_font(self.font_raw, atlas_width=512, atlas_height=128)
        details["legacy_layout_metrics"] = [{"point_size": point_size,
                                            "scaled_max_advance": advance, "scaled_max_height": height}
                                           for point_size, advance, height in ((12, 70, 60), (24, 55, 54), (48, 43, 49))]
        self.image_name = "generated/images/newfonts/ps2_ascii/48#__0400.bimage"
        self.font_name = "newfonts/ps2_ascii/48.dat"
        self.manifest = {"mode": "cook", "supported_languages": ["english"], "runtime_ready": False,
                         "issues": [], "cooked": {"runtime_images": {self.image_name: self.image_asset},
                                                    "assets": {self.font_name: self.font_asset}},
                         "image_resolutions": [{"logical_image": "newfonts/ps2_ascii/48.tga",
                                                "selected": self.image_name, "status": "resolved"}],
                         "fonts": {self.font_name: details},
                         "font_resolutions": [{"requested": "PS2 ASCII", "family": "ps2_ascii",
                                               "alias_chain": ["PS2 ASCII", "PS2_ASCII"]}],
                         "language": {"font_aliases": {"#font_": "PS2 ASCII", "#font_an": "PS2 ASCII"}},
                         "materials": {"requested": {"newfonts/ps2_ascii/48.tga": {
                             "definition": "implicit-image-default", "map_resources": ["newfonts/ps2_ascii/48"],
                             "stage_count": 1}, "_white": {"definition": "implicit-image-default"}}}}

    def build(self):
        return catalog.build_catalog(self.manifest, self.directory)

    @staticmethod
    def string(raw, header, offset):
        begin = header[13] + offset
        return raw[begin:raw.index(b"\0", begin)].decode("ascii")

    def test_wire_layout_counts_original_font_and_independent_legacy_metrics(self):
        raw, summary = self.build()
        header = catalog.HEADER.unpack_from(raw)
        descriptor_bytes = self.image_asset["table_bytes"]
        self.assertEqual(descriptor_bytes, 48 + 32 + 16 + 32)
        self.assertEqual(header[:8], (catalog.MAGIC, 2, len(raw), 1, 1, 1, 5, 1))
        self.assertEqual(header[8], 64)
        self.assertEqual(header[9], header[8] + 32)
        self.assertEqual(header[10], header[9] + 56)
        self.assertEqual(header[11], header[10] + 5 * 8)
        self.assertEqual(header[12], header[11] + 8)
        self.assertEqual(header[13], header[12] + descriptor_bytes)
        self.assertEqual(header[13] + header[14], len(raw))
        self.assertEqual(header[15], 0)
        image = catalog.IMAGE.unpack_from(raw, header[8])
        self.assertEqual(self.string(raw, header, image[0]), self.image_name)
        self.assertEqual(self.string(raw, header, image[1]), self.image_asset["path"])
        self.assertEqual(image[2:5], (512, 128, self.image_asset["bytes"]))
        self.assertEqual(raw[image[5]:image[5] + image[6]], self.image_raw[:descriptor_bytes])
        font = catalog.FONT.unpack_from(raw, header[9])
        self.assertEqual(self.string(raw, header, font[0]), "ps2_ascii")
        self.assertEqual(self.string(raw, header, font[1]), self.font_name)
        self.assertEqual(self.string(raw, header, font[2]), self.font_asset["path"])
        self.assertEqual(font[3:6], (len(self.font_raw), catalog.fnv1a32(self.font_raw), 0))
        self.assertEqual(font[6:12], (70, 60, 55, 54, 43, 49))
        self.assertEqual(font[12:], (0, 0))
        self.assertEqual((summary["format"], summary["page_count"], summary["descriptor_bytes"]),
                         ("UICT2", 1, descriptor_bytes))
        self.assertEqual(summary["material_policy"],
                         "implicit-image-default only; native wrappers provide one constant image stage")
        self.assertNotIn(b".bswf", raw)

    def test_aliases_keep_case_space_normalization_and_empty_english_alias(self):
        raw, _ = self.build()
        header = catalog.HEADER.unpack_from(raw)
        aliases = {}
        for index in range(header[6]):
            string, font = catalog.ALIAS.unpack_from(raw, header[10] + index * 8)
            aliases[self.string(raw, header, string)] = font
        self.assertEqual(aliases, {"": 0, "PS2 ASCII": 0, "PS2_ASCII": 0, "an": 0, "ps2_ascii": 0})

    def test_deterministic_under_input_dictionary_and_resolution_order(self):
        expected = self.build()
        self.manifest["materials"]["requested"] = dict(reversed(list(self.manifest["materials"]["requested"].items())))
        self.manifest["language"]["font_aliases"] = dict(reversed(list(self.manifest["language"]["font_aliases"].items())))
        self.manifest["font_resolutions"][0]["alias_chain"].reverse()
        self.assertEqual(self.build(), expected)

    def test_explicit_materials_omitted_and_recorded_without_authorizing_semantics(self):
        material = self.manifest["materials"]["requested"]["newfonts/ps2_ascii/48.tga"]
        material["definition"] = "explicit"
        material["stage_count"] = 99
        raw, summary = self.build()
        self.assertEqual(catalog.HEADER.unpack_from(raw)[7], 0)
        self.assertEqual(summary["blockers"], [
            "Explicit material declarations are omitted from target bindings; their semantics need separate admission."])
        self.assertEqual(summary["omitted_materials"], [{
            "name": "newfonts/ps2_ascii/48", "definition": "explicit",
            "reason": "Only implicit image-default bindings are admitted; source declaration semantics remain unavailable."}])

    def test_retail_movie_activation_other_languages_and_issues_rejected(self):
        for key, value in (("runtime_ready", True), ("mode", "inventory"),
                           ("supported_languages", ["english", "french"]), ("issues", [{"kind": "missing"}])):
            with self.subTest(key=key):
                original = self.manifest[key]
                self.manifest[key] = value
                with self.assertRaisesRegex(ValueError, "English cook"):
                    self.build()
                self.manifest[key] = original

    def test_paths_lengths_hashes_and_font_metrics_checked(self):
        for section, field, value in (("image", "path", "../escape.gstx"),
                                      ("image", "bytes", 10),
                                      ("image", "logical_width", 256),
                                      ("font", "path", "assets/" + "0" * 64 + ".dat"),
                                      ("font", "bytes", catalog.MAX_FONT_BYTES + 1)):
            with self.subTest(section=section, field=field):
                manifest = copy.deepcopy(self.manifest)
                entry = (manifest["cooked"]["runtime_images"][self.image_name] if section == "image" else
                         manifest["cooked"]["assets"][self.font_name])
                entry[field] = value
                with self.assertRaises(ValueError):
                    catalog.build_catalog(manifest, self.directory)
        for value in (0, -1, 4097, True):
            with self.subTest(value=value):
                manifest = copy.deepcopy(self.manifest)
                manifest["fonts"][self.font_name]["legacy_layout_metrics"][0]["scaled_max_advance"] = value
                with self.assertRaisesRegex(ValueError, "integer"):
                    catalog.build_catalog(manifest, self.directory)

    def test_missing_font_atlas_alias_and_ambiguous_material_fail_closed(self):
        for mutation in (lambda value: value["image_resolutions"].clear(),
                         lambda value: value["language"]["font_aliases"].update({"#font_x": "missing"}),
                         lambda value: value["materials"]["requested"]["newfonts/ps2_ascii/48.tga"].update(
                             {"map_resources": ["missing/image"]})):
            manifest = copy.deepcopy(self.manifest)
            mutation(manifest)
            with self.assertRaises(ValueError):
                catalog.build_catalog(manifest, self.directory)

    def test_changed_texture_bytes_and_strict_admission_refuse_the_catalog(self):
        source = self.directory / self.image_asset["path"]
        changed = bytearray(self.image_raw)
        changed[-1] ^= 1
        try:
            source.write_bytes(changed)
            with self.assertRaisesRegex(ValueError, "SHA256"):
                self.build()
        finally:
            source.write_bytes(self.image_raw)
        # Content-addressed corruptions reach the strict reader: header, reserved, page record and pixels.
        page_offset = struct.unpack_from("<I", self.image_raw, 48 + 8)[0]
        for offset in (0, 8, 32, 48, page_offset):
            corrupted = bytearray(self.image_raw)
            corrupted[offset] ^= 0x10
            manifest = copy.deepcopy(self.manifest)
            manifest["cooked"]["runtime_images"][self.image_name] = {
                **self.image_asset, **gstx_fixture.store(self.directory, bytes(corrupted))}
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, "GSTX"):
                catalog.build_catalog(manifest, self.directory)

    def test_asset_symlink_and_corrupted_font_rejected(self):
        source = self.directory / self.font_asset["path"]
        original = source.read_bytes()
        try:
            source.write_bytes(bytes([original[0] ^ 1]) + original[1:])
            with self.assertRaisesRegex(ValueError, "SHA256"):
                self.build()
        finally:
            source.write_bytes(original)
        alternative = self.directory / "other.dat"
        alternative.write_bytes(original)
        try:
            source.unlink()
            source.symlink_to(alternative)
            with self.assertRaisesRegex(ValueError, "unsafe"):
                self.build()
        finally:
            source.unlink()
            source.write_bytes(original)
            alternative.unlink()

    def test_catalog_caps_and_canonical_source_names(self):
        for target, values in (("runtime_images", {"bad/../image": self.image_asset}),
                               ("runtime_images", {"image/" + str(index): self.image_asset
                                                   for index in range(catalog.MAX_IMAGES + 1)})):
            manifest = copy.deepcopy(self.manifest)
            manifest["cooked"][target] = values
            with self.subTest(count=len(values)), self.assertRaises(ValueError):
                catalog.build_catalog(manifest, self.directory)
        manifest = copy.deepcopy(self.manifest)
        manifest["fonts"] = {"newfonts/f" + str(index) + "/48.dat": next(iter(manifest["fonts"].values()))
                             for index in range(catalog.MAX_FONTS + 1)}
        with self.assertRaisesRegex(ValueError, "count"):
            catalog.build_catalog(manifest, self.directory)


if __name__ == "__main__":
    unittest.main()
