#!/usr/bin/env python3
"""Adversarial host tests for complete OT-117 libsodium API/config admission."""
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
import crypto_libsodium_api_config_admission as admission_validator  # noqa: E402
import crypto_libsodium_api_config_evidence as evidence_validator  # noqa: E402


BUNDLE_PATH = CRYPTO / "OT-117-OT005-LIBSODIUM-API-CONFIG-OPERATION-EVIDENCE-V0.json"
API_PATH = CRYPTO / "OT-117-OT005-LIBSODIUM-API-CONFIG-EVIDENCE-V2.json"
ADMISSION_PATH = CRYPTO / "OT-117-OT005-LIBSODIUM-API-CONFIG-ADMISSION-DELTA-V0.json"
CONTRACT_PATH = CRYPTO / "OT-108-OT005-PER-CANDIDATE-API-CONFIG-ACCEPTANCE-CONTRACT-V1.json"
EXPECTED = {
    BUNDLE_PATH: ("b1daf950473ea9e86ddff16d8f28efbc0972582902d5b262686cf73d0907bf58", "6419ac77392aa7b7f295cbda7719a16581a617e7564c2afec6c03aac7b2fea90"),
    API_PATH: ("34888d71da2c9042856ea48c7b1225f21c1345582c144239cab0096ff03e69b5", "6e5e969c3b3f7bf29372e15e3cc75c693fb00f57f7f297c14cc77123dec4610d"),
    ADMISSION_PATH: ("527c5e713a6d96d38b7dfbfb9d3e0ceb891f4652f8428e2ca65e0b8ea8f316d2", "6eb12a9cf44bdc37e6ac87081b1d256b94f5ee013459c3549606c56b5faf0527"),
}
HISTORICAL_RAW = {
    "OT-108-OT005-PER-CANDIDATE-API-CONFIG-ACCEPTANCE-CONTRACT-V1.json": "575e8747cdba643f2abb6f3252a62adbf3f12a9faaa46ca1d4bf4ce8bd9d23f3",
    "OT-109-OT005-MBEDTLS-PSA-API-CONFIG-ADMISSION-DELTA-V0.json": "0311b8b264d264d4a005ac9be8531c06175521362ed54ac9f40bdef1fd7a5df0",
    "OT-116-OT005-SUCCESSOR-READINESS-DECISION-V1.json": "333f8d525160f45627a13913e5d1adabe8e5c8374290af32b9af1df96ef1bd7e",
    "OT-116-OT005-EXECUTABLE-BENCHMARK-PLAN-V1.json": "0280570b74b2b505b5a92e7834b24136caa0956ff1c536d7638bff0a8b105f2a",
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
    except (evidence_validator.ValidationError, admission_validator.ValidationError, acceptance.ValidationError) as exc:
        assert contains in str(exc), (contains, str(exc))
        return
    raise AssertionError(f"expected OT-117 rejection containing {contains!r}")


def test_canonical_artifacts_and_parent_chain() -> None:
    bundle, api, admission, contract = artifacts()
    bundle_result = evidence_validator.validate_operation_bundle(bundle)
    api_result = evidence_validator.validate_api_evidence(api, bundle, contract)
    result = admission_validator.validate(admission, bundle, api, contract)
    assert bundle_result["eligible_operation_count"] == 8
    assert bundle_result["coverage_state"] == "complete_selectable"
    assert api_result["selection_eligible"] is True
    assert result == {
        "schema": "OTLAPIA0", "version": 0,
        "admission_id": admission_validator.ADMISSION_ID,
        "accepted_api_config_count": 2, "source_count": 3,
        "candidate_import_count": 0, "phase_zero_complete": False,
        "measurement_ready": False, "selection_eligible": True,
        "execution_authorized": False, "selection_authorized": False,
        "score_credit_added": False,
        "admission_sha256": EXPECTED[ADMISSION_PATH][1],
    }
    for path, (raw_digest, canonical_digest) in EXPECTED.items():
        value = admission_validator.load(path) if path == ADMISSION_PATH else evidence_validator.load(path)
        assert hashlib.sha256(path.read_bytes()).hexdigest() == raw_digest
        assert evidence_validator.canonical_sha256(value) == canonical_digest


def test_complete_primary_eight_of_eight_is_required() -> None:
    bundle, api, _, contract = artifacts()
    assert [item["operation_id"] for item in api["operation_results"]] == list(acceptance.OPERATIONS)
    assert all(item["state"] == "eligible" and item["evidence_sha256"] for item in api["operation_results"])
    assert api["coverage_state"] == "complete_selectable" and api["selection_eligible"] is True
    changed = copy.deepcopy(api)
    changed["operation_results"][-1] = {"operation_id": "noise_xk_handshake", "state": "unavailable", "evidence_sha256": None}
    changed["coverage_state"] = "comparison_partial"
    changed["selection_eligible"] = False
    changed["result"] = "partial_comparison_only"
    expect_error(lambda: evidence_validator.validate_api_evidence(changed, bundle, contract), "partial evidence requires comparison role")


def test_configuration_reproduction_is_exact_and_nonbuilding() -> None:
    bundle, _, _, _ = artifacts()
    reproduction = bundle["configuration_reproduction"]
    assert reproduction["run_count"] == 2 and reproduction["runs_identical"] is True
    assert reproduction["generated_sdkconfig_bytes"] == 107001
    assert reproduction["generated_sdkconfig_sha256"] == acceptance.CANDIDATE_BY_ID["espressif_libsodium"]["generated_sdkconfig_sha256"]
    for run in reproduction["runs"]:
        assert run["configuration_only"] is True
        assert run["candidate_source_copied"] is False
        assert run["candidate_compiled"] is False
        assert run["benchmark_executed"] is False
    changed = copy.deepcopy(bundle); changed["configuration_reproduction"]["runs"][0]["candidate_compiled"] = True
    expect_error(lambda: evidence_validator.validate_operation_bundle(changed), "must be false")


def test_noise_adapter_identity_hashes_and_api_are_exact() -> None:
    bundle, _, _, _ = artifacts()
    adapter = bundle["adapter"]
    assert adapter["schema"] == "OTNXK0" and adapter["version"] == 0
    assert adapter["suite"] == "Noise_XK_25519_ChaChaPoly_SHA256"
    assert adapter["header_raw_sha256"] == evidence_validator.EXPECTED_ADAPTER_HEADER_RAW_SHA256
    assert adapter["source_raw_sha256"] == evidence_validator.EXPECTED_ADAPTER_SOURCE_RAW_SHA256
    assert adapter["direct_api_symbols"] == list(evidence_validator.ADAPTER_DIRECT_SYMBOLS)
    assert "crypto_scalarmult_curve25519_base" not in adapter["direct_api_symbols"]
    assert hashlib.sha256(evidence_validator.ADAPTER_HEADER.read_bytes()).hexdigest() == adapter["header_raw_sha256"]
    assert hashlib.sha256(evidence_validator.ADAPTER_SOURCE.read_bytes()).hexdigest() == adapter["source_raw_sha256"]
    changed = copy.deepcopy(bundle); changed["adapter"]["source_raw_sha256"] = "00" * 32
    expect_error(lambda: evidence_validator.validate_operation_bundle(changed), "adapter binding")


def test_operation_order_facts_and_purpose_distinct_digests_fail_closed() -> None:
    bundle, api, _, contract = artifacts()
    result = evidence_validator.validate_operation_bundle(bundle)
    assert set(result["operation_evidence_sha256"]) == set(acceptance.OPERATIONS)
    assert len(set(result["operation_evidence_sha256"].values())) == 8
    cases = []
    changed = copy.deepcopy(bundle); changed["operation_records"][0], changed["operation_records"][1] = changed["operation_records"][1], changed["operation_records"][0]; cases.append(changed)
    changed = copy.deepcopy(bundle); changed["operation_records"][-1]["api_symbols"].append("invented_noise_api"); cases.append(changed)
    changed = copy.deepcopy(bundle); changed["operation_records"][-1]["source_state"] = "primitive_only"; cases.append(changed)
    for changed in cases:
        expect_error(lambda changed=changed: evidence_validator.validate_operation_bundle(changed), "operation facts")
    changed = copy.deepcopy(api); changed["operation_results"][-1]["evidence_sha256"] = changed["operation_results"][0]["evidence_sha256"]
    expect_error(lambda: evidence_validator.validate_api_evidence(changed, bundle, contract), "purpose-distinct")


def test_cross_candidate_and_authority_substitution_fail_closed() -> None:
    bundle, api, _, contract = artifacts()
    cases = []
    changed = copy.deepcopy(api); changed["source_evidence_sha256"] = acceptance.CANDIDATE_BY_ID["monocypher"]["source_evidence_sha256"]; cases.append(changed)
    changed = copy.deepcopy(api); changed["final_sdkconfig_sha256"] = acceptance.CANDIDATE_BY_ID["monocypher"]["generated_sdkconfig_sha256"]; cases.append(changed)
    changed = copy.deepcopy(api); changed["execution_authorized"] = True; cases.append(changed)
    changed = copy.deepcopy(api); changed["selection_authorized"] = True; cases.append(changed)
    for changed in cases:
        expect_error(lambda changed=changed: evidence_validator.validate_api_evidence(changed, bundle, contract), "")


def test_independent_admission_is_cumulative_and_phase_zero_remains_blocked() -> None:
    bundle, api, admission, contract = artifacts()
    assert acceptance.ACCEPTED_API_CONFIG_EVIDENCE_SHA256["espressif_libsodium"] == frozenset()
    expect_error(lambda: acceptance.validate_evidence(api, contract), "not independently accepted")
    result = admission_validator.validate(admission, bundle, api, contract)
    assert result["accepted_api_config_count"] == 2 and result["phase_zero_complete"] is False
    registry = admission["accepted_api_config_evidence_sha256"]
    assert registry["espressif_libsodium"] == [EXPECTED[API_PATH][1]]
    assert registry["esp_idf_mbedtls_psa"] == [admission_validator.MBEDTLS_EVIDENCE_SHA256]
    assert registry["monocypher"] == []
    cases = []
    changed = copy.deepcopy(admission); changed["acceptance_counts"]["api_config"] = 3; cases.append((changed, "acceptance counts"))
    changed = copy.deepcopy(admission); changed["phase_zero"]["complete"] = True; cases.append((changed, "phase-zero"))
    changed = copy.deepcopy(admission); changed["measurement_blockers"] = []; cases.append((changed, "measurement blocker"))
    for changed, message in cases:
        expect_error(lambda changed=changed: admission_validator.validate(changed, bundle, api, contract), message)


def test_all_authority_and_selection_claims_remain_false() -> None:
    bundle, api, admission, contract = artifacts()
    assert not any(bundle["authority"].values())
    assert not any(admission["authority"].values())
    assert api["selection_eligible"] is True and api["selection_authorized"] is False
    assert admission["admitted_candidate"]["selection_eligible"] is True
    assert admission["admitted_candidate"]["selection_authorized"] is False
    for field in admission["authority"]:
        changed = copy.deepcopy(admission); changed["authority"][field] = True
        expect_error(lambda changed=changed: admission_validator.validate(changed, bundle, api, contract), "authority disposition")


def test_historical_contracts_remain_byte_exact() -> None:
    for name, digest in HISTORICAL_RAW.items():
        assert hashlib.sha256((CRYPTO / name).read_bytes()).hexdigest() == digest
    contract = acceptance.load(CONTRACT_PATH)
    result = acceptance.validate_contract(contract)
    assert result["accepted_api_config_count"] == 0
    assert acceptance.ACCEPTED_API_CONFIG_EVIDENCE_SHA256 == {
        candidate["candidate_id"]: frozenset() for candidate in acceptance.CANDIDATE_RECORDS
    }


def test_clis_are_bounded_private_and_raw_byte_exact() -> None:
    commands = (
        [sys.executable, str(evidence_validator.__file__), "--bundle", str(BUNDLE_PATH), "--api-evidence", str(API_PATH), "--contract", str(CONTRACT_PATH)],
        [sys.executable, str(admission_validator.__file__), "--admission", str(ADMISSION_PATH), "--bundle", str(BUNDLE_PATH), "--api-evidence", str(API_PATH), "--contract", str(CONTRACT_PATH)],
    )
    for command in commands:
        completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        assert completed.returncode == 0, completed.stderr
        output = json.loads(completed.stdout)
        assert output["execution_authorized"] is False and output["selection_authorized"] is False
        hostile = subprocess.run(command[:2] + ["--private=C:\\Users\\operator\\secret.json"], cwd=ROOT, capture_output=True, text=True)
        assert hostile.returncode == 2 and hostile.stdout == ""
        assert hostile.stderr.strip() == "ERROR: invalid arguments"
        assert "Users" not in hostile.stderr and "Traceback" not in hostile.stderr
    with tempfile.TemporaryDirectory() as directory:
        temporary = Path(directory)
        for source, command_index, command in ((BUNDLE_PATH, 3, commands[0]), (API_PATH, 5, commands[0]), (ADMISSION_PATH, 3, commands[1])):
            target = temporary / source.name
            value = json.loads(source.read_text(encoding="utf-8"))
            target.write_text(json.dumps(value, indent=4, ensure_ascii=False).replace("\n", "\r\n") + "\r\n", encoding="utf-8", newline="")
            changed = list(command); changed[command_index] = str(target)
            completed = subprocess.run(changed, cwd=ROOT, capture_output=True, text=True)
            assert completed.returncode == 1 and completed.stdout == ""
            assert completed.stderr.strip() == "ERROR: validation failed"


def main() -> int:
    tests = (
        test_canonical_artifacts_and_parent_chain,
        test_complete_primary_eight_of_eight_is_required,
        test_configuration_reproduction_is_exact_and_nonbuilding,
        test_noise_adapter_identity_hashes_and_api_are_exact,
        test_operation_order_facts_and_purpose_distinct_digests_fail_closed,
        test_cross_candidate_and_authority_substitution_fail_closed,
        test_independent_admission_is_cumulative_and_phase_zero_remains_blocked,
        test_all_authority_and_selection_claims_remain_false,
        test_historical_contracts_remain_byte_exact,
        test_clis_are_bounded_private_and_raw_byte_exact,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} OT-117 libsodium API/config admission scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
