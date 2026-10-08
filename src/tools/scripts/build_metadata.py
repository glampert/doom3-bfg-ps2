#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write stable compiler/flag stamps and reproducible EE artifact size reports."""

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import struct
import subprocess
import sys


def command_output(command: list[str]) -> str:
    return subprocess.check_output(command, text=True).strip()


def write_if_changed(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8")


def stamp(output: Path, tokens: list[str]) -> None:
    if not tokens:
        raise ValueError("A compiler and its flags are required")
    data = {"compiler": tokens[0], "compiler_version": command_output([tokens[0], "--version"]),
            "tokens": tokens[1:]}
    write_if_changed(output, json.dumps(data, indent=2) + "\n")


def load_segments(path: Path) -> list[dict]:
    with path.open("rb") as stream:
        header = stream.read(52)
        if header[:6] != b"\x7fELF\x01\x01":
            raise ValueError(f"{path}: expected little-endian ELF32")
        fields = struct.unpack("<16sHHIIIIIHHHHHH", header)
        if fields[2] != 8:
            raise ValueError(f"{path}: expected MIPS ELF machine")
        offset, entry_size, count = fields[5], fields[9], fields[10]
        result = []
        for index in range(count):
            stream.seek(offset + index * entry_size)
            program = struct.unpack("<IIIIIIII", stream.read(32))
            if program[0] == 1:
                result.append({"virtual_address": program[2], "file_bytes": program[4],
                               "memory_bytes": program[5], "flags": program[6]})
        return result


def source_identity(sources: list[str], dependencies: list[str], policies: list[str]) -> dict:
    root = Path.cwd().resolve()
    inputs = set(sources + policies)
    for dependency in dependencies:
        # The first logical Make rule lists real inputs; subsequent -MP rules
        # are empty header targets. MD also tracks upstream/vendor bridge headers;
        # inputs outside this checkout are excluded from the project hash set below.
        rule = Path(dependency).read_text(encoding="utf-8").replace("\\\n", " ").splitlines()[0]
        inputs.update(shlex.split(rule.partition(":")[2]))
    hashes = {}
    for input_name in sorted(inputs):
        path = Path(input_name).resolve()
        if path.is_relative_to(root):
            hashes[str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
    try:
        commit = command_output(["git", "rev-parse", "HEAD"])
        dirty = bool(command_output(["git", "status", "--porcelain"]))
        diff = subprocess.check_output(["git", "diff", "--binary", "HEAD", "--"])
        diff_hash = hashlib.sha256(diff).hexdigest()
    except (OSError, subprocess.CalledProcessError):
        commit, dirty, diff_hash = "unknown", True, None
    return {"source_commit": commit, "source_dirty": dirty, "tracked_diff_sha256": diff_hash,
            "project_input_sha256": hashes}


def report(args: argparse.Namespace) -> None:
    elf = args.elf
    tool_prefix = args.compiler.removesuffix("g++")
    size_text = command_output([tool_prefix + "size", "-A", str(elf)])
    largest = command_output([tool_prefix + "nm", "-S", "--size-sort", "--radix=d", "-C", str(elf)])
    largest = "\n".join(reversed(largest.splitlines()[-40:]))
    segments = load_segments(elf)
    source_hashes = {source: hashlib.sha256(Path(source).read_bytes()).hexdigest()
                     for source in sorted(set(args.sources))}
    flags = {path: json.loads(Path(path).read_text(encoding="utf-8")) for path in args.flags}
    identity = json.loads(args.identity.read_text(encoding="utf-8"))
    source_commit, source_dirty = identity["source_commit"], identity["source_dirty"]
    link_map = elf.with_name("d3bfg.map")
    data = {
        "schema_version": 1,
        "milestone": args.milestone,
        "configuration": args.configuration,
        "elf": str(elf),
        "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
        "runnable_elf_sha256": hashlib.sha256(args.runnable_elf.read_bytes()).hexdigest(),
        "link_map_sha256": hashlib.sha256(link_map.read_bytes()).hexdigest(),
        "source_identity_sha256": hashlib.sha256(args.identity.read_bytes()).hexdigest(),
        "compiler_version": command_output([args.compiler, "--version"]),
        "source_commit": source_commit,
        "source_dirty": source_dirty,
        "source_sha256": source_hashes,
        "project_input_sha256": identity["project_input_sha256"],
        "tracked_diff_sha256": identity["tracked_diff_sha256"],
        "flag_stamps": flags,
        "load_segments": segments,
        "loaded_memory_bytes": sum(segment["memory_bytes"] for segment in segments),
        "section_sizes": size_text,
        "largest_symbols": largest,
    }
    write_if_changed(args.output.with_suffix(".json"), json.dumps(data, indent=2) + "\n")
    text = (f"Doom 3 BFG PS2: {args.milestone} ({args.configuration})\n"
            f"Source commit: {source_commit}; worktree changes: {source_dirty}\n"
            f"ELF SHA-256: {data['elf_sha256']}\n"
            f"PT_LOAD memory total: {data['loaded_memory_bytes']} bytes\n\n"
            f"{size_text}\n\nLargest symbols (size descending):\n{largest}\n")
    write_if_changed(args.output.with_suffix(".txt"), text)
    print(f"build report: {args.milestone}, PT_LOAD {data['loaded_memory_bytes']} bytes; "
          f"{args.output.with_suffix('.json')}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="mode", required=True)
    flags_parser = subparsers.add_parser("stamp")
    flags_parser.add_argument("output", type=Path)
    flags_parser.add_argument("tokens", nargs=argparse.REMAINDER)
    identity_parser = subparsers.add_parser("identity")
    identity_parser.add_argument("--output", type=Path, required=True)
    identity_parser.add_argument("--sources", nargs="+", required=True)
    identity_parser.add_argument("--dependencies", nargs="+", required=True)
    identity_parser.add_argument("--policies", nargs="+", required=True)
    report_parser = subparsers.add_parser("report")
    report_parser.add_argument("--elf", type=Path, required=True)
    report_parser.add_argument("--runnable-elf", type=Path, required=True)
    report_parser.add_argument("--identity", type=Path, required=True)
    report_parser.add_argument("--output", type=Path, required=True)
    report_parser.add_argument("--compiler", required=True)
    report_parser.add_argument("--configuration", required=True)
    report_parser.add_argument("--milestone", required=True)
    report_parser.add_argument("--flags", nargs="+", required=True)
    report_parser.add_argument("--sources", nargs="+", required=True)
    args = parser.parse_args()
    try:
        if args.mode == "stamp":
            stamp(args.output, args.tokens)
        elif args.mode == "identity":
            identity = source_identity(args.sources, args.dependencies, args.policies)
            write_if_changed(args.output, json.dumps(identity, indent=2) + "\n")
        else:
            report(args)
    except (OSError, ValueError, subprocess.CalledProcessError, struct.error) as error:
        print(f"build metadata: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
