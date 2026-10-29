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
import swf_fixture
import gstx_fixture
import run_retail_movies as runner
import stage_retail_ui as staging
from ui_font import inspect_language, inspect_font
from ui_gstx import fnv1a32
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
        width, height, rgba = swf_fixture.charset_pixels(font=True)
        textures = {"font": gstx_fixture.alpha_texture(width, height, gstx_fixture.rgba_alpha(rgba)),
                    "shell": gstx_fixture.solid_texture(256, 128, (40, 60, 80, 255)),
                    "dialog": gstx_fixture.solid_texture(128, 64, (80, 60, 40, 255)),
                    "cover": gstx_fixture.solid_texture(384, 192, (10, 20, 30, 255)),
                    "cross": gstx_fixture.solid_texture(128, 64, (30, 90, 200, 255))}
        self.manifest["cooked"]["runtime_images"] = {name: gstx_fixture.write_asset(self.source, raw)
                                                     for name, raw in textures.items()}
        self.manifest["image_resolutions"] = [{"logical_image": "newfonts/ps2_ascii/48.tga", "selected": "font", "status": "resolved"},
            {"logical_image": "generated/swf/shell.tga", "selected": "shell"},
            {"logical_image": "generated/swf/dialog.tga", "selected": "dialog"},
            {"logical_image": "guis/assets/mainmenu/doom3_cover.tga", "selected": "cover"},
            {"logical_image": "guis/assets/hud/controller/ps3/cross", "selected": "cross"}]
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
        """An inactive or executing movie step's slice of a session log."""
        log = ""
        open_images = {next(row["selected"] for row in manifest["image_resolutions"] if row["logical_image"] == font[:-4] + ".tga")
                       for font in manifest["fonts"]}
        for phase, (name, movie_name, movie) in enumerate(phases):
            prototype, rows = runner.expected_labels(manifest, self.source, movie)
            mode = "active" if execution else "inactive"
            log += (f"RETAIL movie={movie} state=loaded frames=120 dictionary=29 texts=7 preview=10 "
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
            capture = runner.movie_capture_name("execution" if execution else "inactive", name)
            log += f"CONSOLE capture={capture} frame={frame} presents={frame} redraws=0\n"
            if execution:
                log += f"RETAIL released movie={movie} images={images} heap=80/100/4\n"
        return log

    def test_native_acceptance_checks_layout_warm_ownership_frames_and_full_cleanup(self):
        manifest = self.label_manifest()
        log = self.acceptance_log(manifest)
        self.assertTrue(runner.classify(log, manifest, self.source)[0])
        for before, after in (("width=160.00", "width=159.00"),
                              ("frames=120", "frames=119"), ("evictions=0", "evictions=1"),
                              ("scripts=inactive", "scripts=active")):
            self.assertIn(before, log)
            self.assertFalse(runner.classify(log.replace(before, after), manifest, self.source)[0])

    def test_movie_textures_match_the_open_converted_images_and_never_upload_again(self):
        manifest = self.label_manifest()
        font = runner.resident_textures(manifest, self.source, {"font"})
        # A 64x16 CLUT texture plus one 512x128 PSMT4 page; 128 table bytes, 16x4 grid cells and 64 items.
        self.assertEqual(font, {"images": 1, "pages": 1, "cluts": 1, "pixel_bytes": 4096 + 32768,
                                "metadata_bytes": 128 + 65 * 4 + 64 * 2})
        for execution in (False, True):
            log = self.acceptance_log(manifest, execution=execution)
            accepted = lambda text: runner.classify(text, manifest, self.source, execution=execution)[0]
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

    def test_images_earlier_steps_opened_stay_in_the_counters(self):
        manifest = self.label_manifest()
        # The session opened the cover at boot; this inactive step keeps it in its counters.
        warm = {"cover"}
        plain = self.acceptance_log(manifest)
        self.assertTrue(runner.classify(plain, manifest, self.source)[0])
        self.assertFalse(runner.classify(plain, manifest, self.source, warm=warm)[0])
        fonts = {next(row["selected"] for row in manifest["image_resolutions"] if row["logical_image"] == font[:-4] + ".tga")
                 for font in manifest["fonts"]}
        with_cover = plain
        for images in (fonts | {"shell"}, fonts | {"shell", "dialog"}):
            before = runner.texture_counters(runner.resident_textures(manifest, self.source, images))
            after = runner.texture_counters(runner.resident_textures(manifest, self.source, images | warm))
            with_cover = with_cover.replace("images={} pages={} cluts={} pixels={} metadata={} ".format(*before),
                                            "images={} pages={} cluts={} pixels={} metadata={} ".format(*after))
        self.assertTrue(runner.classify(with_cover, manifest, self.source, warm=warm)[0])

    def render_log(self, manifest):
        swf = manifest["swfs"]["generated/swf/dialog.bswf"]
        language = inspect_language((self.source / manifest["cooked"]["assets"]["strings/english.lang"]["path"]).read_bytes())
        options = "|".join(language["entries"][key] for key in runner.QUIT_OPTIONS)
        images = runner.resident_images(manifest) | runner.prompt_images(manifest) | {"dialog"}
        counters = runner.texture_counters(runner.resident_textures(manifest, self.source, images))
        textures = "[D3BFG] RETAIL textures images={} pages={} cluts={} pixels={} metadata={}".format(*counters)
        log = ""
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
        return log

    def test_dialog_render_requires_resident_textures_masks_owner_and_scripts(self):
        manifest = self.label_manifest()
        self.language += b' { "#str_swf_accept" "Accept" "#str_swf_cancel" "Cancel" "#str_swf_change_game" "Change Game" }'
        self.language = self.language.replace(b' } {', b'')
        self.add_asset("strings/english.lang", self.language)
        manifest["language"] = inspect_language(self.language)
        log = self.render_log(manifest)
        accepted = lambda text: runner.classify_dialog_render(text, manifest, self.source)[0]
        self.assertTrue(accepted(log), runner.classify_dialog_render(log, manifest, self.source)[1])
        first = next(runner.TEXTURES_RE.finditer(log)).group(0)
        pixels = "pixels=" + first.split("pixels=")[1].split()[0]
        images = int(first.split("images=")[1].split()[0])
        texture_rows = [match.group(0) for match in runner.TEXTURES_RE.finditer(log)]
        stalled = log.replace(texture_rows[1], texture_rows[1].replace("triangles=2000", "triangles=1000"))
        for changed in (log.replace(pixels, pixels + "1"), log.replace(f"images={images} pages", f"images={images + 1} pages"),
                        log.replace("uploads=5", "uploads=0"), replace_last(log, "uploads=5", "uploads=6"),
                        replace_last(log, "evictions=0", "evictions=1"), stalled,
                        log.replace("masks=10 strokes=0", "masks=9 strokes=0"),
                        log.replace("options=Accept|Cancel|Change Game", "options=Accept|Cancel|Quit"),
                        replace_last(log, "SWF dependency movie=swf/dialog.swf kind=frame name=rollOn\n", ""),
                        replace_last(log, " heap=80/100/4", " heap=81/100/4")):
            with self.subTest(changed=changed):
                self.assertFalse(accepted(changed))

    def shell_render_log(self, manifest, menu=False):
        swf = manifest["swfs"]["generated/swf/shell.bswf"]
        phases = runner.MENU_RENDER_PHASES if menu else runner.SHELL_RENDER_PHASES
        features = swf["features"]
        images = runner.menu_images(manifest) if menu else runner.shell_images(manifest)
        counters = runner.texture_counters(runner.resident_textures(manifest, self.source, images))
        channel_cluts, channel_pixels = runner.channel_counters(manifest, self.source)
        textures = "[D3BFG] RETAIL textures images={} pages={} cluts={} pixels={} metadata={}".format(*counters)
        log = ""
        frame = 0
        work = 0
        rolled = 0
        for name, frames in phases:
            log += (f"RETAIL drawable movie=0 masks={features['masks']} strokes={features['strokes']} "
                    f"screen_blends={runner.screen_placements(swf)} additive_colors={features['color_add']}\n")
            waited = frames
            for index, (suffix, gap) in enumerate(runner.capture_gaps(phases).items()):
                waited += gap
                commands = 7 + 2 * index
                frame += frames if suffix == "a" else gap
                work += 1000
                # Outlines and the screen glow draw only once the logo has rolled on.
                rolled += 1000 if suffix != "a" else 0
                owner = (("RETAIL menu screen={} next={} session={} platform={} callbacks={} root_focus={}\n"
                          .format(*runner.MENU_SCREENS[suffix]) +
                          "RETAIL menu input events={} handled={} dialog={} quit={}\n".format(*runner.MENU_INPUTS[suffix])) if menu else
                         "RETAIL owner screen=0 next=0 carousel_items=1 carousel_slots=5 carousel_view=0 credits=1 "
                         f"callbacks=2 logo={1 if suffix == 'b' else 0}\n")
                log += (f"RETAIL movie=0 state=loaded frames={waited} dictionary={len(swf['dictionary'])} "
                        f"texts={swf['dictionary_counts']['edittext']} preview=10 "
                        f"english={manifest['language']['entry_count']} scripts=active heap=100/120/8\n"
                        + owner +
                        f"RETAIL execution movie=0 ticks={waited} root=1 sprites=2 texts=3 instructions=1000 calls=5 "
                        f"commands={commands} depth=2 native_steps=20 clock=fixed\n"
                        f"RETAIL draw movie=0 full=1 triangles={work} flushes=10 masks={work // 100} peak_masks=1 "
                        f"peak_vertices=300 peak_indexes=600 peak_draws=40 depth_masks={work // 200} lines={rolled // 10} "
                        f"screen={rolled // 50} color_terms={work // 20}\n"
                        f"{textures} triangles={work} uploads=5 evictions=0 channel_cluts={channel_cluts} "
                        f"channel_pixels={channel_pixels}\n"
                        f"CONSOLE capture=movie-{name}-{suffix}.tga frame={frame} presents={frame} redraws=0\n")
            log += f"RETAIL released movie=0 images={len(images)} heap=80/100/4\n"
        return log

    def test_shell_render_requires_outlines_depth_masks_screen_color_terms_and_channel_cluts(self):
        manifest = self.label_manifest()
        swf = manifest["swfs"]["generated/swf/shell.bswf"]
        # The fixture stand-in carries every feature the shell needs.
        self.assertEqual((swf["features"]["strokes"], runner.screen_placements(swf), swf["features"]["color_add"]), (2, 1, 2))
        log = self.shell_render_log(manifest)
        accepted = lambda text: runner.classify_shell_render(text, manifest, self.source)[0]
        self.assertTrue(accepted(log), runner.classify_shell_render(log, manifest, self.source)[1])
        # The channel CLUTs upload at the first screen pass, after the start screen's capture.
        self.assertTrue(accepted(log.replace("uploads=5 evictions=0 channel", "uploads=2 evictions=0 channel", 1)))
        channel_cluts, channel_pixels = runner.channel_counters(manifest, self.source)
        draw_rows = [match.group(0) for match in runner.SHELL_DRAW.finditer(log)]
        stalled = log.replace(draw_rows[3], draw_rows[3].replace("lines=200", "lines=100"))
        for changed in (log.replace("strokes=2 ", "strokes=1 "), log.replace("screen_blends=1 ", "screen_blends=0 "),
                        log.replace("additive_colors=2", "additive_colors=3"), log.replace("masks=10 strokes", "masks=9 strokes"),
                        log.replace("depth_masks=5 ", "depth_masks=0 "), log.replace("lines=100 ", "lines=0 "),
                        log.replace("screen=20 ", "screen=0 "), log.replace("color_terms=50", "color_terms=0"),
                        log.replace("callbacks=2 logo=1", "callbacks=2 logo=0"), replace_last(log, "logo=0", "logo=1"),
                        log.replace(f"channel_cluts={channel_cluts} ", f"channel_cluts={channel_cluts + 1} "),
                        log.replace(f"channel_pixels={channel_pixels}", f"channel_pixels={channel_pixels - 1}"),
                        log.replace("clock=fixed", "clock=real"), stalled, replace_last(log, "uploads=5", "uploads=6"),
                        log.replace("uploads=5 evictions=0 channel", "uploads=6 evictions=0 channel", 1),
                        replace_last(log, "evictions=0", "evictions=1"), log.replace("peak_masks=1", "peak_masks=5"),
                        log.replace("carousel_items=1", "carousel_items=2"),
                        replace_last(log, "RETAIL released movie=0 images=", "RETAIL released movie=0 images=1"),
                        replace_last(log, "heap=80/100/4", "heap=81/100/4")):
            with self.subTest(changed=changed):
                self.assertFalse(accepted(changed))

    def test_menu_render_requires_key_driven_screens_focus_and_prerendered_text(self):
        manifest = self.label_manifest()
        log = self.shell_render_log(manifest, menu=True)
        verdict = lambda text: runner.classify_shell_render(text, manifest, self.source, menu=True)
        self.assertEqual(verdict(log), (True, "native shell handler drew its start and root screens, driven by keyboard and pad keys"))
        # The scaffold owner's rows cannot stand in for the native handler's, and the reverse.
        self.assertFalse(runner.classify_shell_render(log, manifest, self.source)[0])
        self.assertFalse(verdict(self.shell_render_log(manifest))[0])
        for changed in (log.replace("screen=0 next=0 session=PRESS_START", "screen=1 next=1 session=PRESS_START", 1),
                        replace_last(log, "session=IDLE", "session=PRESS_START"),
                        log.replace("platform=1", "platform=2", 1), log.replace("callbacks=5", "callbacks=4", 1),
                        log.replace("root_focus=2", "root_focus=1", 1), replace_last(log, "root_focus=0", "root_focus=3"),
                        log.replace("events=2 handled=1", "events=2 handled=2", 1),
                        replace_last(log, "events=8 handled=6", "events=10 handled=7"),
                        log + "[D3BFG] UI glyph size font=sarasori_rg scale=0.30000,0.35000 stroke=2.000 "
                                    "prerendered=no\n",
                        replace_last(log, "RETAIL drawable movie=0", "RETAIL undrawable movie=0"),
                        replace_last(log, "heap=80/100/4", "heap=81/100/4"), replace_last(log, "evictions=0", "evictions=1")):
            with self.subTest(changed=changed):
                self.assertFalse(verdict(changed)[0])

    def menu_quit_log(self, manifest):
        shell, dialog = manifest["swfs"]["generated/swf/shell.bswf"], manifest["swfs"]["generated/swf/dialog.bswf"]
        counters = runner.texture_counters(runner.resident_textures(manifest, self.source, runner.menu_images(manifest)))
        channel_cluts, channel_pixels = runner.channel_counters(manifest, self.source)
        textures = ("[D3BFG] RETAIL textures images={} pages={} cluts={} pixels={} metadata={}".format(*counters) +
                    f" triangles=100 uploads=5 evictions=0 channel_cluts={channel_cluts} channel_pixels={channel_pixels}\n")
        log = (f"RETAIL drawable movie=0 masks={shell['features']['masks']} strokes={shell['features']['strokes']} "
               f"screen_blends={runner.screen_placements(shell)} additive_colors={shell['features']['color_add']}\n"
               f"RETAIL drawable movie=1 masks={dialog['features']['masks']} strokes=0 screen_blends=0 additive_colors=0\n")
        frame = 0
        for suffix, _, gap in runner.MENU_QUIT_STEPS:
            frame += gap + 1
            if suffix == "d":
                log += "".join(f"SWF dependency movie=dialog kind={kind} name={name}\n" for kind, name in runner.DIALOG_DEPENDENCIES
                               if kind != "frame")
            if suffix == "f":
                log += "SWF dependency movie=dialog kind=native-callback name=func1\n"
            log += ("RETAIL menu screen={} next={} session={} platform={} callbacks={} root_focus={}\n"
                    .format(*runner.MENU_QUIT_SCREENS[suffix]) +
                    "RETAIL menu input events={} handled={} dialog={} quit={}\n".format(*runner.MENU_QUIT_INPUTS[suffix]) +
                    textures + f"CONSOLE capture=movie-menuquit-{suffix}.tga frame={frame} presents={frame} redraws=0\n")
        frame += 1
        log += ("run_done \nSWF dependency movie=dialog kind=native-callback name=func0\n"
                f"CONSOLE capture=movie-quit.tga frame={frame} presents={frame} redraws=0\n"
                "RETAIL released movie=0 images=11 heap=80/100/4\n"
                f"[D3BFG] CONSOLE shutdown complete frames={frame} presents={frame} redraws=0\n"
                "HEADLESS memory baseline=7424/7600/4 final=7424/7600/4 peak=100/120 arena=200\n"
                "COMMON stage=system shutdown\nBOOT production shutdown complete\n")
        return log

    def test_menu_quit_requires_cancel_then_accept_through_the_native_dialog(self):
        manifest = self.label_manifest()
        log = self.menu_quit_log(manifest)
        verdict = lambda text: runner.classify_menu_quit(text, manifest, self.source)
        self.assertEqual(verdict(log), (True, "native quit dialog opened from the root, cancelled, then accepted, quitting through Common"))
        for changed in (log.replace("root_focus=3", "root_focus=2", 1),
                        log.replace("events=10 handled=7 dialog=0 quit=0", "events=10 handled=8 dialog=1 quit=2"),
                        log.replace("SWF dependency movie=dialog kind=native-callback name=func1\n", ""),
                        log.replace("name=func0", "name=func1"),
                        log.replace("SWF dependency movie=dialog kind=method name=showMessage\n", ""),
                        log + "[D3BFG] UI glyph size font=sarasori_rg scale=0.18333,0.21389 stroke=0.000 "
                                    "prerendered=no\n",
                        log.replace("RETAIL drawable movie=1", "RETAIL undrawable movie=1"),
                        log.replace("evictions=0", "evictions=1", 1),
                        log.replace("RETAIL released movie=0 images=11 heap=80/100/4\n", ""),
                        replace_last(log, "[D3BFG] CONSOLE shutdown complete frames=", "[D3BFG] CONSOLE shutdown complete frames=9")):
            with self.subTest(changed=changed):
                self.assertFalse(verdict(changed)[0])

    def menu_boot_log(self, manifest):
        shell, dialog = manifest["swfs"]["generated/swf/shell.bswf"], manifest["swfs"]["generated/swf/dialog.bswf"]
        counters = runner.texture_counters(runner.resident_textures(manifest, self.source, runner.menu_images(manifest)))
        channel_cluts, channel_pixels = runner.channel_counters(manifest, self.source)
        textures = ("[D3BFG] RETAIL textures images={} pages={} cluts={} pixels={} metadata={}".format(*counters) +
                    f" triangles=100 uploads=5 evictions=0 channel_cluts={channel_cluts} channel_pixels={channel_pixels}\n")
        log = ("[D3BFG] BOOT presentation=ntsc menu=1\nGS ready NTSC\n"
               f"RETAIL drawable movie=0 masks={shell['features']['masks']} strokes={shell['features']['strokes']} "
               f"screen_blends={runner.screen_placements(shell)} additive_colors={shell['features']['color_add']}\n"
               f"RETAIL drawable movie=1 masks={dialog['features']['masks']} strokes=0 screen_blends=0 additive_colors=0\n"
               "[D3BFG] HEADLESS frames=0 game_frames=0 state=console console=0 game_nomap=1 render_device=1 sound_output=0 "
               "heap=100/120/8\nrun_ready \n")
        frame = 1
        for suffix, _, gap in runner.MENU_BOOT_STEPS:
            frame += gap + 1
            log += (f"RETAIL movie=0 state=loaded frames={frame - 1} dictionary={len(shell['dictionary'])} "
                    f"texts={shell['dictionary_counts']['edittext']} preview=10 "
                    f"english={manifest['language']['entry_count']} scripts=active heap=100/120/8\n"
                    "RETAIL menu screen={} next={} session={} platform={} callbacks={} root_focus={}\n"
                    .format(*runner.MENU_BOOT_SCREENS[suffix]) +
                    "RETAIL menu input events={} handled={} dialog={} quit={}\n".format(*runner.MENU_BOOT_INPUTS[suffix]) +
                    f"RETAIL execution movie=0 ticks={frame - 1} root=1 sprites=2 texts=3 instructions=1000 calls=5 "
                    f"commands=9 depth=2 native_steps=20 clock=real\n" +
                    textures + f"CONSOLE capture=movie-menuboot-{suffix}.tga frame={frame} presents={frame} redraws=0\n")
        return log + "RETAIL released movie=0 images=19 heap=80/100/4\n"

    def test_menu_boot_requires_the_policy_to_open_both_movies_before_the_startup_commands(self):
        manifest = self.label_manifest()
        log = self.menu_boot_log(manifest)
        verdict = lambda text: runner.classify_menu_boot(text, "run", "ntsc", manifest, self.source)
        self.assertEqual(verdict(log), (True, "native menu opened at startup from the package policy, driven by keys on real time"))
        shell_load = log[log.index("RETAIL drawable movie=0"):log.index("RETAIL drawable movie=1")]
        for changed in (log.replace("menu=1", "menu=0"),
                        log.replace(shell_load, "").replace("run_ready \n", "run_ready \n" + shell_load),
                        log.replace("run_ready \n", "run_ready \nps2_uiRetail menu\n"),
                        log.replace("state=console console=0", "state=console console=1"),
                        log.replace("clock=real", "clock=fixed"),
                        log.replace("root_focus=0", "root_focus=1", 1),
                        replace_last(log, "events=4 handled=2", "events=4 handled=3"),
                        replace_last(log, "session=PRESS_START", "session=IDLE"),
                        log + "[D3BFG] UI glyph size font=sarasori_rg scale=0.30000,0.35000 stroke=2.000 "
                                    "prerendered=no\n",
                        log.replace("evictions=0", "evictions=1", 1),
                        log.replace("RETAIL released movie=0 images=19 heap=80/100/4\n", ""),
                        log.replace("RETAIL drawable movie=0", "RETAIL undrawable movie=0")):
            with self.subTest(changed=changed):
                self.assertFalse(verdict(changed)[0])

    def menu_screens_log(self, manifest):
        shell, dialog = manifest["swfs"]["generated/swf/shell.bswf"], manifest["swfs"]["generated/swf/dialog.bswf"]
        counters = runner.texture_counters(runner.resident_textures(manifest, self.source, runner.menu_images(manifest)))
        channel_cluts, channel_pixels = runner.channel_counters(manifest, self.source)
        textures = ("[D3BFG] RETAIL textures images={} pages={} cluts={} pixels={} metadata={}".format(*counters) +
                    f" triangles=100 uploads=5 evictions=0 channel_cluts={channel_cluts} channel_pixels={channel_pixels}\n")
        log = (f"RETAIL drawable movie=0 masks={shell['features']['masks']} strokes={shell['features']['strokes']} "
               f"screen_blends={runner.screen_placements(shell)} additive_colors={shell['features']['color_add']}\n"
               f"RETAIL drawable movie=1 masks={dialog['features']['masks']} strokes=0 screen_blends=0 additive_colors=0\n")
        frame = 0
        work = 0
        for suffix, _, gap in runner.MENU_SCREENS_STEPS:
            frame += gap + (1 if suffix != "a" else 0)
            work += 1000
            log += (f"RETAIL movie=0 state=loaded frames={frame} dictionary={len(shell['dictionary'])} "
                    f"texts={shell['dictionary_counts']['edittext']} preview=10 "
                    f"english={manifest['language']['entry_count']} scripts=active heap=100/120/8\n"
                    "RETAIL menu screen={} next={} session={} platform={} callbacks={} root_focus={}\n"
                    .format(*runner.MENU_SCREENS_SCREENS[suffix]) +
                    "RETAIL menu input events={} handled={} dialog={} quit={}\n".format(*runner.MENU_SCREENS_INPUTS[suffix]) +
                    f"RETAIL execution movie=0 ticks={frame} root=1 sprites=2 texts=3 instructions=1000 calls=5 "
                    f"commands=9 depth=2 native_steps=20 clock=fixed\n"
                    f"RETAIL draw movie=0 full=1 triangles={work} flushes=10 masks={work // 100} peak_masks=1 "
                    f"peak_vertices=300 peak_indexes=600 peak_draws=40 depth_masks={work // 200} lines={work // 10} "
                    f"screen={work // 50} color_terms={work // 20}\n" +
                    textures + f"CONSOLE capture=movie-menuscreens-{suffix}.tga frame={frame} presents={frame} redraws=0\n")
        return log + "RETAIL released movie=0 images=19 heap=80/100/4\n"

    def test_menu_screens_require_campaign_and_settings_reached_scrolled_and_left(self):
        manifest = self.label_manifest()
        log = self.menu_screens_log(manifest)
        verdict = lambda text: runner.classify_menu_screens(text, manifest, self.source)
        self.assertEqual(verdict(log), (True, "native campaign and settings screens opened from the root, scrolled and returned, "
                                              "with pre-rendered text"))
        for changed in (log.replace("screen=3 next=3", "screen=1 next=1", 1),
                        log.replace("screen=13 next=13", "screen=1 next=1", 1),
                        replace_last(log, "root_focus=1", "root_focus=0"),
                        log.replace("events=6 handled=4", "events=6 handled=3"),
                        log.replace("clock=fixed", "clock=real", 1),
                        log + "shell capability unavailable: screen 5\n",
                        log + "[D3BFG] UI glyph size font=sarasori_rg scale=0.35000,0.40833 stroke=1.750 "
                                    "prerendered=no\n",
                        replace_last(log, "triangles=9000 flushes", "triangles=8000 flushes"),
                        log.replace("evictions=0", "evictions=1", 1),
                        log.replace("RETAIL released movie=0 images=19 heap=80/100/4\n", ""),
                        log.replace("RETAIL drawable movie=0", "RETAIL undrawable movie=0")):
            with self.subTest(changed=changed):
                self.assertFalse(verdict(changed)[0])

    def test_shell_captures_require_identical_warm_reloads_and_detail(self):
        detailed = bytes(value for index in range(512 * 448) for value in (index % 251, index % 241, index % 239))
        flat = bytes((16, 24, 40)) * (512 * 448)
        header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 512, 448, 24, 0x20)

        def write(images):
            for name in runner.render_capture_names(runner.SHELL_RENDER_PHASES):
                pixels = images.get(name, detailed)
                bgr = bytearray(pixels)
                bgr[0::3], bgr[2::3] = pixels[2::3], pixels[0::3]
                (self.source / name).write_bytes(header + bgr)

        write({})
        self.assertIn("movie-shell3-b.tga", runner.verify_shell_render_captures(self.source, "ntsc"))
        changed = bytearray(detailed)
        changed[3000] ^= 0x40
        for images, failure in (({"movie-shell3-a.tga": bytes(changed)}, "warm reloads differ"),
                                ({"movie-shell1-a.tga": flat}, "detail")):
            with self.subTest(failure=failure):
                write(images)
                with self.assertRaisesRegex(ValueError, failure):
                    runner.verify_shell_render_captures(self.source, "ntsc")

        # The native menu's phases compare their own warm reloads.
        for name in runner.render_capture_names(runner.MENU_RENDER_PHASES):
            pixels = bytes(changed) if name == "movie-menu3-d.tga" else detailed
            bgr = bytearray(pixels)
            bgr[0::3], bgr[2::3] = pixels[2::3], pixels[0::3]
            (self.source / name).write_bytes(header + bgr)
        with self.assertRaisesRegex(ValueError, r"warm reloads differ .*\(d\)"):
            runner.verify_shell_render_captures(self.source, "ntsc", runner.MENU_RENDER_PHASES)

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
        self.assertTrue(runner.classify(log, manifest, self.source, execution=True)[0])
        self.assertFalse(runner.classify(log, manifest, self.source)[0])
        inactive = self.acceptance_log(manifest)
        self.assertFalse(runner.classify(inactive, manifest, self.source, execution=True)[0])
        for before, after in (("scripts=active", "scripts=inactive"), ("ticks=120", "ticks=119"),
                              ("ticks=120", "ticks=121"), ("root=1", "root=0"), ("root=1", "root=65536"),
                              ("frames=120", "frames=121"),
                              ("sprites=2", "sprites=0"), ("sprites=2", "sprites=2049"),
                              ("texts=3", "texts=0"), ("texts=3", "texts=513"),
                              ("depth=2", "depth=0"), ("depth=2", "depth=17"),
                              ("instructions=1000", "instructions=0"), ("calls=5", "calls=0"),
                              ("commands=7", "commands=0"), ("native_steps=20", "native_steps=0"),
                              ("native_steps=20", "native_steps=1"),
                              ("carousel_items=1", "carousel_items=0"), ("carousel_slots=5", "carousel_slots=3"),
                              ("credits=1", "credits=0"), ("callbacks=2", "callbacks=1"),
                              ("owner screen=0", "owner screen=1"), ("heap=100/120/8", "heap=121/120/8"),
                              ("heap=100/120/8", "heap=100/16777217/8"), ("heap=100/120/8", "heap=100/120/0")):
            with self.subTest(before=before, after=after):
                self.assertFalse(runner.classify(log.replace(before, after), manifest,
                                                 self.source, execution=True)[0])

    def test_execution_acceptance_rejects_stale_or_wrong_movie_status(self):
        manifest = self.label_manifest()
        log = self.acceptance_log(manifest, execution=True)
        dialog = next(row.group(0) for row in runner.EXECUTION.finditer(log) if row[1] == "1")
        for changed in (log.replace(dialog, ""), log.replace(dialog, dialog.replace("movie=1", "movie=0")),
                        log.replace("RETAIL movie=1 state=loaded", "RETAIL movie=1 state=stale")):
            self.assertFalse(runner.classify(changed, manifest, self.source, execution=True)[0])
        inactive = self.acceptance_log(manifest)
        self.assertFalse(runner.classify(inactive.replace("RETAIL movie=1 state=loaded", "RETAIL movie=1 state=stale"),
                                         manifest, self.source)[0])
        injected = inactive.replace("RETAIL movie=1 state=loaded", dialog + "\nRETAIL movie=1 state=loaded")
        self.assertFalse(runner.classify(injected, manifest, self.source)[0])

    def test_shell_execution_requires_fresh_native_owner_and_releases_it_for_dialog(self):
        manifest = self.label_manifest()
        log = self.acceptance_log(manifest, execution=True)
        owner = list(runner.OWNER.finditer(log))[-1].group(0)
        prefix, suffix = log.rsplit(owner, 1)
        self.assertFalse(runner.classify(prefix + suffix, manifest, self.source, execution=True)[0])
        leaked = log.replace("RETAIL movie=1 state=loaded", owner + "\nRETAIL movie=1 state=loaded")
        self.assertFalse(runner.classify(leaked, manifest, self.source, execution=True)[0])
        inactive = self.acceptance_log(manifest)
        self.assertFalse(runner.classify(owner + "\n" + inactive, manifest, self.source)[0])

    def test_execution_requires_actual_native_callback_traces_in_every_loaded_movie(self):
        manifest = self.label_manifest()
        log = self.acceptance_log(manifest, execution=True)
        accepted = lambda text: runner.classify(text, manifest, self.source, execution=True)[0]
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
        self.assertTrue(runner.classify(log, manifest, self.source, execution=True)[0])
        for before, after in (("ticks=60", "ticks=121"), ("instructions=500", "instructions=1001")):
            self.assertFalse(runner.classify(log.replace(before, after), manifest,
                                             self.source, execution=True)[0])

    def test_execution_releases_exact_warm_ownership_despite_different_live_animation_allocations(self):
        manifest = self.label_manifest()
        log = self.acceptance_log(manifest, execution=True)
        accepted = lambda text: runner.classify(text, manifest, self.source, execution=True)[0]
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
        moved = moved.replace("CONSOLE capture=movie-execution-shell1.tga", final + "\nCONSOLE capture=movie-execution-shell1.tga")
        self.assertNotEqual(moved, log.replace(final, "", 1))
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

        dialog_phases = tuple(("dialog" + str(index), "dialog", 1) for index in (1, 2, 3))

        def compare(first, second=baseline, third=baseline, standard="ntsc", phases=dialog_phases):
            captures = {}
            for (name, _, _), rgb in zip(phases, (first, second, third)):
                bgr = bytearray(rgb)
                bgr[0::3], bgr[2::3] = rgb[2::3], rgb[0::3]
                path = self.source / runner.movie_capture_name("inactive", name)
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
        self.assertEqual(words[8:12], (len(self.movie), fnv1a32(self.movie), 29, 10))
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
