#!/usr/bin/env python3
"""Adversarial host tests for the OT-108 API/config acceptance contract."""

from __future__ import annotations

import copy
import hashlib
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

import crypto_api_config_acceptance_contract as acceptance  # noqa: E402
import crypto_benchmark as benchmark  # noqa: E402
import crypto_benchmark_baseline as baseline  # noqa: E402
import crypto_benchmark_readiness as readiness  # noqa: E402
import crypto_candidate_source_lock as source_lock  # noqa: E402
import crypto_final_candidate_build_configuration_admission as configuration  # noqa: E402
import crypto_mbedtls_static_eligibility as mbedtls  # noqa: E402


CONTRACT_PATH = ROOT / "tests" / "benchmarks" / "crypto" / "OT-108-OT005-PER-CANDIDATE-API-CONFIG-ACCEPTANCE-CONTRACT-V1.json"
PLAN_PATH = ROOT / "tests" / "benchmarks" / "crypto" / "OT-005-CRYPTO-BENCHMARK-PLAN-V0.json"
BASELINE_PATH = ROOT / "tests" / "benchmarks" / "crypto" / "OT-093-OT005-BUILD-BASELINE-V0.json"
LEGACY_PATHS = {
    "OT-094-OT005-CANDIDATE-READINESS-V0.json": "bb607158cbe8ac95a470f0a6c87fbb6d8d986259cf86540b14245fc1167dc7ae",
    "OT-095-OT005-CANDIDATE-SOURCE-LOCK-ADMISSION-V0.json": "3fb904e1d5770613ec5d84560ea91dc3ec318a8a96c89c7d4333aa229267bab8",
    "OT-096-OT005-MBEDTLS-STATIC-ELIGIBILITY-V0.json": "1a49125c3b236a5b744c0ca198e5a1f30b1509d9e58d86cce836f70fb1f10030",
    "OT-107-OT005-FINAL-CANDIDATE-BUILD-CONFIGURATION-PROPOSAL-V0.json": "f9072a602a9c139b1e7728735db04cc270720bc37e0429c22bcdb0cd56202a15",
    "OT-107-OT005-FINAL-CANDIDATE-BUILD-CONFIGURATION-EVIDENCE-V0.json": "0c1b8cb574a210c6123b82b565e6ea8e12cee59bacd6ab4b94b293ddf9d2dfbc",
    "OT-107-OT005-FINAL-CANDIDATE-BUILD-CONFIGURATION-ADMISSION-DELTA-V0.json": "3d71dfb02b6fd25e0881ac63cc085174c2cdd7e5ac2cd1e12c320ec34928f5a2",
}


def expect_error(action, label: str) -> None:
    try:
        action()
    except acceptance.ValidationError:
        return
    raise AssertionError(f"expected OT-108 rejection: {label}")


def expect_boundary_error(action, label: str) -> None:
    try:
        action()
    except (acceptance.ValidationError, readiness.ValidationError):
        return
    raise AssertionError(f"expected OT-108 readiness-boundary rejection: {label}")


def contract() -> dict:
    return acceptance.load(CONTRACT_PATH)


def evidence(candidate_id: str, eligible_operations: set[str] | None = None) -> dict:
    candidate = acceptance.CANDIDATE_BY_ID[candidate_id]
    eligible = set(acceptance.OPERATIONS) if eligible_operations is None else eligible_operations
    complete = eligible == set(acceptance.OPERATIONS)
    operation_results = []
    for operation in acceptance.OPERATIONS:
        is_eligible = operation in eligible
        operation_results.append({
            "operation_id": operation,
            "state": "eligible" if is_eligible else "unavailable",
            "evidence_sha256": hashlib.sha256(f"{candidate_id}:{operation}".encode("ascii")).hexdigest() if is_eligible else None,
        })
    return {
        "schema": "OTCAPI0", "version": 2,
        "artifact_kind": "candidate_api_config_evidence",
        "evidence_id": f"OT-108-OT005-{candidate_id.upper().replace('_', '-')}-API-CONFIG-EVIDENCE-V2",
        "recorded_date": "2026-08-21",
        "contract_sha256": acceptance.canonical_sha256(contract()),
        "candidate_id": candidate_id, "role": candidate["role"],
        "source_evidence_sha256": candidate["source_evidence_sha256"],
        "final_sdkconfig_sha256": candidate["generated_sdkconfig_sha256"],
        "operation_results": operation_results,
        "coverage_state": "complete_selectable" if complete else "comparison_partial",
        "comparison_measurement_eligible": True,
        "selection_eligible": complete,
        "result": "complete_api_config_eligible" if complete else "partial_comparison_only",
        "execution_authorized": False, "selection_authorized": False,
        "score_credit_added": False,
    }


