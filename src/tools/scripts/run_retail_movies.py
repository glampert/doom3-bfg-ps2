#!/usr/bin/env python3
# ================================================================================================
# File: run_retail_movies.py
# Brief: Verify native shell/dialog admission, bounded execution, English text and the fully drawn movies.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import argparse
from datetime import datetime, timezone
import json
import math
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
from ui_font import inspect_language
from ui_page_package import fnv1a32
from run_retail_ui import TEXTURES_RE, resident_images, resident_textures, texture_counters


LABELS = ("#str_swf_campaign", "#str_swf_settings", "#str_swf_quit")
STATE = re.compile(r"RETAIL movie=(\d) state=loaded frames=(\d+) dictionary=(\d+) texts=(\d+) preview=(\d+) english=(\d+) scripts=(inactive|active) heap=(\d+)/(\d+)/(\d+)")
EXECUTION = re.compile(r"RETAIL execution movie=(\d) ticks=(\d+) root=(\d+) sprites=(\d+) texts=(\d+) instructions=(\d+) calls=(\d+) commands=(\d+) depth=(\d+) native_steps=(\d+)")
OWNER = re.compile(r"RETAIL owner screen=(-?\d+) next=(-?\d+) carousel_items=(\d+) carousel_slots=(\d+) carousel_view=(\d+) credits=(\d+) callbacks=(\d+)")
DEPENDENCY = re.compile(r"SWF dependency movie=(\S+) kind=(\S+) name=([^\r\n]+)")
RELEASED = re.compile(r"RETAIL released movie=(\d) images=(\d+) heap=(\d+)/(\d+)/(\d+)")
LABEL = re.compile(r"RETAIL label=(\d) id=(\S+) width=([\d.]+) font=(\d+) height=(\d+)")
CAPTURE = re.compile(r"CONSOLE capture=(movie-[\w-]+\.tga) frame=(\d+) presents=(\d+) redraws=(\d+)")
DRAWABLE = re.compile(r"RETAIL drawable movie=1 masks=(\d+) strokes=(\d+) screen_blends=(\d+) additive_colors=(\d+)")
DIALOG_OWNER = re.compile(r"RETAIL dialog owner message=(\d+) type=(\d+) title=(\S+) text=(\S+) "
                          r"options=([^|\r\n]*)\|([^|\r\n]*)\|([^\r\n]*?) choices=(\d+)")
DRAW = re.compile(r"RETAIL draw movie=1 full=1 triangles=(\d+) flushes=(\d+) masks=(\d+) peak_masks=(\d+) "
                  r"peak_vertices=(\d+) peak_indexes=(\d+) peak_draws=(\d+)")
SHELL_DRAWABLE = re.compile(r"RETAIL drawable movie=0 masks=(\d+) strokes=(\d+) screen_blends=(\d+) additive_colors=(\d+)")
SHELL_DRAW = re.compile(r"RETAIL draw movie=0 full=1 triangles=(\d+) flushes=(\d+) masks=(\d+) peak_masks=(\d+) "
                        r"peak_vertices=(\d+) peak_indexes=(\d+) peak_draws=(\d+) depth_masks=(\d+) lines=(\d+) "
                        r"screen=(\d+) color_terms=(\d+)")
SHELL_EXECUTION = re.compile(r"RETAIL execution movie=0 ticks=(\d+) root=(\d+) sprites=(\d+) texts=(\d+) instructions=(\d+) "
                             r"calls=(\d+) commands=(\d+) depth=(\d+) native_steps=(\d+) clock=(fixed|real)")
CHANNELS = re.compile(r"RETAIL textures images=\d+ .* channel_cluts=(\d+) channel_pixels=(\d+)")
SHELL_OWNER = re.compile(r"RETAIL owner screen=(-?\d+) next=(-?\d+) carousel_items=(\d+) carousel_slots=(\d+) "
                         r"carousel_view=(\d+) credits=(\d+) callbacks=(\d+) logo=([01])")
MENU = re.compile(r"RETAIL menu screen=(-?\d+) next=(-?\d+) session=(\w+) platform=(\d+) callbacks=(\d+) root_focus=(-?\d+)")
MENU_INPUT = re.compile(r"RETAIL menu input events=(\d+) handled=(\d+) dialog=([01]) quit=([0-2])")
UNRENDERED = re.compile(r"UI glyph size font=(\S+) scale=\S+ stroke=(\S+) prerendered=no")
EXECUTION_NEGATIVES = ("movie-placement", "movie-opcode", "movie-branch", "movie-operand", "movie-budget", "movie-native-alias")
DIALOG_NEGATIVES = ("dialog-alpha-add",)
NEGATIVES = ("profile", "movie-missing", "movie-checksum", "movie-count", "movie-init", "english") + EXECUTION_NEGATIVES + DIALOG_NEGATIVES
PHASES = (("shell1", "shell", 0), ("dialog", "dialog", 1), ("shell2", "shell", 0), ("shell3", "shell", 0))
DIALOG_PHASES = (("dialog1", "dialog", 1), ("dialog2", "dialog", 1), ("dialog3", "dialog", 1))
# The first full dialog load sustains 120 frames; warm reloads prove ownership with shorter runs.
RENDER_PHASES = (("render1", 120), ("render2", 30), ("render3", 30))
# A second capture this many frames later locates the looping focus highlight, the only animation at rest.
RENDER_GAP = 10
# The shell draws on a fixed-step clock: its two warm reloads reach identical frames. Each load
# captures the start screen, then rolls the logo on as the native shell does on leaving it; this many
# frames later the logo's outlines and its screen-blended glow are drawing.
SHELL_RENDER_PHASES = (("shell1", 120), ("shell2", 30), ("shell3", 30))
SHELL_LOGO_GAP = 30
# The native shell handler drives the menu on the same clock, through key events. Each load captures
# its start screen, then presses accept, which signs in the first local user (b: the root screen has
# rolled on, with the logo's outlines and screen-blended glow), moves the root's focus two rows down
# (c: Credits) and presses back (d: the start screen again). A frame separates each capture from the
# next key, whose effects would otherwise reach the captured frame.
MENU_RENDER_PHASES = (("menu1", 120), ("menu2", 30), ("menu3", 30))
MENU_STEPS = (("b", ("accept",), 90), ("c", ("down", "down"), 30), ("d", ("back",), 90))
# Keyboard keys drive the first load and the pad's buttons the warm reloads, which must match.
MENU_KEYS = ({"accept": "enter", "down": "down", "back": "escape"},
             {"accept": "cross", "down": "dpad_down", "back": "circle"})
# Handler screens, session state, PS3 platform, admitted callbacks and the root's focus at each capture.
MENU_SCREENS = {"a": ("0", "0", "PRESS_START", "1", "5", "0"), "b": ("1", "1", "IDLE", "1", "5", "0"),
                "c": ("1", "1", "IDLE", "1", "5", "2"), "d": ("0", "0", "PRESS_START", "1", "5", "2")}
# Events reaching the menu and those it handled: accept's press only, both halves of each scroll, and
# back's press only (a released button has no release handler); no dialog is queued.
MENU_INPUTS = {"a": ("0", "0", "0", "0"), "b": ("2", "1", "0", "0"), "c": ("6", "5", "0", "0"), "d": ("8", "6", "0", "0")}
# The native quit confirmation, on one load: sign in, wrap the root's focus up to Quit and open the
# dialog (d). Its three options take focus left to right and accept selects the focused one: move to
# Cancel (e) and select it (f), open the dialog again (g), then accept it, which quits the game through
# Common right after the final capture.
MENU_QUIT_STEPS = (("a", (), 120), ("b", ("cross",), 90), ("c", ("dpad_up",), 30), ("d", ("cross",), 60),
                   ("e", ("dpad_right",), 30), ("f", ("cross",), 60), ("g", ("cross",), 60))
MENU_QUIT_SCREENS = {"a": ("0", "0", "PRESS_START", "1", "5", "0"), "b": ("1", "1", "IDLE", "1", "5", "0"),
                     **{name: ("1", "1", "IDLE", "1", "5", "3") for name in "cdefg"}}
# A shown dialog takes both halves of every key, until a choice closes it: the release of the key that
# selected Cancel then reaches the root, which has no handler for it. Dialog 1 is active, quit 2 shown.
MENU_QUIT_INPUTS = {"a": ("0", "0", "0", "0"), "b": ("2", "1", "0", "0"), "c": ("4", "3", "0", "0"),
                    "d": ("6", "4", "1", "2"), "e": ("8", "6", "1", "2"), "f": ("10", "7", "0", "0"),
                    "g": ("12", "8", "1", "2")}
