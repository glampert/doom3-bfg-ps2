#!/usr/bin/env python3
# ================================================================================================
# File: inventory_retail_ui.py
# Brief: Inventory local retail menu dependencies and atomically publish bounded host cooking candidates.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""Host-only retail UI inventory/cooking. Proprietary outputs stay under build/.

The resulting package is deliberately not admitted by the native authored loader.
Static script evidence is conservative; it cannot establish runtime reachability.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile

from ui_bimage import decode_level, inspect_bimage
from ui_font import inspect_font, inspect_old_font, inspect_language, language_coverage
from ui_material import index_materials, resolve_material, canonical_material_name
from ui_pages import write_pages
from ui_resources import MAX_RESOURCE_BYTES, ResourceCatalog, digest, identity, logical_name
from ui_swf import inspect_swf

ROOT = Path(__file__).resolve().parents[3]
MAX_CONFIG_BYTES = 128 * 1024
MAX_TOTAL_PAGES = 1024
MAX_INSPECTED_RESOURCE_BYTES = 256 * 1024 * 1024
MAX_IMAGE_CANDIDATES = 4096
PIXEL_CACHE_BUDGET = 2 * 1024 * 1024
_BLOCKERS = [
    "Native masks/stencil and their nested/concave composition remain unsupported.",
    "Nonzero additive color and required special blend modes need measured 2D-6 closure.",
    "Cooked pages need page-aware native binding and triangle splitting; dictionary names alone do not page RenderShape.",
    "General retail BSWF/font/material/image admission and bounded target loading are not implemented.",
    "Static AVM1 inventory cannot close dynamic names, callbacks or runtime-generated strings/resources.",
    "Native offline PressStart/Root/Quit-dialog startup, input policy and optional media gating remain pending.",
]


def _bytes(path, max_bytes=MAX_RESOURCE_BYTES):
    before = identity(path)
    if not 0 <= before[1] <= max_bytes:
        raise ValueError("input exceeds bounded read: " + path.name)
    data = path.read_bytes()
    if identity(path) != before or len(data) != before[1]:
        raise ValueError("input changed during inventory: " + path.name)
    return data


def _contained(root, name):
    logical_name(name)  # Validate logical syntax while retaining real filesystem case.
    path = root / name.replace("\\", "/")
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError("source path escapes data root")
    return path


def _validate_output(repo_root, output, data_root):
    repo_root = repo_root.resolve()
    build = repo_root / "build"
    output = output.absolute()
    resolved = output.resolve()
    if resolved == build.resolve() or not resolved.is_relative_to(build.resolve()):
        raise ValueError("retail UI output must be a descendant of repository build/")
    if (resolved.is_relative_to(data_root.resolve()) or data_root.resolve().is_relative_to(resolved)):
        raise ValueError("retail UI output overlaps source data")
    current = output
    while current != current.parent:
        if current.is_symlink():
            raise ValueError("retail UI output components must not be symlinks")
        # macOS may spell the same repository through /var versus /private/var;
        # stop at its actual root rather than rejecting unrelated OS aliases.
        if current.resolve() == repo_root:
            break
        current = current.parent
    if build.is_symlink() or (output / "assets").is_symlink() or (output / "manifest.json").is_symlink():
        raise ValueError("retail UI output assets/build/manifest must not be symlinks")
    return resolved


def _check_input_targets(output, inputs, targets):
    """Keep explicit source files outside publication ownership, including hardlinks."""
    identities = set()
    for path in inputs:
        resolved = path.resolve()
        if (resolved == output or resolved.is_relative_to(output) or output.is_relative_to(resolved) or
                any(parent.resolve() == output for parent in path.absolute().parents)):
            raise ValueError("retail UI output overlaps explicit config/source input")
        info = path.stat()
        if not path.is_file():
            raise ValueError("retail UI explicit input must be a regular file")
        identities.add((info.st_dev, info.st_ino))
    for target in targets:
        if target.exists():
            info = target.stat()
            if (info.st_dev, info.st_ino) in identities:
                raise ValueError("retail UI publication target aliases config/source input")


