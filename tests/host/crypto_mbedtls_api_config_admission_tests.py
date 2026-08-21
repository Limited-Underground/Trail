#!/usr/bin/env python3
"""Adversarial host tests for the immutable OT-109 API/config admission."""

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
CRYPTO = ROOT / "tests" / "benchmarks" / "crypto"
sys.path.insert(0, str(TOOLS))

import crypto_api_config_acceptance_contract as acceptance  # noqa: E402
import crypto_benchmark as benchmark  # noqa: E402
import crypto_benchmark_baseline as baseline  # noqa: E402
import crypto_benchmark_readiness as readiness  # noqa: E402
import crypto_candidate_source_lock as source_lock  # noqa: E402
import crypto_final_candidate_build_configuration_admission as configuration  # noqa: E402
import crypto_mbedtls_api_config_admission as admission_validator  # noqa: E402
import crypto_mbedtls_api_config_evidence as evidence_validator  # noqa: E402
import crypto_mbedtls_static_eligibility as static_eligibility  # noqa: E402


BUNDLE_PATH = CRYPTO / "OT-109-OT005-MBEDTLS-PSA-API-CONFIG-OPERATION-EVIDENCE-V0.json"
API_PATH = CRYPTO / "OT-109-OT005-MBEDTLS-PSA-API-CONFIG-EVIDENCE-V2.json"
ADMISSION_PATH = CRYPTO / "OT-109-OT005-MBEDTLS-PSA-API-CONFIG-ADMISSION-DELTA-V0.json"
CONTRACT_PATH = CRYPTO / "OT-108-OT005-PER-CANDIDATE-API-CONFIG-ACCEPTANCE-CONTRACT-V1.json"
EXPECTED = {
    BUNDLE_PATH: ("ea85f548deee36ca34241747cdf567036febfb9eecd88d9e134d3383edf2379e", "6a17a6f5a753a19d2d78d7cb6f0c757ef9791e0bf2e953e27afc3eccb04f27ed"),
    API_PATH: ("67532e10704d02489b72a72ef55607743c00a5bd8504276750931b5d986f6155", "22975ac7fbd3c9faab1ae0c9fa952a58dc4a7a893de3cc74604182b3492fe1f8"),
    ADMISSION_PATH: ("0311b8b264d264d4a005ac9be8531c06175521362ed54ac9f40bdef1fd7a5df0", "fed7b009a97a60678b2dfbba23d933b974aa0fec46d0b460ac7eb221e91931dd"),
}
HISTORICAL_RAW = {
    "OT-094-OT005-CANDIDATE-READINESS-V0.json": "bb607158cbe8ac95a470f0a6c87fbb6d8d986259cf86540b14245fc1167dc7ae",
    "OT-095-OT005-CANDIDATE-SOURCE-LOCK-ADMISSION-V0.json": "3fb904e1d5770613ec5d84560ea91dc3ec318a8a96c89c7d4333aa229267bab8",
    "OT-096-OT005-MBEDTLS-STATIC-ELIGIBILITY-V0.json": "1a49125c3b236a5b744c0ca198e5a1f30b1509d9e58d86cce836f70fb1f10030",
    "OT-105-OT005-MBEDTLS-PSA-SOURCE-EVIDENCE-V0.json": "ae12ad7da6702ac85092e9cb8ad793b749871153fadee8b1a276e5a46b036e49",
    "OT-105-OT005-MBEDTLS-PSA-SOURCE-LOCK-ADMISSION-DELTA-V0.json": "26b6acdc9928eb9510a0baed53c609a4f9a23288155636c6462747745f28ac85",
    "OT-107-OT005-FINAL-CANDIDATE-BUILD-CONFIGURATION-EVIDENCE-V0.json": "0c1b8cb574a210c6123b82b565e6ea8e12cee59bacd6ab4b94b293ddf9d2dfbc",
    "OT-107-OT005-FINAL-CANDIDATE-BUILD-CONFIGURATION-ADMISSION-DELTA-V0.json": "3d71dfb02b6fd25e0881ac63cc085174c2cdd7e5ac2cd1e12c320ec34928f5a2",
    "OT-108-OT005-PER-CANDIDATE-API-CONFIG-ACCEPTANCE-CONTRACT-V1.json": "575e8747cdba643f2abb6f3252a62adbf3f12a9faaa46ca1d4bf4ce8bd9d23f3",
}


