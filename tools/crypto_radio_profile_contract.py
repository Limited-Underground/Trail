#!/usr/bin/env python3
"""Strict host-only validator for the OT-110 US915 radio evidence contract."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

SCHEMA = "OTRPF0"
VERSION = 0
CONTRACT_ID = "OT-110-OT005-US915-DIRECT-RADIO-PROFILE-EVIDENCE-CONTRACT-V0"
CONTRACT_PATH = Path(__file__).resolve().parents[1] / "tests" / "benchmarks" / "crypto" / f"{CONTRACT_ID}.json"
EXPECTED_CONTRACT_SHA256 = "d5b44cea761b12ad6422be250bf0a827469441643d6f5e944932a91cc92b68d9"
EXPECTED_CONTRACT_RAW_SHA256 = "8af36e000d5cd0478d1a829fb5a1f2b330cdf09bad188445d30579c348f7e2e1"
MAX_BYTES = 131072
MAX_DEPTH = 16
MAX_ITEMS = 1024
_HEX64 = re.compile(r"^[0-9a-f]{64}$")
_PRIVATE = re.compile(r"(?:[A-Za-z]:\\|/Users/|/home/|COM\d+|tty(?:USB|ACM)\d+|(?:[0-9A-F]{2}:){5}[0-9A-F]{2})", re.I)


class ContractError(ValueError):
    pass


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        raise ContractError("invalid arguments")


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for key, value in pairs:
        if key in out:
            raise ContractError("duplicate key")
        out[key] = value
    return out


def _scan(value: Any, depth: int = 0) -> int:
    if depth > MAX_DEPTH:
        raise ContractError("structure too deep")
    if isinstance(value, str):
        if len(value) > 4096 or _PRIVATE.search(value):
            raise ContractError("unsafe text")
        return 1
    if value is None or type(value) in (bool, int):
        return 1
    if isinstance(value, list):
        count = 1 + sum(_scan(v, depth + 1) for v in value)
    elif isinstance(value, dict):
        count = 1 + sum(_scan(k, depth + 1) + _scan(v, depth + 1) for k, v in value.items())
    else:
        raise ContractError("unsupported value")
    if count > MAX_ITEMS:
        raise ContractError("structure too large")
    return count


def load(path: Path | str, expected_raw_sha256: str | None = None) -> dict[str, Any]:
    raw = Path(path).read_bytes()
    if len(raw) > MAX_BYTES:
        raise ContractError("file too large")
    if expected_raw_sha256 is not None and hashlib.sha256(raw).hexdigest() != expected_raw_sha256:
        raise ContractError("raw digest mismatch")
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ContractError("invalid JSON") from exc
    if not isinstance(value, dict):
        raise ContractError("root must be object")
    _scan(value)
    return value


def canonical_sha256(value: Any) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()).hexdigest()


def _keys(value: dict[str, Any], expected: tuple[str, ...]) -> None:
    if set(value) != set(expected):
        raise ContractError("field set mismatch")


def _exact(value: Any, expected: Any) -> None:
    if type(value) is not type(expected) or value != expected:
        raise ContractError("value mismatch")


def validate_contract(contract: dict[str, Any]) -> dict[str, Any]:
    _scan(contract)
    _keys(contract, ("schema", "version", "artifact_kind", "contract_id", "accepted_date", "status", "public_result", "parents", "closure_boundary", "profile_requirements", "device_requirements", "preflight_requirements", "evidence_contract", "admission_contract", "authority", "claims"))
    exact_top = {
        "schema": SCHEMA, "version": VERSION, "artifact_kind": "direct_radio_profile_evidence_contract",
        "contract_id": CONTRACT_ID, "accepted_date": "2026-08-21",
        "status": "direct_radio_profile_evidence_contract_frozen_host_only",
        "public_result": "US915-DIRECT-RADIO-PROFILE-EVIDENCE-CONTRACT-FROZEN-HOST-ONLY; VALUES-UNMEASURED; TWO-PHYSICAL-NODES-REQUIRED; NO-DEVICE-FLASH-OR-RADIO-AUTHORITY; READINESS-BLOCKED",
    }
    for key, expected in exact_top.items():
        _exact(contract[key], expected)
    expected_parents = [
        ("docs/decisions/0003-crypto-benchmark-gate.md", "6a88a00aba6383e07e1d1ca8daed3c48972806bc384e6d3980d27ea67accf359"),
        ("tests/benchmarks/crypto/OT-094-OT005-CANDIDATE-READINESS-V0.json", "bb607158cbe8ac95a470f0a6c87fbb6d8d986259cf86540b14245fc1167dc7ae"),
        ("tests/benchmarks/crypto/OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-EVIDENCE-V0.json", "517809caf31250d126cc3619f9d05386a92811a594dca0087d9acbf1b671147e"),
        ("tests/benchmarks/crypto/OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-ADMISSION-DELTA-V0.json", "98cce120cadc1bddf5851f1480ae181488e17277ba0a2c8c8c38a70a062be105"),
        ("tests/benchmarks/crypto/OT-107-OT005-FINAL-CANDIDATE-BUILD-CONFIGURATION-ADMISSION-DELTA-V0.json", "3d71dfb02b6fd25e0881ac63cc085174c2cdd7e5ac2cd1e12c320ec34928f5a2"),
        ("tests/benchmarks/crypto/OT-109-OT005-MBEDTLS-PSA-API-CONFIG-ADMISSION-DELTA-V0.json", "0311b8b264d264d4a005ac9be8531c06175521362ed54ac9f40bdef1fd7a5df0"),
    ]
    if not isinstance(contract["parents"], list) or len(contract["parents"]) != len(expected_parents):
        raise ContractError("parent mismatch")
    for item, (path, digest) in zip(contract["parents"], expected_parents):
        _exact(item, {"path": path, "raw_sha256": digest})
    closure = contract["closure_boundary"]
    _exact(closure, {"blocker_id": "direct_radio_mtu_phy_region_unresolved", "region_code": "US915", "measured_values_status": "unmeasured", "current_blocker_count": 1, "independent_admission_required": True, "contract_freeze_closes_blocker": False, "physical_evidence_required": True, "readiness_after_contract": "blocked", "readiness_after_future_admission": "successor_readiness_and_new_executable_plan_required"})
    profile = contract["profile_requirements"]
    _keys(profile, ("modulation", "region_code", "ordered_fields", "frequency_hz", "bandwidth_hz", "spreading_factor", "coding_rate_denominator", "tx_power_dbm", "preamble_symbols", "sync_word", "exact_boolean_fields", "direct_payload_ceiling_bytes", "benchmark_mtu_protocol_test_requirement", "benchmark_mtu_must_not_exceed_direct_ceiling", "frozen_measured_values"))
    _exact(profile["modulation"], "LoRa"); _exact(profile["region_code"], "US915")
    _exact(profile["ordered_fields"], ["rf_variant", "region_code", "frequency_hz", "bandwidth_hz", "spreading_factor", "coding_rate_denominator", "tx_power_dbm", "preamble_symbols", "explicit_header", "crc_enabled", "low_data_rate_optimization", "sync_word", "direct_payload_ceiling_bytes"])
    for key, expected in {"frequency_hz": {"minimum": 902000000, "maximum": 928000000, "occupied_band_must_fit": True}, "bandwidth_hz": {"minimum": 7800, "maximum": 500000}, "spreading_factor": {"minimum": 5, "maximum": 12}, "coding_rate_denominator": {"minimum": 5, "maximum": 8}, "tx_power_dbm": {"minimum": -20, "maximum": 30}, "preamble_symbols": {"minimum": 4, "maximum": 65535}, "sync_word": {"minimum": 0, "maximum": 65535}, "direct_payload_ceiling_bytes": {"minimum": 1, "maximum": 255}}.items(): _exact(profile[key], expected)
    _exact(profile["exact_boolean_fields"], ["explicit_header", "crc_enabled", "low_data_rate_optimization"]); _exact(profile["benchmark_mtu_protocol_test_requirement"], {"bytes": 163, "classification": "protocol_test_requirement_not_measurement"}); _exact(profile["benchmark_mtu_must_not_exceed_direct_ceiling"], True)
    _exact(profile["frozen_measured_values"], {"rf_variant": None, "frequency_hz": None, "bandwidth_hz": None, "spreading_factor": None, "coding_rate_denominator": None, "tx_power_dbm": None, "preamble_symbols": None, "explicit_header": None, "crc_enabled": None, "low_data_rate_optimization": None, "sync_word": None, "direct_payload_ceiling_bytes": None})
    devices = contract["device_requirements"]
    _exact(devices, {"physical_node_count": 2, "primary": {"evidence_unit": "OT-DEV-001", "target_id": "heltec-v4-bench-candidate", "manufacturer": "Heltec Automation", "board_model": "HTIT-WB32LAF", "revision": "V4.2", "identity_parent_required": True}, "peer": {"identity_state": "must_be_independently_resolved_before_execution", "same_model_or_revision_assumed": False}, "per_node_required": ["device_evidence_unit", "accepted_identity_parent_sha256", "firmware_image_sha256", "firmware_source_sha256", "effective_configuration_sha256", "radio_adapter_source_sha256", "antenna_model", "antenna_gain_dbi", "antenna_connector", "cable_loss_db", "applied_profile_readback"], "same_exact_profile_on_both_nodes": True, "us915_capable_antenna_attached_before_transmit": True, "restore_evidence": {"successor_recovery_contract_required": True, "per_node_required": ["restore_route_id", "restore_manifest_sha256", "restore_firmware_image_sha256", "restore_partition_table_sha256", "restore_effective_configuration_sha256", "private_custody_sanitized_receipt_sha256", "pre_write_readback_sha256", "post_restore_readback_sha256", "security_admission_sha256"], "private_restore_material_publication_prohibited": True, "pre_write_restore_evidence_must_be_admitted": True, "post_test_restore_readback_required": True}, "both_nodes_require_direct_test_firmware": True})
    preflight = contract["preflight_requirements"]
    _exact(preflight, {"fresh_owner_authority_required": ["device_access_authorized", "flash_authorized", "radio_transmit_authorized"], "regulatory_inputs_required": ["fcc_id", "grant_or_exhibit_reference", "manufacturer_operating_instruction", "allowed_radio_mode", "antenna_model", "antenna_gain_dbi", "antenna_connector", "cable_loss_db"], "off_air_validation_required_before_transmit": True, "receiver_configured_before_transmitter": True, "public_artifact_must_exclude": ["device_mac", "ble_identity", "serial_port", "filesystem_path", "exact_coordinates", "private_key", "radio_key"], "legal_or_regulatory_acceptance_not_inferred": True})
    evidence = contract["evidence_contract"]
    _keys(evidence, ("schema", "version", "artifact_kind", "required_sequence", "required_metrics", "acceptance", "proof_boundary", "does_not_prove"))
    _exact(evidence["schema"], "OTRPE0"); _exact(evidence["version"], 0); _exact(evidence["artifact_kind"], "direct_radio_profile_evidence")
    expected_sequence = [{"step": 1, "operation": "off_air_preflight", "transmit": False, "required_successes": 2}, {"step": 2, "operation": "configure_receivers", "transmit": False, "required_successes": 2}, {"step": 3, "operation": "one_byte_probe_each_direction", "transmit": True, "frames_per_direction": 1, "payload_bytes": 1}, {"step": 4, "operation": "benchmark_mtu_each_direction", "transmit": True, "frames_per_direction": 100, "payload_bytes": 163}, {"step": 5, "operation": "direct_ceiling_each_direction", "transmit": True, "frames_per_direction": 10, "payload_bytes": "direct_payload_ceiling_bytes"}, {"step": 6, "operation": "oversize_local_reject", "transmit": False, "attempts_per_node": 1, "payload_bytes": 256}, {"step": 7, "operation": "restart_both_nodes", "transmit": False, "required_successes": 2}, {"step": 8, "operation": "post_restart_benchmark_mtu_each_direction", "transmit": True, "frames_per_direction": 10, "payload_bytes": 163}]
    _exact(evidence["required_sequence"], expected_sequence)
    _exact(evidence["required_metrics"], ["attempted", "sent", "received", "lost", "duplicates", "corrupt", "unexpected", "rtt_ms_p50", "rtt_ms_p95", "rtt_ms_max", "rssi_dbm_min", "rssi_dbm_max", "snr_db_min", "snr_db_max", "distance_class", "timeout_policy", "theoretical_airtime_ms"])
    _exact(evidence["acceptance"], {"distance_class": "close_bench_no_coordinates", "payload_hash_and_sequence_exact": True, "lost": 0, "duplicates": 0, "corrupt": 0, "unexpected": 0, "oversize_256_local_reject_without_transmit": True, "restart_retains_exact_profile": True, "all_directional_steps_pass": True})
    _exact(evidence["proof_boundary"], "commanded-and-read-back-profile-plus-bounded-over-air-interoperability-only")
    _exact(evidence["does_not_prove"], ["calibrated_emitted_frequency", "calibrated_emitted_power", "regulatory_compliance", "range", "production_support", "benchmark_readiness", "candidate_selection"])
    _exact(contract["admission_contract"], {"schema": "OTRPA0", "version": 0, "artifact_kind": "direct_radio_profile_admission_delta", "must_bind_raw_and_canonical_sha256": ["contract", "evidence"], "only_closable_blocker_id": "direct_radio_mtu_phy_region_unresolved", "successor_blockers_after_acceptance": [], "new_immutable_executable_plan_required": True, "readiness_advanced_by_admission": False, "benchmark_or_selection_authority": False, "score_credit_added": False})
    authority_keys = ("device_access_authorized", "firmware_build_authorized", "flash_authorized", "radio_transmit_authorized", "benchmark_execution_authorized", "candidate_selection_authorized", "suite_selection_authorized", "packet_v1_authorized", "regulatory_acceptance_claimed", "score_credit_added")
    _exact(contract["authority"], {key: False for key in authority_keys})
    _exact(contract["claims"], {"contract_frozen": True, "profile_selected": False, "profile_values_measured": False, "physical_evidence_generated": False, "blocker_closed": False, "device_accessed": False, "device_flashed": False, "radio_used": False, "benchmark_executed": False, "candidate_selected": False, "suite_selected": False, "production_support_proven": False, "regulatory_compliance_proven": False, "score_credit_added": False})
    digest = canonical_sha256(contract)
    if EXPECTED_CONTRACT_SHA256 != "TO_BE_PINNED" and digest != EXPECTED_CONTRACT_SHA256:
        raise ContractError("canonical digest mismatch")
    return {"schema": SCHEMA, "version": VERSION, "contract_id": CONTRACT_ID, "status": contract["status"], "blocker_id": closure["blocker_id"], "region_code": closure["region_code"], "physical_node_count": devices["physical_node_count"], "measured_values_status": closure["measured_values_status"], "blocker_closed": contract["claims"]["blocker_closed"], "radio_transmit_authorized": contract["authority"]["radio_transmit_authorized"], "readiness_advanced": False, "contract_canonical_sha256": digest}


def main(argv: list[str] | None = None) -> int:
    parser = SafeArgumentParser(add_help=True)
    parser.add_argument("--contract", type=Path, default=CONTRACT_PATH)
    try:
        args = parser.parse_args(argv)
        expected_raw = EXPECTED_CONTRACT_RAW_SHA256 if EXPECTED_CONTRACT_RAW_SHA256 != "TO_BE_PINNED" else None
        result = validate_contract(load(args.contract, expected_raw))
        result["contract_raw_sha256"] = EXPECTED_CONTRACT_RAW_SHA256
    except (ContractError, OSError):
        print("ERROR: validation failed", file=sys.stderr)
        return 2
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
