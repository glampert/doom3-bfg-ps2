#!/usr/bin/env python3
# ================================================================================================
# File: test_ui_gstx.py
# Brief: Check the strict GSTX reader, its sampling and bounds, the runtime counters it predicts
# and the re-export job description the cook hands the converter.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import json
import random
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "src/tools/scripts"))
import gstx_fixture
import ui_gstx
import ui_reexport


def two_placements():
    """A 64x32 image: its left half halved into page texels 0..15, its right half at full size."""
    page = {"width": 128, "height": 64, "format": ui_gstx.PSMT8,
            "data": bytes((x * 7 + y * 13) % 256 for y in range(64) for x in range(128))}
    palette = [(index, 255 - index, index // 2, index) for index in range(256)]
    clut = [palette[ui_gstx.clut_swizzle(index)] for index in range(256)]
    placements = [{"match": (1, 1, 30, 30), "source": (0, 0, 32, 32), "dest": (0, 0, 16, 16), "page": 0, "clut": 0},
                  {"match": (33, 1, 30, 30), "source": (32, 0, 32, 32), "dest": (16, 0, 32, 32), "page": 0, "clut": 0}]
    return ui_gstx.build_texture(64, 32, [page], [clut], placements)


class TextureTests(unittest.TestCase):
    def setUp(self):
        self.raw = two_placements()
        self.texture = ui_gstx.Texture(self.raw)

    def test_round_trip_keeps_every_field_and_the_descriptor_is_the_tables(self):
        texture = self.texture
        self.assertEqual((texture.width, texture.height, texture.table_bytes), (64, 32, 48 + 32 + 16 + 2 * 32))
        self.assertEqual(texture.descriptor(self.raw), self.raw[:texture.table_bytes])
        self.assertEqual(len(texture.pages[0]["data"]), 128 * 64)
        self.assertEqual(texture.placements[1]["dest"], (16, 0, 32, 32))
        self.assertEqual(len(self.raw) % 16, 0)

    def test_every_table_byte_and_any_data_byte_are_checked(self):
        for offset in range(self.texture.table_bytes):
            damaged = bytearray(self.raw)
            damaged[offset] ^= 0x10
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                ui_gstx.Texture(bytes(damaged))
        page_offset = int.from_bytes(self.raw[48 + 8:48 + 12], "little")
        clut_offset = int.from_bytes(self.raw[48 + 32 + 4:48 + 32 + 8], "little")
        for offset, message in ((page_offset + 100, "checksum"), (clut_offset + 5, "CLUT")):
            damaged = bytearray(self.raw)
            damaged[offset] ^= 1
            with self.assertRaisesRegex(ValueError, message):
                ui_gstx.Texture(bytes(damaged))
        for changed in (self.raw + bytes(16), self.raw[:-16]):
            with self.assertRaises(ValueError):
                ui_gstx.Texture(changed)

    def test_overlapping_matches_and_rectangles_outside_their_page_are_refused(self):
        page = {"width": 128, "height": 64, "format": ui_gstx.PSMT8, "data": bytes(128 * 64)}
        clut = [(0, 0, 0, 255)] * 256
        base = {"match": (0, 0, 8, 8), "source": (0, 0, 8, 8), "dest": (0, 0, 8, 8), "page": 0, "clut": 0}
        for placements in ([base, {**base, "match": (4, 4, 8, 8), "source": (0, 0, 16, 16)}],
                           [{**base, "dest": (124, 0, 8, 8)}], [{**base, "dest": (0, 0, 9, 8)}],
                           [{**base, "match": (0, 0, 9, 8)}]):
            with self.subTest(placements=placements), self.assertRaises(ValueError):
                ui_gstx.build_texture(16, 16, [page], [clut], placements)

    def test_sampling_follows_each_placement_and_misses_outside_them(self):
        texture = self.texture
        self.assertIsNone(texture.sample(0.5, 0.5))
        self.assertIsNone(texture.sample(32.5, 16.0))
        self.assertEqual(texture.alpha(0, 0), 0)
        # Full-size texel centers read their own texel exactly.
        for x, y in ((40, 5), (50, 20)):
            index = texture.pages[0]["data"][y * 128 + (x - 32 + 16)]
            self.assertEqual(texture.sample(x + 0.5, y + 0.5), (index, 255 - index, index // 2, index))

    def test_alpha_bounds_hold_everywhere_inside_their_rectangle(self):
        texture = self.texture
        generator = random.Random(7)
        for _ in range(200):
            right = generator.random() < 0.5
            x0 = generator.uniform(34 if right else 2, 58 if right else 26)
            y0 = generator.uniform(2, 26)
            x1, y1 = x0 + generator.uniform(0, 3), y0 + generator.uniform(0, 3)
            low, high = texture.alpha_bounds(x0, y0, x1, y1)
            for _ in range(20):
                alpha = texture.sample(generator.uniform(x0, x1), generator.uniform(y0, y1))[3]
                self.assertTrue(low - 1 <= alpha <= high + 1, (x0, y0, x1, y1, low, high, alpha))
        self.assertIsNone(texture.alpha_bounds(0.0, 0.0, 0.5, 0.5))

    def test_bounds_ignore_texels_without_filter_weight(self):
        # A three-texel opaque column at full size stays opaque within one texel of its center.
        alpha = bytearray(32 * 32)
        for y in range(32):
            alpha[y * 32 + 10:y * 32 + 13] = bytes((255, 255, 255))
        texture = ui_gstx.Texture(gstx_fixture.alpha_texture(32, 32, bytes(alpha)))
        self.assertEqual(texture.alpha_bounds(10.5, 10.5, 12.5, 12.5), (255, 255))
        self.assertEqual(texture.alpha_bounds(10.4, 10.5, 12.5, 12.5), (0, 255))

    def test_runtime_stats_mirror_the_target_counters(self):
        stats = ui_gstx.runtime_stats(self.texture)
        # 64x16 CLUT texels plus the page; a 2x1 grid of 32-texel cells with one placement each.
        self.assertEqual(stats, {"pages": 1, "cluts": 1, "pixel_bytes": 64 * 16 * 4 + 128 * 64,
                                 "metadata_bytes": self.texture.table_bytes + 3 * 4 + 2 * 2})


POLICY = json.loads((ROOT / "config/ui_retail.json").read_text())["conversion"]["policy"]


def container(records):
    payload = bytearray()
    table = bytearray(struct.pack(">i", len(records)))
    for name, data in records:
        encoded = name.encode("ascii")
        table.extend(struct.pack("<i", len(encoded)) + encoded + struct.pack(">ii", 12 + len(payload), len(data)))
        payload.extend(data)
    return struct.pack(">Iii", 0xD000000D, 12 + len(payload), len(table)) + payload + table


def dxt1_image(width, height, color565):
    blocks = ((width + 3) // 4) * ((height + 3) // 4)
    return (struct.pack(">q7i", -1, 0x0A4D4942, 1, 7, 0, width, height, 1) +
            struct.pack(">5i", 0, 0, width, height, blocks * 8) + struct.pack("<HHI", color565, 0, 0) * blocks)


class ConverterCommandTests(unittest.TestCase):
    def setUp(self):
        if not ui_reexport.TOOL.is_file():
            self.fail("asset_reexport is not built; run make asset-reexport (make test-host builds it)")
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        records = [("generated/images/flat#__0200.bimage", dxt1_image(64, 32, 0x07E0))]
        (self.root / "_common.resources").write_bytes(container(records))
        (self.root / "_ordered.resources").write_bytes(container([]))

    def run_job(self, job, previews=False):
        path = self.root / "job.json"
        path.write_text(json.dumps(job))
        command = [str(ui_reexport.TOOL), "ui", "--data", str(self.root), "--job", str(path),
                   "--output", str(self.root / "out")]
        if previews:
            command += ["--previews", str(self.root / "previews")]
        return subprocess.run(command, capture_output=True, text=True, timeout=60)

    def job(self, **texture):
        return {"version": 1, "policy": POLICY, "textures": [{"name": "flat", "source": "generated/images/flat#__0200.bimage",
                                                             "output": "flat.gstx", **texture}]}

    def test_job_converts_reports_and_previews_one_texture(self):
        result = self.run_job(self.job(width=64, height=32, placements=[{"match": [1, 1, 30, 30], "source": [0, 0, 32, 32]}]),
                              previews=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        texture = ui_gstx.read_texture(self.root / "out/flat.gstx")
        report = json.loads((self.root / "out/report.json").read_text())
        entry = report["textures"][0]
        self.assertEqual((texture.width, texture.height, entry["logical"], entry["file_bytes"]),
                         (64, 32, [64, 32], (self.root / "out/flat.gstx").stat().st_size))
        self.assertEqual(entry["placements"][0]["dest"], [0, 0, 16, 16])
        self.assertEqual(texture.sample(16.0, 16.0), (0, 255, 0, 255))
        self.assertIsNone(texture.sample(40.0, 16.0))
        for suffix in ("original", "converted"):
            self.assertEqual((self.root / f"previews/flat.{suffix}.png").read_bytes()[:8], b"\x89PNG\r\n\x1a\n")

    def test_invalid_policies_names_and_sources_are_refused(self):
        cases = (({"policy": {**POLICY, "alpha_levels": 1}}, "alpha_levels"),
                 ({"policy": {**POLICY, "t4_max_error": 2.5}}, "t4_max_error"),
                 ({"policy": {**POLICY, "max_page_extent": 200}}, "max_page_extent"),
                 ({"policy": {**POLICY, "colors": 8}}, "unknown policy field"),
                 ({"output": "../flat.gstx"}, "output"), ({"source": "generated/images/missing.bimage"}, "missing"),
                 ({"width": 65}, "dimensions"),
                 ({"placements": [{"match": [0, 0, 70, 8], "source": [0, 0, 70, 8]}]}, "placement"))
        for change, message in cases:
            job = self.job()
            texture = job["textures"][0]
            if "policy" in change:
                job["policy"] = change["policy"]
            else:
                texture.update(change)
            with self.subTest(change=change):
                result = self.run_job(job)
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn(message, result.stderr)


class ReexportJobTests(unittest.TestCase):
    def test_policy_fields_are_exact_bounded_and_typed(self):
        policy = ui_reexport.validate_policy(dict(POLICY))
        for changed, message in (({**policy, "extra": 1}, "exactly"), ({**policy, "scale": True}, "out of range"),
                                 ({**policy, "alpha_levels": 17}, "out of range"),
                                 ({**policy, "alpha_levels": 8.0}, "out of range"),
                                 ({**policy, "group_max_error": 47.5}, "out of range"),
                                 ({**policy, "max_page_extent": 384 + 64}, "multiple of 128"),
                                 ({**policy, "share_palettes": "yes"}, "boolean")):
            with self.subTest(changed=changed), self.assertRaisesRegex(ValueError, message):
                ui_reexport.validate_policy(changed)

    def test_output_names_are_plain_and_unique_per_resource(self):
        self.assertEqual(ui_reexport.output_name("generated/images/guis/swf/shell#__0200.bimage"), "guis_swf_shell.gstx")
        self.assertEqual(ui_reexport.output_name("newfonts/Arial Narrow/48.dat"), "newfonts_arial_narrow_48.dat.gstx")
        with self.assertRaises(ValueError):
            ui_reexport.output_name("x" * 200)

    def test_swf_placements_own_content_and_map_the_packed_block(self):
        swf = {"dictionary": [{"type": "image", "atlas": True, "size": [5, 8], "atlas_offset": [9, 1]},
                              {"type": "image", "atlas": False, "size": [4, 4], "atlas_offset": [0, 0]},
                              {"type": "shape"}]}
        self.assertEqual(ui_reexport.swf_placements(swf), [{"match": [9, 1, 5, 8], "source": [8, 0, 8, 12]}])
        for size in range(1, 40):
            block = ui_reexport._allocation(size)
            self.assertTrue(block % 4 == 0 and block - size >= 2 and block - 4 - size < 2, size)
        swf["dictionary"][0]["atlas_offset"] = [0, 1]
        with self.assertRaisesRegex(ValueError, "border"):
            ui_reexport.swf_placements(swf)

    def test_font_placements_keep_selected_glyphs_with_a_clamped_ring(self):
        glyphs = [{"codepoint": 65, "s": 0, "t": 0, "width": 4, "height": 5},
                  {"codepoint": 66, "s": 10, "t": 2, "width": 3, "height": 3},
                  {"codepoint": 67, "s": 10, "t": 2, "width": 3, "height": 3},
                  {"codepoint": 68, "s": 13, "t": 6, "width": 3, "height": 2},
                  {"codepoint": 65, "s": 30, "t": 0, "width": 2, "height": 2, "selected_for_codepoint": False}]
        placements = ui_reexport.font_placements({"glyphs": glyphs}, {65, 66, 67, 68}, 16, 8)
        self.assertEqual(placements, [{"match": [0, 0, 4, 5], "source": [0, 0, 5, 6]},
                                      {"match": [10, 2, 3, 3], "source": [9, 1, 5, 5]},
                                      {"match": [13, 6, 3, 2], "source": [12, 5, 4, 3]}])
        with self.assertRaisesRegex(ValueError, "outside"):
            ui_reexport.font_placements({"glyphs": glyphs}, {68}, 15, 8)

    def test_empty_glyphs_match_the_ring_their_native_quad_samples(self):
        # A space's quad spans half a texel either side of the glyph; its triangle centroids lie a sixth
        # of a texel either side, so an empty axis matches the whole clamped ring.
        glyphs = [{"codepoint": 32, "s": 6, "t": 3, "width": 0, "height": 0},
                  {"codepoint": 160, "s": 6, "t": 3, "width": 0, "height": 0},
                  {"codepoint": 0x2009, "s": 0, "t": 7, "width": 0, "height": 0},
                  {"codepoint": 124, "s": 10, "t": 1, "width": 0, "height": 4}]
        placements = ui_reexport.font_placements({"glyphs": glyphs}, {32, 160, 0x2009, 124}, 16, 8)
        self.assertEqual(placements, [{"match": [9, 1, 2, 4], "source": [9, 0, 2, 6]},
                                      {"match": [5, 2, 2, 2], "source": [5, 2, 2, 2]},
                                      {"match": [0, 6, 1, 2], "source": [0, 6, 1, 2]}])
        with self.assertRaisesRegex(ValueError, "outside"):
            ui_reexport.font_placements({"glyphs": glyphs}, {32}, 5, 8)

    def test_english_codepoints_add_printable_ascii_and_drop_line_breaks(self):
        swfs = {"a": {"text": {"literal_codepoints": [10, 13, 0x2122]}}}
        codepoints = ui_reexport.english_codepoints({"codepoints": [0xE9, 13]}, swfs)
        self.assertTrue(set(range(32, 127)) <= codepoints)
        self.assertTrue({0xE9, 0x2122} <= codepoints)
        self.assertFalse({10, 13} & codepoints)


if __name__ == "__main__":
    unittest.main()
