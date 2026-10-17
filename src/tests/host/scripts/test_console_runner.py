#!/usr/bin/env python3
# ================================================================================================
# File: test_console_runner.py
# Brief: Reject false native-console acceptance and stale or malformed presentation packages/captures.
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


def console_image(height):
    rgb = bytearray(bytes((16, 24, 40)) * (512 * height))

    def rectangle(x, y, width, rows, color):
        left, right = math.ceil(x * 512 / 640), math.ceil((x + width) * 512 / 640)
        top, bottom = math.ceil(y * height / 480), math.ceil((y + rows) * height / 480)
        for row in range(top, bottom):
            offset = (row * 512 + left) * 3
            rgb[offset:offset + (right - left) * 3] = bytes(color) * (right - left)

    rectangle(0, 0, 640, 238, (4, 6, 10))
    rectangle(0, 238, 640, 2, (0, 255, 255))
    # Author only the glyphs that the fixed edited-input acceptance samples, independently of offsets.
    glyphs = {
        "]": (14, 2, 2, 2, 2, 2, 14), "e": (0, 0, 14, 17, 31, 16, 14),
        "c": (0, 0, 14, 17, 16, 17, 14), "h": (16, 16, 30, 17, 17, 17, 17),
        "o": (0, 0, 14, 17, 17, 17, 14), "p": (0, 30, 17, 17, 30, 16, 16),
        "g": (0, 15, 17, 17, 15, 1, 14),
    }
    for index, character in enumerate("]echo pending"):
        for row, bits in enumerate(glyphs.get(character, ())):
            for column in range(5):
                if bits & (1 << (4 - column)):
                    rectangle(40 + index * 8 + column * 1.5, 209 + row * 2, 1.5, 2,
                              (0, 255, 255) if index == 0 else (255, 255, 255))
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
        self.path = Path(self.temporary.name) / "console.tga"

    def test_native_edit_pixels_in_both_video_modes(self):
        for standard, height in (("ntsc", 448), ("pal", 512)):
            self.path.write_bytes(tga(height, console_image(height)))
            result = console_capture.verify_edit(self.path, standard)
            self.assertEqual((result["width"], result["height"]), (512, height))
            self.assertEqual(len(result["checks"]), len(console_capture.EDIT_PIXELS))

    def test_opaque_or_absent_console_background_is_rejected(self):
        for replacement in ((0, 0, 0), (16, 24, 40)):
            pixels = console_image(448)
            x, y = int(8 * 512 / 640), int(200 * 448 / 480)
            offset = (y * 512 + x) * 3
            pixels[offset:offset + 3] = bytes(replacement)
            self.path.write_bytes(tga(448, pixels))
            with self.assertRaisesRegex(ValueError, "half-console-background"):
                console_capture.verify_edit(self.path, "ntsc")

    def test_missing_text_or_opaque_glyphs_are_rejected(self):
        for name, x, y, _ in console_capture.EDIT_PIXELS:
            pixels = console_image(448)
            offset = (int(y * 448 / 480) * 512 + int(x * 512 / 640)) * 3
            actual = pixels[offset:offset + 3]
            pixels[offset:offset + 3] = bytes((255, 0, 0)) if actual != bytes((255, 0, 0)) else bytes(3)
            self.path.write_bytes(tga(448, pixels))
            with self.subTest(pixel=name), self.assertRaisesRegex(ValueError, name):
                console_capture.verify_edit(self.path, "ntsc")

    def test_wrong_dimensions_or_profile_are_rejected(self):
        self.path.write_bytes(tga(448, console_image(448)))
        with self.assertRaisesRegex(ValueError, "expected 512x512"):
            console_capture.verify_edit(self.path, "pal")
        with self.assertRaisesRegex(ValueError, "standard"):
            console_capture.verify_edit(self.path, "secam")
        self.path.write_bytes(self.path.read_bytes()[:-1])
        with self.assertRaises(ValueError):
            console_capture.verify_edit(self.path, "ntsc")


class ConsoleStaging(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "build/debug"
        self.source.mkdir(parents=True)
        self.assets = self.root / "assets/headless"
        (self.assets / "base").mkdir(parents=True)
        (self.assets / "base/d3bfg.cfg").write_text("ps2_status\n")
        self.output = self.root / "package"
        for name in ("d3bfg.elf", "d3bfg_unstripped.elf", "d3bfg.map", "report.txt", "objects.rsp"):
            (self.source / name).write_text(name)
        report = {"passed": True, "kind": "game", "test_inputs": [], "test_symbols": [], "flag_stamps": {}}
        for name, key in (("d3bfg.elf", "runnable_elf_sha256"), ("d3bfg_unstripped.elf", "elf_sha256"), ("d3bfg.map", "link_map_sha256")):
            report[key] = staging.sha256(self.source / name)
        (self.source / "report.json").write_text(json.dumps(report))
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(staging, "REPO_ROOT", self.root).start()
        mock.patch.object(staging, "BOOT_ROOT", self.assets).start()

    def test_explicit_policy_replaces_stale_mode_and_preserves_user_config(self):
        for mode in ("ntsc", "pal", "headless"):
            staging.stage("debug", self.output, mode)
            self.assertEqual((self.output / "presentation.cfg").read_text(), mode + "\n")
            inputs = json.loads((self.output / "boot-data.json").read_text())
            self.assertEqual(inputs["presentation.cfg"], staging.sha256(self.output / "presentation.cfg"))
            (self.output / "base/d3bfg.cfg").write_text("user settings\n")
        self.assertEqual((self.output / "base/d3bfg.cfg").read_text(), "user settings\n")

    def test_default_policy_is_explicit_headless(self):
        staging.stage("debug", self.output)
        self.assertEqual((self.output / "presentation.cfg").read_text(), "headless\n")

    def test_invalid_policy_or_modified_build_is_rejected_before_staging(self):
        with self.assertRaisesRegex(ValueError, "presentation"):
            staging.stage("debug", self.output, "invalid")
        self.assertFalse(self.output.exists())
        (self.source / "d3bfg.elf").write_text("modified")
        with self.assertRaisesRegex(ValueError, "report does not match"):
            staging.stage("debug", self.output, "ntsc")
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
