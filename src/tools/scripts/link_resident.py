#!/usr/bin/env python3
# ================================================================================================
# File: link_resident.py
# Brief: Preserve a real whole-object campaign link attempt and its unresolved dependency evidence.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""A failed link stays a failed gate. No garbage collection or unresolved-symbol suppression."""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys

from build_metadata import load_segments, source_identity, write_if_changed


REGISTRATIONS = (
    "GetGameAPI", "gameLocal", "game", "gameEdit", "idCVar::staticVars",
    "idClass::Type", "idEntity::Type", "idPlayer::Type",
    "declManager", "collisionModelManager", "AASFileManager",
)


def category(symbol: str) -> str:
    if symbol.startswith(("jpeg_", "inflate", "deflate", "crc32")):
        return "codecs"
    if symbol.startswith(("ASE_", "MA_", "lwGetObject", "lwFreeObject")):
        return "source-model-import"
    if symbol.startswith(("Sys_", "sys_", "in_", "usercmdGen", "userCmdStrings")):
        return "platform-input"
    if symbol.startswith(("idRender", "idImage", "idVertexCache", "idCinematic", "idResolutionScale",
                          "r_", "R_", "RB_", "render", "globalImages", "vertexCache", "resolutionScale",
                          "tr", "backEnd", "glConfig", "stereoRender_")):
        return "renderer"
    if symbol.startswith(("idMultiplayerGame", "idMenuHandler_Shell", "idMenuScreen_Shell",
                          "vtable for idMenuHandler_Shell", "idSaveGame", "saveGame_", "Leaderboard", "net_")):
        return "deferred-shell-network-save"
    if symbol.startswith("idCommonLocal"):
        return "common"
    if symbol.startswith("idAchievementManager") or symbol == "g_demoMode":
        return "campaign-achievements"
    if symbol.startswith(("idSndWindow", "vtable for idSndWindow")):
        return "deferred-ui"
    return "unclassified"


def diagnostics(log: str) -> tuple[list[dict], list[str]]:
    unresolved: dict[str, set[str]] = {}
    duplicates = set()
    requestor = "unknown"
    for line in log.splitlines():
        obj = re.search(r"(?:^|\s)([^\s:]+\.o):", line)
        if obj:
            requestor = obj.group(1)
        ref = re.search(r"undefined reference to [`‘](.+?)['’]", line)
        if ref:
            unresolved.setdefault(ref.group(1), set()).add(requestor)
        duplicate = re.search(r"multiple definition of [`‘](.+?)['’]", line)
        if duplicate:
            duplicates.add(duplicate.group(1))
    return ([{"symbol": name, "category": category(name), "requestors": sorted(requestors)}
             for name, requestors in sorted(unresolved.items())], sorted(duplicates))


