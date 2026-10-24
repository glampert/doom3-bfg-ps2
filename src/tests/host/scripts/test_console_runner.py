#!/usr/bin/env python3
# ================================================================================================
# File: test_console_runner.py
# Brief: Reject false native-console acceptance, stale or malformed presentation packages and captures
# whose input line differs from the cooked console charset.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/scripts"))
import console_capture
import run_console as runner
import stage_headless as staging
import stage_retail_ui
from test_retail_ui_runner import synthetic_package


def completed(standard="ntsc"):
    status = "[D3BFG] CONSOLE frames={frame} presents={frame} redraws={redraws} active={active}\n"
    log = f"game initialized.\nLOOP ready;\nrun_begin\nrun_ready\n[D3BFG] GS ready {standard.upper()} 512x448\n[D3BFG] UI adapters ready\n"
    log += status.format(frame=0, redraws=0, active=1)
    log += "]echo native_console_run\nnative_console_run \n]echo native_console_run\nnative_console_run \n"
    log += status.format(frame=120, redraws=2, active=0)
    log += status.format(frame=121, redraws=2, active=1)
    log += "[D3BFG] CONSOLE capture=console-edit.tga frame=122 presents=122 redraws=2\n"
    log += "]history\n]quit\n[D3BFG] CONSOLE capture=console-quit.tga frame=123 presents=123 redraws=3\n"
    log += status.format(frame=123, redraws=3, active=1)
    log += "------------ Game Shutdown\n[D3BFG] CONSOLE shutdown complete frames=123 presents=123 redraws=3\n"
    log += "COMMON stage=system shutdown\nHEADLESS memory baseline=1/2/3 final=1/2/3 peak=10/20 arena=30\nBOOT production shutdown complete\n"
    return "".join("[    1.0000] " + line for line in log.splitlines(keepends=True))


def console_image(height, manifest, source):
    """Render what the native console shows: the half console, its border and the input line's
    glyphs sampled from the synthetic charset exactly as the target's quads map them."""
    rgb = bytearray(bytes((16, 24, 40)) * (512 * height))

    def rectangle(x, y, width, rows, color):
        left, right = math.ceil(x * 512 / 640), math.ceil((x + width) * 512 / 640)
        top, bottom = math.ceil(y * height / 480), math.ceil((y + rows) * height / 480)
        for row in range(top, bottom):
            offset = (row * 512 + left) * 3
            rgb[offset:offset + (right - left) * 3] = bytes(color) * (right - left)

    rectangle(0, 0, 640, 238, console_capture.DARK)
    rectangle(0, 238, 640, 2, console_capture.CYAN)
    atlas = console_capture.charset_atlas(manifest, source)
    lines = height
    for index, character in enumerate(console_capture.INPUT_TEXT):
        if character == " ":
            continue
        code = ord(character)
        cell_x, cell_y = (code & 15) * 16, (code >> 4) * 16
        x0 = (console_capture.INPUT_LINE[0] + index * 8) * 512 / 640
        y0 = console_capture.INPUT_LINE[1] * lines / 480
        x1, y1 = x0 + 8 * 512 / 640, y0 + 16 * lines / 480
        color = console_capture.CYAN if index == 0 else console_capture.WHITE
        for row in range(math.ceil(y0), math.ceil(y1)):
            for column in range(math.ceil(x0), math.ceil(x1)):
                u = cell_x + (column - x0) / (x1 - x0) * 16
                v = cell_y + (row - y0) / (y1 - y0) * 16
                sampled = atlas.texture.sample(u, v)
                alpha = 0 if sampled is None else sampled[3]
                offset = (row * 512 + column) * 3
                rgb[offset:offset + 3] = bytes(console_capture.blend(color, alpha / 255, console_capture.DARK))
    return rgb


def tga(height, pixels):
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 512, height, 24, 0x20)
    bgr = bytearray(len(pixels))
    bgr[0::3], bgr[1::3], bgr[2::3] = pixels[2::3], pixels[1::3], pixels[0::3]
    return header + bgr


class ConsoleAcceptance(unittest.TestCase):
    def test_complete_ntsc_and_pal(self):
        for standard in ("ntsc", "pal"):
            self.assertTrue(runner.classify(completed(standard), "run", standard)[0])

    def test_crash_and_stale_identity_override_completion(self):
        for log in (completed() + "[D3BFG] FATAL failure", completed() + "TLB Miss, pc=0x1000 addr=0x0 [load]", completed().replace("run_begin", "old_begin")):
            self.assertFalse(runner.classify(log, "run", "ntsc")[0])

    def test_native_frames_and_redraw_only_requests(self):
        for old, new in (("presents=123", "presents=124"), ("redraws=3", "redraws=1"), ("frames=123", "frames=119")):
            with self.subTest(field=old):
                self.assertFalse(runner.classify(completed().replace(old, new), "run", "ntsc")[0])

    def test_native_history_toggle_and_quit_are_required(self):
        for old, new in (("]echo native_console_run\n", ""), ("native_console_run \n", ""), ("active=0", "active=1"), ("]history\n", ""), ("]quit\n", "")):
            with self.subTest(field=old):
                self.assertFalse(runner.classify(completed().replace(old, new), "run", "ntsc")[0])

    def test_capture_must_be_completed_and_final_quit_must_drain_it(self):
        for old, new in (("console-edit.tga", "missing.tga"), ("frame=122 presents=122", "frame=119 presents=119"),
                         ("frame=123 presents=123", "frame=122 presents=122"), ("frame=122 presents=122", "frame=122 presents=123")):
            with self.subTest(field=old):
                self.assertFalse(runner.classify(completed().replace(old, new), "run", "ntsc")[0])

    def test_shutdown_and_recovery_are_required(self):
        for log in (completed().replace("BOOT production shutdown complete", ""), completed().replace("final=1/2/3", "final=1/2/4"),
                    completed() + "[D3BFG] CONSOLE shutdown complete frames=123 presents=123 redraws=3\n"):
            self.assertFalse(runner.classify(log, "run", "ntsc")[0])