def test_canonical_contract_is_exactly_bounded() -> None:
    value = contract()
    assert acceptance.validate_contract(value) == {
        "schema": "OTCAC0", "version": 1,
        "contract_id": acceptance.CONTRACT_ID,
        "status": "per_candidate_api_config_acceptance_contract_frozen_host_only",
        "public_result": acceptance.PUBLIC_RESULT,
        "contract_sha256": "ccdb11e19031a4a01c717e30c172332c5e71ca79ee2b36b721277e55ca9a6c22",
        "source_count": 3, "accepted_api_config_count": 0,
        "candidate_import_count": 0, "blocker_count": 2,
        "execution_authorized": False, "selection_authorized": False,
        "score_credit_added": False,
    }
    assert len({item["generated_sdkconfig_sha256"] for item in value["candidates"]}) == 3
    assert value["acceptance_counts"] == {"source": 3, "api_config": 0, "candidate_import": 0}
    assert not any(value["authority"].values()) and not any(value["claims"].values())


def test_each_candidate_is_bound_to_its_distinct_sdkconfig() -> None:
    for candidate in acceptance.CANDIDATE_RECORDS:
        value = evidence(candidate["candidate_id"])
        result = acceptance.validate_evidence_structure(value, contract())
        assert result["candidate_id"] == candidate["candidate_id"]
        assert result["final_sdkconfig_sha256"] == candidate["generated_sdkconfig_sha256"]
        replacements = [
            "00" * 32,
            next(other["generated_sdkconfig_sha256"] for other in acceptance.CANDIDATE_RECORDS if other["candidate_id"] != candidate["candidate_id"]),
        ]
        for replacement in replacements:
            changed = copy.deepcopy(value)
            changed["final_sdkconfig_sha256"] = replacement
            expect_error(lambda changed=changed: acceptance.validate_evidence_structure(changed, contract()), "candidate sdkconfig substitution")


def test_complete_and_partial_dispositions_are_exact_partitions() -> None:
    complete = acceptance.validate_evidence_structure(evidence("espressif_libsodium"), contract())
    assert complete["coverage_state"] == "complete_selectable"
    assert complete["eligible_operation_count"] == len(acceptance.OPERATIONS)
    assert complete["selection_eligible"] is True
    supported = {"x25519", "sha256", "hkdf_sha256", "chacha20poly1305_encrypt", "chacha20poly1305_decrypt"}
    partial_value = evidence("esp_idf_mbedtls_psa", supported)
    partial = acceptance.validate_evidence_structure(partial_value, contract())
    assert partial["coverage_state"] == "comparison_partial"
    assert partial["eligible_operation_count"] == 5
    assert partial["comparison_measurement_eligible"] is True
    assert partial["selection_eligible"] is False
    expect_error(lambda: acceptance.validate_evidence_structure(evidence("esp_idf_mbedtls_psa", set()), contract()), "empty partial set")
    expect_error(lambda: acceptance.validate_evidence_structure(evidence("espressif_libsodium", {"sha256"}), contract()), "partial primary")
    reordered = copy.deepcopy(partial_value)
    reordered["operation_results"][0], reordered["operation_results"][1] = reordered["operation_results"][1], reordered["operation_results"][0]
    expect_error(lambda: acceptance.validate_evidence_structure(reordered, contract()), "operation reordering")


def test_partial_comparison_cannot_claim_selection_or_execution() -> None:
    base = evidence("esp_idf_mbedtls_psa", {"x25519", "sha256"})
    for field in ("selection_eligible", "selection_authorized", "execution_authorized", "score_credit_added"):
        changed = copy.deepcopy(base)
        changed[field] = True
        expect_error(lambda changed=changed: acceptance.validate_evidence_structure(changed, contract()), f"partial comparison {field}")
    changed = copy.deepcopy(base)
    changed["coverage_state"] = "complete_selectable"
    expect_error(lambda: acceptance.validate_evidence_structure(changed, contract()), "partial candidate promoted to selectable")


def test_cross_candidate_source_and_configuration_substitution_fails() -> None:
    changed = evidence("esp_idf_mbedtls_psa", {"x25519", "sha256"})
    monocypher = acceptance.CANDIDATE_BY_ID["monocypher"]
    changed["candidate_id"] = "monocypher"
    changed["role"] = monocypher["role"]
    changed["source_evidence_sha256"] = monocypher["source_evidence_sha256"]
    expect_error(lambda: acceptance.validate_evidence_structure(changed, contract()), "cross-candidate configuration substitution")
    changed = evidence("monocypher", {"x25519", "sha256"})
    changed["source_evidence_sha256"] = acceptance.CANDIDATE_BY_ID["esp_idf_mbedtls_psa"]["source_evidence_sha256"]
    expect_error(lambda: acceptance.validate_evidence_structure(changed, contract()), "cross-candidate source substitution")


def test_unavailable_operations_cannot_carry_execution_evidence() -> None:
    value = evidence("esp_idf_mbedtls_psa", {"x25519", "sha256"})
    unavailable = next(item for item in value["operation_results"] if item["state"] == "unavailable")
    unavailable["evidence_sha256"] = hashlib.sha256(b"unsupported execution").hexdigest()
    expect_error(lambda: acceptance.validate_evidence_structure(value, contract()), "unsupported operation execution evidence")
    value = evidence("esp_idf_mbedtls_psa", {"x25519", "sha256"})
    eligible = [item for item in value["operation_results"] if item["state"] == "eligible"]
    eligible[1]["evidence_sha256"] = eligible[0]["evidence_sha256"]
    expect_error(lambda: acceptance.validate_evidence_structure(value, contract()), "cross-operation evidence reuse")


