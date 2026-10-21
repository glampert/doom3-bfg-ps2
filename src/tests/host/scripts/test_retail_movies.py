#!/usr/bin/env python3
# ================================================================================================
# File: test_retail_movies.py
# Brief: Check movie staging, execution acceptance and guards without retail data or an emulator.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "src/tools/scripts"))
import build_authored_ui
import gstx_fixture
import run_retail_movies as runner
import stage_retail_ui as staging
from ui_font import inspect_language, inspect_font
from ui_page_package import fnv1a32
from ui_swf import Inspector, Reader, inspect_swf


def replace_last(text, old, new):
    head, separator, tail = text.rpartition(old)
    if not separator:
        raise AssertionError("missing log text: " + old)
    return head + new + tail


class MovieResourcesTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.source = Path(self.temporary.name)
        (self.source / "assets").mkdir()
        self.movie = (ROOT / "src/tests/data/ui/base/generated/swf/ps2_demo.bswf").read_bytes()
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
        font = (ROOT / "src/tests/data/ui/base" / font_name).read_bytes()
        self.add_asset(font_name, font)
        self.manifest["fonts"] = {font_name: inspect_font(font)}
        swf = self.manifest["swfs"]["generated/swf/shell.bswf"]
        requested = next(entry["name"] for entry in swf["dictionary"] if entry["type"] == "font")
        self.manifest["font_resolutions"] = [{"requested": requested, "family": "ps2_ascii"}]
        width, height, rgba = build_authored_ui.charset_pixels(font=True)
        textures = {"font": gstx_fixture.alpha_texture(width, height, gstx_fixture.rgba_alpha(rgba)),
                    "shell": gstx_fixture.solid_texture(256, 128, (40, 60, 80, 255)),
                    "dialog": gstx_fixture.solid_texture(128, 64, (80, 60, 40, 255)),
                    "cover": gstx_fixture.solid_texture(384, 192, (10, 20, 30, 255))}
        self.manifest["cooked"]["runtime_images"] = {name: gstx_fixture.write_asset(self.source, raw)
                                                     for name, raw in textures.items()}
        self.manifest["image_resolutions"] = [{"logical_image": "newfonts/ps2_ascii/48.tga", "selected": "font", "status": "resolved"},
            {"logical_image": "generated/swf/shell.tga", "selected": "shell"},
            {"logical_image": "generated/swf/dialog.tga", "selected": "dialog"},
            {"logical_image": "guis/assets/mainmenu/doom3_cover.tga", "selected": "cover"}]
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

    def acceptance_log(self, manifest, execution=False, phases=runner.PHASES):
        log = "GS ready NTSC\nrun_ready \n"
        open_images = {next(row["selected"] for row in manifest["image_resolutions"] if row["logical_image"] == font[:-4] + ".tga")
                       for font in manifest["fonts"]}
        for phase, (name, movie_name, movie) in enumerate(phases):
            prototype, rows = runner.expected_labels(manifest, self.source, movie)
            mode = "active" if execution else "inactive"
            log += (f"RETAIL movie={movie} state=loaded frames=120 dictionary=20 texts=7 preview=10 "
                    f"english=3 scripts={mode} heap=100/120/8\n")
            if execution:
                log += (f"RETAIL execution movie={movie} ticks=120 root=1 sprites=2 texts=3 "
                        "instructions=1000 calls=5 commands=7 depth=2 native_steps=20\n")
                if movie == 0:
                    log += "RETAIL owner screen=0 next=0 carousel_items=1 carousel_slots=5 carousel_view=0 credits=1 callbacks=2\n"
                    log += ("SWF dependency movie=swf/shell.swf kind=function name=refreshCarousel\n"
                            "SWF dependency movie=swf/shell.swf kind=native-callback name=refreshCarousel\n"
                            "SWF dependency movie=swf/shell.swf kind=owner-caller name=options\n"
                            "SWF dependency movie=swf/shell.swf kind=function name=updateCredits\n"
                            "SWF dependency movie=swf/shell.swf kind=native-callback name=updateCredits\n")
                else:
                    log += ("SWF dependency movie=swf/dialog.swf kind=function name=getLocalString\n"
                            "SWF dependency movie=swf/dialog.swf kind=native-callback name=getLocalString\n"
                            "SWF dependency movie=swf/dialog.swf kind=method name=calcNumLines\n"
                            "SWF dependency movie=swf/dialog.swf kind=native-callback name=calcNumLines\n"
                            "SWF dependency movie=swf/dialog.swf kind=function name=getPlatform\n"
                            "SWF dependency movie=swf/dialog.swf kind=native-callback name=getPlatform\n")
            for index, key, width, font, height in rows:
                log += f"RETAIL label={index} id={key} width={width}.00 font={font} height={height}\n"
            open_images.add(next(row["selected"] for row in manifest["image_resolutions"]
                                 if row["logical_image"] == "generated/swf/" + movie_name + ".tga"))
            if execution and movie == 0:
                open_images.add(next(row["selected"] for row in manifest["image_resolutions"]
                                     if row["logical_image"] == "guis/assets/mainmenu/doom3_cover.tga"))
            images = len(open_images)
            counters = runner.texture_counters(runner.resident_textures(manifest, self.source, open_images))
            log += ("[D3BFG] RETAIL textures images={} pages={} cluts={} pixels={} metadata={} ".format(*counters) +
                    f"triangles={(phase + 1) * 200} uploads=3 evictions=0\n")
            frame = (phase + 1) * 120
            log += f"CONSOLE capture=movie-{name}.tga frame={frame} presents={frame} redraws=0\n"
            if execution:
                log += f"RETAIL released movie={movie} images={images} heap=80/100/4\n"
        final = len(phases) * 120 + 1
        log += (f"run_done \nCONSOLE capture=movie-quit.tga frame={final} presents={final} redraws=0\n"
                f"[D3BFG] CONSOLE shutdown complete frames={final} presents={final} redraws=0\n"
                "HEADLESS memory baseline=7424/7600/4 final=7424/7600/4 peak=100/120 arena=200\n"
                "COMMON stage=system shutdown\nBOOT production shutdown complete\n")
        return log

    def test_native_acceptance_checks_layout_warm_ownership_frames_and_full_cleanup(self):
        manifest = self.label_manifest()
        log = self.acceptance_log(manifest)
        self.assertTrue(runner.classify(log, "run", "ntsc", manifest, self.source)[0])
        for before, after in (("width=160.00", "width=159.00"), ("final=7424", "final=7425"),
                              ("frames=120", "frames=119"), ("evictions=0", "evictions=1"),
                              ("scripts=inactive", "scripts=active")):
            self.assertIn(before, log)
            self.assertFalse(runner.classify(log.replace(before, after), "run", "ntsc", manifest, self.source)[0])
        stale_final = log.replace("frame=481 presents=481", "frame=480 presents=480")
        stale_final = stale_final.replace("frames=481 presents=481", "frames=480 presents=480")
        self.assertFalse(runner.classify(stale_final, "run", "ntsc", manifest, self.source)[0])

    def test_movie_textures_match_the_open_converted_images_and_never_upload_again(self):
        manifest = self.label_manifest()
        font = runner.resident_textures(manifest, self.source, {"font"})
        # A 64x16 CLUT texture plus one 512x128 PSMT4 page; 128 table bytes, 16x4 grid cells and 64 items.
        self.assertEqual(font, {"images": 1, "pages": 1, "cluts": 1, "pixel_bytes": 4096 + 32768,
                                "metadata_bytes": 128 + 65 * 4 + 64 * 2})
        for execution in (False, True):
            log = self.acceptance_log(manifest, execution=execution)
            accepted = lambda text: runner.classify(text, "run", "ntsc", manifest, self.source, execution=execution)[0]
            self.assertTrue(accepted(log))
            first = next(runner.TEXTURES_RE.finditer(log))
            pixels, images = "pixels=" + first[4], "images=" + first[1]
            for changed in (log.replace(first[0], first[0].replace(pixels, pixels + "1")),
                            log.replace(first[0], first[0].replace(images, f"images={int(first[1]) + 1}")),
                            log.replace("uploads=3", "uploads=0"), replace_last(log, "uploads=3", "uploads=4"),
                            replace_last(log, "evictions=0", "evictions=1"),
                            replace_last(log, "triangles=800", "triangles=600")):
                with self.subTest(execution=execution, changed=changed):
                    self.assertFalse(accepted(changed))

    def render_log(self, manifest):
        swf = manifest["swfs"]["generated/swf/dialog.bswf"]
        language = inspect_language((self.source / manifest["cooked"]["assets"]["strings/english.lang"]["path"]).read_bytes())
        options = "|".join(language["entries"][key] for key in runner.QUIT_OPTIONS)
        images = runner.font_images(manifest) | {"dialog"}
        counters = runner.texture_counters(runner.resident_textures(manifest, self.source, images))
        textures = "[D3BFG] RETAIL textures images={} pages={} cluts={} pixels={} metadata={}".format(*counters)
        log = "GS ready NTSC\nrun_ready \n"
        frame = 0
        triangles = 0
        for name, frames in runner.RENDER_PHASES:
            log += f"RETAIL drawable movie=1 masks={swf['features']['masks']} strokes=0 screen_blends=0 additive_colors=0\n"
            for kind, dependency in runner.DIALOG_DEPENDENCIES:
                log += f"SWF dependency movie=swf/dialog.swf kind={kind} name={dependency}\n"
            for suffix, waited, commands in (("a", frames, 7), ("b", frames + runner.RENDER_GAP, 9)):
                frame += frames if suffix == "a" else runner.RENDER_GAP
                triangles += 1000
                log += (f"RETAIL movie=1 state=loaded frames={waited} dictionary={len(swf['dictionary'])} "
                        f"texts={swf['dictionary_counts']['edittext']} preview=10 "
                        f"english={manifest['language']['entry_count']} scripts=active heap=100/120/8\n"
                        f"RETAIL execution movie=1 ticks={waited} root=1 sprites=2 texts=3 instructions=1000 calls=5 "
                        f"commands={commands} depth=2 native_steps=20\n"
                        f"RETAIL draw movie=1 full=1 triangles={triangles} flushes=10 masks={triangles // 100} "
                        "peak_masks=2 peak_vertices=300 peak_indexes=600 peak_draws=40\n"
                        "RETAIL dialog owner message=66 type=8 title=#str_01975 text=#str_dlg_confirm_quit "
                        f"options={options} choices=3\n"
                        f"{textures} triangles={triangles} uploads=5 evictions=0\n"
                        f"CONSOLE capture=movie-{name}-{suffix}.tga frame={frame} presents={frame} redraws=0\n")
            log += f"RETAIL released movie=1 images={len(images)} heap=80/100/4\n"
        frame += 1
        log += (f"run_done \nCONSOLE capture=movie-quit.tga frame={frame} presents={frame} redraws=0\n"
                f"[D3BFG] CONSOLE shutdown complete frames={frame} presents={frame} redraws=0\n"
                "HEADLESS memory baseline=7424/7600/4 final=7424/7600/4 peak=100/120 arena=200\n"
                "COMMON stage=system shutdown\nBOOT production shutdown complete\n")
        return log

    def test_dialog_render_requires_resident_textures_masks_owner_and_scripts(self):
        manifest = self.label_manifest()
        self.language += b' { "#str_swf_accept" "Accept" "#str_swf_cancel" "Cancel" "#str_swf_change_game" "Change Game" }'
        self.language = self.language.replace(b' } {', b'')
        self.add_asset("strings/english.lang", self.language)
        manifest["language"] = inspect_language(self.language)
        log = self.render_log(manifest)
        accepted = lambda text: runner.classify_dialog_render(text, "run", "ntsc", manifest, self.source)[0]
        self.assertTrue(accepted(log), runner.classify_dialog_render(log, "run", "ntsc", manifest, self.source)[1])
        first = next(runner.TEXTURES_RE.finditer(log)).group(0)
        pixels = "pixels=" + first.split("pixels=")[1].split()[0]
        texture_rows = [match.group(0) for match in runner.TEXTURES_RE.finditer(log)]
        stalled = log.replace(texture_rows[1], texture_rows[1].replace("triangles=2000", "triangles=1000"))
        for changed in (log.replace(pixels, pixels + "1"), log.replace("images=2 pages", "images=3 pages"),
                        log.replace("uploads=5", "uploads=0"), replace_last(log, "uploads=5", "uploads=6"),
                        replace_last(log, "evictions=0", "evictions=1"), stalled,
                        log.replace("masks=8 strokes=0", "masks=7 strokes=0"),
                        log.replace("options=Accept|Cancel|Change Game", "options=Accept|Cancel|Quit"),
                        replace_last(log, "SWF dependency movie=swf/dialog.swf kind=frame name=rollOn\n", ""),
                        replace_last(log, "RETAIL released movie=1 images=2 heap=80/100/4",
                                     "RETAIL released movie=1 images=2 heap=81/100/4")):
            with self.subTest(changed=changed):
                self.assertFalse(accepted(changed))

    def test_final_capture_requires_complete_native_dimensions_and_a_readable_file(self):
        path = self.source / "movie-quit.tga"
        with self.assertRaises(OSError):
            runner.verify_final_capture(path, "ntsc")
        for standard, height in (("ntsc", 448), ("pal", 512)):
            header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 512, height, 24, 0x20)
            raw = header + bytes(512 * height * 3)
            path.write_bytes(raw)
            self.assertEqual(runner.verify_final_capture(path, standard)["height"], height)
            with self.assertRaisesRegex(ValueError, "dimensions"):
                runner.verify_final_capture(path, "pal" if standard == "ntsc" else "ntsc")
            path.write_bytes(raw[:-1])
            with self.assertRaises(ValueError):
                runner.verify_final_capture(path, standard)

    def test_execution_acceptance_requires_active_scripts_live_hierarchy_and_native_work(self):
        manifest = self.label_manifest()
        log = self.acceptance_log(manifest, execution=True)
        self.assertTrue(runner.classify(log, "run", "ntsc", manifest, self.source, execution=True)[0])
        self.assertFalse(runner.classify(log, "run", "ntsc", manifest, self.source)[0])
        inactive = self.acceptance_log(manifest)
        self.assertFalse(runner.classify(inactive, "run", "ntsc", manifest, self.source, execution=True)[0])
        for before, after in (("scripts=active", "scripts=inactive"), ("ticks=120", "ticks=119"),
                              ("ticks=120", "ticks=121"), ("root=1", "root=0"), ("root=1", "root=65536"),
                              ("frames=120", "frames=121"),
                              ("sprites=2", "sprites=0"), ("sprites=2", "sprites=2049"),
                              ("texts=3", "texts=0"), ("texts=3", "texts=513"),
                              ("depth=2", "depth=0"), ("depth=2", "depth=17"),
                              ("instructions=1000", "instructions=0"), ("calls=5", "calls=0"),
                              ("commands=7", "commands=0"), ("native_steps=20", "native_steps=0"),
                              ("native_steps=20", "native_steps=1"),
                              ("final=7424", "final=7425"),
                              ("carousel_items=1", "carousel_items=0"), ("carousel_slots=5", "carousel_slots=3"),
                              ("credits=1", "credits=0"), ("callbacks=2", "callbacks=1"),
                              ("owner screen=0", "owner screen=1"), ("heap=100/120/8", "heap=121/120/8"),
                              ("heap=100/120/8", "heap=100/16777217/8"), ("heap=100/120/8", "heap=100/120/0")):
            with self.subTest(before=before, after=after):
                self.assertFalse(runner.classify(log.replace(before, after), "run", "ntsc", manifest,
                                                 self.source, execution=True)[0])

    def test_execution_acceptance_rejects_stale_or_wrong_movie_status(self):
        manifest = self.label_manifest()
        log = self.acceptance_log(manifest, execution=True)
        dialog = next(row.group(0) for row in runner.EXECUTION.finditer(log) if row[1] == "1")
        for changed in (log.replace(dialog, ""), log.replace(dialog, dialog.replace("movie=1", "movie=0")),
                        log.replace("RETAIL movie=1 state=loaded", "RETAIL movie=1 state=stale")):
            self.assertFalse(runner.classify(changed, "run", "ntsc", manifest, self.source, execution=True)[0])
        inactive = self.acceptance_log(manifest)
        self.assertFalse(runner.classify(inactive.replace("RETAIL movie=1 state=loaded", "RETAIL movie=1 state=stale"),
                                         "run", "ntsc", manifest, self.source)[0])
        injected = inactive.replace("RETAIL movie=1 state=loaded", dialog + "\nRETAIL movie=1 state=loaded")
        self.assertFalse(runner.classify(injected, "run", "ntsc", manifest, self.source)[0])

    def test_shell_execution_requires_fresh_native_owner_and_releases_it_for_dialog(self):
        manifest = self.label_manifest()
        log = self.acceptance_log(manifest, execution=True)
        owner = list(runner.OWNER.finditer(log))[-1].group(0)
        prefix, suffix = log.rsplit(owner, 1)
        self.assertFalse(runner.classify(prefix + suffix, "run", "ntsc", manifest, self.source, execution=True)[0])
        leaked = log.replace("RETAIL movie=1 state=loaded", owner + "\nRETAIL movie=1 state=loaded")
        self.assertFalse(runner.classify(leaked, "run", "ntsc", manifest, self.source, execution=True)[0])
        inactive = self.acceptance_log(manifest)
        self.assertFalse(runner.classify(owner + "\n" + inactive, "run", "ntsc", manifest, self.source)[0])

    def test_execution_requires_actual_native_callback_traces_in_every_loaded_movie(self):
        manifest = self.label_manifest()
        log = self.acceptance_log(manifest, execution=True)
        accepted = lambda text: runner.classify(text, "run", "ntsc", manifest, self.source, execution=True)[0]
        for movie, kind, name in (("shell", "native-callback", "refreshCarousel"),
                                  ("shell", "native-callback", "updateCredits"),
                                  ("shell", "owner-caller", "options"),
                                  ("dialog", "native-callback", "getLocalString"),
                                  ("dialog", "native-callback", "calcNumLines"),
                                  ("dialog", "native-callback", "getPlatform")):
            line = f"SWF dependency movie=swf/{movie}.swf kind={kind} name={name}\n"
            for changed in (log.replace(line, ""), log.replace(line, line.replace("kind=" + kind, "kind=registered")),
                            log.replace(line, line.replace("swf/" + movie + ".swf", "swf/other.swf"))):
                with self.subTest(movie=movie, kind=kind, name=name):
                    self.assertFalse(accepted(changed))
            prefix, suffix = log.rsplit(line, 1)
            self.assertFalse(accepted(prefix + suffix))

    def test_execution_counters_are_monotonic_until_each_movie_is_reloaded(self):
        manifest = self.label_manifest()
        log = self.acceptance_log(manifest, execution=True)
        final = next(runner.EXECUTION.finditer(log)).group(0)
        earlier = final.replace("ticks=120", "ticks=60").replace("instructions=1000", "instructions=500")
        log = log.replace(final, earlier + "\n" + final)
        self.assertTrue(runner.classify(log, "run", "ntsc", manifest, self.source, execution=True)[0])
        for before, after in (("ticks=60", "ticks=121"), ("instructions=500", "instructions=1001")):
            self.assertFalse(runner.classify(log.replace(before, after), "run", "ntsc", manifest,
                                             self.source, execution=True)[0])

    def test_dialog_execution_proves_three_sustained_reloads_without_shell_admission(self):
        manifest = self.label_manifest()
        log = self.acceptance_log(manifest, execution=True, phases=runner.DIALOG_PHASES)
        accepted = lambda text: runner.classify(text, "run", "ntsc", manifest, self.source,
                                               execution=True, phases=runner.DIALOG_PHASES)[0]
        self.assertTrue(accepted(log))
        self.assertNotIn("RETAIL movie=0", log)
        self.assertIn("frames=361", log)
        for before, after in (("movie-dialog2.tga", "movie-shell2.tga"),
                              ("ticks=120", "ticks=119"), ("scripts=active", "scripts=inactive"),
                              ("RETAIL execution movie=1", "RETAIL execution movie=0"),
                              ("images=2", "images=3"), ("final=7424", "final=7425")):
            with self.subTest(before=before):
                self.assertFalse(accepted(log.replace(before, after)))
        prefix, suffix = log.rsplit("heap=100/120/8", 1)
        self.assertTrue(accepted(prefix + "heap=101/120/8" + suffix))
        self.assertFalse(runner.classify(log, "run", "ntsc", manifest, self.source, execution=True)[0])

    def test_execution_releases_exact_warm_ownership_despite_different_live_animation_allocations(self):
        manifest = self.label_manifest()
        log = self.acceptance_log(manifest, execution=True)
        accepted = lambda text: runner.classify(text, "run", "ntsc", manifest, self.source, execution=True)[0]
        prefix, suffix = log.rsplit("heap=100/120/8", 1)
        varied = prefix + "heap=11128409/11828856/15842" + suffix
        self.assertTrue(accepted(varied))
        final = list(runner.RELEASED.finditer(log))[-1].group(0)
        prefix, suffix = log.rsplit(final, 1)
        for changed in ("", final + "\n" + final, final.replace("movie=0", "movie=1"),
                        final.replace("images=4", "images=3"), final.replace("heap=80", "heap=81"),
                        final.replace("/100/", "/101/"), final.replace("/4", "/5"),
                        final.replace("heap=80/100/4", "heap=101/100/4"),
                        final.replace("heap=80/100/4", "heap=80/16777217/4"),
                        final.replace("heap=80/100/4", "heap=80/100/0")):
            with self.subTest(release=changed):
                self.assertFalse(accepted(prefix + changed + suffix))
        moved = log.replace(final, "", 1)
        moved = moved.replace("CONSOLE capture=movie-shell1.tga", final + "\nCONSOLE capture=movie-shell1.tga")
        self.assertFalse(accepted(moved))

    def test_dialog_reload_tolerance_is_limited_to_measured_cold_final_glyph_edge(self):
        manifest = self.label_manifest()
        x1, x2, row = runner.dialog_rounding_row(manifest, self.source)
        self.assertGreaterEqual(x2 - x1, 5)
        baseline = bytes((16, 24, 40)) * 512 * 448
        header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 512, 448, 24, 0x20)

        def variant(count=4, delta=25, x=x1, y=row):
            pixels = bytearray(baseline)
            for index in range(count):
                pixels[(y * 512 + x + index) * 3] += delta
            return pixels

        def compare(first, second=baseline, third=baseline, standard="ntsc", phases=runner.DIALOG_PHASES):
            captures = {}
            for (name, _, _), rgb in zip(phases, (first, second, third)):
                bgr = bytearray(rgb)
                bgr[0::3], bgr[2::3] = rgb[2::3], rgb[0::3]
                path = self.source / ("movie-" + name + ".tga")
                path.write_bytes(header + bgr)
                captures[path.name] = {"sha256": runner.sha256(path)}
            return runner.verify_reload_captures(self.source, phases, captures, manifest, self.source, standard)

        self.assertTrue(compare(baseline)["1"]["identical"])
        proof = compare(variant())["1"]
        self.assertEqual((proof["changed_pixels"], proof["maximum_channel_delta"]), (4, 25))
        self.assertTrue(proof["warm_identical"])
        for changed in (variant(count=5), variant(delta=26), variant(y=row - 1), variant(x=x1 - 1)):
            with self.assertRaisesRegex(ValueError, "cold glyph edge tolerance"):
                compare(changed)
        with self.assertRaisesRegex(ValueError, "changed across"):
            compare(variant(), third=variant(count=1))
        with self.assertRaisesRegex(ValueError, "changed across"):
            compare(variant(), standard="pal")
        shell_phases = tuple(("shell" + str(index), "shell", 0) for index in (1, 2, 3))
        with self.assertRaisesRegex(ValueError, "changed across"):
            compare(variant(), phases=shell_phases)

    def test_profile_records_exact_sources_and_dictionary_text_prototype(self):
        raw, assets = staging.movie_resources(self.source, self.manifest)
        words = struct.unpack("<16I", raw)
        self.assertEqual(words[:5], (0x564D4955, 1, 64, 1, 2))
        self.assertEqual(words[5:8], (len(self.language), fnv1a32(self.language), 3))
        self.assertEqual(words[8:12], (len(self.movie), fnv1a32(self.movie), 20, 10))
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
                               ("movie-placement", "retail movie execution preflight failed: shell"),
                               ("movie-opcode", "retail movie execution preflight failed: shell"),
                               ("movie-branch", "retail movie execution preflight failed: shell"),
                               ("movie-operand", "retail movie execution preflight failed: shell"),
                               ("movie-budget", "retail SWF execution rejected (swf/shell.swf): instruction budget"),
                               ("english", "retail movie resource checksum mismatch: english")):
            command = "execute" if case in runner.EXECUTION_NEGATIVES else "movie"
            log = f']echo "{identity}_ready"\n{identity}_ready \n]ps2_uiRetail {command} shell\n[D3BFG] FATAL {expected}\n'
            self.assertTrue(runner.classify_negative(log, identity, case)[0])
            self.assertFalse(runner.classify_negative(log.replace(expected, "out of memory"), identity, case)[0])
            self.assertFalse(runner.classify_negative(log + "RETAIL movie=0 state=loaded\n", identity, case)[0])
            self.assertFalse(runner.classify_negative(log + "RETAIL movie=1 state=loaded\n", identity, case)[0])
            self.assertFalse(runner.classify_negative(log + "CONSOLE capture=movie-shell1.tga frame=1 presents=1 redraws=0\n", identity, case)[0])
            self.assertFalse(runner.classify_negative(log + "CONSOLE capture=unexpected.tga frame=1 presents=1 redraws=0\n", identity, case)[0])
            if case == "movie-budget":
                self.assertFalse(runner.classify_negative(log.replace("(swf/shell.swf)", "(swf/dialog.swf)"), identity, case)[0])

    def test_structural_mutations_recompute_integrity_to_exercise_native_reader(self):
        for case in ("movie-count", "movie-init") + runner.EXECUTION_NEGATIVES:
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

    def test_execution_mutations_change_the_intended_placement_or_action_operand(self):
        commands = runner.root_commands(self.movie)
        action = next(row for row in commands if row[0] == 12)
        placement = next(row for row in commands if row[0] == 26)
        self.assertGreater(action[2], 5)
        for case, expected in (("movie-opcode", b"\xff"), ("movie-branch", b"\x99\x02\x00\xff\x7f"),
                               ("movie-operand", b"\x96\x01\x00\x00"), ("movie-placement", b"\x86")):
            output = self.source / case
            directory = output / "base/retail"
            directory.mkdir(parents=True)
            profile, _ = staging.movie_resources(self.source, self.manifest)
            (directory / "movies.uim").write_bytes(profile)
            (directory / "shell.bswf").write_bytes(self.movie)
            runner.mutate(output, case)
            mutated = (directory / "shell.bswf").read_bytes()
            offset = placement[1] if case == "movie-placement" else action[1]
            self.assertEqual(mutated[offset:offset + len(expected)], expected)
            self.assertEqual(mutated[:offset], self.movie[:offset])
            self.assertEqual(mutated[offset + len(expected):], self.movie[offset + len(expected):])

    def test_instruction_budget_mutation_is_valid_backward_branch_not_a_parser_failure(self):
        directory = self.source / "budget/base/retail"
        directory.mkdir(parents=True)
        profile, _ = staging.movie_resources(self.source, self.manifest)
        (directory / "movies.uim").write_bytes(profile)
        (directory / "shell.bswf").write_bytes(self.movie)
        runner.mutate(self.source / "budget", "movie-budget")
        mutated = (directory / "shell.bswf").read_bytes()
        self.assertEqual(len(mutated), len(self.movie))
        inventory = inspect_swf(mutated)
        self.assertEqual(inventory["scripts"]["opcodes"]["Jump"], 1)
        self.assertEqual(inventory["dictionary_counts"], self.manifest["swfs"]["generated/swf/shell.bswf"]["dictionary_counts"])

    def test_native_callback_alias_expands_a_valid_root_action_and_updates_profile_lengths(self):
        minimal = runner.replace_root_action(self.movie, b"\x07\0")
        logical = "generated/swf/shell.bswf"
        self.add_asset(logical, minimal)
        self.manifest["swfs"][logical] = inspect_swf(bytes(minimal))
        directory = self.source / "alias/base/retail"
        directory.mkdir(parents=True)
        profile, _ = staging.movie_resources(self.source, self.manifest)
        (directory / "movies.uim").write_bytes(profile)
        (directory / "shell.bswf").write_bytes(minimal)
        runner.mutate(self.source / "alias", "movie-native-alias")
        mutated = (directory / "shell.bswf").read_bytes()
        self.assertGreater(len(mutated), len(minimal))
        inventory = inspect_swf(mutated)
        self.assertEqual(inventory["dictionary_counts"], self.manifest["swfs"][logical]["dictionary_counts"])
        words = struct.unpack("<16I", (directory / "movies.uim").read_bytes())
        self.assertEqual(words[8:10], (len(mutated), fnv1a32(mutated)))
        commands = runner.root_commands(mutated)
        _, offset, size = next(row for row in commands if row[0] == 12)
        actions = mutated[offset:offset + size]
        self.assertIn(b"getLocalString\0", actions)
        self.assertIn(b"setCVarInteger\0", actions)
        inspector = Inspector()
        inspector.actions(Reader(actions))
        self.assertEqual(dict(inspector.opcodes), {"Push": 2, "GetVariable": 1, "SetVariable": 1,
                                                  "CallFunction": 1, "Pop": 1, "End": 1})

    def test_native_callback_alias_guard_requires_the_identity_failure_at_the_admitted_name(self):
        trace = "SWF dependency movie=swf/shell.swf kind=native-callback name=getLocalString"
        fatal = "retail SWF execution rejected (swf/shell.swf): native callback identity unavailable"
        log = "run_ready \n]ps2_uiRetail execute shell\n" + trace + "\n[D3BFG] FATAL " + fatal + "\n"
        accepted = lambda text: runner.classify_negative(text, "run", "movie-native-alias")[0]
        self.assertTrue(accepted(log))
        for changed in (log.replace(trace, ""), log.replace("name=getLocalString", "name=setCVarInteger"),
                        log.replace(fatal, "out of memory"), log.replace("identity unavailable", "signature unavailable"),
                        log.replace("rejected (swf/shell.swf)", "rejected (swf/dialog.swf)"),
                        log + "RETAIL movie=0 state=loaded\n",
                        log + "RETAIL movie=1 state=loaded\n",
                        log + "CONSOLE capture=movie-shell1.tga frame=1 presents=1 redraws=0\n", log + "TLB Miss\n"):
            with self.subTest(log=changed):
                self.assertFalse(accepted(changed))


if __name__ == "__main__":
    unittest.main()
