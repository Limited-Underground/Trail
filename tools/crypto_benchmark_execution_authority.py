#!/usr/bin/env python3
"""Fail-closed OT-121 one-time Phase 2 benchmark authority validator."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
CRYPTO = ROOT / "tests" / "benchmarks" / "crypto"
AUTHORITY = CRYPTO / "OT-121-OT005-PHASE-TWO-EXECUTION-AUTHORITY-V1.json"
AUTHORITY_PIN = (
    "765aacd8a33862265b46da2d60333759cd96b72a8acae9e70b22c5bda2dbd90f",
    "a2e9bbea78282c3a0451654f39c0be49c875217933ef02b7bc384860f32f3105",
)
PARENTS = {
    "otcbx1_v1": (CRYPTO / "OT-116-OT005-EXECUTABLE-BENCHMARK-PLAN-V1.json", "0280570b74b2b505b5a92e7834b24136caa0956ff1c536d7638bff0a8b105f2a", "7844d7be1824784a0ff58ba58df25bda85c6059c445f217a4d520215bfff50a8"),
    "otrtpa1_v1": (CRYPTO / "OT-119-OT005-SECOND-NODE-EXACT-PROFILE-ADMISSION-DELTA-V1.json", "afd3d8b17f80c49560f9fad71e93703ef6d142ee538146fc5829b2a0799d0e36", "0eff2d934891f36999bdafb2a14ffc755b258c19bccb96a5a8d96db06105a443"),
    "otciba1_v1": (CRYPTO / "OT-120-OT005-CANDIDATE-IMPORT-BUILD-ADMISSION-DELTA-V1.json", "90af31966553bee58fcf71e4decfee8d2bcadfee58ef026e3f96cffcd6f45ccf", "0c55f49803d833c075670b17fa8d033bd5a7cd4997e8714ff247161f7fa2057b"),
}
MAX_BYTES = 131072
MAX_DEPTH = 18
MAX_ITEMS = 4096
PRIVATE = re.compile(r"(?:[A-Za-z]:\\|/(?:Users|home)/|COM\d+|tty(?:USB|ACM)\d+|(?:[0-9A-F]{2}:){5}[0-9A-F]{2}|(?:pin|password|private[_ -]?key|secret)\s*[:=])", re.I)
OPERATIONS = ["ed25519_sign", "ed25519_verify", "x25519", "sha256", "hkdf_sha256", "chacha20poly1305_encrypt", "chacha20poly1305_decrypt", "noise_xk_handshake"]
MEASUREMENTS = ["min_us", "median_us", "p95_us", "max_us", "linked_flash_delta_bytes", "static_ram_bytes", "peak_dynamic_ram_bytes", "max_stack_used_bytes", "watchdog_resets"]
RADIO_MEASUREMENTS = ["handshake_total_wire_bytes", "fragments", "measured_airtime_us", "bounded_retry_result"]


class ValidationError(Exception):
    """Artifact is malformed, private, mutated, or exceeds owner authority."""


class SafeParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        raise ValidationError("invalid arguments")


def canonical_sha256(value: Any) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()).hexdigest()


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValidationError("duplicate key")
        result[key] = value
    return result


def _shape(value: Any, depth: int = 0) -> int:
    if depth > MAX_DEPTH:
        raise ValidationError("excessive depth")
    if isinstance(value, dict):
        return 1 + sum(_shape(key, depth + 1) + _shape(item, depth + 1) for key, item in value.items())
    if isinstance(value, list):
        return 1 + sum(_shape(item, depth + 1) for item in value)
    if value is None or type(value) in (str, int, bool):
        return 1
    raise ValidationError("unsupported value")


def load(path: Path, pin: tuple[str, str] | None = None) -> dict[str, Any]:
    raw = path.read_bytes()
    if len(raw) > MAX_BYTES or PRIVATE.search(raw.decode("utf-8", errors="ignore")):
        raise ValidationError("unsafe artifact")
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
    except (UnicodeDecodeError, json.JSONDecodeError):
        raise ValidationError("invalid json") from None
    if type(value) is not dict or _shape(value) > MAX_ITEMS:
        raise ValidationError("invalid shape")
    if pin and (hashlib.sha256(raw).hexdigest(), canonical_sha256(value)) != pin:
        raise ValidationError("digest mismatch")
    return value


def _exact(value: Any, expected: dict[str, Any], label: str) -> dict[str, Any]:
    if type(value) is not dict or set(value) != set(expected):
        raise ValidationError(f"{label} shape mismatch")
    for key, expected_value in expected.items():
        if value[key] != expected_value or type(value[key]) is not type(expected_value):
            raise ValidationError(f"{label}.{key} mismatch")
    return value


def validate_parent_files() -> dict[str, dict[str, Any]]:
    values = {name: load(path, (raw, canonical)) for name, (path, raw, canonical) in PARENTS.items()}
    plan, profile, imports = values["otcbx1_v1"], values["otrtpa1_v1"], values["otciba1_v1"]
    if plan.get("schema") != "OTCBX1" or plan.get("version") != 1 or plan.get("contract_id") != "OT-116-OT005-EXECUTABLE-BENCHMARK-PLAN-V1":
        raise ValidationError("plan identity mismatch")
    if profile.get("schema") != "OTRTPA1" or profile.get("acceptance_counts", {}).get("exact_profile_units") != 2 or profile.get("phase_zero", {}).get("complete") is not True:
        raise ValidationError("profile admission mismatch")
    if imports.get("schema") != "OTCIBA1" or imports.get("acceptance_counts") != {"exact_profile_units": 2, "source": 3, "api_config": 3, "candidate_import": 3} or imports.get("phases", {}).get("phase_one_complete") is not True:
        raise ValidationError("import admission mismatch")
    return values


def validate_authority(value: dict[str, Any], parents: dict[str, dict[str, Any]]) -> dict[str, Any]:
    expected_top = {"schema", "version", "artifact_kind", "authority_id", "accepted_date", "status", "public_result", "parents", "owner_authorization", "preconditions", "target_scope", "measurement_scope", "temporary_flash_scope", "radio_scope", "one_time_authority", "withheld_authority", "claims"}
    if type(value) is not dict or set(value) != expected_top:
        raise ValidationError("authority shape mismatch")
    _exact({key: value[key] for key in ("schema", "version", "artifact_kind", "authority_id", "accepted_date", "status", "public_result")}, {
        "schema": "OTCBXA1", "version": 1, "artifact_kind": "one_time_phase_two_execution_authority", "authority_id": "OT-121-OT005-PHASE-TWO-EXECUTION-AUTHORITY-V1", "accepted_date": "2026-08-23", "status": "phase_two_execution_authorized_not_executed", "public_result": "OT-121 ONE-TIME PHASE-TWO EXACT-TARGET BENCHMARK EXECUTION AUTHORIZED; TEMPORARY FLASH DEVICE KEY-ENTROPY AND CONDITIONAL US915 2-DBM CLOSE-BENCH SCOPE ONLY; NO SELECTION IMPLEMENTATION SUPPORT REGULATORY PRODUCTION OR SCORE AUTHORITY",
    }, "identity")
    expected_parent_keys = set(PARENTS)
    if type(value["parents"]) is not dict or set(value["parents"]) != expected_parent_keys:
        raise ValidationError("parent shape mismatch")
    for name, (path, raw, canonical) in PARENTS.items():
        _exact(value["parents"][name], {"path": path.relative_to(ROOT).as_posix(), "raw_sha256": raw, "canonical_sha256": canonical}, f"parents.{name}")
    _exact(value["owner_authorization"], {
        "authorization_source": "owner_direct_thread_instruction", "temporary_benchmark_flash_and_device_access_disclosed_before_resumption": True, "owner_explicitly_resumed_after_disclosure": True, "resumption_date": "2026-08-23", "private_conversation_content_retained": False, "continuing_authority_created": False,
    }, "owner_authorization")
    _exact(value["preconditions"], {
        "phase_zero_complete": True, "phase_one_complete": True, "acceptance_counts": {"exact_profile_units": 2, "source": 3, "api_config": 3, "candidate_import": 3}, "fresh_execution_authority_present": True, "independent_result_admission_still_required": True,
    }, "preconditions")
    plan = parents["otcbx1_v1"]
    _exact(value["target_scope"], {
        "target_id": "heltec-v4-bench-candidate", "measurement_units": ["OT-DEV-001", "OT-DEV-002"], "manufacturer": "Heltec Automation", "board_model": "HTIT-WB32LAF", "exact_received_revision": "V4.2", "mcu": "ESP32-S3", "processor_revision": "v0.2", "flash_bytes": 16777216, "psram_bytes": 2097152, "supported": False,
    }, "target_scope")
    phase_two = plan["ordered_phases"][2]
    _exact(value["measurement_scope"], {
        "phase": 2, "name": "two_node_target_measurement", "candidate_order": ["espressif_libsodium", "esp_idf_mbedtls_psa", "monocypher"], "operations": OPERATIONS, "minimum_repetitions_per_admitted_operation": phase_two["minimum_repetitions_per_admitted_operation"], "required_measurements": MEASUREMENTS, "required_radio_measurements": RADIO_MEASUREMENTS, "unavailable_operations_must_have_no_measurements": True, "raw_traces_retained_privately": True, "public_device_identifiers_prohibited": True,
    }, "measurement_scope")
    _exact(value["temporary_flash_scope"], {
        "benchmark_firmware_only": True, "persistent_application_change_authorized": False, "partition_table_change_authorized": False, "bootloader_change_authorized": False, "security_fuse_or_flash_encryption_change_authorized": False, "restore_pre_execution_opentrail_image_after_measurement_or_abort": True, "verify_flash_and_restore_readback": True,
    }, "temporary_flash_scope")
    radio = plan["radio_profile"]
    _exact(value["radio_scope"], {
        "conditional": True, "both_nodes_antenna_preflight_required_before_transmit": True, "fail_closed_if_antenna_preflight_is_not_affirmative": True, "bench_context": "two_node_close_bench_only", "region_code": radio["region_code"], "frequency_hz": radio["frequency_hz"], "bandwidth_hz": radio["bandwidth_hz"], "spreading_factor": radio["spreading_factor"], "coding_rate": radio["coding_rate"], "sync_word": radio["sync_word"], "tx_power_command_setpoint_dbm": 2, "benchmark_total_wire_bytes": radio["benchmark_total_wire_bytes"], "range_or_regulatory_claim_authorized": False,
    }, "radio_scope")
    _exact(value["one_time_authority"], {
        "phase_two_execution_authorized": True, "benchmark_build_authorized": True, "benchmark_execution_authorized": True, "device_access_authorized": True, "temporary_flash_authorized": True, "test_key_or_entropy_operation_authorized": True, "two_node_exact_target_measurement_authorized": True, "conditional_radio_transmit_authorized": True, "consumed_on_phase_two_completion_or_abort": True, "reusable": False,
    }, "one_time_authority")
    _exact(value["withheld_authority"], {
        "candidate_selection_authorized": False, "suite_selection_authorized": False, "handshake_or_kdf_selection_authorized": False, "packet_v1_wire_selection_authorized": False, "secure_lora_implementation_authorized": False, "supported_target_declaration_authorized": False, "compatibility_declaration_authorized": False, "regulatory_acceptance_authorized": False, "production_use_authorized": False, "field_or_range_claim_authorized": False, "phase_three_result_admission_authorized": False, "score_credit_authorized": False, "continuing_authority_created": False,
    }, "withheld_authority")
    _exact(value["claims"], {
        "phase_zero_complete": True, "phase_one_complete": True, "fresh_execution_authority_accepted": True, "phase_two_execution_authorized": True, "measurement_ready": True, "benchmark_built": False, "benchmark_executed": False, "hardware_or_device_accessed_by_ot121": False, "firmware_flashed_by_ot121": False, "radio_used_by_ot121": False, "key_or_entropy_operation_performed_by_ot121": False, "candidate_selected": False, "suite_selected": False, "wire_format_selected": False, "secure_lora_implemented": False, "support_proven": False, "compatibility_proven": False, "regulatory_compliance_proven": False, "production_ready": False, "physical_measurement_evidence_added": False, "score_credit_added": False,
    }, "claims")
    return {"schema": "OTCBXA1", "status": value["status"], "phase_two_execution_authorized": True, "measurement_ready": True, "benchmark_executed": False, "canonical_sha256": canonical_sha256(value)}


def main(argv: list[str] | None = None) -> int:
    parser = SafeParser()
    parser.add_argument("--authority", type=Path, default=AUTHORITY)
    try:
        args = parser.parse_args(argv)
        parents = validate_parent_files()
        pin = AUTHORITY_PIN if args.authority.resolve() == AUTHORITY.resolve() else None
        authority = load(args.authority, pin)
        result = validate_authority(authority, parents)
    except (ValidationError, OSError, KeyError, TypeError, ValueError, IndexError):
        print("ERROR: validation failed", file=sys.stderr)
        return 2
    print(json.dumps({**result, "authority_raw_sha256": hashlib.sha256(args.authority.read_bytes()).hexdigest()}, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
