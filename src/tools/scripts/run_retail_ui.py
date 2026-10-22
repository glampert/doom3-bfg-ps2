#!/usr/bin/env python3
# ================================================================================================
# File: run_retail_ui.py
# Brief: Verify opt-in native English font admission, layout, resident paletted textures and cleanup in production.
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

import retail_ui_capture
import run_console
import ui_gstx
import run_headless
import run_pcsx2_test as smoke
from run_ui import BASELINE, TARGET_CRASH_RE, echoed
from stage_headless import REPO_ROOT, boot_data, sha256
from stage_retail_ui import inspect_package, stage_retail


STATE_RE = re.compile(r"\[D3BFG\] RETAIL state=loaded cycle=(\d+) font=(\d+) name=([^ ]+) frames=(\d+) heap=(\d+)/(\d+)/(\d+)")
UNLOADED_RE = re.compile(r"\[D3BFG\] RETAIL state=unloaded cycle=(\d+) heap=(\d+)/(\d+)/(\d+)")
TEXTURES_RE = re.compile(r"\[D3BFG\] RETAIL textures images=(\d+) pages=(\d+) cluts=(\d+) pixels=(\d+) metadata=(\d+) "
                         r"triangles=(\d+) uploads=(\d+) evictions=(\d+)")
METRICS_RE = re.compile(r"\[D3BFG\] RETAIL metrics font=(\d+) scale=(\d+) width=([\d.]+) height=([\d.]+)")
CAPTURE_RE = re.compile(r"\[D3BFG\] CONSOLE capture=(retail-(?:f[0-7]|return|reload|quit)\.tga) frame=(\d+) presents=(\d+) redraws=(\d+)")
NEGATIVES = ("catalog-header", "font-missing", "font-mutated", "texture-table", "texture-pixels")


def _font_names(manifest: dict) -> list[str]:
    return [name.split("/")[1] for name in sorted(manifest["fonts"])]


def _font_page(manifest: dict, index: int) -> dict:
    logical = sorted(manifest["fonts"])[index][:-4] + ".tga"
    names = {entry["selected"] for entry in manifest["image_resolutions"]
             if entry["logical_image"] == logical and entry["status"] == "resolved"}
    if len(names) != 1:
        raise ValueError("retail UI font texture binding is ambiguous")
    return manifest["cooked"]["runtime_images"][names.pop()]


def resident_textures(manifest: dict, source: Path, images) -> dict:
    """The RETAIL textures counters while exactly these runtime images are open."""
    totals = {"images": 0, "pages": 0, "cluts": 0, "pixel_bytes": 0, "metadata_bytes": 0}
    for image in sorted(set(images)):
        stats = ui_gstx.runtime_stats(ui_gstx.read_texture(source / manifest["cooked"]["runtime_images"][image]["path"]))
        totals["images"] += 1
        for key in ("pages", "cluts", "pixel_bytes", "metadata_bytes"):
            totals[key] += stats[key]
    return totals


def texture_counters(totals: dict) -> tuple:
    return tuple(totals[key] for key in ("images", "pages", "cluts", "pixel_bytes", "metadata_bytes"))


def font_images(manifest: dict) -> set:
    """Every font's converted atlas, and its pre-rendered glyph set, which loads with the font."""
    names = set()
    for font in manifest["fonts"]:
        names.update(entry["selected"] for entry in manifest["image_resolutions"]
                     if entry["logical_image"] == font[:-4] + ".tga" and entry["status"] == "resolved")
        if font in manifest["cooked"].get("glyph_sets", {}):
            names.add(manifest["cooked"]["glyph_sets"][font]["image"])
    return names


def font_textures(manifest: dict, source: Path) -> dict:
    """Counters with every font's converted texture open, as the font screen holds them."""
    return resident_textures(manifest, source, font_images(manifest))