def _configuration(path):
    raw = _bytes(path, MAX_CONFIG_BYTES)
    try:
        config = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("invalid retail UI configuration") from error
    if not isinstance(config, dict) or type(config.get("schema_version")) is not int or config["schema_version"] != 1:
        raise ValueError("unsupported retail UI configuration schema")
    for key in ("roots", "language_ids", "material_names"):
        values = config.get(key)
        if (not isinstance(values, list) or not 1 <= len(values) <= 4096 or
                any(not isinstance(value, str) or not value or len(value) > 512 for value in values)):
            raise ValueError("invalid retail UI configuration list: " + key)
    if len(config["roots"]) > 16:
        raise ValueError("retail UI root count exceeds 16 bound")
    if not isinstance(config.get("source_language"), str):
        raise ValueError("retail UI source_language is required")
    for name in config["roots"] + [config["source_language"]]:
        logical_name(name)
    page = config.get("page_config", {})
    if (page.get("extent") != 256 or page.get("gutter") != 1 or
            page.get("pixel_cache_budget_bytes") != PIXEL_CACHE_BUDGET or page.get("resampling") != "none"):
        raise ValueError("unsupported retail UI page profile")
    return config, digest(raw)


def _font_family(name):
    clean = name.replace("\\", "/").lower()
    clean = logical_name(clean.replace(" ", "_").replace("-", "_"))
    if "/" in clean:
        raise ValueError("unsupported nested font family")
    return clean


def _resolve_font(name, aliases):
    chain, current = [], name
    while len(chain) < 16:
        # RegisterFont applies this literal, case-sensitive replacement before
        # every constructor/remap, including recursive alias registrations.
        current = current.replace("fonts/", "")
        if current in chain:
            raise ValueError("localization font alias cycle")
        chain.append(current)
        # Native default terminates remapping. Otherwise aliases use the original
        # registered name before spaces/hyphens are normalized by RemapFont.
        if current == "Arial_Narrow":
            break
        target = aliases.get("#font_" + current.casefold())
        if target is None:
            target = aliases.get("#font_*")
            if target is not None and target.casefold() == current.casefold():
                target = None
        if target is not None:
            current = target
            continue
        normalized = current.replace(" ", "_").replace("-", "_")
        if normalized == current:
            break
        current = normalized
    else:
        raise ValueError("localization font alias depth exceeds bound")
    return {"requested": name, "family": _font_family(current), "alias_chain": chain}


def _image_resolution(name, names, font=False):
    source = logical_name(name)
    if source.startswith("generated/images/") and source.endswith(".bimage"):
        expected = source
        candidates = [source] if source in names else []
    else:
        for extension in (".tga", ".png", ".jpg", ".dds"):
            if source.endswith(extension):
                source = source[:-len(extension)]
                break
        prefix = "generated/images/" + source + "#__"
        expected = prefix + ("0400" if font else "0200") + ".bimage"
        candidates = sorted(value for value in names if value.startswith(prefix) and value.endswith(".bimage"))
    return {"logical_image": name, "expected": expected, "candidates": candidates,
            "status": "resolved" if expected in names else ("ambiguous-variants" if len(candidates) > 1 else "missing-expected-variant"),
            "selected": expected if expected in names else None}


def _asset(stage, data, extension):
    relative = "assets/" + digest(data) + extension
    path = stage / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        if path.read_bytes() != data:
            raise ValueError("content-addressed asset collision")
    else:
        if path.write_bytes(data) != len(data):
            raise OSError("short retail UI asset write")
    return {"path": relative, "bytes": len(data), "sha256": digest(data)}


