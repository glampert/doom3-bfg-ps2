#!/usr/bin/env python3
# ================================================================================================
# File: run_ui_session.py
# Brief: Run every retail UI screen in one production session per standard, from the boot menu to its quit dialog.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""One emulator launch per video standard covers the retail UI. The package boots into the native
menu; each step then drives one screen through HostFs commands and is checked on its own slice of
the log, between echo barriers, while the session as a whole must present one frame per Common
frame, end through the quit dialog and recover the startup ledger. Images a step opens stay warm
for the rest of the session, so each step's texture counters include its predecessors'."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
import uuid

import console_capture
import run_console
import run_headless
import run_pcsx2_test as smoke
import run_retail_movies as movies
import run_retail_ui as fonts
from run_console import BASELINE, TARGET_CRASH_RE, echoed
from stage_headless import REPO_ROOT, boot_data, sha256
from stage_retail_ui import inspect_package, stage_retail


# Each step and the seconds it may take in a debug build under PCSX2, from its first command.
STEPS = (("boot", 300), ("console", 90), ("fonts", 180), ("inactive", 180), ("execution", 300), ("dialog", 180),
         ("shell", 300), ("menu", 420), ("screens", 300), ("quit", 300))
STARTUP_SECONDS = 120
CAPTURE_RE = re.compile(r"\[D3BFG\] CONSOLE capture=(\S+\.tga) frame=(\d+) presents=(\d+) redraws=(\d+)")


def barrier(identity: str, step: str, edge: str) -> str:
    return f"{identity}_{step}_{edge}"


def drivers(identity: str, manifest: dict) -> dict:
    """Each step's command batches, with the condition over its log that ends each wait."""
    begin = lambda step: barrier(identity, step, "begin")
    end = lambda step: barrier(identity, step, "end")
    return {
        "boot": movies.menu_boot_driver(end("boot")),
        "console": run_console.driver(identity, begin("console"), end("console")),
        "fonts": fonts.driver(manifest, begin("fonts"), end("fonts")),
        "inactive": movies.movies_driver(begin("inactive"), end("inactive"), execution=False),
        "execution": movies.movies_driver(begin("execution"), end("execution"), execution=True),
        "dialog": movies.render_driver(begin("dialog"), end("dialog"), "ps2_uiRetail dialog quit", "real"),
        "shell": movies.render_driver(begin("shell"), end("shell"), "ps2_uiRetail draw shell", "fixed",
                                      movies.SHELL_RENDER_PHASES),
        "menu": movies.render_driver(begin("menu"), end("menu"), "ps2_uiRetail menu", "fixed", movies.MENU_RENDER_PHASES),
        "screens": movies.menu_screens_driver(begin("screens"), end("screens")),
        "quit": movies.menu_quit_driver(begin("quit"), end("quit")),
    }


def echo_span(log: str, marker: str):
    return re.search(re.escape(marker) + r" +\r?\n", log)


def windows(log: str, identity: str) -> dict:
    """Each step's slice of the log: from its begin barrier's output to its end barrier's. The boot step
    starts with the log, since the menu opened before the startup commands; the quit step ends with it."""
    result = {}
    for step, _ in STEPS:
        start = 0 if step == "boot" else echo_span(log, barrier(identity, step, "begin"))
        finish = len(log) if step == "quit" else echo_span(log, barrier(identity, step, "end"))
        if start is None or finish is None:
            continue
        first = start if isinstance(start, int) else start.end()
        last = finish if isinstance(finish, int) else finish.start()
        if first < last:
            result[step] = log[first:last]
    return result


