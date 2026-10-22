#!/usr/bin/env python3
# ================================================================================================
# File: run_console.py
# Brief: Exercise the production native console, edit history, final presentation and ordered shutdown.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

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
import run_headless
import run_pcsx2_test as smoke
from stage_headless import REPO_ROOT, boot_data, sha256, stage


STATUS_RE = re.compile(r"\[D3BFG\] CONSOLE frames=(\d+) presents=(\d+) redraws=(\d+) active=([01])")
CAPTURE_RE = re.compile(r"\[D3BFG\] CONSOLE capture=(console-(?:edit|quit)\.tga) frame=(\d+) presents=(\d+) redraws=(\d+)")
SHUTDOWN_RE = re.compile(r"\[D3BFG\] CONSOLE shutdown complete frames=(\d+) presents=(\d+) redraws=(\d+)")


def native_echo(identity: str) -> str:
    return "echo native_console_" + identity.rsplit("_", 1)[-1]


def classify(log: str, identity: str, standard: str) -> tuple[bool, str]:
    if smoke.CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
        return False, "crash or fatal diagnostic"
    required = ("game initialized.", "LOOP ready;", f"{identity}_begin", f"{identity}_ready",
                f"[D3BFG] GS ready {standard.upper()}", "[D3BFG] UI adapters ready",
                "------------ Game Shutdown", "COMMON stage=system shutdown", "BOOT production shutdown complete")
    if any(marker not in log for marker in required):
        return False, "incomplete native visible boot/shutdown or stale identity"
    shutdowns = SHUTDOWN_RE.findall(log)
    if len(shutdowns) != 1:
        return False, "missing or duplicate visible shutdown"
    frames, presents, redraws = map(int, shutdowns[0])
    if frames < 120 or frames != presents or redraws < 3:
        return False, "native frames, presentations or redraw-only requests are inconsistent"
    statuses = [tuple(map(int, match)) for match in STATUS_RE.findall(log)]
    if len(statuses) < 4 or not any(status[3] == 0 for status in statuses) or statuses[-1] != (frames, presents, redraws, 1):
        return False, "native console did not close, reopen and finish active"
    if any(frame != present or frame > frames or redraw > redraws for frame, present, redraw, _ in statuses):
        return False, "status reports unexpected extra presentations or ticks"
    # A timestamp's closing bracket precedes each native prompt in PCSX2's file log.
    command_lines = re.findall(r"\]([A-Za-z][^\r\n]*)", log)
    if command_lines.count(native_echo(identity)) != 2 or "history" not in command_lines or "quit" not in command_lines:
        return False, "native edit submission, history replay or quit did not execute"
    echo_output = re.escape(native_echo(identity).removeprefix("echo ")) + r" +\r?\n"
    if len(re.findall(echo_output, log)) != 2:
        return False, "native echo command output was not produced twice"
    captures = CAPTURE_RE.findall(log)
    if len(captures) != 2 or [capture[0] for capture in captures] != ["console-edit.tga", "console-quit.tga"]:
        return False, "missing, reordered or duplicate native captures"
    edit_frame = int(captures[0][1])
    for _, frame, present, redraw in captures:
        if not 120 <= int(frame) <= frames or int(frame) != int(present) or not 2 <= int(redraw) <= redraws:
            return False, "capture did not follow a completed native presentation"
    if int(captures[1][1]) < edit_frame or int(captures[1][1]) != frames:
        return False, "pending quit capture was not drained by the final native frame"
    memory = run_headless.MEMORY_RE.findall(log)
    if len(memory) != 1 or memory[0][:3] != memory[0][3:6]:
        return False, "startup allocations were not recovered"
    return True, "native visible console completed"


def append_commands(output: Path, *commands: str) -> None:
    with (output / "console.txt").open("ab") as stream:
        stream.write(("\n".join(commands) + "\n").encode("ascii"))


