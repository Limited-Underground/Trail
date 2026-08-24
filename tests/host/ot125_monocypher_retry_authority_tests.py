#!/usr/bin/env python3
"""Host-only integrity tests for OT-124 abort and OT-125 retry authority."""

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
    "ot125_monocypher_retry_authority",
    TOOLS / "ot125_monocypher_retry_authority.py",
)
assert SPEC is not None and SPEC.loader is not None
authority = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = authority
SPEC.loader.exec_module(authority)


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Ot125MonocypherRetryAuthorityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.value = authority.load(authority.AUTHORITY_PATH, authority.AUTHORITY_PIN)
        self.parents = authority.validate_parent_files()

    def test_01_exact_authority_and_runner_binding_validate(self) -> None:
        result = authority.validate_authority(self.value, self.parents)
        self.assertTrue(result["phase_two_execution_authorized"])
        self.assertFalse(result["benchmark_executed"])
        self.assertEqual(result["attempt_count"], 1)
        self.assertFalse(result["reusable"])
        self.assertEqual(
            sha(TOOLS / "ot125_monocypher_retry_runner.py"),
            authority.RUNNER_RAW_SHA256,
        )

    def test_02_consumed_ot121_authority_is_rejected_as_ot125(self) -> None:
        old = CRYPTO / "OT-121-OT005-PHASE-TWO-EXECUTION-AUTHORITY-V1.json"
        with self.assertRaises(authority.ValidationError):
            authority.load(old, authority.AUTHORITY_PIN)
        self.assertEqual(sha(old), authority.CONSUMED_AUTHORITY_RAW_SHA256)

    def test_03_authority_mutations_fail_closed(self) -> None:
        mutations = (
            (("owner_authorization", "granted"), False),
            (("runner", "raw_sha256"), "0" * 64),
            (("execution", "attempt_count"), 2),
            (("execution", "application_offset"), 0),
            (("execution", "radio_allowed"), True),
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
        with tempfile.TemporaryDirectory(prefix="ot125-authority-parent-") as directory:
            prior_root = authority.ROOT
            try:
                authority.ROOT = Path(directory).resolve()
                with self.assertRaises(authority.ValidationError):
                    authority.validate_parent_files()
            finally:
                authority.ROOT = prior_root

    def test_05_duplicate_json_and_wrong_cli_path_are_sanitized(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot125-authority-json-") as directory:
            bad = Path(directory).resolve() / "private-authority.json"
            bad.write_text('{"schema":"x","schema":"y"}\n', encoding="ascii")
            with self.assertRaises(authority.ValidationError):
                authority.load(bad)
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                exit_code = authority.main(["--authority", str(bad)])
            self.assertEqual(exit_code, 2)
            self.assertNotIn(str(bad), stdout.getvalue())

    def test_06_abort_receipt_is_privacy_safe_and_admits_no_result(self) -> None:
        path = CRYPTO / "OT-124-OT005-MONOCYPHER-COMPARISON-EXECUTION-ABORT-RECEIPT-V0.json"
        self.assertEqual(sha(path), authority.ABORT_RAW_SHA256)
        value = json.loads(path.read_text(encoding="ascii"))
        self.assertTrue(value["all_touched_nodes_restored"])
        self.assertFalse(value["abort"]["benchmark_result_admitted"])
        self.assertTrue(value["authority"]["consumed_by_abort"])
        self.assertFalse(value["authority"]["reusable"])
        self.assertTrue(all(item is False for key, item in value["privacy"].items()
                            if key != "anonymous_role_labels_only"))
        self.assertTrue(value["privacy"]["anonymous_role_labels_only"])

    def test_07_accepted_ot123_runner_remains_immutable(self) -> None:
        self.assertEqual(
            sha(TOOLS / "ot123_monocypher_runner.py"),
            "f6c4070512d1c8d0d58bd386646f1e4098084b275610a7834dbf0ee0145eb1d1",
        )
        source = (TOOLS / "ot125_monocypher_retry_runner.py").read_text(
            encoding="utf-8"
        )
        self.assertIn("import ot125_monocypher_retry_authority", source)
        self.assertNotIn("import crypto_benchmark_execution_authority", source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
