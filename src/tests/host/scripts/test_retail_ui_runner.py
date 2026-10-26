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

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/scripts"))
import gstx_fixture
import retail_ui_capture
import run_retail_ui as runner
import stage_retail_ui as staging
import ui_retail_catalog
from ui_font import inspect_font


CHARSET_IMAGE = "generated/images/textures/bigchars#__0200.bimage"


def charset_alpha() -> bytes:
    """A 256x256 console charset with one block glyph per printable byte: a 10x12 slab of ink
    around a 4x4 hole, so every cell offers both probe kinds."""
    alpha = bytearray(256 * 256)
    for code in range(32, 127):
        if code == 32:
            continue
        cell_x, cell_y = (code & 15) * 16, (code >> 4) * 16
        for y in range(2, 14):
            for x in range(3, 13):
                if not (6 <= x < 10 and 6 <= y < 10):
                    alpha[(cell_y + y) * 256 + cell_x + x] = 255
    return bytes(alpha)


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
    image_asset = gstx_fixture.write_asset(directory, gstx_fixture.alpha_texture(256, 32, gstx_fixture.rgba_alpha(rgba)))
    charset_asset = gstx_fixture.write_asset(directory, gstx_fixture.alpha_texture(256, 256, charset_alpha()))
    details = inspect_font(font, atlas_width=256, atlas_height=32)
    details["legacy_layout_metrics"] = [{"point_size": size, "scaled_max_advance": width, "scaled_max_height": height}
                                        for size, width, height in ((12, 48, 56), (24, 42, 48), (48, 40, 48))]
    image_name = "generated/images/newfonts/proof/48#__0400.bimage"
    manifest = {"mode": "cook", "supported_languages": ["english"], "runtime_ready": False, "issues": [],
                "fonts": {"newfonts/proof/48.dat": details},
                "font_resolutions": [{"requested": "Proof", "family": "proof", "alias_chain": ["proof"]}],
                "language": {"font_aliases": {"#font_": "Proof"}},
                "image_resolutions": [{"logical_image": "newfonts/proof/48.tga", "selected": image_name, "status": "resolved"},
                                      {"logical_image": "textures/bigchars", "selected": CHARSET_IMAGE, "status": "resolved"}],
                "materials": {"requested": {"newfonts/proof/48.tga": {"definition": "implicit-image-default",
                                                                          "map_resources": ["newfonts/proof/48"], "stage_count": 1}}},
                "cooked": {"runtime_images": {image_name: image_asset, CHARSET_IMAGE: charset_asset},
                           "assets": {"newfonts/proof/48.dat": font_asset}}}
    raw, summary = ui_retail_catalog.build_catalog(manifest, directory)
    digest = hashlib.sha256(raw).hexdigest()
    descriptor = {**summary, "path": "assets/" + digest + ".uic", "bytes": len(raw), "sha256": digest}
    (directory / descriptor["path"]).write_bytes(raw)
    manifest["cooked"]["runtime_catalog"] = descriptor
    (directory / "manifest.json").write_text(json.dumps(manifest))
    return manifest


def completed(manifest, textures, first_cycle=1):
    """The font step's slice of a session log; earlier screens have already loaded first_cycle - 1 times."""
    text = ""
    phases = (("retail-f0.tga", first_cycle), ("retail-return.tga", first_cycle), ("retail-reload.tga", first_cycle + 1))
    for index, (name, cycle) in enumerate(phases):
        frame = (index + 1) * 120
        text += (f"[D3BFG] RETAIL state=loaded cycle={cycle} font=0 name=proof frames=120 heap=100/120/8\n"
                 f"[D3BFG] RETAIL textures images={textures['images']} pages={textures['pages']} cluts={textures['cluts']} "
                 f"pixels={textures['pixel_bytes']} metadata={textures['metadata_bytes']} triangles={frame * 10} "
                 "uploads=3 evictions=0\n")
        for scale_index, (width, height) in enumerate(((12, 14), (21, 24), (40, 48))):
            text += f"[D3BFG] RETAIL metrics font=0 scale={scale_index} width={width:.2f} height={height:.2f}\n"
        text += f"[D3BFG] CONSOLE capture={name} frame={frame} presents={frame} redraws=0\n"
        if index == 1:
            text += f"[D3BFG] RETAIL state=unloaded cycle={first_cycle} heap=80/96/4\n"
    return text + f"[D3BFG] RETAIL state=unloaded cycle={first_cycle + 1} heap=80/96/4\n"


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


