#!/usr/bin/env python3
# ================================================================================================
# File: run_pcsx2_test.py
# Brief: Run a bounded PS2 smoke scenario and keep the matching ELF, results and diagnostics together.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""PCSX2 smoke runner. It neither changes emulator settings nor stops another session."""

from __future__ import annotations

import argparse
import configparser
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import struct
import subprocess
import sys
import time
import uuid


REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_EMULATOR = Path("/Applications/PCSX2.app/Contents/MacOS/PCSX2")
DEFAULT_CONFIG = Path.home() / "Library/Application Support/PCSX2/inis/PCSX2.ini"
RESULT_LIMIT = 4096
LIFECYCLE_STAGES = ("system", "idlib", "commands", "cvars", "filesystem", "jobs", "session")
NEGATIVE_PROBES = {
    "jpeg-invalid": "SWF JPEG: invalid input",
    "jpeg-oversize": "SWF JPEG: invalid or oversized dimensions (limit 1024)",
    "jpeg-marker": "SWF JPEG: marker extends past input",
    "jpeg-truncated": "SWF JPEG: truncated input",
    "jpeg-signature": "SWF JPEG: Not a JPEG file",
    "input-init": "input capability unavailable: idUsercmdGen::Init",
    "input-map": "input capability unavailable: idUsercmdGen::InitForNewMap",
    "input-build": "input capability unavailable: idUsercmdGen::BuildCurrentUsercmd",
    "input-current": "input capability unavailable: idUsercmdGen::GetCurrentUsercmd",
    "input-inhibit": "input capability unavailable: idUsercmdGen::InhibitUsercmd",
    "input-mouse": "input capability unavailable: idUsercmdGen::MouseState",
    "input-button": "input capability unavailable: idUsercmdGen::ButtonState",
    "input-key": "input capability unavailable: idUsercmdGen::KeyState",
    "input-null-command": "input command string must not be null",
    "input-forced-enable": "input capability unavailable: idUsercmdGen::BuildCurrentUsercmd",
    "input-rumble": "platform capability unavailable: Sys_SetRumble",
    "multiplayer-run": "multiplayer capability unavailable: idMultiplayerGame::Run",
    "multiplayer-chat": "multiplayer capability unavailable: idMultiplayerGame::AddChatLine",
    "multiplayer-snapshot": "multiplayer capability unavailable: idMultiplayerGame::WriteToSnapshot",
    "multiplayer-scoreboard": "multiplayer capability unavailable: idMultiplayerGame::SetScoreboardActive",
    "multiplayer-modes": "multiplayer capability unavailable: idMultiplayerGame::GetGameModes",
    "save-forced-enable": "save game manager capability is unavailable",
    "online-flags": "online/stats/party match flags",
    "no-user": "match requires local user",
    "bad-load-order": "loading completion state",
    "network": "FindOrCreateMatch",
    "classic": "Classic title switching",
    "save-manager": "save game manager capability is unavailable",
    "bad-device": "only input device zero is supported",
    "audio-resource": "sound sample load failed: audio-fixtures/missing (WAV file not found)",
    "audio-duration": "sound sample query requires loaded data: idSoundSample::LengthInMsec",
    "audio-wave-format": "sound sample load failed: audio-fixtures/unsupported (unsupported PCM format)",
    "audio-wave-truncated": "sound sample load failed: audio-fixtures/truncated (RIFF length mismatch)",
    "audio-wave-chunk": "sound sample load failed: audio-fixtures/chunk (chunk exceeds RIFF)",
    "audio-wave-budget": "sound sample load failed: audio-fixtures/budget (PCM data exceeds fixture budget)",
    "audio-device": "audio capability unavailable: idSoundHardware::Init",
    "audio-voice-clock": "logical audio requires a clock callback",
    "audio-voice-backwards": "logical audio clock moved backwards",
    "audio-voice-unloaded": "logical voice allocation requires loaded samples",
    "audio-voice-offset": "logical voice start offset is negative",
    "audio-voice-pitch": "logical voice pitch must be between 0 and 8",
    "audio-voice-pinned": "sound sample still referenced by logical voices: _default-pin",
    "audio-voice-double-free": "logical voice is not allocated",
    "audio-voice-foreign": "logical voice does not belong to this pool",
    "audio-voice-format": "logical voice samples require matching channels",
    "audio-voice-flags": "logical voice received unknown sound flags",
    "platform-launch": "platform capability unavailable: Sys_Launch",
    "platform-negative-duration": "Sys_SecToStr requires a nonnegative duration",
    "model-ase": "source-model import unavailable: ASE_Load",
    "model-lwo": "source-model import unavailable: lwGetObject",
    "model-ma": "source-model import unavailable: MA_Load",
    "renderer-init": "renderer capability unavailable: idRenderSystemLocal::Init",
    "renderer-width": "renderer capability unavailable: idRenderSystemLocal::GetWidth",
    "renderer-draw": "renderer capability unavailable: idRenderSystemLocal::DrawStretchPic",
    "renderer-image": "renderer capability unavailable: idImageManager::ImageFromFile",
    "renderer-vertices": "renderer capability unavailable: idVertexCache::ActuallyAlloc",
    "renderer-shader": "renderer capability unavailable: idRenderProgManager::FindGLSLProgram",
    "renderer-cinematic": "renderer capability unavailable: idCinematic::Alloc",
    "renderer-demo": "renderer capability unavailable: idRenderSystemLocal::WriteDemoPics",
}
GAME_FAILURES = {
    "game-missing-map": "headless game fixture map is missing or oversized",
    "game-syntax": "script maps/logic.script:",
}
REQUIRED_GAME_CHECKS = frozenset({"game/native-init", "game/logic-world", "game/native-ticks-script-events",
                                  "game/script-target-command", "game/delayed-events-posted", "game/entity-removal-cancellation",
                                  "game/map-shutdown", "game/reload-ledger", "game/full-shutdown-ledger"})
