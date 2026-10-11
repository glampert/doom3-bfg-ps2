#!/usr/bin/env python3
# ================================================================================================
# File: run_headless.py
# Brief: Verify the production console loop, live commands and ordered shutdown in isolated PCSX2 runs.
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

import run_pcsx2_test as smoke
from stage_headless import REPO_ROOT, sha256, stage

STATUS_RE = re.compile(r"\[D3BFG\] HEADLESS frames=(\d+) game_frames=0 state=console console=1 game_nomap=1 render_device=0 sound_output=0 heap=(\d+)/(\d+)/(\d+)")
MEMORY_RE = re.compile(r"HEADLESS memory baseline=(\d+)/(\d+)/(\d+) final=(\d+)/(\d+)/(\d+) peak=(\d+)/(\d+) arena=(\d+)")


def read_log(output: Path) -> str:
    console = smoke.read_text(output / "emulog.txt")
    stdout = smoke.read_text(output / "stdout.txt")
    # PCSX2 mirrors IOP lines to both streams. Count native markers once, but reject crashes in either.
    if smoke.CRASH_RE.search(stdout) or "[D3BFG] FATAL" in stdout:
        return console + "\n" + stdout
    return console if "[D3BFG] BOOT production BEGIN" in console else stdout


def classify(log: str, case: str, identity: str) -> tuple[bool, str]:
    if smoke.CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
        return False, "crash or fatal diagnostic"
    required = ("game initialized.", "HEADLESS console ready;", "LOOP ready;",
                "------------ Game Shutdown", "COMMON stage=system shutdown", "BOOT production shutdown complete")
    if any(marker not in log for marker in required):
        return False, "incomplete native boot/shutdown"
    if f"{identity}_begin" not in log:
        return False, "stale or missing run identity"
    frames = re.findall(r"HEADLESS shutdown complete frames=(\d+)", log)
    if len(frames) != 1 or (int(frames[0]) != 120 if case == "bounded" else int(frames[0]) < 120):
        return False, "incorrect completed frame count"
    statuses = STATUS_RE.findall(log)
    if len(statuses) < 2 or int(statuses[-1][0]) != int(frames[0]):
        return False, "missing native console/NOMAP frame state"
    memory = MEMORY_RE.findall(log)
    if len(memory) != 1 or memory[0][:3] != memory[0][3:6]:
        return False, "startup allocations were not recovered"
    if case == "bounded":
        if "HEADLESS frame limit reached frames=120" not in log:
            return False, "frame limit did not request shutdown"
    elif f"]echo \"{identity}_command\"" not in log or "]quit" not in log or "]history" not in log:
        return False, "live console commands did not execute"
    elif log.count("discarding invalid or oversized headless console line") != 2 or "]quit x" in log:
        return False, "invalid console input was not rejected"
    return True, "native headless console completed"


def run(args: argparse.Namespace, case: str, version: str) -> bool:
    if smoke.emulator_is_running(args.emulator):
        raise ValueError("an emulator is already running; each headless run requires an isolated session")
    smoke.check_config(args.config)
    identity = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_headless_" + uuid.uuid4().hex[:12]
    output = stage(args.build, REPO_ROOT / "build/test-results" / identity)
    report = json.loads((output / "report.json").read_text())
    for name in report["flag_stamps"]:
        shutil.copy2(REPO_ROOT / name, output / Path(name).name)
    shutil.copy2(args.config, output / "PCSX2.ini.snapshot")
    config = f'echo "{identity}_begin"\n'
    config += "ps2_maxFrames 120\nps2_status\nwait 60\nps2_status\n" if case == "bounded" else f'wait 120\necho "{identity}_ready"\nps2_status\n'
    (output / "base/d3bfg.cfg").write_text(config)
    (output / "boot-data.json").write_text(json.dumps({str(path.relative_to(output)): sha256(path) for path in sorted((output / "base").rglob("*")) if path.is_file()}, indent=2) + "\n")
    (output / "console.txt").write_bytes(b"")
    command = [str(args.emulator), "-batch", "-elf", str(output / "d3bfg.elf"), "-logfile", str(output / "emulog.txt")]
    started = time.monotonic()
    partial_at = None
    submitted = False
    completed = False
    failure = "watchdog timeout"
    with (output / "stdout.txt").open("wb") as stdout:
        process = subprocess.Popen(command, cwd=output, stdout=stdout, stderr=subprocess.STDOUT)
        try:
            while time.monotonic() - started < args.timeout:
                log = read_log(output)
                if smoke.CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
                    failure = "crash or fatal diagnostic"
                    break
                if case == "console" and f"{identity}_ready" in log:
                    if partial_at is None:
                        with (output / "console.txt").open("ab") as stream:
                            stream.write(f'echo "{identity}_command"'.encode())
                        partial_at = time.monotonic()
                    elif not submitted and time.monotonic() - partial_at > 0.35:
                        if f"]echo \"{identity}_command\"" in log:
                            failure = "unfinished console line executed"
                            break
                        with (output / "console.txt").open("ab") as stream:
                            stream.write(b"\nquit " + b"x" * 1100 + b"\nquit\x00\nps2_status\nhistory\nquit\n")
                        submitted = True
                if "BOOT production shutdown complete" in log:
                    completed = True
                    break
                if process.poll() is not None:
                    failure = "emulator exited before completion"
                    break
                time.sleep(0.05)
        finally:
            smoke.stop_owned_process(process)
    log = read_log(output)
    passed, reason = classify(log, case, identity) if completed else (False, failure)
    if process.returncode != 0:
        passed, reason = False, f"emulator exited with status {process.returncode}"
    # Match source/ELF evidence and retain the exact runtime settings and input bytes.
    data = {"passed": passed, "reason": reason, "configuration": args.build, "case": case,
            "command": command, "emulator_version": version, "elapsed_seconds": time.monotonic() - started,
            "emulator_returncode": process.returncode,
            "boot_data": {str(path.relative_to(output)): sha256(path) for path in sorted((output / "base").rglob("*")) if path.is_file()},
            "console_sha256": sha256(output / "console.txt")}
    (output / "summary.json").write_text(json.dumps(data, indent=2) + "\n")
    print(f"{'PASS' if passed else 'FAIL'} {case}: {reason}\nArtifacts: {output}", flush=True)
    if not passed:
        print(log[-6000:], flush=True)
    return passed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", choices=("debug", "release"), default="debug")
    parser.add_argument("--case", choices=("bounded", "console"))
    parser.add_argument("--emulator", type=Path, default=smoke.DEFAULT_EMULATOR)
    parser.add_argument("--config", type=Path, default=smoke.DEFAULT_CONFIG)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()
    if not 0 < args.timeout <= 3600:
        parser.error("timeout must be positive and at most 3600 seconds")
    try:
        if smoke.emulator_is_running(args.emulator):
            raise ValueError("an emulator is already running; close it before starting an isolated headless run")
        smoke.check_config(args.config)
        version = smoke.emulator_version(args.emulator)
        for case in (args.case,) if args.case else ("bounded", "console"):
            if not run(args, case, version):
                return 1
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        parser.exit(2, f"error: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
