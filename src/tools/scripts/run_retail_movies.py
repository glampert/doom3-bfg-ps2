#!/usr/bin/env python3
# ================================================================================================
# File: run_retail_movies.py
# Brief: Verify inactive native shell/dialog dictionaries and English SWF text through production.
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
import run_headless
import run_pcsx2_test as smoke
from run_ui import BASELINE, TARGET_CRASH_RE, echoed
from stage_headless import REPO_ROOT, boot_data, sha256
from stage_retail_ui import inspect_package, stage_retail
from ui_font import inspect_language
from ui_page_package import fnv1a32
from run_retail_ui import PAGES_RE


LABELS = ("#str_swf_campaign", "#str_swf_settings", "#str_swf_quit")
STATE = re.compile(r"RETAIL movie=(\d) state=loaded frames=(\d+) dictionary=(\d+) texts=(\d+) preview=(\d+) english=(\d+) scripts=inactive heap=(\d+)/(\d+)/(\d+)")
LABEL = re.compile(r"RETAIL label=(\d) id=(\S+) width=([\d.]+) font=(\d+) height=(\d+)")
CAPTURE = re.compile(r"CONSOLE capture=(movie-[\w-]+\.tga) frame=(\d+) presents=(\d+) redraws=(\d+)")
NEGATIVES = ("profile", "movie-missing", "movie-checksum", "movie-count", "movie-init", "english")
PHASES = (("shell1", "shell", 0), ("dialog", "dialog", 1), ("shell2", "shell", 0), ("shell3", "shell", 0))


def expected_labels(manifest: dict, source: Path, movie: int) -> tuple[dict, list[tuple]]:
    swf = manifest["swfs"]["generated/swf/" + ("shell" if movie == 0 else "dialog") + ".bswf"]
    prototype = next(entry for entry in swf["dictionary"] if entry["type"] == "edittext" and entry["font_height"] >= 400)
    name = swf["dictionary"][prototype["font_id"]]["name"]
    language_asset = manifest["cooked"]["assets"]["strings/english.lang"]
    language = inspect_language((source / language_asset["path"]).read_bytes())
    resolution = next(record for record in manifest["font_resolutions"] if record["requested"] == name)
    font = manifest["fonts"]["newfonts/" + resolution["family"] + "/48.dat"]
    glyphs = {glyph["codepoint"]: glyph for glyph in font["glyphs"]}
    rows = []
    for index, key in enumerate(LABELS):
        value = language["entries"][key]
        width = sum(glyphs[ord(char)]["advance"] for char in value) * prototype["font_height"] / (20 * 48)
        if not prototype["flags"] & 16:
            width = min(width, prototype["bounds"][2] - prototype["bounds"][0])
        rows.append((index, key, int(width), prototype["font_id"], prototype["font_height"]))
    return prototype, rows


def text_probes(manifest: dict, source: Path, movie: int, standard: str) -> list[dict]:
    prototype, _ = expected_labels(manifest, source, movie)
    swf = manifest["swfs"]["generated/swf/" + ("shell" if movie == 0 else "dialog") + ".bswf"]
    requested = swf["dictionary"][prototype["font_id"]]["name"]
    family = next(row["family"] for row in manifest["font_resolutions"] if row["requested"] == requested)
    font_name = "newfonts/" + family + "/48.dat"
    font = manifest["fonts"][font_name]
    glyphs = {row["codepoint"]: row for row in font["glyphs"]}
    image = retail_ui_capture._image_for_font(manifest, font_name)
    atlas = retail_ui_capture.Atlas(source / manifest["cooked"]["runtime_images"][image]["path"])
    language = inspect_language((source / manifest["cooked"]["assets"]["strings/english.lang"]["path"]).read_bytes())
    scale = prototype["font_height"] / 960
    color = prototype["color_rgba"]
    screen_height = 448 if standard == "ntsc" else 512
    probes = []
    for row, label in enumerate(LABELS):
        word = language["entries"][label]
        width = sum(glyphs[ord(char)]["advance"] * scale for char in word)
        left = prototype["bounds"][0] + prototype["left_margin"] / 20
        right = prototype["bounds"][2] - prototype["right_margin"] / 20
        pen = (left if prototype["align"] == 0 else right - width if prototype["align"] == 1 else (left + right - width) / 2)
        pen += 48 - prototype["bounds"][0]
        baseline = 100 + row * 110 + scale * (font["ascender"] + 1.15)
        row_probes = []
        for ordinal, char in enumerate(word):
            glyph = glyphs[ord(char)]
            selected = 0
            for y in range(2, glyph["height"] - 2):
                for x in range(2, glyph["width"] - 2):
                    alpha = [atlas.alpha(glyph["s"] + x + dx, glyph["t"] + y + dy)
                             for dy in range(-1, 2) for dx in range(-1, 2)]
                    if min(alpha) < 224 or max(alpha) - min(alpha) > 8:
                        continue
                    sx = int((pen + glyph["left"] * scale + (x + 1) * (glyph["width"] * scale + 1) / (glyph["width"] + 1)) * 512 / 640)
                    sy = int((baseline - glyph["top"] * scale + (y + 1) * (glyph["height"] * scale + 1) / (glyph["height"] + 1)) * screen_height / 480)
                    if not (1 <= sx < 511 and 1 <= sy < screen_height - 1):
                        continue
                    if any(abs(sx - probe["x"]) < 4 and abs(sy - probe["y"]) < 4 for probe in row_probes):
                        continue
                    opacity = sum(alpha) / (len(alpha) * 255) * color[3] / 255
                    expected = [round(channel * opacity + backdrop * (1 - opacity))
                                for channel, backdrop in zip(color[:3], retail_ui_capture.BACKGROUND)]
                    row_probes.append({"label": row, "glyph": ordinal, "x": sx, "y": sy, "expected": expected})
                    selected += 1
                    if selected == 2:
                        break
                if selected == 2:
                    break
            pen += glyph["advance"] * scale
        if len(row_probes) < 2:
            raise ValueError("source glyphs lack sufficient stable native text probes")
        probes.extend(row_probes)
    return probes


