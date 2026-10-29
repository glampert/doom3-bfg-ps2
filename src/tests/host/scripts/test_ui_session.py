#!/usr/bin/env python3
# ================================================================================================
# File: test_ui_session.py
# Brief: Check the merged UI session's step order, log slices and session-wide acceptance without an emulator.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "src/tools/scripts"))
import run_ui_session as session


def replace_last(text, old, new):
    head, separator, tail = text.rpartition(old)
    if not separator:
        raise AssertionError("missing log text: " + old)
    return head + new + tail


def session_log(standard="ntsc"):
    """A session's log reduced to what spans its steps: boot, barriers, captures and shutdown."""
    log = (f"[D3BFG] BOOT presentation={standard} menu=1\n[D3BFG] GS ready {standard.upper()} 512x448\n"
           "[D3BFG] UI adapters ready\nrun_ready \n")
    frame = 0
    for step, _ in session.STEPS:
        if step != "boot":
            log += f"run_{step}_begin \n"
        frame += 10
        log += f"[D3BFG] CONSOLE capture={step}-a.tga frame={frame} presents={frame} redraws=1\n"
        log += f"[D3BFG] CONSOLE frames={frame} presents={frame} redraws=1 active=1\n"
        log += f"run_{step}_end \n"
    frame += 1
    return log + (f"[D3BFG] CONSOLE capture=movie-quit.tga frame={frame} presents={frame} redraws=1\n"
                  f"------------ Game Shutdown\n[D3BFG] CONSOLE shutdown complete frames={frame} presents={frame} redraws=1\n"
                  "COMMON stage=system shutdown\n"
                  "Memory baseline=7424/7600/4 final=7424/7600/4 peak=10/20 arena=30\n"
                  "======== Shutdown complete ========\n")


class SessionAcceptance(unittest.TestCase):
    def verdict(self, log, standard="ntsc"):
        return session.classify_session(log, "run", standard)

    def test_complete_session_in_both_standards(self):
        for standard in ("ntsc", "pal"):
            self.assertEqual(self.verdict(session_log(standard), standard), (True, "native UI session completed"))
        self.assertFalse(self.verdict(session_log("pal"), "ntsc")[0])

    def test_boot_policy_barriers_and_shutdown_are_required(self):
        log = session_log()
        for changed in (log.replace("menu=1", "menu=0"), log.replace("run_ready \n", ""),
                        log.replace("run_fonts_begin \n", ""), log.replace("run_quit_end \n", ""),
                        log.replace("run_console_begin \n", "").replace("run_fonts_end \n", "run_fonts_end \nrun_console_begin \n"),
                        log.replace("======== Shutdown complete ========\n", ""),
                        log + "[D3BFG] FATAL unexpected\n", log + "TLB Miss, pc=0x1000 addr=0x0 [load]\n"):
            with self.subTest(changed=changed):
                self.assertFalse(self.verdict(changed)[0])

    def test_unreviewed_screens_and_unrendered_text_fail_anywhere(self):
        log = session_log()
        for line in ("shell capability unavailable: screen 5\n",
                     "[D3BFG] UI glyph size font=sarasori_rg scale=0.30000,0.35000 stroke=2.000 prerendered=no\n"):
            with self.subTest(line=line):
                self.assertFalse(self.verdict(log.replace("run_dialog_end \n", line + "run_dialog_end \n"))[0])

    def test_only_the_movie_label_steps_may_draw_unrendered_sizes(self):
        line = "[D3BFG] UI glyph size font=sarasori_rg scale=0.60000,0.70000 stroke=0.000 prerendered=no\n"
        log = session_log()
        for step in ("inactive", "execution"):
            with self.subTest(step=step):
                self.assertTrue(self.verdict(log.replace(f"run_{step}_end \n", line + f"run_{step}_end \n"))[0])
        for step in ("boot", "console", "fonts", "dialog", "shell", "menu", "screens", "quit"):
            with self.subTest(step=step):
                self.assertFalse(self.verdict(log.replace(f"run_{step}_end \n", line + f"run_{step}_end \n"))[0])

    def test_frames_captures_and_ledger_span_the_whole_session(self):
        log = session_log()
        final = log.split("movie-quit.tga frame=")[1].split()[0]
        for changed in (log.replace(f"shutdown complete frames={final} presents={final}",
                                    f"shutdown complete frames={final} presents={int(final) + 1}"),
                        log + f"[D3BFG] CONSOLE shutdown complete frames={final} presents={final} redraws=1\n",
                        log.replace("frames=50 presents=50 redraws=1 active=1", "frames=50 presents=51 redraws=1 active=1"),
                        log.replace("capture=fonts-a.tga", "capture=boot-a.tga"),
                        log.replace("capture=fonts-a.tga frame=30 presents=30", "capture=fonts-a.tga frame=30 presents=31"),
                        log.replace("capture=fonts-a.tga frame=30", "capture=fonts-a.tga frame=20"),
                        replace_last(log, f"frame={final} presents={final} redraws=1\n------------",
                                     f"frame={int(final) - 1} presents={int(final) - 1} redraws=1\n------------"),
                        log.replace("final=7424/7600/4", "final=7488/7600/4")):
            with self.subTest(changed=changed):
                self.assertFalse(self.verdict(changed)[0])


