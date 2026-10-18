#!/usr/bin/env python3
# ================================================================================================
# File: test_ui_inventory.py
# Brief: Verify retail pipeline integration, safe paths and atomic publication using authored assets.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "src/tools/scripts"))
import inventory_retail_ui as pipeline


def container(records):
    payload = bytearray()
    table = bytearray(struct.pack(">i", len(records)))
    for name, data in records:
        encoded = name.encode("ascii")
        table.extend(struct.pack("<i", len(encoded)) + encoded + struct.pack(">ii", 12 + len(payload), len(data)))
        payload.extend(data)
    return struct.pack(">Iii", 0xD000000D, 12 + len(payload), len(table)) + payload + table


def image(width, height, color_format=0):
    size = ((width + 3) // 4) * ((height + 3) // 4)
    block = struct.pack("<HHI", 0x07E0, 0, 0)
    return (struct.pack(">q7i", -1, 0x0A4D4942, 1, 7, color_format, width, height, 1) +
            struct.pack(">5i", 0, 0, width, height, size * 8) + block * size)


class InventoryPipelineTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.data = self.root / "gamedata"
        self.data.mkdir()
        self.output = self.root / "build/retail-ui"
        self.config_path = self.root / "config.json"
        self.config = {"schema_version": 1, "scope": "authored integration only",
                       "roots": ["generated/swf/fixture.bswf"],
                       "source_language": "strings/english.lang", "language_ids": ["#str_test"],
                       "material_names": ["_white"], "source_contracts": [],
                       "path_contracts": {"fixture": ["pulse", "missing"]}, "exclusions": ["no native activation"],
                       "page_config": {"extent": 256, "gutter": 1, "pixel_cache_budget_bytes": 2097152,
                                       "resampling": "none"}}
        self.config_path.write_text(json.dumps(self.config))
        self.raw_swf = (ROOT / "src/tests/data/ui/base/generated/swf/ps2_demo.bswf").read_bytes()
        self.raw_font = (ROOT / "src/tests/data/ui/base/newfonts/ps2_ascii/48.dat").read_bytes()
        self.records = [("generated/swf/fixture.bswf", self.raw_swf),
                        ("newfonts/ps2_ascii/48.dat", self.raw_font),
                        ("generated/images/generated/swf/fixture#__0200.bimage", image(4, 4)),
                        ("generated/images/newfonts/ps2_ascii/48#__0400.bimage", image(512, 128, 3)),
                        ("materials/test.mtr", b"generated/swf/fixture { { map generated/swf/fixture } }\n"
                         b"newfonts/ps2_ascii/48 { { map newfonts/ps2_ascii/48 } }\n")]
        self.language = b'{ "#str_test" "LOCALIZED_PAYLOAD_SECRET" "#font_an" "ps2_ascii" }'
        (self.data / "strings").mkdir()
        (self.data / "strings/english.lang").write_bytes(self.language)
        self.save_containers()

    def save_containers(self, ordered=None):
        (self.data / "_common.resources").write_bytes(container(self.records))
        (self.data / "_ordered.resources").write_bytes(container(ordered or []))

    def run_inventory(self, cook=False, output=None):
        return pipeline.build_inventory(self.data, output or self.output, self.config_path, cook, repo_root=self.root)

    def test_inventory_is_sanitized_conservative_and_deterministic(self):
        first = self.run_inventory()
        first_bytes = (self.output / "manifest.json").read_bytes()
        second = self.run_inventory()
        self.assertEqual(first, second)
        self.assertEqual(first_bytes, (self.output / "manifest.json").read_bytes())
        self.assertNotIn(b"LOCALIZED_PAYLOAD_SECRET", first_bytes)
        self.assertNotIn("entries", first["language"])
        self.assertFalse(first["runtime_ready"])
        self.assertFalse(first["dynamic_dependency_closure"])
        self.assertEqual(first["issues"], [])
        self.assertEqual(len(first["swfs"]), 1)
        self.assertEqual(len(first["fonts"]), 1)
        self.assertEqual(len(first["images"]), 2)
        self.assertEqual(first["font_resolutions"][0]["family"], "ps2_ascii")
        self.assertEqual(first["language"]["selected_coverage"]["resolved_count"], 1)
        self.assertEqual(first["path_contracts"]["fixture"], [{"path": "pulse", "present_in_static_expansion": True},
                                                            {"path": "missing", "present_in_static_expansion": False}])
        self.assertFalse((self.output / "assets").exists())

    def test_cook_copies_full_sources_and_lossless_hash_named_pages(self):
        manifest = self.run_inventory(cook=True)
        saved = (self.output / "manifest.json").read_bytes()
        self.assertEqual(self.run_inventory(cook=True), manifest)
        self.assertEqual(saved, (self.output / "manifest.json").read_bytes())
        for name, expected in (("strings/english.lang", self.language), ("generated/swf/fixture.bswf", self.raw_swf),
                               ("newfonts/ps2_ascii/48.dat", self.raw_font)):
            record = manifest["cooked"]["assets"][name]
            self.assertEqual((self.output / record["path"]).read_bytes(), expected)
            self.assertIn(hashlib.sha256(expected).hexdigest(), record["path"])
        files = list((self.output / "assets").iterdir())
        self.assertEqual(manifest["cooked"]["file_count"], len(files))
        self.assertEqual(manifest["cooked"]["storage_bytes"], sum(path.stat().st_size for path in files))
        self.assertEqual(manifest["working_set"]["proposed_resident_pages"], 8)
        for layout in manifest["cooked"]["page_layouts"].values():
            self.assertEqual(layout["page_width"], 256)
            for page in layout["pages"]:
                payload = (self.output / page["path"]).read_bytes()
                self.assertEqual(len(payload), 256 * 256 * 4)
                self.assertEqual(hashlib.sha256(payload).hexdigest(), page["sha256"])

    def test_ordered_duplicate_precedence_keeps_evidence(self):
        replacement = self.raw_swf
        self.save_containers([("generated/swf/fixture.bswf", replacement)])
        manifest = self.run_inventory()
        selected = next(value for value in manifest["inputs"] if value["name"] == "generated/swf/fixture.bswf")
        self.assertEqual(selected["selected"]["container"], "_ordered.resources")
        self.assertEqual(len(selected["copies"]), 2)
        self.assertTrue(selected["duplicate_content_identical"])

    def test_bad_source_and_failed_decode_preserve_prior_manifest_and_assets(self):
        self.run_inventory(cook=True)
        saved = (self.output / "manifest.json").read_bytes()
        assets = {path.name: path.read_bytes() for path in (self.output / "assets").iterdir()}
        self.records[0] = (self.records[0][0], self.raw_swf[:-1])
        self.save_containers()
        with self.assertRaises(ValueError):
            self.run_inventory(cook=True)
        self.assertEqual(saved, (self.output / "manifest.json").read_bytes())
        self.records[0] = (self.records[0][0], self.raw_swf)
        self.records[2] = (self.records[2][0], image(4, 4, 2))
        self.save_containers()
        with self.assertRaisesRegex(ValueError, "unsupported.*color format"):
            self.run_inventory(cook=True)
        self.assertEqual(saved, (self.output / "manifest.json").read_bytes())
        self.assertEqual(assets, {path.name: path.read_bytes() for path in (self.output / "assets").iterdir()})
        self.assertEqual(list(self.output.parent.glob(".ui-retail-*")), [])

    def test_failed_manifest_replace_preserves_all_previous_references(self):
        self.run_inventory(cook=True)
        saved = (self.output / "manifest.json").read_bytes()
        assets = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in (self.output / "assets").iterdir()}
        with patch.object(pipeline.os, "replace", side_effect=OSError("injected publication failure")):
            with self.assertRaisesRegex(OSError, "publication failure"):
                self.run_inventory(cook=True)
        self.assertEqual(saved, (self.output / "manifest.json").read_bytes())
        for name, expected in assets.items():
            self.assertEqual(hashlib.sha256((self.output / "assets" / name).read_bytes()).hexdigest(), expected)

    def test_missing_and_ambiguous_images_are_reported_without_guessing(self):
        self.records[2] = (self.records[2][0].replace("0200", "0100"), image(4, 4))
        self.records.append((self.records[2][0].replace("0100", "0300"), image(4, 4)))
        self.save_containers()
        manifest = self.run_inventory()
        records = [value for value in manifest["image_resolutions"] if "fixture" in value["expected"]]
        self.assertTrue(all(value["status"] == "ambiguous-variants" and value["selected"] is None for value in records))
        self.assertTrue(manifest["issues"])
        saved = (self.output / "manifest.json").read_bytes()
        with self.assertRaisesRegex(ValueError, "unresolved required"):
            self.run_inventory(cook=True)
        self.assertEqual(saved, (self.output / "manifest.json").read_bytes())

    def test_conflicting_materials_and_missing_alias_family_are_explicit(self):
        self.records.append(("materials/conflict.mtr", b"generated/swf/fixture { { map different/image } }"))
        self.save_containers()
        manifest = self.run_inventory()
        self.assertIn("material-definition-conflict", {issue["kind"] for issue in manifest["issues"]})
        (self.data / "strings/english.lang").write_bytes(b'{ "#str_test" "x" "#font_an" "missing_family" }')
        manifest = self.run_inventory()
        self.assertIn("missing-font", {issue["kind"] for issue in manifest["issues"]})
        with self.assertRaisesRegex(ValueError, "unresolved required"):
            self.run_inventory(cook=True)

    def test_output_is_restricted_to_build_and_rejects_symlink_escapes(self):
        for output in (self.root / "outside", self.root / "build", self.data, self.data.parent):
            with self.subTest(output=output), self.assertRaises(ValueError):
                self.run_inventory(output=output)
        (self.root / "build").mkdir(exist_ok=True)
        (self.root / "build/link").symlink_to(self.data, target_is_directory=True)
        with self.assertRaises(ValueError):
            self.run_inventory(output=self.root / "build/link/package")
        # Even a symlink whose target remains inside build is an excluded component.
        (self.root / "build/real").mkdir()
        (self.root / "build/internal").symlink_to(self.root / "build/real", target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "symlink"):
            self.run_inventory(output=self.root / "build/internal/package")

    def test_source_language_escape_and_alias_cycle_fail_before_output(self):
        language = self.data / "strings/english.lang"
        language.unlink()
        outside = self.root / "outside.lang"
        outside.write_bytes(self.language)
        language.symlink_to(outside)
        with self.assertRaisesRegex(ValueError, "escapes"):
            self.run_inventory()
        self.assertFalse(self.output.exists())
        language.unlink()
        language.write_bytes(b'{ "#str_test" "x" "#font_a" "b" "#font_b" "a" }')
        with self.assertRaisesRegex(ValueError, "alias cycle"):
            self.run_inventory()
        self.assertFalse(self.output.exists())

    def test_page_profile_and_schema_are_checked_before_publication(self):
        for key, value in (("schema_version", 2), ("roots", [])):
            changed = {**self.config, key: value}
            self.config_path.write_text(json.dumps(changed))
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.run_inventory()
            self.assertFalse(self.output.exists())
        changed = {**self.config, "page_config": {**self.config["page_config"], "extent": 512}}
        self.config_path.write_text(json.dumps(changed))
        with self.assertRaisesRegex(ValueError, "page profile"):
            self.run_inventory()
        self.assertFalse(self.output.exists())

    def test_case_sensitive_source_contracts_and_normalized_font_aliases(self):
        source = self.root / "src/NativeCase.cpp"
        source.parent.mkdir()
        source.write_bytes(b"native contract source\n")
        self.config["source_contracts"] = ["src/NativeCase.cpp"]
        self.config_path.write_text(json.dumps(self.config))
        manifest = self.run_inventory()
        self.assertEqual(manifest["source_contracts"][0]["sha256"], hashlib.sha256(source.read_bytes()).hexdigest())
        # Native RemapFont queries aliases again after replacing spaces/hyphens.
        self.assertEqual(pipeline._resolve_font("Some Font", {"#font_some_font": "ps2_ascii"})["family"], "ps2_ascii")
        self.assertEqual(pipeline._resolve_font("", {"#font_": "ps2_ascii"})["family"], "ps2_ascii")
        self.assertEqual(pipeline._resolve_font("fonts/Custom Face", {"#font_custom face": "fonts/Alias_Font"})["family"], "alias_font")
        self.assertEqual(pipeline._resolve_font("Arial_Narrow", {"#font_arial_narrow": "Alias_Font"})["family"], "arial_narrow")
        self.assertEqual(pipeline._resolve_font("arial_narrow", {"#font_arial_narrow": "Alias_Font"})["family"], "alias_font")
        self.assertEqual(pipeline._resolve_font("arial_narrow", {"#font_arial_narrow": "Arial_Narrow"})["family"], "arial_narrow")
        with self.assertRaisesRegex(ValueError, "nested font family"):
            pipeline._resolve_font("FONTS/Custom Face", {"#font_custom face": "Alias_Font"})

    def test_config_and_source_inputs_cannot_be_owned_by_output_tree(self):
        self.output.mkdir(parents=True)
        destination = self.output / "manifest.json"
        original = self.config_path.read_bytes()
        destination.write_bytes(original)
        self.config_path = destination
        with self.assertRaisesRegex(ValueError, "overlaps explicit"):
            self.run_inventory(cook=True)
        self.assertEqual(destination.read_bytes(), original)
        self.assertEqual(list(self.output.iterdir()), [destination])
        self.assertEqual(list(self.output.parent.glob(".ui-retail-*")), [])

        self.config_path = self.root / "config.json"
        self.config["source_contracts"] = ["build/retail-ui/manifest.json"]
        self.config_path.write_text(json.dumps(self.config))
        with self.assertRaisesRegex(ValueError, "overlaps explicit"):
            self.run_inventory(cook=True)
        self.assertEqual(destination.read_bytes(), original)
        self.assertEqual(list(self.output.iterdir()), [destination])
        self.assertEqual(list(self.output.parent.glob(".ui-retail-*")), [])

    def test_manifest_cannot_be_hardlinked_to_external_config(self):
        self.output.mkdir(parents=True)
        destination = self.output / "manifest.json"
        original = self.config_path.read_bytes()
        os.link(self.config_path, destination)
        with self.assertRaisesRegex(ValueError, "aliases config/source"):
            self.run_inventory(cook=True)
        self.assertEqual(self.config_path.read_bytes(), original)
        self.assertEqual(destination.read_bytes(), original)
        self.assertEqual(list(self.output.iterdir()), [destination])
        self.assertEqual(list(self.output.parent.glob(".ui-retail-*")), [])

    def test_content_target_cannot_be_hardlinked_to_source_contract(self):
        manifest = self.run_inventory(cook=True)
        original_manifest = (self.output / "manifest.json").read_bytes()
        record = manifest["cooked"]["assets"]["generated/swf/fixture.bswf"]
        source = self.root / "Contract.cpp"
        source.write_bytes(self.raw_swf)
        destination = self.output / record["path"]
        destination.unlink()
        os.link(source, destination)
        self.config["source_contracts"] = ["Contract.cpp"]
        self.config_path.write_text(json.dumps(self.config))
        old_files = {path.name for path in (self.output / "assets").iterdir()}
        with self.assertRaisesRegex(ValueError, "aliases config/source"):
            self.run_inventory(cook=True)
        self.assertEqual((self.output / "manifest.json").read_bytes(), original_manifest)
        self.assertEqual(source.read_bytes(), self.raw_swf)
        self.assertEqual(destination.read_bytes(), self.raw_swf)
        self.assertEqual({path.name for path in (self.output / "assets").iterdir()}, old_files)

    def test_missing_required_localization_refuses_cook_and_preserves_old_package(self):
        self.run_inventory(cook=True)
        saved = (self.output / "manifest.json").read_bytes()
        old_files = {path.name: path.read_bytes() for path in (self.output / "assets").iterdir()}
        (self.data / "strings/english.lang").write_bytes(b'{ "#str_other" "x" "#font_an" "ps2_ascii" }')
        diagnostic = self.run_inventory(output=self.root / "build/diagnostic")
        self.assertIn({"kind": "localization-id-unresolved", "name": "#str_test",
                       "reason": "missing-key", "chain": ["#str_test"]}, diagnostic["issues"])
        with self.assertRaisesRegex(ValueError, "localization-id-unresolved #str_test"):
            self.run_inventory(cook=True)
        self.assertEqual((self.output / "manifest.json").read_bytes(), saved)
        self.assertEqual({path.name: path.read_bytes() for path in (self.output / "assets").iterdir()}, old_files)

    def test_missing_selected_label_glyph_refuses_cook_and_preserves_old_package(self):
        self.run_inventory(cook=True)
        saved = (self.output / "manifest.json").read_bytes()
        old_files = {path.name: path.read_bytes() for path in (self.output / "assets").iterdir()}
        (self.data / "strings/english.lang").write_bytes(b'\xef\xbb\xbf' + '{ "#str_test" "snowman \u2603" "#font_an" "ps2_ascii" }'.encode("utf-8"))
        diagnostic = self.run_inventory(output=self.root / "build/diagnostic")
        self.assertIn({"kind": "selected-language-glyphs-missing", "name": "newfonts/ps2_ascii/48.dat",
                       "codepoints": [0x2603]}, diagnostic["issues"])
        with self.assertRaisesRegex(ValueError, "selected-language-glyphs-missing newfonts/ps2_ascii/48.dat"):
            self.run_inventory(cook=True)
        self.assertEqual((self.output / "manifest.json").read_bytes(), saved)
        self.assertEqual({path.name: path.read_bytes() for path in (self.output / "assets").iterdir()}, old_files)


if __name__ == "__main__":
    unittest.main()