SCENARIOS = ("game", *GAME_FAILURES, "platform", "core", "core-missing-fixture") + tuple(
    "lifecycle-" + stage for stage in LIFECYCLE_STAGES) + tuple(NEGATIVE_PROBES)
CRASH_RE = re.compile(r"TLB Miss|\[D3BFG\] FATAL|Assertion failed|Bus error", re.IGNORECASE)
VERSION_RE = re.compile(
    r"^PCSX2 v[0-9]+\.[0-9]+\.[0-9]+(?:[-+][A-Za-z0-9][A-Za-z0-9._-]*)?"
    r"(?:[ \t]+\([A-Za-z0-9 ._:/+-]+\))?[ \t]*$", re.MULTILINE)
CHECK_RE = re.compile(r"\[D3BFG\] CHECK ([A-Za-z0-9_./-]+) (PASS|FAIL)(?:\s|$)")
REQUIRED_CORE_CHECKS = frozenset({
    "codecs/crc-known-vector", "codecs/zlib-streaming-ledger", "codecs/zlib-error-cleanup",
    "codecs/jpeg-rgba-tables-ledger",
    "input/native-action-table", "input/inactive-cleanup-ledger", "input/disabled-controller-policy",
    "deferred/offline-multiplayer-ledger", "deferred/save-metadata-ledger", "deferred/disabled-save-policy",
    "renderer/inactive-interfaces", "renderer/empty-resource-ledger", "renderer/disabled-resolution-cvars",
    "offline/common-idle-demo-ledger",
    "core/platform-language-duration-utc",
    "audio/sample-metadata-ledger",
    "audio/pcm-timing-amplitude-ledger", "audio/default-reload-ledger",
    "audio/voice-timeline", "audio/voice-playback-ledger", "audio/voice-loop-envelope-ledger", "audio/voice-pool-ledger",
    "offline/real-common", "offline/local-user", "offline/registration-idempotent",
    "offline/transient-profile-achievements", "offline/persistence-unavailable",
    "offline/campaign-transitions-copy", "offline/match-reload-ledger", "offline/signout-routing-stale-handle",
    "allocation-before-main", "alignment", "tag-accounting", "unsized-free", "invalid-requests",
    "zero-size", "global-new-forms", "global-delete-forms", "ledger-restored",
    "calloc-overflow",
    "core/initialized", "core/command-buffer-wait", "core/cvar-registration-command", "core/exec-config",
    "core/filesystem-lexer-fixture", "core/filesystem-missing-path-jail", "core/parser-macro-error", "core/shutdown",
    "core/filesystem-streaming-length", "core/filesystem-directory-filters", "core/filesystem-device-path-bounds",
    "core/filesystem-write-append-short-read", "core/filesystem-parents-driver-errors", "core/filesystem-zip-timestamp",
    "idlib/drawvert-half-layout", "idlib/matrix-compose", "idlib/empty-trace-silhouette",
    "idlib/polynomial-copy-lifetime",
    "idlib/tagged-overaligned-new",
    "idlib/surface-ray-parallel", "idlib/lexer-comments-strings-numbers", "idlib/bounded-format-static-scalar",
    "idlib/synchronous-jobs-dependency-sync-reuse", "idlib/single-thread-mutex-signal-atomics",
    "idlib/event-overflow-transfer-clear-ledger",
    "types/inherited-const", "types/rejected-null-sibling", "types/translation-unit-identity",
    "types/menu-hierarchy", "types/gui-hierarchy", "types/model-hierarchy", "types/real-file-objects",
})


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def read_text(path: Path) -> str:
    try:
        return path.read_text(errors="replace")
    except FileNotFoundError:
        return ""


