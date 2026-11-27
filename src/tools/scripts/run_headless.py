#!/usr/bin/env python3
# ================================================================================================
# File: run_headless.py
# Brief: Verify the production console loop, live commands, retail startup and ordered shutdown in isolated PCSX2 runs.
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
from run_console import BASELINE
from stage_headless import REPO_ROOT, boot_data, sha256, stage

# Production boot/shutdown markers printed by src/ps2/system/main.cpp; every runner matches
# these, so rewording a line there must update it here too.
BOOT_BEGIN = "======== PS2 Game boot begin ========"
LOOP_READY = "Main loop ready;"
SHUTDOWN_COMPLETE = "======== Shutdown complete ========"
STATUS_RE = re.compile(r"\[D3BFG\] COMMON frames=(\d+) game_frames=0 state=console console=1 game_nomap=1 render_device=0 sound_output=0 heap=(\d+)/(\d+)/(\d+)")
MEMORY_RE = re.compile(r"Memory baseline=(\d+)/(\d+)/(\d+) final=(\d+)/(\d+)/(\d+) peak=(\d+)/(\d+) arena=(\d+)")
STARTUP_STAGES = ("startup-foundation", "startup-decls", "startup-game")
STARTUP_RE = re.compile(r"\[D3BFG\] MEMORY (startup-\w+) requested=(\d+) backing=(\d+) allocations=(\d+)")
STARTUP_TAG_RE = re.compile(r"\[D3BFG\] TAG startup-game (\w+) requested=(\d+) backing=(\d+) allocations=(\d+)")
# The cooked program's load line (idProgram::Startup on the PS2).
SCRIPT_LOAD_RE = re.compile(r"Loaded 'script/doom_main\.d3sp': (\d+) statements, (\d+) functions, (\d+) defs, "
                            r"(\d+) types, (\d+) global bytes")
SCRIPT_FIELDS = ("statements", "functions", "defs", "types", "global_bytes")
# The retail case mounts the containers and loads the complete retail program, cooked on the host.
RETAIL_MARKERS = ("Loaded resource file _common.resources", "Loaded resource file _ordered.resources",
                  "Loaded 'script/doom_main.d3sp'")
# The check variant (make SCRIPT_CHECK=1) compiles the script at boot and logs the image the cook's
# writer makes of it, which must be the host cook's file byte for byte.
CHECK_MARKERS = ("Loaded resource file _common.resources", "Loaded resource file _ordered.resources",
                 "Compiled 'script/doom_main.script'")
SCRIPT_IMAGE_RE = re.compile(r"\[D3BFG\] SCRIPT_IMAGE bytes=(\d+) fnv=([0-9a-f]{8})")
# Debug builds read every declaration's text back from the containers (ps2_verifyDeclText); a read
# that does not match its scan's checksum is fatal on the target.
DECL_TEXT_RE = re.compile(r"\[D3BFG\] DECLTEXT verified=(\d+) bytes=(\d+) largest=(\d+) ms=(\d+) scattered_ms=(\d+)")
DECL_TEXT_FIELDS = ("verified", "bytes", "largest", "ms", "scattered_ms")
# Debug retail runs measure mars_city1's game data twice (ps2_mapdata, M3.1): the map's container, its
# cooked entities, the collision models and the AAS, each step's ledger and the freed totals.
MAP_DATA_MAP = "game/mars_city1"
MAP_DATA_RE = re.compile(r"\[D3BFG\] MAPDATA map=(\S+) entities=(\d+) geometry_crc=(0x[0-9a-f]{8}) "
                         r"collision_crc=(0x[0-9a-f]{8}) collision=(\d) aas_types=(\d+) aas_loaded=(\d+)")
MAP_DATA_MEMORY_RE = re.compile(r"\[D3BFG\] MEMORY mapdata-(\w+) requested=(\d+) backing=(\d+) allocations=(\d+) "
                                r"peak_requested=(\d+) peak_backing=(\d+)")
MAP_DATA_STEP_RE = re.compile(r"\[D3BFG\] MAPDATA_STEP (\w+) ms=(\d+)")
MAP_DATA_STEPS = ("begin", "container", "entities", "collision", "aas", "end")
DEFAULT_TIMEOUTS = {"bounded": 30.0, "console": 30.0, "retail": 300.0}


def map_data(log: str) -> list[dict]:
    """Each ps2_mapdata cycle: its summary line, and per step the ledger and the EE milliseconds."""
    cycles = []
    memory = MAP_DATA_MEMORY_RE.findall(log)
    steps = MAP_DATA_STEP_RE.findall(log)
    per_cycle = len(MAP_DATA_STEPS)
    for index, match in enumerate(MAP_DATA_RE.findall(log)):
        name, entities, geometry, collision_crc, collision, types, loaded = match
        ledgers = memory[index * per_cycle:(index + 1) * per_cycle]
        times = dict(steps[index * (per_cycle - 1):(index + 1) * (per_cycle - 1)])
        cycles.append({"map": name, "entities": int(entities), "geometry_crc": geometry, "collision_crc": collision_crc,
                       "collision": collision == "1", "aas_types": int(types), "aas_loaded": int(loaded),
                       "steps": [{"step": step, "requested": int(requested), "backing": int(backing), "allocations": int(count),
                                  "peak_requested": int(peak), "peak_backing": int(peak_backing),
                                  "ms": int(times[step]) if step in times else None}
                                 for step, requested, backing, count, peak, peak_backing in ledgers]})
    return cycles


