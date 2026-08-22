#!/usr/bin/env python3
"""Fail-closed validator for the OT-113 direct-radio execution contract."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

SCHEMA = "OTRPX1"
VERSION = 1
CONTRACT_ID = "OT-113-OT005-US915-DIRECT-RADIO-PROFILE-EXECUTION-CONTRACT-V1"
ROOT = Path(__file__).resolve().parents[1]
CONTRACT_PATH = ROOT / "tests" / "benchmarks" / "crypto" / f"{CONTRACT_ID}.json"
EXPECTED_CONTRACT_SHA256 = "d73ebf7340c4351b5daa775d7cb9342f6650baf376f1c45944049d7efe49462c"
EXPECTED_CONTRACT_RAW_SHA256 = "c59fd52f8c1608f7e7dfdb5c166504bb7ad7fb02c6e82b3d3d0677c79cd2c87c"
MAX_BYTES = 131072
MAX_DEPTH = 18
MAX_ITEMS = 2048
_PRIVATE = re.compile(
    r"(?:[A-Za-z]:\\|/Users/|/home/|COM\d+|tty(?:USB|ACM)\d+|(?:[0-9A-F]{2}:){5}[0-9A-F]{2})",
    re.I,
)


class ContractError(ValueError):
    pass


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        raise ContractError("invalid arguments")


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ContractError("duplicate key")
        result[key] = value
    return result


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
        count = 1 + sum(_scan(item, depth + 1) for item in value)
    elif isinstance(value, dict):
        count = 1 + sum(
            _scan(key, depth + 1) + _scan(item, depth + 1)
            for key, item in value.items()
        )
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
    encoded = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _require(condition: bool) -> None:
    if not condition:
        raise ContractError("contract invariant mismatch")


def validate_contract(contract: dict[str, Any]) -> dict[str, Any]:
    _scan(contract)
    expected_keys = {
        "schema", "version", "artifact_kind", "contract_id", "accepted_date",
        "status", "public_result", "append_only_parents", "supersession",
        "bench_boundary", "commanded_profile", "wire_protocol",
        "ordered_execution", "receipt_contract", "authority",
        "future_evidence_and_admission", "prohibited_claims", "claims",
    }
    _require(set(contract) == expected_keys)
    _require(contract["schema"] == SCHEMA and type(contract["version"]) is int)
    _require(contract["version"] == VERSION)
    _require(contract["contract_id"] == CONTRACT_ID)
    _require(contract["artifact_kind"] == "direct_radio_profile_execution_contract")
    _require(contract["accepted_date"] == "2026-08-21")
    _require(contract["status"] == "executable_successor_contract_frozen_host_only")

    parents = contract["append_only_parents"]
    _require(parents == [
        {
            "path": "tests/benchmarks/crypto/OT-110-OT005-US915-DIRECT-RADIO-PROFILE-EVIDENCE-CONTRACT-V0.json",
            "raw_sha256": "8af36e000d5cd0478d1a829fb5a1f2b330cdf09bad188445d30579c348f7e2e1",
            "canonical_sha256": "d5b44cea761b12ad6422be250bf0a827469441643d6f5e944932a91cc92b68d9",
        },
        {
            "path": "tests/hardware/OT-112-2026-08-21.md",
            "raw_sha256": "7e2c6ffe263696fcaa71297b35f58177936ace011a2d2291ded910ceae40f19b",
        },
    ])
    for parent in parents:
        parent_path = ROOT / parent["path"]
        _require(hashlib.sha256(parent_path.read_bytes()).hexdigest() == parent["raw_sha256"])

    supersession = contract["supersession"]
    _require(supersession["scope"] == "execution_preconditions_only")
    _require(supersession["historical_evidence_superseded"] is False)
    _require(supersession["parent_contract_mutated"] is False)
    _require(supersession["all_other_otrpf0_boundaries_preserved"] is True)

    bench = contract["bench_boundary"]
    _require(bench["physical_nodes"] == 2)
    _require(bench["public_device_identifiers_prohibited"] is True)
    _require(bench["location"] == "owner_confirmed_usa_close_bench_no_coordinates")
    _require(bench["antennas"] == "owner_confirmed_attached_supplied_high_band_antennas")
    _require(bench["recovery"] == {
        "classification": "disposable_bench",
        "sufficient_route": "rom_bootloader_plus_committed_source_plus_exact_built_image",
        "preserve_or_restore_meshcore_required": False,
        "preserve_or_restore_prior_diagnostic_state_required": False,
        "permitted_post_test_state": "diagnostic_firmware_may_remain_installed",
    })

    profile = contract["commanded_profile"]
    _require({
        key: profile[key] for key in (
            "region_code", "frequency_hz", "bandwidth_hz", "spreading_factor",
            "coding_rate", "explicit_header", "crc_enabled",
            "low_data_rate_optimization", "sync_word", "preamble_symbols",
            "tx_power_command_setpoint_dbm", "receiver_start_required",
        )
    } == {
        "region_code": "US915", "frequency_hz": 915000000,
        "bandwidth_hz": 125000, "spreading_factor": 7, "coding_rate": "4/5",
        "explicit_header": True, "crc_enabled": True,
        "low_data_rate_optimization": False, "sync_word": "0x12",
        "preamble_symbols": 8, "tx_power_command_setpoint_dbm": 2,
        "receiver_start_required": True,
    })
    proof = profile["applied_profile_proof"]
    _require(proof["required"] == [
        "exact_successful_driver_command_chain_receipts",
        "successful_receiver_start_receipt",
        "bounded_ota_interoperability_before_restart",
        "bounded_ota_interoperability_after_restart",
    ])
    _require("register_readback" in proof["not_claimed"] and "eirp" in proof["not_claimed"])

    wire = contract["wire_protocol"]
    _require(wire["byte_order"] == "little_endian_for_multibyte_integers")
    data = wire["data"]
    _require(data["name"] == "OTD1" and data["header_bytes"] == 16)
    _require(data["raw_probe"] == {"total_wire_bytes": 1, "bytes_hex": "a5", "not_otd1": True})
    _require(data["protocol_test"] == {"fill_bytes": 147, "total_wire_bytes": 163})
    _require(data["measured_ceiling"] == {"fill_bytes": 239, "total_wire_bytes": 255})
    _require(data["oversize_request"] == {
        "requested_total_wire_bytes": 256, "local_reject": True,
        "tx_receipt_must_not_exist": True,
    })
    _require([entry["offset"] for entry in data["header_layout"]] == [0, 4, 5, 6, 7, 8, 12])
    ack = wire["ack"]
    _require(ack["name"] == "OTA1" and ack["total_wire_bytes"] == 16)
    _require(ack["bounded"] is True and ack["one_ack_per_valid_data_frame"] is True)

    _require(contract["ordered_execution"] == [
        {"order": 1, "operation": "off_air_preflight", "required_successes": 2, "transmit": False},
        {"order": 2, "operation": "configure_receivers", "required_successes": 2, "transmit": False},
        {"order": 3, "operation": "raw_probe", "frames_per_direction": 1, "data_wire_bytes": 1},
        {"order": 4, "operation": "protocol_test", "frames_per_direction": 100, "data_wire_bytes": 163},
        {"order": 5, "operation": "measured_ceiling", "frames_per_direction": 10, "data_wire_bytes": 255},
        {"order": 6, "operation": "oversize_local_reject", "requests_per_node": 1, "requested_wire_bytes": 256, "transmit": False},
        {"order": 7, "operation": "restart_both_nodes", "required_successes": 2, "transmit": False},
        {"order": 8, "operation": "post_restart_protocol_test", "frames_per_direction": 10, "data_wire_bytes": 163},
    ])

    receipts = contract["receipt_contract"]
    _require(receipts["machine_receipts_required"] is True)
    _require(set(receipts["runner_receipt_mapping"]) == {
        "BOOT", "PROFILE", "STATUS", "ARM", "ACK_ARM", "TX", "RX",
        "REJECT", "RX_ERROR", "RX_ARM", "RESTART", "SESSION_START",
        "SESSION_END",
    })
    _require(receipts["device_monotonic_timestamp_field"] == "mono_us")
    _require(receipts["per_frame_exact_reconciliation"] == [
        "session", "direction", "sequence", "data_wire_sha256", "ack_wire_sha256"
    ])
    _require(receipts["acceptance_counts"] == {
        "lost": 0, "duplicate": 0, "corrupt": 0, "unexpected": 0
    })
    _require(receipts["per_received_frame_metrics"] == ["rssi_dbm", "snr_db"])
    _require(receipts["protocol_rtt"]["start"].startswith("DATA_TX receipt"))
    _require(receipts["protocol_rtt"]["end"].startswith("matching bounded OTA1 ACK_RX"))
    _require(receipts["protocol_rtt"]["includes"] == "responder receive_validate_turnaround_and_ack_transmit")

    future = contract["future_evidence_and_admission"]
    _require(future == {
        "evidence_schema": "OTRPE1", "admission_schema": "OTRPA1",
        "evidence_must_bind": ["contract_raw_sha256", "contract_canonical_sha256"],
        "admission_must_bind": ["contract_raw_sha256", "contract_canonical_sha256", "evidence_raw_sha256", "evidence_canonical_sha256"],
        "independent_admission_required": True,
        "only_closable_requirement": "direct_radio_mtu_phy_region_unresolved",
        "admission_advances_readiness": False,
        "successor_readiness_decision_required": True,
        "new_executable_benchmark_plan_required": True,
    })
    authority = contract["authority"]
    _require(authority["not_authorized"] == [
        "benchmark_execution", "candidate_selection", "secure_lora",
        "packet_v1", "score_credit",
    ])
    claims = contract["claims"]
    _require(claims == {
        "contract_frozen": True, "physical_evidence_generated": False,
        "direct_radio_requirement_closed": False, "readiness_advanced": False,
        "benchmark_executed": False, "candidate_selected": False,
        "score_credit_added": False,
    })

    digest = canonical_sha256(contract)
    if EXPECTED_CONTRACT_SHA256 != "TO_BE_PINNED":
        _require(digest == EXPECTED_CONTRACT_SHA256)
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "contract_id": CONTRACT_ID,
        "status": contract["status"],
        "region_code": profile["region_code"],
        "physical_nodes": bench["physical_nodes"],
        "evidence_schema": future["evidence_schema"],
        "admission_schema": future["admission_schema"],
        "requirement_closed": claims["direct_radio_requirement_closed"],
        "readiness_advanced": claims["readiness_advanced"],
        "contract_canonical_sha256": digest,
    }


def main(argv: list[str] | None = None) -> int:
    parser = SafeArgumentParser(add_help=True)
    parser.add_argument("--contract", type=Path, default=CONTRACT_PATH)
    try:
        args = parser.parse_args(argv)
        expected_raw = (
            EXPECTED_CONTRACT_RAW_SHA256
            if EXPECTED_CONTRACT_RAW_SHA256 != "TO_BE_PINNED"
            else None
        )
        result = validate_contract(load(args.contract, expected_raw))
        result["contract_raw_sha256"] = hashlib.sha256(args.contract.read_bytes()).hexdigest()
    except (ContractError, OSError):
        print("ERROR: validation failed", file=sys.stderr)
        return 2
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
