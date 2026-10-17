#!/usr/bin/env python3
# ================================================================================================
# File: stage_retail_ui.py
# Brief: Stage an opt-in English font proof from checked local cooked resources and production ELF.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import argparse
import json
from pathlib import Path
import re
import shutil

from stage_headless import REPO_ROOT, boot_data, sha256, stage
from ui_retail_catalog import build_catalog


_DIGEST = re.compile(r"[0-9a-f]{64}\Z")


def _under_build(path: Path) -> Path:
    absolute = path.absolute()
    alias = (REPO_ROOT / "build").absolute()
    root = alias if absolute.is_relative_to(alias) else alias.resolve()
    if (absolute == root or not absolute.is_relative_to(root) or
            not absolute.resolve().is_relative_to(root.resolve())):
        raise ValueError("retail UI proof directories must be descendants of repository build/")
    current = absolute
    while current != root:
        if current.is_symlink():
            raise ValueError("retail UI proof paths must not contain symlinks")
        current = current.parent
    if alias.is_symlink():
        raise ValueError("retail UI proof build root must not be a symlink")
    return absolute.resolve()


def _asset(source: Path, descriptor: dict, suffix: str) -> tuple[Path, dict]:
    digest = descriptor.get("sha256")
    length = descriptor.get("bytes")
    if (not isinstance(digest, str) or not _DIGEST.fullmatch(digest) or
            type(length) is not int or not 1 <= length <= 64 * 1024 * 1024 + 16384):
        raise ValueError("retail UI proof asset identity is invalid")
    relative = "assets/" + digest + suffix
    if descriptor.get("path") != relative:
        raise ValueError("retail UI proof asset is not content-addressed")
    path = source / relative
    if ((source / "assets").is_symlink() or path.is_symlink() or not path.is_file() or
            path.stat().st_size != length or sha256(path) != digest):
        raise ValueError("retail UI proof asset is missing, unsafe or changed")
    return path, {"path": relative, "bytes": length, "sha256": digest}


def inspect_package(source: Path) -> tuple[dict, list[tuple[Path, dict]], Path]:
    source = _under_build(source)
    manifest_path = source / "manifest.json"
    if manifest_path.is_symlink() or not manifest_path.is_file() or manifest_path.stat().st_size > 32 * 1024 * 1024:
        raise ValueError("retail UI proof requires a bounded regular cook manifest")
    manifest = json.loads(manifest_path.read_text())
    cooked = manifest["cooked"]
    descriptor = cooked["runtime_catalog"]
    catalog, _ = _asset(source, descriptor, ".uic")
    expected, summary = build_catalog(manifest, source)
    if catalog.read_bytes() != expected or any(descriptor.get(key) != value for key, value in summary.items()):
        raise ValueError("retail UI proof catalog differs from the checked cook")
    assets = {}
    for name in sorted(cooked["runtime_images"]):
        path, identity = _asset(source, cooked["runtime_images"][name], ".uip")
        assets[path.name] = (path, identity)
    for name in sorted(manifest["fonts"]):
        path, identity = _asset(source, cooked["assets"][name], ".dat")
        assets[path.name] = (path, identity)
    return manifest, list(assets.values()), catalog


def stage_retail(build: str, output: Path, standard: str, source: Path) -> Path:
    if standard not in ("ntsc", "pal"):
        raise ValueError("retail UI proof requires ntsc or pal presentation")
    source, output = _under_build(source), _under_build(output)
    if source == output or source.is_relative_to(output) or output.is_relative_to(source):
        raise ValueError("retail UI proof staging must not overlap the source cook")
    manifest, assets, catalog = inspect_package(source)
    for relative in ("base", "base/retail", "base/retail/assets", "base/retail_ui.uic"):
        if (output / relative).is_symlink():
            raise ValueError("retail UI proof output must not contain symlinks")
    config_existed = (output / "base/d3bfg.cfg").exists()
    output = stage(build, output, standard)
    destination = output / "base/retail/assets"
    destination.mkdir(parents=True, exist_ok=True)
    for path, _ in assets:
        target = destination / path.name
        if target.is_symlink():
            raise ValueError("retail UI proof asset destination must not be a symlink")
        shutil.copy2(path, target)
    shutil.copy2(catalog, output / "base/retail_ui.uic")
    config = output / "base/d3bfg.cfg"
    if not config_existed:
        text = config.read_text()
        config.write_text(text + ("" if text.endswith("\n") else "\n") + "ps2_uiRetail load\n")
    metadata = {"scope": "native English font resources; retail SWF and Shell remain inactive",
                "source_manifest_sha256": sha256(source / "manifest.json"),
                "catalog": manifest["cooked"]["runtime_catalog"],
                "assets": [identity for _, identity in assets],
                "fonts": sorted(manifest["fonts"]), "runtime_ready": False}
    (output / "retail-proof.json").write_text(json.dumps(metadata, indent=2) + "\n")
    (output / "boot-data.json").write_text(json.dumps(boot_data(output), indent=2) + "\n")
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", choices=("debug", "release"), default="debug")
    parser.add_argument("--standard", choices=("ntsc", "pal"), default="ntsc")
    parser.add_argument("--source", type=Path, default=REPO_ROOT / "build/ui-retail")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        output = args.output or REPO_ROOT / "build" / args.build / "ui-retail"
        package = stage_retail(args.build, output, args.standard, args.source)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, "error: " + str(error) + "\n")
    print("Production native English font package: " + str(package))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
