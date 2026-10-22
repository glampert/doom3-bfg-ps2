#!/usr/bin/env python3
# ================================================================================================
# File: run_ui.py
# Brief: Exercise native authored SWF animation, input, captures and reload cleanup in production.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time
import uuid

import run_console
import run_headless
import run_pcsx2_test as smoke
from stage_headless import REPO_ROOT, boot_data, sha256, stage
import ui_capture


LOADED_RE = re.compile(r"\[D3BFG\] SWF state=loaded cycle=(\d+) frames=(\d+) pulse=(-?\d+) focus=(-?\d+) selected=(-?\d+) selects=(\d+) heap=(\d+)/(\d+)/(\d+)")
UNLOADED_RE = re.compile(r"\[D3BFG\] SWF state=unloaded cycle=(\d+) heap=(\d+)/(\d+)/(\d+)")
CAPTURE_RE = re.compile(r"\[D3BFG\] CONSOLE capture=(ui-(?:c[123]-[ab]|quit)\.tga) frame=(\d+) presents=(\d+) redraws=(\d+)")
PAGES_RE = re.compile(r"\[D3BFG\] UI pages images=(\d+) slots=(\d+) cache=(\d+) scratch=(\d+) metadata=(\d+) "
                      r"loads=(\d+) hits=(\d+) evictions=(\d+) triangles=(\d+)")
TARGET_CRASH_RE = re.compile(r"TLB Miss|Assertion failed|Bus error", re.IGNORECASE)
NEGATIVES = tuple(ui_capture.NEGATIVE_DIAGNOSTICS)
BASELINE = (7424, 7600, 4)
ANIMATION_WAITS = (16, 1, 2, 3, 5, 7, 11, 13, 17, 19, 23, 29)


def animation_probe_commands(identity: str, cycle: int, probe: int) -> tuple[str, ...]:
    if not 1 <= probe <= 12:
        raise ValueError("native UI animation probe must be within the twelve-probe bound")
    # Native timelines use elapsed time. A fixed frame delay can repeatedly land on the same
    # four-frame pulse phase when rendering slows down; vary bounded delays without accepting
    # a frozen animation or changing the watchdog deadline.
    frames = ANIMATION_WAITS[probe - 1]
    return ("ps2_ui resume", f"wait {frames}", "ps2_ui pause", "ps2_ui status",
            f'echo "{identity}_c{cycle}_probe{probe}"')


def echoed(log: str, marker: str) -> bool:
    # HostFs logs submitted lines before wait delays execute. Only actual Echo output is a barrier.
    return re.search(re.escape(marker) + r" +\r?\n", log) is not None


def capture_states(log: str) -> dict[str, tuple[int, ...]]:
    states = {}
    for capture in CAPTURE_RE.finditer(log):
        if capture[1] == "ui-quit.tga":
            continue
        statuses = list(LOADED_RE.finditer(log, 0, capture.start()))
        if not statuses:
            raise ValueError("native SWF capture has no preceding state")
        states[capture[1]] = tuple(map(int, statuses[-1].groups()))
    return states


def capture_page_states(log: str) -> dict[str, tuple[int, ...]]:
    states = {}
    for capture in CAPTURE_RE.finditer(log):
        if capture[1] == "ui-quit.tga":
            continue
        statuses = list(PAGES_RE.finditer(log, 0, capture.start()))
        if not statuses:
            raise ValueError("native SWF capture has no preceding shared page state")
        states[capture[1]] = tuple(map(int, statuses[-1].groups()))
    return states