def test_independent_admission_registry_is_empty_and_fail_closed() -> None:
    assert acceptance.ACCEPTED_API_CONFIG_EVIDENCE_SHA256 == {candidate["candidate_id"]: frozenset() for candidate in acceptance.CANDIDATE_RECORDS}
    value = evidence("espressif_libsodium")
    acceptance.validate_evidence_structure(value, contract())
    expect_error(lambda: acceptance.validate_evidence(value, contract()), "self-authored evidence without independent digest admission")
    changed = contract()
    changed["accepted_api_config_evidence_sha256"]["espressif_libsodium"] = [acceptance.canonical_sha256(value)]
    expect_error(lambda: acceptance.validate_contract(changed), "self-amended admission registry")


def test_readiness_successor_hook_is_bounded_and_requires_exact_candidate_coverage() -> None:
    boundary = readiness.validate_per_candidate_api_config_boundary(contract())
    assert boundary["source_count"] == 3
    assert boundary["accepted_api_config_count"] == 0
    assert boundary["candidate_import_count"] == 0
    assert boundary["blocker_count"] == 2
    assert boundary["fully_resolved"] is False
    assert boundary["execution_authorized"] is False
    assert boundary["selection_authorized"] is False
    assert boundary["score_credit_added"] is False
    expect_boundary_error(
        lambda: readiness.validate_per_candidate_api_config_boundary(contract(), []),
        "explicit empty evidence list",
    )

    documents = [
        evidence("espressif_libsodium"),
        evidence(
            "esp_idf_mbedtls_psa",
            {"x25519", "sha256", "hkdf_sha256", "chacha20poly1305_encrypt", "chacha20poly1305_decrypt"},
        ),
        evidence("monocypher"),
    ]
    original = acceptance.ACCEPTED_API_CONFIG_EVIDENCE_SHA256
    try:
        acceptance.ACCEPTED_API_CONFIG_EVIDENCE_SHA256 = {
            item["candidate_id"]: frozenset({acceptance.canonical_sha256(item)})
            for item in documents
        }
        admitted = readiness.validate_per_candidate_api_config_boundary(contract(), documents)
        assert admitted["accepted_api_config_count"] == 3
        assert admitted["fully_resolved"] is False
        assert admitted["execution_authorized"] is False
        assert admitted["selection_authorized"] is False
        expect_boundary_error(
            lambda: readiness.validate_per_candidate_api_config_boundary(contract(), documents[:2]),
            "missing candidate",
        )
        duplicated = [documents[0], documents[0], documents[2]]
        expect_boundary_error(
            lambda: readiness.validate_per_candidate_api_config_boundary(contract(), duplicated),
            "duplicate candidate",
        )
    finally:
        acceptance.ACCEPTED_API_CONFIG_EVIDENCE_SHA256 = original


def test_historical_artifacts_and_legacy_validation_are_preserved() -> None:
    crypto_root = ROOT / "tests" / "benchmarks" / "crypto"
    for name, expected in LEGACY_PATHS.items():
        assert hashlib.sha256((crypto_root / name).read_bytes()).hexdigest() == expected
    plan = benchmark._load(PLAN_PATH)
    baseline_value = baseline.load(BASELINE_PATH)
    readiness_result = readiness.validate(readiness.load(crypto_root / "OT-094-OT005-CANDIDATE-READINESS-V0.json"), plan, baseline_value)
    assert readiness_result["blocker_count"] == 6 and readiness_result["execution_authorized"] is False
    source_result = source_lock.validate_contract(source_lock.load(crypto_root / "OT-095-OT005-CANDIDATE-SOURCE-LOCK-ADMISSION-V0.json"))
    assert source_result["accepted_source_lock_count"] == 0 and source_result["readiness_advanced"] is False
    mbedtls_result = mbedtls.validate(mbedtls.load(crypto_root / "OT-096-OT005-MBEDTLS-STATIC-ELIGIBILITY-V0.json"))
    assert mbedtls_result["present_operation_count"] == 5
    assert mbedtls_result["candidate_api_config_eligible"] is False
    configuration_result = configuration.validate()
    assert configuration_result["current_blocker_count"] == 2
    assert configuration_result["execution_authorized"] is False


def main() -> int:
    tests = (
        test_canonical_contract_is_exactly_bounded,
        test_each_candidate_is_bound_to_its_distinct_sdkconfig,
        test_complete_and_partial_dispositions_are_exact_partitions,
        test_partial_comparison_cannot_claim_selection_or_execution,
        test_cross_candidate_source_and_configuration_substitution_fails,
        test_unavailable_operations_cannot_carry_execution_evidence,
        test_independent_admission_registry_is_empty_and_fail_closed,
        test_readiness_successor_hook_is_bounded_and_requires_exact_candidate_coverage,
        test_historical_artifacts_and_legacy_validation_are_preserved,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} OT-108 API/config acceptance-contract scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