def verify_capture(path: Path, manifest: dict, source: Path, movie: int, standard: str) -> dict:
    width, height, pixels = retail_ui_capture.read_tga(path)
    if (width, height) != (512, 448 if standard == "ntsc" else 512):
        raise ValueError("invalid movie resource capture dimensions")
    probes = text_probes(manifest, source, movie, standard)
    for probe in probes:
        # One pixel around the projected center accommodates GS subpixel/raster rounding.
        error = min(max(abs(actual - expected) for actual, expected in
                        zip(pixels[((probe["y"] + dy) * width + probe["x"] + dx) * 3:][:3], probe["expected"]))
                    for dx in (-1, 0, 1) for dy in (-1, 0, 1))
        if error > 8:
            raise ValueError("native English text source pixel differs: " + str(probe))
    return {"sha256": sha256(path), "source_probes": probes}


def classify(log: str, identity: str, standard: str, manifest: dict, source: Path) -> tuple[bool, str]:
    if TARGET_CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
        return False, "native movie resource crash or fatal"
    if any(marker not in log for marker in (f"GS ready {standard.upper()}", "BOOT production shutdown complete", "COMMON stage=system shutdown")):
        return False, "incomplete native boot or shutdown"
    if not all(echoed(log, identity + suffix) for suffix in ("_ready", "_done")):
        return False, "stale or missing native barriers"
    captures = list(CAPTURE.finditer(log))
    if [match[1] for match in captures] != ["movie-" + name + ".tga" for name, _, _ in PHASES] + ["movie-quit.tga"]:
        return False, "missing or reordered native movie resource captures"
    ledgers = []
    previous = 0
    previous_pages = None
    open_images = set()
    for font in manifest["fonts"]:
        logical = font[:-4] + ".tga"
        open_images.add(next(row["selected"] for row in manifest["image_resolutions"] if row["logical_image"] == logical))
    for capture, (_, _, movie) in zip(captures, PHASES):
        states = STATE.findall(log[:capture.start()])
        labels = LABEL.findall(log[:capture.start()])
        if not states or len(labels) < 3:
            return False, "native movie or label state missing"
        state = tuple(map(int, states[-1]))
        swf = manifest["swfs"]["generated/swf/" + ("shell" if movie == 0 else "dialog") + ".bswf"]
        prototype, expected = expected_labels(manifest, source, movie)
        if (state[0] != movie or state[1] < 120 or state[2] != len(swf["dictionary"]) or
                state[3] != swf["dictionary_counts"]["edittext"] or state[4] != prototype["id"] or
                state[5] != manifest["language"]["entry_count"]):
            return False, "native movie counts, English ownership or frame interval differs"
        actual = [(int(row[0]), row[1], float(row[2]), int(row[3]), int(row[4])) for row in labels[-3:]]
        if actual != expected:
            return False, "native English text layout differs from source glyph advances"
        frame, presents = int(capture[2]), int(capture[3])
        if frame <= previous or frame != presents:
            return False, "movie resource presentation/frame identity differs"
        previous = frame
        logical = "generated/swf/" + ("shell" if movie == 0 else "dialog") + ".tga"
        open_images.add(next(row["selected"] for row in manifest["image_resolutions"] if row["logical_image"] == logical))
        page_rows = PAGES_RE.findall(log[:capture.start()])
        if not page_rows:
            return False, "native movie resource page accounting missing"
        pages = tuple(map(int, page_rows[-1]))
        metadata = sum(manifest["cooked"]["runtime_images"][image]["page_count"] * 8 for image in open_images)
        if pages[:5] != (len(open_images), 6, 1572864, 262144, metadata):
            return False, "native movie atlases multiplied the shared pixel budget"
        if pages[5] == 0 or pages[6] == 0 or pages[8] == 0:
            return False, "native movie text did not draw through the page cache"
        if previous_pages is not None and (pages[5] != previous_pages[5] or pages[7] != previous_pages[7] or
                                           pages[6] <= previous_pages[6] or pages[8] <= previous_pages[8]):
            return False, "warm native text reloaded pixels or stopped drawing"
        previous_pages = pages
        ledgers.append(state[6:])
    # The first dialog use admits its atlas metadata/material. Compare repeated shell loads only
    # after both lazy image bindings exist; the complete startup ledger still must recover below.
    if ledgers[2] != ledgers[3]:
        return False, "native dictionary reload changed the warm allocation ledger"
    memory = run_headless.MEMORY_RE.findall(log)
    if len(memory) != 1 or tuple(map(int, memory[0][:3])) != BASELINE or memory[0][:3] != memory[0][3:6]:
        return False, "movie resource shutdown leaked native ownership"
    shutdown = run_console.SHUTDOWN_RE.findall(log)
    if len(shutdown) != 1 or int(shutdown[0][0]) < 480 or int(shutdown[0][0]) != int(shutdown[0][1]):
        return False, "movie resource frame loop did not sustain one presentation per frame"
    if (int(captures[-1][2]), int(captures[-1][3])) != tuple(map(int, shutdown[0][:2])):
        return False, "final movie capture was not drained before shutdown"
    return True, "native retail dictionaries and English text passed bounded inactive reloads"