def classify(log: str, identity: str, standard: str) -> tuple[bool, str]:
    if smoke.CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
        return False, "crash or fatal diagnostic"
    required = ("game initialized.", "LOOP ready;", f"{identity}_begin", f"{identity}_ready", f"{identity}_done",
                f"[D3BFG] GS ready {standard.upper()}", "[D3BFG] UI adapters ready", "------------ Game Shutdown",
                "COMMON stage=system shutdown", "BOOT production shutdown complete")
    if any(marker not in log for marker in required):
        return False, "incomplete native UI boot/shutdown or stale identity"
    if not all(echoed(log, f"{identity}_{suffix}") for suffix in ("begin", "ready", "done")):
        return False, "native UI command barriers did not execute"
    shutdowns = run_console.SHUTDOWN_RE.findall(log)
    if len(shutdowns) != 1:
        return False, "missing or duplicate native presentation shutdown"
    frames, presents, redraws = map(int, shutdowns[0])
    if frames < 360 or frames != presents or redraws < 3:
        return False, "native UI frames and presentations are inconsistent"
    if any(int(frame) != int(present) or int(frame) > frames or int(redraw) > redraws
           for frame, present, redraw, _ in run_console.STATUS_RE.findall(log)):
        return False, "console redraw advanced simulation or presented an extra frame"
    captures = CAPTURE_RE.findall(log)
    expected = [f"ui-c{cycle}-{phase}.tga" for cycle in (1, 2, 3) for phase in ("a", "b")] + ["ui-quit.tga"]
    if [capture[0] for capture in captures] != expected:
        return False, "missing, duplicate or reordered native UI captures"
    previous = 0
    for _, frame, present, redraw in captures:
        frame, present, redraw = int(frame), int(present), int(redraw)
        if not previous < frame <= frames or frame != present or redraw > redraws:
            return False, "UI capture did not follow a distinct completed native presentation"
        previous = frame
    if previous != frames:
        return False, "pending quit capture was not drained by the final native frame"
    try:
        states = capture_states(log)
        page_states = capture_page_states(log)
    except ValueError as error:
        return False, str(error)
    previous_page_state = None
    for page_state in page_states.values():
        images, slots, cache, scratch, metadata, loads, hits, evictions, triangles = page_state
        if (images, slots, cache, scratch, metadata) != (2, 4, 1048576, 262144, 56):
            return False, "native SWF and English font did not share the fixed page budget"
        if loads != 4 or hits == 0 or triangles == 0 or evictions != 0:
            return False, "native SWF and English font did not draw through the shared page cache"
        if previous_page_state is not None:
            previous_loads, previous_hits, previous_evictions, previous_triangles = previous_page_state
            if loads < previous_loads or hits <= previous_hits or evictions < previous_evictions or triangles <= previous_triangles:
                return False, "native shared page counters reset or stopped across captures and reloads"
        previous_page_state = (loads, hits, evictions, triangles)
    for cycle in (1, 2, 3):
        first, second = (states[f"ui-c{cycle}-{phase}.tga"] for phase in ("a", "b"))
        if any(state[0] != cycle or state[1] < 120 or state[2] not in (1, 2, 3, 4) for state in (first, second)):
            return False, "native SWF timeline did not complete 120 frames per load"
        if first[2] == second[2] or second[1] <= first[1]:
            return False, "native SWF animation did not advance between paused captures"
        if second[2] not in (2, 3):
            return False, "native SWF capture did not exercise fractional animated alpha"
        if first[3:6] != (0, -1, 0) or second[3:6] != (1, 1, 1):
            return False, "native SWF focus/select script state did not follow key events"
        first_capture = next(match for match in CAPTURE_RE.finditer(log) if match[1] == f"ui-c{cycle}-a.tga")
        second_capture = next(match for match in CAPTURE_RE.finditer(log) if match[1] == f"ui-c{cycle}-b.tga")
        overlay = run_console.STATUS_RE.findall(log[first_capture.end():second_capture.start()])
        if [status[3] for status in overlay] != ["1", "0"]:
            return False, "native console did not open over the SWF and return focus before capture"
        if int(overlay[1][0]) <= int(overlay[0][0]):
            return False, "console overlay was not presented over the active SWF"
    unloaded = [tuple(map(int, state)) for state in UNLOADED_RE.findall(log)]
    if len(unloaded) != 3 or [state[0] for state in unloaded] != [1, 2, 3]:
        return False, "native SWF did not unload each of three load cycles"
    if any(state[1:] != unloaded[0][1:] for state in unloaded[1:]):
        return False, "warm SWF load/unload allocations were not recovered"
    memory = run_headless.MEMORY_RE.findall(log)
    if len(memory) != 1 or tuple(map(int, memory[0][:3])) != BASELINE or memory[0][:3] != memory[0][3:6]:
        return False, "production startup allocations were not fully recovered"
    if "]quit" not in log:
        return False, "native quit command did not execute"
    if log.count("]echo ui_console\n") != 3 or len(re.findall(r"(?<!echo )ui_console +\r?\n", log)) != 3:
        return False, "native console editing and UpdateScreen did not execute over each movie"
    return True, "native authored SWF completed three animation/input/reload cycles"


def classify_negative(log: str, identity: str, case: str, expected: str) -> tuple[bool, str]:
    if TARGET_CRASH_RE.search(log):
        return False, "EE crash while rejecting invalid UI data"
    if not echoed(log, f"{identity}_begin") or "]ps2_ui load" not in log:
        return False, "missing native UI load attempt or stale identity"
    pixel_failure = case == "movie-page-pixels"
    if "BOOT production shutdown complete" in log or CAPTURE_RE.search(log):
        return False, "invalid UI data completed a native capture or normal shutdown"
    if not pixel_failure and "[D3BFG] SWF state=loaded" in log:
        return False, "invalid UI data was published as a loaded movie"
    if pixel_failure and ("[D3BFG] SWF state=loaded" not in log or "page read or checksum" not in log):
        return False, "corrupt page pixels did not reach the checked native streaming path"
    fatals = re.findall(r"\[D3BFG\] FATAL ([^\r\n]+)", log)
    if not fatals or any(expected not in fatal for fatal in fatals):
        return False, "missing or unexpected UI validation failure"
    return True, f"native UI rejected {case} before completing its first presentation" if pixel_failure else f"native UI rejected {case} before publishing ownership"


