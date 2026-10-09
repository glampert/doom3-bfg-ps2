#!/usr/bin/env python3
# ================================================================================================
# File: run_common_tests.py
# Brief: Run isolated Common startup/shutdown and offline capability failure probes in PCSX2.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""Each probe gets a fresh process because Doom static CVar registration is one-shot."""

import argparse
from pathlib import Path
import subprocess
import sys

from run_pcsx2_test import DEFAULT_CONFIG, DEFAULT_EMULATOR, LIFECYCLE_STAGES, NEGATIVE_PROBES, REPO_ROOT, run


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=REPO_ROOT / "build/debug/d3bfg.elf")
    parser.add_argument("--emulator", type=Path, default=DEFAULT_EMULATOR)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--output", type=Path, default=REPO_ROOT / "build/test-results")
    parser.add_argument("--timeout", type=float, default=45.0)
    args = parser.parse_args()
    if not 0 < args.timeout <= 3600:
        parser.error("timeout must be greater than zero and at most 3600 seconds")
    args.symbols = None
    args.fixture = None
    scenarios = tuple("lifecycle-" + stage for stage in LIFECYCLE_STAGES) + tuple(NEGATIVE_PROBES)
    try:
        for scenario in scenarios:
            args.scenario = scenario
            passed, _ = run(args)
            if not passed:
                return 1
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    print(f"Common probes: {len(scenarios)} passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