def check_map_data(log: str) -> tuple[bool, str]:
    """Both cycles load collision and the 48-unit AAS against the cooked CRC, and the warm cycle
    frees back to exactly the ledger it began with."""
    cycles = map_data(log)
    if len(cycles) != 2 or any([step["step"] for step in cycle["steps"]] != list(MAP_DATA_STEPS) for cycle in cycles):
        return False, "ps2_mapdata did not run two complete cycles"
    for cycle in cycles:
        if not cycle["collision"] or cycle["aas_loaded"] < 1 or cycle["geometry_crc"] != cycle["collision_crc"]:
            return False, "the cooked map's CRC did not load the retail collision and AAS"
    warm = cycles[1]["steps"]
    key = lambda step: (step["requested"], step["backing"], step["allocations"])
    if key(warm[0]) != key(warm[-1]) or key(cycles[0]["steps"][-1]) != key(warm[0]):
        return False, "the warm ps2_mapdata cycle did not free back to its starting ledger"
    return True, "map data loaded and freed"


def startup_memory(log: str) -> dict:
    """Startup ledgers by stage, the largest tags after game startup and the compiled script size."""
    stages = {stage: {"requested": int(requested), "backing": int(backing), "allocations": int(count)}
              for stage, requested, backing, count in STARTUP_RE.findall(log)}
    tags = sorted(({"tag": tag, "requested": int(requested), "backing": int(backing), "allocations": int(count)}
                   for tag, requested, backing, count in STARTUP_TAG_RE.findall(log)), key=lambda tag: -tag["backing"])
    match = SCRIPT_LOAD_RE.search(log)
    script = dict(zip(SCRIPT_FIELDS, map(int, match.groups()))) if match else {}
    return {"stages": stages, "tags": tags, "script": script}


def read_log(output: Path) -> str:
    console = smoke.read_text(output / "emulog.txt")
    stdout = smoke.read_text(output / "stdout.txt")
    # PCSX2 mirrors IOP lines to both streams. Count native markers once, but reject crashes in either.
    if smoke.CRASH_RE.search(stdout) or "[D3BFG] FATAL" in stdout:
        return console + "\n" + stdout
    return console if BOOT_BEGIN in console else stdout