POSITIVE_CASES = ("native", "execution", "dialog-execution", "dialog-render", "shell-render", "menu-render", "menu-quit")
EXECUTION_HEAP_CEILING = 16 * 1024 * 1024
PROMPT_ART = "guis/assets/hud/controller/ps3/"
# The native quit dialog's identity from Common_dialog.h (GDM_QUIT_GAME, DIALOG_DYNAMIC) and GetDialogMsg.
QUIT_DIALOG = ("66", "8", "#str_01975", "#str_dlg_confirm_quit")
QUIT_OPTIONS = ("#str_swf_accept", "#str_swf_cancel", "#str_swf_change_game")
DIALOG_DEPENDENCIES = (("method", "showMessage"), ("method", "updatePrompts"), ("method", "updateCmds"),
                       ("native-callback", "getLocalString"), ("native-callback", "getPlatform"),
                       ("native-callback", "swapPS3Buttons"), ("native-callback", "calcNumLines"),
                       ("localization", "#str_dlg_confirm_quit"), ("frame", "rollOn"))


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
            # Strongest coverage bounds first; each probe accepts what the texture can produce
            # within one texel of its projected position.
            for y, x, (low, high) in retail_ui_capture.glyph_candidates(atlas, glyph)["ink"]:
                if selected == 2:
                    break
                sx = int((pen + glyph["left"] * scale + (x + 1) * (glyph["width"] * scale + 1) / (glyph["width"] + 1)) * 512 / 640)
                sy = int((baseline - glyph["top"] * scale + (y + 1) * (glyph["height"] * scale + 1) / (glyph["height"] + 1)) * screen_height / 480)
                if not (1 <= sx < 511 and 1 <= sy < screen_height - 1):
                    continue
                if any(abs(sx - probe["x"]) < 4 and abs(sy - probe["y"]) < 4 for probe in row_probes):
                    continue
                darkest, brightest = retail_ui_capture.ink_interval(color[:3], low / 255 * color[3] / 255,
                                                                    high / 255 * color[3] / 255)
                expected = retail_ui_capture.blend(color[:3], atlas.alpha(glyph["s"] + x, glyph["t"] + y) / 255 * color[3] / 255)
                row_probes.append({"label": row, "glyph": ordinal, "x": sx, "y": sy, "expected": list(expected),
                                   "low": list(darkest), "high": list(brightest)})
                selected += 1
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
        error = min(max(max(low - actual, actual - high, 0) for actual, low, high in
                        zip(pixels[((probe["y"] + dy) * width + probe["x"] + dx) * 3:][:3], probe["low"], probe["high"]))
                    for dx in (-1, 0, 1) for dy in (-1, 0, 1))
        if error > retail_ui_capture.PIXEL_TOLERANCE:
            raise ValueError("native English text source pixel differs: " + str(probe))
    return {"sha256": sha256(path), "source_probes": probes}


def verify_final_capture(path: Path, standard: str) -> dict:
    width, height, _ = retail_ui_capture.read_tga(path)
    if (width, height) != (512, 448 if standard == "ntsc" else 512):
        raise ValueError("invalid final movie capture dimensions")
    return {"sha256": sha256(path), "width": width, "height": height}


def dialog_rounding_row(manifest: dict, source: Path) -> tuple[int, int, int]:
    """Project the final dialog label glyph's bottom raster row from its source layout."""
    prototype, _ = expected_labels(manifest, source, 1)
    swf = manifest["swfs"]["generated/swf/dialog.bswf"]
    requested = swf["dictionary"][prototype["font_id"]]["name"]
    family = next(row["family"] for row in manifest["font_resolutions"] if row["requested"] == requested)
    font = manifest["fonts"]["newfonts/" + family + "/48.dat"]
    glyphs = {row["codepoint"]: row for row in font["glyphs"]}
    language = inspect_language((source / manifest["cooked"]["assets"]["strings/english.lang"]["path"]).read_bytes())
    word = language["entries"][LABELS[-1]]
    scale = prototype["font_height"] / 960
    width = sum(glyphs[ord(char)]["advance"] * scale for char in word)
    left = prototype["bounds"][0] + prototype["left_margin"] / 20
    right = prototype["bounds"][2] - prototype["right_margin"] / 20
    pen = (left if prototype["align"] == 0 else right - width if prototype["align"] == 1 else (left + right - width) / 2)
    pen += 48 - prototype["bounds"][0] + sum(glyphs[ord(char)]["advance"] * scale for char in word[:-1])
    glyph = glyphs[ord(word[-1])]
    baseline = 320 + scale * (font["ascender"] + 1.15)
    x1 = (pen + glyph["left"] * scale) * 512 / 640
    x2 = (pen + (glyph["left"] + glyph["width"]) * scale + 1) * 512 / 640
    bottom = (baseline - glyph["top"] * scale + glyph["height"] * scale + 1) * 448 / 480
    return math.floor(x1), math.ceil(x2), math.floor(bottom)