def classify_session(log: str, identity: str, standard: str) -> tuple[bool, str]:
    """Checks that span the session: boot, barrier order, one presentation per frame, the final
    capture drained by the dialog's quit and the startup ledger recovered."""
    if TARGET_CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
        return False, "crash or fatal diagnostic"
    required = (f"[D3BFG] BOOT presentation={standard} menu=1", f"[D3BFG] GS ready {standard.upper()}",
                "[D3BFG] UI adapters ready", "------------ Game Shutdown", "COMMON stage=system shutdown",
                "BOOT production shutdown complete")
    if any(marker not in log for marker in required):
        return False, "incomplete native menu boot or shutdown"
    markers = [identity + "_ready"] + [barrier(identity, step, edge) for step, _ in STEPS for edge in ("begin", "end")
                                       if (step, edge) != ("boot", "begin")]
    positions = [echo_span(log, marker) for marker in markers]
    if any(position is None for position in positions):
        return False, "missing native step barrier"
    if any(later.start() <= earlier.start() for earlier, later in zip(positions, positions[1:])):
        return False, "native step barriers ran out of order"
    if "shell capability unavailable" in log:
        return False, "a screen outside the reviewed shell was reached"
    # The inactive and executing movie steps draw isolated English labels at the movie's largest
    # text height through native quads; every screen the menu shows must use pre-rendered sizes.
    sliced = windows(log, identity)
    labels = [sliced[step] for step in ("inactive", "execution") if step in sliced]
    screens = log
    for label in labels:
        screens = screens.replace(label, "")
    if movies.UNRENDERED.search(screens):
        return False, "native text drew at a size without pre-rendered glyphs"
    shutdown = run_console.SHUTDOWN_RE.findall(log)
    if len(shutdown) != 1:
        return False, "missing or duplicate native presentation shutdown"
    frames, presents, _ = map(int, shutdown[0])
    if frames != presents:
        return False, "native frames and presentations differ"
    if any(int(frame) != int(present) or int(frame) > frames for frame, present, _, _ in run_console.STATUS_RE.findall(log)):
        return False, "a redraw advanced simulation or presented an extra frame"
    captures = CAPTURE_RE.findall(log)
    if len({name for name, _, _, _ in captures}) != len(captures):
        return False, "duplicate native capture"
    previous = 0
    for _, frame, present, _ in captures:
        if int(frame) <= previous or int(frame) != int(present):
            return False, "a capture did not follow a distinct completed presentation"
        previous = int(frame)
    if not captures or captures[-1][0] != "movie-quit.tga" or (int(captures[-1][1]), int(captures[-1][2])) != (frames, presents):
        return False, "the quit dialog did not end the frame loop right after the final capture"
    memory = run_headless.MEMORY_RE.findall(log)
    if len(memory) != 1 or tuple(map(int, memory[0][:3])) != BASELINE or memory[0][:3] != memory[0][3:6]:
        return False, "production shutdown did not recover the startup allocation ledger"
    return True, "native UI session completed"


def classify_steps(log: str, identity: str, standard: str, manifest: dict, source: Path) -> dict:
    """Each step's verdict on its own slice of the log; warm images accumulate in step order."""
    sliced = windows(log, identity)
    images = {"boot": movies.menu_images(manifest), "fonts": fonts.resident_images(manifest),
              "shell": movies.shell_images(manifest), "menu": movies.menu_images(manifest),
              "screens": movies.menu_images(manifest), "quit": movies.menu_images(manifest)}
    checks = {
        "boot": lambda window, warm: movies.classify_menu_boot(window, identity, standard, manifest, source, warm),
        "console": lambda window, warm: run_console.classify(window, identity),
        "fonts": lambda window, warm: fonts.classify(window, manifest, source, warm),
        "inactive": lambda window, warm: movies.classify(window, manifest, source, False, movies.PHASES, warm),
        "execution": lambda window, warm: movies.classify(window, manifest, source, True, movies.PHASES, warm),
        "dialog": lambda window, warm: movies.classify_dialog_render(window, manifest, source, warm),
        "shell": lambda window, warm: movies.classify_shell_render(window, manifest, source, False, warm),
        "menu": lambda window, warm: movies.classify_shell_render(window, manifest, source, True, warm),
        "screens": lambda window, warm: movies.classify_menu_screens(window, manifest, source, warm),
        "quit": lambda window, warm: movies.classify_menu_quit(window, manifest, source, warm),
    }
    warm = set()
    results = {}
    for step, _ in STEPS:
        if step not in sliced:
            results[step] = (False, "step did not run")
            continue
        try:
            results[step] = checks[step](sliced[step], frozenset(warm))
        except (KeyError, IndexError, ValueError, TypeError) as error:
            # A slice whose native state is missing or malformed fails the step, not the runner.
            results[step] = (False, f"unreadable native state: {error!r}")
        warm |= images.get(step, set())
    return results