def artifacts() -> tuple[dict, dict, dict, dict]:
    return (
        evidence_validator.load(BUNDLE_PATH),
        evidence_validator.load(API_PATH),
        admission_validator.load(ADMISSION_PATH),
        acceptance.load(CONTRACT_PATH),
    )


def expect_error(action, contains: str) -> None:
    try:
        action()
    except (
        evidence_validator.ValidationError,
        admission_validator.ValidationError,
        acceptance.ValidationError,
        readiness.ValidationError,
    ) as exc:
        assert contains in str(exc), (contains, str(exc))
        return
    raise AssertionError(f"expected OT-109 rejection containing {contains!r}")


def test_canonical_artifacts_and_exact_parent_chain() -> None:
    bundle, api, admission, contract = artifacts()
    bundle_result = evidence_validator.validate_operation_bundle(bundle)
    api_result = evidence_validator.validate_api_evidence(api, bundle, contract)
    result = admission_validator.validate(admission, bundle, api, contract)
    assert bundle_result["eligible_operation_count"] == 5
    assert bundle_result["coverage_state"] == "comparison_partial"
    assert bundle_result["selection_eligible"] is False
    assert set(bundle_result["operation_evidence_sha256"]) == evidence_validator.ELIGIBLE_OPERATIONS
    assert api_result["eligible_operation_count"] == 5
    assert api_result["selection_eligible"] is False
    assert result == {
        "schema": "OTCAPIA0", "version": 0,
        "admission_id": admission_validator.ADMISSION_ID,
        "accepted_api_config_count": 1, "source_count": 3,
        "candidate_import_count": 0, "blocker_count": 1,
        "fully_resolved": False, "execution_authorized": False,
        "selection_authorized": False, "score_credit_added": False,
        "admission_sha256": EXPECTED[ADMISSION_PATH][1],
    }
    assert evidence_validator.EXPECTED_BUNDLE_SHA256 == EXPECTED[BUNDLE_PATH][1]
    assert evidence_validator.EXPECTED_API_EVIDENCE_SHA256 == EXPECTED[API_PATH][1]
    assert admission_validator.EXPECTED_ADMISSION_SHA256 == EXPECTED[ADMISSION_PATH][1]
    assert admission["parents"] == {
        "otcac0_v1_raw_sha256": HISTORICAL_RAW[CONTRACT_PATH.name],
        "otcac0_v1_canonical_sha256": acceptance.canonical_sha256(contract),
        "otcapioe0_v0_raw_sha256": EXPECTED[BUNDLE_PATH][0],
        "otcapioe0_v0_canonical_sha256": EXPECTED[BUNDLE_PATH][1],
        "otcapi0_v2_raw_sha256": EXPECTED[API_PATH][0],
        "otcapi0_v2_canonical_sha256": EXPECTED[API_PATH][1],
    }


def test_exact_five_of_eight_disposition_is_nonselectable() -> None:
    bundle, api, _, contract = artifacts()
    expected_states = {
        operation: "eligible" if operation in evidence_validator.ELIGIBLE_OPERATIONS else "unavailable"
        for operation in evidence_validator.OPERATIONS
    }
    assert {item["operation_id"]: item["state"] for item in bundle["operation_records"]} == expected_states
    assert {item["operation_id"]: item["state"] for item in api["operation_results"]} == expected_states
    assert api["coverage_state"] == "comparison_partial"
    assert api["comparison_measurement_eligible"] is True
    assert api["selection_eligible"] is False
    assert api["execution_authorized"] is False
    assert api["selection_authorized"] is False
    assert bundle["claims"]["api_config_evidence_generated"] is True
    assert not any(
        value
        for field, value in bundle["claims"].items()
        if field != "api_config_evidence_generated"
    )
    evidence_validator.validate_api_evidence(api, bundle, contract)
    changed = copy.deepcopy(bundle)
    changed["claims"]["api_config_evidence_generated"] = False
    expect_error(lambda: evidence_validator.validate_operation_bundle(changed), "claim disposition")
    for field in bundle["claims"]:
        if field == "api_config_evidence_generated":
            continue
        changed = copy.deepcopy(bundle)
        changed["claims"][field] = True
        expect_error(
            lambda changed=changed: evidence_validator.validate_operation_bundle(changed),
            "claim disposition",
        )