def classify(log: str, identity: str, standard: str, manifest: dict, expected_textures: dict) -> tuple[bool, str]:
    if smoke.CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
        return False, "crash or fatal diagnostic"
    required = ("game initialized.", "LOOP ready;", f"[D3BFG] GS ready {standard.upper()}",
                "[D3BFG] UI adapters ready", "------------ Game Shutdown", "COMMON stage=system shutdown",
                "BOOT production shutdown complete")
    if any(marker not in log for marker in required) or not all(echoed(log, f"{identity}_{suffix}") for suffix in ("begin", "ready", "done")):
        return False, "incomplete native retail boot/shutdown or stale command barriers"
    if "[D3BFG] SWF state=loaded" in log:
        return False, "authored movie was loaded during the separate retail resource proof"
    fonts = _font_names(manifest)
    phases = [(f"retail-f{index}.tga", index, 1) for index in range(len(fonts))]
    phases += [("retail-return.tga", 0, 1), ("retail-reload.tga", 0, 2)]
    captures = list(CAPTURE_RE.finditer(log))
    if [match[1] for match in captures] != [name for name, _, _ in phases] + ["retail-quit.tga"]:
        return False, "missing, duplicate or reordered retail captures"
    shutdown = run_console.SHUTDOWN_RE.findall(log)
    if len(shutdown) != 1:
        return False, "missing or duplicate retail presentation shutdown"
    frames, presents, redraws = map(int, shutdown[0])
    if frames < 120 * len(phases) or frames != presents:
        return False, "retail native frame and presentation counts differ"
    previous_frame = 0
    previous_textures = None
    resident = texture_counters(expected_textures)
    glyph_sets = manifest["cooked"].get("glyph_sets", {})
    if resident[0] != len(fonts) + sum(font in glyph_sets for font in manifest["fonts"]):
        return False, "retail font textures do not match the cooked font set"
    for match, (name, font_index, cycle) in zip(captures, phases):
        frame, present, redraw = map(int, match.groups()[1:])
        if not previous_frame < frame <= frames or frame != present or redraw > redraws:
            return False, "retail capture did not follow a distinct completed presentation"
        previous_frame = frame
        before = log[:match.start()]
        states, textures = STATE_RE.findall(before), TEXTURES_RE.findall(before)
        if not states or not textures:
            return False, "retail capture lacks preceding native state"
        state = states[-1]
        if (int(state[0]), int(state[1]), state[2]) != (cycle, font_index, fonts[font_index]) or int(state[3]) < 120:
            return False, "retail font did not complete its bounded native frame interval"
        texture_state = tuple(map(int, textures[-1]))
        if texture_state[:5] != resident:
            return False, "retail fonts do not hold exactly their converted paletted textures"
        triangles, uploads, evictions = texture_state[5:]
        if triangles == 0 or uploads == 0 or evictions != 0:
            return False, "retail texture drawing, uploads or residency accounting is invalid"
        if previous_textures is not None:
            old_triangles, old_uploads, _ = previous_textures
            if triangles <= old_triangles or uploads < old_uploads:
                return False, "retail texture counters reset or stopped between font phases"
            if name == "retail-reload.tga" and uploads != old_uploads:
                return False, "warm retail reload uploaded textures again"
        previous_textures = texture_state[5:]
        metrics = METRICS_RE.findall(before)
        if len(metrics) < 3:
            return False, "native retail legacy layout metrics were not reported"
        expected = manifest["fonts"][sorted(manifest["fonts"])[font_index]]["legacy_layout_metrics"]
        for index, (row, scale) in enumerate(zip(metrics[-3:], (0.25, 0.5, 1.0))):
            if ((int(row[0]), int(row[1])) != (font_index, index) or
                    float(row[2]) != expected[index]["scaled_max_advance"] * scale or
                    float(row[3]) != expected[index]["scaled_max_height"] * scale):
                return False, "native retail font layout differs from full source legacy metrics"
    if int(captures[-1][2]) != frames or int(captures[-1][3]) != presents:
        return False, "retail quit capture was not drained by the final presentation"
    unloaded = UNLOADED_RE.findall(log)
    if [int(row[0]) for row in unloaded] != [1, 2]:
        return False, "retail reload/unload sequence did not complete"
    memory = run_headless.MEMORY_RE.findall(log)
    if len(memory) != 1 or tuple(map(int, memory[0][:3])) != BASELINE or memory[0][:3] != memory[0][3:6]:
        return False, "retail production shutdown did not recover the startup allocation ledger"
    if "]quit" not in log:
        return False, "native quit command did not execute"
    return True, "native English fonts preserved source layout from resident paletted textures across reloads"


