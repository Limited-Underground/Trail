#!/usr/bin/env python3
"""Fail-closed admission tests for the Heltec protected-storage transition."""

from __future__ import annotations

import csv
import hashlib
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "firmware" / "targets" / "heltec_v4_bench"
ACTIVE_TABLE = TARGET / "partitions.csv"
CANDIDATE_TABLE = TARGET / "protected-storage-partitions.candidate.csv"
TRANSITION_PLAN = TARGET / "protected-storage-transition-plan.json"
READ_PLAN = TARGET / "protected-storage-transition-read-plan.json"
PROVISIONING_PLAN = TARGET / "protected-storage-provisioning-plan.json"
READ_TOOL = ROOT / "tools" / "Get-HeltecV4ProtectedStorageEvidence.ps1"
GIT_ATTRIBUTES = ROOT / ".gitattributes"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def table_rows(path: Path) -> list[dict]:
    with path.open(newline="", encoding="utf-8") as handle:
        rows = [row for row in csv.reader(handle)
                if row and not row[0].lstrip().startswith("#")]
    return [
        {
            "name": row[0],
            "type": row[1],
            "subtype": row[2],
            "offset": int(row[3], 0),
            "size_bytes": int(row[4], 0),
            "flags": [value for value in row[5:] if value],
        }
        for row in rows
    ]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def evaluate_source_region(data: bytes | None, plan: dict) -> str:
    evidence = plan["source_region_evidence"]
    if data is None:
        return evidence["results"]["unknown"]
    required = evidence["required_pattern"]
    if len(data) != required["byte_count"]:
        return evidence["results"]["size_mismatch"]
    if any(value != required["byte_value"] for value in data):
        return evidence["results"]["non_ff"]
    return evidence["results"]["all_ff"]


def test_exact_layout_transition() -> None:
    plan = load_json(TRANSITION_PLAN)
    require(plan["schema"] == "OTPST0/v0",
            "unexpected transition-plan schema")
    require(plan["status"] == "DESIGN-ONLY-ADMISSION-CLOSED" and
            plan["as_of"] == "2026-08-17" and
            plan["target"] == "heltec_v4_bench",
            "transition plan must remain dated, target-bound, and closed")
    require(plan["active_target_configuration_changed"] is False,
            "transition plan must not claim an active target change")

    attributes = GIT_ATTRIBUTES.read_text(encoding="utf-8").splitlines()
    require(
        "firmware/targets/heltec_v4_bench/partitions.csv text eol=crlf" in
        attributes and
        "firmware/targets/heltec_v4_bench/"
        "protected-storage-partitions.candidate.csv text eol=lf" in
        attributes,
        "partition-table artifact line endings must remain deterministic")

    source = plan["transition"]["source_layout"]
    candidate = plan["transition"]["candidate_layout"]
    require(source["schema"] == "OTHP0/v0" and
            source["table"] == ACTIVE_TABLE.name and
            source["sha256"] ==
            "4F064C125AA641697E0539EAF9EDA9D1CDECAB46DD8FF387988B900F3EFE2389" and
            source["byte_length"] == 452 and
            source["flash_size_bytes"] == 16777216,
            "source layout identity changed")
    require(candidate["schema"] == "OTPS0/v0" and
            candidate["table"] == CANDIDATE_TABLE.name and
            candidate["sha256"] ==
            "310FD207687C6D9964F8C1BF83031ACFD5EAFD837E7DBFCFF429A5CEE168C3CA" and
            candidate["byte_length"] == 411 and
            candidate["flash_size_bytes"] == 16777216,
            "candidate layout identity changed")

    require(sha256(ACTIVE_TABLE) == source["sha256"] and
            ACTIVE_TABLE.stat().st_size == source["byte_length"] and
            table_rows(ACTIVE_TABLE) == source["rows"],
            "active OTHP0 table no longer matches the transition contract")
    require(sha256(CANDIDATE_TABLE) == candidate["sha256"] and
            CANDIDATE_TABLE.stat().st_size == candidate["byte_length"] and
            table_rows(CANDIDATE_TABLE) == candidate["rows"],
            "candidate OTPS0 table no longer matches the transition contract")

    require(source["rows"][:-1] == candidate["rows"][:4],
            "transition must preserve every partition before the final 1 MiB")
    source_tail = source["rows"][-1]
    candidate_tail = candidate["rows"][4:]
    require(source_tail == {
        "name": "ot_state", "type": "0x40", "subtype": "0x00",
        "offset": 15728640, "size_bytes": 1048576, "flags": [],
    }, "source transition region changed")
    require(candidate_tail == [
        {
            "name": "ot_auth", "type": "data", "subtype": "nvs",
            "offset": 15728640, "size_bytes": 65536,
            "flags": ["encrypted"],
        },
        {
            "name": "ot_state", "type": "0x40", "subtype": "0x00",
            "offset": 15794176, "size_bytes": 983040, "flags": [],
        },
    ], "candidate transition region changed")
    require(sum(row["size_bytes"] for row in candidate_tail) ==
            source_tail["size_bytes"] and
            candidate_tail[0]["offset"] == source_tail["offset"] and
            candidate_tail[-1]["offset"] + candidate_tail[-1]["size_bytes"] ==
            source_tail["offset"] + source_tail["size_bytes"],
            "candidate must exactly subdivide the source 1 MiB region")


