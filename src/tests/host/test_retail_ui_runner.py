#!/usr/bin/env python3
# ================================================================================================
# File: test_retail_ui_runner.py
# Brief: Reject stale retail proof logs, unsafe staging, changed resources and unreadable captures.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/scripts"))
import retail_ui_capture
import run_retail_ui as runner
import stage_retail_ui as staging
import ui_retail_catalog
from ui_font import inspect_font
from ui_pages import write_pages
from ui_page_package import write_page_package


def synthetic_package(directory):
    directory.mkdir(parents=True)
    (directory / "assets").mkdir()
    glyphs = []
    codepoints = sorted(set(map(ord, retail_ui_capture.PHRASE)))
    rgba = bytearray(bytes((255, 255, 255, 0)) * 256 * 32)
    for index, _ in enumerate(codepoints):
        left = index * 28 + 1
        glyphs.append(struct.pack("<BBbbBxHH", 24, 24, 24, 0, 28, left, 1))
        for y in range(1, 13):
            for x in range(left, left + 12):
                rgba[(y * 256 + x) * 4 + 3] = 255
    font = (struct.pack(">Ihhhh", 0x6964662A, 48, 24, 0, len(codepoints)) + b"".join(glyphs) +
            struct.pack("<" + "I" * len(codepoints), *codepoints))
    font_hash = hashlib.sha256(font).hexdigest()
    font_asset = {"path": "assets/" + font_hash + ".dat", "bytes": len(font), "sha256": font_hash}
    (directory / font_asset["path"]).write_bytes(font)
    image_asset = write_page_package(write_pages(256, 32, bytes(rgba), directory), directory)
    details = inspect_font(font, atlas_width=256, atlas_height=32)
    details["legacy_layout_metrics"] = [{"point_size": size, "scaled_max_advance": width, "scaled_max_height": height}
                                        for size, width, height in ((12, 48, 56), (24, 42, 48), (48, 40, 48))]
    image_name = "generated/images/newfonts/proof/48#__0400.bimage"
    manifest = {"mode": "cook", "supported_languages": ["english"], "runtime_ready": False, "issues": [],
                "fonts": {"newfonts/proof/48.dat": details},
                "font_resolutions": [{"requested": "Proof", "family": "proof", "alias_chain": ["proof"]}],
                "language": {"font_aliases": {"#font_": "Proof"}},
                "image_resolutions": [{"logical_image": "newfonts/proof/48.tga", "selected": image_name, "status": "resolved"}],
                "materials": {"requested": {"newfonts/proof/48.tga": {"definition": "implicit-image-default",
                                                                          "map_resources": ["newfonts/proof/48"], "stage_count": 1}}},
                "cooked": {"runtime_images": {image_name: image_asset}, "assets": {"newfonts/proof/48.dat": font_asset}}}
    raw, summary = ui_retail_catalog.build_catalog(manifest, directory)
    digest = hashlib.sha256(raw).hexdigest()
    descriptor = {**summary, "path": "assets/" + digest + ".uic", "bytes": len(raw), "sha256": digest}
    (directory / descriptor["path"]).write_bytes(raw)
    manifest["cooked"]["runtime_catalog"] = descriptor
    (directory / "manifest.json").write_text(json.dumps(manifest))
    return manifest


