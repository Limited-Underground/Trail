#!/usr/bin/env python3
"""Host-only integrity tests for OT-126 abort and OT-127 retry authority."""

from __future__ import annotations

import contextlib
import copy
import hashlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
CRYPTO = ROOT / "tests" / "benchmarks" / "crypto"
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location(
    "ot127_monocypher_retry_authority",
    TOOLS / "ot127_monocypher_retry_authority.py",
)
assert SPEC is not None and SPEC.loader is not None
authority = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = authority
SPEC.loader.exec_module(authority)


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Ot127MonocypherRetryAuthorityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.value = authority.load(authority.AUTHORITY_PATH, authority.AUTHORITY_PIN)
        self.parents = authority.validate_parent_files()

    def test_01_exact_authority_and_runner_binding_validate(self) -> None:
        result = authority.validate_authority(self.value, self.parents)
        self.assertTrue(result["phase_two_execution_authorized"])
        self.assertFalse(result["benchmark_executed"])
        self.assertEqual(result["attempt_count"], 1)
        self.assertEqual(result["capture_deadline_seconds"], 180)
        self.assertGreaterEqual(result["initial_frame_grace_seconds"], 10)
        self.assertFalse(result["reusable"])
        self.assertEqual(
            sha(TOOLS / "ot127_monocypher_retry_runner.py"),
            authority.RUNNER_RAW_SHA256,
        )

    def test_02_consumed_ot125_authority_is_rejected_as_ot127(self) -> None:
        old = CRYPTO / "OT-125-OT005-MONOCYPHER-CORRECTIVE-RETRY-AUTHORITY-V0.json"
        with self.assertRaises(authority.ValidationError):
            authority.load(old, authority.AUTHORITY_PIN)
        self.assertEqual(sha(old), authority.CONSUMED_AUTHORITY_RAW_SHA256)

    def test_03_authority_mutations_fail_closed(self) -> None:
        mutations = (
            (("owner_authorization", "granted"), False),
            (("runner", "raw_sha256"), "0" * 64),
            (("execution", "attempt_count"), 2),
            (("execution", "application_offset"), 0),
            (("execution", "all_preflight_devices_reset_before_journal"), False),
            (("execution", "all_preflight_devices_reset_on_preflight_failure"), False),
            (("execution", "fresh_reset_before_each_serial_open"), False),
            (("execution", "fresh_serial_open_per_capture_cycle"), False),
            (("execution", "initial_frame_grace_seconds"), 9),
            (("execution", "capture_deadline_seconds"), 181),
            (("execution", "capture_deadline_cli_override_allowed"), True),
            (("execution", "radio_allowed"), True),
            (("execution", "selection_allowed"), True),
            (("private_artifacts", "all_prior_private_artifacts_preserved"), False),
            (("consumption", "reusable"), True),
            (("claims", "benchmark_executed"), True),
            (("candidate", "selection_eligible"), True),
        )
        for path, replacement in mutations:
            with self.subTest(path=path):
                changed = copy.deepcopy(self.value)
                changed[path[0]][path[1]] = replacement
                with self.assertRaises(authority.ValidationError):
                    authority.validate_authority(changed, self.parents)

    def test_04_lineage_tampering_and_missing_parent_fail_closed(self) -> None:
        changed = dict(self.parents)
        changed["abort_receipt"] = "0" * 64
        with self.assertRaises(authority.ValidationError):
            authority.validate_authority(self.value, changed)
        with tempfile.TemporaryDirectory(prefix="ot127-authority-parent-") as directory:
            prior_root = authority.ROOT
            try:
                authority.ROOT = Path(directory).resolve()
                with self.assertRaises(authority.ValidationError):
                    authority.validate_parent_files()
            finally:
                authority.ROOT = prior_root

    def test_05_duplicate_json_and_wrong_cli_path_are_sanitized(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot127-authority-json-") as directory:
            bad = Path(directory).resolve() / "private-authority.json"
            bad.write_text('{"schema":"x","schema":"y"}\n', encoding="ascii")
            with self.assertRaises(authority.ValidationError):
                authority.load(bad)
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                exit_code = authority.main(["--authority", str(bad)])
            self.assertEqual(exit_code, 2)
            self.assertNotIn(str(bad), stdout.getvalue())

    def test_06_abort_receipt_is_exact_restored_and_consumed_parent(self) -> None:
        path = (
            CRYPTO
            / "OT-126-OT005-MONOCYPHER-CORRECTIVE-RETRY-EXECUTION-ABORT-RECEIPT-V0.json"
        )
        self.assertEqual(sha(path), authority.ABORT_RAW_SHA256)
        value = json.loads(path.read_text(encoding="ascii"))
        self.assertTrue(value["all_touched_nodes_restored"])
        self.assertTrue(value["all_devices_trail_application_verified"])
        self.assertTrue(value["all_devices_runtime_reset_complete"])
        self.assertTrue(value["two_usb_endpoints_returned"])
        self.assertTrue(value["abort"]["root_cause_confirmed"])
        self.assertFalse(value["abort"]["benchmark_result_admitted"])
        self.assertTrue(value["authority"]["consumed_by_abort"])
        self.assertFalse(value["authority"]["reusable"])

    def test_07_runner_has_fixed_timing_and_no_cli_override(self) -> None:
        source = (TOOLS / "ot127_monocypher_retry_runner.py").read_text(
            encoding="utf-8"
        )
        self.assertIn("FIRST_FRAME_GRACE_SECONDS = 10.0", source)
        self.assertIn("CAPTURE_DEADLINE_SECONDS = 180.0", source)
        self.assertIn("self.hard_reset(private_port)", source)
        self.assertIn("_verify_installed_application_preflight(config, transport, restore)", source)
        self.assertNotIn("--capture-deadline", source)
        self.assertNotIn("--first-frame-grace", source)

    def test_08_no_radio_or_selection_authority_and_prior_private_preserved(self) -> None:
        execution = self.value["execution"]
        self.assertFalse(execution["radio_allowed"])
        self.assertFalse(execution["selection_allowed"])
        self.assertFalse(self.value["candidate"]["selection_eligible"])
        self.assertTrue(
            self.value["private_artifacts"]["all_prior_private_artifacts_preserved"]
        )
        self.assertTrue(self.value["consumption"]["consumed_on_success_or_abort"])
        self.assertFalse(self.value["consumption"]["reusable"])
        self.assertTrue(all(item is False for item in self.value["claims"].values()))


if __name__ == "__main__":
    unittest.main(verbosity=2)
