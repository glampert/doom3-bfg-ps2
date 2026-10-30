#!/usr/bin/env python3
# ================================================================================================
# File: link_resident.py
# Brief: Gate a real whole-object campaign link, then emit the runnable from a section-collected link.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""A failed link stays a failed gate. The gate links every object without garbage collection or
unresolved-symbol suppression; only after it passes is the runnable linked again with --gc-sections."""

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


# .pdr (MIPS procedure descriptors) and .comment are never loaded; the unstripped ELF keeps them.
STRIP_FLAGS = ("--strip-all", "-R", ".pdr", "-R", ".comment")

REGISTRATIONS = (
    "GetGameAPI", "gameLocal", "game", "gameEdit", "idCVar::staticVars",
    "idClass::Type", "idEntity::Type", "idPlayer::Type",
    "declManager", "collisionModelManager", "AASFileManager",
)
# Section collection must keep every statically registered root. GetGameAPI is an entry point
# that only production calls, so collection rightly drops it from test images.
COLLECTED_REGISTRATIONS = tuple(name for name in REGISTRATIONS if name != "GetGameAPI")


def category(symbol: str) -> str:
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


def registered(prefix: str, elf: Path, roots: tuple[str, ...]) -> tuple[dict, str]:
    symbols = subprocess.check_output([prefix + "nm", "-g", "--defined-only", "-C", str(elf)], text=True)
    names = {line.split(maxsplit=2)[-1] for line in symbols.splitlines() if len(line.split(maxsplit=2)) == 3}
    return {name: name in names for name in roots}, symbols


def executable(elf: Path) -> bool:
    with elf.open("rb") as stream:
        header = stream.read(52)
    return struct.unpack_from("<H", header, 16)[0] == 2 and bool(load_segments(elf))


def run(args: argparse.Namespace) -> int:
    args.output.mkdir(parents=True, exist_ok=True)
    name = getattr(args, "name", "d3bfg")
    kind = getattr(args, "kind", "game")
    if kind not in ("game", "tests") or name != {"game": "d3bfg", "tests": "d3bfg_tests"}[kind]:
        raise ValueError("invalid resident executable name")
    elf = args.output / (name + "_unstripped.elf")
    candidate = elf.with_suffix(".elf.tmp")
    collected = elf.with_suffix(".elf.gc.tmp")
    runnable = args.output / (name + ".elf")
    for path in (elf, candidate, collected, runnable, args.output / "report.json", args.output / "report.txt"):
        path.unlink(missing_ok=True)
    if len(args.sources) != len(args.objects) or not args.objects:
        raise ValueError("one explicit source is required for each resident object")
    if len(set(args.objects)) != len(args.objects) or len(set(args.sources)) != len(args.sources):
        raise ValueError("duplicate resident source/object")
    if any("doomclassic" in str(path).casefold() for path in args.sources + args.objects):
        raise ValueError("Classic source/object in resident link")
    if kind == "game" and any("tests" in Path(path).parts for path in args.sources + args.objects):
        raise ValueError("test source/object in production link")
    for path in args.objects:
        if not Path(path).is_file():
            raise ValueError(f"missing resident object: {path}")
    response = args.output / "objects.rsp"
    response.write_text("\n".join(shlex.quote(path) for path in args.objects) + "\n")
    gate_map = args.output / (name + "_gate.map")
    link_map = args.output / (name + ".map")
    gate_map.unlink(missing_ok=True)
    link_map.unlink(missing_ok=True)
    libraries = getattr(args, "libraries", ("kernel", "m", "patches"))
    if (not libraries or len(set(libraries)) != len(libraries) or
            any(name not in ("draw", "graph", "packet2", "dma", "pad", "kbd", "kernel", "m", "patches") for name in libraries) or
            not {"kernel", "m", "patches"}.issubset(libraries)):
        raise ValueError("invalid resident SDK library list")

    def link_command(output: Path, map_path: Path, *extra: str) -> list[str]:
        return [args.compiler, "-o", str(output), "@" + str(response),
                "-T" + str(args.linkfile), "-L" + str(args.sdk_lib), "-Wl,-zmax-page-size=128",
                "-Wl,--demangle=gnu-v3", "-Wl,-Map," + str(map_path), *extra, *["-l" + name for name in libraries]]

    gate_command = link_command(candidate, gate_map)
    command = link_command(collected, link_map, "-Wl,--gc-sections")
    result = subprocess.run(gate_command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
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
        "schema_version": 1, "configuration": args.configuration, "milestone": "resident-link", "kind": kind,
        "linker_returncode": result.returncode, "passed": False, "command": command,
        "gate_command": gate_command, "gate_garbage_collection": False, "runnable_linker_returncode": None,
        "object_count": len(args.objects), "garbage_collection": True,
        "inputs": [{"source": source, "object": obj,
                    "sha256": hashlib.sha256(Path(obj).read_bytes()).hexdigest()}
                   for source, obj in zip(args.sources, args.objects)],
        "unresolved": unresolved, "duplicate_definitions": duplicates,
        "categories": dict(sorted(Counter(item["category"] for item in unresolved).items())),
        "registrations": None, "runnable_registrations": None, "gate_loaded_memory_bytes": None,
        "loaded_memory_bytes": None,
        "flag_stamps": {path: json.loads(Path(path).read_text()) for path in getattr(args, "flags", [])},
        "source_identity": source_identity(args.sources, [str(Path(p).with_suffix(".d")) for p in args.objects],
                                           ["Makefile", "config/sources.mk", "config/source_inventory.json",
                                            "src/tools/scripts/link_resident.py"]),
    }
    if kind == "game":
        data["test_inputs"] = [path for path in data["source_identity"].get("project_input_sha256", {})
                               if "tests" in Path(path).parts]
    if result.returncode == 0 and not unresolved and not duplicates:
        # A resident gate must retain the actual game hierarchy and registration roots.
        try:
            data["registrations"], symbols = registered(prefix, candidate, REGISTRATIONS)
            map_text = gate_map.read_text()
            data["missing_map_objects"] = [obj for obj in args.objects if obj not in map_text]
            data["executable"] = executable(candidate)
            data["gate_loaded_memory_bytes"] = sum(segment["memory_bytes"] for segment in load_segments(candidate))
            data["gate_link_map_sha256"] = hashlib.sha256(gate_map.read_bytes()).hexdigest()
            data["test_symbols"] = [line for line in symbols.splitlines()
                                    if any(token in line for token in ("ps2::tests::", "HeadlessFixture"))]
            isolated = kind != "game" or (not data["test_inputs"] and not data["test_symbols"])
            gated = (data["executable"] and all(data["registrations"].values()) and
                     not data["missing_map_objects"] and isolated)
            if gated:
                # Collection runs only after the gate resolved every object, so it cannot hide undefined references.
                collection = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                            check=False)
                (args.output / "link_gc.log").write_text(collection.stdout)
                data["runnable_linker_returncode"] = collection.returncode
                if collection.returncode == 0 and diagnostics(collection.stdout) == ([], []):
                    data["runnable_registrations"], _ = registered(prefix, collected, COLLECTED_REGISTRATIONS)
                    data["loaded_memory_bytes"] = sum(segment["memory_bytes"] for segment in load_segments(collected))
                    data["passed"] = executable(collected) and all(data["runnable_registrations"].values())
            if data["passed"]:
                subprocess.run([prefix + "strip", *STRIP_FLAGS, "-o", str(runnable), str(collected)], check=True)
                collected.replace(elf)
                data["elf_sha256"] = hashlib.sha256(elf.read_bytes()).hexdigest()
                data["runnable_elf_sha256"] = hashlib.sha256(runnable.read_bytes()).hexdigest()
                data["link_map_sha256"] = hashlib.sha256(link_map.read_bytes()).hexdigest()
        except (OSError, ValueError, subprocess.SubprocessError, struct.error) as error:
            data["passed"] = False
            data["verification_error"] = str(error)
            runnable.unlink(missing_ok=True)
    candidate.unlink(missing_ok=True)
    collected.unlink(missing_ok=True)
    write_if_changed(args.output / "report.json", json.dumps(data, indent=2) + "\n")
    summary = [f"Resident campaign link: {'PASS' if data['passed'] else 'FAIL'} ({args.configuration})",
               f"Objects: {len(args.objects)}; gate linker exit: {result.returncode} (no GC); "
               f"runnable linker exit: {data['runnable_linker_returncode']} (--gc-sections)",
               f"PT_LOAD bytes: gate {data['gate_loaded_memory_bytes']}, runnable {data['loaded_memory_bytes']}",
               f"Unresolved symbols: {len(unresolved)}; duplicate definitions: {len(duplicates)}"]
    summary += [f"  {name}: {count}" for name, count in data["categories"].items()]
    summary += ["", *[f"{item['category']}: {item['symbol']}\n  " + ", ".join(item["requestors"])
                      for item in unresolved]]
    write_if_changed(args.output / "report.txt", "\n".join(summary) + "\n")
    print("\n".join(summary[:4]))
    print(f"Evidence: {args.output / 'report.json'}; {args.output / 'link.log'}")
    return 0 if data["passed"] else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--configuration", choices=("debug", "release"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--kind", choices=("game", "tests"), default="game")
    parser.add_argument("--name", choices=("d3bfg", "d3bfg_tests"), default="d3bfg")
    parser.add_argument("--linkfile", type=Path, required=True)
    parser.add_argument("--sdk-lib", type=Path, required=True)
    parser.add_argument("--libraries", nargs="+", default=("kernel", "m", "patches"),
                        help="explicit audited SDK libraries, in link order")
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