def _publish(stage, output, manifest, protected_inputs):
    # Preflight every destination before acquiring any published asset ownership.
    # A hardlinked manifest could otherwise replace an explicit input inode, and
    # content targets must likewise remain independent of config/source files.
    staged_assets = sorted((stage / "assets").iterdir()) if (stage / "assets").exists() else []
    _check_input_targets(output, protected_inputs,
                         [output / "manifest.json"] + [output / "assets" / path.name for path in staged_assets])
    output.mkdir(parents=True, exist_ok=True)
    destination = output / "assets"
    if destination.is_symlink():
        raise ValueError("retail UI asset directory must not be a symlink")
    if staged_assets:
        destination.mkdir(exist_ok=True)
        for source in staged_assets:
            target = destination / source.name
            if target.is_symlink():
                raise ValueError("retail UI asset output must not be a symlink")
            try:
                os.link(source, target)
            except FileExistsError:
                if target.stat().st_size != source.stat().st_size or target.read_bytes() != source.read_bytes():
                    raise ValueError("existing immutable retail UI asset differs")
    raw = (json.dumps(manifest, sort_keys=True, indent=2, ensure_ascii=True) + "\n").encode("ascii")
    (stage / "manifest.json").write_bytes(raw)
    os.replace(stage / "manifest.json", output / "manifest.json")


