#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the explicit EE manifests against every shipped BFG source unit."""

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


PROJECTS = ("idlib.vcxproj", "doomexe.vcxproj", "game-d3xp.vcxproj", "external.vcxproj")
SOURCE_SUFFIXES = {".c", ".cpp", ".cc"}
EE_LISTS = ("PS2_CXX_SRC", "CORE_CXX_SRC", "CORE_FRAMEWORK_CXX_SRC", "CORE_BACKEND_CXX_SRC", "CORE_C_SRC", "CORE_BOOT_CXX_SRC",
            "CAMPAIGN_CXX_SRC", "VENDOR_C_SRC", "VENDOR_CXX_SRC")


def read_make_lists(path: Path) -> dict[str, list[str]]:
    text = path.read_text(encoding="utf-8").replace("\\\n", " ")
    result = {}
    for name in EE_LISTS:
        match = re.search(r"^" + name + r"[ \t]*=[ \t]*(.*)$", text, re.MULTILINE)
        if not match:
            raise ValueError(f"{path}: missing explicit {name}")
        result[name] = match.group(1).split("#", 1)[0].split()
    return result


def audit(root: Path) -> tuple[dict, list[str]]:
    inventory = json.loads((root / "config/source_inventory.json").read_text(encoding="utf-8"))
    lists = read_make_lists(root / "config/sources.mk")
    neo = root / "src/neo"
    actual = {"neo/" + str(path.relative_to(neo)) for path in neo.rglob("*")
              if path.is_file() and path.suffix in SOURCE_SUFFIXES}
    canonical = {path.casefold(): path for path in actual}
    errors = []
    if len(actual) != len(canonical):
        errors.append("Case-colliding source paths cannot be reproduced on every host")
    dispositions = {}
    for name, category in inventory["categories"].items():
        for source in category["sources"]:
            if source in dispositions:
                errors.append(f"Duplicate inventory entry: {source}")
            dispositions[source] = name
    for source in sorted(actual - dispositions.keys()):
        errors.append(f"Unclassified shipped source: {source}")
    for source in sorted(dispositions.keys() - actual):
        errors.append(f"Inventory source no longer exists: {source}")

    project_counts = {}
    ns = {"ms": "http://schemas.microsoft.com/developer/msbuild/2003"}
    for project in PROJECTS:
        units = []
        for node in ET.parse(neo / project).findall(".//ms:ClCompile", ns):
            if "Include" not in node.attrib:
                continue
            source = "neo/" + node.attrib["Include"].replace("\\", "/")
            actual_source = canonical.get(source.casefold())
            if actual_source is None:
                errors.append(f"{project} names missing source: {source}")
            else:
                units.append(actual_source)
                if actual_source not in dispositions:
                    errors.append(f"{project} unit has no disposition: {actual_source}")
        project_counts[project] = len(units)
    if project_counts != inventory["upstream_projects"]:
        errors.append("Upstream project counts changed; review and update the source inventory")

    for name, sources in lists.items():
        if len(sources) != len(set(sources)):
            errors.append(f"{name} contains duplicate source entries")
        for source in sources:
            if "$" in source or "*" in source:
                errors.append(f"{name} must use explicit source paths: {source}")
            if "doomclassic" in source.casefold():
                errors.append(f"Classic Doom source present in {name}: {source}")
            if not (root / "src" / source).is_file():
                errors.append(f"{name} source missing: {source}")
            if source.startswith("neo/") and dispositions.get(source) != "campaign_runtime":
                errors.append(f"{name} enables an excluded/replaced source: {source}")
    intended = set(inventory["categories"]["campaign_runtime"]["sources"])
    if set(lists["CAMPAIGN_CXX_SRC"]) != intended:
        errors.append("CAMPAIGN_CXX_SRC differs from the reviewed campaign disposition")
    classic_count = sum(1 for path in (root / "doomclassic").rglob("*")
                        if path.is_file() and path.suffix in SOURCE_SUFFIXES)
    summary = {
        "upstream_projects": project_counts,
        "source_count": len(actual),
        "categories": dict(Counter(dispositions.values())),
        "target_lists": {name: len(sources) for name, sources in lists.items()},
        "classic_units_excluded": classic_count,
    }
    return summary, errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--json", action="store_true", help="Print the audited counts as JSON")
    args = parser.parse_args()
    try:
        summary, errors = audit(args.root.resolve())
    except (OSError, ValueError, ET.ParseError, KeyError) as error:
        print(f"source audit: {error}", file=sys.stderr)
        return 1
    for error in errors:
        print(f"source audit: {error}", file=sys.stderr)
    if errors:
        return 1
    if args.json:
        print(json.dumps(summary, indent=2))
    else:
        print(f"source audit: {summary['source_count']} classified BFG units; "
              f"{summary['classic_units_excluded']} Classic Doom units excluded")
        print("target lists: " + ", ".join(f"{key}={value}" for key, value in summary["target_lists"].items()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
