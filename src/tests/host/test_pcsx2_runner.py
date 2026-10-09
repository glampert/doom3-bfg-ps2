#!/usr/bin/env python3
# ================================================================================================
# File: test_pcsx2_runner.py
# Brief: Exercise smoke-runner failure classification and watchdog ownership with a fake emulator.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[2] / "tools/scripts/run_pcsx2_test.py"
SPEC = importlib.util.spec_from_file_location("pcsx2_runner", SCRIPT)
assert SPEC and SPEC.loader
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)


def complete_log(test_id: str = "run1", core: str = "SKIP", status: str = "PASS") -> str:
    checks = ""
    if core != "SKIP":
        checks = "".join(f"[D3BFG] CHECK {name} " +
                         ("FAIL" if core == "FAIL" and name == "core/filesystem-lexer-fixture" else "PASS") + "\n"
                         for name in sorted(runner.REQUIRED_CORE_CHECKS))
    return (checks + f"[D3BFG] STAGE platform PASS\n[D3BFG] STAGE core {core}\n"
            f"[D3BFG] RESULT {test_id} {status}\n")


def complete_result(test_id: str = "run1", core: str = "SKIP") -> dict:
    return {"schema": 1, "test_id": test_id, "manifest": "PASS", "platform": "PASS", "core": core}


class ClassifierTests(unittest.TestCase):
    def test_platform_completion(self):
        self.assertEqual(runner.classify_run("run1", "platform", complete_result(), complete_log(), None, False),
                         (True, "completed"))

    def test_core_completion(self):
        self.assertTrue(runner.classify_run("run1", "core", complete_result(core="PASS"),
                                           complete_log(core="PASS"), None, False)[0])

    def test_missing_filesystem_service_check_rejected(self):
        log = complete_log(core="PASS").replace("[D3BFG] CHECK core/filesystem-directory-filters PASS\n", "")
        self.assertEqual(runner.classify_run("run1", "core", complete_result(core="PASS"), log, None, False),
                         (False, "missing core checks"))

    def test_expected_missing_fixture_failure(self):
        self.assertEqual(runner.classify_run("run1", "core-missing-fixture", complete_result(core="FAIL"),
                                            complete_log(core="FAIL", status="FAIL"), None, False),
                         (True, "expected failure observed"))

    def test_wrong_negative_stage_cannot_pass(self):
        result = complete_result(core="FAIL")
        result["platform"] = "FAIL"
        self.assertFalse(runner.classify_run("run1", "core-missing-fixture", result,
                                            complete_log(core="FAIL", status="FAIL"), None, False)[0])

    def test_unrelated_negative_failure_rejected(self):
        log = complete_log(core="FAIL", status="FAIL").replace("CHECK core/shutdown PASS", "CHECK core/shutdown FAIL")
        self.assertEqual(runner.classify_run("run1", "core-missing-fixture", complete_result(core="FAIL"),
                                            log, None, False), (False, "unexpected core check failure"))

    def test_missing_negative_checks_rejected(self):
        log = complete_log(core="FAIL", status="FAIL").replace("[D3BFG] CHECK core/shutdown PASS\n", "")
        self.assertEqual(runner.classify_run("run1", "core-missing-fixture", complete_result(core="FAIL"),
                                            log, None, False), (False, "missing core checks"))

    def test_clean_process_exit_without_result(self):
        self.assertFalse(runner.classify_run("run1", "platform", None, complete_log(), 0, False)[0])

    def test_stale_result(self):
        self.assertEqual(runner.classify_run("run1", "platform", complete_result("old"), complete_log(), None, False),
                         (False, "invalid or stale result identity"))

    def test_missing_marker(self):
        self.assertEqual(runner.classify_run("run1", "platform", complete_result(), "", None, False),
                         (False, "missing stage or completion marker"))

    def test_crash_after_pass(self):
        self.assertEqual(runner.classify_run("run1", "platform", complete_result(),
                                            complete_log() + "TLB Miss, pc=0x100000 addr=0x10", None, False),
                         (False, "crash diagnostic"))

    def test_late_pass_does_not_cancel_watchdog(self):
        self.assertEqual(runner.classify_run("run1", "platform", complete_result(), complete_log(), None, True),
                         (False, "watchdog timeout"))