def mutate(output: Path, case: str) -> dict:
    # These are authored package inputs only. Never modify source assets or the user's retail base.
    if case.startswith("movie-page-"):
        relative = ui_capture.MOVIE_PAGE_PATH
    elif case.startswith("font-page-"):
        relative = ui_capture.FONT_PAGE_PATH
    else:
        relative = ui_capture.MOVIE_PATH if case.startswith("bswf-") else ui_capture.FONT_PATH
    path = output / "base" / relative
    original = path.read_bytes()
    before = sha256(path)
    if case.endswith("-missing"):
        path.unlink()
    elif case.endswith("-truncated"):
        path.write_bytes(original[:-1])
    elif case.endswith("-oversized"):
        path.write_bytes(original + bytes(65537))
    elif case.endswith("-mutated"):
        changed = bytearray(original)
        changed[-1] ^= 1
        path.write_bytes(changed)
    elif case.endswith("-header"):
        changed = bytearray(original)
        changed[0] ^= 1
        path.write_bytes(changed)
    elif case.endswith("-table"):
        changed = bytearray(original)
        changed[32] ^= 1
        path.write_bytes(changed)
    elif case.endswith("-pixels"):
        changed = bytearray(original)
        payload = 32 + struct.unpack_from("<I", original, 24)[0] * 8
        changed[payload] ^= 1
        path.write_bytes(changed)
    else:
        raise ValueError(f"unknown UI negative case: {case}")
    return {"path": str(path.relative_to(output)), "original_sha256": before,
            "mutated_sha256": sha256(path) if path.exists() else None}