def test_configuration_reproduction_and_candidate_bindings_fail_closed() -> None:
    bundle, _, _, _ = artifacts()
    mutations = []
    changed = copy.deepcopy(bundle); changed["parents"]["otcmse0_v0_canonical_sha256"] = "00" * 32; mutations.append((changed, "parent or candidate binding"))
    changed = copy.deepcopy(bundle); changed["candidate"]["generated_sdkconfig_sha256"] = "11" * 32; mutations.append((changed, "parent or candidate binding"))
    changed = copy.deepcopy(bundle); changed["configuration_reproduction"]["generated_sdkconfig_sha256"] = "22" * 32; mutations.append((changed, "configuration reproduction"))
    changed = copy.deepcopy(bundle); changed["configuration_reproduction"]["required_effective_symbols"].pop(); mutations.append((changed, "effective symbol lists"))
    changed = copy.deepcopy(bundle); changed["configuration_reproduction"]["forbidden_effective_symbols"].append("CONFIG_INVENTED=y"); mutations.append((changed, "effective symbol lists"))
    changed = copy.deepcopy(bundle); changed["configuration_reproduction"]["runs"][0]["exit_code"] = 1; mutations.append((changed, "configuration run"))
    changed = copy.deepcopy(bundle); changed["configuration_reproduction"]["runs"][1]["candidate_compiled"] = True; mutations.append((changed, "must be false"))
    for changed, message in mutations:
        expect_error(lambda changed=changed: evidence_validator.validate_operation_bundle(changed), message)


def test_operation_order_facts_and_unavailable_states_fail_closed() -> None:
    bundle, _, _, _ = artifacts()
    cases = []
    changed = copy.deepcopy(bundle); changed["operation_records"][0], changed["operation_records"][1] = changed["operation_records"][1], changed["operation_records"][0]; cases.append(changed)
    changed = copy.deepcopy(bundle); changed["operation_records"][0]["state"] = "eligible"; cases.append(changed)
    changed = copy.deepcopy(bundle); changed["operation_records"][0]["api_symbols"] = ["invented_ed25519"]; cases.append(changed)
    changed = copy.deepcopy(bundle); changed["operation_records"][2]["source_anchor_ids"] = changed["operation_records"][3]["source_anchor_ids"]; cases.append(changed)
    changed = copy.deepcopy(bundle); changed["operation_records"][2]["source_state"] = "invented"; cases.append(changed)
    changed = copy.deepcopy(bundle); changed["operation_records"][2]["final_sdkconfig_sha256"] = "33" * 32; cases.append(changed)
    for changed in cases:
        expect_error(lambda changed=changed: evidence_validator.validate_operation_bundle(changed), "operation")


def test_api_evidence_rejects_cross_candidate_unsupported_and_selection_claims() -> None:
    bundle, api, _, contract = artifacts()
    cases = []
    changed = copy.deepcopy(api); changed["final_sdkconfig_sha256"] = acceptance.CANDIDATE_BY_ID["monocypher"]["generated_sdkconfig_sha256"]; cases.append(changed)
    changed = copy.deepcopy(api); changed["source_evidence_sha256"] = acceptance.CANDIDATE_BY_ID["monocypher"]["source_evidence_sha256"]; cases.append(changed)
    changed = copy.deepcopy(api); changed["operation_results"][0]["evidence_sha256"] = "44" * 32; cases.append(changed)
    changed = copy.deepcopy(api); changed["operation_results"][2]["evidence_sha256"] = changed["operation_results"][3]["evidence_sha256"]; cases.append(changed)
    changed = copy.deepcopy(api); changed["selection_eligible"] = True; cases.append(changed)
    changed = copy.deepcopy(api); changed["selection_authorized"] = True; cases.append(changed)
    changed = copy.deepcopy(api); changed["execution_authorized"] = True; cases.append(changed)
    for changed in cases:
        expect_error(lambda changed=changed: evidence_validator.validate_api_evidence(changed, bundle, contract), "")