def read_result(path: Path) -> dict | None:
    try:
        if path.stat().st_size > RESULT_LIMIT:
            return {"invalid": "result exceeds size limit"}
        result = json.loads(path.read_text())
        return result if isinstance(result, dict) else {"invalid": "result is not an object"}
    except (FileNotFoundError, json.JSONDecodeError):
        # The target closes the file before its completion marker; a poll can see a partial write.
        return None
    except UnicodeDecodeError:
        return {"invalid": "result is not UTF-8"}


def classify_run(test_id: str, scenario: str, result: dict | None, log: str,
                 returncode: int | None, timed_out: bool) -> tuple[bool, str]:
    """A process exiting successfully is insufficient: target identity, stages and result must agree."""
    if scenario in GAME_FAILURES:
        begin = log.find("[D3BFG] STAGE game BEGIN")
        fatal = log.find("[D3BFG] FATAL", begin) if begin >= 0 else -1
        diagnostic = log[fatal:].splitlines()[0] if fatal >= 0 else ""
        accepted = (not timed_out and returncode in (None, 0) and result is None and
                    not re.search(r"TLB Miss|Bus error|returned unexpectedly", log, re.IGNORECASE) and
                    f"[D3BFG] RUN {test_id} BEGIN" in log and
                    "[D3BFG] CHECK game/native-init PASS" in log and
                    GAME_FAILURES[scenario] in diagnostic)
        return accepted, "expected game fatal observed" if accepted else "missing expected game fatal"
    if scenario == "game":
        checks = CHECK_RE.findall(log)
        ticks = [tuple(map(int, match)) for match in re.findall(
            r"GAME_TICK cycle=(\d+) frame=(\d+) time=(\d+) expected=(\d+) think=(\d+) script=(\d+)(?:\s|$)", log)]
        expected_ticks = [(cycle, frame, frame * 1000 // 60, frame * 1000 // 60, frame, frame)
                          for cycle in range(3) for frame in range(1, 9)]
        commands = re.findall(
            r"GAME_COMMAND cycle=(\d+) frame=(\d+) command=(\S+) expected=(\S+) pending=(\d+)(?:\s|$)", log)
        expected_commands = [(str(cycle), str(frame), command, command, "0")
                             for cycle in range(3) for frame in range(1, 9)
                             for command in ("fixture-activated" if frame in (4, 6) else "none",)]
        events = [tuple(map(int, match)) for match in re.findall(
            r"GAME_EVENTS cycle=(\d+) activation_ms=(\d+) canceled_ms=(\d+) posted=(\d+)(?:\s|$)", log)]
        expected_events = [(cycle, 90, 120, 2) for cycle in range(3)]
        lifetimes = [tuple(map(int, match)) for match in re.findall(
            r"GAME_LIFETIME cycle=(\d+) frame=(\d+) valid=(\d+) resolved=(\d+) named=(\d+)(?:\s|$)", log)]
        expected_lifetimes = [(cycle, frame, int(frame < 7), int(frame < 7), int(frame < 7))
                              for cycle in range(3) for frame in range(1, 9)]
        accepted = (not timed_out and not CRASH_RE.search(log) and returncode in (None, 0) and
                    result is not None and result.get("schema") == 1 and result.get("test_id") == test_id and
                    all(result.get(key) == "PASS" for key in ("manifest", "platform", "game")) and
                    result.get("core") == "SKIP" and
                    f"[D3BFG] RUN {test_id} BEGIN" in log and "[D3BFG] STAGE game BEGIN" in log and
                    "[D3BFG] STAGE game PASS" in log and
                    f"[D3BFG] RESULT {test_id} PASS" in log and
                    REQUIRED_GAME_CHECKS.issubset({name for name, status in checks if status == "PASS"}) and
                    not any(status == "FAIL" for name, status in checks) and
                    ticks in (expected_ticks, expected_ticks + expected_ticks) and
                    commands in (expected_commands, expected_commands + expected_commands) and
                    events in (expected_events, expected_events + expected_events) and
                    lifetimes in (expected_lifetimes, expected_lifetimes + expected_lifetimes))
        return accepted, "game completed" if accepted else "incomplete or failed game fixture"
    if scenario in NEGATIVE_PROBES:
        if timed_out:
            return False, "watchdog timeout"
        if re.search(r"TLB Miss|Bus error", log, re.IGNORECASE):
            return False, "crash diagnostic"
        if result is not None or "returned unexpectedly" in log:
            return False, "negative probe returned"
        if returncode not in (None, 0):
            return False, f"emulator exited with status {returncode}"
        begin = f"[D3BFG] PROBE {scenario} BEGIN"
        fatal = log.find("[D3BFG] FATAL", log.find(begin)) if begin in log else -1
        expected = NEGATIVE_PROBES[scenario]
        # Check the first fatal line, with a name boundary: Init must not accept InitForNewMap,
        # and a later expected message must not hide an unrelated initial failure.
        diagnostic = log[fatal:].splitlines()[0] if fatal >= 0 else ""
        matches = re.search(re.escape(expected) + r"(?![A-Za-z0-9_])", diagnostic) is not None
        if f"[D3BFG] RUN {test_id} BEGIN" not in log or "[D3BFG] CHECK probe/ready PASS" not in log or not matches:
            return False, "missing expected probe fatal"
        return True, "expected fatal observed"
    if CRASH_RE.search(log):
        return False, "crash diagnostic"
    if timed_out:
        return False, "watchdog timeout"
    if result is not None:
        if result.get("schema") != 1 or result.get("test_id") != test_id:
            return False, "invalid or stale result identity"
        if result.get("manifest") != "PASS" or result.get("platform") != "PASS":
            return False, "manifest or platform failed"
        expected_core = "SKIP" if scenario == "platform" else "FAIL" if scenario == "core-missing-fixture" else "PASS"
        if result.get("core") != expected_core:
            return False, "unexpected core result"
        expected_status = "FAIL" if scenario == "core-missing-fixture" else "PASS"
        expected_markers = (
            "[D3BFG] STAGE platform PASS",
            f"[D3BFG] STAGE core {expected_core}",
            f"[D3BFG] RESULT {test_id} {expected_status}",
        )
        if not all(marker in log for marker in expected_markers):
            return False, "missing stage or completion marker"
        if scenario.startswith("lifecycle-"):
            stage = scenario.removeprefix("lifecycle-")
            completed = LIFECYCLE_STAGES[:LIFECYCLE_STAGES.index(stage) + 1]
            ready = re.findall(r"\[D3BFG\] COMMON stage=(\w+) ready", log)
            shutdown = re.findall(r"\[D3BFG\] COMMON stage=(\w+) shutdown", log)
            # stdout and emulator logs can both contain the same complete sequence.
            valid_ready = tuple(ready) in (completed, completed + completed)
            reverse = tuple(reversed(completed))
            valid_shutdown = tuple(shutdown) in (reverse, reverse + reverse)
            if not valid_ready or not valid_shutdown or "[D3BFG] CHECK probe/partial-shutdown PASS" not in log:
                return False, "invalid partial startup/shutdown sequence"
        elif scenario != "platform":
            checks = CHECK_RE.findall(log)
            names = {name for name, _ in checks}
            failures = {name for name, status in checks if status == "FAIL"}
            if not REQUIRED_CORE_CHECKS.issubset(names):
                return False, "missing core checks"
            expected_failures = {"core/filesystem-lexer-fixture"} if scenario == "core-missing-fixture" else set()
            if failures != expected_failures:
                return False, "unexpected core check failure"
        if returncode not in (None, 0):
            return False, f"emulator exited with status {returncode}"
        return True, "expected failure observed" if expected_status == "FAIL" else "completed"
    if returncode is not None:
        return False, f"emulator exited before structured result (status {returncode})"
    return False, "missing structured result"


def emulator_is_running(emulator: Path) -> bool:
    # Checking processes is read-only; termination later uses only the Popen object we create.
    listing = subprocess.run(["ps", "-axo", "pid=,comm="], capture_output=True, text=True, check=True)
    for line in listing.stdout.splitlines():
        fields = line.strip().split(maxsplit=1)
        if len(fields) == 2 and int(fields[0]) != os.getpid():
            command = Path(fields[1])
            if command.name.lower() == "pcsx2" or command == emulator:
                return True
    return False


def check_config(path: Path) -> None:
    config = configparser.ConfigParser(interpolation=None, strict=False)
    with path.open() as source:
        config.read_file(source)
    for section, option in (("EmuCore", "HostFs"), ("Logging", "EnableIOPConsole"),
                            ("Logging", "EnableFileLogging")):
        if not config.getboolean(section, option, fallback=False):
            raise ValueError(f"{path}: [{section}] {option} must already be true; settings were not changed")


def emulator_version(emulator: Path) -> str:
    version = subprocess.run([str(emulator), "-version"], capture_output=True,
                             text=True, timeout=10, check=False)
    output = "\n".join(part.strip() for part in (version.stdout, version.stderr) if part.strip())
    # Installed PCSX2 2.6.3 prints a valid version and exits 1 for information
    # commands. Status 1 is accepted only with the anchored version line.
    if version.returncode not in (0, 1) or VERSION_RE.search(output) is None:
        raise ValueError(f"PCSX2 -version failed validation (status {version.returncode})")
    return output


def stop_owned_process(process: subprocess.Popen) -> None:
    if process.poll() is None:
        # PCSX2 handles SIGINT by shutting down normally. No process-name-wide kills are used.
        process.send_signal(signal.SIGINT)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)


