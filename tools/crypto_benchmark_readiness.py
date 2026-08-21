#!/usr/bin/env python3
"""Validate the host-only OT-005 candidate-readiness boundary."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

import crypto_benchmark_baseline as baseline_validator
import crypto_candidate_source_lock as source_lock_validator
import crypto_api_config_acceptance_contract as api_config_contract_validator


SCHEMA = "OTCBR0"
VERSION = 0
BLOCKED_STATUS = "readiness_blocked"
READY_STATUS = "ready"
BLOCKED_PUBLIC_RESULT = (
    "CANDIDATE-READINESS-CONTRACT-FROZEN-HOST-ONLY; "
    "OTCB0-EXECUTION-BLOCKED"
)
READY_PUBLIC_RESULT = "CANDIDATE-READINESS-VERIFIED; OTCB0-EXECUTION-AUTHORIZED"
MAX_BYTES = 65_536
MAX_DEPTH = 12
MAX_NODES = 1_024
MAX_STRING = 256
HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
DATE = re.compile(r"^20[0-9]{2}-[0-9]{2}-[0-9]{2}$")
READINESS_ID = re.compile(r"^OT-[0-9]{3}-OT005-CANDIDATE-READINESS-V0$")
PRIVATE_TEXT = (
    re.compile(r"[A-Za-z]:\\"),
    re.compile(r"/(?:home|users)/", re.IGNORECASE),
    re.compile(r"\bCOM[0-9]+\b", re.IGNORECASE),
    re.compile(r"\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", re.IGNORECASE),
    re.compile(
        r"\b(?:pin|password|private[_ -]?key|secret|latitude|longitude)\s*[:=]",
        re.IGNORECASE,
    ),
)

EXPECTED_PLAN_SHA256 = (
    "49792b585286823ffa9b7589704d57e8393b3dbf3d514917ffd7b5970301edb7"
)
EXPECTED_BASELINE_SHA256 = (
    "16ffe83af7e3c1f00b5d123eae30e3ac4a0ea2dea0cb08bcc60b990d3e881733"
)
CANDIDATES = (
    ("espressif_libsodium", "primary"),
    ("esp_idf_mbedtls_psa", "comparison"),
    ("monocypher", "comparison"),
)
BLOCKERS = (
    "exact_received_target_profile_unresolved",
    "final_candidate_build_configuration_unresolved",
    "espressif_libsodium_source_lock_absent",
    "esp_idf_mbedtls_psa_dependency_lock_and_api_config_unresolved",
    "monocypher_source_lock_absent",
    "direct_radio_mtu_phy_region_unresolved",
)
REQUIREMENT_NEEDS = (
    (True, True, False),
    (True, False, False),
    (True, False, True),
    (False, False, False),
    (True, False, True),
    (True, True, False),
)
AUTHORITY_FIELDS = (
    "dependency_acquisition_authorized",
    "candidate_import_authorized",
    "result_template_authorized",
    "benchmark_build_authorized",
    "benchmark_execution_authorized",
    "device_access_authorized",
    "radio_transmit_authorized",
    "key_or_entropy_operation_authorized",
    "suite_selection_authorized",
    "packet_v1_authorized",
    "score_credit_added",
)
CLAIM_FIELDS = (
    "ot005_candidate_imported",
    "candidate_benchmark_executed",
    "candidate_selected",
    "secure_lora_adapter_imported",
    "secure_lora_adapter_executed",
    "suite_selected",
    "handshake_kdf_selected",
    "packet_v1_wire_selected",
    "radio_profile_selected",
    "supported_target",
    "hardware_or_device_accessed",
    "key_or_entropy_operation",
    "physical_evidence_added",
    "score_credit_added",
)

# A future reviewed change may add a canonical, independently audited ready
# artifact digest. An arbitrary structurally resolved document is not a trust
# anchor and cannot admit the legacy OTCB0/v0 execution path.
ACCEPTED_READY_READINESS_SHA256: frozenset[str] = frozenset()


class ValidationError(ValueError):
    """The readiness record is malformed, private, or exceeds its authority."""


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        del message
        print("ERROR: invalid command line", file=sys.stderr)
        raise SystemExit(2)


def _pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in items:
        if key in value:
            raise ValidationError("readiness JSON contains a duplicate key")
        value[key] = item
    return value


def load(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as source:
            raw = source.read(MAX_BYTES + 1)
        if len(raw) > MAX_BYTES:
            raise ValidationError("readiness exceeds the size limit")
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
    except ValidationError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError, RecursionError, ValueError) as exc:
        raise ValidationError("readiness JSON is unreadable or invalid") from exc
    return _object(value, "readiness")


def canonical_sha256(value: Any) -> str:
    try:
        encoded = json.dumps(
            value, ensure_ascii=True, sort_keys=True, separators=(",", ":")
        ).encode("utf-8")
    except (TypeError, ValueError, RecursionError) as exc:
        raise ValidationError("artifact is not canonically serializable") from exc
    return hashlib.sha256(encoded).hexdigest()


def _object(value: Any, path: str) -> dict[str, Any]:
    if type(value) is not dict:
        raise ValidationError(f"{path} must be an exact object")
    return value


def _list(value: Any, path: str) -> list[Any]:
    if type(value) is not list:
        raise ValidationError(f"{path} must be an exact list")
    return value


def _exact_keys(value: dict[str, Any], expected: set[str], path: str) -> None:
    if set(value) != expected:
        raise ValidationError(f"{path} must contain the exact canonical fields")


def _string(value: Any, path: str) -> str:
    if type(value) is not str or not value or len(value) > MAX_STRING:
        raise ValidationError(f"{path} must be a bounded nonempty exact string")
    return value


def _nullable_string(value: Any, path: str) -> str | None:
    if value is None:
        return None
    return _string(value, path)


def _integer(value: Any, path: str, *, minimum: int, maximum: int) -> int:
    if type(value) is not int or value < minimum or value > maximum:
        raise ValidationError(f"{path} must be an exact integer in range")
    return value


def _boolean(value: Any, path: str, expected: bool | None = None) -> bool:
    if type(value) is not bool or (expected is not None and value is not expected):
        requirement = "a Boolean" if expected is None else str(expected).lower()
        raise ValidationError(f"{path} must be {requirement}")
    return value


def _sha(value: Any, path: str) -> str:
    text = _string(value, path)
    if not HEX64.fullmatch(text):
        raise ValidationError(f"{path} must be a lowercase SHA-256")
    return text


def _nullable_sha(value: Any, path: str) -> str | None:
    return None if value is None else _sha(value, path)


def _commit(value: Any, path: str) -> str:
    text = _string(value, path)
    if not HEX40.fullmatch(text):
        raise ValidationError(f"{path} must be a lowercase 40-character commit")
    return text


def _nullable_commit(value: Any, path: str) -> str | None:
    return None if value is None else _commit(value, path)


def _scan_structure(value: Any) -> None:
    seen: set[int] = set()
    nodes = 0

    def visit(child: Any, depth: int) -> None:
        nonlocal nodes
        nodes += 1
        if nodes > MAX_NODES or depth > MAX_DEPTH:
            raise ValidationError("artifact exceeds structural bounds")
        if type(child) is dict:
            identity = id(child)
            if identity in seen or len(child) > 32:
                raise ValidationError("artifact contains a cycle or oversized object")
            seen.add(identity)
            for key, item in child.items():
                if type(key) is not str:
                    raise ValidationError("artifact object keys must be exact strings")
                visit(key, depth + 1)
                visit(item, depth + 1)
            seen.remove(identity)
        elif type(child) is list:
            identity = id(child)
            if identity in seen or len(child) > 32:
                raise ValidationError("artifact contains a cycle or oversized list")
            seen.add(identity)
            for item in child:
                visit(item, depth + 1)
            seen.remove(identity)
        elif type(child) is str:
            if len(child) > MAX_STRING:
                raise ValidationError("artifact contains oversized text")
            for pattern in PRIVATE_TEXT:
                if pattern.search(child):
                    raise ValidationError("artifact contains private machine or device text")
        elif child is not None and type(child) not in (int, bool):
            raise ValidationError("artifact contains a noncanonical JSON type")

    visit(value, 0)


def _validate_baseline_reference(
    reference: dict[str, Any], baseline: dict[str, Any]
) -> None:
    _exact_keys(
        reference,
        {
            "schema",
            "version",
            "baseline_id",
            "baseline_sha256",
            "status",
            "target_id",
            "esp_idf_version",
            "esp_idf_commit",
            "compiler",
            "compiler_version",
            "generated_sdkconfig_sha256",
            "generated_sdkconfig_role",
        },
        "preselection_baseline",
    )
    baseline_result = baseline_validator.validate(baseline)
    if baseline_result["baseline_sha256"] != EXPECTED_BASELINE_SHA256:
        raise ValidationError("preselection baseline canonical digest is not accepted")
    expected = {
        "schema": "OTCBL0",
        "version": 0,
        "baseline_id": baseline["baseline_id"],
        "baseline_sha256": baseline_result["baseline_sha256"],
        "status": baseline["status"],
        "target_id": baseline["target"]["target_id"],
        "esp_idf_version": baseline["toolchain"]["esp_idf_version"],
        "esp_idf_commit": baseline["toolchain"]["esp_idf_commit"],
        "compiler": baseline["toolchain"]["compiler"],
        "compiler_version": baseline["toolchain"]["compiler_version"],
        "generated_sdkconfig_sha256": baseline["inputs"][
            "generated_sdkconfig_sha256"
        ],
        "generated_sdkconfig_role": baseline["inputs"][
            "generated_sdkconfig_role"
        ],
    }
    if reference != expected:
        raise ValidationError("preselection baseline reference does not reconcile")


def _validate_plan_reference(
    reference: dict[str, Any], plan: dict[str, Any], status: str
) -> None:
    _exact_keys(
        reference,
        {
            "schema",
            "version",
            "benchmark_id",
            "plan_sha256",
            "plan_status",
            "execution_authorized",
        },
        "otcb0_snapshot",
    )
    if reference["schema"] != "OTCB0" or _integer(
        reference["version"], "otcb0_snapshot.version", minimum=0, maximum=0
    ) != 0:
        raise ValidationError("OTCB0 snapshot schema/version mismatch")
    if plan.get("schema") != "OTCB0" or plan.get("version") != 0:
        raise ValidationError("referenced plan is not OTCB0/v0")
    if reference["benchmark_id"] != plan.get("benchmark_id"):
        raise ValidationError("OTCB0 benchmark identifier mismatch")
    digest = canonical_sha256(plan)
    if reference["plan_sha256"] != digest:
        raise ValidationError("OTCB0 plan digest mismatch")
    if status == BLOCKED_STATUS:
        if digest != EXPECTED_PLAN_SHA256 or plan.get("status") != "draft_blocked":
            raise ValidationError("blocked readiness must preserve the historical plan")
        if reference["plan_status"] != "draft_blocked":
            raise ValidationError("blocked readiness plan status mismatch")
        _boolean(
            reference["execution_authorized"],
            "otcb0_snapshot.execution_authorized",
            False,
        )
    else:
        if plan.get("status") != "ready" or reference["plan_status"] != "ready":
            raise ValidationError("resolved readiness requires a ready plan")
        _boolean(
            reference["execution_authorized"],
            "otcb0_snapshot.execution_authorized",
            True,
        )


def _reconcile_accepted_source_lock(
    candidate: dict[str, Any],
    plan_candidate: dict[str, Any],
    accepted: dict[str, Any],
) -> None:
    version = candidate["observed_version"] or candidate["declared_version"]
    expected = {
        "candidate_id": candidate["candidate_id"],
        "role": candidate["role"],
        "version_string": version,
        "license_spdx": candidate["license_spdx"],
        "source_commit": candidate["source_commit"],
        "parent_source_commit": candidate["parent_source_commit"],
        "lock_kind": candidate["dependency_lock_kind"],
        "project_dependency_lock_sha256": candidate["dependency_lock_sha256"],
        "source_evidence_sha256": candidate["source_evidence_sha256"],
    }
    if any(accepted.get(field) != value for field, value in expected.items()):
        raise ValidationError(
            "ready candidate does not reconcile with accepted source-lock evidence"
        )
    if (
        plan_candidate.get("candidate_id") != accepted["candidate_id"]
        or plan_candidate.get("role") != accepted["role"]
        or plan_candidate.get("version") != accepted["version_string"]
        or plan_candidate.get("source_commit") != accepted["source_commit"]
        or plan_candidate.get("lock_sha256")
        != accepted["project_dependency_lock_sha256"]
        or plan_candidate.get("license_spdx") != accepted["license_spdx"]
    ):
        raise ValidationError(
            "ready plan does not reconcile with accepted source-lock evidence"
        )


def validate(
    readiness: dict[str, Any],
    plan: dict[str, Any],
    baseline: dict[str, Any],
    source_lock_contract: dict[str, Any] | None = None,
    source_evidence: list[dict[str, Any]] | None = None,
    api_config_evidence: list[dict[str, Any]] | None = None,
    candidate_import_evidence: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    for value in (readiness, plan, baseline):
        _scan_structure(value)
    readiness = _object(readiness, "readiness")
    plan = _object(plan, "plan")
    baseline = _object(baseline, "baseline")
    _exact_keys(
        readiness,
        {
            "schema",
            "version",
            "artifact_kind",
            "readiness_id",
            "accepted_date",
            "status",
            "public_result",
            "otcb0_snapshot",
            "preselection_baseline",
            "target_readiness",
            "candidate_configuration",
            "candidates",
            "radio_readiness",
            "readiness_requirements",
            "blockers",
            "authority",
            "claims",
        },
        "readiness",
    )
    if readiness["schema"] != SCHEMA or _integer(
        readiness["version"], "version", minimum=0, maximum=0
    ) != VERSION or readiness["artifact_kind"] != "candidate_readiness":
        raise ValidationError("readiness schema/version/artifact_kind mismatch")
    readiness_id = _string(readiness["readiness_id"], "readiness_id")
    if not READINESS_ID.fullmatch(readiness_id):
        raise ValidationError("readiness_id is not canonical")
    if not DATE.fullmatch(_string(readiness["accepted_date"], "accepted_date")):
        raise ValidationError("accepted_date must be a public calendar date")
    status = _string(readiness["status"], "status")
    if status not in (BLOCKED_STATUS, READY_STATUS):
        raise ValidationError("status must be readiness_blocked or ready")
    expected_public_result = (
        BLOCKED_PUBLIC_RESULT if status == BLOCKED_STATUS else READY_PUBLIC_RESULT
    )
    if readiness["public_result"] != expected_public_result:
        raise ValidationError("public_result does not match readiness status")

    _validate_plan_reference(
        _object(readiness["otcb0_snapshot"], "otcb0_snapshot"), plan, status
    )
    _validate_baseline_reference(
        _object(readiness["preselection_baseline"], "preselection_baseline"),
        baseline,
    )

    target = _object(readiness["target_readiness"], "target_readiness")
    _exact_keys(
        target,
        {
            "state",
            "target_id",
            "manufacturer",
            "board_model",
            "exact_received_revision",
            "rf_variant",
            "mcu",
            "processor_revision",
            "flash_bytes",
            "psram_bytes",
            "supported",
            "evidence_sha256",
        },
        "target_readiness",
    )
    baseline_target = baseline["target"]
    _integer(
        target["flash_bytes"],
        "target_readiness.flash_bytes",
        minimum=1,
        maximum=64 * 1024 * 1024,
    )
    _integer(
        target["psram_bytes"],
        "target_readiness.psram_bytes",
        minimum=0,
        maximum=32 * 1024 * 1024,
    )
    for field in ("target_id", "mcu", "processor_revision", "flash_bytes", "psram_bytes"):
        if target[field] != baseline_target[field]:
            raise ValidationError("target readiness does not match OT-093 baseline")

    configuration = _object(
        readiness["candidate_configuration"], "candidate_configuration"
    )
    _exact_keys(
        configuration,
        {
            "state",
            "baseline_sdkconfig_sha256",
            "baseline_role",
            "final_common_sdkconfig_sha256",
            "candidate_overlays",
            "evidence_sha256",
        },
        "candidate_configuration",
    )
    baseline_inputs = baseline["inputs"]
    if configuration["baseline_sdkconfig_sha256"] != baseline_inputs[
        "generated_sdkconfig_sha256"
    ] or configuration["baseline_role"] != baseline_inputs["generated_sdkconfig_role"]:
        raise ValidationError("candidate configuration baseline mismatch")
    overlays = _list(configuration["candidate_overlays"], "candidate_overlays")
    if len(overlays) != len(CANDIDATES):
        raise ValidationError("candidate overlays must preserve the exact candidate order")
    for index, ((candidate_id, _), raw) in enumerate(zip(CANDIDATES, overlays)):
        overlay = _object(raw, f"candidate_overlays[{index}]")
        _exact_keys(
            overlay,
            {"candidate_id", "overlay_sha256", "generated_sdkconfig_sha256"},
            f"candidate_overlays[{index}]",
        )
        if overlay["candidate_id"] != candidate_id:
            raise ValidationError("candidate overlay order mismatch")

    candidates = _list(readiness["candidates"], "candidates")
    if len(candidates) != len(CANDIDATES):
        raise ValidationError("readiness candidates must preserve the exact set and order")
    candidate_values: list[dict[str, Any]] = []
    for index, ((candidate_id, role), raw) in enumerate(zip(CANDIDATES, candidates)):
        candidate = _object(raw, f"candidates[{index}]")
        _exact_keys(
            candidate,
            {
                "candidate_id",
                "role",
                "declared_version",
                "observed_version",
                "source_state",
                "source_commit",
                "parent_source_commit",
                "dependency_lock_kind",
                "dependency_lock_sha256",
                "source_evidence_sha256",
                "license_spdx",
                "imported",
                "benchmark_eligible",
                "executed",
                "selected",
            },
            f"candidates[{index}]",
        )
        if candidate["candidate_id"] != candidate_id or candidate["role"] != role:
            raise ValidationError("candidate identity, role, or order mismatch")
        for field in ("declared_version", "observed_version", "dependency_lock_kind"):
            _nullable_string(candidate[field], f"candidates[{index}].{field}")
        for field in ("source_commit", "parent_source_commit"):
            _nullable_commit(candidate[field], f"candidates[{index}].{field}")
        for field in ("dependency_lock_sha256", "source_evidence_sha256"):
            _nullable_sha(candidate[field], f"candidates[{index}].{field}")
        _string(candidate["source_state"], f"candidates[{index}].source_state")
        _string(candidate["license_spdx"], f"candidates[{index}].license_spdx")
        for field in ("imported", "benchmark_eligible", "executed", "selected"):
            _boolean(candidate[field], f"candidates[{index}].{field}")
        candidate_values.append(candidate)

    radio = _object(readiness["radio_readiness"], "radio_readiness")
    _exact_keys(
        radio,
        {
            "state",
            "rf_variant",
            "region_code",
            "frequency_hz",
            "bandwidth_hz",
            "spreading_factor",
            "coding_rate_denominator",
            "tx_power_dbm",
            "preamble_symbols",
            "explicit_header",
            "crc_enabled",
            "low_data_rate_optimization",
            "sync_word",
            "direct_payload_ceiling_bytes",
            "benchmark_mtu_bytes",
            "evidence_sha256",
        },
        "radio_readiness",
    )

    requirements = _list(readiness["readiness_requirements"], "readiness_requirements")
    if len(requirements) != len(BLOCKERS):
        raise ValidationError("readiness requirements must preserve all six gates")
    for index, (blocker_id, needs, raw) in enumerate(
        zip(BLOCKERS, REQUIREMENT_NEEDS, requirements)
    ):
        requirement = _object(raw, f"readiness_requirements[{index}]")
        _exact_keys(
            requirement,
            {
                "blocker_id",
                "state",
                "closure_evidence_sha256",
                "requires_owner_decision",
                "requires_physical_evidence",
                "requires_external_source_acquisition",
            },
            f"readiness_requirements[{index}]",
        )
        if requirement["blocker_id"] != blocker_id:
            raise ValidationError("readiness requirement order mismatch")
        _nullable_sha(
            requirement["closure_evidence_sha256"],
            f"readiness_requirements[{index}].closure_evidence_sha256",
        )
        for field in (
            "requires_owner_decision",
            "requires_physical_evidence",
            "requires_external_source_acquisition",
        ):
            _boolean(requirement[field], f"readiness_requirements[{index}].{field}")
        actual_needs = (
            requirement["requires_owner_decision"],
            requirement["requires_physical_evidence"],
            requirement["requires_external_source_acquisition"],
        )
        if actual_needs != needs:
            raise ValidationError("readiness requirement authority/evidence needs mismatch")

    authority = _object(readiness["authority"], "authority")
    _exact_keys(authority, set(AUTHORITY_FIELDS), "authority")
    claims = _object(readiness["claims"], "claims")
    _exact_keys(claims, set(CLAIM_FIELDS), "claims")
    blockers = _list(readiness["blockers"], "blockers")

    if status == BLOCKED_STATUS:
        if any(
            value is not None
            for value in (
                source_lock_contract,
                source_evidence,
                api_config_evidence,
                candidate_import_evidence,
            )
        ):
            raise ValidationError(
                "blocked readiness cannot consume source-lock admission evidence"
            )
        if readiness_id != "OT-094-OT005-CANDIDATE-READINESS-V0" or readiness["accepted_date"] != "2026-08-20":
            raise ValidationError("canonical blocked readiness identity/date mismatch")
        if target["state"] != "blocked":
            raise ValidationError("blocked target readiness state mismatch")
        for field in ("manufacturer", "board_model", "exact_received_revision", "rf_variant", "evidence_sha256"):
            if target[field] is not None:
                raise ValidationError("blocked target readiness contains unsupported closure evidence")
        _boolean(target["supported"], "target_readiness.supported", False)
        if configuration["state"] != "blocked":
            raise ValidationError("blocked candidate configuration state mismatch")
        for field in ("final_common_sdkconfig_sha256", "evidence_sha256"):
            if configuration[field] is not None:
                raise ValidationError("blocked candidate configuration contains closure evidence")
        for overlay in overlays:
            if overlay["overlay_sha256"] is not None or overlay["generated_sdkconfig_sha256"] is not None:
                raise ValidationError("blocked candidate overlay contains closure evidence")
        expected_candidate_facts = (
            ("1.0.22", None, "not_project_locked", None, None, "ISC"),
            (None, "4.1.0", "installed_comparison_source_observed_not_locked", "6cc42afad309e861f4c07e6f106e2ab14a9cb8e5", "7101770dc6db2667b3c477cc31365dd1acd6db4e", "Apache-2.0"),
            ("4.0.3", None, "not_project_locked", None, None, "CC0-1.0 OR BSD-2-Clause"),
        )
        for candidate, expected in zip(candidate_values, expected_candidate_facts):
            actual = (
                candidate["declared_version"],
                candidate["observed_version"],
                candidate["source_state"],
                candidate["source_commit"],
                candidate["parent_source_commit"],
                candidate["license_spdx"],
            )
            if actual != expected:
                raise ValidationError("blocked candidate observation mismatch")
            for field in ("dependency_lock_kind", "dependency_lock_sha256", "source_evidence_sha256"):
                if candidate[field] is not None:
                    raise ValidationError("blocked candidate contains unsupported lock evidence")
            for field in ("imported", "benchmark_eligible", "executed", "selected"):
                _boolean(candidate[field], f"candidate.{field}", False)
        if radio["state"] != "blocked" or any(
            radio[field] is not None for field in radio if field != "state"
        ):
            raise ValidationError("blocked radio readiness must contain only null evidence")
        for requirement in requirements:
            if requirement["state"] != "blocked" or requirement["closure_evidence_sha256"] is not None:
                raise ValidationError("blocked readiness requirement contains closure evidence")
        if blockers != list(BLOCKERS):
            raise ValidationError("blocked readiness must preserve the exact six blockers")
        for field in AUTHORITY_FIELDS:
            _boolean(authority[field], f"authority.{field}", False)
        for field in CLAIM_FIELDS:
            _boolean(claims[field], f"claims.{field}", False)
        fully_resolved = False
    else:
        if (
            source_lock_contract is None
            or source_evidence is None
            or api_config_evidence is None
            or candidate_import_evidence is None
        ):
            raise ValidationError(
                "accepted source-lock, API/config, and import evidence are required"
            )
        if any(
            type(items) is not list or len(items) != len(CANDIDATES)
            for items in (
                source_evidence,
                api_config_evidence,
                candidate_import_evidence,
            )
        ):
            raise ValidationError(
                "accepted source-lock, API/config, and import evidence are required"
            )
        source_lock_result = source_lock_validator.validate_contract(
            source_lock_contract
        )
        if (
            source_lock_result["accepted_source_lock_count"] != len(CANDIDATES)
            or source_lock_result["readiness_advanced"] is not True
        ):
            raise ValidationError(
                "source-lock contract state does not admit all candidates for readiness"
            )
        accepted_source_locks: list[dict[str, Any]] = []
        for (candidate_id, _), evidence in zip(CANDIDATES, source_evidence):
            accepted = source_lock_validator.validate_source_evidence(
                evidence, source_lock_contract
            )
            if accepted["candidate_id"] != candidate_id:
                raise ValidationError("accepted source-lock evidence order mismatch")
            accepted_source_locks.append(accepted)
        if len(accepted_source_locks) != len(CANDIDATES):
            raise ValidationError("all candidate source locks must be independently accepted")
        accepted_api_configs: list[dict[str, Any]] = []
        for accepted_source, evidence in zip(
            accepted_source_locks, api_config_evidence
        ):
            accepted = source_lock_validator.validate_api_config_evidence(
                evidence, source_lock_contract, accepted_source
            )
            if accepted["candidate_id"] != accepted_source["candidate_id"]:
                raise ValidationError("accepted API/config evidence order mismatch")
            accepted_api_configs.append(accepted)
        accepted_candidate_imports: list[dict[str, Any]] = []
        for accepted_source, accepted_api, evidence in zip(
            accepted_source_locks,
            accepted_api_configs,
            candidate_import_evidence,
        ):
            accepted = source_lock_validator.validate_candidate_import_evidence(
                evidence, source_lock_contract, accepted_source, accepted_api
            )
            if accepted["candidate_id"] != accepted_source["candidate_id"]:
                raise ValidationError("accepted candidate-import evidence order mismatch")
            accepted_candidate_imports.append(accepted)
        if target["state"] != "resolved":
            raise ValidationError("ready target must be resolved")
        for field in ("manufacturer", "board_model", "exact_received_revision", "rf_variant"):
            _string(target[field], f"target_readiness.{field}")
        _boolean(target["supported"], "target_readiness.supported", True)
        _sha(target["evidence_sha256"], "target_readiness.evidence_sha256")
        plan_target = plan.get("target")
        if type(plan_target) is not dict or plan_target != {
            "manufacturer": target["manufacturer"],
            "board_model": target["board_model"],
            "board_revision": target["exact_received_revision"],
            "mcu": target["mcu"],
            "flash_bytes": target["flash_bytes"],
            "psram_bytes": target["psram_bytes"],
        }:
            raise ValidationError("ready target does not reconcile with plan")
        if configuration["state"] != "resolved":
            raise ValidationError("ready candidate configuration must be resolved")
        final_sdkconfig = _sha(
            configuration["final_common_sdkconfig_sha256"],
            "candidate_configuration.final_common_sdkconfig_sha256",
        )
        for accepted_api in accepted_api_configs:
            if accepted_api["final_sdkconfig_sha256"] != final_sdkconfig:
                raise ValidationError(
                    "accepted API/config evidence does not match final sdkconfig"
                )
        _sha(configuration["evidence_sha256"], "candidate_configuration.evidence_sha256")
        if plan.get("toolchain", {}).get("sdkconfig_sha256") != final_sdkconfig:
            raise ValidationError("ready plan sdkconfig does not match readiness")
        plan_toolchain = plan.get("toolchain")
        if type(plan_toolchain) is not dict or {
            field: plan_toolchain.get(field)
            for field in (
                "esp_idf_version",
                "esp_idf_commit",
                "compiler",
                "compiler_version",
            )
        } != {
            field: readiness["preselection_baseline"][field]
            for field in (
                "esp_idf_version",
                "esp_idf_commit",
                "compiler",
                "compiler_version",
            )
        }:
            raise ValidationError("ready toolchain does not match preselection baseline")
        for overlay in overlays:
            _sha(overlay["overlay_sha256"], "candidate_overlay.overlay_sha256")
            if _sha(
                overlay["generated_sdkconfig_sha256"],
                "candidate_overlay.generated_sdkconfig_sha256",
            ) != final_sdkconfig:
                raise ValidationError("ready candidate sdkconfig must match the common plan")
        plan_candidates = plan.get("candidates")
        if type(plan_candidates) is not list or len(plan_candidates) != len(CANDIDATES):
            raise ValidationError("ready plan candidate set is unavailable")
        for (
            candidate,
            plan_candidate,
            accepted_source_lock,
            accepted_api_config,
            accepted_candidate_import,
        ) in zip(
            candidate_values,
            plan_candidates,
            accepted_source_locks,
            accepted_api_configs,
            accepted_candidate_imports,
        ):
            if candidate["source_state"] != "locked":
                raise ValidationError("ready candidate source must be locked")
            version = candidate["observed_version"] or candidate["declared_version"]
            _string(version, "candidate.version")
            source_commit = _commit(candidate["source_commit"], "candidate.source_commit")
            _string(candidate["dependency_lock_kind"], "candidate.dependency_lock_kind")
            lock_sha = _sha(candidate["dependency_lock_sha256"], "candidate.dependency_lock_sha256")
            _sha(candidate["source_evidence_sha256"], "candidate.source_evidence_sha256")
            for field in ("imported", "benchmark_eligible"):
                _boolean(candidate[field], f"candidate.{field}", True)
            for field in ("executed", "selected"):
                _boolean(candidate[field], f"candidate.{field}", False)
            if (
                plan_candidate.get("candidate_id") != candidate["candidate_id"]
                or plan_candidate.get("role") != candidate["role"]
                or plan_candidate.get("version") != version
                or plan_candidate.get("source_commit") != source_commit
                or plan_candidate.get("lock_sha256") != lock_sha
                or plan_candidate.get("license_spdx") != candidate["license_spdx"]
            ):
                raise ValidationError("ready candidate does not reconcile with plan")
            _reconcile_accepted_source_lock(
                candidate, plan_candidate, accepted_source_lock
            )
            if (
                accepted_api_config["candidate_id"] != candidate["candidate_id"]
                or accepted_candidate_import["candidate_id"]
                != candidate["candidate_id"]
                or accepted_candidate_import["source_evidence_sha256"]
                != candidate["source_evidence_sha256"]
                or accepted_candidate_import["project_dependency_lock_sha256"]
                != candidate["dependency_lock_sha256"]
                or accepted_candidate_import["api_config_evidence_sha256"]
                != accepted_api_config["api_config_evidence_sha256"]
                or accepted_candidate_import["imported_for_benchmark"] is not True
                or accepted_api_config["api_config_eligible"] is not True
            ):
                raise ValidationError(
                    "ready candidate does not reconcile with accepted API/config and import evidence"
                )
        if radio["state"] != "resolved":
            raise ValidationError("ready radio must be resolved")
        for field in ("rf_variant", "region_code"):
            _string(radio[field], f"radio_readiness.{field}")
        if radio["rf_variant"] != target["rf_variant"]:
            raise ValidationError("target and radio RF variants differ")
        ranges = {
            "frequency_hz": (137_000_000, 1_020_000_000),
            "bandwidth_hz": (7_800, 500_000),
            "spreading_factor": (5, 12),
            "coding_rate_denominator": (5, 8),
            "tx_power_dbm": (-20, 30),
            "preamble_symbols": (4, 65_535),
            "sync_word": (0, 65_535),
            "direct_payload_ceiling_bytes": (1, 255),
            "benchmark_mtu_bytes": (1, 255),
        }
        for field, (minimum, maximum) in ranges.items():
            _integer(radio[field], f"radio_readiness.{field}", minimum=minimum, maximum=maximum)
        for field in ("explicit_header", "crc_enabled", "low_data_rate_optimization"):
            _boolean(radio[field], f"radio_readiness.{field}")
        if radio["benchmark_mtu_bytes"] > radio["direct_payload_ceiling_bytes"]:
            raise ValidationError("benchmark MTU exceeds direct payload ceiling")
        _sha(radio["evidence_sha256"], "radio_readiness.evidence_sha256")
        plan_radio = plan.get("radio", {})
        for readiness_field, plan_field in (
            ("benchmark_mtu_bytes", "mtu_bytes"),
            ("frequency_hz", "frequency_hz"),
            ("bandwidth_hz", "bandwidth_hz"),
            ("spreading_factor", "spreading_factor"),
            ("coding_rate_denominator", "coding_rate_denominator"),
        ):
            if radio[readiness_field] != plan_radio.get(plan_field):
                raise ValidationError("ready radio does not reconcile with plan")
        for requirement in requirements:
            if requirement["state"] != "closed":
                raise ValidationError("ready requirement remains blocked")
            _sha(
                requirement["closure_evidence_sha256"],
                "readiness_requirement.closure_evidence_sha256",
            )
        if blockers:
            raise ValidationError("ready readiness cannot retain blockers")
        required_true = {
            "dependency_acquisition_authorized",
            "candidate_import_authorized",
            "result_template_authorized",
            "benchmark_build_authorized",
            "benchmark_execution_authorized",
            "device_access_authorized",
            "radio_transmit_authorized",
            "key_or_entropy_operation_authorized",
        }
        for field in AUTHORITY_FIELDS:
            _boolean(authority[field], f"authority.{field}", field in required_true)
        for field in CLAIM_FIELDS:
            expected = field in {
                "ot005_candidate_imported",
                "radio_profile_selected",
                "supported_target",
                "hardware_or_device_accessed",
                "physical_evidence_added",
            }
            _boolean(claims[field], f"claims.{field}", expected)
        fully_resolved = True

    digest = canonical_sha256(readiness)
    if fully_resolved and digest not in ACCEPTED_READY_READINESS_SHA256:
        raise ValidationError(
            "resolved readiness digest is not independently accepted"
        )
    accepted_for_legacy_v0 = fully_resolved
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "readiness_id": readiness_id,
        "status": status,
        "public_result": readiness["public_result"],
        "blocker_count": len(blockers),
        "fully_resolved": fully_resolved,
        "accepted_for_legacy_v0": accepted_for_legacy_v0,
        "execution_authorized": bool(
            accepted_for_legacy_v0
            and authority["benchmark_execution_authorized"] is True
        ),
        "score_credit_added": False,
        "readiness_sha256": digest,
    }


def validate_per_candidate_api_config_boundary(
    contract: dict[str, Any],
    api_config_evidence: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    """Validate the append-only OT-108 successor boundary without changing OTCBR0/v0."""
    contract_result = api_config_contract_validator.validate_contract(contract)
    accepted: list[dict[str, Any]] = []
    if api_config_evidence is not None:
        accepted = [
            api_config_contract_validator.validate_evidence(item, contract)
            for item in api_config_evidence
        ]
        candidate_ids = [item["candidate_id"] for item in accepted]
        if len(candidate_ids) != len(set(candidate_ids)):
            raise ValidationError("per-candidate API/config evidence contains a duplicate candidate")
        if set(candidate_ids) != set(api_config_contract_validator.CANDIDATE_BY_ID):
            raise ValidationError("per-candidate API/config evidence must cover every candidate")

    return {
        "schema": contract_result["schema"],
        "version": contract_result["version"],
        "contract_id": contract_result["contract_id"],
        "status": contract_result["status"],
        "public_result": contract_result["public_result"],
        "source_count": contract_result["source_count"],
        "accepted_api_config_count": len(accepted),
        "candidate_import_count": contract_result["candidate_import_count"],
        "blocker_count": contract_result["blocker_count"],
        "fully_resolved": False,
        "execution_authorized": False,
        "selection_authorized": False,
        "score_credit_added": False,
        "contract_sha256": contract_result["contract_sha256"],
    }

def main(argv: list[str] | None = None) -> int:
    parser = SafeArgumentParser(description=__doc__)
    parser.add_argument("--readiness", required=True, type=Path)
    parser.add_argument("--plan", required=True, type=Path)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--source-lock-contract", type=Path)
    parser.add_argument("--source-evidence", action="append", type=Path)
    parser.add_argument("--api-config-evidence", action="append", type=Path)
    parser.add_argument("--candidate-import-evidence", action="append", type=Path)
    args = parser.parse_args(argv)
    try:
        contract = (
            source_lock_validator.load(args.source_lock_contract)
            if args.source_lock_contract is not None
            else None
        )
        evidence = (
            [source_lock_validator.load(path) for path in args.source_evidence]
            if args.source_evidence is not None
            else None
        )
        api_evidence = (
            [source_lock_validator.load(path) for path in args.api_config_evidence]
            if args.api_config_evidence is not None
            else None
        )
        import_evidence = (
            [source_lock_validator.load(path) for path in args.candidate_import_evidence]
            if args.candidate_import_evidence is not None
            else None
        )
        result = validate(
            load(args.readiness),
            load(args.plan),
            load(args.baseline),
            contract,
            evidence,
            api_evidence,
            import_evidence,
        )
        print(json.dumps(result, sort_keys=True))
        return 0
    except (
        ValidationError,
        baseline_validator.ValidationError,
        source_lock_validator.ValidationError,
    ):
        print("ERROR: candidate readiness is invalid or unaccepted", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
