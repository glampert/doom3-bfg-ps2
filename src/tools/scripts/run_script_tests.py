#!/usr/bin/env python3
# ================================================================================================
# File: run_script_tests.py
# Brief: Archive isolated EE compiler probes and require identity, cleanup and expected diagnostics.
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

CASES = {
    "valid": None,
    "include": None,
    "include-missing": "file 'absent.script' not found",
    "syntax": 'Unknown value "unknown"',
    "eof": "Unexpected end of file",
    "lexical": "newline inside string",
    "vector": "expected float value",
    "divide": "Divide by zero",
    "remainder": "Remainder by zero",
    "type": "Type mismatch on redeclaration",
    "event": "Unknown event",
    "depth": "compiler nesting exceeds",
    "globals": "Exceeded global memory size",
    "statements": "Exceeded maximum allowed number of statements",
    "functions": "Exceeded maximum allowed number of functions",
}


def classify(test_id, case, log, returncode, timed_out):
    if timed_out or returncode not in (None, 0):
        return False
    if re.search(r"TLB Miss|Bus error|Assertion failed", log, re.I):
        return False
    if f"[D3BFG] SCRIPT BEGIN {test_id} {case}" not in log:
        return False
    expected = CASES[case]
    if expected is None:
        return (f"[D3BFG] SCRIPT RESULT {test_id} PASS" in log and
                "[D3BFG] FATAL" not in log and " FAIL" not in log)
    cleanup = f"[D3BFG] SCRIPT CLEANUP {test_id} PASS"
    fatal = re.search(r"\[D3BFG\] FATAL script ([^\n]+):(\d+): ([^\n]+)", log)
    return (cleanup in log and fatal is not None and expected in fatal[3] and
            (fatal[1] == (case + ".fixture" if case in ("globals", "statements", "functions") else "fixture.script")) and
            int(fatal[2]) >= 1 and log.index(cleanup) < fatal.start() and
            f"SCRIPT RESULT {test_id}" not in log and " FAIL" not in log)


def run_case(elf, case, version):
    test_id = "script_" + uuid.uuid4().hex[:16]
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ_") + test_id
    output = smoke.REPO_ROOT / "build/test-results" / run_id
    output.mkdir(parents=True)
    shutil.copy2(elf, output / "d3bfg.elf")
    shutil.copy2(elf.with_name("d3bfg_unstripped.elf"), output / "d3bfg_unstripped.elf")
    artifacts = smoke.archive_build_artifacts(elf, output / "d3bfg.elf", output / "d3bfg_unstripped.elf", output)
    shutil.copy2(smoke.DEFAULT_CONFIG, output / "PCSX2.ini.snapshot")
    (output / "script-case.txt").write_text(f"{test_id}\n{case}\n")
    (output / "included.script").write_text("float answer = 42;\n")
    command = [str(smoke.DEFAULT_EMULATOR), "-batch", "-elf", str(output / "d3bfg.elf"),
               "-logfile", str(output / "emulog.txt")]
    metadata = {"test_id": test_id, "case": case, "command": command, "emulator_version": version,
                "artifacts": artifacts, "fixture_sha256": smoke.sha256(output / "script-case.txt"),
                "include_sha256": smoke.sha256(output / "included.script"),
                "elf_sha256": smoke.sha256(output / "d3bfg.elf"),
                "config_sha256": smoke.sha256(output / "PCSX2.ini.snapshot")}
    (output / "run.json").write_text(json.dumps(metadata, indent=2) + "\n")
    started = time.monotonic()
    timed_out = False
    with (output / "stdout.txt").open("wb") as stdout:
        process = subprocess.Popen(command, cwd=output, stdout=stdout, stderr=subprocess.STDOUT)
        try:
            while True:
                log = smoke.read_text(output / "emulog.txt") + "\n" + smoke.read_text(output / "stdout.txt")
                returncode = process.poll()
                if f"SCRIPT RESULT {test_id}" in log or smoke.CRASH_RE.search(log) or returncode is not None:
                    break
                if time.monotonic() - started > 30:
                    timed_out = True
                    break
                time.sleep(0.1)
        finally:
            smoke.stop_owned_process(process)
    log = smoke.read_text(output / "emulog.txt") + "\n" + smoke.read_text(output / "stdout.txt")
    passed = classify(test_id, case, log, returncode, timed_out)
    (output / "summary.json").write_text(json.dumps({"passed": passed, "timed_out": timed_out,
        "returncode_before_cleanup": returncode, "returncode_after_cleanup": process.returncode}, indent=2) + "\n")
    print(f"{'PASS' if passed else 'FAIL'} {case}: {output}", flush=True)
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", choices=tuple(CASES), help="run one case instead of the complete matrix")
    parser.add_argument("--elf", type=Path, default=smoke.REPO_ROOT / "build/debug-script/d3bfg.elf")
    args = parser.parse_args()
    if smoke.emulator_is_running(smoke.DEFAULT_EMULATOR):
        parser.error("an emulator is already running; close it before running isolated compiler tests")
    smoke.check_config(smoke.DEFAULT_CONFIG)
    version = smoke.emulator_version(smoke.DEFAULT_EMULATOR)
    passed = True
    for case in ([args.case] if args.case else CASES):
        passed = run_case(args.elf.resolve(), case, version) and passed
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
