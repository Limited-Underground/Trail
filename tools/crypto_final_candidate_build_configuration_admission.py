#!/usr/bin/env python3
"""Validate the append-only OT-107 candidate-configuration admission delta."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import unicodedata
from pathlib import Path
from typing import Any

import crypto_final_candidate_build_configuration_contract as evidence_contract


ROOT = Path(__file__).resolve().parents[1]
PROPOSAL = ROOT / "tests/benchmarks/crypto/OT-107-OT005-FINAL-CANDIDATE-BUILD-CONFIGURATION-PROPOSAL-V0.json"
EVIDENCE = ROOT / "tests/benchmarks/crypto/OT-107-OT005-FINAL-CANDIDATE-BUILD-CONFIGURATION-EVIDENCE-V0.json"
ADMISSION = ROOT / "tests/benchmarks/crypto/OT-107-OT005-FINAL-CANDIDATE-BUILD-CONFIGURATION-ADMISSION-DELTA-V0.json"
OT094 = ROOT / "tests/benchmarks/crypto/OT-094-OT005-CANDIDATE-READINESS-V0.json"
OT105 = ROOT / "tests/benchmarks/crypto/OT-105-OT005-MBEDTLS-PSA-SOURCE-LOCK-ADMISSION-DELTA-V0.json"

PROPOSAL_SHA256 = "f9072a602a9c139b1e7728735db04cc270720bc37e0429c22bcdb0cd56202a15"
EVIDENCE_SHA256 = "0c1b8cb574a210c6123b82b565e6ea8e12cee59bacd6ab4b94b293ddf9d2dfbc"
ADMISSION_SHA256 = "3d71dfb02b6fd25e0881ac63cc085174c2cdd7e5ac2cd1e12c320ec34928f5a2"
OT094_SHA256 = "bb607158cbe8ac95a470f0a6c87fbb6d8d986259cf86540b14245fc1167dc7ae"
OT105_SHA256 = "26b6acdc9928eb9510a0baed53c609a4f9a23288155636c6462747745f28ac85"

RESULT = (
    "FINAL-CANDIDATE-BUILD-CONFIGURATION-ADMITTED-HOST-ONLY; "
    "PER-CANDIDATE-SDKCONFIG-DIGESTS-ACCEPTED; TWO-OTCBR0-REQUIREMENTS-"
    "REMAIN; NO-CANDIDATE-IMPORT-BUILD-BENCHMARK-DEVICE-RADIO-OR-SELECTION; "
    "OTCBR0-READINESS-BLOCKED"
)
CANDIDATES = (
    ("espressif_libsodium", "primary"),
    ("esp_idf_mbedtls_psa", "comparison"),
    ("monocypher", "comparison"),
)
HISTORICAL_SIX = (
    "exact_received_target_profile_unresolved",
    "final_candidate_build_configuration_unresolved",
    "espressif_libsodium_source_lock_absent",
    "esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved",
    "monocypher_source_lock_absent",
    "direct_radio_mtu_phy_region_unresolved",
)
PRIOR_THREE = (
    "final_candidate_build_configuration_unresolved",
    "esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved",
    "direct_radio_mtu_phy_region_unresolved",
)
CURRENT_TWO = PRIOR_THREE[1:]
COUNTS = {"source": 3, "api_config": 0, "candidate_import": 0}
HEX64 = re.compile(r"^[0-9a-f]{64}$")
DATE = re.compile(r"^20[0-9]{2}-[0-9]{2}-[0-9]{2}$")
PRIVATE = (
    re.compile(r"[A-Za-z]:\\"),
    re.compile(r"/(?:home|users)/", re.IGNORECASE),
    re.compile(r"\bCOM[0-9]+\b", re.IGNORECASE),
    re.compile(r"\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", re.IGNORECASE),
    re.compile(
        r"\b(?:pin|password|private[_ -]?key|secret|latitude|longitude)\s*[:=]",
        re.IGNORECASE,
    ),
)
MAX_BYTES = 512_000
MAX_DEPTH = 20
MAX_NODES = 20_000
MAX_STRING = 4096


class AdmissionError(ValueError):
    """An OT-107 record is malformed, mutated, private, or over-authoritative."""


def _pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in items:
        if key in value:
            raise AdmissionError("duplicate key")
        value[key] = item
    return value


def _scan(value: Any, depth: int = 0, nodes: list[int] | None = None) -> None:
    nodes = [0] if nodes is None else nodes
    nodes[0] += 1
    if depth > MAX_DEPTH or nodes[0] > MAX_NODES:
        raise AdmissionError("bounds")
    if type(value) is dict:
        for key, item in value.items():
            if (
                type(key) is not str
                or not key
                or len(key) > MAX_STRING
                or unicodedata.normalize("NFC", key) != key
            ):
                raise AdmissionError("key")
            _scan(item, depth + 1, nodes)
    elif type(value) is list:
        for item in value:
            _scan(item, depth + 1, nodes)
    elif type(value) is str:
        if (
            not value
            or len(value) > MAX_STRING
            or unicodedata.normalize("NFC", value) != value
            or any(pattern.search(value) for pattern in PRIVATE)
        ):
            raise AdmissionError("text")
    elif value is not None and type(value) not in (bool, int):
        raise AdmissionError("scalar")


def _json(path: Path, digest: str | None) -> dict[str, Any]:
    try:
        raw = Path(path).read_bytes()
    except OSError as exc:
        raise AdmissionError("record unavailable") from exc
    if not raw or len(raw) > MAX_BYTES:
        raise AdmissionError("record bounds")
    if digest is not None and hashlib.sha256(raw).hexdigest() != digest:
        raise AdmissionError("immutable bytes")
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
    except (UnicodeError, json.JSONDecodeError, RecursionError, ValueError) as exc:
        raise AdmissionError("json") from exc
    _scan(value)
    if type(value) is not dict:
        raise AdmissionError("json root")
    return value


def _keys(value: Any, expected: set[str], label: str) -> dict[str, Any]:
    if type(value) is not dict or set(value) != expected:
        raise AdmissionError(f"{label} fields")
    return value


def _same(value: Any, expected: Any, label: str) -> None:
    if type(value) is not type(expected):
        raise AdmissionError(label)
    if type(expected) is dict:
        if set(value) != set(expected):
            raise AdmissionError(label)
        for key in expected:
            _same(value[key], expected[key], label)
    elif type(expected) is list:
        if len(value) != len(expected):
            raise AdmissionError(label)
        for actual, wanted in zip(value, expected, strict=True):
            _same(actual, wanted, label)
    elif value != expected:
        raise AdmissionError(label)


def _sha(value: Any, label: str) -> str:
    if type(value) is not str or HEX64.fullmatch(value) is None:
        raise AdmissionError(label)
    return value


def _integer(value: Any, label: str, *, minimum: int = 0) -> int:
    if type(value) is not int or value < minimum:
        raise AdmissionError(label)
    return value


def _boolean(value: Any, label: str, expected: bool | None = None) -> bool:
    if type(value) is not bool or (expected is not None and value is not expected):
        raise AdmissionError(label)
    return value


def _all_false(value: Any, expected: set[str], label: str) -> None:
    values = _keys(value, expected, label)
    if any(_boolean(item, f"{label}.{name}") for name, item in values.items()):
        raise AdmissionError(label)


def _validate_proposal(value: dict[str, Any]) -> dict[str, Any]:
    if (
        value.get("schema") != "OTCBCP0"
        or type(value.get("version")) is not int
        or value["version"] != 0
        or value.get("proposal_id")
        != "OT-107-OT005-FINAL-CANDIDATE-BUILD-CONFIGURATION-PROPOSAL-V0"
        or value.get("status") != "configuration_proposed_owner_approval_required"
        or value.get("generated_digest_rule") != "per_candidate_successor_required"
        or value.get("acceptance_counts") != COUNTS
        or value.get("current_three_blockers") != list(PRIOR_THREE)
        or value.get("closed_by_this_proposal") != []
        or value.get("owner_approval")
        != {"approved": False, "approved_proposal_raw_sha256": None}
    ):
        raise AdmissionError("proposal semantics")
    policy = value.get("policy_recommendation", {})
    if policy.get("replace_historical_single_sdkconfig_rule_with_per_candidate_generated_digests") is not True:
        raise AdmissionError("proposal digest policy")
    overlays = value.get("candidate_overlays")
    if type(overlays) is not list or len(overlays) != len(CANDIDATES):
        raise AdmissionError("proposal candidates")
    for raw, (candidate_id, role) in zip(overlays, CANDIDATES, strict=True):
        if (
            type(raw) is not dict
            or raw.get("candidate_id") != candidate_id
            or raw.get("role") != role
            or raw.get("generation_executed") is not False
            or raw.get("generated_sdkconfig_sha256") is not None
        ):
            raise AdmissionError("proposal candidate order")
        _sha(raw.get("overlay_lf_sha256"), "proposal overlay")
    if any(value.get("authority", {}).values()) or any(value.get("claims", {}).values()):
        raise AdmissionError("proposal authority")
    return value


def _validate_parents(ot094: dict[str, Any], ot105: dict[str, Any]) -> None:
    if (
        ot094.get("schema") != "OTCBR0"
        or ot094.get("version") != 0
        or ot094.get("status") != "readiness_blocked"
        or ot094.get("blockers") != list(HISTORICAL_SIX)
    ):
        raise AdmissionError("OT-094 parent")
    if (
        ot105.get("schema") != "OTMPSLA0"
        or ot105.get("version") != 0
        or ot105.get("status")
        != "mbedtls_psa_source_dependency_lock_admitted_host_only_readiness_blocked"
        or ot105.get("acceptance_counts") != COUNTS
        or ot105.get("current_three_blockers") != list(PRIOR_THREE)
        or ot105.get("claims", {}).get("readiness_accepted") is not False
        or any(ot105.get("authority", {}).values())
    ):
        raise AdmissionError("OT-105 parent")


def _validate_evidence(value: dict[str, Any], proposal: dict[str, Any]) -> list[dict[str, Any]]:
    evidence_contract.validate_evidence_contract(value, proposal, AdmissionError)
    if (
        value.get("schema") != "OTCBCGE0"
        or type(value.get("version")) is not int
        or value["version"] != 0
        or value.get("evidence_id")
        != "OT-107-OT005-FINAL-CANDIDATE-BUILD-CONFIGURATION-EVIDENCE-V0"
        or value.get("parents", {}).get("proposal_raw_sha256") != PROPOSAL_SHA256
    ):
        raise AdmissionError("configuration evidence identity")
    candidates = value.get("candidates")
    if type(candidates) is not list or len(candidates) != len(CANDIDATES):
        raise AdmissionError("configuration evidence candidates")
    proposal_by_id = {
        item["candidate_id"]: item for item in proposal["candidate_overlays"]
    }
    accepted: list[dict[str, Any]] = []
    for raw, (candidate_id, role) in zip(candidates, CANDIDATES, strict=True):
        if type(raw) is not dict or raw.get("candidate_id") != candidate_id or raw.get("role") != role:
            raise AdmissionError("configuration evidence candidate order")
        proposed = proposal_by_id[candidate_id]
        if (
            raw.get("overlay_lf_sha256") != proposed["overlay_lf_sha256"]
            or raw.get("kconfig_settings") != proposed["kconfig_settings"]
            or raw.get("source_requirements") != proposed["source_requirements"]
            or raw.get("compile_definitions") != proposed["compile_definitions"]
        ):
            raise AdmissionError("configuration evidence proposal mismatch")
        receipts = raw.get("generation_receipts")
        if type(receipts) is not list or len(receipts) != 2:
            raise AdmissionError("configuration evidence receipts")
        digests: list[tuple[int, str]] = []
        for receipt, run in zip(receipts, ("A", "B"), strict=True):
            if type(receipt) is not dict or receipt.get("run") != run:
                raise AdmissionError("configuration receipt order")
            for field in (
                "isolated_root_initially_absent",
                "configuration_only",
            ):
                _boolean(receipt.get(field), f"configuration receipt {field}", True)
            for field in (
                "candidate_source_copied",
                "candidate_compiled",
                "benchmark_executed",
                "device_accessed",
                "radio_used",
            ):
                _boolean(receipt.get(field), f"configuration receipt {field}", False)
            _same(receipt.get("exit_code"), 0, "configuration receipt exit")
            digests.append(
                (
                    _integer(
                        receipt.get("generated_sdkconfig_bytes"),
                        "configuration receipt bytes",
                        minimum=1,
                    ),
                    _sha(
                        receipt.get("generated_sdkconfig_sha256"),
                        "configuration receipt digest",
                    ),
                )
            )
        if digests[0] != digests[1]:
            raise AdmissionError("configuration receipt reproducibility")
        accepted.append(
            {
                "candidate_id": candidate_id,
                "role": role,
                "overlay_lf_sha256": proposed["overlay_lf_sha256"],
                "generated_sdkconfig_bytes": digests[0][0],
                "generated_sdkconfig_sha256": digests[0][1],
            }
        )
    if value.get("acceptance_counts") != COUNTS:
        raise AdmissionError("configuration evidence counts")
    claims = value.get("claims", {})
    if claims.get("final_candidate_configuration_evidence_generated") is not True:
        raise AdmissionError("configuration evidence claim")
    if any(
        type(item) is not bool
        or item is not (name == "final_candidate_configuration_evidence_generated")
        for name, item in claims.items()
    ):
        raise AdmissionError("configuration evidence claims")
    if any(value.get("authority", {}).values()):
        raise AdmissionError("configuration evidence authority")
    return accepted


def validate(
    admission_path: Path = ADMISSION,
    *,
    evidence_path: Path = EVIDENCE,
    proposal_path: Path = PROPOSAL,
    ot094_path: Path = OT094,
    ot105_path: Path = OT105,
    enforce_digest: bool = True,
) -> dict[str, Any]:
    proposal = _validate_proposal(
        _json(Path(proposal_path), PROPOSAL_SHA256 if enforce_digest else None)
    )
    ot094 = _json(Path(ot094_path), OT094_SHA256 if enforce_digest else None)
    ot105 = _json(Path(ot105_path), OT105_SHA256 if enforce_digest else None)
    _validate_parents(ot094, ot105)
    evidence = _json(
        Path(evidence_path), EVIDENCE_SHA256 if enforce_digest else None
    )
    accepted = _validate_evidence(evidence, proposal)
    admission = _json(
        Path(admission_path), ADMISSION_SHA256 if enforce_digest else None
    )
    expected_keys = {
        "schema",
        "version",
        "artifact_kind",
        "admission_id",
        "accepted_date",
        "status",
        "public_result",
        "parents",
        "owner_approval",
        "configuration_evidence",
        "acceptance_counts",
        "historical_six_blockers",
        "prior_current_three_blockers",
        "closed_by_this_delta",
        "current_two_blockers",
        "accepted_candidate_configurations",
        "authority",
        "claims",
    }
    _keys(admission, expected_keys, "admission")
    identity = (
        admission["schema"],
        admission["version"],
        admission["artifact_kind"],
        admission["admission_id"],
        admission["accepted_date"],
        admission["status"],
        admission["public_result"],
    )
    expected_identity = (
        "OTCBCGA0",
        0,
        "append_only_final_candidate_build_configuration_acceptance_delta",
        "OT-107-OT005-FINAL-CANDIDATE-BUILD-CONFIGURATION-ADMISSION-DELTA-V0",
        "2026-08-21",
        "final_candidate_build_configuration_admitted_readiness_blocked",
        RESULT,
    )
    if identity != expected_identity or type(admission["version"]) is not int:
        raise AdmissionError("admission identity")
    _same(
        admission["parents"],
        {
            "otcbr0_v0_raw_sha256": OT094_SHA256,
            "otmpsla0_v0_raw_sha256": OT105_SHA256,
            "otcbcp0_v0_raw_sha256": PROPOSAL_SHA256,
            "otcbcge0_v0_raw_sha256": EVIDENCE_SHA256,
        },
        "admission parents",
    )
    _same(
        admission["owner_approval"],
        {"approved": True, "approved_proposal_raw_sha256": PROPOSAL_SHA256},
        "owner approval",
    )
    _same(
        admission["configuration_evidence"],
        {
            "path": "tests/benchmarks/crypto/OT-107-OT005-FINAL-CANDIDATE-BUILD-CONFIGURATION-EVIDENCE-V0.json",
            "sha256": EVIDENCE_SHA256,
        },
        "configuration evidence binding",
    )
    _same(admission["acceptance_counts"], COUNTS, "admission counts")
    _same(
        admission["historical_six_blockers"],
        list(HISTORICAL_SIX),
        "historical blockers",
    )
    _same(
        admission["prior_current_three_blockers"],
        list(PRIOR_THREE),
        "prior blockers",
    )
    _same(
        admission["closed_by_this_delta"],
        [
            {
                "blocker_id": PRIOR_THREE[0],
                "closure_evidence_sha256": EVIDENCE_SHA256,
            }
        ],
        "closed blocker",
    )
    _same(admission["current_two_blockers"], list(CURRENT_TWO), "current blockers")
    _same(
        admission["accepted_candidate_configurations"],
        accepted,
        "accepted candidate configurations",
    )
    authority_fields = {
        "final_candidate_configuration_authorized",
        "candidate_import_authorized",
        "benchmark_build_authorized",
        "benchmark_execution_authorized",
        "device_access_authorized",
        "radio_transmit_authorized",
        "key_or_entropy_operation_authorized",
        "suite_selection_authorized",
        "packet_v1_authorized",
        "score_credit_added",
    }
    authority = _keys(admission["authority"], authority_fields, "authority")
    for name, item in authority.items():
        _boolean(item, f"authority.{name}", name == "final_candidate_configuration_authorized")
    claim_fields = {
        "final_candidate_configuration_proven",
        "api_config_eligibility_proven",
        "candidate_import_accepted",
        "benchmark_executed",
        "readiness_accepted",
        "direct_radio_profile_resolved",
        "hardware_or_device_accessed",
        "candidate_selected",
        "suite_selected",
        "packet_v1_wire_selected",
        "score_credit_added",
    }
    claims = _keys(admission["claims"], claim_fields, "claims")
    for name, item in claims.items():
        _boolean(item, f"claims.{name}", name == "final_candidate_configuration_proven")
    return {
        "schema": "OTCBCGA0",
        "version": 0,
        "public_result": RESULT,
        "accepted_candidate_count": len(accepted),
        "acceptance_counts": COUNTS,
        "current_blocker_count": len(CURRENT_TWO),
        "readiness_accepted": False,
        "execution_authorized": False,
        "score_credit_added": False,
    }


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        del message
        print("OTCBCGA0 invalid command line", file=sys.stderr)
        raise SystemExit(2)


def main(argv: list[str] | None = None) -> int:
    parser = SafeArgumentParser(description=__doc__)
    parser.add_argument("admission", nargs="?", type=Path, default=ADMISSION)
    parser.add_argument("--evidence", type=Path, default=EVIDENCE)
    parser.add_argument("--proposal", type=Path, default=PROPOSAL)
    args = parser.parse_args(argv)
    try:
        result = validate(
            args.admission,
            evidence_path=args.evidence,
            proposal_path=args.proposal,
        )
    except (OSError, AdmissionError, KeyError, TypeError, UnicodeError, RecursionError):
        print("OTCBCGA0 validation failed", file=sys.stderr)
        return 2
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