def test_evidence_identity_and_digest_binding() -> None:
    plan = load_json(TRANSITION_PLAN)
    binding = plan["evidence_binding"]
    require(binding["requirements"] == {
        "same_nonzero_operation_id": True,
        "same_nonzero_evidence_set_id": True,
        "nonzero_installed_generation": True,
        "nonzero_source_generation": True,
        "nonzero_recovery_generation": True,
        "source_layout_sha256":
            "4F064C125AA641697E0539EAF9EDA9D1CDECAB46DD8FF387988B900F3EFE2389",
        "candidate_layout_sha256":
            "310FD207687C6D9964F8C1BF83031ACFD5EAFD837E7DBFCFF429A5CEE168C3CA",
        "blank_source_region_sha256":
            "F5FB04AA5B882706B9309E885F19477261336EF76A150C3B4D3489DFAC3953EC",
        "recovery_partition_table_sha256":
            "4F064C125AA641697E0539EAF9EDA9D1CDECAB46DD8FF387988B900F3EFE2389",
        "nonzero_recovery_application_sha256": True,
        "nonzero_rom_recovery_route_id": True,
        "authority_binds_all_generations_and_digests": True,
    }, "transition evidence binding changed")
    require(binding["current"] == {
        "operation_id": None,
        "evidence_set_id": None,
        "installed_generation": None,
        "source_generation": None,
        "recovery_generation": None,
        "recovery_application_sha256": None,
        "rom_recovery_route_id": None,
    } and binding["current_result"] == "DENY",
            "current transition must have no reusable evidence identity")

    identities = [None, 0, 1]
    require(all(not (operation and evidence_set)
                for operation, evidence_set in (
                    (identities[0], 1), (1, identities[1]), (0, 0))),
            "missing or zero operation/evidence-set identity must deny")
    require(binding["requirements"]["source_layout_sha256"] ==
            plan["transition"]["source_layout"]["sha256"] and
            binding["requirements"]["candidate_layout_sha256"] ==
            plan["transition"]["candidate_layout"]["sha256"],
            "evidence digests must bind the exact transition layouts")


def test_blank_source_proof_is_required_and_nonauthorizing() -> None:
    plan = load_json(TRANSITION_PLAN)
    evidence = plan["source_region_evidence"]
    require(evidence == {
        "required_before_promotion": True,
        "evidence_status": "NOT-CAPTURED",
        "region": {
            "source_partition": "ot_state",
            "offset": 15728640,
            "size_bytes": 1048576,
        },
        "required_pattern": {
            "name": "ALL-FF",
            "byte_value": 255,
            "byte_count": 1048576,
        },
        "retained_bytes_allowed": 0,
        "results": {
            "unknown": "DENY",
            "size_mismatch": "DENY",
            "non_ff": "DENY",
            "all_ff": "SOURCE-PROOF-SATISFIED-ONLY",
        },
    }, "source proof contract changed")
    require(evaluate_source_region(None, plan) == "DENY",
            "unknown source contents must deny")
    require(evaluate_source_region(b"\xff" * (1048576 - 1), plan) == "DENY",
            "short source proof must deny")
    require(evaluate_source_region(
        b"\xff" * 65536 + b"\x00" + b"\xff" * (1048576 - 65537),
        plan) == "DENY", "one retained non-FF byte must deny")
    require(evaluate_source_region(b"\xff" * 1048576, plan) ==
            "SOURCE-PROOF-SATISFIED-ONLY",
            "only an exact all-FF 1 MiB observation may satisfy source proof")

    admission = plan["promotion_admission"]
    require(admission["current_result"] == "DENY" and
            admission["source_proof_alone_authorizes_transition"] is False and
            admission["partition_table_promotion_authorized"] is False and
            len(admission["remaining_requirements"]) == 5,
            "source proof must never become transition authority")


def test_rollback_boundary_is_phase_specific() -> None:
    policy = load_json(TRANSITION_PLAN)["rollback_policy"]
    before = policy["pre_authorization_commit"]
    after = policy["post_authorization_commit"]
    require(before["othp0_restoration"] == "CONDITIONAL" and
            len(before["conditions_all_required"]) == 4 and
            before["restoration_authorized"] is False,
            "pre-authorization restoration must remain conditional and unauthorized")
    require(any("no authorization-partition" in condition
                for condition in before["conditions_all_required"]) and
            any("1048576-byte" in condition and "ALL-FF" in condition
                for condition in before["conditions_all_required"]),
            "pre-authorization restoration must require no auth commit and blank media")
    require(after == {
        "othp0_restoration": "DENY",
        "reason": (
            "OTHP0 would reinterpret or overwrite committed "
            "authorization-partition bytes"),
        "requires_forward_recovery_contract": True,
        "restoration_authorized": False,
    }, "post-authorization OTHP0 restoration must deny")


