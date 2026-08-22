#!/usr/bin/env python3
"""Adversarial tests for the fail-closed OT-113 execution contract."""

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

import crypto_radio_profile_execution_contract as execution_contract  # noqa: E402

CONTRACT_PATH = execution_contract.CONTRACT_PATH


def fixture() -> dict:
    return execution_contract.load(CONTRACT_PATH)


def expect_error(action, label: str) -> None:
    try:
        action()
    except execution_contract.ContractError:
        return
    raise AssertionError(f"expected OT-113 rejection: {label}")


def changed(path: tuple[object, ...], value: object) -> dict:
    result = copy.deepcopy(fixture())
    cursor = result
    for part in path[:-1]:
        cursor = cursor[part]  # type: ignore[index]
    cursor[path[-1]] = value  # type: ignore[index]
    return result


def reject(path: tuple[object, ...], value: object, label: str) -> None:
    expect_error(lambda: execution_contract.validate_contract(changed(path, value)), label)


def test_contract_is_exact_host_only_and_append_only() -> None:
    contract = fixture()
    result = execution_contract.validate_contract(contract)
    assert result["schema"] == "OTRPX1" and result["version"] == 1
    assert result["requirement_closed"] is False
    assert result["readiness_advanced"] is False
    assert result["evidence_schema"] == "OTRPE1"
    assert result["admission_schema"] == "OTRPA1"
    assert result["contract_canonical_sha256"] == execution_contract.EXPECTED_CONTRACT_SHA256
    assert hashlib.sha256(CONTRACT_PATH.read_bytes()).hexdigest() == execution_contract.EXPECTED_CONTRACT_RAW_SHA256
    for parent in contract["append_only_parents"]:
        assert hashlib.sha256((ROOT / parent["path"]).read_bytes()).hexdigest() == parent["raw_sha256"]
    reject(("append_only_parents", 0, "raw_sha256"), "00" * 32, "mutated OTRPF0 parent")
    reject(("append_only_parents", 0, "canonical_sha256"), "11" * 32, "mutated OTRPF0 canonical parent")
    reject(("append_only_parents", 1, "raw_sha256"), "22" * 32, "mutated OT-112 evidence parent")


def test_supersession_is_narrow_and_history_is_preserved() -> None:
    contract = fixture()
    supersession = contract["supersession"]
    assert supersession["scope"] == "execution_preconditions_only"
    assert supersession["historical_evidence_superseded"] is False
    assert "successor_recovery_contract_and_prewrite_restore_evidence_precondition" in supersession["superseded_otrpf0_requirements"]
    reject(("supersession", "scope"), "all_parent_requirements", "broad supersession")
    reject(("supersession", "historical_evidence_superseded"), True, "historical erasure")
    reject(("supersession", "all_other_otrpf0_boundaries_preserved"), False, "unbounded successor")


def test_disposable_bench_and_profile_are_exact() -> None:
    contract = fixture()
    recovery = contract["bench_boundary"]["recovery"]
    assert recovery["sufficient_route"] == "rom_bootloader_plus_committed_source_plus_exact_built_image"
    assert recovery["preserve_or_restore_meshcore_required"] is False
    assert recovery["permitted_post_test_state"] == "diagnostic_firmware_may_remain_installed"
    assert contract["commanded_profile"]["tx_power_command_setpoint_dbm"] == 2
    assert contract["commanded_profile"]["applied_profile_proof"]["required"][-1] == "bounded_ota_interoperability_after_restart"
    reject(("bench_boundary", "location"), "exact_coordinates", "coordinate publication")
    reject(("bench_boundary", "recovery", "preserve_or_restore_meshcore_required"), True, "MeshCore restore duty")
    reject(("commanded_profile", "frequency_hz"), 914999999, "frequency drift")
    reject(("commanded_profile", "tx_power_command_setpoint_dbm"), 22, "power drift")
    reject(("commanded_profile", "applied_profile_proof", "required", 0), "register_readback", "readback substitution")


def test_wire_bytes_and_ordered_counts_are_immutable() -> None:
    contract = fixture()
    data = contract["wire_protocol"]["data"]
    assert data["raw_probe"] == {"total_wire_bytes": 1, "bytes_hex": "a5", "not_otd1": True}
    assert data["header_bytes"] == 16
    assert data["protocol_test"] == {"fill_bytes": 147, "total_wire_bytes": 163}
    assert data["measured_ceiling"] == {"fill_bytes": 239, "total_wire_bytes": 255}
    assert data["oversize_request"]["requested_total_wire_bytes"] == 256
    assert contract["wire_protocol"]["ack"]["name"] == "OTA1"
    sequence = contract["ordered_execution"]
    assert [step["order"] for step in sequence] == list(range(1, 9))
    assert sequence[0]["operation"] == "off_air_preflight" and sequence[0]["transmit"] is False
    assert sequence[1]["operation"] == "configure_receivers" and sequence[1]["transmit"] is False
    assert [sequence[index].get("frames_per_direction") for index in (2, 3, 4, 7)] == [1, 100, 10, 10]
    reject(("wire_protocol", "data", "raw_probe", "bytes_hex"), "5a", "probe byte drift")
    reject(("wire_protocol", "data", "protocol_test", "fill_bytes"), 163, "header excluded from total")
    reject(("wire_protocol", "data", "measured_ceiling", "total_wire_bytes"), 254, "ceiling drift")
    reject(("wire_protocol", "data", "oversize_request", "tx_receipt_must_not_exist"), False, "oversize TX")
    reject(("ordered_execution", 3, "frames_per_direction"), 99, "truncated 163-byte run")
    reject(("ordered_execution", 6, "operation"), "skip_restart", "restart bypass")