def replace_last(text, old, new):
    head, separator, tail = text.rpartition(old)
    if not separator:
        raise AssertionError("missing log text: " + old)
    return head + new + tail


class RetailProofTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "build/cook"
        self.manifest = synthetic_package(self.source)
        self.textures = runner.font_textures(self.manifest, self.source)
        patcher = mock.patch.object(staging, "REPO_ROOT", self.root)
        patcher.start()
        self.addCleanup(patcher.stop)

    def classify(self, log, warm=frozenset()):
        return runner.classify(log, self.manifest, self.source, warm)

    def records_font_image(self):
        return next(entry["selected"] for entry in self.manifest["image_resolutions"]
                    if entry["logical_image"] == "newfonts/proof/48.tga")

    def test_staging_admits_only_catalog_native_fonts_and_converted_textures(self):
        manifest, assets, catalog = staging.inspect_package(self.source)
        self.assertEqual(manifest, self.manifest)
        self.assertEqual({path.suffix for path, _ in assets}, {".dat", ".gstx"})
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
        # The font atlas plus the console charset, which every visible package opens at startup.
        font_only = runner.resident_textures(self.manifest, self.source, {self.records_font_image()})
        self.assertEqual(font_only, {"images": 1, "pages": 1, "cluts": 1, "pixel_bytes": 20480, "metadata_bytes": 180})
        charset = runner.resident_textures(self.manifest, self.source, {CHARSET_IMAGE})
        self.assertEqual(self.textures, {key: font_only[key] + charset[key] for key in font_only})
        self.assertEqual(self.textures["images"], 2)
        passed, reason = self.classify(completed(self.manifest, self.textures))
        self.assertTrue(passed, reason)
        # The session's earlier screens loaded the retail screen before: cycles count from there.
        passed, reason = self.classify(completed(self.manifest, self.textures, 5))
        self.assertTrue(passed, reason)

    def test_glyph_sets_count_as_font_textures(self):
        manifest = json.loads(json.dumps(self.manifest))
        atlas = next(iter(manifest["cooked"]["runtime_images"].values()))
        manifest["cooked"]["runtime_images"]["newfonts/proof/ps2_glyphs"] = atlas
        manifest["cooked"]["glyph_sets"] = {"newfonts/proof/48.dat": {"image": "newfonts/proof/ps2_glyphs", "sizes": []}}
        textures = runner.font_textures(manifest, self.source)
        self.assertEqual(textures["images"], 3)
        passed, reason = runner.classify(completed(manifest, textures), manifest, self.source)
        self.assertTrue(passed, reason)
        # The target must open the glyph set with its font, and the runner must expect it.
        self.assertFalse(runner.classify(completed(manifest, self.textures), manifest, self.source)[0])

    def test_images_earlier_screens_opened_stay_in_the_counters(self):
        warm = {"newfonts/proof/ps2_warm"}
        manifest = json.loads(json.dumps(self.manifest))
        manifest["cooked"]["runtime_images"]["newfonts/proof/ps2_warm"] = next(iter(manifest["cooked"]["runtime_images"].values()))
        textures = runner.resident_textures(manifest, self.source, runner.resident_images(manifest) | warm)
        self.assertTrue(runner.classify(completed(manifest, textures), manifest, self.source, warm)[0])
        self.assertFalse(runner.classify(completed(manifest, self.textures), manifest, self.source, warm)[0])
        self.assertFalse(runner.classify(completed(manifest, textures), manifest, self.source)[0])

    def test_short_intervals_crashes_and_authored_movie_do_not_pass_retail_acceptance(self):
        log = completed(self.manifest, self.textures)
        changes = (("frames=120", "frames=119"), ("cycle=2 font=0", "cycle=1 font=0"),
                   ("frame=240 presents=240", "frame=240 presents=241"))
        for old, new in changes:
            with self.subTest(old=old):
                self.assertFalse(self.classify(log.replace(old, new))[0])
        for suffix in ("TLB Miss", "[D3BFG] FATAL unexpected", "[D3BFG] SWF state=loaded"):
            self.assertFalse(self.classify(log + suffix)[0])

    def test_legacy_metrics_resident_textures_and_reloads_are_not_optional(self):
        log = completed(self.manifest, self.textures)
        counters = self.textures
        changes = (("width=12.00", "width=11.00"), ("height=24.00", "height=23.00"),
                   (f"images={counters['images']}", f"images={counters['images'] + 1}"),
                   (f"pages={counters['pages']} ", f"pages={counters['pages'] + 1} "),
                   (f"cluts={counters['cluts']} ", f"cluts={counters['cluts'] + 1} "),
                   (f"pixels={counters['pixel_bytes']}", f"pixels={counters['pixel_bytes'] + 1}"),
                   (f"metadata={counters['metadata_bytes']}", f"metadata={counters['metadata_bytes'] + 8}"),
                   ("triangles=2400", "triangles=1200"), ("evictions=0", "evictions=1"),
                   ("uploads=3", "uploads=0"), ("retail-return.tga", "retail-absent.tga"),
                   ("state=unloaded cycle=2", "state=unloaded cycle=3"))
        for old, new in changes:
            with self.subTest(old=old):
                self.assertFalse(self.classify(log.replace(old, new))[0])
        # Only the warm reload must reuse the resident textures; earlier phases may upload more.
        self.assertTrue(self.classify(log.replace("uploads=3", "uploads=2", 1))[0])
        self.assertFalse(self.classify(replace_last(log, "uploads=3", "uploads=4"))[0])
        self.assertFalse(self.classify(replace_last(log, "uploads=3", "uploads=2"))[0])

    def test_negative_checks_require_the_specific_guard_before_a_presentation(self):
        expected = {"catalog-header": ("retail UI catalog invalid", None),
                    "font-missing": ("retail font missing: proof", None),
                    "font-mutated": ("retail font content invalid: proof", None),
                    "texture-table": ("retail UI texture profile mismatch: proof",
                                      "texture length or tables differ from the catalog"),
                    "texture-pixels": ("retail UI texture profile mismatch: proof", "page block, encoding or checksum")}
        for case, (diagnostic, reason) in expected.items():
            if case == "catalog-header":
                # The presentation opens the catalog at startup, before the config echoes the identity.
                log = "[D3BFG] GS ready NTSC\n[D3BFG] UI adapters ready\n"
            else:
                log = "run_begin \n]ps2_uiRetail load\n"
            if reason is not None:
                log += "[D3BFG] UI paletted image rejected: " + reason + "\n"
            log += "[D3BFG] FATAL " + diagnostic + "\n"
            self.assertTrue(runner.classify_negative(log, "run", case)[0], case)
            wrongs = [log.replace(diagnostic, "out of memory"), log + "BOOT production shutdown complete", log + "TLB Miss"]
            if case == "catalog-header":
                wrongs += [log.replace("[D3BFG] UI adapters ready\n", ""),
                           log.replace("[D3BFG] FATAL", "[D3BFG] RETAIL catalog ready bytes=1\n[D3BFG] FATAL")]
            else:
                wrongs += [log.replace("run_begin ", "old_begin "),
                           log.replace("]ps2_uiRetail load\n", "]ps2_uiRetail load\nRETAIL state=loaded\n")]
            if reason is not None:
                wrongs.append(log.replace(reason, "invalid open state or catalog descriptor"))
            for wrong in wrongs:
                self.assertFalse(runner.classify_negative(wrong, "run", case)[0], case)

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