def mutate(output: Path, case: str) -> None:
    path = output / "base/retail/shell.bswf"
    profile = output / "base/retail/movies.uim"
    if case == "movie-missing":
        path.unlink()
        return
    if case == "profile":
        path = profile
    elif case == "english":
        path = output / "base/retail/english.lang"
    raw = bytearray(path.read_bytes())
    if case == "movie-count":
        raw[24:28] = b"\xff\xff\xff\xff"
    elif case == "movie-init":
        # Skip root offsets, labels, command buffer and command descriptors to its init count.
        offset = 24
        count = struct.unpack_from(">I", raw, offset)[0]
        offset += 4 + count * 4
        labels = struct.unpack_from(">I", raw, offset)[0]
        offset += 4
        for _ in range(labels):
            size = struct.unpack_from("<I", raw, offset + 4)[0]
            offset += 8 + size
        size = struct.unpack_from(">I", raw, offset)[0]
        offset += 4 + size
        commands = struct.unpack_from(">I", raw, offset)[0]
        offset += 4 + commands * 8
        raw[offset:offset + 4] = b"\0\0\0\1"
    else:
        raw[0] ^= 1
    path.write_bytes(raw)
    if case in ("movie-count", "movie-init"):
        descriptor = bytearray(profile.read_bytes())
        struct.pack_into("<I", descriptor, 36, fnv1a32(raw))
        profile.write_bytes(descriptor)


def classify_negative(log: str, identity: str, case: str) -> tuple[bool, str]:
    expected = {"profile": "retail movie profile invalid", "movie-missing": "retail movie resource missing:",
                "movie-checksum": "retail movie resource checksum mismatch:",
                "movie-count": "retail movie structural preflight failed:",
                "movie-init": "retail movie structural preflight failed:",
                "english": "retail movie resource checksum mismatch:"}[case]
    fatals = re.findall(r"\[D3BFG\] FATAL ([^\r\n]+)", log)
    valid = (echoed(log, identity + "_ready") and "]ps2_uiRetail movie shell" in log and fatals and
             all(expected in fatal for fatal in fatals) and not TARGET_CRASH_RE.search(log) and
             not CAPTURE.search(log) and "RETAIL movie=0 state=loaded" not in log)
    return bool(valid), "expected native movie admission guard" if valid else "missing specific movie admission failure"