def run(args: argparse.Namespace, standard: str, version: str) -> bool:
    if smoke.emulator_is_running(args.emulator):
        raise ValueError("an emulator is already running; each console run requires an isolated session")
    smoke.check_config(args.config)
    identity = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_console_" + uuid.uuid4().hex[:12]
    output = stage(args.build, REPO_ROOT / "build/test-results" / identity, standard, catalog=args.source)
    report = json.loads((output / "report.json").read_text())
    for name in report["flag_stamps"]:
        shutil.copy2(REPO_ROOT / name, output / Path(name).name)
    shutil.copy2(args.config, output / "PCSX2.ini.snapshot")
    (output / "base/d3bfg.cfg").write_text(f'echo "{identity}_begin"\nwait 120\necho "{identity}_ready"\nps2_status\n')
    (output / "boot-data.json").write_text(json.dumps(boot_data(output), indent=2) + "\n")
    (output / "console.txt").write_bytes(b"")
    command = [str(args.emulator), "-batch", "-elf", str(output / "d3bfg.elf"), "-logfile", str(output / "emulog.txt")]
    started = time.monotonic()
    phase = "startup"
    reopened_at = None
    completed = False
    failure = "watchdog timeout"
    with (output / "stdout.txt").open("wb") as stdout:
        process = subprocess.Popen(command, cwd=output, stdout=stdout, stderr=subprocess.STDOUT)
        try:
            while time.monotonic() - started < args.timeout:
                log = run_headless.read_log(output)
                if smoke.CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
                    failure = "crash or fatal diagnostic"
                    break
                if phase == "startup" and f"{identity}_ready" in log:
                    append_commands(output, f'ps2_consoleText "{native_echo(identity)}"', "ps2_consoleKey enter")
                    phase = "echo"
                elif phase == "echo" and re.search(re.escape("]" + native_echo(identity)) + r"\r?\n", log):
                    append_commands(output, "ps2_consoleKey up", "ps2_consoleKey enter")
                    phase = "history"
                elif phase == "history" and len(re.findall(re.escape("]" + native_echo(identity)) + r"\r?\n", log)) == 2:
                    append_commands(output, "ps2_consoleKey toggle", "ps2_status")
                    phase = "closed"
                elif phase == "closed" and any(match[3] == "0" for match in STATUS_RE.findall(log)):
                    append_commands(output, "ps2_consoleKey toggle", "ps2_status")
                    phase = "reopened"
                    reopened_at = time.monotonic()
                elif phase == "reopened" and time.monotonic() - reopened_at > 0.75:
                    # Deliberate correction and cursor movement exercise the native edit field.
                    append_commands(output, 'ps2_consoleText "echo pendinx"', "ps2_consoleKey backspace",
                                    'ps2_consoleText "g"', "ps2_consoleKey left", "ps2_consoleKey right",
                                    'ps2_capture "console-edit.tga"', "ps2_status")
                    phase = "edit-capture"
                elif phase == "edit-capture" and "[D3BFG] CONSOLE capture=console-edit.tga " in log:
                    # Capture and quit share one input batch: quit must still finish and capture its frame.
                    append_commands(output, "history", "ps2_consoleKey home", *(["ps2_consoleKey delete"] * len("echo pending")),
                                    'ps2_consoleText "quit"', 'ps2_capture "console-quit.tga"', "ps2_consoleKey enter")
                    phase = "quit"
                if "BOOT production shutdown complete" in log:
                    completed = True
                    break
                if process.poll() is not None:
                    failure = "emulator exited before completion"
                    break
                time.sleep(0.05)
        finally:
            smoke.stop_owned_process(process)
    log = run_headless.read_log(output)
    passed, reason = classify(log, identity, standard) if completed else (False, failure)
    capture_checks = {}
    if passed:
        try:
            manifest = json.loads((args.source / "manifest.json").read_text())
            capture_checks["console-edit.tga"] = console_capture.verify_edit(output / "console-edit.tga", standard, manifest, args.source)
            width, height, _ = console_capture.read_tga(output / "console-quit.tga")
            expected_height = 448 if standard == "ntsc" else 512
            if (width, height) != (512, expected_height):
                raise ValueError("final quit capture has incorrect dimensions")
            capture_checks["console-quit.tga"] = {"width": width, "height": height, "sha256": sha256(output / "console-quit.tga")}
        except (OSError, ValueError) as error:
            passed, reason = False, str(error)
    if process.returncode != 0:
        passed, reason = False, f"emulator exited with status {process.returncode}"
    data = {"passed": passed, "reason": reason, "configuration": args.build, "standard": standard,
            "command": command, "emulator_version": version, "elapsed_seconds": time.monotonic() - started,
            "emulator_returncode": process.returncode, "boot_data": boot_data(output),
            "console_sha256": sha256(output / "console.txt"), "capture_checks": capture_checks,
            "pixel_inspection_required": True}
    (output / "summary.json").write_text(json.dumps(data, indent=2) + "\n")
    print(f"{'PASS' if passed else 'FAIL'} console-{standard}: {reason}\nArtifacts: {output}", flush=True)
    if not passed:
        print(log[-6000:], flush=True)
    return passed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", choices=("debug", "release"), default="debug")
    parser.add_argument("--source", type=Path, default=REPO_ROOT / "build/ui-retail",
                        help="the cooked retail UI whose catalog supplies the console charset")
    parser.add_argument("--standard", choices=("ntsc", "pal"))
    parser.add_argument("--emulator", type=Path, default=smoke.DEFAULT_EMULATOR)
    parser.add_argument("--config", type=Path, default=smoke.DEFAULT_CONFIG)
    parser.add_argument("--timeout", type=float, default=45.0)
    args = parser.parse_args()
    if not 0 < args.timeout <= 3600:
        parser.error("timeout must be positive and at most 3600 seconds")
    try:
        if smoke.emulator_is_running(args.emulator):
            raise ValueError("an emulator is already running; close it before starting an isolated console run")
        smoke.check_config(args.config)
        version = smoke.emulator_version(args.emulator)
        for standard in (args.standard,) if args.standard else ("ntsc", "pal"):
            if not run(args, standard, version):
                return 1
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        parser.exit(2, f"error: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
