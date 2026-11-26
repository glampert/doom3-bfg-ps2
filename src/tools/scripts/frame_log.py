#!/usr/bin/env python3
# ================================================================================================
# File: frame_log.py
# Brief: Read and summarise a frame log (framelog.txt from ps2_framelog): per-stage times, frame-time budgets, the worst frames and the streaming and loader totals.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""A frame log is the package's framelog.txt after a ps2_framelog run (frame_log.cpp): FLOG#
notes, a FLOG#hdr row naming the columns, one FLOG row a frame, FLOG#loader with the loader's
totals over the run and FLOG#end with the row count (a file without it was cut short). Times are
microseconds of the EE: game (events, commands, the game step), view (the 3D views built), ui (the
screens and console), submit (the kick, the waits and the flip), and within the frame gpu (waiting
for VU1 and the GS to finish a chain) and vsync (waiting for the vertical blank); io is the loader
thread's time in reads during the frame. A log built with the GPU sampler splits gpu into what held
the chain up while the EE waited (GPU_COLUMNS: VU1 running, the GS not taking more, DIRECT data,
VIF1 unpacking or waiting for the DMA, and the GS's last primitives); with serial frames (the label's
pipeline=0) that is the whole chain, so its VU1 time over the vertices drawn is VU1's cycles a vertex,
the figure to hold against vu_cost.py's model. Columns a log lacks are left out.

Usage: frame_log.py framelog.txt [--compare other.txt] [--worst N] [--json summary.json]"""

import argparse
import json
from pathlib import Path

TIME_COLUMNS = ("frame_us", "game_us", "view_us", "ui_us", "submit_us", "gpu_us", "vu_us", "gs_us", "direct_us", "vif_us", "tail_us",
                "vsync_us", "io_us")
# The GPU sampler's split of gpu_us.
GPU_COLUMNS = ("vu_us", "gs_us", "direct_us", "vif_us", "tail_us")
# VU1 runs at the EE's clock.
VU1_CYCLES_PER_US = 294.912
COUNT_COLUMNS = ("batches", "lit_batches", "pass_batches", "vertices", "packet_bytes", "chain_bytes", "uploads", "upload_bytes",
                 "stream_loads", "stream_bytes", "stream_misses", "fallbacks", "pass_fallbacks", "loader_bytes")
# Frame-time lines: two fields of NTSC (33.3 ms) and of PAL (40 ms), and RENDERING_ARCHITECTURE.md
# 19's R9 ceilings (no frame above 50 / 60 ms).
BUDGETS_US = (16683, 33367, 40000, 50000, 60000)
# A frame paced by the vertical blank measures its fields within a few microseconds either way: one
# that took a field and 2 us still took one field.
JITTER_US = 50
# A field, from the label's standard (NTSC's 59.94 Hz unless it says PAL).
FIELD_US = {"NTSC": 1e6 / 59.94, "PAL": 20000.0}


def parse(text: str) -> dict:
    """The log's notes, columns, rows (dicts of ints) and end count (None when cut short)."""
    notes, columns, rows, loader, end = {}, None, [], {}, None
    for line in text.splitlines():
        if not line.startswith("FLOG"):
            continue
        kind, _, rest = line.partition(",")
        if kind == "FLOG#hdr":
            columns = rest.split(",")
        elif kind == "FLOG":
            if columns is None:
                raise ValueError("a FLOG row before the header")
            values = rest.split(",")
            if len(values) != len(columns):
                raise ValueError(f"a row of {len(values)} fields under a header of {len(columns)}")
            rows.append(dict(zip(columns, (int(value) for value in values))))
        elif kind == "FLOG#loader":
            loader = {key: int(value) for key, value in (field.split("=") for field in rest.split(","))}
        elif kind == "FLOG#end":
            end = int(rest)
        else:
            notes[kind[5:]] = rest
    if columns is None:
        raise ValueError("no FLOG#hdr row")
    return {"notes": notes, "columns": columns, "rows": rows, "loader": loader, "end": end}


def percentile(values: list, fraction: float) -> int:
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(fraction * len(ordered)))] if ordered else 0