def completed(manifest, standard="ntsc"):
    text = (f"game initialized.\nLOOP ready;\nrun_begin \nrun_ready \n[D3BFG] GS ready {standard.upper()}\n"
            "[D3BFG] UI adapters ready\n")
    for index, (name, cycle) in enumerate((("retail-f0.tga", 1), ("retail-return.tga", 1), ("retail-reload.tga", 2))):
        frame = (index + 1) * 120
        text += (f"[D3BFG] RETAIL state=loaded cycle={cycle} font=0 name=proof frames=120 heap=100/120/8\n"
                 f"[D3BFG] RETAIL pages images=1 slots=6 cache=1572864 scratch=262144 metadata=16 "
                 f"loads=2 hits={frame} evictions=0 triangles={frame * 10}\n")
        for scale_index, (width, height) in enumerate(((12, 14), (21, 24), (40, 48))):
            text += f"[D3BFG] RETAIL metrics font=0 scale={scale_index} width={width:.2f} height={height:.2f}\n"
        text += f"[D3BFG] CONSOLE capture={name} frame={frame} presents={frame} redraws=0\n"
        if index == 1:
            text += "[D3BFG] RETAIL state=unloaded cycle=1 heap=80/96/4\n"
    text += ("[D3BFG] RETAIL state=unloaded cycle=2 heap=80/96/4\nrun_done \n]quit\n"
             "[D3BFG] CONSOLE capture=retail-quit.tga frame=361 presents=361 redraws=0\n"
             "[D3BFG] CONSOLE shutdown complete frames=361 presents=361 redraws=0\n------------ Game Shutdown\n"
             "COMMON stage=system shutdown\nHEADLESS memory baseline=7424/7600/4 final=7424/7600/4 peak=10000/12000 arena=16384\n"
             "BOOT production shutdown complete\n")
    return text


def tga(manifest, source, standard):
    height = 448 if standard == "ntsc" else 512
    rgb = bytearray(bytes(retail_ui_capture.BACKGROUND) * 512 * height)
    for baseline in (120, 220, 350):
        for y in range(int((baseline - 4) * height / 480), int(baseline * height / 480)):
            for x in range(320, 350):
                rgb[(y * 512 + x) * 3:(y * 512 + x) * 3 + 3] = bytes(retail_ui_capture.TEXT)
    for probe in retail_ui_capture.expected_probes(manifest, source, 0, standard):
        offset = (probe["y"] * 512 + probe["x"]) * 3
        rgb[offset:offset + 3] = bytes(probe["expected"])
    bgr = bytearray(len(rgb))
    bgr[0::3], bgr[1::3], bgr[2::3] = rgb[2::3], rgb[1::3], rgb[0::3]
    return struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 512, height, 24, 0x20) + bgr


class RetailProofTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "build/cook"
        self.manifest = synthetic_package(self.source)
        patcher = mock.patch.object(staging, "REPO_ROOT", self.root)
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_staging_admits_only_catalog_native_fonts_and_paged_images(self):
        manifest, assets, catalog = staging.inspect_package(self.source)
        self.assertEqual(manifest, self.manifest)
        self.assertEqual({path.suffix for path, _ in assets}, {".dat", ".uip"})
        self.assertEqual(catalog.suffix, ".uic")
        self.assertNotIn(b".bswf", catalog.read_bytes())

    def test_changed_catalog_font_and_streamed_pixels_refuse_staging(self):
        records = [self.manifest["cooked"]["runtime_catalog"],
                   self.manifest["cooked"]["assets"]["newfonts/proof/48.dat"],
                   next(iter(self.manifest["cooked"]["runtime_images"].values()))]
        for record in records:
            path = self.source / record["path"]
            original = path.read_bytes()
            path.write_bytes(original[:-1] + bytes([original[-1] ^ 1]))
            with self.subTest(extension=path.suffix), self.assertRaisesRegex(ValueError, "changed|mismatch"):
                staging.inspect_package(self.source)
            path.write_bytes(original)

    def test_outside_build_overlaps_and_symlink_inputs_refuse_staging(self):
        with self.assertRaisesRegex(ValueError, "descendants"):
            staging.inspect_package(self.root)
        with self.assertRaisesRegex(ValueError, "overlap"):
            staging.stage_retail("debug", self.source / "output", "ntsc", self.source)
        link = self.root / "build/link"
        link.symlink_to(self.source, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "symlinks"):
            staging.inspect_package(link)

    def test_valid_native_log_preserves_metrics_and_budgets_in_both_standards(self):
        for standard in ("ntsc", "pal"):
            passed, reason = runner.classify(completed(self.manifest, standard), "run", standard, self.manifest)
            self.assertTrue(passed, reason)

    def test_stale_barriers_crashes_and_authored_movie_do_not_pass_retail_acceptance(self):
        log = completed(self.manifest)
        changes = (("run_ready \n", ']echo "run_ready"\n'), ("run_done ", "old_done "),
                   ("BOOT production shutdown complete", ""), ("frames=120", "frames=119"),
                   ("final=7424/7600/4", "final=7424/7600/5"))
        for old, new in changes:
            with self.subTest(old=old):
                self.assertFalse(runner.classify(log.replace(old, new), "run", "ntsc", self.manifest)[0])
        for suffix in ("TLB Miss", "[D3BFG] FATAL unexpected", "[D3BFG] SWF state=loaded"):
            self.assertFalse(runner.classify(log + suffix, "run", "ntsc", self.manifest)[0])

    def test_legacy_metrics_cache_growth_and_reloads_are_not_optional(self):
        log = completed(self.manifest)
        changes = (("width=12.00", "width=11.00"), ("height=24.00", "height=23.00"),
                   ("slots=6", "slots=8"), ("cache=1572864", "cache=2097152"),
                   ("metadata=16", "metadata=32"), ("hits=240", "hits=120"),
                   ("loads=2 hits=360", "loads=3 hits=360"), ("retail-return.tga", "retail-absent.tga"),
                   ("state=unloaded cycle=2", "state=unloaded cycle=3"))
        for old, new in changes:
            with self.subTest(old=old):
                self.assertFalse(runner.classify(log.replace(old, new), "run", "ntsc", self.manifest)[0])

    def test_negative_checks_require_the_specific_guard_before_a_presentation(self):
        expected = {"catalog-header": "retail UI catalog invalid", "font-missing": "retail font missing: proof",
                    "font-mutated": "retail font content invalid: proof", "page-table": "retail UI page profile mismatch: proof",
                    "page-pixels": "native UI paged triangle failed"}
        for case, diagnostic in expected.items():
            log = "run_begin \n]ps2_uiRetail load\n"
            if case == "page-pixels":
                log += "RETAIL state=loaded\n[D3BFG] UI page image rejected: page read or checksum\n"
            log += "[D3BFG] FATAL " + diagnostic + "\n"
            self.assertTrue(runner.classify_negative(log, "run", case)[0])
            for wrong in (log.replace(diagnostic, "out of memory"), log + "BOOT production shutdown complete",
                          log + "TLB Miss", log.replace("run_begin ", "old_begin ")):
                self.assertFalse(runner.classify_negative(wrong, "run", case)[0])

    def test_capture_probes_use_local_source_ink_holes_and_all_three_size_rows(self):
        path = self.root / "capture.tga"
        for standard in ("ntsc", "pal"):
            path.write_bytes(tga(self.manifest, self.source, standard))
            result = retail_ui_capture.verify(path, self.manifest, self.source, 0, standard)
            self.assertGreaterEqual(len(result["checks"]), 9)
            self.assertEqual(len(result["rows"]), 3)

    def test_capture_geometry_or_source_ink_corruption_cannot_be_skipped(self):
        path = self.root / "capture.tga"
        original = tga(self.manifest, self.source, "ntsc")
        for probe in retail_ui_capture.expected_probes(self.manifest, self.source, 0, "ntsc"):
            changed = bytearray(original)
            offset = 18 + (probe["y"] * 512 + probe["x"]) * 3
            changed[offset:offset + 3] = b"\x00\x00\xff"
            path.write_bytes(changed)
            with self.subTest(probe=probe["name"]), self.assertRaisesRegex(ValueError, "expected"):
                retail_ui_capture.verify(path, self.manifest, self.source, 0, "ntsc")


if __name__ == "__main__":
    unittest.main()
