#!/usr/bin/env python3
"""Host-only tests for the immutable OT-107 configuration admission."""

from __future__ import annotations

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import crypto_final_candidate_build_configuration_admission as admission
import crypto_final_candidate_build_configuration_contract as contract


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def expect_error(action, label: str) -> None:
    try:
        action()
    except admission.AdmissionError:
        return
    raise AssertionError(f"expected OT-107 rejection: {label}")


def test_canonical_admission_is_exactly_bounded() -> None:
    result = admission.validate()
    assert result["accepted_candidate_count"] == 3
    assert result["acceptance_counts"] == {
        "source": 3,
        "api_config": 0,
        "candidate_import": 0,
    }
    assert result["current_blocker_count"] == 2
    assert result["readiness_accepted"] is False
    assert result["execution_authorized"] is False
    assert result["score_credit_added"] is False


def test_raw_anchors_are_exact() -> None:
    expected = {
        admission.PROPOSAL: admission.PROPOSAL_SHA256,
        admission.EVIDENCE: admission.EVIDENCE_SHA256,
        admission.ADMISSION: admission.ADMISSION_SHA256,
        admission.OT094: admission.OT094_SHA256,
        admission.OT105: admission.OT105_SHA256,
    }
    for path, digest in expected.items():
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest


def test_evidence_contract_rejects_extra_missing_and_mutated_fields() -> None:
    evidence = load(admission.EVIDENCE)
    proposal = load(admission.PROPOSAL)
    contract.validate_evidence_contract(evidence, proposal, admission.AdmissionError)
    changed = copy.deepcopy(evidence)
    changed["invented"] = False
    expect_error(
        lambda: contract.validate_evidence_contract(
            changed, proposal, admission.AdmissionError
        ),
        "extra root field",
    )
    changed = copy.deepcopy(evidence)
    changed["candidates"][0]["generation_receipts"][1][
        "generated_sdkconfig_sha256"
    ] = "00" * 32
    expect_error(
        lambda: contract.validate_evidence_contract(
            changed, proposal, admission.AdmissionError
        ),
        "candidate digest mutation",
    )
    changed = copy.deepcopy(evidence)
    changed["candidates"][1]["generation_receipts"][0][
        "required_effective_symbols"
    ].pop()
    expect_error(
        lambda: contract.validate_evidence_contract(
            changed, proposal, admission.AdmissionError
        ),
        "silently dropped Kconfig symbol",
    )
    changed = copy.deepcopy(evidence)
    changed["claims"]["candidate_compiled"] = True
    expect_error(
        lambda: contract.validate_evidence_contract(
            changed, proposal, admission.AdmissionError
        ),
        "overclaim",
    )


def test_historical_proposal_stays_unapproved_and_immutable() -> None:
    proposal = load(admission.PROPOSAL)
    admission._validate_proposal(proposal)
    changed = copy.deepcopy(proposal)
    changed["owner_approval"] = {
        "approved": True,
        "approved_proposal_raw_sha256": admission.PROPOSAL_SHA256,
    }
    expect_error(lambda: admission._validate_proposal(changed), "proposal rewrite")


def test_admission_rejects_blocker_authority_and_type_tampering() -> None:
    value = load(admission.ADMISSION)
    cases = []
    changed = copy.deepcopy(value)
    changed["current_two_blockers"].append(
        "final_candidate_build_configuration_unresolved"
    )
    cases.append(changed)
    changed = copy.deepcopy(value)
    changed["authority"]["benchmark_execution_authorized"] = True
    cases.append(changed)
    changed = copy.deepcopy(value)
    changed["claims"]["readiness_accepted"] = True
    cases.append(changed)
    changed = copy.deepcopy(value)
    changed["acceptance_counts"]["api_config"] = True
    cases.append(changed)
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "admission.json"
        for changed in cases:
            path.write_text(json.dumps(changed), encoding="utf-8")
            expect_error(
                lambda path=path: admission.validate(
                    path, enforce_digest=False
                ),
                "admission mutation",
            )


def test_duplicate_private_and_oversized_documents_fail_closed() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        duplicate = root / "duplicate.json"
        duplicate.write_text('{"schema":"OTCBCGE0","schema":"OTCBCGE0"}', encoding="utf-8")
        expect_error(lambda: admission._json(duplicate, None), "duplicate key")
        private = root / "private.json"
        private.write_text(
            json.dumps({"path": "C:" + "\\Users\\operator\\capture.txt"}),
            encoding="utf-8",
        )
        expect_error(lambda: admission._json(private, None), "private path")
        oversized = root / "oversized.json"
        oversized.write_bytes(b"x" * (admission.MAX_BYTES + 1))
        expect_error(lambda: admission._json(oversized, None), "oversized")


def test_cli_emits_only_the_public_bounded_result() -> None:
    completed = subprocess.run(
        [sys.executable, str(admission.__file__)],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 0, completed.stderr
    result = json.loads(completed.stdout)
    assert result["current_blocker_count"] == 2
    assert result["execution_authorized"] is False
    hostile = subprocess.run(
        [sys.executable, str(admission.__file__), "--private=C:\\Users\\operator"],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    assert hostile.returncode == 2
    assert hostile.stdout == ""
    assert "Users" not in hostile.stderr and "Traceback" not in hostile.stderr


def test_validator_has_no_execution_or_network_capability() -> None:
    for path in (Path(admission.__file__), Path(contract.__file__)):
        source = path.read_text(encoding="utf-8")
        for token in (
            "import socket",
            "import requests",
            "import urllib",
            "import subprocess",
            "os.system",
            "Start-Process",
        ):
            assert token not in source


def main() -> int:
    tests = (
        test_canonical_admission_is_exactly_bounded,
        test_raw_anchors_are_exact,
        test_evidence_contract_rejects_extra_missing_and_mutated_fields,
        test_historical_proposal_stays_unapproved_and_immutable,
        test_admission_rejects_blocker_authority_and_type_tampering,
        test_duplicate_private_and_oversized_documents_fail_closed,
        test_cli_emits_only_the_public_bounded_result,
        test_validator_has_no_execution_or_network_capability,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} OT-107 configuration-admission scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