def summarize(log: dict, worst: int = 8) -> dict:
    rows = log["rows"]
    if not rows:
        raise ValueError("the log has no rows")
    stages = {}
    for column in (column for column in TIME_COLUMNS if column in rows[0]):
        values = [row[column] for row in rows]
        stages[column] = {"mean": round(sum(values) / len(values)), "p50": percentile(values, 0.5), "p95": percentile(values, 0.95),
                          "p99": percentile(values, 0.99), "max": max(values)}
    counts = {column: {"mean": round(sum(row[column] for row in rows) / len(rows)), "max": max(row[column] for row in rows),
                       "total": sum(row[column] for row in rows)} for column in COUNT_COLUMNS}
    frames = [row["frame_us"] for row in rows]
    over = {str(budget): sum(1 for value in frames if value > budget + JITTER_US) for budget in BUDGETS_US}
    standard = "PAL" if "standard=PAL" in log["notes"].get("label", "") else "NTSC"
    fields = {}
    for value in frames:
        count = max(1, round(value / FIELD_US[standard]))
        fields[count] = fields.get(count, 0) + 1
    slowest = sorted(rows, key=lambda row: row["frame_us"], reverse=True)[:worst]
    gpu = None
    if all(column in rows[0] for column in GPU_COLUMNS):
        wait = sum(row["gpu_us"] for row in rows)
        shares = {column[:-3]: sum(row[column] for row in rows) for column in GPU_COLUMNS}
        vertices = sum(row["vertices"] for row in rows)
        gpu = {"wait_us": wait, "us": shares, "percent": {name: round(100.0 * value / wait, 1) if wait else 0.0 for name, value in shares.items()},
               "pipelined": "pipeline=0" not in log["notes"].get("label", ""),
               "vu1_cycles_per_vertex": round(shares["vu"] * VU1_CYCLES_PER_US / vertices, 1) if vertices else 0.0}
    return {"rows": len(rows), "complete": log["end"] == len(rows), "notes": log["notes"], "stages": stages, "counts": counts, "gpu": gpu,
            "over_budget": over, "fields": {str(count): fields[count] for count in sorted(fields)}, "standard": standard,
            "seconds": round(sum(frames) / 1e6, 2), "loader": log["loader"],
            "worst": [{key: row[key] for key in ("frame", "frame_us", "game_us", "view_us", "submit_us", "gpu_us", "vsync_us", "area",
                                                  "batches", "vertices", "uploads", "stream_misses", "x", "y", "z", "yaw")}
                      for row in slowest]}


def format_summary(summary: dict, other: dict | None = None) -> str:
    notes = summary["notes"]
    lines = [f"{summary['rows']} frames in {summary['seconds']} s, {'complete' if summary['complete'] else 'CUT SHORT'}; "
             f"{notes.get('build', '?')} build, {notes.get('display', '?')}, {notes.get('package', '?')}, {notes.get('label', '')}",
             f"{'stage':10} {'mean':>8} {'p50':>8} {'p95':>8} {'p99':>8} {'max':>8}" + ("   mean was" if other else "")]
    for column, stats in summary["stages"].items():
        line = f"{column:10} " + " ".join(f"{stats[key]:8d}" for key in ("mean", "p50", "p95", "p99", "max"))
        if other and column in other["stages"]:
            before = other["stages"][column]["mean"]
            line += f" {before:9d} ({(stats['mean'] - before) / before * 100 if before else 0.0:+.1f}%)"
        lines.append(line)
    lines.append("frames over: " + ", ".join(f"{int(budget) / 1000:.1f} ms {count}" for budget, count in summary["over_budget"].items()))
    lines.append(f"{summary['standard']} fields a frame: " + ", ".join(f"{count} x{fields}" for count, fields in summary["fields"].items()))
    counts = summary["counts"]
    lines.append(f"per frame: batches {counts['batches']['mean']} (max {counts['batches']['max']}), vertices {counts['vertices']['mean']} "
                 f"(max {counts['vertices']['max']}), chain {counts['chain_bytes']['mean']} bytes (max {counts['chain_bytes']['max']})")
    lines.append(f"textures: {counts['uploads']['total']} uploads, {counts['upload_bytes']['total']} bytes; streamed {counts['stream_loads']['total']} "
                 f"({counts['stream_bytes']['total']} bytes), misses {counts['stream_misses']['total']}, fallbacks {counts['fallbacks']['total']}, "
                 f"pass fallbacks {counts['pass_fallbacks']['total']}")
    gpu = summary.get("gpu")
    if gpu:
        scope = "the waits only (pipelined frames)" if gpu["pipelined"] else "whole chains (serial frames)"
        lines.append(f"gpu wait {gpu['wait_us'] / 1e6:.2f} s, {scope}: " +
                     ", ".join(f"{name} {gpu['percent'][name]}%" for name in gpu["us"]) +
                     f"; VU1 {gpu['vu1_cycles_per_vertex']} cycles a vertex drawn")
    loader = summary["loader"]
    if loader:
        lines.append(f"loader: {loader.get('bytes', 0)} bytes in {loader.get('chunks', 0)} reads and {loader.get('seeks', 0)} seeks, "
                     f"{loader.get('io_us', 0) / 1000:.1f} ms inside them, {loader.get('kib_per_s', 0)} KiB/s")
    lines.append("slowest frames: frame us (gpu, vsync) area at x,y,z yaw")
    for row in summary["worst"]:
        lines.append(f"  {row['frame']:6d} {row['frame_us']:7d} ({row['gpu_us']}, {row['vsync_us']}) area {row['area']} at "
                     f"{row['x']},{row['y']},{row['z']} yaw {row['yaw']}; {row['batches']} batches, {row['uploads']} uploads, "
                     f"{row['stream_misses']} misses")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("log", type=Path)
    parser.add_argument("--compare", type=Path, help="a baseline frame log: each stage's mean against it")
    parser.add_argument("--worst", type=int, default=8)
    parser.add_argument("--json", type=Path, help="also write the summary here")
    args = parser.parse_args()
    summary = summarize(parse(args.log.read_text(errors="replace")), args.worst)
    other = summarize(parse(args.compare.read_text(errors="replace"))) if args.compare else None
    print(format_summary(summary, other))
    if args.json:
        args.json.write_text(json.dumps(summary, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