def mutate(output: Path, manifest: dict, case: str) -> dict:
    if case == "catalog-header":
        path = output / "base/retail_ui.uic"
    elif case.startswith("font-"):
        asset = manifest["cooked"]["assets"][sorted(manifest["fonts"])[0]]
        path = output / "base/retail" / asset["path"]
    elif case.startswith("texture-"):
        path = output / "base/retail" / _font_page(manifest, 0)["path"]
    else:
        raise ValueError("unknown retail negative case")
    raw = bytearray(path.read_bytes())
    original = sha256(path)
    if case == "font-missing":
        path.unlink()
    else:
        offset = 0 if case == "catalog-header" else len(raw) - 1
        if case == "texture-table":
            # A reserved header byte: the file no longer equals the catalog's admitted tables.
            offset = 32
        elif case == "texture-pixels":
            # Inside the first page block, after its tables were admitted.
            offset = struct.unpack_from("<I", raw, ui_gstx.HEADER_BYTES + 8)[0] + 1
        raw[offset] ^= 1
        path.write_bytes(raw)
    return {"path": str(path.relative_to(output)), "original_sha256": original,
            "mutated_sha256": sha256(path) if path.exists() else None}


def classify_negative(log: str, identity: str, case: str) -> tuple[bool, str]:
    if TARGET_CRASH_RE.search(log):
        return False, "EE crash while rejecting invalid retail resources"
    if not echoed(log, f"{identity}_begin") or "]ps2_uiRetail load" not in log:
        return False, "missing retail load attempt or stale native barrier"
    if CAPTURE_RE.search(log) or "BOOT production shutdown complete" in log:
        return False, "invalid retail resources completed a capture or normal shutdown"
    fatals = re.findall(r"\[D3BFG\] FATAL ([^\r\n]+)", log)
    expected = {"catalog-header": "retail UI catalog invalid", "font-missing": "retail font missing:",
                "font-mutated": "retail font content invalid:", "texture-table": "retail UI texture profile mismatch:",
                "texture-pixels": "retail UI texture profile mismatch:"}[case]
    if not fatals or any(expected not in diagnostic for diagnostic in fatals):
        return False, "missing or unexpected retail resource validation failure"
    reason = {"texture-table": "texture length or tables differ from the catalog",
              "texture-pixels": "page block, encoding or checksum"}.get(case)
    if reason is not None and reason not in log:
        return False, "corrupt retail texture was not refused by its specific check"
    if "RETAIL state=loaded" in log:
        return False, "invalid retail resources were published as a loaded proof"
    return True, "invalid retail resources were rejected before the first completed presentation"