def verify_step_captures(step: str, output: Path, standard: str, manifest: dict, source: Path) -> dict:
    if step == "boot":
        return movies.verify_menu_captures(output, standard, movies.menu_boot_capture_names())
    if step == "console":
        return {"console-edit.tga": console_capture.verify_edit(output / "console-edit.tga", standard, manifest, source)}
    if step == "fonts":
        return fonts.verify_captures(output, manifest, source, standard)
    if step in ("inactive", "execution"):
        captures = {}
        for name, _, movie in movies.PHASES:
            path = output / movies.movie_capture_name(step, name)
            captures[path.name] = movies.verify_capture(path, manifest, source, movie, standard)
        captures["reload_comparisons"] = movies.verify_reload_captures(output, movies.PHASES, captures, manifest, source,
                                                                       standard, step)
        return captures
    if step == "dialog":
        return movies.verify_dialog_render_captures(output, standard)
    if step in ("shell", "menu"):
        return movies.verify_shell_render_captures(output, standard,
                                                   movies.SHELL_RENDER_PHASES if step == "shell" else movies.MENU_RENDER_PHASES)
    if step == "screens":
        return movies.verify_menu_captures(output, standard, movies.menu_screens_capture_names())
    captures = movies.verify_menu_captures(output, standard, movies.menu_quit_capture_names()[:-1])
    captures["movie-quit.tga"] = movies.verify_final_capture(output / "movie-quit.tga", standard)
    return captures