def run(args: argparse.Namespace) -> int:
    args.output.mkdir(parents=True, exist_ok=True)
    elf = args.output / "d3bfg_unstripped.elf"
    candidate = args.output / "d3bfg_unstripped.elf.tmp"
    runnable = args.output / "d3bfg.elf"
    for path in (elf, candidate, runnable, args.output / "report.json", args.output / "report.txt"):
        path.unlink(missing_ok=True)
    if len(args.sources) != len(args.objects) or not args.objects:
        raise ValueError("one explicit source is required for each resident object")
    if len(set(args.objects)) != len(args.objects) or len(set(args.sources)) != len(args.sources):
        raise ValueError("duplicate resident source/object")
    if any("doomclassic" in str(path).casefold() for path in args.sources + args.objects):
        raise ValueError("Classic source/object in resident link")
    for path in args.objects:
        if not Path(path).is_file():
            raise ValueError(f"missing resident object: {path}")
    response = args.output / "objects.rsp"
    response.write_text("\n".join(shlex.quote(path) for path in args.objects) + "\n")
    link_map = args.output / "d3bfg.map"
    link_map.unlink(missing_ok=True)
    command = [args.compiler, "-o", str(candidate), "@" + str(response),
               "-T" + str(args.linkfile), "-L" + str(args.sdk_lib), "-Wl,-zmax-page-size=128",
               "-Wl,--demangle=gnu-v3", "-Wl,-Map," + str(link_map), "-lkernel", "-lm", "-lpatches"]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
    (args.output / "link.log").write_text(result.stdout)
    unresolved, duplicates = diagnostics(result.stdout)
    prefix = args.compiler.removesuffix("g++")
    if unresolved:
        raw_names = [item["symbol"] for item in unresolved]
        names = subprocess.check_output([prefix + "c++filt"], input="\n".join(raw_names) + "\n", text=True).splitlines()
        if len(names) != len(raw_names):
            raise ValueError("demangler did not preserve the unresolved symbol list")
        for item, name in zip(unresolved, names):
            item["mangled"] = item["symbol"]
            item["symbol"] = name
            item["category"] = category(name)
    data = {
        "schema_version": 1, "configuration": args.configuration, "milestone": "resident-link",
        "linker_returncode": result.returncode, "passed": False, "command": command,
        "object_count": len(args.objects), "garbage_collection": False,
        "inputs": [{"source": source, "object": obj,
                    "sha256": hashlib.sha256(Path(obj).read_bytes()).hexdigest()}
                   for source, obj in zip(args.sources, args.objects)],
        "unresolved": unresolved, "duplicate_definitions": duplicates,
        "categories": dict(sorted(Counter(item["category"] for item in unresolved).items())),
        "registrations": None, "loaded_memory_bytes": None,
        "flag_stamps": {path: json.loads(Path(path).read_text()) for path in getattr(args, "flags", [])},
        "source_identity": source_identity(args.sources, [str(Path(p).with_suffix(".d")) for p in args.objects],
                                           ["Makefile", "config/sources.mk", "config/source_inventory.json",
                                            "src/tools/scripts/link_resident.py"]),
    }
    if result.returncode == 0 and not unresolved and not duplicates:
        # A resident gate must retain the actual game hierarchy and registration roots.
        try:
            symbols = subprocess.check_output([prefix + "nm", "-g", "--defined-only", "-C", str(candidate)], text=True)
            names = {line.split(maxsplit=2)[-1] for line in symbols.splitlines() if len(line.split(maxsplit=2)) == 3}
            data["registrations"] = {name: name in names for name in REGISTRATIONS}
            map_text = link_map.read_text()
            data["missing_map_objects"] = [obj for obj in args.objects if obj not in map_text]
            segments = load_segments(candidate)
            with candidate.open("rb") as stream:
                header = stream.read(52)
            data["executable"] = struct.unpack_from("<H", header, 16)[0] == 2 and bool(segments)
            data["loaded_memory_bytes"] = sum(segment["memory_bytes"] for segment in segments)
            data["passed"] = data["executable"] and all(data["registrations"].values()) and not data["missing_map_objects"]
            if data["passed"]:
                subprocess.run([prefix + "strip", "--strip-all", "-o", str(runnable), str(candidate)], check=True)
                candidate.replace(elf)
        except (OSError, ValueError, subprocess.SubprocessError, struct.error) as error:
            data["passed"] = False
            data["verification_error"] = str(error)
            runnable.unlink(missing_ok=True)
    candidate.unlink(missing_ok=True)
    write_if_changed(args.output / "report.json", json.dumps(data, indent=2) + "\n")
    summary = [f"Resident campaign link: {'PASS' if data['passed'] else 'FAIL'} ({args.configuration})",
               f"Objects: {len(args.objects)}; linker exit: {result.returncode}; GC: disabled",
               f"Unresolved symbols: {len(unresolved)}; duplicate definitions: {len(duplicates)}"]
    summary += [f"  {name}: {count}" for name, count in data["categories"].items()]
    summary += ["", *[f"{item['category']}: {item['symbol']}\n  " + ", ".join(item["requestors"])
                      for item in unresolved]]
    write_if_changed(args.output / "report.txt", "\n".join(summary) + "\n")
    print("\n".join(summary[:3]))
    print(f"Evidence: {args.output / 'report.json'}; {args.output / 'link.log'}")
    return 0 if data["passed"] else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--configuration", choices=("debug", "release"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--linkfile", type=Path, required=True)
    parser.add_argument("--sdk-lib", type=Path, required=True)
    parser.add_argument("--sources", nargs="+", required=True)
    parser.add_argument("--objects", nargs="+", required=True)
    parser.add_argument("--flags", nargs="+", required=True)
    args = parser.parse_args()
    try:
        return run(args)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"resident link: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
