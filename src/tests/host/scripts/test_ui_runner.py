#!/usr/bin/env python3
# ================================================================================================
# File: test_ui_runner.py
# Brief: Reject false SWF acceptance from stale logs, skipped native script work or corrupt captures.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import ast
import inspect
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/scripts"))
import run_ui as runner
import stage_headless as staging
import ui_capture


def completed(standard="ntsc"):
    log = (f"game initialized.\nLOOP ready;\nrun_begin \nrun_ready \n[D3BFG] GS ready {standard.upper()} 512x448\n"
           "[D3BFG] UI adapters ready\n")
    for cycle in (1, 2, 3):
        for phase, frames, pulse, focus, selected, selects in (("a", 120, 1, 0, -1, 0), ("b", 132, 2, 1, 1, 1)):
            frame = (cycle - 1) * 140 + frames
            redraws = cycle - 1 if phase == "a" else cycle
            log += (f"[D3BFG] SWF state=loaded cycle={cycle} frames={frames} pulse={pulse} focus={focus} "
                    f"selected={selected} selects={selects} heap=100/120/6\n"
                    f"[D3BFG] UI pages images=2 slots=4 cache=1048576 scratch=262144 metadata=56 "
                    f"loads=4 hits={frame * 4} evictions=0 triangles={frame * 20}\n"
                    f"[D3BFG] CONSOLE capture=ui-c{cycle}-{phase}.tga frame={frame} presents={frame} redraws={redraws}\n")
            if phase == "a":
                log += (f"[D3BFG] CONSOLE frames={frame + 1} presents={frame + 1} redraws={redraws} active=1\n"
                        "]echo ui_console\nui_console \n"
                        f"[D3BFG] CONSOLE frames={frame + 7} presents={frame + 7} redraws={redraws + 1} active=0\n")
        log += f"[D3BFG] SWF state=unloaded cycle={cycle} heap=80/96/4\n"
    log += ("run_done \n]quit\n[D3BFG] CONSOLE capture=ui-quit.tga frame=420 presents=420 redraws=3\n"
            "[D3BFG] CONSOLE shutdown complete frames=420 presents=420 redraws=3\n------------ Game Shutdown\n"
            "COMMON stage=system shutdown\nHEADLESS memory baseline=7424/7600/4 final=7424/7600/4 peak=10000/12000 arena=16384\n"
            "BOOT production shutdown complete\n")
    return "".join("[    1.0000] " + line for line in log.splitlines(keepends=True))


