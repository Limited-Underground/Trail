#!/usr/bin/env python3
"""Adversarial tests for OT-114 OTRPE1/OTRPA1 validation."""

from __future__ import annotations

import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import crypto_radio_profile_evidence_admission as validator  # noqa: E402


def rejected(action) -> None:
    try:
        action()
    except (validator.ValidationError, KeyError, TypeError, ValueError):
        return
    raise AssertionError("expected rejection")


def fixtures() -> tuple[dict, dict, dict]:
    return (
        validator.load_pinned(validator.RECEIPT, validator.PINS["receipt"]),
        validator.load_pinned(validator.EVIDENCE, validator.PINS["evidence"]),
        validator.load_pinned(validator.ADMISSION, validator.PINS["admission"]),
    )


def test_exact_chain_passes() -> None:
    receipt, evidence, admission = fixtures()
    validator.validate_receipt(receipt)
    validator.validate_evidence(evidence)
    validator.validate_admission(admission)


def test_frame_hash_identity_and_timeout_drift_reject() -> None:
    receipt, _, _ = fixtures()
    for key, value in (("data_wire_sha256", "00" * 32), ("ack_timeout_ms", 2452)):
        changed = copy.deepcopy(receipt)
        changed["frames"][2][key] = value
        rejected(lambda changed=changed: validator.validate_receipt(changed))
    changed = copy.deepcopy(receipt)
    changed["frames"][3]["sequence"] = changed["frames"][2]["sequence"]
    rejected(lambda: validator.validate_receipt(changed))


def test_loss_session_and_privacy_drift_reject() -> None:
    receipt, _, _ = fixtures()
    for path, value in (("lost", 1), ("session_start_receipts", 3)):
        changed = copy.deepcopy(receipt)
        changed["summary"][path] = value
        rejected(lambda changed=changed: validator.validate_receipt(changed))
    changed = copy.deepcopy(receipt)
    changed["privacy"]["device_identifiers_recorded"] = True
    rejected(lambda: validator.validate_receipt(changed))


def test_evidence_cannot_self_admit_or_drop_artifacts() -> None:
    _, evidence, _ = fixtures()
    changed = copy.deepcopy(evidence)
    changed["acceptance"]["evidence_self_closes_requirement"] = True
    rejected(lambda: validator.validate_evidence(changed))
    changed = copy.deepcopy(evidence)
    changed["artifact_bindings"]["per_node_exact_image_context_receipts"] = 1
    rejected(lambda: validator.validate_evidence(changed))


def test_admission_closes_only_one_requirement_without_readiness() -> None:
    _, _, admission = fixtures()
    for key, value in (("only_closed_requirement", "all"), ("readiness_advanced", True),
                       ("new_executable_benchmark_plan_required", False)):
        changed = copy.deepcopy(admission)
        changed["admission"][key] = value
        rejected(lambda changed=changed: validator.validate_admission(changed))


def test_raw_duplicate_private_and_cli_are_fail_closed() -> None:
    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        for text in ('{"schema":"OTRPE1","schema":"OTRPE1"}',
                     '{"path":"C:\\\\Users\\\\operator\\\\secret"}'):
            path = temp / "hostile.json"
            path.write_text(text, encoding="utf-8")
            rejected(lambda path=path: validator.load_pinned(path, validator.PINS["evidence"]))
    result = subprocess.run([sys.executable, str(validator.__file__)], cwd=ROOT,
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    payload = json.loads(result.stdout)
    assert payload["closed_requirement"] == "direct_radio_mtu_phy_region_unresolved"
    assert payload["readiness_advanced"] is False


def main() -> int:
    tests = (test_exact_chain_passes, test_frame_hash_identity_and_timeout_drift_reject,
             test_loss_session_and_privacy_drift_reject,
             test_evidence_cannot_self_admit_or_drop_artifacts,
             test_admission_closes_only_one_requirement_without_readiness,
             test_raw_duplicate_private_and_cli_are_fail_closed)
    for test in tests:
        test()
    print(f"PASS: {len(tests)} OT-114 evidence/admission scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