def archive_build_artifacts(elf: Path, staged_elf: Path, staged_symbols: Path, output: Path) -> dict:
    source = elf.parent
    report_path = source / "build-report.json"
    report = json.loads(report_path.read_text(encoding="utf-8"))
    if report.get("elf_sha256") != sha256(staged_symbols) or report.get("runnable_elf_sha256") != sha256(staged_elf):
        raise ValueError("build report does not match the staged ELF and symbols; rebuild before running")
    artifacts = {"build-report.json": None, "build-report.txt": None,
                 "d3bfg.map": report.get("link_map_sha256"),
                 ".source-identity.json": report.get("source_identity_sha256")}
    for flag_path, expected in report["flag_stamps"].items():
        name = Path(flag_path).name
        if json.loads((source / name).read_text(encoding="utf-8")) != expected:
            raise ValueError(f"build flag stamp does not match the ELF report: {name}")
        artifacts[name] = sha256(source / name)
    hashes = {}
    for name, expected_hash in artifacts.items():
        shutil.copy2(source / name, output / name)
        actual_hash = sha256(output / name)
        if name in ("d3bfg.map", ".source-identity.json") and expected_hash is None:
            raise ValueError(f"build report lacks the artifact identity: {name}")
        if expected_hash is not None and expected_hash != actual_hash:
            raise ValueError(f"build artifact does not match the ELF report: {name}")
        hashes[name] = actual_hash
    return hashes


