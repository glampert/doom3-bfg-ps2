#!/usr/bin/env python3
# ================================================================================================
# File: stage_headless.py
# Brief: Package production with authored startup data and an explicit headless or visible console policy.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import argparse
import hashlib
import json
from pathlib import Path
import shutil

REPO_ROOT = Path(__file__).resolve().parents[3]
BOOT_ROOT = REPO_ROOT / "src/tests/data/headless"
UI_ROOT = REPO_ROOT / "src/tests/data/ui"
PRESENTATIONS = ("headless", "ntsc", "pal")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def boot_data(output: Path) -> dict[str, str]:
    paths = sorted(path for path in (output / "base").rglob("*") if path.is_file())
    return {str(path.relative_to(output)): sha256(path) for path in paths + [output / "presentation.cfg"]}


def stage(build: str, output: Path, presentation: str = "headless", include_ui: bool = False) -> Path:
    if presentation not in PRESENTATIONS:
        raise ValueError("presentation must be headless, ntsc or pal")
    source = REPO_ROOT / "build" / build
    report = json.loads((source / "report.json").read_text())
    if not report["passed"] or report["kind"] != "game" or report["test_inputs"] or report["test_symbols"]:
        raise ValueError("headless staging requires a passed production link without test inputs")
    for name, key in (("d3bfg.elf", "runnable_elf_sha256"), ("d3bfg_unstripped.elf", "elf_sha256"), ("d3bfg.map", "link_map_sha256")):
        if sha256(source / name) != report[key]:
            raise ValueError(f"production report does not match {name}")
    for name, expected in report["flag_stamps"].items():
        if json.loads((REPO_ROOT / name).read_text()) != expected:
            raise ValueError(f"production report does not match {name}; rebuild before staging")
    ui_files = sorted(path for path in (UI_ROOT / "base").rglob("*") if path.is_file()) if include_ui else []
    if include_ui and not ui_files:
        raise ValueError("UI staging requires authored src/tests/data/ui/base data")
    output = output.resolve()
    if output == source.resolve() or any(output.is_relative_to(root.resolve()) or root.resolve().is_relative_to(output) for root in (BOOT_ROOT, UI_ROOT)):
        raise ValueError("headless staging must use a separate package directory")
    config = output / "base/d3bfg.cfg"
    config_existed = config.exists()
    output.mkdir(parents=True, exist_ok=True)
    for name in ("d3bfg.elf", "d3bfg_unstripped.elf", "d3bfg.map", "report.json", "report.txt", "objects.rsp"):
        shutil.copy2(source / name, output / name)
    for path in sorted(BOOT_ROOT.rglob("*")):
        if not path.is_file():
            continue
        target = output / path.relative_to(BOOT_ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        # Preserve the user's port settings across rebuilds of the runnable package.
        if path.name != "d3bfg.cfg" or not target.exists():
            shutil.copy2(path, target)
    for path in ui_files:
        target = output / path.relative_to(UI_ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
    if include_ui and presentation != "headless" and not config_existed:
        text = config.read_text()
        if "ps2_ui load" not in (line.strip() for line in text.splitlines()):
            config.write_text(text + ("" if text.endswith("\n") else "\n") + "ps2_ui load\n")
    # Always replace startup policy, including when reusing a formerly visible package for headless.
    (output / "presentation.cfg").write_text(presentation + "\n")
    (output / "boot-data.json").write_text(json.dumps(boot_data(output), indent=2) + "\n")
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", choices=("debug", "release"), default="debug")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--presentation", choices=PRESENTATIONS, default="headless")
    parser.add_argument("--ui", action="store_true", help="include the authored native SWF and font data")
    args = parser.parse_args()
    try:
        package = "ui" if args.ui else "headless" if args.presentation == "headless" else "console"
        output = stage(args.build, args.output or REPO_ROOT / "build" / args.build / package, args.presentation, args.ui)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"error: {error}\n")
    print(f"Production {args.presentation} package: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