def run(args: argparse.Namespace, standard: str, version: str, case: str = "native") -> bool:
    if smoke.emulator_is_running(args.emulator):
        raise ValueError("an emulator is already running; each native UI run requires an isolated session")
    smoke.check_config(args.config)
    identity = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_ui_" + uuid.uuid4().hex[:12]
    output = stage(args.build, REPO_ROOT / "build/test-results" / identity, standard, include_ui=True, catalog=args.source)
    report = json.loads((output / "report.json").read_text())
    for name in report["flag_stamps"]:
        shutil.copy2(REPO_ROOT / name, output / Path(name).name)
    shutil.copy2(args.config, output / "PCSX2.ini.snapshot")
    config = f'echo "{identity}_begin"\nps2_maxFrames 0\n'
    config += f'echo "{identity}_ready"\n'
    (output / "base/d3bfg.cfg").write_text(config)
    mutation = mutate(output, case) if case != "native" else None
    (output / "boot-data.json").write_text(json.dumps(boot_data(output), indent=2) + "\n")
    (output / "console.txt").write_bytes(b"")
    command = [str(args.emulator), "-batch", "-elf", str(output / "d3bfg.elf"), "-logfile", str(output / "emulog.txt")]
    started = time.monotonic()
    phase, cycle, probe = "startup", 1, 0
    first_pulse = None
    completed = False
    failure = "watchdog timeout"
    with (output / "stdout.txt").open("wb") as stdout:
        process = subprocess.Popen(command, cwd=output, stdout=stdout, stderr=subprocess.STDOUT)
        try:
            while time.monotonic() - started < args.timeout:
                log = run_headless.read_log(output)
                if TARGET_CRASH_RE.search(log):
                    failure = "EE crash during native UI run"
                    break
                if "[D3BFG] FATAL" in log:
                    completed = case != "native"
                    failure = "unexpected fatal diagnostic"
                    break
                append = lambda *commands: run_console.append_commands(output, *commands)
                if case != "native" and phase == "startup" and echoed(log, f"{identity}_ready"):
                    append("ps2_ui load")
                    phase = "negative-load"
                if case == "native":
                    if phase == "startup" and echoed(log, f"{identity}_ready"):
                        append("ps2_ui load", "wait 120", "ps2_ui pause", "ps2_ui status", f'ps2_capture "ui-c{cycle}-a.tga"')
                        phase = "first-capture"
                    elif phase == "first-capture" and f"CONSOLE capture=ui-c{cycle}-a.tga " in log:
                        first_pulse = capture_states(log)[f"ui-c{cycle}-a.tga"][2]
                        probe = 1
                        append("ps2_consoleKey toggle", "ps2_status", 'ps2_consoleText "echo ui_console"', "ps2_consoleKey enter",
                               "wait 6", "ps2_consoleKey toggle", "ps2_status", "ps2_uiKey tab", "ps2_uiKey enter",
                               *animation_probe_commands(identity, cycle, probe))
                        phase = "animation-probe"
                    elif phase == "animation-probe" and echoed(log, f"{identity}_c{cycle}_probe{probe}"):
                        states = LOADED_RE.findall(log)
                        if not states:
                            failure = "paused movie did not publish native script state"
                            break
                        pulse = int(states[-1][2])
                        if pulse != first_pulse and pulse in (2, 3):
                            append(f'ps2_capture "ui-c{cycle}-b.tga"')
                            phase = "second-capture"
                        elif probe < 12:
                            probe += 1
                            append(*animation_probe_commands(identity, cycle, probe))
                        else:
                            failure = "native SWF timeline stayed on one pulse after twelve bounded probes"
                            break
                    elif phase == "second-capture" and f"CONSOLE capture=ui-c{cycle}-b.tga " in log:
                        # Run-identifying echo barriers are ordinary history entries. Remove their
                        # varying host harness allocations before comparing native movie ownership.
                        append("clearHistory", "ps2_ui unload", f'echo "{identity}_c{cycle}_unloaded"')
                        phase = "unloaded"
                    elif phase == "unloaded" and echoed(log, f"{identity}_c{cycle}_unloaded"):
                        if cycle < 3:
                            cycle += 1
                            append("ps2_ui load", "wait 120", "ps2_ui pause", "ps2_ui status", f'ps2_capture "ui-c{cycle}-a.tga"')
                            phase = "first-capture"
                        else:
                            append(f'echo "{identity}_done"', 'ps2_capture "ui-quit.tga"', "quit")
                            phase = "quit"
                if "BOOT production shutdown complete" in log:
                    completed = True
                    break
                if process.poll() is not None:
                    failure = "emulator exited before native UI completion"
                    break
                time.sleep(0.05)
        finally:
            smoke.stop_owned_process(process)
    log = run_headless.read_log(output)
    expected_failure = ui_capture.NEGATIVE_DIAGNOSTICS.get(case)
    if not completed:
        passed, reason = False, failure
    elif case == "native":
        passed, reason = classify(log, identity, standard)
    else:
        passed, reason = classify_negative(log, identity, case, expected_failure)
    captures = {}
    if passed and case == "native":
        try:
            for name, state in capture_states(log).items():
                captures[name] = ui_capture.verify(output / name, standard, pulse=state[2], focus=state[3], selected=state[4])
            width, height, _ = ui_capture.read_tga(output / "ui-quit.tga")
            if (width, height) != (512, 448 if standard == "ntsc" else 512):
                raise ValueError("final UI quit capture has incorrect dimensions")
            captures["ui-quit.tga"] = {"width": width, "height": height, "sha256": sha256(output / "ui-quit.tga")}
        except (OSError, ValueError) as error:
            passed, reason = False, str(error)
    if process.returncode != 0:
        passed, reason = False, f"emulator exited with status {process.returncode}"
    data = {"passed": passed, "reason": reason, "configuration": args.build, "standard": standard, "case": case,
            "command": command, "emulator_version": version, "elapsed_seconds": time.monotonic() - started,
            "emulator_returncode": process.returncode, "boot_data": boot_data(output), "mutation": mutation,
            "console_sha256": sha256(output / "console.txt"), "capture_checks": captures,
            "pixel_inspection_required": case == "native"}
    (output / "summary.json").write_text(json.dumps(data, indent=2) + "\n")
    print(f"{'PASS' if passed else 'FAIL'} ui-{standard}-{case}: {reason}\nArtifacts: {output}", flush=True)
    if not passed:
        print(log[-6000:], flush=True)
    return passed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", choices=("debug", "release"), default="debug")
    parser.add_argument("--source", type=Path, default=REPO_ROOT / "build/ui-retail",
                        help="the cooked retail UI whose catalog supplies the console charset")
    parser.add_argument("--standard", choices=("ntsc", "pal"))
    parser.add_argument("--case", choices=("native",) + NEGATIVES, help="default: both native standards and twelve invalid-data cases")
    parser.add_argument("--emulator", type=Path, default=smoke.DEFAULT_EMULATOR)
    parser.add_argument("--config", type=Path, default=smoke.DEFAULT_CONFIG)
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()
    if not 0 < args.timeout <= 3600:
        parser.error("timeout must be positive and at most 3600 seconds")
    try:
        if smoke.emulator_is_running(args.emulator):
            raise ValueError("an emulator is already running; close it before starting isolated native UI runs")
        smoke.check_config(args.config)
        version = smoke.emulator_version(args.emulator)
        if args.case is None or args.case == "native":
            for standard in (args.standard,) if args.standard else ("ntsc", "pal"):
                if not run(args, standard, version):
                    return 1
        for case in NEGATIVES if args.case is None else (() if args.case == "native" else (args.case,)):
            if not run(args, args.standard or "ntsc", version, case):
                return 1
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        parser.exit(2, f"error: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