def archive_resident_artifacts(elf: Path, staged_elf: Path, symbols: Path, output: Path) -> dict:
    source = elf.parent
    report = json.loads((source / "report.json").read_text())
    if (report.get("passed") is not True or report.get("garbage_collection") is not False or
            not report.get("registrations") or not all(report["registrations"].values()) or
            report.get("object_count", 0) <= 0 or report.get("unresolved") != [] or
            report.get("duplicate_definitions") != [] or report.get("missing_map_objects") != [] or
            report.get("elf_sha256") != sha256(symbols) or
            report.get("runnable_elf_sha256") != sha256(staged_elf) or
            report.get("link_map_sha256") != sha256(source / "d3bfg.map")):
        raise ValueError("resident report does not match the staged game images and map")
    hashes = {}
    for name in ("report.json", "report.txt", "d3bfg.map", "objects.rsp"):
        shutil.copy2(source / name, output / name)
        hashes[name] = sha256(output / name)
    for path, expected in report["flag_stamps"].items():
        stamp = REPO_ROOT / path
        if json.loads(stamp.read_text()) != expected:
            raise ValueError("resident build flags changed; rebuild before running")
        shutil.copy2(stamp, output / stamp.name)
        hashes[stamp.name] = sha256(stamp)
    return hashes


def stage_game_fixtures(directory: Path, scenario: str) -> None:
    for folder in ("def", "maps", "script", "materials", "fx", "particles", "af", "newpdas"):
        (directory / folder).mkdir(parents=True, exist_ok=True)
    (directory / "def/fixture.def").write_text(
        'entityDef aas_types {}\nentityDef worldspawn { "spawnclass" "idWorldspawn" "noclipmodel" "1" }\n')
    (directory / "script/doom_defs.script").write_text(
        'scriptEvent void waitFrame();\nscriptEvent void activate(entity activator);\nscriptEvent void remove();\n')
    (directory / "script/doom_main.script").write_text(
        'float fixtureTicks = 0;\nvoid doom_main() {}\n')
    if scenario != "game-missing-map":
        (directory / "maps/logic.map").write_text(
            'Version 2\n{\n"classname" "worldspawn"\n"name" "logic_world"\n}\n')
    (directory / "maps/logic.script").write_text(
        'void main() { fixtureTicks = absent; }\n' if scenario == "game-syntax" else
        'void main() { while (fixtureTicks < 8) { fixtureTicks++; '
        'if (fixtureTicks == 4) { $logic_target.activate($logic_probe); } '
        'if (fixtureTicks == 7) { $logic_target.remove(); } sys.waitFrame(); } }\n')