def verify_reload_captures(output: Path, phases: tuple, captures: dict,
                           manifest: dict, source: Path, standard: str) -> dict:
    comparisons = {}
    for movie in sorted({entry[2] for entry in phases}):
        names = ["movie-" + name + ".tga" for name, _, index in phases if index == movie]
        if len(names) < 2:
            continue
        hashes = [captures[name]["sha256"] for name in names]
        if len(set(hashes)) == 1:
            comparisons[str(movie)] = {"identical": True, "changed_pixels": 0, "maximum_channel_delta": 0}
            continue
        if movie != 1 or standard != "ntsc" or len(names) < 3 or len(set(hashes[1:])) != 1:
            raise ValueError("native movie text changed across dictionary reloads")
        width, height, first = retail_ui_capture.read_tga(output / names[0])
        later_width, later_height, later = retail_ui_capture.read_tga(output / names[1])
        if (width, height) != (512, 448) or (later_width, later_height) != (width, height):
            raise ValueError("native dialog reload capture dimensions differ")
        changed = [offset for offset in range(0, len(first), 3) if first[offset:offset + 3] != later[offset:offset + 3]]
        if not changed:
            raise ValueError("native movie capture bytes changed without pixel differences")
        maximum = max(abs(first[offset + channel] - later[offset + channel]) for offset in changed for channel in range(3))
        x1, x2, row = dialog_rounding_row(manifest, source)
        # PORT_STATUS records this native NTSC final-T edge variance: at most four
        # low-coverage pixels and 25 channel units. Keep all source probes and require
        # byte-identical warm reloads; this exception applies only to the cold edge row.
        if len(changed) > 4 or maximum > 25 or any(not x1 <= (offset // 3) % width < x2 or
                                                (offset // 3) // width != row for offset in changed):
            raise ValueError("native dialog reload differs outside the measured cold glyph edge tolerance")
        comparisons[str(movie)] = {"identical": False, "changed_pixels": len(changed),
                                   "maximum_channel_delta": maximum, "source_glyph_row": [x1, x2, row],
                                   "warm_identical": True, "first_sha256": hashes[0], "warm_sha256": hashes[1]}
    return comparisons


def classify(log: str, identity: str, standard: str, manifest: dict, source: Path,
             execution: bool = False, phases: tuple = PHASES) -> tuple[bool, str]:
    if TARGET_CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
        return False, "native movie resource crash or fatal"
    if any(marker not in log for marker in (f"GS ready {standard.upper()}", "BOOT production shutdown complete", "COMMON stage=system shutdown")):
        return False, "incomplete native boot or shutdown"
    if not all(echoed(log, identity + suffix) for suffix in ("_ready", "_done")):
        return False, "stale or missing native barriers"
    captures = list(CAPTURE.finditer(log))
    if [match[1] for match in captures] != ["movie-" + name + ".tga" for name, _, _ in phases] + ["movie-quit.tga"]:
        return False, "missing or reordered native movie resource captures"
    ledgers = []
    image_counts = []
    previous = 0
    previous_capture = 0
    previous_textures = None
    open_images = resident_images(manifest)
    for capture, (_, _, movie) in zip(captures, phases):
        phase_log = log[previous_capture:capture.start()]
        states = STATE.findall(phase_log)
        labels = LABEL.findall(phase_log)
        if not states or len(labels) < 3:
            return False, "native movie or label state missing"
        row = states[-1]
        state = tuple(map(int, row[:6] + row[7:]))
        if row[6] != ("active" if execution else "inactive"):
            return False, "native movie script activation mode differs"
        swf = manifest["swfs"]["generated/swf/" + ("shell" if movie == 0 else "dialog") + ".bswf"]
        prototype, expected = expected_labels(manifest, source, movie)
        if (state[0] != movie or state[1] < 120 or state[2] != len(swf["dictionary"]) or
                state[3] != swf["dictionary_counts"]["edittext"] or state[4] != prototype["id"] or
                state[5] != manifest["language"]["entry_count"]):
            return False, "native movie counts, English ownership or frame interval differs"
        execution_rows = EXECUTION.findall(phase_log)
        owner_rows = OWNER.findall(phase_log)
        if execution:
            if not execution_rows:
                return False, "native movie execution accounting missing"
            executions = [tuple(map(int, entry)) for entry in execution_rows]
            for earlier, later in zip(executions, executions[1:]):
                if (earlier[0] != movie or later[0] != movie or
                        any(later[index] < earlier[index] for index in (1, 5, 6, 7, 9))):
                    return False, "native movie execution totals moved backwards"
            measured = executions[-1]
            movie_identity, ticks, root, sprites, texts, instructions, calls, commands, depth, native_steps = measured
            root_frames = next(sprite["frames"] for sprite in swf["sprites"] if sprite["dictionary_id"] is None)
            if (movie_identity != movie or ticks < 120 or ticks != state[1] or not 1 <= root <= root_frames or
                    not 1 <= sprites <= 2048 or not 1 <= texts <= 512 or not 1 <= depth <= 16):
                return False, "native movie timeline or live hierarchy differs"
            # Activation contributes one step; the sustained run must advance beyond it.
            if min(instructions, calls, commands) == 0 or native_steps <= 1:
                return False, "native movie scripts or post-activation timelines did not execute"
            requested, backing, allocations = state[6:]
            if not 0 < requested <= backing <= EXECUTION_HEAP_CEILING or allocations == 0:
                return False, "native movie execution heap exceeds its bounded profile"
            dependencies = set(DEPENDENCY.findall(phase_log))
            if movie == 0:
                if not owner_rows or tuple(map(int, owner_rows[-1])) != (0, 0, 1, 5, 0, 1, 2):
                    return False, "native shell carousel or credits owner state differs"
                required = {("swf/shell.swf", kind, name) for kind, name in
                            (("function", "refreshCarousel"), ("native-callback", "refreshCarousel"),
                             ("owner-caller", "options"), ("function", "updateCredits"),
                             ("native-callback", "updateCredits"))}
            elif owner_rows:
                return False, "native shell widgets remained active in the dialog movie"
            else:
                required = {("swf/dialog.swf", kind, name) for kind, name in
                            (("function", "getLocalString"), ("native-callback", "getLocalString"),
                             ("method", "calcNumLines"), ("native-callback", "calcNumLines"),
                             ("function", "getPlatform"), ("native-callback", "getPlatform"))}
            if not required.issubset(dependencies):
                return False, "native movie required callback work was not observed in this load"
        elif execution_rows:
            return False, "inactive native movie unexpectedly executed scripts"
        elif owner_rows:
            return False, "inactive native movie unexpectedly retained shell widgets"
        actual = [(int(row[0]), row[1], float(row[2]), int(row[3]), int(row[4])) for row in labels[-3:]]
        if actual != expected:
            return False, "native English text layout differs from source glyph advances"
        frame, presents = int(capture[2]), int(capture[3])
        if frame <= previous or frame != presents:
            return False, "movie resource presentation/frame identity differs"
        previous = frame
        previous_capture = capture.end()
        logical = "generated/swf/" + ("shell" if movie == 0 else "dialog") + ".tga"
        open_images.add(next(row["selected"] for row in manifest["image_resolutions"] if row["logical_image"] == logical))
        if execution and movie == 0:
            open_images.add(next(row["selected"] for row in manifest["image_resolutions"]
                                 if row["logical_image"] == "guis/assets/mainmenu/doom3_cover.tga"))
        texture_rows = TEXTURES_RE.findall(phase_log)
        if not texture_rows:
            return False, "native movie texture accounting missing"
        textures = tuple(map(int, texture_rows[-1]))
        if textures[:5] != texture_counters(resident_textures(manifest, source, open_images)):
            return False, "native movie textures differ from the open converted images"
        triangles, uploads, evictions = textures[5:]
        if triangles == 0 or uploads == 0 or evictions != 0:
            return False, "native movie text did not draw from resident textures"
        # Images stay open across movie reloads, so later phases draw without uploading again.
        if previous_textures is not None and (uploads != previous_textures[1] or triangles <= previous_textures[0]):
            return False, "warm native text uploaded textures again or stopped drawing"
        previous_textures = textures[5:]
        ledgers.append(state[6:])
        image_counts.append(len(open_images))
    if execution:
        released = list(RELEASED.finditer(log))
        if len(released) != len(phases):
            return False, "native movie release accounting missing or duplicated"
        release_ledgers = []
        for index, (release, (_, _, movie)) in enumerate(zip(released, phases)):
            released_movie, images, requested, backing, allocations = map(int, release.groups())
            if (released_movie != movie or images != image_counts[index] or
                    not captures[index].end() < release.start() < captures[index + 1].start()):
                return False, "native movie release identity, retained images or ordering differs"
            if not 0 < requested <= backing <= EXECUTION_HEAP_CEILING or allocations == 0:
                return False, "native movie release heap exceeds its bounded profile"
            release_ledgers.append((images, requested, backing, allocations))
        repeated = [index for index, phase in enumerate(phases) if phase[2] == phases[-1][2]]
        if len(repeated) < 2 or release_ledgers[repeated[-2]] != release_ledgers[repeated[-1]]:
            return False, "native movie release changed the warm allocation ledger"
    # Inactive dictionaries retain identical ownership. Active native timelines may
    # reach different animation frames at a common-frame boundary, so compare their
    # released ownership above after transient sprite/text state has been destroyed.
    elif len(ledgers) < 2 or phases[-2][2] != phases[-1][2] or ledgers[-2] != ledgers[-1]:
        return False, "native dictionary reload changed the warm allocation ledger"
    memory = run_headless.MEMORY_RE.findall(log)
    if len(memory) != 1 or tuple(map(int, memory[0][:3])) != BASELINE or memory[0][:3] != memory[0][3:6]:
        return False, "movie resource shutdown leaked native ownership"
    shutdown = run_console.SHUTDOWN_RE.findall(log)
    if len(shutdown) != 1 or int(shutdown[0][0]) < 120 * len(phases) or int(shutdown[0][0]) != int(shutdown[0][1]):
        return False, "movie resource frame loop did not sustain one presentation per frame"
    if (int(captures[-1][2]) <= previous or
            (int(captures[-1][2]), int(captures[-1][3])) != tuple(map(int, shutdown[0][:2]))):
        return False, "final movie capture was not drained before shutdown"
    return True, ("native retail timelines, scripts and English text passed bounded execution reloads" if execution else
                  "native retail dictionaries and English text passed bounded inactive reloads")


def render_commands(step: int, phases=RENDER_PHASES) -> list[str]:
    name, frames = phases[step]
    commands = [f"wait {frames}", "ps2_uiRetail status", f'ps2_capture "movie-{name}-a.tga"']
    if phases is MENU_RENDER_PHASES:
        keys = MENU_KEYS[min(step, 1)]
        for suffix, presses, gap in MENU_STEPS:
            commands += ["wait 1", *(f"ps2_uiRetail key {keys[press]}" for press in presses), f"wait {gap}",
                         "ps2_uiRetail status", f'ps2_capture "movie-{name}-{suffix}.tga"']
        return commands
    between, gap = (["ps2_uiRetail logo on"], SHELL_LOGO_GAP) if phases is SHELL_RENDER_PHASES else ([], RENDER_GAP)
    return commands + [*between, f"wait {gap}", "ps2_uiRetail status", f'ps2_capture "movie-{name}-b.tga"']


def capture_gaps(phases=RENDER_PHASES) -> dict:
    """Each capture of a phase and the frames it follows the previous one by, at least."""
    if phases is MENU_RENDER_PHASES:
        return {"a": 0, **{suffix: 1 + gap for suffix, _, gap in MENU_STEPS}}
    return {"a": 0, "b": SHELL_LOGO_GAP if phases is SHELL_RENDER_PHASES else RENDER_GAP}


def render_capture_names(phases=RENDER_PHASES) -> list[str]:
    return [f"movie-{name}-{suffix}.tga" for name, _ in phases for suffix in capture_gaps(phases)] + ["movie-quit.tga"]


def screen_placements(swf: dict) -> int:
    return sum(entry["blend_mode"] == 4 for sprite in swf["sprites"] for entry in sprite["placements"])


def prompt_images(manifest: dict) -> set:
    """The PS3 pad prompt art, which a drawn retail movie loads for its text's button tags."""
    return {row["selected"] for row in manifest["image_resolutions"] if row["logical_image"].startswith(PROMPT_ART)}


def shell_images(manifest: dict) -> set:
    """Every retail screen's resident images, the shell atlas, the carousel's Doom 3 cover and the
    pad prompt art of the drawn shell."""
    images = resident_images(manifest) | prompt_images(manifest)
    for logical in ("generated/swf/shell.tga", "guis/assets/mainmenu/doom3_cover.tga"):
        images.add(next(row["selected"] for row in manifest["image_resolutions"] if row["logical_image"] == logical))
    return images


def menu_images(manifest: dict) -> set:
    """The shell's images plus the dialog atlas, which Common's dialog manager opens with the menu."""
    images = shell_images(manifest)
    images.add(next(row["selected"] for row in manifest["image_resolutions"] if row["logical_image"] == "generated/swf/dialog.tga"))
    return images


def menu_quit_commands() -> list[str]:
    commands = []
    for suffix, presses, gap in MENU_QUIT_STEPS:
        commands += [*(f"ps2_uiRetail key {press}" for press in presses), f"wait {gap}", "ps2_uiRetail status",
                     f'ps2_capture "movie-menuquit-{suffix}.tga"', "wait 1"]
    # The accept choice quits through Common at the frame's end, after the queued final capture.
    return commands


def channel_counters(manifest: dict, source: Path) -> tuple[int, int]:
    """Screen blending's three channel CLUT textures for the shell atlas, laid out like its CLUTs."""
    atlas = next(row["selected"] for row in manifest["image_resolutions"] if row["logical_image"] == "generated/swf/shell.tga")
    texture = ui_gstx.read_texture(source / manifest["cooked"]["runtime_images"][atlas]["path"])
    rows = (len(texture.cluts) + 3) // 4 * 16
    return 3 * len(texture.cluts), 3 * 64 * rows * 4


def classify_shell_render(log: str, identity: str, standard: str, manifest: dict, source: Path,
                          menu: bool = False) -> tuple[bool, str]:
    """Check the fully drawn native shell: admission, outlines, depth masks, screen, color terms and ownership.

    With menu set, the native shell handler owns the movie and key events drive it: each load must
    show the start screen, the root screen after accept, the root's focus moved and the start screen
    after back, with every text size drawn from pre-rendered glyphs."""
    phases = MENU_RENDER_PHASES if menu else SHELL_RENDER_PHASES
    gaps = capture_gaps(phases)
    names = tuple(gaps)
    if TARGET_CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
        return False, "native shell crash or fatal"
    if any(marker not in log for marker in (f"GS ready {standard.upper()}", "BOOT production shutdown complete", "COMMON stage=system shutdown")):
        return False, "incomplete native boot or shutdown"
    if not all(echoed(log, identity + suffix) for suffix in ("_ready", "_done")):
        return False, "stale or missing native barriers"
    captures = list(CAPTURE.finditer(log))
    if [match[1] for match in captures] != render_capture_names(phases):
        return False, "missing or reordered native shell captures"
    if UNRENDERED.search(log):
        return False, "native shell drew text at a size without pre-rendered glyphs"
    swf = manifest["swfs"]["generated/swf/shell.bswf"]
    features = swf["features"]
    expected_admission = (features["masks"], features["strokes"], screen_placements(swf), features["color_add"])
    resident = texture_counters(resident_textures(manifest, source, menu_images(manifest) if menu else shell_images(manifest)))
    channels = channel_counters(manifest, source)
    loads = list(SHELL_DRAWABLE.finditer(log))
    if len(loads) != len(phases) or any(tuple(map(int, load.groups())) != expected_admission for load in loads):
        return False, "native shell draw admission missing, repeated or different from the inspected feature profile"
    previous_frame = 0
    uploads = set()
    for phase, (_, frames) in enumerate(phases):
        shots = captures[len(names) * phase:len(names) * (phase + 1)]
        window_end = loads[phase + 1].start() if phase + 1 < len(loads) else len(log)
        if not loads[phase].end() < shots[0].start() or not shots[-1].start() < window_end:
            return False, "native shell phase ordering differs"
        rows = {}
        minimum = frames
        for name, start, shot in zip(names, [loads[phase].end()] + [shot.end() for shot in shots[:-1]], shots):
            window = log[start:shot.start()]
            states, executions, draws, owners, textures, channel_rows = (
                STATE.findall(window), SHELL_EXECUTION.findall(window), SHELL_DRAW.findall(window),
                (MENU if menu else SHELL_OWNER).findall(window), TEXTURES_RE.findall(window), CHANNELS.findall(window))
            inputs = MENU_INPUT.findall(window) if menu else [None]
            if not states or not executions or not draws or not owners or not textures or not channel_rows or not inputs:
                return False, "native shell status missing before capture"
            state, scripts, execution = tuple(map(int, states[-1][:6] + states[-1][7:])), states[-1][6], executions[-1]
            draw, owner = tuple(map(int, draws[-1])), owners[-1]
            texture, channel = tuple(map(int, textures[-1])), tuple(map(int, channel_rows[-1]))
            rows[name] = draw, texture
            minimum += gaps[name]
            if (state[0] != 0 or state[1] < minimum or state[2] != len(swf["dictionary"]) or
                    state[3] != swf["dictionary_counts"]["edittext"] or state[5] != manifest["language"]["entry_count"] or
                    scripts != "active"):
                return False, "native shell movie state, English ownership or frame interval differs"
            if not 0 < state[6] <= state[7] <= EXECUTION_HEAP_CEILING or state[8] == 0:
                return False, "native shell heap exceeds its bounded profile"
            ticks, root, sprites, texts, instructions, calls, commands, depth, steps = map(int, execution[:9])
            if (execution[9] != "fixed" or ticks != state[1] or root < 1 or not 1 <= sprites <= 2048 or not 1 <= texts <= 512 or
                    not 1 <= depth <= 16 or min(instructions, calls, commands) == 0 or steps <= 1):
                return False, "native shell timeline, fixed clock or live hierarchy differs"
            if menu and (tuple(owner) != MENU_SCREENS[name] or tuple(inputs[-1]) != MENU_INPUTS[name]):
                return False, "native menu screen, session, platform, callbacks, focus or key events differ"
            if not menu and tuple(map(int, owner)) != (0, 0, 1, 5, 0, 1, 2, 1 if name == "b" else 0):
                return False, "native shell owner state differs from the minimal carousel, Credits owners or logo state"
            (triangles, flushes, masks, peak_masks, vertices, indexes, draws_peak, depth_masks, lines, screen,
             color_terms) = draw
            # The first start screen draws no outline or screen blend; the rolled-on logo draws both.
            rolled = (lines, screen) if name != "a" or phase > 0 else (1, 1)
            if (min(triangles, flushes, masks, depth_masks, color_terms, *rolled) == 0 or not 1 <= peak_masks <= 4 or
                    not 0 < vertices <= 4096 or not 0 < indexes <= 6144 or not 0 < draws_peak <= 128):
                return False, "native shell drawing, masks, outlines, screen, color terms or geometry bounds differ"
            if texture[:5] != resident or min(texture[5], texture[6]) == 0 or texture[7] != 0 or channel != channels:
                return False, "native shell textures differ from its converted images and channel CLUTs, or were evicted"
        before, after = rows["a"][0], rows["b"][0]
        if not all(after[index] > before[index] for index in (0, 2, 7, 8, 9, 10)):
            return False, "native shell stopped drawing masks, outlines, screen blends or color terms"
        if any(rows[later][0][index] <= rows[earlier][0][index] for earlier, later in zip(names[1:], names[2:]) for index in (0, 2, 7)):
            return False, "native menu stopped drawing between its captures"
        # The channel CLUTs upload at the first screen pass, after the start screen's capture;
        # from then on, reloads included, nothing uploads again.
        if phase == 0:
            if rows["b"][1][6] < rows["a"][1][6]:
                return False, "native shell upload counter went backwards"
        else:
            uploads.add(rows["a"][1][6])
        uploads.update(rows[name][1][6] for name in names[1:])
        if len(uploads) != 1:
            return False, "native shell uploaded textures again while drawing"
        for shot in shots:
            frame, presents = int(shot[2]), int(shot[3])
            if frame <= previous_frame or frame != presents:
                return False, "native shell presentation/frame identity differs"
            previous_frame = frame
    released = list(RELEASED.finditer(log))
    if len(released) != len(phases):
        return False, "native shell release accounting missing or duplicated"
    ledgers = []
    for index, release in enumerate(released):
        movie, open_images, requested, backing, allocations = map(int, release.groups())
        boundary = loads[index + 1].start() if index + 1 < len(loads) else captures[-1].start()
        if (movie != 0 or not captures[len(names) * (index + 1) - 1].end() < release.start() < boundary or
                not 0 < requested <= backing <= EXECUTION_HEAP_CEILING or allocations == 0):
            return False, "native shell release identity or ordering differs"
        ledgers.append((open_images, requested, backing, allocations))
    if ledgers[-1] != ledgers[-2]:
        return False, "native shell release changed the warm allocation ledger"
    memory = run_headless.MEMORY_RE.findall(log)
    if len(memory) != 1 or tuple(map(int, memory[0][:3])) != BASELINE or memory[0][:3] != memory[0][3:6]:
        return False, "shell shutdown leaked native ownership"
    shutdown = run_console.SHUTDOWN_RE.findall(log)
    minimum = sum(frames + sum(gaps.values()) for _, frames in phases)
    if len(shutdown) != 1 or int(shutdown[0][0]) < minimum or int(shutdown[0][0]) != int(shutdown[0][1]):
        return False, "shell frame loop did not sustain one presentation per frame"
    if (int(captures[-1][2]) <= previous_frame or
            (int(captures[-1][2]), int(captures[-1][3])) != tuple(map(int, shutdown[0][:2]))):
        return False, "final shell capture was not drained before shutdown"
    if menu:
        return True, "native shell handler drew its start and root screens, driven by keyboard and pad keys"
    return True, "native retail shell drew in full through outlines, depth masks, screen blends, color terms and resident textures"


def verify_shell_render_captures(output: Path, standard: str, phases=SHELL_RENDER_PHASES) -> dict:
    """The fixed clock reaches the same frames on both warm reloads: their captures must match exactly."""
    height = 448 if standard == "ntsc" else 512
    images = {}
    for name in render_capture_names(phases)[:-1]:
        width, actual_height, pixels = retail_ui_capture.read_tga(output / name)
        if (width, actual_height) != (512, height):
            raise ValueError("invalid native shell capture dimensions")
        images[name] = pixels
    for suffix in capture_gaps(phases):
        second, third = images[f"movie-{phases[1][0]}-{suffix}.tga"], images[f"movie-{phases[2][0]}-{suffix}.tga"]
        if second != third:
            offset = next(index for index in range(0, len(second), 3) if second[index:index + 3] != third[index:index + 3])
            raise ValueError(f"native shell warm reloads differ at x={offset // 3 % 512} y={offset // 3 // 512} ({suffix})")
    distinct = {}
    for name, pixels in images.items():
        distinct[name] = len({bytes(pixels[offset:offset + 3]) for offset in range(0, len(pixels), 3)})
        # Artwork, glows and text give hundreds of 16-bit colors; a blank or flat frame has a handful.
        if distinct[name] < 128:
            raise ValueError(f"native shell capture lacks artwork and text detail: {name}")
    result = {name: {"sha256": sha256(output / name), "distinct_colors": distinct[name]} for name in images}
    result["movie-quit.tga"] = verify_final_capture(output / "movie-quit.tga", standard)
    return result


def menu_quit_capture_names() -> list[str]:
    return [f"movie-menuquit-{suffix}.tga" for suffix, _, _ in MENU_QUIT_STEPS] + ["movie-quit.tga"]


def classify_menu_quit(log: str, identity: str, standard: str, manifest: dict, source: Path) -> tuple[bool, str]:
    """Check the native quit confirmation: opened from the root's Quit through Common's dialog manager,
    cancelled, opened again and accepted, which quits the game right after the final capture."""
    if TARGET_CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
        return False, "native menu crash or fatal"
    if any(marker not in log for marker in (f"GS ready {standard.upper()}", "BOOT production shutdown complete", "COMMON stage=system shutdown")):
        return False, "incomplete native boot or shutdown"
    if not all(echoed(log, identity + suffix) for suffix in ("_ready", "_done")):
        return False, "stale or missing native barriers"
    captures = list(CAPTURE.finditer(log))
    if [match[1] for match in captures] != menu_quit_capture_names():
        return False, "missing or reordered native menu captures"
    shell, dialog = manifest["swfs"]["generated/swf/shell.bswf"], manifest["swfs"]["generated/swf/dialog.bswf"]
    shell_loads, dialog_loads = list(SHELL_DRAWABLE.finditer(log)), list(DRAWABLE.finditer(log))
    expected_shell = (shell["features"]["masks"], shell["features"]["strokes"], screen_placements(shell), shell["features"]["color_add"])
    if (len(shell_loads) != 1 or tuple(map(int, shell_loads[0].groups())) != expected_shell or len(dialog_loads) != 1 or
            tuple(map(int, dialog_loads[0].groups())) != (dialog["features"]["masks"], 0, 0, 0)):
        return False, "shell and dialog draw admissions missing, repeated or different from the inspected profiles"
    # The dialog's button tags draw as prompt icons, and every other size is pre-rendered.
    if UNRENDERED.search(log):
        return False, "native menu drew text at a size without pre-rendered glyphs"
    resident = texture_counters(resident_textures(manifest, source, menu_images(manifest)))
    channels = channel_counters(manifest, source)
    start = shell_loads[0].end()
    for (suffix, _, _), capture in zip(MENU_QUIT_STEPS, captures):
        window = log[start:capture.start()]
        owners, inputs, textures, channel_rows = (MENU.findall(window), MENU_INPUT.findall(window),
                                                 TEXTURES_RE.findall(window), CHANNELS.findall(window))
        if not owners or not inputs or not textures or not channel_rows:
            return False, "native menu status missing before capture"
        if tuple(owners[-1]) != MENU_QUIT_SCREENS[suffix] or tuple(inputs[-1]) != MENU_QUIT_INPUTS[suffix]:
            return False, f"native menu screen, focus, key events or dialog state differ at capture {suffix}"
        texture = tuple(map(int, textures[-1]))
        if texture[:5] != resident or texture[7] != 0 or tuple(map(int, channel_rows[-1])) != channels:
            return False, "native menu textures differ from the shell's and dialog's converted images, or were evicted"
        start = capture.end()
    # The shown dialog reaches its native layout services, and its buttons call the pinned choices.
    # Traces are recorded once per kind and name, and the shell plays rollOn frames first.
    dependencies = set(DEPENDENCY.findall(log[captures[2].end():captures[3].start()]))
    services = {dependency for dependency in DIALOG_DEPENDENCIES if dependency[0] != "frame"}
    if not services.issubset({(kind, name) for movie, kind, name in dependencies if movie == "dialog"}):
        return False, "native quit dialog services missing"
    # Each button calls its pinned choice through a copied property: Cancel's after the focus move,
    # Accept's after the dialog reopens (traces are recorded once per name).
    choices = [match for match in DEPENDENCY.finditer(log) if match[1] == "dialog" and match[2] == "native-callback" and
               re.fullmatch(r"func\d*", match[3])]
    if (len(choices) != 2 or choices[0][3] == choices[1][3] or not captures[4].end() < choices[0].start() < captures[5].start() or
            not captures[6].end() < choices[1].start()):
        return False, "native dialog choices were not called once to cancel and once to accept"
    released = list(RELEASED.finditer(log))
    if len(released) != 1 or released[0].start() < captures[-1].end() or int(released[0][1]) != 0:
        return False, "native menu release missing, repeated or before the dialog's quit"
    memory = run_headless.MEMORY_RE.findall(log)
    if len(memory) != 1 or tuple(map(int, memory[0][:3])) != BASELINE or memory[0][:3] != memory[0][3:6]:
        return False, "menu shutdown leaked native ownership"
    shutdown = run_console.SHUTDOWN_RE.findall(log)
    minimum = sum(gap + 1 for _, _, gap in MENU_QUIT_STEPS)
    if (len(shutdown) != 1 or int(shutdown[0][0]) < minimum or int(shutdown[0][0]) != int(shutdown[0][1]) or
            (int(captures[-1][2]), int(captures[-1][3])) != tuple(map(int, shutdown[0][:2]))):
        return False, "the dialog's quit did not end the frame loop after the final capture"
    return True, "native quit dialog opened from the root, cancelled, then accepted, quitting through Common"


def verify_menu_quit_captures(output: Path, standard: str) -> dict:
    height = 448 if standard == "ntsc" else 512
    result = {}
    for name in menu_quit_capture_names():
        width, actual_height, pixels = retail_ui_capture.read_tga(output / name)
        if (width, actual_height) != (512, height):
            raise ValueError("invalid native menu capture dimensions")
        distinct = len({bytes(pixels[offset:offset + 3]) for offset in range(0, len(pixels), 3)})
        if distinct < 128:
            raise ValueError(f"native menu capture lacks artwork and text detail: {name}")
        result[name] = {"sha256": sha256(output / name), "distinct_colors": distinct}
    return result


def classify_dialog_render(log: str, identity: str, standard: str, manifest: dict, source: Path) -> tuple[bool, str]:
    """Check the fully drawn native quit dialog: admission, owner state, scripts, masks, textures and ownership."""
    if TARGET_CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
        return False, "native dialog crash or fatal"
    if any(marker not in log for marker in (f"GS ready {standard.upper()}", "BOOT production shutdown complete", "COMMON stage=system shutdown")):
        return False, "incomplete native boot or shutdown"
    if not all(echoed(log, identity + suffix) for suffix in ("_ready", "_done")):
        return False, "stale or missing native barriers"
    captures = list(CAPTURE.finditer(log))
    if [match[1] for match in captures] != render_capture_names():
        return False, "missing or reordered native dialog captures"
    # Button tags draw as prompt icons, and every text size is pre-rendered.
    if UNRENDERED.search(log):
        return False, "native dialog drew text at a size without pre-rendered glyphs"
    swf = manifest["swfs"]["generated/swf/dialog.bswf"]
    language = inspect_language((source / manifest["cooked"]["assets"]["strings/english.lang"]["path"]).read_bytes())
    options = tuple(language["entries"][key] for key in QUIT_OPTIONS)
    expected_owner = QUIT_DIALOG + options + ("3",)
    images = resident_images(manifest) | prompt_images(manifest)
    images.add(next(row["selected"] for row in manifest["image_resolutions"]
                    if row["logical_image"] == "generated/swf/dialog.tga"))
    resident = texture_counters(resident_textures(manifest, source, images))
    loads = list(DRAWABLE.finditer(log))
    if len(loads) != len(RENDER_PHASES):
        return False, "native dialog draw admission missing or repeated"
    for admission in loads:
        if tuple(map(int, admission.groups())) != (swf["features"]["masks"], 0, 0, 0):
            return False, "native dialog draw admission differs from the inspected feature profile"
    previous_frame = 0
    uploads = set()
    for phase, (_, frames) in enumerate(RENDER_PHASES):
        first, second = captures[2 * phase], captures[2 * phase + 1]
        window_end = loads[phase + 1].start() if phase + 1 < len(loads) else len(log)
        if not loads[phase].end() < first.start() < second.start() < window_end:
            return False, "native dialog phase ordering differs"
        rows = {}
        for name, start, end in (("a", loads[phase].end(), first.start()), ("b", first.end(), second.start())):
            window = log[start:end]
            states, executions, draws, owners, textures = (STATE.findall(window), EXECUTION.findall(window), DRAW.findall(window),
                                                           DIALOG_OWNER.findall(window), TEXTURES_RE.findall(window))
            if not states or not executions or not draws or not owners or not textures:
                return False, "native dialog status missing before capture"
            rows[name] = (tuple(map(int, states[-1][:6] + states[-1][7:])), states[-1][6],
                          tuple(map(int, executions[-1])), tuple(map(int, draws[-1])), owners[-1], tuple(map(int, textures[-1])))
        for name, (state, scripts, execution, draw, owner, texture) in rows.items():
            minimum = frames + (RENDER_GAP if name == "b" else 0)
            if (state[0] != 1 or state[1] < minimum or state[2] != len(swf["dictionary"]) or
                    state[3] != swf["dictionary_counts"]["edittext"] or state[5] != manifest["language"]["entry_count"] or
                    scripts != "active"):
                return False, "native dialog movie state, English ownership or frame interval differs"
            if not 0 < state[6] <= state[7] <= EXECUTION_HEAP_CEILING or state[8] == 0:
                return False, "native dialog heap exceeds its bounded profile"
            movie, ticks, root, sprites, texts, instructions, calls, commands, depth, steps = execution
            if (movie != 1 or ticks != state[1] or root != 1 or not 1 <= sprites <= 2048 or not 1 <= texts <= 512 or
                    not 1 <= depth <= 16 or min(instructions, calls, commands) == 0 or steps <= 1):
                return False, "native dialog timeline or live hierarchy differs"
            if owner != expected_owner:
                return False, "native quit dialog state differs from Common's message table or English options"
            triangles, flushes, masks, peak_masks, vertices, indexes, draws_peak = draw
            if (min(triangles, flushes, masks) == 0 or not 1 <= peak_masks <= 4 or not 0 < vertices <= 4096 or
                    not 0 < indexes <= 6144 or not 0 < draws_peak <= 128):
                return False, "native dialog drawing, masks or geometry bounds differ"
            if texture[:5] != resident or min(texture[5], texture[6]) == 0 or texture[7] != 0:
                return False, "native dialog textures differ from its converted images, did not draw or were evicted"
        if not (rows["b"][3][0] > rows["a"][3][0] and rows["b"][3][2] > rows["a"][3][2] and
                rows["b"][2][7] > rows["a"][2][7] and rows["b"][5][5] > rows["a"][5][5]):
            return False, "native dialog stopped drawing masks, textures or running its timeline"
        # Resident textures: once the first frames have drawn, nothing uploads again, reloads included.
        uploads.update((rows["a"][5][6], rows["b"][5][6]))
        if len(uploads) != 1:
            return False, "native dialog uploaded textures again while drawing"
        dependencies = {(kind, name) for movie, kind, name in DEPENDENCY.findall(log[loads[phase].start():second.start()])
                        if movie == "swf/dialog.swf"}
        if not set(DIALOG_DEPENDENCIES).issubset(dependencies):
            return False, "native dialog required script work was not observed in this load"
        frame, presents = int(second[2]), int(second[3])
        if int(first[2]) <= previous_frame or frame <= int(first[2]) or frame != presents or int(first[2]) != int(first[3]):
            return False, "native dialog presentation/frame identity differs"
        previous_frame = frame
    released = list(RELEASED.finditer(log))
    if len(released) != len(RENDER_PHASES):
        return False, "native dialog release accounting missing or duplicated"
    ledgers = []
    for index, release in enumerate(released):
        movie, open_images, requested, backing, allocations = map(int, release.groups())
        boundary = loads[index + 1].start() if index + 1 < len(loads) else captures[-1].start()
        if (movie != 1 or open_images != len(images) or not captures[2 * index + 1].end() < release.start() < boundary or
                not 0 < requested <= backing <= EXECUTION_HEAP_CEILING or allocations == 0):
            return False, "native dialog release identity, retained images or ordering differs"
        ledgers.append((open_images, requested, backing, allocations))
    if ledgers[-1] != ledgers[-2]:
        return False, "native dialog release changed the warm allocation ledger"
    memory = run_headless.MEMORY_RE.findall(log)
    if len(memory) != 1 or tuple(map(int, memory[0][:3])) != BASELINE or memory[0][:3] != memory[0][3:6]:
        return False, "dialog shutdown leaked native ownership"
    shutdown = run_console.SHUTDOWN_RE.findall(log)
    minimum = sum(frames + RENDER_GAP for _, frames in RENDER_PHASES)
    if len(shutdown) != 1 or int(shutdown[0][0]) < minimum or int(shutdown[0][0]) != int(shutdown[0][1]):
        return False, "dialog frame loop did not sustain one presentation per frame"
    if (int(captures[-1][2]) <= previous_frame or
            (int(captures[-1][2]), int(captures[-1][3])) != tuple(map(int, shutdown[0][:2]))):
        return False, "final dialog capture was not drained before shutdown"
    return True, "native retail quit dialog drew in full through admitted masks, blends, resident textures and text"


def verify_dialog_render_captures(output: Path, standard: str) -> dict:
    """Locate the looping focus highlight within each load, then require reloads to match everywhere else."""
    height = 448 if standard == "ntsc" else 512
    images = {}
    for name in render_capture_names()[:-1]:
        width, actual_height, pixels = retail_ui_capture.read_tga(output / name)
        if (width, actual_height) != (512, height):
            raise ValueError("invalid native dialog capture dimensions")
        images[name] = pixels
    animated = set()
    for name, _ in RENDER_PHASES:
        first, second = images[f"movie-{name}-a.tga"], images[f"movie-{name}-b.tga"]
        animated.update(offset // 3 for offset in range(0, len(first), 3) if first[offset:offset + 3] != second[offset:offset + 3])
    # The dialog's only animation at rest is the selected option's pulse: a small bounded region.
    region = None
    if animated:
        xs, ys = [pixel % 512 for pixel in animated], [pixel // 512 for pixel in animated]
        region = (min(xs) - 2, min(ys) - 2, max(xs) + 2, max(ys) + 2)
        if (region[2] - region[0]) * (region[3] - region[1]) > 512 * height // 100:
            raise ValueError("native dialog animation exceeds the selected-option highlight bound")
    reference = images["movie-" + RENDER_PHASES[0][0] + "-a.tga"]
    compared = 0
    for name, pixels in images.items():
        for pixel in range(512 * height):
            x, y = pixel % 512, pixel // 512
            if region is not None and region[0] <= x <= region[2] and region[1] <= y <= region[3]:
                continue
            if pixels[pixel * 3:pixel * 3 + 3] != reference[pixel * 3:pixel * 3 + 3]:
                raise ValueError(f"native dialog differs outside its animated highlight: {name} x={x} y={y}")
            compared += 1
    colors = {bytes(reference[offset:offset + 3]) for offset in range(0, len(reference), 3)}
    # The 16-bit framebuffer yields about 300 distinct colors here; a blank or flat frame has a handful.
    if len(colors) < 128:
        raise ValueError("native dialog capture lacks textured and text detail")
    result = {name: {"sha256": sha256(output / name)} for name in images}
    result["movie-quit.tga"] = verify_final_capture(output / "movie-quit.tga", standard)
    result["comparison"] = {"animated_region": region, "animated_pixels": len(animated),
                            "static_pixels_compared": compared, "distinct_colors": len(colors)}
    return result


def root_commands(raw: bytes) -> list[tuple[int, int, int]]:
    """Locate trusted native root command bodies before applying rejection mutations."""
    offset = 24
    count = struct.unpack_from(">I", raw, offset)[0]
    offset += 4 + count * 4
    labels = struct.unpack_from(">I", raw, offset)[0]
    offset += 4
    for _ in range(labels):
        size = struct.unpack_from("<I", raw, offset + 4)[0]
        offset += 8 + size
    size = struct.unpack_from(">I", raw, offset)[0]
    body = offset + 4
    offset = body + size
    commands = struct.unpack_from(">I", raw, offset)[0]
    rows = []
    for index in range(commands):
        tag, size = struct.unpack_from(">II", raw, offset + 4 + index * 8)
        rows.append((tag, body, size))
        body += size
    return rows


def replace_root_command(raw: bytes, tag: int, replacement: bytes, new_tag: int = None) -> bytearray:
    """Replace the first trusted root command with a tag, retaining subsequent native container fields."""
    commands = root_commands(raw)
    selected = next(((index, row) for index, row in enumerate(commands) if row[0] == tag), None)
    if selected is None:
        raise ValueError("retail root lacks the command required for replacement")
    index, (_, offset, size) = selected
    buffer_start = commands[0][1]
    buffer_size = struct.unpack_from(">I", raw, buffer_start - 4)[0]
    descriptor = buffer_start + buffer_size + 4 + index * 8
    result = bytearray(raw)
    struct.pack_into(">II", result, descriptor, tag if new_tag is None else new_tag, len(replacement))
    struct.pack_into(">I", result, buffer_start - 4, buffer_size + len(replacement) - size)
    result[offset:offset + size] = replacement
    return result


def replace_root_action(raw: bytes, replacement: bytes) -> bytearray:
    """Replace a trusted root DoAction while retaining subsequent native container fields."""
    return replace_root_command(raw, 12, replacement)


def alpha_add_dialog(raw: bytes) -> bytearray:
    """Re-place the dialog's first root character with a color transform whose alpha add term is set."""
    placement = next((row for row in root_commands(raw) if row[0] == 26), None)
    if placement is None or not raw[placement[1]] & 2:
        raise ValueError("retail dialog root lacks a character placement to re-encode")
    depth, character = struct.unpack_from("<HH", raw, placement[1] + 1)
    # Character and color flags; the transform has add terms only, two bits each: R, G and B zero, A one.
    return replace_root_command(raw, 26, bytes((0x0A,)) + struct.pack("<HH", depth, character) + b"\x88\x04")


def mutate(output: Path, case: str) -> None:
    path = output / "base/retail/shell.bswf"
    profile = output / "base/retail/movies.uim"
    if case == "dialog-alpha-add":
        dialog = output / "base/retail/dialog.bswf"
        raw = alpha_add_dialog(dialog.read_bytes())
        dialog.write_bytes(raw)
        descriptor = bytearray(profile.read_bytes())
        # The dialog's length/FNV words follow the shell's in the fixed movie profile.
        struct.pack_into("<II", descriptor, 48, len(raw), fnv1a32(raw))
        profile.write_bytes(descriptor)
        return
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
    elif case in EXECUTION_NEGATIVES:
        tag = 26 if case == "movie-placement" else 12
        command = next((row for row in root_commands(raw) if row[0] == tag), None)
        minimum = {"movie-budget": 6, "movie-native-alias": 1}.get(case, 5)
        if command is None or command[2] < minimum:
            raise ValueError("retail root lacks the command required for mutation: " + case)
        offset = command[1]
        if case == "movie-placement":
            raw[offset] |= 0x80  # A clip-action block is outside the admitted placement language.
        elif case == "movie-opcode":
            raw[offset] = 0xFF
        elif case == "movie-branch":
            raw[offset:offset + 5] = b"\x99\x02\x00\xff\x7f"
        elif case == "movie-budget":
            # A backward jump to its own instruction boundary is valid grammar but never terminates.
            # Stop instructions preserve the body length without noncanonical bytes after End.
            raw[offset:offset + command[2]] = b"\x99\x02\x00\xfb\xff" + b"\x07" * (command[2] - 6) + b"\0"
        elif case == "movie-native-alias":
            def string(value):
                return b"\0" + value.encode("ascii") + b"\0"

            def push(values):
                return b"\x96" + struct.pack("<H", len(values)) + values

            # Overwrite an existing global slot with another real native function, then call it
            # through the otherwise admitted English callback name and signature.
            alias = push(string("getLocalString") + string("setCVarInteger")) + b"\x1c\x1d"
            arguments = string("#str_swf_campaign") + b"\x07" + struct.pack("<i", 1) + string("getLocalString")
            raw = replace_root_action(raw, alias + push(arguments) + b"\x3d\x17\0")
        else:
            raw[offset:offset + 4] = b"\x96\x01\x00\x00"  # Push string without its required terminator.
    else:
        raw[0] ^= 1
    path.write_bytes(raw)
    if case.startswith("movie-") and case != "movie-checksum":
        descriptor = bytearray(profile.read_bytes())
        struct.pack_into("<I", descriptor, 32, len(raw))
        struct.pack_into("<I", descriptor, 36, fnv1a32(raw))
        profile.write_bytes(descriptor)


def classify_negative(log: str, identity: str, case: str) -> tuple[bool, str]:
    expected = {"profile": "retail movie profile invalid", "movie-missing": "retail movie resource missing:",
                "movie-checksum": "retail movie resource checksum mismatch:",
                "movie-count": "retail movie structural preflight failed:",
                "movie-init": "retail movie structural preflight failed:",
                "movie-placement": "retail movie execution preflight failed:",
                "movie-opcode": "retail movie execution preflight failed:",
                "movie-branch": "retail movie execution preflight failed:",
                "movie-operand": "retail movie execution preflight failed:",
                "movie-budget": "retail SWF execution rejected (swf/shell.swf): instruction budget",
                "movie-native-alias": "retail SWF execution rejected (swf/shell.swf): native callback identity unavailable",
                "dialog-alpha-add": "retail movie draw features unavailable: generated/swf/dialog.bswf additive_alphas=1",
                "english": "retail movie resource checksum mismatch:"}[case]
    fatals = re.findall(r"\[D3BFG\] FATAL ([^\r\n]+)", log)
    command = ("dialog quit" if case in DIALOG_NEGATIVES else
               "execute shell" if case in EXECUTION_NEGATIVES else "movie shell")
    valid = (echoed(log, identity + "_ready") and "]ps2_uiRetail " + command in log and fatals and
             all(expected in fatal for fatal in fatals) and not TARGET_CRASH_RE.search(log) and
             "CONSOLE capture=" not in log and not re.search(r"RETAIL movie=\d+ state=loaded", log))
    if case in ("movie-budget", "movie-native-alias", "dialog-alpha-add"):
        valid = valid and all(fatal == expected for fatal in fatals)
    if case == "movie-native-alias":
        valid = (valid and
                 re.search(r"SWF dependency movie=swf/shell\.swf kind=native-callback name=getLocalString(?:\r?\n|$)", log))
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
    positive = case in POSITIVE_CASES
    execution = case in ("execution", "dialog-execution")
    render = case in ("dialog-render", "shell-render", "menu-render")
    shell = case == "shell-render"
    menu = case == "menu-render"
    menu_quit = case == "menu-quit"
    render_phases = MENU_RENDER_PHASES if menu else SHELL_RENDER_PHASES if shell else RENDER_PHASES
    draw_command = "ps2_uiRetail menu" if menu else "ps2_uiRetail draw shell" if shell else "ps2_uiRetail dialog quit"
    last_capture = list(capture_gaps(render_phases))[-1]
    phases = DIALOG_PHASES if case == "dialog-execution" else PHASES
    movie_command = "execute" if execution or case in EXECUTION_NEGATIVES else "movie"
    first_command = draw_command if render or case in DIALOG_NEGATIVES else f"ps2_uiRetail {movie_command} {phases[0][1]}"
    # Resident paletted textures draw the full dialog at about 60 ms per PCSX2 debug frame, so its
    # three loads finish in under 20 s; streamed RGBA pages had needed about 1.6 s per frame. The
    # full shell on the fixed clock takes 100-140 ms per debug frame, under a minute for its loads;
    # the native menu's four captures per load take about two minutes.
    timeout = args.timeout if args.timeout is not None else (300 if shell or menu or menu_quit else 120)
    if case in NEGATIVES:
        mutate(output, case)
    (output / "boot-data.json").write_text(json.dumps(boot_data(output), indent=2) + "\n")
    command = [str(args.emulator), "-batch", "-elf", str(output / "d3bfg.elf"), "-logfile", str(output / "emulog.txt")]
    started = time.monotonic()
    phase, step, completed = "startup", 0, False
    # Record why the session ended, separately from the target verdict, for unexpected stops.
    session_end = "timeout"
    with (output / "stdout.txt").open("wb") as stdout:
        process = subprocess.Popen(command, cwd=output, stdout=stdout, stderr=subprocess.STDOUT)
        try:
            while time.monotonic() - started < timeout:
                log = run_headless.read_log(output)
                if TARGET_CRASH_RE.search(log):
                    session_end = "target crash text"
                    break
                if "[D3BFG] FATAL" in log:
                    session_end = "native fatal"
                    completed = not positive
                    break
                if phase == "startup" and echoed(log, identity + "_ready") and menu_quit:
                    # The dialog's accept ends the session; nothing follows its final capture.
                    run_console.append_commands(output, "ps2_uiRetail load", "ps2_uiRetail clock fixed", "ps2_uiRetail menu",
                                                *menu_quit_commands(), f'echo "{identity}_done"', 'ps2_capture "movie-quit.tga"',
                                                "ps2_uiRetail key cross")
                    phase = "quit"
                elif phase == "startup" and echoed(log, identity + "_ready"):
                    name, movie, _ = phases[0]
                    if shell or menu:
                        run_console.append_commands(output, "ps2_uiRetail load", "ps2_uiRetail clock fixed", first_command)
                    else:
                        run_console.append_commands(output, "ps2_uiRetail load", first_command)
                    phase = "capture"
                    if render:
                        run_console.append_commands(output, *render_commands(0, render_phases))
                    elif positive:
                        run_console.append_commands(output, "wait 120", "clearHistory", "ps2_uiRetail status", f'ps2_capture "movie-{name}.tga"')
                elif render and phase == "capture" and f"CONSOLE capture=movie-{render_phases[step][0]}-{last_capture}.tga " in log:
                    step += 1
                    if step < len(render_phases):
                        # Clear history before the next load releases the previous movie's ownership.
                        run_console.append_commands(output, "clearHistory", draw_command, *render_commands(step, render_phases))
                    else:
                        run_console.append_commands(output, "clearHistory", "ps2_uiRetail unload", f'echo "{identity}_done"',
                                                    'ps2_capture "movie-quit.tga"', "quit")
                        phase = "quit"
                elif positive and not render and phase == "capture" and f"CONSOLE capture=movie-{phases[step][0]}.tga " in log:
                    step += 1
                    if step < len(phases):
                        name, movie, _ = phases[step]
                        run_console.append_commands(output, "ps2_uiRetail " + movie_command + " " + movie, "wait 120", "clearHistory",
                                                    "ps2_uiRetail status", f'ps2_capture "movie-{name}.tga"')
                    else:
                        # HostFs queues the whole batch into history before execution. Clear the
                        # future echo's storage before taking the final released-ownership checkpoint.
                        run_console.append_commands(output, "clearHistory", "ps2_uiRetail unload", f'echo "{identity}_done"',
                                                    'ps2_capture "movie-quit.tga"', "quit")
                        phase = "quit"
                if "BOOT production shutdown complete" in log:
                    session_end = "native shutdown"
                    completed = True
                    break
                if process.poll() is not None:
                    session_end = f"emulator exited with status {process.returncode} after {time.monotonic() - started:.1f} s"
                    break
                time.sleep(0.05)
        finally:
            smoke.stop_owned_process(process)
    log = run_headless.read_log(output)
    if not completed:
        passed, reason = False, "native watchdog, crash or early exit: " + session_end
    elif menu_quit:
        passed, reason = classify_menu_quit(log, identity, standard, manifest, args.source)
    elif shell or menu:
        passed, reason = classify_shell_render(log, identity, standard, manifest, args.source, menu=menu)
    elif render:
        passed, reason = classify_dialog_render(log, identity, standard, manifest, args.source)
    elif positive:
        passed, reason = classify(log, identity, standard, manifest, args.source, execution=execution, phases=phases)
    else:
        passed, reason = classify_negative(log, identity, case)
    captures = {}
    reload_comparisons = {}
    if passed and menu_quit:
        try:
            captures = verify_menu_quit_captures(output, standard)
        except (OSError, ValueError) as error:
            passed, reason = False, str(error)
    elif passed and render:
        try:
            captures = (verify_shell_render_captures(output, standard, render_phases) if shell or menu else
                        verify_dialog_render_captures(output, standard))
        except (OSError, ValueError) as error:
            passed, reason = False, str(error)
    elif passed and positive:
        try:
            for name, _, movie in phases:
                path = output / ("movie-" + name + ".tga")
                captures[path.name] = verify_capture(path, manifest, args.source, movie, standard)
            reload_comparisons = verify_reload_captures(output, phases, captures, manifest, args.source, standard)
            captures["movie-quit.tga"] = verify_final_capture(output / "movie-quit.tga", standard)
        except (OSError, ValueError) as error:
            passed, reason = False, str(error)
    if process.returncode != 0:
        passed, reason = False, "emulator exited with status " + str(process.returncode)
    (output / "summary.json").write_text(json.dumps({"passed": passed, "reason": reason, "build": args.build,
        "standard": standard, "case": case, "command": command, "boot_data": boot_data(output),
        "capture_sha256": captures, "pixel_inspection_required": positive, "scripts_active": execution,
        "reload_comparisons": reload_comparisons, "shell_active": shell or menu or menu_quit, "native_menu": menu or menu_quit,
        "full_draw": render or menu_quit,
        "session_end": session_end, "session_seconds": round(time.monotonic() - started, 1)}, indent=2) + "\n")
    print(f"{'PASS' if passed else 'FAIL'} retail-movies-{standard}-{case}: {reason}\nArtifacts: {output}", flush=True)
    if not passed:
        print(log[-6000:], flush=True)
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", choices=("debug", "release"), default="debug")
    parser.add_argument("--source", type=Path, default=REPO_ROOT / "build/ui-retail")
    parser.add_argument("--standard", choices=("ntsc", "pal"))
    parser.add_argument("--case", choices=POSITIVE_CASES + NEGATIVES)
    parser.add_argument("--emulator", type=Path, default=smoke.DEFAULT_EMULATOR)
    parser.add_argument("--config", type=Path, default=smoke.DEFAULT_CONFIG)
    parser.add_argument("--timeout", type=float, help="seconds per session; 120 by default")
    args = parser.parse_args()
    if args.timeout is not None and not 0 < args.timeout <= 3600:
        parser.error("timeout must be positive and at most 3600 seconds")
    try:
        smoke.check_config(args.config)
        if args.case is None or args.case in POSITIVE_CASES:
            for case in ("native", "execution", "dialog-render", "shell-render", "menu-render", "menu-quit") if args.case is None else (args.case,):
                for standard in (args.standard,) if args.standard else ("ntsc", "pal"):
                    if not run(args, standard, case):
                        return 1
        for case in NEGATIVES if args.case is None else (() if args.case in POSITIVE_CASES else (args.case,)):
            if not run(args, args.standard or "ntsc", case):
                return 1
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        parser.exit(2, "error: " + str(error) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
