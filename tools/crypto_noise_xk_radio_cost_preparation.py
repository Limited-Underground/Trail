#!/usr/bin/env python3
"""Fail-closed validator for the OT-152 Noise XK radio-cost preparation."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_RECORD = ROOT / "tests/benchmarks/crypto/OT-152-OT005-LIBSODIUM-NOISE-XK-RADIO-COST-PREPARATION-V0.json"
EXPECTED_RECORD_RAW_SHA256 = "f556e9c70a4e46afc12d4ff7cbfd3ea8ad95f9b3055313a1ffbbcefe21611fb4"
EXPECTED_RECORD_CANONICAL_SHA256 = "aba89cdc3e295ee33c73bb477a2d97ae758386142eb1c15f2a429aec1297c715"
MAX_BYTES = 131072
MAX_DEPTH = 20
MAX_ITEMS = 4096

PINS = {
    "benchmark_plan": ("tests/benchmarks/crypto/OT-116-OT005-EXECUTABLE-BENCHMARK-PLAN-V1.json", "0280570b74b2b505b5a92e7834b24136caa0956ff1c536d7638bff0a8b105f2a", "7844d7be1824784a0ff58ba58df25bda85c6059c445f217a4d520215bfff50a8"),
    "direct_radio_evidence": ("tests/benchmarks/crypto/OT-114-OT005-US915-DIRECT-RADIO-PROFILE-EVIDENCE-V1.json", "b6d2a7ce4ebe3ab233ebbc748ab7831ff12cf4d8f6504d2d7e23dae108bd5876", "ac7e77a4438772a4c5b5f2b17472b302a3520e186a21b88125a9314ee6998bf0"),
    "direct_radio_admission": ("tests/benchmarks/crypto/OT-114-OT005-US915-DIRECT-RADIO-PROFILE-ADMISSION-DELTA-V1.json", "19325f730b96b9dbeeb4f64682c4913e7586d1995ef419b26408d82be12ef266", "eecf2b821ef2c25274cc5d3a179494b1545eb4a859280a48459ebb83c79ed257"),
    "libsodium_operation_evidence": ("tests/benchmarks/crypto/OT-117-OT005-LIBSODIUM-API-CONFIG-OPERATION-EVIDENCE-V0.json", "b1daf950473ea9e86ddff16d8f28efbc0972582902d5b262686cf73d0907bf58", "6419ac77392aa7b7f295cbda7719a16581a617e7564c2afec6c03aac7b2fea90"),
    "libsodium_api_admission": ("tests/benchmarks/crypto/OT-117-OT005-LIBSODIUM-API-CONFIG-ADMISSION-DELTA-V0.json", "527c5e713a6d96d38b7dfbfb9d3e0ceb891f4652f8428e2ca65e0b8ea8f316d2", "6eb12a9cf44bdc37e6ac87081b1d256b94f5ee013459c3549606c56b5faf0527"),
    "libsodium_noise_result": ("tests/benchmarks/crypto/OT-122-OT005-LIBSODIUM-NOISE-RESOURCE-EXECUTION-RECEIPT-V0.json", "2b023c640bfbec8ad6eb5d1d63d65e8f1ad75dcfe593566aba6b3d468355a178", "3cf651719d1e5ce96dad85fc364f5cb9e1c43bdcb64d2ac45b748dcc67c18949"),
    "phase_two_reconciliation": ("tests/benchmarks/crypto/OT-148-OT005-PHASE-TWO-CORPUS-RECONCILIATION-V0.json", "beddc729f8449c3f2e3a09f62ba6947312f4e1893eee38abfa7a9a616f1bae1c", "dcc0b408fdcc02df70717cd93ac8e5133c7159563c15d1da0f74a6c1bb70d1d0"),
}

ADAPTER_PINS = {
    "header": ("tests/benchmarks/crypto/adapters/libsodium_noise_xk_v0/noise_xk_libsodium.h", "b7c649434cdffe648e467bb117849ae0296a73fa041d614d3d4ba32578e40c45"),
    "source": ("tests/benchmarks/crypto/adapters/libsodium_noise_xk_v0/noise_xk_libsodium.c", "8534fe1a6a4b68cd37e491ebd0f564dd38fd3935fb21d8f2d45aa8333ae442b8"),
}

_PRIVATE = re.compile(r"(?:[A-Za-z]:\\|/Users/|/home/|COM\d+|tty(?:USB|ACM)\d+|(?:[0-9A-F]{2}:){5}[0-9A-F]{2})", re.I)


class PreparationError(ValueError):
    pass


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        raise PreparationError("invalid arguments")


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise PreparationError("duplicate key")
        result[key] = value
    return result


def _reject_constant(_value: str) -> None:
    raise PreparationError("non-finite number")


def _scan(value: Any, depth: int = 0) -> int:
    if depth > MAX_DEPTH:
        raise PreparationError("structure too deep")
    if isinstance(value, str):
        if len(value) > 4096 or _PRIVATE.search(value):
            raise PreparationError("unsafe text")
        return 1
    if value is None or type(value) in (bool, int):
        return 1
    if isinstance(value, float):
        if not math.isfinite(value):
            raise PreparationError("non-finite number")
        return 1
    if isinstance(value, list):
        count = 1 + sum(_scan(item, depth + 1) for item in value)
    elif isinstance(value, dict):
        count = 1 + sum(_scan(key, depth + 1) + _scan(item, depth + 1) for key, item in value.items())
    else:
        raise PreparationError("unsupported value")
    if count > MAX_ITEMS:
        raise PreparationError("structure too large")
    return count


def canonical_sha256(value: Any) -> str:
    raw = json.dumps(value, ensure_ascii=False, allow_nan=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(raw).hexdigest()


def _need(condition: bool, message: str) -> None:
    if not condition:
        raise PreparationError(message)


def _load_json(path: Path, expected_raw: str | None = None, expected_canonical: str | None = None) -> tuple[bytes, dict[str, Any]]:
    raw = path.read_bytes()
    _need(bool(raw) and len(raw) <= MAX_BYTES, f"invalid file size: {path.name}")
    if expected_raw is not None:
        _need(hashlib.sha256(raw).hexdigest() == expected_raw, f"raw digest mismatch: {path.name}")
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs, parse_constant=_reject_constant)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise PreparationError(f"invalid JSON: {path.name}") from exc
    _need(isinstance(value, dict), f"root must be object: {path.name}")
    _scan(value)
    if expected_canonical is not None:
        _need(canonical_sha256(value) == expected_canonical, f"canonical digest mismatch: {path.name}")
    return raw, value


def load_record(path: Path = DEFAULT_RECORD) -> dict[str, Any]:
    return _load_json(path)[1]


def lora_airtime_us(payload_bytes: int, profile: dict[str, Any]) -> int:
    _need(type(payload_bytes) is int and 0 <= payload_bytes <= 255, "invalid payload length")
    sf = profile["spreading_factor"]
    bandwidth = profile["bandwidth_hz"]
    de = 1 if profile["low_data_rate_optimization"] else 0
    implicit_header = 0 if profile["explicit_header"] else 1
    crc = 1 if profile["crc_enabled"] else 0
    symbol_us_numerator = (1 << sf) * 1_000_000
    _need(symbol_us_numerator % bandwidth == 0, "nonintegral symbol duration")
    symbol_us = symbol_us_numerator // bandwidth
    numerator = 8 * payload_bytes - 4 * sf + 28 + 16 * crc - 20 * implicit_header
    denominator = 4 * (sf - 2 * de)
    encoded_blocks = max(0, (numerator + denominator - 1) // denominator)
    payload_symbols = 8 + encoded_blocks * profile["coding_rate_denominator"]
    preamble_us = ((profile["preamble_symbols"] * 4 + 17) * symbol_us) // 4
    return preamble_us + payload_symbols * symbol_us


def response_timeout_ms(outbound_bytes: int, response_bytes: int, profile: dict[str, Any], model: dict[str, Any]) -> int:
    total_us = lora_airtime_us(outbound_bytes, profile) + lora_airtime_us(response_bytes, profile)
    total_us += (model["responder_turnaround_bound_ms"] + model["scheduling_margin_ms"]) * 1000
    return (total_us + 999) // 1000


def _validate_bindings(record: dict[str, Any]) -> dict[str, dict[str, Any]]:
    _need(set(record["bindings"]) == set(PINS), "binding set mismatch")
    loaded: dict[str, dict[str, Any]] = {}
    for name, (relative_path, raw_pin, canonical_pin) in PINS.items():
        _need(record["bindings"][name] == {"path": relative_path, "raw_sha256": raw_pin, "canonical_sha256": canonical_pin}, f"binding mismatch: {name}")
        loaded[name] = _load_json(ROOT / relative_path, raw_pin, canonical_pin)[1]
    return loaded


def _header_constants() -> dict[str, int]:
    relative_path, raw_pin = ADAPTER_PINS["header"]
    raw = (ROOT / relative_path).read_bytes()
    _need(hashlib.sha256(raw).hexdigest() == raw_pin, "adapter header digest mismatch")
    text = raw.decode("utf-8")
    result: dict[str, int] = {}
    for name in ("OT_NOISE_XK_MESSAGE_1_BYTES", "OT_NOISE_XK_MESSAGE_2_BYTES", "OT_NOISE_XK_MESSAGE_3_BYTES", "OT_NOISE_XK_TOTAL_HANDSHAKE_BYTES"):
        match = re.search(rf"^#define {name} (\d+)U$", text, re.M)
        _need(match is not None, f"missing adapter constant: {name}")
        result[name] = int(match.group(1))
    source_path, source_pin = ADAPTER_PINS["source"]
    _need(hashlib.sha256((ROOT / source_path).read_bytes()).hexdigest() == source_pin, "adapter source digest mismatch")
    return result


def validate(record: dict[str, Any]) -> dict[str, Any]:
    _scan(record)
    _need(record["schema"] == "OTNXRP0" and record["version"] == 0, "schema mismatch")
    _need(record["artifact_kind"] == "libsodium_noise_xk_radio_cost_measurement_preparation", "artifact kind mismatch")
    _need(record["record_id"] == "OT-152-OT005-LIBSODIUM-NOISE-XK-RADIO-COST-PREPARATION-V0", "record id mismatch")
    _need(record["recorded_date"] == "2026-08-27", "recorded date mismatch")
    _need(record["status"] == "host_only_measurement_contract_frozen_execution_not_authorized", "status mismatch")
    parents = _validate_bindings(record)

    plan = parents["benchmark_plan"]
    phase_two = next(item for item in plan["ordered_phases"] if item["phase"] == 2)
    _need(phase_two["required_radio_measurements"] == ["handshake_total_wire_bytes", "fragments", "measured_airtime_us", "bounded_retry_result"], "Phase 2 radio surface drift")
    _need(plan["authority"]["radio_transmit_authorized"] is False, "parent radio authority drift")

    radio_evidence = parents["direct_radio_evidence"]
    radio_admission = parents["direct_radio_admission"]
    _need(radio_evidence["acceptance"]["contract_satisfied"] is True, "radio evidence not accepted")
    _need(radio_admission["admission"]["only_closed_requirement"] == "direct_radio_mtu_phy_region_unresolved", "radio admission drift")
    _need(radio_admission["admission"]["remaining_requirement_count"] == 0, "radio requirement not closed")

    operation_evidence = parents["libsodium_operation_evidence"]
    noise_operation = next(item for item in operation_evidence["operation_records"] if item["operation_id"] == "noise_xk_handshake")
    _need(noise_operation["state"] == "eligible", "Noise XK operation not eligible")
    _need(operation_evidence["adapter"]["schema"] == "OTNXK0" and operation_evidence["adapter"]["benchmark_only"] is True, "adapter admission drift")
    _need(parents["libsodium_api_admission"]["admitted_candidate"]["selection_eligible"] is True, "libsodium admission drift")

    noise_result = parents["libsodium_noise_result"]
    _need(noise_result["node_count"] == 2 and noise_result["restoration_complete"] is True, "Noise XK result state mismatch")
    _need(noise_result["claims"]["radio_used"] is False and noise_result["claims"]["phase_two_complete"] is False, "Noise XK result overclaim")
    for node in noise_result["nodes"]:
        summary = node["local_primitive_result"]
        _need("noise_xk_handshake" in summary["summaries"] and summary["operations_completed"] == 8, "Noise XK result missing")

    reconciliation = parents["phase_two_reconciliation"]
    unresolved = "noise_xk_handshake_wire_bytes_fragments_measured_airtime_and_bounded_retry_result"
    _need(unresolved in reconciliation["unresolved_required_evidence"], "radio blocker missing")
    _need(reconciliation["claims"]["phase_two_complete"] is False, "Phase 2 parent overclaim")

    constants = _header_constants()
    _need(constants == {
        "OT_NOISE_XK_MESSAGE_1_BYTES": 48,
        "OT_NOISE_XK_MESSAGE_2_BYTES": 48,
        "OT_NOISE_XK_MESSAGE_3_BYTES": 64,
        "OT_NOISE_XK_TOTAL_HANDSHAKE_BYTES": 160,
    }, "adapter message constants drift")
    adapter = record["adapter"]
    _need(adapter == {
        "schema": "OTNXK0", "version": 0, "protocol_name": "Noise_XK_25519_ChaChaPoly_SHA256",
        "header_path": ADAPTER_PINS["header"][0], "header_raw_sha256": ADAPTER_PINS["header"][1],
        "source_path": ADAPTER_PINS["source"][0], "source_raw_sha256": ADAPTER_PINS["source"][1],
        "benchmark_only": True,
    }, "adapter binding mismatch")

    profile = record["radio_profile"]
    expected_profile = {
        "region_code": "US915", "frequency_hz": 915000000, "bandwidth_hz": 125000,
        "spreading_factor": 7, "coding_rate_denominator": 5, "explicit_header": True,
        "crc_enabled": True, "low_data_rate_optimization": False, "sync_word": "0x12",
        "preamble_symbols": 8, "tx_power_command_setpoint_dbm": 2,
        "admitted_direct_total_wire_ceiling_bytes": 255, "calibrated_rf_readback": False,
    }
    _need(profile == expected_profile, "radio profile mismatch")
    _need({key: radio_evidence["profile"][key] for key in ("region_code", "frequency_hz", "bandwidth_hz", "spreading_factor", "coding_rate_denominator", "explicit_header", "crc_enabled", "low_data_rate_optimization", "sync_word", "preamble_symbols", "tx_power_command_setpoint_dbm")} == {key: profile[key] for key in ("region_code", "frequency_hz", "bandwidth_hz", "spreading_factor", "coding_rate_denominator", "explicit_header", "crc_enabled", "low_data_rate_optimization", "sync_word", "preamble_symbols", "tx_power_command_setpoint_dbm")}, "accepted radio profile drift")
    _need(radio_evidence["execution_result"]["direct_payload_ceiling_bytes"] == profile["admitted_direct_total_wire_ceiling_bytes"], "direct ceiling drift")

    messages = record["handshake_surface"]["messages"]
    _need(messages == [
        {"order": 1, "message": "message_1", "direction": "initiator_to_responder", "wire_bytes": 48, "radio_frames": 1},
        {"order": 2, "message": "message_2", "direction": "responder_to_initiator", "wire_bytes": 48, "radio_frames": 1},
        {"order": 3, "message": "message_3", "direction": "initiator_to_responder", "wire_bytes": 64, "radio_frames": 1},
    ], "handshake message surface mismatch")
    _need(record["handshake_surface"] == {
        "messages": messages, "handshake_total_wire_bytes": 160, "message_count": 3,
        "fragments": 3, "per_message_fragment_count": [1, 1, 1],
        "message_fragmentation_required": False, "separate_ota1_ack_frames_used": False,
        "packet_v1_framing_selected": False,
        "measurement_payload_scope": "exact_raw_benchmark_only_noise_xk_messages_as_lora_payload_bytes",
    }, "handshake surface mismatch")
    _need(all(message["wire_bytes"] <= profile["admitted_direct_total_wire_ceiling_bytes"] for message in messages), "message exceeds direct ceiling")

    model = record["airtime_model"]
    airtime_48 = lora_airtime_us(48, profile)
    airtime_64 = lora_airtime_us(64, profile)
    _need(airtime_48 == 97536 and airtime_64 == 118016, "airtime calculation drift")
    _need(lora_airtime_us(163, profile) == 266496 and lora_airtime_us(255, profile) == 399616, "OT-114 airtime reproduction drift")
    _need(model == {
        "model": "semtech_lora_explicit_header_payload_airtime", "symbol_duration_us": 1024,
        "preamble_airtime_us": 12544, "payload_airtime_us_by_wire_bytes": {"48": 97536, "64": 118016},
        "successful_handshake_theoretical_airtime_us": 313088,
        "response_timeout_policy": "ceil(outbound_airtime_plus_expected_response_airtime_plus_responder_turnaround_plus_scheduling_margin)",
        "responder_turnaround_bound_ms": 500, "scheduling_margin_ms": 1500,
        "message_2_response_timeout_ms": 2196, "message_3_response_timeout_ms": 2216,
        "measured_airtime_definition": "sum_of_each_successful_tx_done_mono_us_minus_tx_start_mono_us",
        "measured_airtime_is_calibrated_rf_airtime": False,
    }, "airtime model mismatch")
    _need(response_timeout_ms(48, 48, profile, model) == 2196, "message 2 timeout mismatch")
    _need(response_timeout_ms(48, 64, profile, model) == 2216, "message 3 timeout mismatch")

    _need(record["role_reversed_execution"] == [
        {"cycle": 1, "initiator": "A", "responder": "B"},
        {"cycle": 2, "initiator": "B", "responder": "A"},
    ], "role reversal mismatch")
    _need(record["baseline_scenario_per_role_cycle"] == {
        "attempts": 1, "retry_count": 0,
        "expected_result": "handshake_completed_and_split_keys_cross_match",
        "handshake_total_wire_bytes": 160, "fragments": 3, "theoretical_airtime_us": 313088,
    }, "baseline scenario mismatch")
    _need(record["bounded_retry_scenario_per_role_cycle"] == {
        "maximum_attempts": 2, "maximum_retries": 1,
        "forced_first_attempt_boundary": "message_1_transmitted_and_received_message_2_intentionally_withheld",
        "first_attempt_wire_bytes": 48, "first_attempt_fragments": 1,
        "retry_trigger": "message_2_response_timeout_at_2196_ms",
        "retry_scope": "abort_wipe_and_restart_whole_handshake_from_message_1",
        "fresh_attempt_identity_required": True, "stale_first_attempt_frames_must_be_rejected": True,
        "second_attempt_expected_result": "handshake_completed_and_split_keys_cross_match",
        "total_wire_bytes_including_retry": 208, "fragments_including_retry": 4,
        "theoretical_airtime_us_including_retry": 410624,
        "expected_retry_result": "one_timeout_one_retry_final_success",
    }, "bounded retry scenario mismatch")
    _need(record["complete_execution_totals"] == {
        "role_cycles": 2, "baseline_handshakes": 2, "bounded_retry_handshakes": 2,
        "forced_timeouts": 2, "successful_final_handshakes": 4,
        "handshake_total_wire_bytes_per_success": 160, "radio_payload_wire_bytes": 736,
        "fragments": 14, "theoretical_airtime_us": 1447424,
    }, "complete execution totals mismatch")

    receipt = record["future_receipt_contract"]
    _need(receipt["required_frame_fields"] == ["cycle", "scenario", "attempt", "message", "direction", "wire_bytes", "payload_sha256", "tx_start_mono_us", "tx_done_mono_us", "measured_airtime_us"], "frame receipt fields mismatch")
    _need(receipt["required_summary_fields"] == ["handshake_total_wire_bytes", "fragments", "measured_airtime_us", "theoretical_airtime_us", "bounded_retry_result"], "summary receipt fields mismatch")
    _need(all(receipt[key] is True for key in ("exact_profile_command_receipts_required", "receiver_start_receipts_required", "per_frame_receive_and_stage_admission_required", "both_role_cycles_required", "temporary_secret_wipe_after_forced_abort_required", "raw_traces_retained_privately", "public_raw_payloads_prohibited", "public_device_identifiers_prohibited")), "receipt safety boundary mismatch")
    _need(all(receipt[key] == 0 for key in ("lost", "duplicates", "corrupt", "unexpected")), "receipt error allowance mismatch")

    _need(record["next_gate"] == {
        "work_item": "OT-153",
        "scope": "freeze_exact_executable_bundle_and_separately_accept_one_bounded_two_node_radio_authority_before_execution",
        "new_immutable_firmware_runner_and_restoration_binding_required": True,
        "fresh_explicit_non_reusable_authority_required": True,
        "success_or_abort_consumes_authority": True,
        "exact_trail_restoration_required": True,
    }, "next gate mismatch")
    authority = record["authority"]
    _need(authority["host_contract_validation_authorized"] is True, "host authority missing")
    _need(all(value is False for key, value in authority.items() if key != "host_contract_validation_authorized"), "unauthorized authority granted")
    claims = record["claims"]
    _need(claims["measurement_contract_prepared"] is True, "preparation claim missing")
    _need(all(claims[key] is False for key in claims if key != "measurement_contract_prepared"), "result or completion overclaim")

    canonical = canonical_sha256(record)
    if EXPECTED_RECORD_CANONICAL_SHA256 != "TO_BE_PINNED":
        _need(canonical == EXPECTED_RECORD_CANONICAL_SHA256, "record canonical digest mismatch")
    return {
        "schema": record["schema"],
        "status": record["status"],
        "handshake_total_wire_bytes": 160,
        "fragments": 3,
        "successful_handshake_theoretical_airtime_us": 313088,
        "bounded_retry_total_wire_bytes": 208,
        "bounded_retry_fragments": 4,
        "bounded_retry_theoretical_airtime_us": 410624,
        "role_cycles": 2,
        "execution_authorized": False,
        "radio_measurement_admitted": False,
        "canonical_sha256": canonical,
    }


def main(argv: list[str] | None = None) -> int:
    parser = SafeArgumentParser()
    parser.add_argument("--record", type=Path, default=DEFAULT_RECORD)
    try:
        args = parser.parse_args(argv)
        raw, record = _load_json(args.record)
        if args.record.resolve() == DEFAULT_RECORD.resolve() and EXPECTED_RECORD_RAW_SHA256 != "TO_BE_PINNED":
            _need(hashlib.sha256(raw).hexdigest() == EXPECTED_RECORD_RAW_SHA256, "record raw digest mismatch")
        print(json.dumps(validate(record), sort_keys=True, separators=(",", ":")))
    except (OSError, KeyError, StopIteration, TypeError, PreparationError) as exc:
        print(json.dumps({"schema": "OTNXRP0", "error": str(exc)}, sort_keys=True, separators=(",", ":")))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