def run(args: argparse.Namespace, standard: str, version: str, case: str = "native") -> bool:
    if smoke.emulator_is_running(args.emulator):
        raise ValueError("an emulator is already running; retail UI checks require an isolated session")
    smoke.check_config(args.config)
    manifest, _, _ = inspect_package(args.source)
    identity = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_retail_" + uuid.uuid4().hex[:12]
    output = stage_retail(args.build, REPO_ROOT / "build/test-results" / identity, standard, args.source)
    report = json.loads((output / "report.json").read_text())
    for name in report["flag_stamps"]:
        shutil.copy2(REPO_ROOT / name, output / Path(name).name)
    shutil.copy2(args.config, output / "PCSX2.ini.snapshot")
    (output / "base/d3bfg.cfg").write_text(f'echo "{identity}_begin"\nps2_maxFrames 0\necho "{identity}_ready"\n')
    mutation = mutate(output, manifest, case) if case != "native" else None
    (output / "boot-data.json").write_text(json.dumps(boot_data(output), indent=2) + "\n")
    (output / "console.txt").write_bytes(b"")
    command = [str(args.emulator), "-batch", "-elf", str(output / "d3bfg.elf"), "-logfile", str(output / "emulog.txt")]
    phases = [(f"retail-f{index}.tga", f"ps2_uiRetail font {index}", index) for index in range(len(_font_names(manifest)))]
    phases += [("retail-return.tga", "ps2_uiRetail font 0", 0), ("retail-reload.tga", "ps2_uiRetail reload", 0)]
    phase, step, completed, failure = "startup", 0, False, "watchdog timeout"
    started = time.monotonic()
    with (output / "stdout.txt").open("wb") as stdout:
        process = subprocess.Popen(command, cwd=output, stdout=stdout, stderr=subprocess.STDOUT)
        try:
            while time.monotonic() - started < args.timeout:
                log = run_headless.read_log(output)
                if TARGET_CRASH_RE.search(log):
                    failure = "EE crash during the native retail resource proof"
                    break
                if "[D3BFG] FATAL" in log:
                    completed = case != "native"
                    failure = "unexpected fatal diagnostic"
                    break
                if phase == "startup" and echoed(log, f"{identity}_ready"):
                    run_console.append_commands(output, "ps2_uiRetail load")
                    if case == "native":
                        name, select, _ = phases[step]
                        run_console.append_commands(output, select, "wait 120", "ps2_uiRetail status", f'ps2_capture "{name}"')
                    phase = "captures"
                elif case == "native" and phase == "captures" and f"CONSOLE capture={phases[step][0]} " in log:
                    step += 1
                    if step < len(phases):
                        name, select, _ = phases[step]
                        run_console.append_commands(output, select, "wait 120", "ps2_uiRetail status", f'ps2_capture "{name}"')
                    else:
                        run_console.append_commands(output, "clearHistory", "ps2_uiRetail unload", f'echo "{identity}_done"',
                                                     'ps2_capture "retail-quit.tga"', "quit")
                        phase = "quit"
                if "BOOT production shutdown complete" in log:
                    completed = True
                    break
                if process.poll() is not None:
                    failure = "emulator exited before native retail completion"
                    break
                time.sleep(0.05)
        finally:
            smoke.stop_owned_process(process)
    log = run_headless.read_log(output)
    if not completed:
        passed, reason = False, failure
    elif case == "native":
        passed, reason = classify(log, identity, standard, manifest, font_textures(manifest, args.source))
    else:
        passed, reason = classify_negative(log, identity, case)
    captures = {}
    if passed and case == "native":
        try:
            for name, _, index in phases:
                captures[name] = retail_ui_capture.verify(output / name, manifest, args.source, index, standard)
            if len({captures[name]["sha256"] for name in ("retail-f0.tga", "retail-return.tga", "retail-reload.tga")}) != 1:
                raise ValueError("retail font capture changed after eviction or warm reload")
            width, height, _ = retail_ui_capture.read_tga(output / "retail-quit.tga")
            if (width, height) != (512, 448 if standard == "ntsc" else 512):
                raise ValueError("retail quit capture has incorrect dimensions")
        except (OSError, ValueError) as error:
            passed, reason = False, str(error)
    if process.returncode != 0:
        passed, reason = False, f"emulator exited with status {process.returncode}"
    summary = {"passed": passed, "reason": reason, "configuration": args.build, "standard": standard, "case": case,
               "command": command, "emulator_version": version, "elapsed_seconds": time.monotonic() - started,
               "emulator_returncode": process.returncode, "boot_data": boot_data(output), "mutation": mutation,
               "source_manifest_sha256": sha256(args.source / "manifest.json"), "capture_checks": captures,
               "pixel_inspection_required": case == "native", "retail_swf_and_shell_active": False}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"{'PASS' if passed else 'FAIL'} retail-ui-{standard}-{case}: {reason}\nArtifacts: {output}", flush=True)
    if not passed:
        print(log[-6000:], flush=True)
    return passed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", choices=("debug", "release"), default="debug")
    parser.add_argument("--source", type=Path, default=REPO_ROOT / "build/ui-retail")
    parser.add_argument("--standard", choices=("ntsc", "pal"))
    parser.add_argument("--case", choices=("native",) + NEGATIVES,
                        help="default: both native standards and five invalid-resource checks")
    parser.add_argument("--emulator", type=Path, default=smoke.DEFAULT_EMULATOR)
    parser.add_argument("--config", type=Path, default=smoke.DEFAULT_CONFIG)
    parser.add_argument("--timeout", type=float, default=120.0)
    args = parser.parse_args()
    if not 0 < args.timeout <= 3600:
        parser.error("timeout must be positive and at most 3600 seconds")
    try:
        if smoke.emulator_is_running(args.emulator):
            raise ValueError("an emulator is already running; close it before starting isolated retail UI runs")
        smoke.check_config(args.config)
        version = smoke.emulator_version(args.emulator)
        if args.case is None or args.case == "native":
            for standard in (args.standard,) if args.standard else ("ntsc", "pal"):
                if not run(args, standard, version):
                    return 1
        for case in NEGATIVES if args.case is None else (() if args.case == "native" else (args.case,)):
            if not run(args, args.standard or "ntsc", version, case):
                return 1
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        parser.exit(2, "error: " + str(error) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
