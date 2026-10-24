#!/usr/bin/env python3
# ================================================================================================
# File: stage_headless.py
# Brief: Package production with authored or retail startup data and an explicit headless or visible console policy.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil

REPO_ROOT = Path(__file__).resolve().parents[3]
BOOT_ROOT = REPO_ROOT / "src/tests/data/headless"
UI_ROOT = REPO_ROOT / "src/tests/data/ui"
RETAIL_ROOT = REPO_ROOT / "src/tests/data/retail_boot"
PRESENTATIONS = ("headless", "ntsc", "pal")
# The debug keyboard's IOP modules, which a visible package carries beside the ELF; the pad uses
# the ROM's. The SDK's own copies, so the package never carries a module from the repository.
IRX_ROOT = Path(os.environ.get("PS2SDK", str(Path.home() / "ps2dev/ps2sdk"))) / "iop/irx"
KEYBOARD_IRX = ("usbd.irx", "ps2kbd.irx")
# The retail declarations, scripts and English strings; sound and per-map containers stay out.
RETAIL_CONTAINERS = ("_common.resources", "_ordered.resources")
RESOURCE_MAGIC = bytes.fromhex("d000000d")


def sha256(path: Path) -> str:
    # Streamed: retail packages hash 90 MB of containers.
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def boot_data(output: Path) -> dict[str, str]:
    paths = sorted(path for root in ("base", "irx") for path in (output / root).rglob("*") if path.is_file())
    return {str(path.relative_to(output)): sha256(path) for path in paths + [output / "presentation.cfg"]}


def link_containers(data_root: Path, base: Path) -> None:
    """Hard-link (or copy) the retail containers; the native directory listing skips symlinks."""
    for name in RETAIL_CONTAINERS:
        source = (data_root / name).resolve()
        with source.open("rb") as stream:
            if stream.read(4) != RESOURCE_MAGIC:
                raise ValueError(f"{source} is not a Doom 3 BFG resource container")
        target = base / name
        if target.is_symlink() or (target.exists() and not target.samefile(source)):
            target.unlink()
        if target.exists():
            continue
        try:
            os.link(source, target)
        except OSError:
            shutil.copy2(source, target)


def stage_keyboard_modules(output: Path) -> None:
    """Copy the keyboard driver's modules into the package's irx directory, replacing any other file there."""
    for name in KEYBOARD_IRX:
        source = IRX_ROOT / name
        if not source.is_file() or source.is_symlink():
            raise ValueError(f"the keyboard driver module is missing from the SDK: {source}")
    destination = output / "irx"
    if destination.is_symlink():
        raise ValueError("the package's irx directory must not be a symlink")
    destination.mkdir(parents=True, exist_ok=True)
    for existing in destination.iterdir():
        if existing.name not in KEYBOARD_IRX:
            if existing.is_symlink() or not existing.is_file():
                raise ValueError("the package's irx directory holds only regular module files")
            existing.unlink()
    for name in KEYBOARD_IRX:
        target = destination / name
        if target.is_symlink():
            raise ValueError("a staged module must not be a symlink")
        shutil.copy2(IRX_ROOT / name, target)


def stage(build: str, output: Path, presentation: str = "headless", include_ui: bool = False,
          retail_data: Path | None = None, catalog: Path | None = None, menu: bool = False) -> Path:
    if presentation not in PRESENTATIONS:
        raise ValueError("presentation must be headless, ntsc or pal")
    # The native menu opens at startup only in a visible mode, and only the retail movie package stages it.
    if menu and (presentation == "headless" or include_ui or retail_data is not None):
        raise ValueError("the menu startup policy needs a visible retail movie package")
    if include_ui and retail_data is not None:
        raise ValueError("the authored UI and retail boot packages are separate")
    # A visible presentation opens the cooked retail catalog at startup for the console charset.
    if presentation != "headless" and catalog is None:
        raise ValueError("a visible package needs the cooked retail catalog (make ui-cook); pass its directory")
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
    roots = (BOOT_ROOT, UI_ROOT, RETAIL_ROOT) + ((retail_data,) if retail_data is not None else ())
    if output == source.resolve() or any(output.is_relative_to(root.resolve()) or root.resolve().is_relative_to(output) for root in roots):
        raise ValueError("headless staging must use a separate package directory")
    config = output / "base/d3bfg.cfg"
    config_existed = config.exists()
    output.mkdir(parents=True, exist_ok=True)
    for name in ("d3bfg.elf", "d3bfg_unstripped.elf", "d3bfg.map", "report.json", "report.txt", "objects.rsp"):
        shutil.copy2(source / name, output / name)
    for path in sorted(BOOT_ROOT.rglob("*")):
        # A retail package takes only the configuration; its containers supply the declarations.
        if not path.is_file() or (retail_data is not None and path.parent != BOOT_ROOT / "base"):
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
    if presentation != "headless":
        # Bound at call time: the retail staging module imports this one.
        from stage_retail_ui import stage_catalog
        stage_catalog(Path(catalog), output)
        stage_keyboard_modules(output)
    else:
        shutil.rmtree(output / "irx", ignore_errors=True)
    if retail_data is not None:
        for path in sorted(path for path in RETAIL_ROOT.rglob("*") if path.is_file()):
            target = output / path.relative_to(RETAIL_ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
        link_containers(retail_data, output / "base")
    if include_ui and presentation != "headless" and not config_existed:
        text = config.read_text()
        if "ps2_ui load" not in (line.strip() for line in text.splitlines()):
            config.write_text(text + ("" if text.endswith("\n") else "\n") + "ps2_ui load\n")
    # Always replace startup policy, including when reusing a formerly visible package for headless.
    (output / "presentation.cfg").write_text(presentation + (" menu" if menu else "") + "\n")
    (output / "boot-data.json").write_text(json.dumps(boot_data(output), indent=2) + "\n")
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", choices=("debug", "release"), default="debug")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--presentation", choices=PRESENTATIONS, default="headless")
    parser.add_argument("--ui", action="store_true", help="include the authored native SWF and font data")
    parser.add_argument("--retail-data", type=Path, help="boot from this installation's retail containers")
    parser.add_argument("--catalog", type=Path, default=REPO_ROOT / "build/ui-retail",
                        help="the cooked retail UI (make ui-cook) a visible package draws the console charset from")
    args = parser.parse_args()
    try:
        package = ("retail-boot" if args.retail_data else "ui" if args.ui else
                   "headless" if args.presentation == "headless" else "console")
        output = stage(args.build, args.output or REPO_ROOT / "build" / args.build / package, args.presentation, args.ui,
                       args.retail_data, args.catalog if args.presentation != "headless" else None)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"error: {error}\n")
    print(f"Production {args.presentation} package: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