class SessionSlices(unittest.TestCase):
    def test_each_step_sees_only_its_own_slice(self):
        log = session_log()
        sliced = session.windows(log, "run")
        self.assertEqual(list(sliced), [step for step, _ in session.STEPS])
        # The menu opened before the startup commands: the boot slice starts with the log.
        self.assertTrue(sliced["boot"].startswith("[D3BFG] BOOT presentation=ntsc menu=1"))
        self.assertIn("boot-a.tga", sliced["boot"])
        self.assertNotIn("console-a.tga", sliced["boot"])
        self.assertEqual(sliced["fonts"].count("CONSOLE capture="), 1)
        self.assertIn("fonts-a.tga", sliced["fonts"])
        # The quit dialog ends the session: its slice runs to the end of the log.
        self.assertTrue(sliced["quit"].endswith("======== Shutdown complete ========\n"))
        self.assertIn("movie-quit.tga", sliced["quit"])

    def test_a_step_without_both_barriers_did_not_run(self):
        log = session_log().replace("run_shell_end \n", "")
        sliced = session.windows(log, "run")
        self.assertNotIn("shell", sliced)
        self.assertIn("menu", sliced)
        with mock.patch.object(session.movies, "menu_images", return_value=set()), \
                mock.patch.object(session.movies, "shell_images", return_value=set()), \
                mock.patch.object(session.fonts, "resident_images", return_value=set()):
            results = session.classify_steps(log, "run", "ntsc", {}, Path("."))
        self.assertEqual(results["shell"], (False, "step did not run"))
        # The synthetic slices hold none of the steps' native state.
        self.assertFalse(any(passed for passed, _ in results.values()))


class SessionDrivers(unittest.TestCase):
    def test_steps_run_in_order_between_their_barriers(self):
        manifest = {"fonts": {"newfonts/proof/48.dat": {}, "newfonts/other/48.dat": {}}}
        drivers = session.drivers("run", manifest)
        self.assertEqual(list(drivers), [step for step, _ in session.STEPS])
        for step, generator in drivers.items():
            batches = [commands for commands, until in generator]
            with self.subTest(step=step):
                if step == "boot":
                    # The startup policy opened the menu: no load or menu command precedes the keys.
                    self.assertFalse(any(command in ("ps2_uiRetail load", "ps2_uiRetail menu") for command in batches[0]))
                else:
                    self.assertEqual(batches[0][0], f'echo "run_{step}_begin"')
                if step == "quit":
                    # The dialog's accept quits after the queued final capture; nothing follows it.
                    self.assertEqual(batches[-1][-3:], ['echo "run_quit_end"', 'ps2_capture "movie-quit.tga"',
                                                        "ps2_uiRetail key cross"])
                else:
                    self.assertEqual(batches[-1][-1], f'echo "run_{step}_end"')
                    # Every step but the console releases what it loaded before the next one starts.
                    if step != "console":
                        self.assertIn("ps2_uiRetail unload", batches[-1])

    def test_clocks_are_chosen_before_each_drawn_movie_loads(self):
        drivers = session.drivers("run", {"fonts": {}})
        first = {step: next(drivers[step])[0] for step in ("dialog", "shell", "menu", "screens", "quit")}
        self.assertIn("ps2_uiRetail clock real", first["dialog"])
        for step in ("shell", "menu", "screens", "quit"):
            with self.subTest(step=step):
                commands = first[step]
                self.assertLess(commands.index("ps2_uiRetail clock fixed"),
                                min(index for index, command in enumerate(commands)
                                    if command in ("ps2_uiRetail draw shell", "ps2_uiRetail menu")))


if __name__ == "__main__":
    unittest.main()