class CommonProbeClassifierTests(unittest.TestCase):
    def partial_log(self, stage="cvars"):
        completed = runner.LIFECYCLE_STAGES[:runner.LIFECYCLE_STAGES.index(stage) + 1]
        log = "".join(f"[D3BFG] COMMON stage={name} ready\n" for name in completed)
        log += "".join(f"[D3BFG] COMMON stage={name} shutdown\n" for name in reversed(completed))
        return log + "[D3BFG] CHECK probe/partial-shutdown PASS\n" + complete_log(core="PASS")

    def test_every_partial_stage(self):
        for stage in runner.LIFECYCLE_STAGES:
            with self.subTest(stage=stage):
                self.assertTrue(runner.classify_run("run1", "lifecycle-" + stage,
                    complete_result(core="PASS"), self.partial_log(stage), None, False)[0])

    def test_reverse_order_is_required(self):
        log = self.partial_log().replace("stage=cvars shutdown", "stage=commands shutdown", 1)
        self.assertFalse(runner.classify_run("run1", "lifecycle-cvars", complete_result(core="PASS"), log, None, False)[0])

    def test_unstarted_stage_shutdown_is_rejected(self):
        log = self.partial_log() + "[D3BFG] COMMON stage=session shutdown\n"
        self.assertFalse(runner.classify_run("run1", "lifecycle-cvars", complete_result(core="PASS"), log, None, False)[0])

    def test_partial_heap_failure_cannot_pass(self):
        log = self.partial_log().replace("probe/partial-shutdown PASS", "probe/partial-shutdown FAIL")
        self.assertFalse(runner.classify_run("run1", "lifecycle-cvars", complete_result(core="PASS"), log, None, False)[0])

    def fatal_log(self, probe):
        return (f"[D3BFG] RUN run1 BEGIN\n[D3BFG] CHECK probe/ready PASS\n"
                f"[D3BFG] PROBE {probe} BEGIN\n[D3BFG] FATAL {runner.NEGATIVE_PROBES[probe]}\n")

    def test_expected_fatals(self):
        for probe in runner.NEGATIVE_PROBES:
            with self.subTest(probe=probe):
                self.assertTrue(runner.classify_run("run1", probe, None, self.fatal_log(probe), None, False)[0])

    def test_input_method_prefix_and_later_message_cannot_pass(self):
        expected = runner.NEGATIVE_PROBES["input-init"]
        wrong_method = self.fatal_log("input-init").replace(expected, runner.NEGATIVE_PROBES["input-map"])
        later_message = self.fatal_log("input-init").replace(expected, "unrelated input failure\n" + expected)
        for log in (wrong_method, later_message):
            with self.subTest(log=log):
                self.assertFalse(runner.classify_run("run1", "input-init", None, log, None, False)[0])

    def test_wrong_fatal_watchdog_and_tlb_fail(self):
        for log, timeout in [(self.fatal_log("network") + "TLB Miss", False),
                             (self.fatal_log("network"), True),
                             (self.fatal_log("network").replace("FindOrCreateMatch", "another failure"), False)]:
            self.assertFalse(runner.classify_run("run1", "network", None, log, None, timeout)[0])

    def test_stale_fatal_identity_and_return_are_rejected(self):
        self.assertFalse(runner.classify_run("old", "network", None, self.fatal_log("network"), None, False)[0])
        self.assertFalse(runner.classify_run("run1", "network", complete_result(), self.fatal_log("network"), None, False)[0])

    def test_missing_offline_check_cannot_pass(self):
        log = complete_log(core="PASS").replace("[D3BFG] CHECK offline/match-reload-ledger PASS\n", "")
        self.assertFalse(runner.classify_run("run1", "core", complete_result(core="PASS"), log, None, False)[0])


class ProcessTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        # Fake-emulator lifecycle tests must not depend on a user's live GUI session or ps access.
        inventory = mock.patch.object(runner, "emulator_is_running", return_value=False)
        inventory.start()
        self.addCleanup(inventory.stop)
        self.elf = self.root / "d3bfg.elf"
        self.elf.write_bytes(b"ELF test input")
        self.symbols = self.root / "d3bfg_unstripped.elf"
        self.symbols.write_bytes(b"ELF test symbols")
        self.config = self.root / "PCSX2.ini"
        self.config.write_text("[EmuCore]\nHostFs = true\n[Logging]\nEnableIOPConsole = true\n"
                               "EnableFileLogging = true\n")
        flags = {"compiler": "fixture compiler", "tokens": ["-std=gnu++20"]}
        (self.root / ".backend-flags.json").write_text(json.dumps(flags))
        (self.root / ".source-identity.json").write_text('{"source_commit":"fixture"}\n')
        (self.root / "d3bfg.map").write_text("fixture map\n")
        (self.root / "build-report.txt").write_text("fixture report\n")
        report = {"elf_sha256": runner.sha256(self.symbols), "runnable_elf_sha256": runner.sha256(self.elf),
                  "source_identity_sha256": runner.sha256(self.root / ".source-identity.json"),
                  "link_map_sha256": runner.sha256(self.root / "d3bfg.map"),
                  "flag_stamps": {str(self.root / ".backend-flags.json"): flags}}
        (self.root / "build-report.json").write_text(json.dumps(report))

    def fake_emulator(self, mode: str, version_status: int = 0) -> Path:
        executable = self.root / "fake_emulator"
        executable.write_text(textwrap.dedent(f"""\
            #!/usr/bin/env python3
            import json
            from pathlib import Path
            import signal
            import sys
            import time
            if '-version' in sys.argv:
                print('PCSX2 v2.6.3\\nhttps://pcsx2.net/')
                sys.exit({version_status!r})
            signal.signal(signal.SIGINT, lambda *_: sys.exit(0))
            test_id = Path('smoke.manifest').read_text().splitlines()[1]
            log = Path(sys.argv[sys.argv.index('-logfile') + 1])
            if {mode!r} == 'timeout':
                log.write_text('fixture waiting\\n')
            else:
                core = 'SKIP'
                status = 'PASS'
                if {mode!r} == 'negative':
                    core, status = 'FAIL', 'FAIL'
                result = {{'schema': 1, 'test_id': test_id, 'manifest': 'PASS', 'platform': 'PASS', 'core': core}}
                Path('result.json').write_text(json.dumps(result))
                if {mode!r} == 'missing-marker':
                    log.write_text('fixture without target markers\\n')
                else:
                    checks = ''
                    if {mode!r} == 'negative':
                        for name in {sorted(runner.REQUIRED_CORE_CHECKS)!r}:
                            check_status = 'FAIL' if name == 'core/filesystem-lexer-fixture' else 'PASS'
                            checks += f'[D3BFG] CHECK {{name}} {{check_status}}\\n'
                    log.write_text(checks + f'[D3BFG] STAGE platform PASS\\n[D3BFG] STAGE core {{core}}\\n'
                                   f'[D3BFG] RESULT {{test_id}} {{status}}\\n')
            while True:
                time.sleep(0.05)
            """))
        executable.chmod(0o755)
        return executable

    def run_fixture(self, mode: str, scenario: str = "platform", version_status: int = 0) -> tuple[bool, Path]:
        args = argparse.Namespace(emulator=self.fake_emulator(mode, version_status), elf=self.elf, symbols=self.symbols,
                                  config=self.config, output=self.root / "results", fixture=None,
                                  scenario=scenario, timeout=0.4)
        return runner.run(args)

    def test_completed_process_archives_inputs_and_results(self):
        passed, output = self.run_fixture("pass")
        self.assertTrue(passed)
        self.assertEqual((output / "d3bfg.elf").read_bytes(), self.elf.read_bytes())
        self.assertEqual((output / "PCSX2.ini.snapshot").read_bytes(), self.config.read_bytes())
        self.assertEqual(json.loads((output / "summary.json").read_text())["returncode_after_cleanup"], 0)
        metadata = json.loads((output / "run.json").read_text())
        self.assertEqual(metadata["elf_sha256"], runner.sha256(self.elf))
        self.assertFalse((self.root / "emulog.txt").exists())
        self.assertIn("build-report.json", metadata["build_artifact_sha256"])
        self.assertEqual((output / ".backend-flags.json").read_bytes(), (self.root / ".backend-flags.json").read_bytes())

    def test_information_command_status_one_still_runs(self):
        passed, output = self.run_fixture("pass", version_status=1)
        self.assertTrue(passed)
        self.assertIn("PCSX2 v2.6.3", json.loads((output / "run.json").read_text())["emulator_version"])

    def test_timeout_stops_owned_process_and_preserves_log(self):
        passed, output = self.run_fixture("timeout")
        self.assertFalse(passed)
        summary = json.loads((output / "summary.json").read_text())
        self.assertTrue(summary["timed_out"])
        self.assertEqual(summary["returncode_after_cleanup"], 0)
        self.assertIn("waiting", (output / "emulog.txt").read_text())

    def test_missing_marker_fails_even_with_result(self):
        passed, output = self.run_fixture("missing-marker")
        self.assertFalse(passed)
        self.assertEqual(json.loads((output / "summary.json").read_text())["reason"], "watchdog timeout")

    def test_expected_failure_stages_no_fixture(self):
        passed, output = self.run_fixture("negative", "core-missing-fixture")
        self.assertTrue(passed)
        self.assertFalse((output / "fixture.txt").exists())
        self.assertEqual((output / "smoke.cfg").read_text(), "set ps2_smoke_config 37\nps2_smoke_command 77\n")
        self.assertEqual(json.loads((output / "run.json").read_text())["smoke_config_sha256"], runner.sha256(output / "smoke.cfg"))
        self.assertEqual(json.loads((output / "summary.json").read_text())["reason"], "expected failure observed")
        metadata = json.loads((output / "run.json").read_text())
        self.assertEqual((output / "fs-fixtures/large.bin").read_bytes(), b"Z" * 71680)
        self.assertTrue((output / "fs-fixtures/child").is_dir())
        self.assertEqual(metadata["filesystem_fixture_sha256"], {
            name: runner.sha256(output / name)
            for name in ("fs-fixtures/large.bin", "fs-fixtures/mixed.TxT")
        })

    def test_disabled_hostfs_rejected_without_edit(self):
        self.config.write_text("[EmuCore]\nHostFs = false\n")
        original = self.config.read_bytes()
        with self.assertRaisesRegex(ValueError, "HostFs"):
            runner.check_config(self.config)
        self.assertEqual(original, self.config.read_bytes())

    def test_existing_emulator_rejected_before_staging(self):
        with mock.patch.object(runner, "emulator_is_running", return_value=True):
            with self.assertRaisesRegex(ValueError, "already running"):
                self.run_fixture("pass")
        self.assertFalse((self.root / "results").exists())

    def test_mismatched_symbols_rejected_before_emulator_launch(self):
        self.symbols.write_bytes(b"different symbols")
        with self.assertRaisesRegex(ValueError, "does not match"):
            self.run_fixture("pass")

    def test_stale_flag_stamp_rejected_before_emulator_launch(self):
        (self.root / ".backend-flags.json").write_text('{"compiler":"changed"}')
        with self.assertRaisesRegex(ValueError, "flag stamp"):
            self.run_fixture("pass")

    def test_changed_map_rejected_before_emulator_launch(self):
        (self.root / "d3bfg.map").write_text("changed map")
        with self.assertRaisesRegex(ValueError, "artifact does not match"):
            self.run_fixture("pass")