def stage_audio_fixtures(directory: Path) -> None:
    """Authored PCM only; never copy retail assets into smoke archives."""
    directory.mkdir(parents=True)

    def wave(rate: int, channels: int, pcm: bytes, junk: bool = False) -> bytes:
        fmt = struct.pack("<HHIIHH", 1, channels, rate, rate * channels * 2, channels * 2, 16)
        chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
        if junk:
            chunks += b"JUNK\x03\x00\x00\x00abc\x00"
        chunks += b"data" + struct.pack("<I", len(pcm)) + pcm
        return b"RIFF" + struct.pack("<I", len(chunks) + 4) + b"WAVE" + chunks

    mono = [(-32768 if i < 11025 // 60 else 8192 if i < 2 * 11025 // 60 else 0)
            for i in range(11025)]
    mono[-1] = 16384
    (directory / "mono.wav").write_bytes(wave(11025, 1, struct.pack("<11025h", *mono), junk=True))
    stereo = wave(44100, 2, struct.pack("<hh", 0, -16384) * 22050)
    (directory / "stereo.wav").write_bytes(stereo)
    unsupported = bytearray(stereo)
    struct.pack_into("<H", unsupported, 20, 2)  # ADPCM format tag.
    (directory / "unsupported.wav").write_bytes(unsupported)
    (directory / "truncated.wav").write_bytes(stereo[:-1])
    bad_chunk = bytearray(stereo)
    struct.pack_into("<I", bad_chunk, 40, 0xffffffff)
    (directory / "chunk.wav").write_bytes(bad_chunk)
    (directory / "budget.wav").write_bytes(wave(8000, 1, b"\x00" * (256 * 1024 + 2)))


def run(args: argparse.Namespace) -> tuple[bool, Path]:
    emulator = args.emulator.resolve()
    elf = args.elf.resolve()
    symbols = args.symbols.resolve() if args.symbols else elf.with_name("d3bfg_unstripped.elf")
    if not emulator.is_file() or not os.access(emulator, os.X_OK):
        raise ValueError(f"emulator is not executable: {emulator}")
    if not elf.is_file() or not symbols.is_file():
        raise ValueError("the stripped ELF and its matching unstripped symbols must both exist")
    if emulator_is_running(emulator):
        raise ValueError("an emulator is already running; close it before starting an isolated smoke run")
    check_config(args.config)
    version = emulator_version(emulator)

    test_id = "smoke_" + uuid.uuid4().hex[:16]
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "_" + test_id
    output = args.output.resolve() / run_id
    output.mkdir(parents=True, exist_ok=False)
    staged_elf = output / "d3bfg.elf"
    shutil.copy2(elf, staged_elf)
    shutil.copy2(symbols, output / "d3bfg_unstripped.elf")
    shutil.copy2(args.config, output / "PCSX2.ini.snapshot")
    game_scenario = args.scenario == "game" or args.scenario in GAME_FAILURES
    archive = archive_resident_artifacts if game_scenario else archive_build_artifacts
    build_artifacts = archive(elf, staged_elf, output / "d3bfg_unstripped.elf", output)
    if game_scenario:
        stage_game_fixtures(output / "game-fixture", args.scenario)
        (output / "game.manifest").write_text(f"D3BFG_GAME_1\n{test_id}\n{args.scenario}\n")

    fixture = output / "fixture.txt"
    if args.scenario != "core-missing-fixture":
        if args.fixture:
            shutil.copy2(args.fixture, fixture)
        else:
            fixture.write_text("D3BFG core fixture\n", encoding="ascii")
    smoke_config = output / "smoke.cfg"
    if args.scenario != "platform" and not game_scenario:
        smoke_config.write_text("set ps2_smoke_config 37\nps2_smoke_command 77\n", encoding="ascii")
        fs_fixtures = output / "fs-fixtures"
        (fs_fixtures / "child").mkdir(parents=True)
        (fs_fixtures / "mixed.TxT").write_bytes(b"authored directory fixture\n")
        (fs_fixtures / "large.bin").write_bytes(b"Z" * 71680)
        stage_audio_fixtures(output / "audio-fixtures")
    probe = args.scenario in NEGATIVE_PROBES or args.scenario.startswith("lifecycle-")
    fixture_path = "host:probe-" + args.scenario if probe else "host:fixture.txt"
    (output / "smoke.manifest").write_text(f"D3BFG_SMOKE 1\n{test_id}\n{fixture_path}\n", encoding="ascii")
    emulator_log = output / "emulog.txt"
    stdout_log = output / "stdout.txt"
    # -logfile is supported by the installed emulator and avoids overwriting the user's emulog.txt.
    command = [str(emulator), "-batch", "-elf", str(staged_elf), "-logfile", str(emulator_log)]
    metadata = {
        "schema": 1, "test_id": test_id, "scenario": args.scenario,
        "started_utc": datetime.now(timezone.utc).isoformat(), "timeout_seconds": args.timeout,
        "elf_sha256": sha256(staged_elf), "symbols_sha256": sha256(output / "d3bfg_unstripped.elf"),
        "manifest_sha256": sha256(output / "smoke.manifest"),
        "fixture_path": fixture_path,
        "fixture_sha256": sha256(fixture) if fixture.exists() else None,
        "smoke_config_sha256": sha256(smoke_config) if smoke_config.exists() else None,
        "filesystem_fixture_sha256": {
            str(path.relative_to(output)): sha256(path)
            for path in sorted((output / "fs-fixtures").rglob("*")) if path.is_file()
        },
        "audio_fixture_sha256": {
            str(path.relative_to(output)): sha256(path)
            for path in sorted((output / "audio-fixtures").glob("*.wav"))
        },
        "game_fixture_sha256": {
            str(path.relative_to(output)): sha256(path)
            for path in sorted((output / "game-fixture").rglob("*")) if path.is_file()
        },
        "game_manifest_sha256": sha256(output / "game.manifest") if game_scenario else None,
        "build_artifact_sha256": build_artifacts,
        "config_sha256": sha256(args.config), "emulator": str(emulator),
        "emulator_version": version, "command": command,
    }
    (output / "run.json").write_text(json.dumps(metadata, indent=2) + "\n")
    started = time.monotonic()
    timed_out = False
    result = None
    returncode = None
    with stdout_log.open("wb") as stdout:
        process = subprocess.Popen(command, cwd=output, stdout=stdout, stderr=subprocess.STDOUT)
        try:
            while True:
                result = read_result(output / "result.json")
                log = read_text(emulator_log) + "\n" + read_text(stdout_log)
                returncode = process.poll()
                # Wait for both a closed result and a final marker, which stdout can lag behind.
                has_completion = f"[D3BFG] RESULT {test_id} " in log
                if (args.scenario in NEGATIVE_PROBES or args.scenario in GAME_FAILURES) and classify_run(test_id, args.scenario, result, log, returncode, False)[0]:
                    break
                if (result is not None and has_completion) or returncode is not None or CRASH_RE.search(log):
                    break
                if time.monotonic() - started >= args.timeout:
                    timed_out = True
                    break
                time.sleep(0.1)
        finally:
            stop_owned_process(process)

    log = read_text(emulator_log) + "\n" + read_text(stdout_log)
    result = read_result(output / "result.json")
    passed, reason = classify_run(test_id, args.scenario, result, log, returncode, timed_out)
    summary = {
        "passed": passed, "reason": reason, "elapsed_seconds": time.monotonic() - started,
        "returncode_before_cleanup": returncode, "returncode_after_cleanup": process.returncode,
        "timed_out": timed_out,
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"{'PASS' if passed else 'FAIL'}: {reason}\nArtifacts: {output}")
    return passed, output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=REPO_ROOT / "build/debug/d3bfg.elf")
    parser.add_argument("--symbols", type=Path, help="matching unstripped ELF (default: next to --elf)")
    parser.add_argument("--emulator", type=Path, default=DEFAULT_EMULATOR)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG,
                        help="PCSX2's active settings file to verify/archive; does not select another profile")
    parser.add_argument("--scenario", choices=SCENARIOS, default="platform")
    parser.add_argument("--fixture", type=Path, help="authored core fixture to stage beside the ELF")
    parser.add_argument("--output", type=Path, default=REPO_ROOT / "build/test-results")
    parser.add_argument("--timeout", type=float, default=45.0)
    args = parser.parse_args()
    if args.timeout <= 0 or args.timeout > 3600:
        parser.error("--timeout must be greater than zero and no greater than 3600 seconds")
    try:
        passed, _ = run(args)
        return 0 if passed else 1
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