class NativeUiAcceptance(unittest.TestCase):
    def test_animation_probe_delays_avoid_fixed_phase_aliasing_without_weakening_acceptance(self):
        commands = [runner.animation_probe_commands("run", 2, probe) for probe in range(1, 13)]
        waits = [int(command[1].split()[1]) for command in commands]
        self.assertEqual(waits, [16, 1, 2, 3, 5, 7, 11, 13, 17, 19, 23, 29])
        self.assertEqual(len(set(waits)), 12)
        for probe, command in enumerate(commands, 1):
            self.assertEqual(command[0], "ps2_ui resume")
            self.assertEqual(command[2:4], ("ps2_ui pause", "ps2_ui status"))
            self.assertEqual(command[4], f'echo "run_c2_probe{probe}"')
        # Pause resets the native timeline remainder. At two render frames per pulse,
        # repeating the earlier four waits from phase 4 only samples phases 4 and 1.
        phase = 4
        aliased = []
        for frames in [16, 17, 19, 23] * 3:
            phase = 1 + ((phase - 1 + frames // 2) % 4)
            aliased.append(phase)
        self.assertEqual(aliased, [4, 4, 1, 4] * 3)
        # Short, non-repeating waits must expose a different required pulse 2/3 from
        # every initial phase at several render/timeline ratios, with either retained
        # or discarded fractional time. No phase or animation assertion is relaxed.
        for frames_per_pulse in (1, 2, 4, 8):
            for initial in (1, 2, 3, 4):
                for retain_remainder in (False, True):
                    phase, remainder = initial, 0
                    phases = []
                    for frames in waits:
                        ticks, remainder = divmod(frames + (remainder if retain_remainder else 0), frames_per_pulse)
                        phase = 1 + ((phase - 1 + ticks) % 4)
                        phases.append(phase)
                    self.assertTrue(any(phase != initial and phase in (2, 3) for phase in phases),
                                    (frames_per_pulse, initial, retain_remainder))
        for probe in (0, 13):
            with self.subTest(probe=probe), self.assertRaises(ValueError):
                runner.animation_probe_commands("run", 2, probe)

    def test_initial_and_retry_animation_probes_share_the_checked_command_builder(self):
        tree = ast.parse(inspect.getsource(runner.run))
        calls = [node for node in ast.walk(tree) if isinstance(node, ast.Call)
                 and isinstance(node.func, ast.Name) and node.func.id == "animation_probe_commands"]
        self.assertEqual(len(calls), 2)
        for call in calls:
            self.assertEqual([ast.unparse(argument) for argument in call.args], ["identity", "cycle", "probe"])

    def test_three_native_loads_in_both_standards(self):
        for standard in ("ntsc", "pal"):
            passed, reason = runner.classify(completed(standard), "run", standard)
            self.assertTrue(passed, reason)

    def test_stale_identity_or_submitted_echo_is_not_a_native_barrier(self):
        self.assertFalse(runner.echoed(']echo "run_ready"\n', "run_ready"))
        self.assertTrue(runner.echoed("[1.0] run_ready \n", "run_ready"))
        for old, new in (("run_begin ", "old_begin "), ("run_ready \n", ']echo "run_ready"\n'), ("run_done ", "")):
            self.assertFalse(runner.classify(completed().replace(old, new), "run", "ntsc")[0])

    def test_crash_fatal_and_incomplete_shutdown_override_good_captures(self):
        for log in (completed() + "[D3BFG] FATAL failure", completed() + "TLB Miss, pc=0x1000 addr=0x0 [load]",
                    completed().replace("BOOT production shutdown complete", "")):
            self.assertFalse(runner.classify(log, "run", "ntsc")[0])

    def test_native_animation_focus_and_selection_are_required(self):
        changes = (("frames=120", "frames=119"), ("pulse=2", "pulse=1"), ("pulse=2", "pulse=5"),
                   ("focus=1", "focus=0"), ("selected=1", "selected=-1"), ("selects=1", "selects=0"),
                   ("cycle=3 frames=", "cycle=2 frames="))
        for old, new in changes:
            with self.subTest(field=old):
                self.assertFalse(runner.classify(completed().replace(old, new), "run", "ntsc")[0])

    def test_console_overlay_requires_draw_edit_redraw_and_returned_ui_focus(self):
        for old, new in (("active=1", "active=0"), ("active=0", "active=1"), ("redraws=3", "redraws=2"),
                         ("]echo ui_console\n", ""), ("ui_console \n", ""),
                         ("frames=127 presents=127", "frames=121 presents=121"),
                         ("frames=127 presents=127", "frames=127 presents=128")):
            with self.subTest(field=old):
                self.assertFalse(runner.classify(completed().replace(old, new), "run", "ntsc")[0])

    def test_each_capture_must_follow_a_distinct_completed_frame(self):
        for old, new in (("ui-c2-a.tga", "missing.tga"), ("frame=132 presents=132", "frame=120 presents=120"),
                         ("frame=132 presents=132", "frame=132 presents=133"), ("frame=420 presents=420", "frame=419 presents=419")):
            with self.subTest(field=old):
                self.assertFalse(runner.classify(completed().replace(old, new, 1), "run", "ntsc")[0])

    def test_reload_and_full_shutdown_ledgers_must_recover(self):
        for old, new in (("state=unloaded cycle=2 heap=80/96/4", "state=unloaded cycle=2 heap=81/96/4"),
                         ("state=unloaded cycle=3", "state=unloaded cycle=4"),
                         ("final=7424/7600/4", "final=7424/7600/5"),
                         ("baseline=7424/7600/4", "baseline=7425/7600/4")):
            self.assertFalse(runner.classify(completed().replace(old, new), "run", "ntsc")[0])

    def test_movie_and_font_require_one_bounded_shared_page_cache(self):
        changes = (("images=2", "images=1"), ("slots=4", "slots=8"), ("cache=1048576", "cache=2097152"),
                   ("scratch=262144", "scratch=524288"), ("metadata=56", "metadata=112"),
                   ("loads=4", "loads=3"), ("loads=4", "loads=5"), ("hits=480", "hits=0"),
                   ("triangles=2400", "triangles=0"), ("evictions=0", "evictions=1"))
        for old, new in changes:
            with self.subTest(field=old):
                self.assertFalse(runner.classify(completed().replace(old, new), "run", "ntsc")[0])

    def test_shared_page_counters_must_advance_without_resetting_between_loads(self):
        for old, new in (("hits=528", "hits=480"), ("triangles=2640", "triangles=2400"),
                         ("loads=4 hits=1040", "loads=3 hits=1040")):
            with self.subTest(field=old):
                self.assertFalse(runner.classify(completed().replace(old, new), "run", "ntsc")[0])
        self.assertFalse(runner.classify(runner.PAGES_RE.sub("", completed()), "run", "ntsc")[0])

    def test_invalid_assets_require_the_specific_prepublication_guard(self):
        for case, expected in ui_capture.NEGATIVE_DIAGNOSTICS.items():
            if case == "movie-page-pixels":
                continue
            log = f"run_begin \n]ps2_ui load\n[D3BFG] FATAL {expected}\n"
            self.assertTrue(runner.classify_negative(log, "run", case, expected)[0])
            for modified in (log.replace(expected, "out of memory"), log.replace("run_begin ", "old_begin "),
                             log + "[D3BFG] SWF state=loaded cycle=1", log + "TLB Miss, pc=0x1000 addr=0x0 [load]"):
                self.assertFalse(runner.classify_negative(modified, "run", case, expected)[0])

    def test_corrupt_page_pixels_fail_on_the_first_checked_stream_without_capture(self):
        expected = ui_capture.NEGATIVE_DIAGNOSTICS["movie-page-pixels"]
        log = ("run_begin \n]ps2_ui load\n[D3BFG] SWF state=loaded cycle=1\n"
               "[D3BFG] UI page image rejected: page read or checksum\n"
               f"[D3BFG] FATAL {expected}\n")
        self.assertTrue(runner.classify_negative(log, "run", "movie-page-pixels", expected)[0])
        for modified in (log.replace("page read or checksum", "read complete"),
                         log.replace("[D3BFG] SWF state=loaded cycle=1\n", ""),
                         log + "[D3BFG] CONSOLE capture=ui-c1-a.tga frame=1 presents=1 redraws=0\n",
                         log + "BOOT production shutdown complete\n", log.replace(expected, "out of memory")):
            self.assertFalse(runner.classify_negative(modified, "run", "movie-page-pixels", expected)[0])


def capture(height, pulse=1, focus=0, selected=-1):
    # Populate only the verifier's read positions: this tests its strict capture/state checks,
    # while the target runner must obtain actual readable geometry from native GS captures.
    rgb = bytearray(bytes(ui_capture.BACKGROUND) * (512 * height))
    for _, x, y, color in ui_capture.expected_pixels(pulse, focus, selected):
        offset = (int(y * height / 480) * 512 + int(x * 512 / 640)) * 3
        rgb[offset:offset + 3] = bytes(color)
    bgr = bytearray(len(rgb))
    bgr[0::3], bgr[1::3], bgr[2::3] = rgb[2::3], rgb[1::3], rgb[0::3]
    return struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 512, height, 24, 0x20) + bgr


class NativeUiPixels(unittest.TestCase):
    def test_native_profile_both_standards_and_all_pulse_colors(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "ui.tga"
            for standard, height in (("ntsc", 448), ("pal", 512)):
                for pulse in (1, 2, 3, 4):
                    for focus, selected in ((0, -1), (1, 1)):
                        path.write_bytes(capture(height, pulse, focus, selected))
                        result = ui_capture.verify(path, standard, pulse=pulse, focus=focus, selected=selected)
                        self.assertGreaterEqual(len(result["checks"]), 30)

    def test_geometry_text_focus_or_animation_pixel_cannot_be_skipped(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "ui.tga"
            for name, x, y, _ in ui_capture.expected_pixels(2, 1, 1):
                image = bytearray(capture(448, 2, 1, 1))
                offset = 18 + (int(y * 448 / 480) * 512 + int(x * 512 / 640)) * 3
                image[offset:offset + 3] = b"\x00\x00\xff"
                path.write_bytes(image)
                with self.subTest(pixel=name), self.assertRaisesRegex(ValueError, name):
                    ui_capture.verify(path, "ntsc", pulse=2, focus=1, selected=1)

    def test_stale_state_truncation_and_wrong_dimensions_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "ui.tga"
            path.write_bytes(capture(448))
            for standard, pulse, focus, selected in (("pal", 1, 0, -1), ("ntsc", 2, 0, -1), ("ntsc", 1, 1, 1), ("ntsc", 0, 0, -1)):
                with self.assertRaises(ValueError):
                    ui_capture.verify(path, standard, pulse=pulse, focus=focus, selected=selected)
            path.write_bytes(path.read_bytes()[:-1])
            with self.assertRaises(ValueError):
                ui_capture.verify(path, "ntsc", pulse=1, focus=0, selected=-1)


class AuthoredUiStaging(unittest.TestCase):
    def test_ui_package_is_opt_in_and_all_mutations_stay_inside_it(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "build/debug"
            source.mkdir(parents=True)
            boot = root / "assets/headless"
            (boot / "base").mkdir(parents=True)
            (boot / "base/d3bfg.cfg").write_text("ps2_status\n")
            ui = root / "assets/ui"
            originals = {}
            page = struct.pack("<8I", 0x47504955, 1, 250, 128, 256, 1, 1, 0)
            page += struct.pack("<2I", 40, 123) + bytes(262144)
            for relative in (ui_capture.MOVIE_PATH, ui_capture.FONT_PATH, ui_capture.MOVIE_PAGE_PATH, ui_capture.FONT_PAGE_PATH):
                path = ui / "base" / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                originals[relative] = page if relative.endswith(".uipg") else b"authored-data"
                path.write_bytes(originals[relative])
            for name in ("d3bfg.elf", "d3bfg_unstripped.elf", "d3bfg.map", "report.txt", "objects.rsp"):
                (source / name).write_text(name)
            report = {"passed": True, "kind": "game", "test_inputs": [], "test_symbols": [], "flag_stamps": {}}
            for name, key in (("d3bfg.elf", "runnable_elf_sha256"), ("d3bfg_unstripped.elf", "elf_sha256"), ("d3bfg.map", "link_map_sha256")):
                report[key] = staging.sha256(source / name)
            (source / "report.json").write_text(json.dumps(report))
            with mock.patch.multiple(staging, REPO_ROOT=root, BOOT_ROOT=boot, UI_ROOT=ui):
                plain = staging.stage("debug", root / "plain")
                self.assertFalse((plain / "base" / ui_capture.MOVIE_PATH).exists())
                headless = staging.stage("debug", root / "ui-headless", include_ui=True)
                self.assertTrue((headless / "base" / ui_capture.MOVIE_PATH).exists())
                self.assertNotIn("ps2_ui load", (headless / "base/d3bfg.cfg").read_text())
                for case in runner.NEGATIVES:
                    output = staging.stage("debug", root / case, "ntsc", include_ui=True)
                    self.assertEqual((output / "base/d3bfg.cfg").read_text().count("ps2_ui load\n"), 1)
                    staging.stage("debug", output, "ntsc", include_ui=True)
                    self.assertEqual((output / "base/d3bfg.cfg").read_text().count("ps2_ui load\n"), 1)
                    mutation = runner.mutate(output, case)
                    self.assertTrue(mutation["path"].startswith("base/"))
                    self.assertNotEqual(mutation["mutated_sha256"], mutation["original_sha256"])
                    for relative, original in originals.items():
                        self.assertEqual((ui / "base" / relative).read_bytes(), original)
                with self.assertRaisesRegex(ValueError, "separate package"):
                    staging.stage("debug", ui, "ntsc", include_ui=True)
                (plain / "base/d3bfg.cfg").write_text("user preferences\n")
                staging.stage("debug", plain, "ntsc", include_ui=True)
                self.assertEqual((plain / "base/d3bfg.cfg").read_text(), "user preferences\n")


if __name__ == "__main__":
    unittest.main()
