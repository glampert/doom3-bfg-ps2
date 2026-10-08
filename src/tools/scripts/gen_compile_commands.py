#!/usr/bin/env python3
# This source code is released under the GNU GPL-3.0-or-later license.
"""Generate a compile database from a successful EE Makefile dry run.

    make compiledb
    make -Bnk all compile-core compile-game | python3 src/tools/scripts/gen_compile_commands.py --stdin

The subprocess mode checks Make's exit status and preserves an existing database
on failure. Entries use argument arrays so paths and quoting remain exact.
"""

import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys


def extract_commands(transcript: str, root: Path) -> list[dict]:
    entries = []
    seen = set()
    for line in transcript.splitlines():
        if " -c " not in line or "mips64r5900el-ps2-elf-g" not in line:
            continue
        try:
            tokens = shlex.split(line.strip())
        except ValueError as error:
            raise ValueError(f"Malformed compiler command: {error}") from error
        if "-c" not in tokens or "-o" not in tokens:
            raise ValueError("Compiler command lacks a source or object output")
        source = tokens[tokens.index("-c") + 1]
        output = tokens[tokens.index("-o") + 1]
        if not source.endswith((".c", ".cpp", ".cc")):
            continue
        if source in seen:
            continue
        seen.add(source)
        entries.append({"directory": str(root), "arguments": tokens,
                        "file": str((root / source).resolve()),
                        "output": str((root / output).resolve())})
    if not entries:
        raise ValueError("No EE compiler commands were found; existing database preserved")
    return entries


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", default="make")
    parser.add_argument("--build", choices=("debug", "release"), default="debug")
    parser.add_argument("--stdin", action="store_true")
    parser.add_argument("--output", type=Path, default=Path("compile_commands.json"))
    args = parser.parse_args()
    root = Path.cwd()
    try:
        if args.stdin:
            transcript = sys.stdin.read()
        else:
            result = subprocess.run([args.make, "--no-print-directory", "-Bnk",
                                     f"BUILD={args.build}", "all", "compile-core", "compile-game"],
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            if result.returncode:
                sys.stderr.write(result.stderr)
                raise ValueError(f"Make dry run failed with status {result.returncode}; existing database preserved")
            transcript = result.stdout
        entries = extract_commands(transcript, root)
        content = json.dumps(entries, indent=2) + "\n"
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != content:
            temporary = args.output.with_suffix(args.output.suffix + ".tmp")
            temporary.write_text(content, encoding="utf-8")
            os.replace(temporary, args.output)
    except (OSError, ValueError, IndexError) as error:
        print(f"gen_compile_commands: {error}", file=sys.stderr)
        return 1
    print(f"gen_compile_commands: wrote {len(entries)} entries", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