def test_independent_admission_counts_and_blocker_cannot_be_self_amended() -> None:
    bundle, api, admission, contract = artifacts()
    assert acceptance.ACCEPTED_API_CONFIG_EVIDENCE_SHA256["esp_idf_mbedtls_psa"] == frozenset()
    expect_error(lambda: acceptance.validate_evidence(api, contract), "not independently accepted")
    admission_validator.validate(admission, bundle, api, contract)
    cases = []
    changed = copy.deepcopy(admission); changed["parents"]["otcapi0_v2_canonical_sha256"] = "55" * 32; cases.append((changed, "parent mismatch"))
    changed = copy.deepcopy(admission); changed["accepted_api_config_evidence_sha256"]["espressif_libsodium"] = [EXPECTED[API_PATH][1]]; cases.append((changed, "accepted registry"))
    changed = copy.deepcopy(admission); changed["acceptance_counts"]["api_config"] = 2; cases.append((changed, "acceptance counts"))
    changed = copy.deepcopy(admission); changed["closed_by_this_delta"]["closure_evidence_sha256"] = "66" * 32; cases.append((changed, "closed blocker"))
    changed = copy.deepcopy(admission); changed["current_one_blocker"] = []; cases.append((changed, "one-blocker"))
    changed = copy.deepcopy(admission); changed["authority"]["benchmark_execution_authorized"] = True; cases.append((changed, "exact false"))
    for changed, message in cases:
        expect_error(lambda changed=changed: admission_validator.validate(changed, bundle, api, contract), message)


def test_readiness_successor_reports_only_the_incremental_admission() -> None:
    bundle, api, admission, contract = artifacts()
    result = readiness.validate_mbedtls_api_config_admission_boundary(admission, bundle, api, contract)
    assert result == {
        "schema": "OTCAPIA0", "version": 0,
        "admission_id": admission_validator.ADMISSION_ID,
        "source_count": 3, "accepted_api_config_count": 1,
        "candidate_import_count": 0, "blocker_count": 1,
        "fully_resolved": False, "execution_authorized": False,
        "selection_authorized": False, "score_credit_added": False,
        "admission_sha256": EXPECTED[ADMISSION_PATH][1],
    }
    changed = copy.deepcopy(admission)
    changed["acceptance_counts"]["candidate_import"] = 1
    expect_error(lambda: readiness.validate_mbedtls_api_config_admission_boundary(changed, bundle, api, contract), "acceptance counts")


def test_historical_artifacts_and_legacy_validators_are_immutable() -> None:
    for name, digest in HISTORICAL_RAW.items():
        assert hashlib.sha256((CRYPTO / name).read_bytes()).hexdigest() == digest
    for path, (raw_digest, canonical_digest) in EXPECTED.items():
        value = admission_validator.load(path) if path == ADMISSION_PATH else evidence_validator.load(path)
        assert hashlib.sha256(path.read_bytes()).hexdigest() == raw_digest
        assert evidence_validator.canonical_sha256(value) == canonical_digest
    plan = benchmark._load(CRYPTO / "OT-005-CRYPTO-BENCHMARK-PLAN-V0.json")
    baseline_value = baseline.load(CRYPTO / "OT-093-OT005-BUILD-BASELINE-V0.json")
    old_readiness = readiness.validate(readiness.load(CRYPTO / "OT-094-OT005-CANDIDATE-READINESS-V0.json"), plan, baseline_value)
    assert old_readiness["blocker_count"] == 6 and old_readiness["execution_authorized"] is False
    old_source = source_lock.validate_contract(source_lock.load(CRYPTO / "OT-095-OT005-CANDIDATE-SOURCE-LOCK-ADMISSION-V0.json"))
    assert old_source["accepted_source_lock_count"] == 0
    old_static = static_eligibility.validate(static_eligibility.load(CRYPTO / "OT-096-OT005-MBEDTLS-STATIC-ELIGIBILITY-V0.json"))
    assert old_static["present_operation_count"] == 5 and old_static["candidate_api_config_eligible"] is False
    assert configuration.validate()["current_blocker_count"] == 2
    old_contract = acceptance.validate_contract(acceptance.load(CONTRACT_PATH))
    assert old_contract["accepted_api_config_count"] == 0 and old_contract["blocker_count"] == 2