def fnv1a(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def check_script_image(log: str, cooked: Path) -> tuple[bool, str]:
    """The EE compile's image (size and FNV-1a) against the host cook's file."""
    images = SCRIPT_IMAGE_RE.findall(log)
    if len(images) != 1:
        return False, "the check variant logged no single script image"
    data = cooked.read_bytes()
    if int(images[0][0]) != len(data) or int(images[0][1], 16) != fnv1a(data):
        return False, (f"the EE compile's image ({images[0][0]} bytes, fnv {images[0][1]}) differs from {cooked} "
                       f"({len(data)} bytes, fnv {fnv1a(data):08x})")
    return True, f"the EE compile's image equals {cooked.name} ({len(data)} bytes, fnv {images[0][1]})"


def decl_text(log: str) -> dict | None:
    match = DECL_TEXT_RE.search(log)
    return dict(zip(DECL_TEXT_FIELDS, map(int, match.groups()))) if match else None


def classify(log: str, case: str, identity: str, compiled: bool = False, verify_decls: bool = False) -> tuple[bool, str]:
    if smoke.CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
        return False, "crash or fatal diagnostic"
    if "[D3BFG] GS ready" in log:
        return False, "headless run initialized a GS device"
    required = ("game initialized.", "COMMON console ready;", LOOP_READY,
                "------------ Game Shutdown", "COMMON stage=system shutdown", SHUTDOWN_COMPLETE)
    if any(marker not in log for marker in required):
        return False, "incomplete native boot/shutdown"
    if f"{identity}_begin" not in log:
        return False, "stale or missing run identity"
    bounded = case in ("bounded", "retail")
    frames = re.findall(r"COMMON shutdown complete frames=(\d+)", log)
    if len(frames) != 1 or (int(frames[0]) != 120 if bounded else int(frames[0]) < 120):
        return False, "incorrect completed frame count"
    statuses = STATUS_RE.findall(log)
    if len(statuses) < 2 or int(statuses[-1][0]) != int(frames[0]):
        return False, "missing native console/NOMAP frame state"
    memory = MEMORY_RE.findall(log)
    if len(memory) != 1 or tuple(map(int, memory[0][:3])) != BASELINE or memory[0][:3] != memory[0][3:6]:
        return False, "startup allocations were not recovered"
    if case == "retail":
        if any(marker not in log for marker in (CHECK_MARKERS if compiled else RETAIL_MARKERS)):
            return False, "retail containers or script were not loaded"
        if [stage for stage, *_ in STARTUP_RE.findall(log)] != list(STARTUP_STAGES):
            return False, "missing or repeated startup memory checkpoints"
        if verify_decls and (decl_text(log) is None or decl_text(log)["verified"] == 0):
            return False, "the declarations' text was not read back from the containers"
        if verify_decls:
            passed, reason = check_map_data(log)
            if not passed:
                return False, reason
    if bounded:
        if "COMMON frame limit reached frames=120" not in log:
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
    retail_data = args.data_root if case == "retail" else None
    build = f"{args.build}-scriptcheck" if args.script_check is not None else args.build
    verify_decls = case == "retail" and args.build == "debug" and args.script_check is None
    output = stage(build, REPO_ROOT / "build/test-results" / identity, retail_data=retail_data,
                   maps=(MAP_DATA_MAP,) if verify_decls else ())
    timeout = args.timeout if args.timeout is not None else DEFAULT_TIMEOUTS[case]
    report = json.loads((output / "report.json").read_text())
    for name in report["flag_stamps"]:
        shutil.copy2(REPO_ROOT / name, output / Path(name).name)
    shutil.copy2(args.config, output / "PCSX2.ini.snapshot")
    config = f'echo "{identity}_begin"\n'
    config += f"ps2_verifyDeclText\nps2_mapdata {MAP_DATA_MAP}\nps2_mapdata {MAP_DATA_MAP}\n" if verify_decls else ""
    config += "ps2_maxFrames 120\nps2_status\nwait 60\nps2_status\n" if case != "console" else f'wait 120\necho "{identity}_ready"\nps2_status\n'
    (output / "base/d3bfg.cfg").write_text(config)
    (output / "boot-data.json").write_text(json.dumps(boot_data(output), indent=2) + "\n")
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
            while time.monotonic() - started < timeout:
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
                if SHUTDOWN_COMPLETE in log:
                    completed = True
                    break
                if process.poll() is not None:
                    failure = "emulator exited before completion"
                    break
                time.sleep(0.05)
        finally:
            smoke.stop_owned_process(process)
    log = read_log(output)
    passed, reason = classify(log, case, identity, args.script_check is not None, verify_decls) if completed else (False, failure)
    if passed and args.script_check is not None:
        passed, reason = check_script_image(log, args.script_check)
    if process.returncode != 0:
        passed, reason = False, f"emulator exited with status {process.returncode}"
    # Match source/ELF evidence and retain the exact runtime settings and input bytes.
    data = {"passed": passed, "reason": reason, "configuration": args.build, "case": case,
            "command": command, "emulator_version": version, "elapsed_seconds": time.monotonic() - started,
            "emulator_returncode": process.returncode,
            "boot_data": boot_data(output),
            "console_sha256": sha256(output / "console.txt"),
            "startup_memory": startup_memory(log)}
    if verify_decls:
        data["decl_text"] = decl_text(log)
        data["map_data"] = map_data(log)
    if retail_data is not None:
        data["retail_data"] = str(retail_data.resolve())
    if args.script_check is not None:
        data["script_check"] = {"image": str(args.script_check.resolve()), "sha256": sha256(args.script_check)}
    (output / "summary.json").write_text(json.dumps(data, indent=2) + "\n")
    game = data["startup_memory"]["stages"].get("startup-game")
    detail = f" (startup heap {game['requested']}/{game['backing']} bytes in {game['allocations']} allocations)" if game else ""
    print(f"{'PASS' if passed else 'FAIL'} {case}: {reason}{detail}\nArtifacts: {output}", flush=True)
    if not passed:
        print(log[-6000:], flush=True)
    return passed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", choices=("debug", "release"), default="debug")
    parser.add_argument("--case", choices=("bounded", "console", "retail"),
                        help="the default runs bounded and console; retail needs a local installation")
    parser.add_argument("--data-root", type=Path, default=REPO_ROOT / "gamedata/d3_bfg",
                        help="retail installation whose containers the retail case mounts")
    parser.add_argument("--emulator", type=Path, default=smoke.DEFAULT_EMULATOR)
    parser.add_argument("--config", type=Path, default=smoke.DEFAULT_CONFIG)
    parser.add_argument("--timeout", type=float, help="seconds per run (default 30, or 300 for retail)")
    parser.add_argument("--script-check", type=Path,
                        help="run the SCRIPT_CHECK=1 variant and hold its compile to this host-cooked image (retail only)")
    args = parser.parse_args()
    if args.script_check is not None and args.case != "retail":
        parser.error("--script-check needs --case retail")
    if args.timeout is not None and not 0 < args.timeout <= 3600:
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
