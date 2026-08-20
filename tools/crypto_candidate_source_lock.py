#!/usr/bin/env python3
"""Validate the host-only OT-005 candidate source-lock admission boundary."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import unicodedata
from pathlib import Path
from typing import Any


SCHEMA = "OTCSL0"
VERSION = 0
ARTIFACT_KIND = "candidate_source_lock_admission_contract"
ADMISSION_ID = "OT-095-OT005-CANDIDATE-SOURCE-LOCK-ADMISSION-V0"
STATUS = "admission_contract_frozen_host_only"
PUBLIC_RESULT = (
    "SOURCE-LOCK-ADMISSION-CONTRACT-FROZEN-HOST-ONLY; "
    "ZERO-SOURCES-ACQUIRED-OR-IMPORTED; OTCBR0-READINESS-BLOCKED"
)
MAX_BYTES = 131_072
MAX_DEPTH = 16
MAX_NODES = 4_096
MAX_STRING = 512
MAX_INTEGER = (1 << 63) - 1
HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
DATE = re.compile(r"^20[0-9]{2}-[0-9]{2}-[0-9]{2}$")
EVIDENCE_ID = re.compile(r"^OT-[0-9]{3}-OT005-[A-Z0-9-]+-SOURCE-EVIDENCE-V0$")
API_EVIDENCE_ID = re.compile(r"^OT-[0-9]{3}-OT005-[A-Z0-9-]+-API-CONFIG-EVIDENCE-V0$")
IMPORT_EVIDENCE_ID = re.compile(r"^OT-[0-9]{3}-OT005-[A-Z0-9-]+-IMPORT-EVIDENCE-V0$")
LOGICAL_PATH = re.compile(r"^[A-Za-z0-9._/-]+$")
WINDOWS_RESERVED_BASENAMES = frozenset(
    ("CON", "PRN", "AUX", "NUL")
    + tuple(f"COM{index}" for index in range(1, 10))
    + tuple(f"LPT{index}" for index in range(1, 10))
)
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
EXPECTED_READINESS_SHA256 = (
    "705b30693196e2f46d8bda7c17acb1e04d7b9092c4a3817286c14d189001b9d3"
)
EXPECTED_CONTRACT_SHA256 = (
    "c0bd923782d0977f8b375cbd2fe8cde5ff132a26b8b6a7ea34a62111bd101f1f"
)

CANDIDATES = (
    {
        "candidate_id": "espressif_libsodium",
        "role": "primary",
        "required_version": "1.0.22",
        "license_spdx": "ISC",
        "source_kind": "external_managed_component",
        "permitted_lock_kinds": ("esp_idf_managed_component_lock",),
        "acquisition_receipt_required": True,
        "parent_idf_binding_required": False,
        "observed_source_commit": None,
        "observed_parent_source_commit": None,
    },
    {
        "candidate_id": "esp_idf_mbedtls_psa",
        "role": "comparison",
        "required_version": "4.1.0",
        "license_spdx": "Apache-2.0",
        "source_kind": "esp_idf_pinned_gitlink",
        "permitted_lock_kinds": ("esp_idf_gitlink_dependency_lock",),
        "acquisition_receipt_required": False,
        "parent_idf_binding_required": True,
        "observed_source_commit": "6cc42afad309e861f4c07e6f106e2ab14a9cb8e5",
        "observed_parent_source_commit": "7101770dc6db2667b3c477cc31365dd1acd6db4e",
    },
    {
        "candidate_id": "monocypher",
        "role": "comparison",
        "required_version": "4.0.3",
        "license_spdx": "CC0-1.0 OR BSD-2-Clause",
        "source_kind": "external_vendored_source",
        "permitted_lock_kinds": (
            "git_submodule_dependency_lock",
            "vendored_source_tree_lock",
        ),
        "acquisition_receipt_required": True,
        "parent_idf_binding_required": False,
        "observed_source_commit": None,
        "observed_parent_source_commit": None,
    },
)
CANDIDATE_BY_ID = {item["candidate_id"]: item for item in CANDIDATES}
EVIDENCE_LAYERS = (
    "acquisition_receipt",
    "immutable_source_tree",
    "project_dependency_lock",
    "api_config_eligibility",
    "candidate_import",
    "benchmark_execution",
)
EVIDENCE_LAYER_DEFINED = (True, True, True, True, True, False)
REQUIRED_API_OPERATIONS = (
    "ed25519_sign",
    "ed25519_verify",
    "x25519",
    "sha256",
    "hkdf_sha256",
    "chacha20poly1305_encrypt",
    "chacha20poly1305_decrypt",
    "noise_xk_handshake",
)
AUTHORITY_FIELDS = (
    "dependency_acquisition_authorized",
    "candidate_import_authorized",
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
    "source_acquired",
    "source_lock_accepted",
    "candidate_imported",
    "api_config_eligibility_proven",
    "candidate_benchmark_executed",
    "candidate_selected",
    "suite_selected",
    "packet_v1_wire_selected",
    "hardware_or_device_accessed",
    "physical_evidence_added",
    "score_credit_added",
)

# Trust is candidate-specific. A future reviewed change must add an exact digest
# to both this code and a new accepted contract revision. A self-authored source
# receipt, tree hash, or project lock is never its own acceptance anchor.
ACCEPTED_SOURCE_EVIDENCE_SHA256: dict[str, frozenset[str]] = {
    candidate["candidate_id"]: frozenset() for candidate in CANDIDATES
}
ACCEPTED_API_CONFIG_EVIDENCE_SHA256: dict[str, frozenset[str]] = {
    candidate["candidate_id"]: frozenset() for candidate in CANDIDATES
}
ACCEPTED_CANDIDATE_IMPORT_EVIDENCE_SHA256: dict[str, frozenset[str]] = {
    candidate["candidate_id"]: frozenset() for candidate in CANDIDATES
}


class ValidationError(ValueError):
    """The contract/evidence is malformed, private, or exceeds its authority."""


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        del message
        print("ERROR: invalid command line", file=sys.stderr)
        raise SystemExit(2)


def _pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in items:
        if key in result:
            raise ValidationError("JSON contains a duplicate key")
        result[key] = value
    return result


def load(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as source:
            raw = source.read(MAX_BYTES + 1)
        if len(raw) > MAX_BYTES:
            raise ValidationError("JSON exceeds the size limit")
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs)
    except ValidationError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError, RecursionError, ValueError) as exc:
        raise ValidationError("JSON is unreadable or invalid") from exc
    return _object(value, "document")


def canonical_sha256(value: dict[str, Any]) -> str:
    try:
        encoded = json.dumps(
            value,
            ensure_ascii=False,
            allow_nan=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
    except (TypeError, ValueError, RecursionError, OverflowError) as exc:
        raise ValidationError("document cannot be serialized canonically") from exc
    return hashlib.sha256(encoded).hexdigest()


def admission_policy_sha256(contract: dict[str, Any]) -> str:
    """Hash policy while excluding mutable future evidence trust anchors."""
    normalized = dict(_object(contract, "contract"))
    normalized["accepted_source_evidence_sha256"] = {
        candidate_id: [] for candidate_id in CANDIDATE_BY_ID
    }
    normalized["accepted_api_config_evidence_sha256"] = {
        candidate_id: [] for candidate_id in CANDIDATE_BY_ID
    }
    normalized["accepted_candidate_import_evidence_sha256"] = {
        candidate_id: [] for candidate_id in CANDIDATE_BY_ID
    }
    return canonical_sha256(normalized)


def _object(value: Any, field: str) -> dict[str, Any]:
    if type(value) is not dict:
        raise ValidationError(f"{field} must be an object")
    return value


def _list(value: Any, field: str) -> list[Any]:
    if type(value) is not list:
        raise ValidationError(f"{field} must be an array")
    return value


def _string(value: Any, field: str) -> str:
    if type(value) is not str or not value or len(value) > MAX_STRING:
        raise ValidationError(f"{field} must be a bounded nonempty string")
    return value


def _boolean(value: Any, field: str, expected: bool | None = None) -> bool:
    if type(value) is not bool:
        raise ValidationError(f"{field} must be a Boolean")
    if expected is not None and value is not expected:
        raise ValidationError(f"{field} must be {str(expected).lower()}")
    return value


def _integer(
    value: Any, field: str, *, minimum: int = 0, maximum: int = MAX_INTEGER
) -> int:
    if type(value) is not int or value < minimum or value > maximum:
        raise ValidationError(f"{field} must be an integer in range")
    return value


def _sha(value: Any, field: str) -> str:
    value = _string(value, field)
    if not HEX64.fullmatch(value):
        raise ValidationError(f"{field} must be a lowercase SHA-256")
    return value


def _commit(value: Any, field: str) -> str:
    value = _string(value, field)
    if not HEX40.fullmatch(value):
        raise ValidationError(f"{field} must be a lowercase 40-hex commit")
    return value


def _exact_keys(value: dict[str, Any], expected: set[str], field: str) -> None:
    if set(value) != expected:
        raise ValidationError(f"{field} fields are not exact")


def _safe_logical_path(value: Any, field: str) -> str:
    path = _string(value, field)
    parts = path.split("/")
    if (
        not LOGICAL_PATH.fullmatch(path)
        or path.startswith("/")
        or "\\" in path
        or ":" in path
        or path != unicodedata.normalize("NFC", path)
        or any(unicodedata.category(character).startswith("C") for character in path)
        or any(
            part in ("", ".", "..")
            or part != part.strip()
            or part.endswith((".", " "))
            or part.split(".", 1)[0].upper() in WINDOWS_RESERVED_BASENAMES
            for part in parts
        )
    ):
        raise ValidationError(f"{field} is not a safe relative POSIX path")
    return path


def _scan_structure(value: Any) -> None:
    seen: set[int] = set()
    nodes = 0

    def visit(item: Any, depth: int) -> None:
        nonlocal nodes
        nodes += 1
        if depth > MAX_DEPTH or nodes > MAX_NODES:
            raise ValidationError("document exceeds structural bounds")
        if type(item) in (dict, list):
            identity = id(item)
            if identity in seen:
                raise ValidationError("document contains a cycle")
            seen.add(identity)
            if type(item) is dict:
                for key, child in item.items():
                    if type(key) is not str or not key or len(key) > MAX_STRING:
                        raise ValidationError("document has a noncanonical key")
                    visit(key, depth + 1)
                    visit(child, depth + 1)
            else:
                for child in item:
                    visit(child, depth + 1)
            seen.remove(identity)
            return
        if type(item) is str:
            if len(item) > MAX_STRING:
                raise ValidationError("document string exceeds the length limit")
            if any(pattern.search(item) for pattern in PRIVATE_TEXT):
                raise ValidationError("document contains private machine or device text")
            return
        if type(item) is int:
            if item < -MAX_INTEGER or item > MAX_INTEGER:
                raise ValidationError("document integer exceeds the magnitude limit")
            return
        if item is None or type(item) in (bool, float):
            if type(item) is float:
                raise ValidationError("document contains a noncanonical JSON number")
            return
        raise ValidationError("document contains a noncanonical JSON type")

    visit(value, 0)


def _all_false(value: Any, fields: tuple[str, ...], name: str) -> None:
    value = _object(value, name)
    _exact_keys(value, set(fields), name)
    for field in fields:
        _boolean(value[field], f"{name}.{field}", False)


def _validate_references(contract: dict[str, Any]) -> None:
    plan = _object(contract["otcb0_snapshot"], "otcb0_snapshot")
    _exact_keys(
        plan,
        {"schema", "version", "benchmark_id", "plan_sha256", "status"},
        "otcb0_snapshot",
    )
    if plan != {
        "schema": "OTCB0",
        "version": 0,
        "benchmark_id": "OT-005-CRYPTO-ESP32S3-V0",
        "plan_sha256": EXPECTED_PLAN_SHA256,
        "status": "draft_blocked",
    }:
        raise ValidationError("historical OTCB0 reference mismatch")

    baseline = _object(contract["preselection_baseline"], "preselection_baseline")
    _exact_keys(
        baseline,
        {"schema", "version", "baseline_id", "baseline_sha256", "status"},
        "preselection_baseline",
    )
    if baseline != {
        "schema": "OTCBL0",
        "version": 0,
        "baseline_id": "OT-093-OT005-BUILD-BASELINE-V0",
        "baseline_sha256": EXPECTED_BASELINE_SHA256,
        "status": "build_baseline_frozen",
    }:
        raise ValidationError("OT-093 baseline reference mismatch")

    readiness = _object(contract["candidate_readiness"], "candidate_readiness")
    _exact_keys(
        readiness,
        {
            "schema",
            "version",
            "readiness_id",
            "readiness_sha256",
            "status",
            "blocker_count",
        },
        "candidate_readiness",
    )
    if readiness != {
        "schema": "OTCBR0",
        "version": 0,
        "readiness_id": "OT-094-OT005-CANDIDATE-READINESS-V0",
        "readiness_sha256": EXPECTED_READINESS_SHA256,
        "status": "readiness_blocked",
        "blocker_count": 6,
    }:
        raise ValidationError("OT-094 readiness reference mismatch")


def validate_contract(contract: dict[str, Any]) -> dict[str, Any]:
    _scan_structure(contract)
    contract = _object(contract, "contract")
    _exact_keys(
        contract,
        {
            "schema",
            "version",
            "artifact_kind",
            "admission_id",
            "accepted_date",
            "status",
            "public_result",
            "otcb0_snapshot",
            "preselection_baseline",
            "candidate_readiness",
            "evidence_layers",
            "manifest_policy",
            "candidates",
            "accepted_source_evidence_sha256",
            "accepted_api_config_evidence_sha256",
            "accepted_candidate_import_evidence_sha256",
            "authority",
            "claims",
        },
        "contract",
    )
    if (
        contract["schema"] != SCHEMA
        or _integer(contract["version"], "version", minimum=0, maximum=0) != VERSION
        or contract["artifact_kind"] != ARTIFACT_KIND
        or contract["admission_id"] != ADMISSION_ID
        or contract["accepted_date"] != "2026-08-20"
        or contract["status"] != STATUS
        or contract["public_result"] != PUBLIC_RESULT
    ):
        raise ValidationError("canonical contract identity or result mismatch")
    if not DATE.fullmatch(contract["accepted_date"]):
        raise ValidationError("accepted_date is not canonical")
    _validate_references(contract)

    layers = _list(contract["evidence_layers"], "evidence_layers")
    if len(layers) != len(EVIDENCE_LAYERS):
        raise ValidationError("evidence layers are incomplete")
    for index, (layer_id, admission_defined, raw) in enumerate(
        zip(EVIDENCE_LAYERS, EVIDENCE_LAYER_DEFINED, layers)
    ):
        layer = _object(raw, f"evidence_layers[{index}]")
        _exact_keys(
            layer,
            {"layer_id", "admission_defined", "sufficient_alone"},
            f"evidence_layers[{index}]",
        )
        if layer["layer_id"] != layer_id:
            raise ValidationError("evidence layer identity/order mismatch")
        _boolean(
            layer["admission_defined"],
            f"evidence_layers[{index}].admission_defined",
            admission_defined,
        )
        _boolean(layer["sufficient_alone"], f"evidence_layers[{index}].sufficient_alone", False)

    policy = _object(contract["manifest_policy"], "manifest_policy")
    expected_policy = {
        "path_encoding": "utf8_relative_posix",
        "path_order": "ordinal_bytewise_ascending",
        "case_policy": "unicode-15.1-nfc-casefold-unique",
        "control_characters_forbidden": True,
        "leading_trailing_whitespace_forbidden": True,
        "trailing_dot_or_space_forbidden": True,
        "windows_reserved_basename_forbidden": True,
        "digest_algorithm": "sha256",
        "digest_scope": "exact_public_manifest_bytes",
        "canonical_json_format": "utf8-no-bom-sort-keys-compact-no-nan-v1",
        "jsonl_format": "utf8-no-bom-one-canonical-json-object-per-line-lf-terminal-v1",
        "acquisition_receipt_kind": "sha256-canonical-json-acquisition-receipt-v1",
        "source_tree_manifest_kind": "sha256-utf8-jsonl-posix-tree-v1",
        "license_manifest_kind": "sha256-utf8-jsonl-license-inventory-v1",
        "sbom_manifest_kind": "sha256-canonical-spdx-json-v1",
        "transitive_manifest_kind": "sha256-utf8-jsonl-transitive-dependencies-v1",
        "patch_manifest_kind": "sha256-utf8-jsonl-ordered-patches-v1",
        "project_lock_digest_kind": "sha256-raw-project-lock-bytes-v1",
        "component_glue_manifest_kind": "sha256-utf8-jsonl-posix-tree-v1",
        "api_operation_evidence_kind": "sha256-canonical-json-operation-evidence-v1",
        "build_graph_manifest_kind": "sha256-utf8-jsonl-build-graph-v1",
        "allowed_entry_kinds": ["directory", "regular_file"],
        "forbidden_entry_kinds": [
            "absolute_path",
            "backslash_path",
            "dot_segment",
            "drive_path",
            "fifo",
            "reparse_point",
            "socket",
            "symlink",
        ],
        "full_tree_manifest_required": True,
        "license_manifest_required": True,
        "sbom_manifest_required": True,
        "transitive_manifest_required": True,
        "patch_manifest_required_even_when_empty": True,
    }
    if policy != expected_policy:
        raise ValidationError("manifest path/symlink/case policy mismatch")

    candidates = _list(contract["candidates"], "candidates")
    if len(candidates) != len(CANDIDATES):
        raise ValidationError("candidate set is incomplete")
    for index, (expected, raw) in enumerate(zip(CANDIDATES, candidates)):
        candidate = _object(raw, f"candidates[{index}]")
        _exact_keys(
            candidate,
            {
                "candidate_id",
                "role",
                "required_version",
                "license_spdx",
                "source_kind",
                "permitted_lock_kinds",
                "acquisition_receipt_required",
                "parent_idf_binding_required",
                "observed_source_commit",
                "observed_parent_source_commit",
                "source_lock_state",
                "accepted_source_evidence_sha256",
                "acquired",
                "imported",
                "api_config_eligible",
                "benchmark_eligible",
                "executed",
                "selected",
            },
            f"candidates[{index}]",
        )
        for field in (
            "candidate_id",
            "role",
            "required_version",
            "license_spdx",
            "source_kind",
            "acquisition_receipt_required",
            "parent_idf_binding_required",
            "observed_source_commit",
            "observed_parent_source_commit",
        ):
            if candidate[field] != expected[field]:
                raise ValidationError("candidate identity/version/license/source policy mismatch")
        if candidate["permitted_lock_kinds"] != list(expected["permitted_lock_kinds"]):
            raise ValidationError("candidate lock-kind allowlist mismatch")
        if candidate["source_lock_state"] != "not_accepted" or candidate["accepted_source_evidence_sha256"] is not None:
            raise ValidationError("canonical candidate must have no accepted source lock")
        for field in (
            "acquired",
            "imported",
            "api_config_eligible",
            "benchmark_eligible",
            "executed",
            "selected",
        ):
            _boolean(candidate[field], f"candidates[{index}].{field}", False)

    for field, expected_anchors in (
        ("accepted_source_evidence_sha256", ACCEPTED_SOURCE_EVIDENCE_SHA256),
        ("accepted_api_config_evidence_sha256", ACCEPTED_API_CONFIG_EVIDENCE_SHA256),
        (
            "accepted_candidate_import_evidence_sha256",
            ACCEPTED_CANDIDATE_IMPORT_EVIDENCE_SHA256,
        ),
    ):
        anchors = _object(contract[field], field)
        _exact_keys(anchors, set(CANDIDATE_BY_ID), field)
        for candidate_id in CANDIDATE_BY_ID:
            values = _list(anchors[candidate_id], f"{field}.{candidate_id}")
            if values != sorted(expected_anchors[candidate_id]):
                raise ValidationError(
                    "candidate-specific accepted evidence anchors mismatch"
                )

    _all_false(contract["authority"], AUTHORITY_FIELDS, "authority")
    _all_false(contract["claims"], CLAIM_FIELDS, "claims")
    digest = canonical_sha256(contract)
    if EXPECTED_CONTRACT_SHA256 and digest != EXPECTED_CONTRACT_SHA256:
        raise ValidationError("canonical source-lock admission digest mismatch")
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "admission_id": ADMISSION_ID,
        "status": STATUS,
        "public_result": PUBLIC_RESULT,
        "candidate_count": len(CANDIDATES),
        "accepted_source_lock_count": 0,
        "otcbr0_blocker_count": 6,
        "readiness_advanced": False,
        "execution_authorized": False,
        "score_credit_added": False,
        "admission_sha256": digest,
        "admission_policy_sha256": admission_policy_sha256(contract),
    }


def _manifest(value: Any, field: str, expected_keys: set[str]) -> dict[str, Any]:
    value = _object(value, field)
    _exact_keys(value, expected_keys, field)
    return value


def validate_source_evidence(
    evidence: dict[str, Any], contract: dict[str, Any]
) -> dict[str, Any]:
    contract_result = validate_contract(contract)
    _scan_structure(evidence)
    evidence = _object(evidence, "source_evidence")
    _exact_keys(
        evidence,
        {
            "schema",
            "version",
            "artifact_kind",
            "evidence_id",
            "recorded_date",
            "contract_policy_sha256",
            "candidate_id",
            "role",
            "version_string",
            "license_spdx",
            "source_kind",
            "lock_kind",
            "source_commit",
            "acquisition_receipt",
            "full_tree_manifest",
            "license_manifest",
            "sbom_manifest",
            "transitive_manifest",
            "patch_manifest",
            "project_dependency_lock",
            "parent_idf_binding",
            "authority",
            "claims",
        },
        "source_evidence",
    )
    if evidence["schema"] != "OTCSLE0" or _integer(
        evidence["version"], "source_evidence.version", minimum=0, maximum=0
    ) != 0 or evidence["artifact_kind"] != "candidate_source_evidence":
        raise ValidationError("source-evidence schema/version/kind mismatch")
    if not EVIDENCE_ID.fullmatch(_string(evidence["evidence_id"], "evidence_id")):
        raise ValidationError("source-evidence identifier is not canonical")
    if not DATE.fullmatch(_string(evidence["recorded_date"], "recorded_date")):
        raise ValidationError("source-evidence date is not canonical")
    if _sha(
        evidence["contract_policy_sha256"], "contract_policy_sha256"
    ) != contract_result["admission_policy_sha256"]:
        raise ValidationError("source evidence does not bind the admission policy")

    candidate_id = _string(evidence["candidate_id"], "candidate_id")
    expected = CANDIDATE_BY_ID.get(candidate_id)
    if expected is None:
        raise ValidationError("source evidence candidate is not canonical")
    if (
        evidence["role"] != expected["role"]
        or evidence["version_string"] != expected["required_version"]
        or evidence["license_spdx"] != expected["license_spdx"]
        or evidence["source_kind"] != expected["source_kind"]
    ):
        raise ValidationError("source evidence candidate/version/license mismatch")
    lock_kind = _string(evidence["lock_kind"], "lock_kind")
    if lock_kind not in expected["permitted_lock_kinds"]:
        raise ValidationError("source evidence lock kind is not permitted")
    source_commit = _commit(evidence["source_commit"], "source_commit")
    receipt_value = _manifest(
        evidence["acquisition_receipt"],
        "acquisition_receipt",
        {"required", "receipt_kind", "receipt_sha256"},
    )
    receipt_required = expected["acquisition_receipt_required"]
    _boolean(receipt_value["required"], "acquisition_receipt.required", receipt_required)
    if receipt_required:
        if receipt_value["receipt_kind"] != "sha256-canonical-json-acquisition-receipt-v1":
            raise ValidationError("acquisition receipt serialization kind mismatch")
        receipt = _sha(receipt_value["receipt_sha256"], "acquisition_receipt.receipt_sha256")
    else:
        if receipt_value["receipt_kind"] is not None or receipt_value["receipt_sha256"] is not None:
            raise ValidationError("installed comparison cannot claim a new acquisition receipt")
        receipt = None

    full_tree = _manifest(
        evidence["full_tree_manifest"],
        "full_tree_manifest",
        {
            "manifest_kind",
            "artifact_id",
            "manifest_sha256",
            "tree_sha256",
            "entry_count",
            "total_bytes",
            "regular_file_count",
            "directory_count",
            "symlink_count",
            "reparse_point_count",
            "casefold_collision_count",
        },
    )
    if (
        full_tree["manifest_kind"] != "sha256-utf8-jsonl-posix-tree-v1"
        or full_tree["artifact_id"] != "candidate-full-source-tree"
    ):
        raise ValidationError("full-tree manifest kind or artifact identity mismatch")
    for field in ("manifest_sha256", "tree_sha256"):
        _sha(full_tree[field], f"full_tree_manifest.{field}")
    entry_count = _integer(full_tree["entry_count"], "full_tree_manifest.entry_count", minimum=1)
    total_bytes = _integer(full_tree["total_bytes"], "full_tree_manifest.total_bytes", minimum=1)
    del total_bytes
    regular = _integer(full_tree["regular_file_count"], "full_tree_manifest.regular_file_count", minimum=1)
    directories = _integer(full_tree["directory_count"], "full_tree_manifest.directory_count")
    if regular + directories != entry_count:
        raise ValidationError("full-tree entry counts do not reconcile")
    for field in ("symlink_count", "reparse_point_count", "casefold_collision_count"):
        _integer(full_tree[field], f"full_tree_manifest.{field}", minimum=0, maximum=0)

    license_manifest = _manifest(
        evidence["license_manifest"],
        "license_manifest",
        {"manifest_kind", "artifact_id", "manifest_sha256", "file_count", "declared_spdx"},
    )
    if (
        license_manifest["manifest_kind"] != "sha256-utf8-jsonl-license-inventory-v1"
        or license_manifest["artifact_id"] != "candidate-license-inventory"
    ):
        raise ValidationError("license manifest kind or artifact identity mismatch")
    _sha(license_manifest["manifest_sha256"], "license_manifest.manifest_sha256")
    _integer(license_manifest["file_count"], "license_manifest.file_count", minimum=1)
    if license_manifest["declared_spdx"] != expected["license_spdx"]:
        raise ValidationError("license manifest SPDX mismatch")

    sbom = _manifest(
        evidence["sbom_manifest"],
        "sbom_manifest",
        {"manifest_kind", "artifact_id", "manifest_sha256", "component_count"},
    )
    if (
        sbom["manifest_kind"] != "sha256-canonical-spdx-json-v1"
        or sbom["artifact_id"] != "candidate-sbom"
    ):
        raise ValidationError("SBOM manifest kind or artifact identity mismatch")
    _sha(sbom["manifest_sha256"], "sbom_manifest.manifest_sha256")
    _integer(sbom["component_count"], "sbom_manifest.component_count", minimum=1)

    transitive = _manifest(
        evidence["transitive_manifest"],
        "transitive_manifest",
        {"manifest_kind", "artifact_id", "manifest_sha256", "dependency_count"},
    )
    if (
        transitive["manifest_kind"] != "sha256-utf8-jsonl-transitive-dependencies-v1"
        or transitive["artifact_id"] != "candidate-transitive-dependencies"
    ):
        raise ValidationError("transitive manifest kind or artifact identity mismatch")
    _sha(transitive["manifest_sha256"], "transitive_manifest.manifest_sha256")
    _integer(transitive["dependency_count"], "transitive_manifest.dependency_count")

    patches = _manifest(
        evidence["patch_manifest"],
        "patch_manifest",
        {"manifest_kind", "artifact_id", "manifest_sha256", "patch_count", "post_patch_tree_sha256"},
    )
    if (
        patches["manifest_kind"] != "sha256-utf8-jsonl-ordered-patches-v1"
        or patches["artifact_id"] != "candidate-patch-set"
    ):
        raise ValidationError("patch manifest kind or artifact identity mismatch")
    _sha(patches["manifest_sha256"], "patch_manifest.manifest_sha256")
    _integer(patches["patch_count"], "patch_manifest.patch_count")
    if _sha(patches["post_patch_tree_sha256"], "patch_manifest.post_patch_tree_sha256") != full_tree["tree_sha256"]:
        raise ValidationError("patch manifest does not bind the admitted full tree")

    project_lock = _manifest(
        evidence["project_dependency_lock"],
        "project_dependency_lock",
        {"lock_kind", "digest_kind", "lock_sha256", "logical_path"},
    )
    if project_lock["lock_kind"] != lock_kind:
        raise ValidationError("project lock kind mismatch")
    if project_lock["digest_kind"] != "sha256-raw-project-lock-bytes-v1":
        raise ValidationError("project lock digest kind mismatch")
    _sha(project_lock["lock_sha256"], "project_dependency_lock.lock_sha256")
    _safe_logical_path(project_lock["logical_path"], "project_dependency_lock.logical_path")

    parent = _manifest(
        evidence["parent_idf_binding"],
        "parent_idf_binding",
        {
            "required",
            "parent_source_commit",
            "gitlink_path",
            "gitlink_commit",
            "component_glue_manifest_sha256",
            "component_glue_manifest_kind",
        },
    )
    required = expected["parent_idf_binding_required"]
    _boolean(parent["required"], "parent_idf_binding.required", required)
    if required:
        parent_commit = _commit(parent["parent_source_commit"], "parent_idf_binding.parent_source_commit")
        if parent_commit != expected["observed_parent_source_commit"]:
            raise ValidationError("parent ESP-IDF commit mismatch")
        if _safe_logical_path(parent["gitlink_path"], "parent_idf_binding.gitlink_path") != "components/mbedtls/mbedtls":
            raise ValidationError("mbedTLS gitlink path mismatch")
        if _commit(parent["gitlink_commit"], "parent_idf_binding.gitlink_commit") != source_commit:
            raise ValidationError("mbedTLS gitlink and source commit differ")
        _sha(
            parent["component_glue_manifest_sha256"],
            "parent_idf_binding.component_glue_manifest_sha256",
        )
        if parent["component_glue_manifest_kind"] != "sha256-utf8-jsonl-posix-tree-v1":
            raise ValidationError("component-glue manifest kind mismatch")
        if source_commit != expected["observed_source_commit"]:
            raise ValidationError("observed mbedTLS source commit mismatch")
    elif any(parent[field] is not None for field in parent if field != "required"):
        raise ValidationError("external candidate cannot claim a parent-IDF binding")

    layer_hashes = [
        full_tree["manifest_sha256"],
        full_tree["tree_sha256"],
        license_manifest["manifest_sha256"],
        sbom["manifest_sha256"],
        transitive["manifest_sha256"],
        patches["manifest_sha256"],
        project_lock["lock_sha256"],
    ]
    if receipt is not None:
        layer_hashes.append(receipt)
    if parent["component_glue_manifest_sha256"] is not None:
        layer_hashes.append(parent["component_glue_manifest_sha256"])
    if len(layer_hashes) != len(set(layer_hashes)):
        raise ValidationError("source-evidence layer digests must be purpose-distinct")

    _all_false(evidence["authority"], AUTHORITY_FIELDS, "source_evidence.authority")
    _all_false(evidence["claims"], CLAIM_FIELDS, "source_evidence.claims")
    digest = canonical_sha256(evidence)
    if digest not in ACCEPTED_SOURCE_EVIDENCE_SHA256[candidate_id]:
        raise ValidationError("source evidence digest is not independently accepted")
    return {
        "schema": "OTCSLE0",
        "version": 0,
        "candidate_id": candidate_id,
        "role": expected["role"],
        "version_string": expected["required_version"],
        "license_spdx": expected["license_spdx"],
        "source_commit": source_commit,
        "lock_kind": lock_kind,
        "project_dependency_lock_sha256": project_lock["lock_sha256"],
        "parent_source_commit": parent["parent_source_commit"],
        "gitlink_path": parent["gitlink_path"],
        "gitlink_commit": parent["gitlink_commit"],
        "source_evidence_sha256": digest,
        "source_lock_accepted": True,
        "import_authorized": False,
        "execution_authorized": False,
        "score_credit_added": False,
    }


def validate_api_config_evidence(
    evidence: dict[str, Any],
    contract: dict[str, Any],
    accepted_source: dict[str, Any],
) -> dict[str, Any]:
    contract_result = validate_contract(contract)
    _scan_structure(evidence)
    evidence = _object(evidence, "api_config_evidence")
    _exact_keys(
        evidence,
        {
            "schema",
            "version",
            "artifact_kind",
            "evidence_id",
            "recorded_date",
            "contract_policy_sha256",
            "candidate_id",
            "role",
            "version_string",
            "license_spdx",
            "source_evidence_sha256",
            "final_sdkconfig_sha256",
            "required_operations",
            "operation_evidence_kind",
            "operation_evidence_sha256",
            "result",
            "execution_authorized",
            "score_credit_added",
        },
        "api_config_evidence",
    )
    if (
        evidence["schema"] != "OTCAPI0"
        or _integer(evidence["version"], "api_config_evidence.version", minimum=0, maximum=0) != 0
        or evidence["artifact_kind"] != "candidate_api_config_eligibility_evidence"
        or evidence["result"] != "api_config_eligible"
    ):
        raise ValidationError("API/config evidence schema/version/result mismatch")
    if not API_EVIDENCE_ID.fullmatch(_string(evidence["evidence_id"], "api_config_evidence.evidence_id")):
        raise ValidationError("API/config evidence identifier is not canonical")
    if not DATE.fullmatch(_string(evidence["recorded_date"], "api_config_evidence.recorded_date")):
        raise ValidationError("API/config evidence date is not canonical")
    if _sha(
        evidence["contract_policy_sha256"],
        "api_config_evidence.contract_policy_sha256",
    ) != contract_result["admission_policy_sha256"]:
        raise ValidationError("API/config evidence does not bind the admission policy")
    candidate_id = _string(evidence["candidate_id"], "api_config_evidence.candidate_id")
    expected = CANDIDATE_BY_ID.get(candidate_id)
    if expected is None:
        raise ValidationError("API/config evidence candidate is not canonical")
    for field in ("candidate_id", "role", "version_string", "license_spdx"):
        if evidence[field] != accepted_source[field]:
            raise ValidationError("API/config evidence does not bind accepted source facts")
    if _sha(
        evidence["source_evidence_sha256"],
        "api_config_evidence.source_evidence_sha256",
    ) != accepted_source["source_evidence_sha256"]:
        raise ValidationError("API/config evidence does not bind accepted source digest")
    final_sdkconfig = _sha(
        evidence["final_sdkconfig_sha256"],
        "api_config_evidence.final_sdkconfig_sha256",
    )
    operations = _list(evidence["required_operations"], "required_operations")
    if operations != list(REQUIRED_API_OPERATIONS):
        raise ValidationError("API/config operations must preserve the fixed complete set")
    if evidence["operation_evidence_kind"] != "sha256-canonical-json-operation-evidence-v1":
        raise ValidationError("API/config operation evidence kind mismatch")
    operation_evidence = _object(
        evidence["operation_evidence_sha256"], "operation_evidence_sha256"
    )
    _exact_keys(operation_evidence, set(REQUIRED_API_OPERATIONS), "operation_evidence_sha256")
    operation_digests = [
        _sha(operation_evidence[operation], f"operation_evidence_sha256.{operation}")
        for operation in REQUIRED_API_OPERATIONS
    ]
    if len(operation_digests) != len(set(operation_digests)):
        raise ValidationError("operation evidence digests must be purpose-distinct")
    _boolean(evidence["execution_authorized"], "api_config_evidence.execution_authorized", False)
    _boolean(evidence["score_credit_added"], "api_config_evidence.score_credit_added", False)
    digest = canonical_sha256(evidence)
    if digest not in ACCEPTED_API_CONFIG_EVIDENCE_SHA256[candidate_id]:
        raise ValidationError("API/config evidence digest is not independently accepted")
    return {
        "schema": "OTCAPI0",
        "version": 0,
        "candidate_id": candidate_id,
        "source_evidence_sha256": accepted_source["source_evidence_sha256"],
        "final_sdkconfig_sha256": final_sdkconfig,
        "api_config_evidence_sha256": digest,
        "api_config_eligible": True,
        "execution_authorized": False,
        "score_credit_added": False,
    }


def validate_candidate_import_evidence(
    evidence: dict[str, Any],
    contract: dict[str, Any],
    accepted_source: dict[str, Any],
    accepted_api_config: dict[str, Any],
) -> dict[str, Any]:
    contract_result = validate_contract(contract)
    _scan_structure(evidence)
    evidence = _object(evidence, "candidate_import_evidence")
    _exact_keys(
        evidence,
        {
            "schema",
            "version",
            "artifact_kind",
            "evidence_id",
            "recorded_date",
            "contract_policy_sha256",
            "candidate_id",
            "role",
            "version_string",
            "license_spdx",
            "source_evidence_sha256",
            "api_config_evidence_sha256",
            "project_dependency_lock_sha256",
            "build_graph_manifest_sha256",
            "build_graph_manifest_kind",
            "result",
            "benchmark_execution_authorized",
            "score_credit_added",
        },
        "candidate_import_evidence",
    )
    if (
        evidence["schema"] != "OTCIMP0"
        or _integer(evidence["version"], "candidate_import_evidence.version", minimum=0, maximum=0) != 0
        or evidence["artifact_kind"] != "candidate_import_evidence"
        or evidence["result"] != "imported_for_benchmark_only"
    ):
        raise ValidationError("candidate-import evidence schema/version/result mismatch")
    if not IMPORT_EVIDENCE_ID.fullmatch(_string(evidence["evidence_id"], "candidate_import_evidence.evidence_id")):
        raise ValidationError("candidate-import evidence identifier is not canonical")
    if not DATE.fullmatch(_string(evidence["recorded_date"], "candidate_import_evidence.recorded_date")):
        raise ValidationError("candidate-import evidence date is not canonical")
    if _sha(
        evidence["contract_policy_sha256"],
        "candidate_import_evidence.contract_policy_sha256",
    ) != contract_result["admission_policy_sha256"]:
        raise ValidationError("candidate-import evidence does not bind the admission policy")
    candidate_id = _string(evidence["candidate_id"], "candidate_import_evidence.candidate_id")
    if candidate_id not in CANDIDATE_BY_ID:
        raise ValidationError("candidate-import evidence candidate is not canonical")
    for field in ("candidate_id", "role", "version_string", "license_spdx"):
        if evidence[field] != accepted_source[field]:
            raise ValidationError("candidate-import evidence does not bind accepted source facts")
    if _sha(
        evidence["source_evidence_sha256"],
        "candidate_import_evidence.source_evidence_sha256",
    ) != accepted_source["source_evidence_sha256"]:
        raise ValidationError("candidate-import evidence does not bind accepted source digest")
    if _sha(
        evidence["api_config_evidence_sha256"],
        "candidate_import_evidence.api_config_evidence_sha256",
    ) != accepted_api_config["api_config_evidence_sha256"]:
        raise ValidationError("candidate-import evidence does not bind accepted API/config digest")
    if _sha(
        evidence["project_dependency_lock_sha256"],
        "candidate_import_evidence.project_dependency_lock_sha256",
    ) != accepted_source["project_dependency_lock_sha256"]:
        raise ValidationError("candidate-import evidence does not bind the project lock")
    build_graph = _sha(
        evidence["build_graph_manifest_sha256"],
        "candidate_import_evidence.build_graph_manifest_sha256",
    )
    if evidence["build_graph_manifest_kind"] != "sha256-utf8-jsonl-build-graph-v1":
        raise ValidationError("candidate-import build-graph manifest kind mismatch")
    _boolean(
        evidence["benchmark_execution_authorized"],
        "candidate_import_evidence.benchmark_execution_authorized",
        False,
    )
    _boolean(evidence["score_credit_added"], "candidate_import_evidence.score_credit_added", False)
    digest = canonical_sha256(evidence)
    if digest not in ACCEPTED_CANDIDATE_IMPORT_EVIDENCE_SHA256[candidate_id]:
        raise ValidationError("candidate-import evidence digest is not independently accepted")
    return {
        "schema": "OTCIMP0",
        "version": 0,
        "candidate_id": candidate_id,
        "source_evidence_sha256": accepted_source["source_evidence_sha256"],
        "api_config_evidence_sha256": accepted_api_config[
            "api_config_evidence_sha256"
        ],
        "project_dependency_lock_sha256": accepted_source[
            "project_dependency_lock_sha256"
        ],
        "build_graph_manifest_sha256": build_graph,
        "candidate_import_evidence_sha256": digest,
        "imported_for_benchmark": True,
        "benchmark_execution_authorized": False,
        "score_credit_added": False,
    }


def main(argv: list[str] | None = None) -> int:
    parser = SafeArgumentParser(description=__doc__)
    parser.add_argument("--contract", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        result = validate_contract(load(args.contract))
        print(json.dumps(result, sort_keys=True))
        return 0
    except ValidationError:
        print("ERROR: source-lock admission is invalid or unaccepted", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