class ConsolePixels(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.path = self.root / "console.tga"
        self.source = self.root / "build/cook"
        self.manifest = synthetic_package(self.source)

    def verify(self, standard):
        return console_capture.verify_edit(self.path, standard, self.manifest, self.source)

    def test_native_edit_pixels_in_both_video_modes(self):
        for standard, height in (("ntsc", 448), ("pal", 512)):
            self.path.write_bytes(tga(height, console_image(height, self.manifest, self.source)))
            result = self.verify(standard)
            self.assertEqual((result["width"], result["height"]), (512, height))
            names = [check["name"] for check in result["checks"]]
            self.assertEqual(names[:3], [name for name, _, _, _ in console_capture.FIXED_PIXELS])
            # The prompt, every letter's ink and hole, and the gap between the words.
            self.assertEqual(sum(name.endswith("-ink") for name in names), 12)
            self.assertEqual(sum(name.endswith("-hole") for name in names), 12)
            self.assertIn("edit-5-space", names)
            self.assertTrue(all(check["passed"] for check in result["checks"]))

    def test_opaque_or_absent_console_background_is_rejected(self):
        for replacement in ((0, 0, 0), (16, 24, 40)):
            pixels = console_image(448, self.manifest, self.source)
            x, y = int(8 * 512 / 640), int(200 * 448 / 480)
            offset = (y * 512 + x) * 3
            pixels[offset:offset + 3] = bytes(replacement)
            self.path.write_bytes(tga(448, pixels))
            with self.assertRaisesRegex(ValueError, "half-console-background"):
                self.verify("ntsc")

    def test_missing_text_or_opaque_glyphs_are_rejected(self):
        for probe in console_capture.expected_probes(self.manifest, self.source, "ntsc"):
            pixels = console_image(448, self.manifest, self.source)
            offset = (probe["y"] * 512 + probe["x"]) * 3
            actual = pixels[offset:offset + 3]
            pixels[offset:offset + 3] = bytes((255, 0, 0)) if actual != bytes((255, 0, 0)) else bytes(3)
            self.path.write_bytes(tga(448, pixels))
            with self.subTest(pixel=probe["name"]), self.assertRaisesRegex(ValueError, probe["name"]):
                self.verify("ntsc")

    def test_probes_come_from_the_cooked_charset(self):
        manifest = json.loads(json.dumps(self.manifest))
        manifest["image_resolutions"] = [entry for entry in manifest["image_resolutions"]
                                         if entry["logical_image"] != "textures/bigchars"]
        with self.assertRaisesRegex(ValueError, "no console charset"):
            console_capture.expected_probes(manifest, self.source, "ntsc")
        for probe in console_capture.expected_probes(self.manifest, self.source, "ntsc"):
            # Ink lands well above the console background (the prompt is cyan, the text white);
            # holes and the gap sit on it exactly.
            if probe["name"].endswith("-ink"):
                self.assertGreater(max(probe["low"]), 40)
            elif probe["name"].endswith(("-hole", "-space")):
                self.assertEqual((probe["low"], probe["high"]), (console_capture.DARK, console_capture.DARK))

    def test_wrong_dimensions_or_profile_are_rejected(self):
        self.path.write_bytes(tga(448, console_image(448, self.manifest, self.source)))
        with self.assertRaisesRegex(ValueError, "expected 512x512"):
            self.verify("pal")
        with self.assertRaisesRegex(ValueError, "standard"):
            self.verify("secam")
        self.path.write_bytes(self.path.read_bytes()[:-1])
        with self.assertRaises(ValueError):
            self.verify("ntsc")


class ConsoleStaging(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "build/debug"
        self.source.mkdir(parents=True)
        self.assets = self.root / "src/tests/data/headless"
        (self.assets / "base").mkdir(parents=True)
        (self.assets / "base/d3bfg.cfg").write_text("ps2_status\n")
        self.output = self.root / "package"
        for name in ("d3bfg.elf", "d3bfg_unstripped.elf", "d3bfg.map", "report.txt", "objects.rsp"):
            (self.source / name).write_text(name)
        report = {"passed": True, "kind": "game", "test_inputs": [], "test_symbols": [], "flag_stamps": {}}
        for name, key in (("d3bfg.elf", "runnable_elf_sha256"), ("d3bfg_unstripped.elf", "elf_sha256"), ("d3bfg.map", "link_map_sha256")):
            report[key] = staging.sha256(self.source / name)
        (self.source / "report.json").write_text(json.dumps(report))
        # The cooked catalog every visible package carries for the console charset.
        self.cook = self.root / "build/cook"
        synthetic_package(self.cook)
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(staging, "REPO_ROOT", self.root).start()
        mock.patch.object(staging, "BOOT_ROOT", self.assets).start()
        # The keyboard driver's modules a visible package copies from the SDK.
        self.irx = self.root / "sdk/iop/irx"
        self.irx.mkdir(parents=True)
        for name in staging.KEYBOARD_IRX:
            (self.irx / name).write_bytes(b"IRX " + name.encode())
        mock.patch.object(staging, "IRX_ROOT", self.irx).start()
        mock.patch.object(stage_retail_ui, "REPO_ROOT", self.root).start()

    def test_explicit_policy_replaces_stale_mode_and_preserves_user_config(self):
        for mode in ("ntsc", "pal", "headless"):
            staging.stage("debug", self.output, mode, catalog=self.cook)
            self.assertEqual((self.output / "presentation.cfg").read_text(), mode + "\n")
            inputs = json.loads((self.output / "boot-data.json").read_text())
            self.assertEqual(inputs["presentation.cfg"], staging.sha256(self.output / "presentation.cfg"))
            (self.output / "base/d3bfg.cfg").write_text("user settings\n")
        self.assertEqual((self.output / "base/d3bfg.cfg").read_text(), "user settings\n")

    def test_visible_packages_carry_the_cooked_catalog_and_headless_ones_do_not(self):
        staging.stage("debug", self.output, "ntsc", catalog=self.cook)
        catalog = self.output / "base/retail_ui.uic"
        self.assertTrue(catalog.is_file())
        # The keyboard driver's modules ride beside the ELF, and only in a visible package.
        for name in staging.KEYBOARD_IRX:
            self.assertEqual((self.output / "irx" / name).read_bytes(), b"IRX " + name.encode())
            self.assertIn("irx/" + name, json.loads((self.output / "boot-data.json").read_text()))
        (self.output / "irx/stale.irx").write_bytes(b"old")
        staging.stage("debug", self.output, "ntsc", catalog=self.cook)
        self.assertFalse((self.output / "irx/stale.irx").exists())
        (self.irx / staging.KEYBOARD_IRX[0]).unlink()
        with self.assertRaisesRegex(ValueError, "keyboard driver module"):
            staging.stage("debug", self.root / "visible-without-irx", "ntsc", catalog=self.cook)
        (self.irx / staging.KEYBOARD_IRX[0]).write_bytes(b"IRX " + staging.KEYBOARD_IRX[0].encode())
        staging.stage("debug", self.output, "headless")
        self.assertFalse((self.output / "irx").exists())
        assets = sorted((self.output / "base/retail/assets").iterdir())
        self.assertEqual({path.suffix for path in assets}, {".dat", ".gstx"})
        inputs = json.loads((self.output / "boot-data.json").read_text())
        self.assertEqual(inputs["base/retail_ui.uic"], staging.sha256(catalog))
        with self.assertRaisesRegex(ValueError, "cooked retail catalog"):
            staging.stage("debug", self.root / "visible-without-cook", "pal")
        self.assertFalse((self.root / "visible-without-cook").exists())
        headless = staging.stage("debug", self.root / "headless")
        self.assertFalse((headless / "base/retail_ui.uic").exists())

    def test_menu_policy_needs_a_visible_retail_movie_package(self):
        staging.stage("debug", self.output, "pal", catalog=self.cook, menu=True)
        self.assertEqual((self.output / "presentation.cfg").read_text(), "pal menu\n")
        staging.stage("debug", self.output, "pal", catalog=self.cook)
        self.assertEqual((self.output / "presentation.cfg").read_text(), "pal\n")
        for kwargs in ({"presentation": "headless"}, {"presentation": "ntsc", "include_ui": True}):
            with self.assertRaisesRegex(ValueError, "menu startup policy"):
                staging.stage("debug", self.root / "menu-invalid", catalog=self.cook, menu=True, **kwargs)
            self.assertFalse((self.root / "menu-invalid").exists())

    def test_default_policy_is_explicit_headless(self):
        staging.stage("debug", self.output)
        self.assertEqual((self.output / "presentation.cfg").read_text(), "headless\n")

    def test_invalid_policy_or_modified_build_is_rejected_before_staging(self):
        with self.assertRaisesRegex(ValueError, "presentation"):
            staging.stage("debug", self.output, "invalid")
        self.assertFalse(self.output.exists())
        (self.source / "d3bfg.elf").write_text("modified")
        with self.assertRaisesRegex(ValueError, "report does not match"):
            staging.stage("debug", self.output, "ntsc", catalog=self.cook)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