class VersionTests(unittest.TestCase):
    def version_result(self, status: int, output: str) -> str:
        result = subprocess.CompletedProcess([], status, stdout=output, stderr="")
        with mock.patch.object(runner.subprocess, "run", return_value=result):
            return runner.emulator_version(Path("pcsx2"))

    def test_status_zero_with_version(self):
        self.assertEqual(self.version_result(0, "PCSX2 v2.6.3\n"), "PCSX2 v2.6.3")

    def test_actual_status_one_output(self):
        self.assertEqual(self.version_result(1, "PCSX2 v2.6.3\nhttps://pcsx2.net/\n\n"),
                         "PCSX2 v2.6.3\nhttps://pcsx2.net/")

    def test_nightly_version_suffix(self):
        self.assertIn("v2.6.3-138-gabcdef", self.version_result(1, "PCSX2 v2.6.3-138-gabcdef (macOS arm64)\n"))

    def test_error_status_with_version_rejected(self):
        with self.assertRaisesRegex(ValueError, "status 2"):
            self.version_result(2, "PCSX2 v2.6.3\n")

    def test_missing_version_rejected_for_both_accepted_statuses(self):
        for status in (0, 1):
            with self.subTest(status=status), self.assertRaisesRegex(ValueError, "validation"):
                self.version_result(status, "Qt initialization failed\n")

    def test_unanchored_or_incomplete_version_rejected(self):
        for output in ("fake PCSX2 v2.6.3", "PCSX2 v2.6", "PCSX2 v2.6.3 unexpected error"):
            with self.subTest(output=output), self.assertRaisesRegex(ValueError, "validation"):
                self.version_result(1, output)


if __name__ == "__main__":
    unittest.main()
