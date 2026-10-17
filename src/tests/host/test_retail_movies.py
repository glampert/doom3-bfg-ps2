#!/usr/bin/env python3
# ================================================================================================
# File: test_retail_movies.py
# Brief: Check inactive movie staging and rejection guards without proprietary data or an emulator.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "src/tools/scripts"))
import run_retail_movies as runner
import stage_retail_ui as staging
from ui_font import inspect_language, inspect_font
from ui_page_package import fnv1a32
from ui_swf import inspect_swf


class MovieResourcesTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.source = Path(self.temporary.name)
        (self.source / "assets").mkdir()
        self.movie = (ROOT / "assets/ui/base/generated/swf/ps2_demo.bswf").read_bytes()
        self.language = b'{ "#str_swf_campaign" "Campaign" "#str_swf_settings" "Settings" "#str_swf_quit" "Quit" }'
        self.manifest = {"cooked": {"assets": {}}, "swfs": {}, "language": inspect_language(self.language)}
        for name in ("shell", "dialog"):
            logical = "generated/swf/" + name + ".bswf"
            self.add_asset(logical, self.movie)
            self.manifest["swfs"][logical] = inspect_swf(self.movie)
        self.add_asset("strings/english.lang", self.language)

    def add_asset(self, logical, raw):
        digest = hashlib.sha256(raw).hexdigest()
        relative = "assets/" + digest + Path(logical).suffix
        (self.source / relative).write_bytes(raw)
        self.manifest["cooked"]["assets"][logical] = {"path": relative, "bytes": len(raw), "sha256": digest}

    def label_manifest(self):
        font_name = "newfonts/ps2_ascii/48.dat"
        font = (ROOT / "assets/ui/base" / font_name).read_bytes()
        self.add_asset(font_name, font)
        self.manifest["fonts"] = {font_name: inspect_font(font)}
        swf = self.manifest["swfs"]["generated/swf/shell.bswf"]
        requested = next(entry["name"] for entry in swf["dictionary"] if entry["type"] == "font")
        self.manifest["font_resolutions"] = [{"requested": requested, "family": "ps2_ascii"}]
        (self.source / "font.uip").write_bytes((ROOT / "assets/ui/base/textures/ps2_ascii.uipg").read_bytes())
        self.manifest["cooked"]["runtime_images"] = {"font": {"path": "font.uip", "page_count": 3},
            "shell": {"page_count": 1}, "dialog": {"page_count": 1}}
        self.manifest["image_resolutions"] = [{"logical_image": "newfonts/ps2_ascii/48.tga", "selected": "font", "status": "resolved"},
            {"logical_image": "generated/swf/shell.tga", "selected": "shell"},
            {"logical_image": "generated/swf/dialog.tga", "selected": "dialog"}]
        return self.manifest

    def test_source_pixel_probes_reject_blank_or_wrong_color_labels(self):
        manifest = self.label_manifest()
        for standard, height in (("ntsc", 448), ("pal", 512)):
            probes = runner.text_probes(manifest, self.source, 0, standard)
            self.assertEqual({probe["label"] for probe in probes}, {0, 1, 2})
            pixels = bytearray(bytes((16, 24, 40)) * 512 * height)
            for probe in probes:
                offset = (probe["y"] * 512 + probe["x"]) * 3
                pixels[offset:offset + 3] = bytes(probe["expected"])
            path = self.source / "labels.tga"
            header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 512, height, 24, 0x20)
            bgr = bytearray(pixels)
            bgr[0::3], bgr[2::3] = pixels[2::3], pixels[0::3]
            path.write_bytes(header + bgr)
            self.assertTrue(runner.verify_capture(path, manifest, self.source, 0, standard))
            path.write_bytes(header + bytes((40, 24, 16)) * 512 * height)
            with self.assertRaisesRegex(ValueError, "source pixel"):
                runner.verify_capture(path, manifest, self.source, 0, standard)
            bgr[0::3] = bytes(len(pixels) // 3)
            bgr[1::3] = bytes(len(pixels) // 3)
            path.write_bytes(header + bgr)
            with self.assertRaisesRegex(ValueError, "source pixel"):
                runner.verify_capture(path, manifest, self.source, 0, standard)

    def test_native_acceptance_checks_layout_warm_ownership_frames_and_full_cleanup(self):
        manifest = self.label_manifest()
        log = "GS ready NTSC\nrun_ready \n"
        for phase, (name, _, movie) in enumerate(runner.PHASES):
            prototype, rows = runner.expected_labels(manifest, self.source, movie)
            log += (f"RETAIL movie={movie} state=loaded frames=120 dictionary=16 texts=6 preview=10 "
                    f"english=3 scripts=inactive heap=100/120/8\n")
            for index, key, width, font, height in rows:
                log += f"RETAIL label={index} id={key} width={width}.00 font={font} height={height}\n"
            images, metadata = (2, 32) if phase == 0 else (3, 40)
            log += (f"[D3BFG] RETAIL pages images={images} slots=6 cache=1572864 scratch=262144 metadata={metadata} "
                    f"loads=3 hits={(phase + 1) * 100} evictions=0 triangles={(phase + 1) * 200}\n")
            frame = (phase + 1) * 120
            log += f"CONSOLE capture=movie-{name}.tga frame={frame} presents={frame} redraws=0\n"
        log += ("run_done \nCONSOLE capture=movie-quit.tga frame=481 presents=481 redraws=0\n"
                "[D3BFG] CONSOLE shutdown complete frames=481 presents=481 redraws=0\n"
                "HEADLESS memory baseline=7424/7600/4 final=7424/7600/4 peak=100/120 arena=200\n"
                "COMMON stage=system shutdown\nBOOT production shutdown complete\n")
        self.assertTrue(runner.classify(log, "run", "ntsc", manifest, self.source)[0])
        for before, after in (("width=160.00", "width=159.00"), ("final=7424", "final=7425"),
                              ("frames=120", "frames=119"), ("cache=1572864", "cache=3145728"),
                              ("scripts=inactive", "scripts=active")):
            self.assertIn(before, log)
            self.assertFalse(runner.classify(log.replace(before, after), "run", "ntsc", manifest, self.source)[0])

    def test_profile_records_exact_sources_and_dictionary_text_prototype(self):
        raw, assets = staging.movie_resources(self.source, self.manifest)
        words = struct.unpack("<16I", raw)
        self.assertEqual(words[:5], (0x564D4955, 1, 64, 1, 2))
        self.assertEqual(words[5:8], (len(self.language), fnv1a32(self.language), 3))
        self.assertEqual(words[8:12], (len(self.movie), fnv1a32(self.movie), 16, 10))
        self.assertEqual(words[8:12], words[12:16])
        self.assertEqual([name for _, name in assets], ["english.lang", "shell.bswf", "dialog.bswf"])
        self.assertEqual(staging.movie_resources(self.source, self.manifest)[0], raw)

    def test_changed_source_or_stale_inventory_cannot_enter_profile(self):
        asset = self.manifest["cooked"]["assets"]["generated/swf/shell.bswf"]
        path = self.source / asset["path"]
        path.write_bytes(self.movie[:-1] + bytes([self.movie[-1] ^ 1]))
        with self.assertRaisesRegex(ValueError, "changed"):
            staging.movie_resources(self.source, self.manifest)
        path.write_bytes(self.movie)
        self.manifest["swfs"]["generated/swf/shell.bswf"]["counts"]["frames"] += 1
        with self.assertRaisesRegex(ValueError, "differs"):
            staging.movie_resources(self.source, self.manifest)

    def test_language_count_and_duplicate_keys_are_checked(self):
        self.manifest["language"]["entry_count"] += 1
        with self.assertRaisesRegex(ValueError, "dictionary"):
            staging.movie_resources(self.source, self.manifest)
        self.manifest["language"]["entry_count"] -= 1
        self.add_asset("strings/english.lang", b'{ "#str_x" "A" "#str_x" "B" }')
        self.manifest["language"]["entry_count"] = 1
        with self.assertRaisesRegex(ValueError, "dictionary"):
            staging.movie_resources(self.source, self.manifest)

    def test_native_negative_requires_specific_guard_and_no_published_movie(self):
        identity = "movie_test"
        for case, expected in (("movie-count", "retail movie structural preflight failed: shell"),
                               ("english", "retail movie resource checksum mismatch: english")):
            log = f']echo "{identity}_ready"\n{identity}_ready \n]ps2_uiRetail movie shell\n[D3BFG] FATAL {expected}\n'
            self.assertTrue(runner.classify_negative(log, identity, case)[0])
            self.assertFalse(runner.classify_negative(log.replace(expected, "out of memory"), identity, case)[0])
            self.assertFalse(runner.classify_negative(log + "RETAIL movie=0 state=loaded\n", identity, case)[0])
            self.assertFalse(runner.classify_negative(log + "CONSOLE capture=movie-shell1.tga frame=1 presents=1 redraws=0\n", identity, case)[0])

    def test_structural_mutations_recompute_integrity_to_exercise_native_reader(self):
        for case in ("movie-count", "movie-init"):
            output = self.source / case
            directory = output / "base/retail"
            directory.mkdir(parents=True)
            profile, _ = staging.movie_resources(self.source, self.manifest)
            (directory / "movies.uim").write_bytes(profile)
            (directory / "shell.bswf").write_bytes(self.movie)
            runner.mutate(output, case)
            raw = (directory / "shell.bswf").read_bytes()
            self.assertNotEqual(raw, self.movie)
            self.assertEqual(struct.unpack_from("<I", (directory / "movies.uim").read_bytes(), 36)[0], fnv1a32(raw))


if __name__ == "__main__":
    unittest.main()
