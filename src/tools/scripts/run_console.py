#!/usr/bin/env python3
# ================================================================================================
# File: run_console.py
# Brief: Drive and check the native console step of the merged UI session: edit history, close/reopen and capture.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

import re
from pathlib import Path

import run_pcsx2_test as smoke


TARGET_CRASH_RE = re.compile(r"TLB Miss|Assertion failed|Bus error", re.IGNORECASE)
# The exact tagged ledger every production session recovers at full shutdown.
BASELINE = (7424, 7600, 4)
STATUS_RE = re.compile(r"\[D3BFG\] CONSOLE frames=(\d+) presents=(\d+) redraws=(\d+) active=([01])")
CAPTURE_RE = re.compile(r"\[D3BFG\] CONSOLE capture=(console-edit\.tga) frame=(\d+) presents=(\d+) redraws=(\d+)")
SHUTDOWN_RE = re.compile(r"\[D3BFG\] CONSOLE shutdown complete frames=(\d+) presents=(\d+) redraws=(\d+)")
# Typed into the edit field for the capture, then deleted.
EDIT_TEXT = "echo pending"


def append_commands(output: Path, *commands: str) -> None:
    with (output / "console.txt").open("ab") as stream:
        stream.write(("\n".join(commands) + "\n").encode("ascii"))


def native_echo(identity: str) -> str:
    return "echo native_console_" + identity.rsplit("_", 1)[-1]


def echoed(log: str, marker: str) -> bool:
    # HostFs logs submitted lines before wait delays execute. Only actual Echo output is a barrier.
    return re.search(re.escape(marker) + r" +\r?\n", log) is not None


def echo_count(log: str, identity: str) -> int:
    return len(re.findall(re.escape("]" + native_echo(identity)) + r"\r?\n", log))


def driver(identity: str, begin: str, end: str):
    """Yield each command batch with the condition, over this step's log, that ends its wait."""
    echo = native_echo(identity)
    yield ([f'echo "{begin}"', "ps2_status", f'ps2_consoleText "{echo}"', "ps2_consoleKey enter"],
           lambda log: echo_count(log, identity) == 1)
    yield ["ps2_consoleKey up", "ps2_consoleKey enter"], lambda log: echo_count(log, identity) == 2
    yield ["ps2_consoleKey toggle", "ps2_status"], lambda log: any(match[3] == "0" for match in STATUS_RE.findall(log))
    yield (["ps2_consoleKey toggle", "ps2_status"],
           lambda log: STATUS_RE.findall(log)[-1][3] == "1")
    # The reopened console slides down over real time: let it settle before the probes are captured.
    # Deliberate correction and cursor movement exercise the native edit field.
    yield (["wait 60", 'ps2_consoleText "echo pendinx"', "ps2_consoleKey backspace", 'ps2_consoleText "g"',
            "ps2_consoleKey left", "ps2_consoleKey right", 'ps2_capture "console-edit.tga"', "ps2_status"],
           lambda log: "[D3BFG] CONSOLE capture=console-edit.tga " in log)
    yield (["history", "ps2_consoleKey home", *(["ps2_consoleKey delete"] * len(EDIT_TEXT)), "ps2_status",
            f'echo "{end}"'], lambda log: echoed(log, end))


def classify(log: str, identity: str) -> tuple[bool, str]:
    """Check the console step's log: native edit submission, history replay, close/reopen and capture."""
    if smoke.CRASH_RE.search(log) or "[D3BFG] FATAL" in log:
        return False, "crash or fatal diagnostic"
    statuses = [tuple(map(int, match)) for match in STATUS_RE.findall(log)]
    if len(statuses) < 5 or statuses[0][3] != 1 or not any(status[3] == 0 for status in statuses) or statuses[-1][3] != 1:
        return False, "native console did not start open, close, reopen and finish active"
    if any(frame != present for frame, present, _, _ in statuses):
        return False, "status reports unexpected extra presentations or ticks"
    if any(later[0] < earlier[0] or later[2] < earlier[2] for earlier, later in zip(statuses, statuses[1:])):
        return False, "native console frame or redraw counters went backwards"
    # Each native Enter requests a synchronous redraw that presents nothing extra.
    if statuses[-1][2] - statuses[0][2] < 2:
        return False, "native Enter did not request redraw-only presentations"
    # A timestamp's closing bracket precedes each native prompt in PCSX2's file log.
    command_lines = re.findall(r"\]([A-Za-z][^\r\n]*)", log)
    if command_lines.count(native_echo(identity)) != 2 or "history" not in command_lines:
        return False, "native edit submission or history replay did not execute"
    echo_output = re.escape(native_echo(identity).removeprefix("echo ")) + r" +\r?\n"
    if len(re.findall(echo_output, log)) != 2:
        return False, "native echo command output was not produced twice"
    captures = CAPTURE_RE.findall(log)
    if len(captures) != 1:
        return False, "missing or duplicate native edit capture"
    frame, present, redraw = map(int, captures[0][1:])
    if frame != present or frame < statuses[0][0] or redraw < statuses[0][2] + 2:
        return False, "capture did not follow a completed native presentation"
    return True, "native console edited, replayed history, closed, reopened and captured its input line"