def test_receipts_reconciliation_airtime_and_rtt_fail_closed() -> None:
    contract = fixture()
    receipts = contract["receipt_contract"]
    assert set(receipts["runner_receipt_mapping"]) == {
        "BOOT", "PROFILE", "STATUS", "ARM", "ACK_ARM", "TX", "RX",
        "REJECT", "RX_ERROR", "RX_ARM", "RESTART", "SESSION_START",
        "SESSION_END",
    }
    assert receipts["device_monotonic_timestamp_field"] == "mono_us"
    assert receipts["per_frame_exact_reconciliation"] == [
        "session", "direction", "sequence", "data_wire_sha256", "ack_wire_sha256"
    ]
    assert receipts["acceptance_counts"] == {"lost": 0, "duplicate": 0, "corrupt": 0, "unexpected": 0}
    assert "theoretical LoRa airtime" in receipts["timeout_policy"]
    assert receipts["protocol_rtt"]["start"].startswith("DATA_TX receipt")
    assert receipts["protocol_rtt"]["end"].startswith("matching bounded OTA1 ACK_RX")
    assert receipts["protocol_rtt"]["includes"] == "responder receive_validate_turnaround_and_ack_transmit"
    reject(("receipt_contract", "machine_receipts_required"), False, "human-only log")
    reject(("receipt_contract", "acceptance_counts", "lost"), 1, "packet loss")
    reject(("receipt_contract", "per_frame_exact_reconciliation", 3), "payload_length", "hash reconciliation removed")
    reject(("receipt_contract", "protocol_rtt", "start"), "command_issue_time", "incorrect RTT start")
    reject(("receipt_contract", "protocol_rtt", "includes"), "sender_only", "turnaround excluded")


def test_authority_admission_and_claim_boundaries_cannot_expand() -> None:
    contract = fixture()
    assert contract["authority"]["not_authorized"] == [
        "benchmark_execution", "candidate_selection", "secure_lora", "packet_v1", "score_credit"
    ]
    future = contract["future_evidence_and_admission"]
    assert future["evidence_must_bind"] == ["contract_raw_sha256", "contract_canonical_sha256"]
    assert future["admission_must_bind"] == [
        "contract_raw_sha256", "contract_canonical_sha256",
        "evidence_raw_sha256", "evidence_canonical_sha256",
    ]
    assert future["only_closable_requirement"] == "direct_radio_mtu_phy_region_unresolved"
    assert future["successor_readiness_decision_required"] is True
    assert future["new_executable_benchmark_plan_required"] is True
    reject(("authority", "not_authorized"), ["score_credit"], "expanded authority")
    reject(("future_evidence_and_admission", "admission_must_bind"), ["evidence_raw_sha256"], "unbound admission")
    reject(("future_evidence_and_admission", "admission_advances_readiness"), True, "false readiness")
    reject(("claims", "direct_radio_requirement_closed"), True, "self admission")
    reject(("claims", "score_credit_added"), True, "score claim")


def test_duplicate_private_and_raw_equivalent_inputs_are_rejected() -> None:
    for value in ("COM44", "C:" + "\\Users\\operator\\secret.json", "/home/operator/evidence"):
        reject(("public_result",), value, "private input")
    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        duplicate = temp / "duplicate.json"
        duplicate.write_text('{"schema":"OTRPX1","schema":"OTRPX1"}', encoding="utf-8")
        expect_error(lambda: execution_contract.load(duplicate), "duplicate key")
        reformatted = temp / CONTRACT_PATH.name
        reformatted.write_text(json.dumps(fixture(), indent=1) + "\n", encoding="utf-8")
        assert execution_contract.canonical_sha256(execution_contract.load(reformatted)) == execution_contract.EXPECTED_CONTRACT_SHA256
        expect_error(
            lambda: execution_contract.load(reformatted, execution_contract.EXPECTED_CONTRACT_RAW_SHA256),
            "canonical-equivalent raw substitution",
        )


def test_cli_is_pinned_and_errors_are_sanitized() -> None:
    command = [sys.executable, str(execution_contract.__file__), "--contract", str(CONTRACT_PATH)]
    completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    assert completed.returncode == 0, completed.stderr
    result = json.loads(completed.stdout)
    assert result["contract_raw_sha256"] == execution_contract.EXPECTED_CONTRACT_RAW_SHA256
    assert result["contract_canonical_sha256"] == execution_contract.EXPECTED_CONTRACT_SHA256
    hostile = subprocess.run(
        [sys.executable, str(execution_contract.__file__), "--private=C:" + "\\Users\\operator\\secret"],
        cwd=ROOT, capture_output=True, text=True,
    )
    assert hostile.returncode == 2 and hostile.stdout == ""
    assert hostile.stderr.strip() == "ERROR: validation failed"
    assert "Users" not in hostile.stderr and "Traceback" not in hostile.stderr


def main() -> int:
    tests = (
        test_contract_is_exact_host_only_and_append_only,
        test_supersession_is_narrow_and_history_is_preserved,
        test_disposable_bench_and_profile_are_exact,
        test_wire_bytes_and_ordered_counts_are_immutable,
        test_receipts_reconciliation_airtime_and_rtt_fail_closed,
        test_authority_admission_and_claim_boundaries_cannot_expand,
        test_duplicate_private_and_raw_equivalent_inputs_are_rejected,
        test_cli_is_pinned_and_errors_are_sanitized,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} OT-113 execution-contract scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