def test_clis_are_bounded_and_hostile_arguments_are_private() -> None:
    commands = (
        [sys.executable, str(evidence_validator.__file__), "--bundle", str(BUNDLE_PATH), "--api-evidence", str(API_PATH), "--contract", str(CONTRACT_PATH)],
        [sys.executable, str(admission_validator.__file__), "--admission", str(ADMISSION_PATH), "--bundle", str(BUNDLE_PATH), "--api-evidence", str(API_PATH), "--contract", str(CONTRACT_PATH)],
    )
    for command in commands:
        completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        assert completed.returncode == 0, completed.stderr
        output = json.loads(completed.stdout)
        assert output["execution_authorized"] is False
        assert output["score_credit_added"] is False
        hostile = subprocess.run(command[:2] + ["--private=C:\\Users\\operator\\secret.json"], cwd=ROOT, capture_output=True, text=True)
        assert hostile.returncode == 2 and hostile.stdout == ""
        assert hostile.stderr.strip() == "ERROR: invalid arguments"
        assert "Users" not in hostile.stderr and "Traceback" not in hostile.stderr

    with tempfile.TemporaryDirectory() as directory:
        temporary = Path(directory)
        reformatted = {}
        for source in (BUNDLE_PATH, API_PATH, ADMISSION_PATH):
            target = temporary / source.name
            value = json.loads(source.read_text(encoding="utf-8"))
            target.write_text(
                json.dumps(value, indent=4, ensure_ascii=False).replace("\n", "\r\n") + "\r\n",
                encoding="utf-8",
                newline="",
            )
            reformatted[source] = target
        reformat_commands = (
            [sys.executable, str(evidence_validator.__file__), "--bundle", str(reformatted[BUNDLE_PATH]), "--api-evidence", str(API_PATH), "--contract", str(CONTRACT_PATH)],
            [sys.executable, str(evidence_validator.__file__), "--bundle", str(BUNDLE_PATH), "--api-evidence", str(reformatted[API_PATH]), "--contract", str(CONTRACT_PATH)],
            [sys.executable, str(admission_validator.__file__), "--admission", str(reformatted[ADMISSION_PATH]), "--bundle", str(BUNDLE_PATH), "--api-evidence", str(API_PATH), "--contract", str(CONTRACT_PATH)],
        )
        for command in reformat_commands:
            completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            assert completed.returncode == 1 and completed.stdout == ""
            assert completed.stderr.strip() == "ERROR: validation failed"


def main() -> int:
    tests = (
        test_canonical_artifacts_and_exact_parent_chain,
        test_exact_five_of_eight_disposition_is_nonselectable,
        test_configuration_reproduction_and_candidate_bindings_fail_closed,
        test_operation_order_facts_and_unavailable_states_fail_closed,
        test_api_evidence_rejects_cross_candidate_unsupported_and_selection_claims,
        test_independent_admission_counts_and_blocker_cannot_be_self_amended,
        test_readiness_successor_reports_only_the_incremental_admission,
        test_historical_artifacts_and_legacy_validators_are_immutable,
        test_clis_are_bounded_and_hostile_arguments_are_private,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} OT-109 mbedTLS API/config admission scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
