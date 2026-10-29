#!/usr/bin/env python3
# ================================================================================================
# File: test_source_inventory.py
# Brief: Preserve upstream manifest coverage while rejecting unreviewed sources or removed trees.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]
SPEC = importlib.util.spec_from_file_location("audit_sources", ROOT / "src/tools/scripts/audit_sources.py")
audit_sources = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit_sources)


class SourceInventoryTests(unittest.TestCase):
    def fixture(self, root):
        repository = ROOT
        (root / "src").symlink_to(repository / "src", target_is_directory=True)
        (root / "config").mkdir()
        for name in ("source_inventory.json", "sources.mk"):
            (root / "config" / name).write_text((repository / "config" / name).read_text())
        return root

    def test_removed_codec_trees_keep_original_project_coverage(self):
        summary, errors = audit_sources.audit(ROOT)
        self.assertEqual(errors, [])
        self.assertEqual(summary["source_count"], 393)
        self.assertEqual(summary["upstream_projects"]["external.vcxproj"], 56)
        self.assertEqual(summary["removed_upstream_units"],
                         {"neo/framework/zlib/": 12, "neo/renderer/jpeg-6/": 44})
        self.assertEqual(summary["categories"]["compression_deferred"], 3)

    def test_deferred_compression_cannot_enter_campaign_list(self):
        with tempfile.TemporaryDirectory() as temp:
            root = self.fixture(Path(temp))
            manifest = root / "config/sources.mk"
            manifest.write_text(manifest.read_text().replace("CAMPAIGN_CXX_SRC = \\\n", "CAMPAIGN_CXX_SRC = \\\n\tneo/framework/Zip.cpp \\\n", 1))
            _, errors = audit_sources.audit(root)
            self.assertTrue(any("excluded/replaced source" in error for error in errors), errors)
            self.assertTrue(any("reviewed campaign disposition" in error for error in errors), errors)

    def test_production_list_rejects_test_sources(self):
        with tempfile.TemporaryDirectory() as temp:
            root = self.fixture(Path(temp))
            manifest = root / "config/sources.mk"
            text = manifest.read_text()
            self.assertIn("PS2_CXX_SRC = ", text)
            manifest.write_text(text.replace("PS2_CXX_SRC = ", "PS2_CXX_SRC = tests/smoketests/game_tests_main.cpp ", 1))
            _, errors = audit_sources.audit(root)
            self.assertTrue(any("test source in production" in error for error in errors), errors)

    def test_unrecorded_removed_tree_is_detected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = self.fixture(Path(temp))
            path = root / "config/source_inventory.json"
            inventory = json.loads(path.read_text())
            del inventory["removed_upstream_trees"]["neo/renderer/jpeg-6/"]
            inventory["removed_upstream_trees"]["neo/renderer/png/"] = "fixture"
            path.write_text(json.dumps(inventory))
            _, errors = audit_sources.audit(root)
            self.assertTrue(any("names missing source: neo/renderer/jpeg-6/" in error for error in errors), errors)
            self.assertTrue(any("names no manifest unit: neo/renderer/png/" in error for error in errors), errors)


if __name__ == "__main__":
    unittest.main()
