#!/usr/bin/env python3
"""Fail-closed validator for the OT-148 partial Phase 2 corpus."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_RECORD = ROOT / "tests/benchmarks/crypto/OT-148-OT005-PHASE-TWO-CORPUS-RECONCILIATION-V0.json"

PINS = {
    "benchmark_plan": ("tests/benchmarks/crypto/OT-116-OT005-EXECUTABLE-BENCHMARK-PLAN-V1.json", "0280570b74b2b505b5a92e7834b24136caa0956ff1c536d7638bff0a8b105f2a", "7844d7be1824784a0ff58ba58df25bda85c6059c445f217a4d520215bfff50a8"),
    "phase_two_authority": ("tests/benchmarks/crypto/OT-121-OT005-PHASE-TWO-EXECUTION-AUTHORITY-V1.json", "765aacd8a33862265b46da2d60333759cd96b72a8acae9e70b22c5bda2dbd90f", "a2e9bbea78282c3a0451654f39c0be49c875217933ef02b7bc384860f32f3105"),
    "mbedtls_psa_api_admission": ("tests/benchmarks/crypto/OT-109-OT005-MBEDTLS-PSA-API-CONFIG-ADMISSION-DELTA-V0.json", "0311b8b264d264d4a005ac9be8531c06175521362ed54ac9f40bdef1fd7a5df0", "fed7b009a97a60678b2dfbba23d933b974aa0fec46d0b460ac7eb221e91931dd"),
    "libsodium_api_admission": ("tests/benchmarks/crypto/OT-117-OT005-LIBSODIUM-API-CONFIG-ADMISSION-DELTA-V0.json", "527c5e713a6d96d38b7dfbfb9d3e0ceb891f4652f8428e2ca65e0b8ea8f316d2", "6eb12a9cf44bdc37e6ac87081b1d256b94f5ee013459c3549606c56b5faf0527"),
    "monocypher_api_admission": ("tests/benchmarks/crypto/OT-118-OT005-MONOCYPHER-API-CONFIG-ADMISSION-DELTA-V0.json", "9fbecf19b206b31fae948b6bc7e7aa4e206ba26aa59b94fb7f07d4e1d300810a", "df412285515fe29525b0bfd7cba45fd7ccd9a3d601be284242886e8adb19fec9"),
    "libsodium_result": ("tests/benchmarks/crypto/OT-122-OT005-LIBSODIUM-NOISE-RESOURCE-EXECUTION-RECEIPT-V0.json", "2b023c640bfbec8ad6eb5d1d63d65e8f1ad75dcfe593566aba6b3d468355a178", "3cf651719d1e5ce96dad85fc364f5cb9e1c43bdcb64d2ac45b748dcc67c18949"),
    "matched_resource_contract": ("tests/benchmarks/crypto/OT-123-OT005-MATCHED-RESOURCE-ACCOUNTING-CONTRACT-V0.json", "4023b35e0aecc6f4c8c5077bdd8646ad81df892c8816a1348bf1c97181c0cc03", "77e0656f286ee8e030c9add21bcbd07e753d78179a31e352f3ec9df000c344f5"),
    "monocypher_result": ("tests/benchmarks/crypto/OT-146-OT005-MONOCYPHER-EXECUTION-RECEIPT-V0.json", "9a5ea09fa8cdf465f5c83b6a0eb69fb80579806b0635ac63083e1234e4f91464", "a53da32a10ad1e80ca2b07659b4865e56d490fd3d2a8c6650925c3beb792292b"),
}

REQUIRED_GATES = [
    "primitive_vectors_and_negative_cases",
    "noise_xk_independent_interoperability",
    "invitation_replay_reorder_timeout_refusal",
    "entropy_and_cold_start_uniqueness",
    "temporary_secret_wipe_and_log_redaction",
    "rollback_safe_counter_interruption",
    "two_device_join_revoke_reset_recovery",
    "license_sbom_and_reproducible_lock",
]

UNRESOLVED = [
    "mbedtls_psa_two_node_five_operation_timing_and_runtime_measurement",
    "matched_linked_flash_and_static_ram_results_with_signed_or_zero_delta_admission",
    "noise_xk_handshake_wire_bytes_fragments_measured_airtime_and_bounded_retry_result",
    "independent_reconciliation_of_all_eight_named_required_gates",
    "private_raw_trace_custody_verification",
]

SEQUENCE = [
    "prepare_mbedtls_psa_target_and_signed_resource_accounting_successor_host_only",
    "accept_fresh_explicit_bounded_hardware_authority",
    "execute_remaining_candidate_resource_and_noise_xk_radio_measurements",
    "independently_reconcile_all_named_gates_and_private_trace_custody",
    "admit_complete_phase_two_corpus_in_phase_three",
    "select_library_suite_handshake_kdf_and_packet_v1_wire",
]


class ReconciliationError(ValueError):
    pass


def canonical_sha256(value: Any) -> str:
    raw = json.dumps(value, ensure_ascii=False, allow_nan=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(raw).hexdigest()


def _need(condition: bool, message: str) -> None:
    if not condition:
        raise ReconciliationError(message)


def _load_json(path: Path) -> tuple[bytes, dict[str, Any]]:
    raw = path.read_bytes()
    value = json.loads(raw)
    _need(isinstance(value, dict), f"not an object: {path}")
    return raw, value


def load_record(path: Path = DEFAULT_RECORD) -> dict[str, Any]:
    return _load_json(path)[1]


def _validate_bindings(record: dict[str, Any]) -> dict[str, dict[str, Any]]:
    _need(set(record["bindings"]) == set(PINS), "binding set mismatch")
    loaded: dict[str, dict[str, Any]] = {}
    for name, (relative_path, expected_raw, expected_canonical) in PINS.items():
        expected = {"path": relative_path, "raw_sha256": expected_raw, "canonical_sha256": expected_canonical}
        _need(record["bindings"][name] == expected, f"binding mismatch: {name}")
        raw, value = _load_json(ROOT / relative_path)
        _need(hashlib.sha256(raw).hexdigest() == expected_raw, f"raw digest mismatch: {name}")
        _need(canonical_sha256(value) == expected_canonical, f"canonical digest mismatch: {name}")
        loaded[name] = value
    return loaded


def validate(record: dict[str, Any]) -> dict[str, Any]:
    _need(record["schema"] == "OTP2CR0" and record["version"] == 0, "schema mismatch")
    _need(record["artifact_kind"] == "partial_phase_two_corpus_reconciliation", "artifact kind mismatch")
    _need(record["status"] == "partial_phase_two_corpus_reconciled_selection_withheld", "status mismatch")
    parents = _validate_bindings(record)

    plan = parents["benchmark_plan"]
    phase_two = next(item for item in plan["ordered_phases"] if item["phase"] == 2)
    phase_three = next(item for item in plan["ordered_phases"] if item["phase"] == 3)
    _need([item["candidate_id"] for item in plan["candidates"]] == ["espressif_libsodium", "esp_idf_mbedtls_psa", "monocypher"], "candidate order drift")
    _need(phase_two["required_measurements"] == ["min_us", "median_us", "p95_us", "max_us", "linked_flash_delta_bytes", "static_ram_bytes", "peak_dynamic_ram_bytes", "max_stack_used_bytes", "watchdog_resets"], "Phase 2 measurement drift")
    _need(phase_two["required_radio_measurements"] == ["handshake_total_wire_bytes", "fragments", "measured_airtime_us", "bounded_retry_result"], "Phase 2 radio drift")
    _need(phase_three["required_before_selection_review"] == ["exact_contract_raw_and_canonical_bindings", "exact_per_candidate_result_bindings", "all_required_gates_reconciled", "privacy_safe_public_summary"], "Phase 3 drift")
    _need(plan["required_gates"] == REQUIRED_GATES, "required gate drift")

    libsodium_admission = parents["libsodium_api_admission"]["admitted_candidate"]
    mbedtls_admission = parents["mbedtls_psa_api_admission"]
    monocypher_admission = parents["monocypher_api_admission"]["admitted_candidate"]
    _need(libsodium_admission["eligible_operation_count"] == 8 and libsodium_admission["selection_eligible"] is True, "libsodium eligibility mismatch")
    _need("FIVE-OF-EIGHT" in mbedtls_admission["public_result"] and "NONSELECTABLE" in mbedtls_admission["public_result"], "mbedTLS admission mismatch")
    _need(monocypher_admission["eligible_operation_count"] == 5 and monocypher_admission["selection_eligible"] is False, "Monocypher eligibility mismatch")

    libsodium = parents["libsodium_result"]
    monocypher = parents["monocypher_result"]
    for receipt, candidate, operations in ((libsodium, "espressif_libsodium", 8), (monocypher, "monocypher", 5)):
        _need(receipt["node_count"] == 2 and receipt["restoration_complete"] is True, f"{candidate} receipt state mismatch")
        _need(receipt["claims"]["phase_two_complete"] is False and receipt["claims"]["radio_used"] is False, f"{candidate} overclaim")
        summaries = [node.get("local_primitive_result", node.get("result_summary")) for node in receipt["nodes"]]
        _need(all(item["candidate_id"] == candidate and item["operations_completed"] == operations for item in summaries), f"{candidate} operation mismatch")

    resources = parents["matched_resource_contract"]
    _need(resources["claims"]["matched_control_built"] is False and resources["claims"]["resource_delta_admitted"] is False, "matched-resource state mismatch")
    _need(resources["result_validator_transition"]["current_validator_can_admit_this_signed_delta_contract"] is False, "signed-delta blocker missing")

    corpus = record["candidate_corpus"]
    _need([item["candidate_id"] for item in corpus] == ["espressif_libsodium", "esp_idf_mbedtls_psa", "monocypher"], "reconciled candidate order mismatch")
    _need([(item["admitted_operation_count"], item["selection_eligible"], item["selected"]) for item in corpus] == [(8, True, False), (5, False, False), (5, False, False)], "candidate disposition mismatch")
    _need(record["recommendation"] == {"candidate_id": "espressif_libsodium", "version": "1.0.22", "status": "evidence_backed_recommendation_only", "basis": "only_candidate_with_complete_eight_of_eight_admitted_operation_surface_and_structural_selection_eligibility", "selection_authorized": False}, "recommendation mismatch")
    _need(record["unresolved_required_evidence"] == UNRESOLVED, "unresolved evidence mismatch")
    _need(record["unreconciled_required_gates"] == REQUIRED_GATES, "unreconciled gate mismatch")
    _need(record["shortest_valid_sequence"] == SEQUENCE, "completion sequence mismatch")

    authority = record["authority"]
    _need(authority["host_reconciliation_authorized"] is True, "host reconciliation authority missing")
    _need(all(value is False for key, value in authority.items() if key != "host_reconciliation_authorized"), "unauthorized authority granted")
    claims = record["claims"]
    _need(claims["partial_corpus_reconciled"] is True, "reconciliation claim missing")
    _need(all(claims[key] is False for key in ("phase_two_complete", "phase_three_admission_complete", "candidate_selected", "suite_selected", "packet_v1_wire_selected", "hardware_or_device_accessed", "public_website_update_required", "score_credit_added")), "completion or selection overclaim")
    return {
        "schema": record["schema"],
        "status": record["status"],
        "phase_two_complete": False,
        "phase_three_admission_complete": False,
        "candidate_selected": False,
        "recommended_candidate": "espressif_libsodium",
        "unresolved_evidence_count": len(UNRESOLVED),
        "unreconciled_gate_count": len(REQUIRED_GATES),
        "canonical_sha256": canonical_sha256(record),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--record", type=Path, default=DEFAULT_RECORD)
    args = parser.parse_args()
    try:
        print(json.dumps(validate(load_record(args.record)), sort_keys=True, separators=(",", ":")))
    except (OSError, json.JSONDecodeError, KeyError, StopIteration, ReconciliationError) as exc:
        print(json.dumps({"schema": "OTP2CR0", "error": str(exc)}, sort_keys=True, separators=(",", ":")))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