def run(args, standard, case="native"):
    if smoke.emulator_is_running(args.emulator):
        raise ValueError("an emulator is already running; movie checks require an isolated session")
    identity = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_movies_" + uuid.uuid4().hex[:12]
    manifest, _, _ = inspect_package(args.source)
    output = stage_retail(args.build, REPO_ROOT / "build/test-results" / identity, standard, args.source, True)
    shutil.copy2(args.config, output / "PCSX2.ini.snapshot")
    report = json.loads((output / "report.json").read_text())
    for name in report["flag_stamps"]:
        shutil.copy2(REPO_ROOT / name, output / Path(name).name)
    (output / "base/d3bfg.cfg").write_text(f'echo "{identity}_ready"\nps2_maxFrames 0\n')
    (output / "console.txt").write_bytes(b"")
    if case != "native":
        mutate(output, case)
    (output / "boot-data.json").write_text(json.dumps(boot_data(output), indent=2) + "\n")
    command = [str(args.emulator), "-batch", "-elf", str(output / "d3bfg.elf"), "-logfile", str(output / "emulog.txt")]
    started = time.monotonic()
    phase, step, completed = "startup", 0, False
    with (output / "stdout.txt").open("wb") as stdout:
        process = subprocess.Popen(command, cwd=output, stdout=stdout, stderr=subprocess.STDOUT)
        try:
            while time.monotonic() - started < args.timeout:
                log = run_headless.read_log(output)
                if TARGET_CRASH_RE.search(log):
                    break
                if "[D3BFG] FATAL" in log:
                    completed = case != "native"
                    break
                if phase == "startup" and echoed(log, identity + "_ready"):
                    run_console.append_commands(output, "ps2_uiRetail load", "ps2_uiRetail movie shell")
                    phase = "capture"
                    if case == "native":
                        run_console.append_commands(output, "wait 120", "clearHistory", "ps2_uiRetail status", 'ps2_capture "movie-shell1.tga"')
                elif case == "native" and phase == "capture" and f"CONSOLE capture=movie-{PHASES[step][0]}.tga " in log:
                    step += 1
                    if step < len(PHASES):
                        name, movie, _ = PHASES[step]
                        run_console.append_commands(output, "ps2_uiRetail movie " + movie, "wait 120", "clearHistory",
                                                    "ps2_uiRetail status", f'ps2_capture "movie-{name}.tga"')
                    else:
                        run_console.append_commands(output, "ps2_uiRetail unload", "clearHistory", f'echo "{identity}_done"',
                                                    'ps2_capture "movie-quit.tga"', "quit")
                        phase = "quit"
                if "BOOT production shutdown complete" in log:
                    completed = True
                    break
                if process.poll() is not None:
                    break
                time.sleep(0.05)
        finally:
            smoke.stop_owned_process(process)
    log = run_headless.read_log(output)
    passed, reason = (classify(log, identity, standard, manifest, args.source) if case == "native" else
                      classify_negative(log, identity, case)) if completed else (False, "native watchdog, crash or early exit")
    captures = {}
    if passed and case == "native":
        try:
            for name, _, _ in PHASES:
                path = output / ("movie-" + name + ".tga")
                captures[path.name] = verify_capture(path, manifest, args.source, 1 if name == "dialog" else 0, standard)
            if len({captures["movie-" + name + ".tga"]["sha256"] for name in ("shell1", "shell2", "shell3")}) != 1:
                raise ValueError("native shell text changed across dictionary reloads")
        except (OSError, ValueError) as error:
            passed, reason = False, str(error)
    if process.returncode != 0:
        passed, reason = False, "emulator exited with status " + str(process.returncode)
    (output / "summary.json").write_text(json.dumps({"passed": passed, "reason": reason, "build": args.build,
        "standard": standard, "case": case, "command": command, "boot_data": boot_data(output),
        "capture_sha256": captures, "pixel_inspection_required": case == "native", "scripts_and_shell_active": False}, indent=2) + "\n")
    print(f"{'PASS' if passed else 'FAIL'} retail-movies-{standard}-{case}: {reason}\nArtifacts: {output}", flush=True)
    if not passed:
        print(log[-6000:], flush=True)
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", choices=("debug", "release"), default="debug")
    parser.add_argument("--source", type=Path, default=REPO_ROOT / "build/ui-retail")
    parser.add_argument("--standard", choices=("ntsc", "pal"))
    parser.add_argument("--case", choices=("native",) + NEGATIVES)
    parser.add_argument("--emulator", type=Path, default=smoke.DEFAULT_EMULATOR)
    parser.add_argument("--config", type=Path, default=smoke.DEFAULT_CONFIG)
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()
    if not 0 < args.timeout <= 3600:
        parser.error("timeout must be positive and at most 3600 seconds")
    try:
        smoke.check_config(args.config)
        if args.case in (None, "native"):
            for standard in (args.standard,) if args.standard else ("ntsc", "pal"):
                if not run(args, standard):
                    return 1
        for case in NEGATIVES if args.case is None else (() if args.case == "native" else (args.case,)):
            if not run(args, args.standard or "ntsc", case):
                return 1
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        parser.exit(2, "error: " + str(error) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