def test_plan_has_no_device_execution_or_authority() -> None:
    plan = load_json(TRANSITION_PLAN)
    provisioning = load_json(PROVISIONING_PLAN)
    require(plan["execution_surface"] == {
        "device_port": None,
        "commands": [],
        "device_access_authorized": False,
    }, "transition plan must contain no port or execution command")
    require(all(value is False for value in plan["capabilities"].values()),
            "transition plan must enable no capability")
    require(all(value is False for value in plan["authorities"].values()),
            "transition plan must grant no authority")
    require(provisioning["partition_transition_plan"] == TRANSITION_PLAN.name,
            "provisioning plan must reference the exact transition contract")
    require(plan["read_evidence_plan"] == READ_PLAN.name,
            "transition plan must reference the exact read-evidence plan")
    require(all(value is False for value in provisioning["runtime"].values()) and
            all(value is False for value in
                provisioning["physical_authority"].values()) and
            provisioning["key_roles"]["efuse_provisioning_authorized"] is False and
            provisioning["rollback_floor"]["provisioning_authorized"] is False,
            "provisioning plan must preserve every denied authority")

    serialized = TRANSITION_PLAN.read_text(encoding="utf-8")
    forbidden = (
        r"\bCOM\d+\b",
        r"\b--port\b",
        r"\bwrite_flash\b",
        r"\berase_flash\b",
        r"\bidf\.py\s+(?:flash|erase-flash)\b",
        r"\besptool(?:\.py)?\b",
    )
    for pattern in forbidden:
        require(re.search(pattern, serialized, re.IGNORECASE) is None,
                f"transition plan contains forbidden execution surface: {pattern}")


def test_read_evidence_plan_is_bounded_and_denied() -> None:
    plan = load_json(READ_PLAN)
    require(plan["schema"] == "OTPSTR0/v0" and
            plan["status"] == "DESIGN-ONLY-READ-AUTHORITY-ABSENT" and
            plan["as_of"] == "2026-08-17" and
            plan["target"] == "heltec_v4_bench" and
            plan["transition_plan"] == TRANSITION_PLAN.name and
            plan["eligible_unit"] == "OT-DEV-001" and
            plan["selected_unit"] is None,
            "read plan must remain exact, unit-bounded, and unauthorized")
    require(plan["execution"] == {
        "authorized": False, "authorization_consumed": False,
        "attempt_limit": 1, "attempts_executed": 0,
        "operation_id": None, "evidence_set_id": None,
        "private_port": None, "commands": [], "result": None,
    }, "read execution must remain absent")
    require(plan["tooling"] == {
        "python_module": "esptool", "exact_version": "5.3.1",
        "chip": "esp32s3", "baud": 115200,
        "before": "no-reset", "after": "no-reset", "no_stub": True,
        "connect_attempts": 1, "open_port_attempts": 1,
        "operator_retry_without_new_authorization": False,
    }, "read tooling boundary changed")
    require(plan["reads"] == [
        {
            "name": "installed_partition_table", "offset": 32768,
            "size_bytes": 3072,
            "expected_sha256":
                "84569AA2BADF3F7294042129B19D0B480784A93A550ADA3253B57BC92A0671AB",
        },
        {
            "name": "source_transition_region", "offset": 15728640,
            "size_bytes": 1048576, "required_byte": 255,
            "expected_sha256":
                "F5FB04AA5B882706B9309E885F19477261336EF76A150C3B4D3489DFAC3953EC",
        },
    ], "read plan must bind exactly two accepted public regions")
    require(plan["evidence_policy"] == {
        "same_nonzero_operation_and_evidence_set_required": True,
        "success_result": "SOURCE-PROOF-SATISFIED-ONLY",
        "source_proof_authorizes_transition": False,
        "raw_bytes_retained": False, "raw_paths_retained": False,
        "device_port_retained": False,
        "device_identifiers_retained": False,
        "nonblank_digest_retained": False,
        "nonblank_byte_location_retained": False,
        "temporary_cleanup_required_before_result": True,
        "failure_output": "DENY",
    }, "read evidence privacy policy changed")
    require(plan["capabilities"]["offline_validation_available"] is True and
            all(value is False for key, value in plan["capabilities"].items()
                if key != "offline_validation_available"),
            "only offline validation may be available")
    require(all(value is False for value in plan["authorities"].values()),
            "read plan must grant no authority")
    require(not READ_TOOL.exists(),
            "no reusable physical executor may ship with a denied plan")


def main() -> int:
    tests = (
        test_exact_layout_transition,
        test_evidence_identity_and_digest_binding,
        test_blank_source_proof_is_required_and_nonauthorizing,
        test_rollback_boundary_is_phase_specific,
        test_plan_has_no_device_execution_or_authority,
        test_read_evidence_plan_is_bounded_and_denied,
    )
    for test in tests:
        test()
        print(f"PASS: {test.__name__}")
    print(f"{len(tests)} Heltec protected-storage transition groups passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
