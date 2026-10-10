#!/usr/bin/env python3
# ================================================================================================
# File: test_source_inventory.py
# Brief: Preserve upstream manifest coverage while rejecting unreviewed or missing relocated codecs.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("audit_sources", ROOT / "src/tools/scripts/audit_sources.py")
audit_sources = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit_sources)


class SourceInventoryTests(unittest.TestCase):
    def fixture(self, root):
        repository = ROOT
        (root / "src").symlink_to(repository / "src", target_is_directory=True)
        (root / "doomclassic").symlink_to(repository / "doomclassic", target_is_directory=True)
        (root / "config").mkdir()
        for name in ("source_inventory.json", "sources.mk"):
            (root / "config" / name).write_text((repository / "config" / name).read_text())
        return root

    def test_relocated_vendor_units_keep_original_project_coverage(self):
        summary, errors = audit_sources.audit(ROOT)
        self.assertEqual(errors, [])
        self.assertEqual(summary["source_count"], 458)
        self.assertEqual(summary["categories"]["vendor_runtime"], 34)
        self.assertEqual(summary["target_lists"]["VENDOR_C_SRC"], 9)
        self.assertEqual(summary["target_lists"]["VENDOR_CXX_SRC"], 25)

    def test_excluded_codec_cannot_enter_runtime_list(self):
        with tempfile.TemporaryDirectory() as temp:
            root = self.fixture(Path(temp))
            manifest = root / "config/sources.mk"
            manifest.write_text(manifest.read_text().replace("VENDOR_C_SRC =", "VENDOR_C_SRC = external/zlib/gzio.c"))
            _, errors = audit_sources.audit(root)
            self.assertTrue(any("excluded/replaced source" in error for error in errors), errors)
            self.assertTrue(any("reviewed runtime disposition" in error for error in errors), errors)

    def test_lost_relocation_is_detected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = self.fixture(Path(temp))
            path = root / "config/source_inventory.json"
            inventory = json.loads(path.read_text())
            del inventory["source_relocations"]["neo/renderer/jpeg-6/"]
            path.write_text(json.dumps(inventory))
            _, errors = audit_sources.audit(root)
            self.assertTrue(any("Inventory source no longer exists" in error for error in errors), errors)
            self.assertTrue(any("names missing source" in error for error in errors), errors)


if __name__ == "__main__":
    unittest.main()