def build_inventory(data_root, output, config_path, cook=False, *, repo_root=ROOT):
    """Inspect all inputs before staging; publish manifest only after complete cooking."""
    data_root, repo_root = Path(data_root).resolve(), Path(repo_root).resolve()
    output = _validate_output(repo_root, Path(output), data_root)
    config_path = Path(config_path)
    protected_inputs = [config_path]
    _check_input_targets(output, protected_inputs, [output / "manifest.json"])
    config, config_hash = _configuration(config_path)
    protected_inputs.extend(_contained(repo_root, name) for name in config.get("source_contracts", []))
    _check_input_targets(output, protected_inputs, [output / "manifest.json"])
    catalog = ResourceCatalog([data_root / "_common.resources", data_root / "_ordered.resources"])
    names = set(catalog.names())
    inputs, retained = {}, {}
    inspected_bytes = 0

    def resource(name):
        nonlocal inspected_bytes
        name = logical_name(name)
        data = catalog.read(name)
        if name not in inputs:
            inspected_bytes += len(data)
            if inspected_bytes > MAX_INSPECTED_RESOURCE_BYTES:
                raise ValueError("retail UI inspected resource bytes exceed 256 MiB bound")
        inputs[name] = catalog.describe(name)
        return data

    language_name = logical_name(config["source_language"])
    if language_name in names:
        language_raw = resource(language_name)
    else:
        language_raw = _bytes(_contained(data_root, language_name), 16 * 1024 * 1024)
        inputs[language_name] = {"name": language_name, "source": "loose", "bytes": len(language_raw), "sha256": digest(language_raw)}
    language = inspect_language(language_raw)
    safe_language = {key: value for key, value in language.items() if key != "entries"}
    swfs, font_names, language_ids, literal_codepoints = {}, set(), set(config["language_ids"]), set()
    image_requests = {(name, False) for name in config["material_names"] if name.lower() not in ("_white", "guisolid")}
    for name in sorted(set(map(logical_name, config["roots"]))):
        data = resource(name)
        swfs[name] = inspect_swf(data)
        retained[name] = data
        dependencies = swfs[name]["dependencies"]
        font_names.update(dependencies["fonts"])
        language_ids.update(dependencies["localization_ids"])
        literal_codepoints.update(swfs[name]["text"]["literal_codepoints"])
        image_requests.add((name[:-5] + ".tga", False))
        image_requests.update((value, False) for value in dependencies["images"])
    aliases = language["font_aliases"]
    resolutions = [_resolve_font(name, aliases) for name in sorted(font_names | set(aliases.values()))]
    families = sorted({record["family"] for record in resolutions})
    if len(families) > 128:
        raise ValueError("retail font family count exceeds bound")
    font_details, issues = {}, []
    for family in families:
        name = "newfonts/" + family + "/48.dat"
        if name not in names:
            issues.append({"kind": "missing-font", "name": name})
            continue
        data = resource(name)
        font_details[name] = inspect_font(data)
        legacy = []
        for point_size, scale in ((12, 4), (24, 2), (48, 1)):
            old_name = "newfonts/" + family + "/old_" + str(point_size) + ".dat"
            if old_name in names:
                old_data = resource(old_name)
                old = inspect_old_font(old_data)
                retained[old_name] = old_data
                legacy.append({"point_size": point_size, "source": old_name, "scale": scale,
                               "sha256": old["sha256"], "bytes": old["bytes"],
                               "scaled_max_advance": old["max_advance"] * scale,
                               "scaled_max_height": old["max_height"] * scale,
                               "policy": "Preserve native legacy layout metrics independently of modern/subset glyph extrema."})
            else:
                legacy.append({"point_size": point_size, "source": None,
                               "scaled_max_advance": font_details[name]["max_advance"],
                               "scaled_max_height": font_details[name]["max_height"],
                               "policy": "Native missing-legacy fallback uses full modern-font extrema without scale."})
        font_details[name]["legacy_layout_metrics"] = legacy
        retained[name] = data
        image_requests.add((name[:-4] + ".tga", True))
    material_requests = {name for name, _ in image_requests} | set(config["material_names"])
    definitions = {}
    material_files = sorted(name for name in names if name.startswith("materials/") and name.endswith(".mtr"))
    if len(material_files) > 512:
        raise ValueError("retail material file count exceeds bound")
    for name in material_files:
        for canonical, summary in index_materials(resource(name)).items():
            definitions.setdefault(canonical, []).append({"source": name, **summary})
    materials = {}
    for name in sorted(material_requests):
        canonical = canonical_material_name(name)
        candidates = definitions.get(canonical, [])
        hashes = {value["sha256"] for value in candidates}
        for value in candidates:
            hashes.update(duplicate["sha256"] for duplicate in value["duplicate_definitions"])
        if len(hashes) > 1:
            materials[name] = {"name": canonical, "definition": "conflicting-declarations", "candidates": candidates}
            issues.append({"kind": "material-definition-conflict", "name": name})
            continue
        summary = candidates[0] if candidates else resolve_material({}, name)
        materials[name] = {**summary, "same_definition_sources": [value["source"] for value in candidates]}
        if summary.get("unresolved_resources"):
            issues.append({"kind": "material-resource-unresolved", "name": name,
                           "resources": summary["unresolved_resources"]})
        for value in summary["map_resources"]:
            if value.lower() not in ("_white", "_black", "_default") and not value.startswith("_"):
                image_requests.add((value, value.lower().startswith("newfonts/")))
    if len(image_requests) > MAX_IMAGE_CANDIDATES:
        raise ValueError("retail UI image candidate count exceeds 4096 bound")
    image_resolutions = [_image_resolution(name, names, font) for name, font in sorted(image_requests)]
    image_details = {}
    for record in image_resolutions:
        if record["status"] != "resolved":
            issues.append({"kind": "image-resolution", **record})
            continue
        name = record["selected"]
        if name not in image_details:
            image_details[name] = inspect_bimage(resource(name))
    for name, details in font_details.items():
        expected = _image_resolution(name[:-4] + ".tga", names, True)["selected"]
        if expected in image_details:
            atlas = image_details[expected]
            details.update(inspect_font(retained[name], atlas_width=atlas["width"], atlas_height=atlas["height"]))
        coverage = language_coverage(language, language_ids, details["codepoints"])
        details["selected_language_coverage"] = coverage
        if coverage["missing_glyphs"]:
            issues.append({"kind": "selected-language-glyphs-missing", "name": name,
                           "codepoints": coverage["missing_glyphs"]})
        details["literal_codepoint_coverage"] = {"requested_count": len(literal_codepoints),
                                                "missing_glyphs": sorted(literal_codepoints - set(details["codepoints"]))}
    coverage = language_coverage(language, language_ids)
    for record in coverage["records"]:
        if not record["resolved"]:
            issues.append({"kind": "localization-id-unresolved", "name": record["key"],
                           "reason": record["reason"], "chain": record["chain"]})
    path_contracts = {}
    for root_name, contracts in config.get("path_contracts", {}).items():
        swf_name = "generated/swf/" + root_name + ".bswf"
        paths = {value["path"].replace("_root.", "", 1).replace(".", "/") for value in swfs.get(swf_name, {}).get("instance_paths", [])}
        path_contracts[root_name] = [{"path": value, "present_in_static_expansion": value in paths} for value in contracts]
    source_contracts = []
    for name in config.get("source_contracts", []):
        source = _contained(repo_root, name)
        data = _bytes(source)
        source_contracts.append({"name": name, "bytes": len(data), "sha256": digest(data)})
    manifest = {"schema_version": 1, "pipeline": "retail-ui-host-candidates", "mode": "cook" if cook else "inventory",
                "runtime_ready": False, "dynamic_dependency_closure": False,
                "scope": config.get("scope"), "config_sha256": config_hash,
                "containers": [{"name": value.path.name, "sha256": value.sha256,
                                "bytes": value.identity[1], "entries": len(value.entries)} for value in catalog.containers],
                "lookup_policy": "common then ordered; last container and last normalized-name occurrence wins",
                "source_contracts": source_contracts, "inputs": [inputs[name] for name in sorted(inputs)],
                "swfs": swfs, "fonts": font_details, "font_resolutions": resolutions,
                "language": {"name": language_name, **safe_language, "selected_coverage": coverage,
                             "cook_policy": "Full validated source dictionary retained; no incomplete subset or proven dynamic coverage claim."},
                "image_resolutions": image_resolutions, "images": image_details, "path_contracts": path_contracts,
                "materials": {"requested": materials, "intrinsics": ["_white", "guiSolid"],
                              "declaration_files": material_files,
                              "lookup_policy": "Conflicting requested definitions are unresolved; equal hashes coalesce. Editor images are not runtime dependencies.",
                              "compatibility": "inventory-only-not-native-material-validation"},
                "issues": issues, "blockers": _BLOCKERS, "exclusions": config.get("exclusions", []),
                "working_set": {"proposed_pixel_cache_bytes": PIXEL_CACHE_BUDGET,
                                "rgba_page_bytes": 256 * 256 * 4, "proposed_resident_pages": 8,
                                "source_decoded_base_bytes": sum(value["decoded_bytes"] for value in image_details.values()),
                                "policy": "Stream bounded pages; package totals are not simultaneous EE/GS residency or a measured target budget."},
                "cooked": {"assets": {}, "page_layouts": {}, "storage_bytes": 0, "file_count": 0}}
    if cook and issues:
        first_issue = issues[0]
        raise ValueError("retail UI cooking has unresolved required UI dependencies: " + first_issue["kind"] +
                         " " + first_issue.get("name", first_issue.get("expected", "")))
    # Inventory work completed before any package directory or prior ownership changes.
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".ui-retail-", dir=output.parent) as temporary:
        stage = Path(temporary)
        if cook:
            all_retained = {**retained, language_name: language_raw}
            for name, data in sorted(all_retained.items()):
                manifest["cooked"]["assets"][name] = _asset(stage, data, Path(name).suffix)
            page_count = 0
            for name in sorted(image_details):
                width, height, rgba = decode_level(resource(name))
                layout = write_pages(width, height, rgba, stage)
                page_count += layout["page_count"]
                if page_count > MAX_TOTAL_PAGES:
                    raise ValueError("retail UI package exceeds 1024 total pages")
                manifest["cooked"]["page_layouts"][name] = layout
            files = list((stage / "assets").iterdir())
            manifest["cooked"]["storage_bytes"] = sum(path.stat().st_size for path in files)
            manifest["cooked"]["file_count"] = len(files)
        _validate_output(repo_root, output, data_root)
        _publish(stage, output, manifest, protected_inputs)
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", type=Path, default=ROOT / "gamedata/d3_bfg")
    parser.add_argument("--output", type=Path, default=ROOT / "build/ui-retail")
    parser.add_argument("--config", type=Path, default=ROOT / "config/ui_retail.json")
    parser.add_argument("--cook", action="store_true", help="publish lossless host candidates; never activates native retail UI")
    args = parser.parse_args(argv)
    try:
        manifest = build_inventory(args.data_root, args.output, args.config, args.cook)
    except (ValueError, OSError, TypeError) as error:
        parser.exit(1, "retail UI: " + str(error) + "\n")
    print("Retail UI " + manifest["mode"] + ": " + str(len(manifest["swfs"])) + " SWFs, " +
          str(len(manifest["fonts"])) + " fonts, " + str(len(manifest["images"])) + " images; " +
          str(len(manifest["issues"])) + " unresolved assets; runtime_ready=false")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