def run(args: argparse.Namespace, standard: str, version: str) -> bool:
    if smoke.emulator_is_running(args.emulator):
        raise ValueError("an emulator is already running; the UI session requires an isolated emulator")
    smoke.check_config(args.config)
    manifest, _, _ = inspect_package(args.source)
    identity = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_session_" + uuid.uuid4().hex[:12]
    output = stage_retail(args.build, REPO_ROOT / "build/test-results" / identity, standard, args.source, True, True)
    report = json.loads((output / "report.json").read_text())
    for name in report["flag_stamps"]:
        shutil.copy2(REPO_ROOT / name, output / Path(name).name)
    shutil.copy2(args.config, output / "PCSX2.ini.snapshot")
    (output / "base/d3bfg.cfg").write_text(f'echo "{identity}_ready"\nps2_maxFrames 0\n')
    (output / "console.txt").write_bytes(b"")
    (output / "boot-data.json").write_text(json.dumps(boot_data(output), indent=2) + "\n")
    command = [str(args.emulator), "-batch", "-elf", str(output / "d3bfg.elf"), "-logfile", str(output / "emulog.txt")]
    steps = drivers(identity, manifest)
    order = [step for step, _ in STEPS]
    budgets = dict(STEPS)
    index, generator, until, offset = -1, None, None, 0
    step_started, timings = {}, {}
    started = time.monotonic()
    deadline = started + STARTUP_SECONDS * args.timeout_scale
    completed, failure = False, "watchdog timeout during startup"

    def start(step_index: int, log: str):
        nonlocal index, generator, until, offset, deadline
        index = step_index
        generator = steps[order[index]]
        offset = len(log)
        deadline = time.monotonic() + budgets[order[index]] * args.timeout_scale
        step_started[order[index]] = time.monotonic()
        commands, until = next(generator)
        run_console.append_commands(output, *commands)

    with (output / "stdout.txt").open("wb") as stdout:
        process = subprocess.Popen(command, cwd=output, stdout=stdout, stderr=subprocess.STDOUT)
        try:
            while True:
                log = run_headless.read_log(output)
                if TARGET_CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
                    failure = f"crash or fatal diagnostic during {order[index] if index >= 0 else 'startup'}"
                    break
                if "BOOT production shutdown complete" in log:
                    completed = True
                    if index >= 0:
                        timings[order[index]] = time.monotonic() - step_started[order[index]]
                    break
                if index < 0:
                    if echoed(log, identity + "_ready"):
                        start(0, log)
                elif until(log[offset:]):
                    try:
                        commands, until = next(generator)
                        run_console.append_commands(output, *commands)
                    except StopIteration:
                        timings[order[index]] = time.monotonic() - step_started[order[index]]
                        if index + 1 < len(order):
                            start(index + 1, log)
                if time.monotonic() > deadline:
                    failure = f"watchdog timeout during {order[index] if index >= 0 else 'startup'}"
                    break
                if process.poll() is not None:
                    failure = f"emulator exited with status {process.returncode} after {time.monotonic() - started:.1f} s"
                    break
                time.sleep(0.1)
        finally:
            smoke.stop_owned_process(process)
    log = run_headless.read_log(output)
    passed, reason = classify_session(log, identity, standard) if completed else (False, failure)
    results = classify_steps(log, identity, standard, manifest, args.source)
    steps_summary = {}
    for step in order:
        step_passed, step_reason = results[step]
        captures = {}
        if step_passed:
            try:
                captures = verify_step_captures(step, output, standard, manifest, args.source)
            except (OSError, ValueError) as error:
                step_passed, step_reason = False, str(error)
        steps_summary[step] = {"passed": step_passed, "reason": step_reason, "captures": captures}
        if passed and not step_passed:
            passed, reason = False, f"{step}: {step_reason}"
    if process.returncode != 0:
        passed, reason = False, f"emulator exited with status {process.returncode}"
    summary = {"passed": passed, "reason": reason, "configuration": args.build, "standard": standard, "command": command,
               "emulator_version": version, "elapsed_seconds": round(time.monotonic() - started, 1),
               "step_seconds": {step: round(seconds, 1) for step, seconds in timings.items()},
               "emulator_returncode": process.returncode, "boot_data": boot_data(output),
               "source_manifest_sha256": sha256(args.source / "manifest.json"), "console_sha256": sha256(output / "console.txt"),
               "steps": steps_summary, "pixel_inspection_required": True}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    for step in order:
        print(f"  {'pass' if steps_summary[step]['passed'] else 'FAIL'} {step}: {steps_summary[step]['reason']}", flush=True)
    print(f"{'PASS' if passed else 'FAIL'} ui-session-{standard}: {reason}\nArtifacts: {output}", flush=True)
    if not passed:
        print(log[-6000:], flush=True)
    return passed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", choices=("debug", "release"), default="debug")
    parser.add_argument("--source", type=Path, default=REPO_ROOT / "build/ui-retail")
    parser.add_argument("--standard", choices=("ntsc", "pal"), help="default: both, one session each")
    parser.add_argument("--emulator", type=Path, default=smoke.DEFAULT_EMULATOR)
    parser.add_argument("--config", type=Path, default=smoke.DEFAULT_CONFIG)
    parser.add_argument("--timeout-scale", type=float, default=1.0, help="multiplies every step's watchdog")
    args = parser.parse_args()
    if not 0 < args.timeout_scale <= 10:
        parser.error("timeout scale must be positive and at most 10")
    try:
        if smoke.emulator_is_running(args.emulator):
            raise ValueError("an emulator is already running; close it before starting the UI session")
        smoke.check_config(args.config)
        version = smoke.emulator_version(args.emulator)
        for standard in (args.standard,) if args.standard else ("ntsc", "pal"):
            if not run(args, standard, version):
                return 1
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        parser.exit(2, "error: " + str(error) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
