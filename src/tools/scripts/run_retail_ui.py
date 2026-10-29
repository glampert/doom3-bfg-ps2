#!/usr/bin/env python3
# ================================================================================================
# File: run_retail_ui.py
# Brief: Drive and check the English font step of the merged UI session; run its invalid-resource cases.
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
from run_console import TARGET_CRASH_RE, echoed
from console_capture import charset_image
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


def resident_images(manifest: dict) -> set:
    """The converted images every retail screen holds open: the console charset, which the
    presentation opens at startup, and each font's atlas with its pre-rendered glyph set."""
    names = set()
    charset = charset_image(manifest)
    if charset is not None:
        names.add(charset)
    for font in manifest["fonts"]:
        names.update(entry["selected"] for entry in manifest["image_resolutions"]
                     if entry["logical_image"] == font[:-4] + ".tga" and entry["status"] == "resolved")
        if font in manifest["cooked"].get("glyph_sets", {}):
            names.add(manifest["cooked"]["glyph_sets"][font]["image"])
    return names


def font_textures(manifest: dict, source: Path) -> dict:
    """Counters with the charset and every font's converted texture open, as the font screen holds them."""
    return resident_textures(manifest, source, resident_images(manifest))


def font_phases(manifest: dict) -> list[tuple[str, str, int]]:
    """Each font capture: its name, the command selecting it and the font it shows."""
    phases = [(f"retail-f{index}.tga", f"ps2_uiRetail font {index}", index) for index in range(len(_font_names(manifest)))]
    return phases + [("retail-return.tga", "ps2_uiRetail font 0", 0), ("retail-reload.tga", "ps2_uiRetail reload", 0)]


def driver(manifest: dict, begin: str, end: str):
    """Yield each command batch with the condition, over this step's log, that ends its wait."""
    for step, (name, select, _) in enumerate(font_phases(manifest)):
        first = [f'echo "{begin}"', "ps2_uiRetail load"] if step == 0 else []
        yield ([*first, select, "wait 120", "ps2_uiRetail status", f'ps2_capture "{name}"'],
               lambda log, name=name: f"CONSOLE capture={name} " in log)
    # HostFs queues the whole batch into history before execution; clear it before the release.
    yield ["clearHistory", "ps2_uiRetail unload", f'echo "{end}"'], lambda log: echoed(log, end)


def classify(log: str, manifest: dict, source: Path, warm: set = frozenset()) -> tuple[bool, str]:
    """Check the font step's log. Images other screens opened earlier in the session stay warm: the
    counters hold those plus the charset and every font's texture and glyph set."""
    if smoke.CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
        return False, "crash or fatal diagnostic"
    fonts = _font_names(manifest)
    phases = [(name, index, 0 if name != "retail-reload.tga" else 1) for name, _, index in font_phases(manifest)]
    captures = list(CAPTURE_RE.finditer(log))
    if [match[1] for match in captures] != [name for name, _, _ in phases]:
        return False, "missing, duplicate or reordered retail captures"
    states = STATE_RE.findall(log)
    if not states:
        return False, "retail capture lacks preceding native state"
    # The cycle counts every retail screen load of the session; this step loads once, then reloads.
    first_cycle = int(states[0][0])
    previous_frame = 0
    previous_textures = None
    resident = texture_counters(resident_textures(manifest, source, set(warm) | resident_images(manifest)))
    for match, (name, font_index, reload) in zip(captures, phases):
        frame, present, _ = map(int, match.groups()[1:])
        if not previous_frame < frame or frame != present:
            return False, "retail capture did not follow a distinct completed presentation"
        previous_frame = frame
        before = log[:match.start()]
        states, textures = STATE_RE.findall(before), TEXTURES_RE.findall(before)
        if not states or not textures:
            return False, "retail capture lacks preceding native state"
        state = states[-1]
        if (int(state[0]), int(state[1]), state[2]) != (first_cycle + reload, font_index, fonts[font_index]) or int(state[3]) < 120:
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
    # The reload unloads between the last two captures; the step's release follows the last.
    unloaded = list(UNLOADED_RE.finditer(log))
    if ([int(row[1]) for row in unloaded] != [first_cycle, first_cycle + 1] or
            not captures[-2].end() < unloaded[0].start() < captures[-1].start() or unloaded[1].start() < captures[-1].end()):
        return False, "retail reload/unload sequence did not complete"
    return True, "native English fonts preserved source layout from resident paletted textures across reloads"


def verify_captures(output: Path, manifest: dict, source: Path, standard: str) -> dict:
    captures = {}
    for name, _, index in font_phases(manifest):
        captures[name] = retail_ui_capture.verify(output / name, manifest, source, index, standard)
    if len({captures[name]["sha256"] for name in ("retail-f0.tga", "retail-return.tga", "retail-reload.tga")}) != 1:
        raise ValueError("retail font capture changed after eviction or warm reload")
    return captures


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
    if case == "catalog-header":
        # The presentation opens the catalog at startup, before d3bfg.cfg echoes the identity.
        if "[D3BFG] UI adapters ready" not in log or "RETAIL catalog ready" in log:
            return False, "corrupt retail catalog was not rejected at presentation startup"
    elif not echoed(log, f"{identity}_begin") or "]ps2_uiRetail load" not in log:
        return False, "missing retail load attempt or stale native barrier"
    if CAPTURE_RE.search(log) or run_headless.SHUTDOWN_COMPLETE in log:
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


def run(args: argparse.Namespace, version: str, case: str) -> bool:
    """One invalid-resource case in a fresh process: each ends in its expected fatal."""
    if smoke.emulator_is_running(args.emulator):
        raise ValueError("an emulator is already running; retail UI checks require an isolated session")
    smoke.check_config(args.config)
    manifest, _, _ = inspect_package(args.source)
    identity = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_retail_" + uuid.uuid4().hex[:12]
    output = stage_retail(args.build, REPO_ROOT / "build/test-results" / identity, "ntsc", args.source)
    report = json.loads((output / "report.json").read_text())
    for name in report["flag_stamps"]:
        shutil.copy2(REPO_ROOT / name, output / Path(name).name)
    shutil.copy2(args.config, output / "PCSX2.ini.snapshot")
    (output / "base/d3bfg.cfg").write_text(f'echo "{identity}_begin"\nps2_maxFrames 0\necho "{identity}_ready"\n')
    mutation = mutate(output, manifest, case)
    (output / "boot-data.json").write_text(json.dumps(boot_data(output), indent=2) + "\n")
    (output / "console.txt").write_bytes(b"")
    command = [str(args.emulator), "-batch", "-elf", str(output / "d3bfg.elf"), "-logfile", str(output / "emulog.txt")]
    loading, completed, failure = False, False, "watchdog timeout"
    started = time.monotonic()
    with (output / "stdout.txt").open("wb") as stdout:
        process = subprocess.Popen(command, cwd=output, stdout=stdout, stderr=subprocess.STDOUT)
        try:
            while time.monotonic() - started < args.timeout:
                log = run_headless.read_log(output)
                if TARGET_CRASH_RE.search(log):
                    failure = "EE crash while rejecting invalid retail resources"
                    break
                if "[D3BFG] FATAL" in log:
                    completed = True
                    break
                if not loading and echoed(log, f"{identity}_ready"):
                    run_console.append_commands(output, "ps2_uiRetail load")
                    loading = True
                if run_headless.SHUTDOWN_COMPLETE in log:
                    failure = "invalid retail resources reached a normal shutdown"
                    break
                if process.poll() is not None:
                    failure = "emulator exited before the expected failure"
                    break
                time.sleep(0.05)
        finally:
            smoke.stop_owned_process(process)
    log = run_headless.read_log(output)
    passed, reason = classify_negative(log, identity, case) if completed else (False, failure)
    if process.returncode != 0:
        passed, reason = False, f"emulator exited with status {process.returncode}"
    summary = {"passed": passed, "reason": reason, "configuration": args.build, "standard": "ntsc", "case": case,
               "command": command, "emulator_version": version, "elapsed_seconds": time.monotonic() - started,
               "emulator_returncode": process.returncode, "boot_data": boot_data(output), "mutation": mutation,
               "source_manifest_sha256": sha256(args.source / "manifest.json")}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"{'PASS' if passed else 'FAIL'} retail-ui-{case}: {reason}\nArtifacts: {output}", flush=True)
    if not passed:
        print(log[-6000:], flush=True)
    return passed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", choices=("debug", "release"), default="debug")
    parser.add_argument("--source", type=Path, default=REPO_ROOT / "build/ui-retail")
    parser.add_argument("--case", choices=NEGATIVES, help="default: all five invalid-resource cases")
    parser.add_argument("--emulator", type=Path, default=smoke.DEFAULT_EMULATOR)
    parser.add_argument("--config", type=Path, default=smoke.DEFAULT_CONFIG)
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()
    if not 0 < args.timeout <= 3600:
        parser.error("timeout must be positive and at most 3600 seconds")
    try:
        if smoke.emulator_is_running(args.emulator):
            raise ValueError("an emulator is already running; close it before starting isolated retail UI runs")
        smoke.check_config(args.config)
        version = smoke.emulator_version(args.emulator)
        for case in (args.case,) if args.case else NEGATIVES:
            if not run(args, version, case):
                return 1
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        parser.exit(2, "error: " + str(error) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
