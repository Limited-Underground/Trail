#!/usr/bin/env python3
"""Host-only OT-150 preparation and future one-attempt authority validator.

The canonical OT-150 bundle preparation supplies the exact candidate image
identity.  This tool has no endpoint, flash, reset, capture, or radio surface.
It can create an authority record only after a later explicit owner grant.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import re
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
RECORDED_DATE = "2026-08-27"
PREPARATION_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-150-OT005-MBEDTLS-PSA-EXECUTABLE-RESOURCE-BUNDLE-PREPARATION-V0.json"
)
AUTHORITY_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-150-OT005-MBEDTLS-PSA-ONE-ATTEMPT-AUTHORITY-V0.json"
)
RESOURCE_RESULT_RELATIVE = (
    "tests/benchmarks/crypto/OT-150-OT005-MATCHED-RESOURCE-RESULT-V1.json"
)
RESOURCE_CONTRACT_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-149-OT005-MATCHED-RESOURCE-ACCOUNTING-SUCCESSOR-V1.json"
)
ADAPTER_RELATIVE = "tools/ot150_mbedtls_psa_hardware_adapter.py"
CONTRACT_TOOL_RELATIVE = "tools/ot150_mbedtls_psa_execution_authority.py"
PROTOCOL_RELATIVE = "tools/ot150_mbedtls_psa_protocol_runner.py"
COORDINATOR_RELATIVE = "tools/ot150_mbedtls_psa_coordinator.py"
BUNDLE_TOOL_RELATIVE = "tools/ot150_mbedtls_psa_bundle.py"
ACCOUNTING_TOOL_RELATIVE = "tools/crypto_matched_resource_accounting.py"

PREPARATION_SCHEMA = "OT150MERBP0"
PREPARATION_ID = (
    "OT-150-OT005-MBEDTLS-PSA-EXECUTABLE-RESOURCE-BUNDLE-PREPARATION-V0"
)
AUTHORITY_SCHEMA = "OT150MPAA0"
AUTHORITY_ID = "OT-150-OT005-MBEDTLS-PSA-ONE-ATTEMPT-AUTHORITY-V0"
PROJECT_VER = "ot150-mbedtls-psa-v0"
APPLICATION_OFFSET = 0x10000
BAUD = 115_200
EXPECTED_FRAME_COUNT = 1_015
START = "OTCBXCTL1 START\n"
READY = "OTCBXCTL1 READY\n"
RESTORE_NAME = "opentrail_heltec_v4_bench.bin"
RESTORE_BYTES = 500_944
RESTORE_SHA256 = "f2a58414f82eed585ba90bf7671b06c8ebfaa82e8b23f6ac2093de95143b4e0e"
MAX_JSON_BYTES = 2_097_152
HASH64 = re.compile(r"^[0-9a-f]{64}$")
SAFE_BIN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}\.bin$")


class ContractError(ValueError):
    """A closed validation failure without private path or backend text."""


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        del message
        raise ContractError("invalid arguments")


def _load_module(name: str, path: Path) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ContractError("contract dependency unavailable")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


bundle_contract = _load_module("_ot150_bundle_contract", ROOT / BUNDLE_TOOL_RELATIVE)
resource_contract = _load_module(
    "_ot150_resource_contract", ROOT / ACCOUNTING_TOOL_RELATIVE
)


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def canonical_bytes(value: Any) -> bytes:
    try:
        return json.dumps(
            value,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=True,
            allow_nan=False,
        ).encode("ascii")
    except (TypeError, ValueError, UnicodeError) as exc:
        raise ContractError("canonical encoding failed") from exc


def canonical_document(value: Any) -> bytes:
    return canonical_bytes(value) + b"\n"


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ContractError("duplicate key")
        result[key] = value
    return result


def _reject_constant(_: str) -> None:
    raise ContractError("invalid JSON constant")


def decode_canonical(raw: bytes, label: str) -> dict[str, Any]:
    if not raw or len(raw) > MAX_JSON_BYTES or raw.startswith(b"\xef\xbb\xbf"):
        raise ContractError(f"{label} unavailable")
    try:
        value = json.loads(
            raw.decode("ascii"),
            object_pairs_hook=_pairs,
            parse_constant=_reject_constant,
        )
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise ContractError(f"{label} unavailable") from exc
    if type(value) is not dict or raw != canonical_document(value):
        raise ContractError(f"{label} is not canonical")
    return value


def _safe_file(path: Path, label: str) -> bytes:
    if not path.is_absolute() or path.is_symlink() or not path.is_file():
        raise ContractError(f"{label} unavailable")
    try:
        return path.read_bytes()
    except OSError as exc:
        raise ContractError(f"{label} unavailable") from exc


def _load_canonical(path: Path, label: str) -> tuple[dict[str, Any], bytes]:
    raw = _safe_file(path, label)
    return decode_canonical(raw, label), raw


def _descriptor(path: Path, relative: str, label: str) -> dict[str, Any]:
    expected = (ROOT / relative).resolve()
    if path.resolve() != expected:
        raise ContractError(f"{label} identity mismatch")
    raw = _safe_file(path, label)
    return {"path": relative, "bytes": len(raw), "raw_sha256": _sha256(raw)}


def _image_descriptor(path: Path, expected: dict[str, Any], label: str) -> None:
    raw = _safe_file(path, label)
    if (
        path.name != expected["name"]
        or len(raw) != expected["bytes"]
        or _sha256(raw) != expected["sha256"]
    ):
        raise ContractError(f"{label} mismatch")


def _exact_keys(value: object, keys: set[str], label: str) -> dict[str, Any]:
    if type(value) is not dict or set(value) != keys:
        raise ContractError(f"{label} shape mismatch")
    return value


def _hash_descriptor(value: object, label: str) -> dict[str, Any]:
    result = _exact_keys(value, {"name", "bytes", "sha256"}, label)
    if (
        type(result["name"]) is not str
        or not result["name"]
        or type(result["bytes"]) is not int
        or result["bytes"] <= 0
        or type(result["sha256"]) is not str
        or HASH64.fullmatch(result["sha256"]) is None
    ):
        raise ContractError(f"{label} invalid")
    return result


def _runtime_expected() -> dict[str, dict[str, Any]]:
    return {
        role: _descriptor((ROOT / relative).resolve(), relative, role)
        for role, relative in (
            ("bundle_validator", BUNDLE_TOOL_RELATIVE),
            ("protocol_transport", PROTOCOL_RELATIVE),
            ("coordinator", COORDINATOR_RELATIVE),
            ("hardware_adapter", ADAPTER_RELATIVE),
            ("execution_authority_tool", CONTRACT_TOOL_RELATIVE),
            ("matched_resource_validator", ACCOUNTING_TOOL_RELATIVE),
        )
    }


def _validate_preparation_shape(value: dict[str, Any]) -> dict[str, Any]:
    _exact_keys(
        value,
        {
            "schema", "version", "artifact_kind", "preparation_id",
            "recorded_date", "status", "bindings", "candidate",
            "build_policy", "builds", "future_public_outputs", "runtime",
            "images", "privacy", "authority", "claims",
        },
        "preparation",
    )
    if (
        value["schema"] != PREPARATION_SCHEMA
        or value["version"] != 0
        or value["artifact_kind"]
        != "mbedtls_psa_executable_resource_bundle_preparation"
        or value["preparation_id"] != PREPARATION_ID
        or value["recorded_date"] != RECORDED_DATE
        or value["status"] != "host_bundle_frozen_fresh_owner_authority_required"
        or value["bindings"] != bundle_contract.fixed_bindings()
    ):
        raise ContractError("preparation identity mismatch")
    if value["candidate"] != {
        "id": "esp_idf_mbedtls_psa",
        "version": "4.1.0",
        "role": "comparison",
        "selection_eligible": False,
        "operations": list(bundle_contract.OPERATIONS),
        "unavailable_operations": list(bundle_contract.UNAVAILABLE_OPERATIONS),
    }:
        raise ContractError("candidate boundary mismatch")
    policy = value["build_policy"]
    if (
        type(policy) is not dict
        or policy.get("project_version") != PROJECT_VER
        or policy.get("run_order") != ["A", "B"]
        or policy.get("a_b_byte_and_hash_equality_required_per_side") is not True
        or policy.get("candidate_control_common_artifacts_equal")
        != list(bundle_contract.COMMON_ARTIFACT_ROLES)
        or policy.get("candidate_control_linkage_artifacts_distinct")
        != list(bundle_contract.DISTINCT_LINKAGE_ROLES)
        or policy.get("json2_format") != "esp_idf_size_json2_v1.2"
        or policy.get("resource_result_admitted_by_this_preparation") is not False
    ):
        raise ContractError("build policy mismatch")
    builds = _exact_keys(value["builds"], {"candidate", "control"}, "builds")
    normalized: dict[str, list[dict[str, Any]]] = {}
    for side in ("candidate", "control"):
        runs = builds[side]
        if type(runs) is not list or len(runs) != 2:
            raise ContractError("build run mismatch")
        checked: list[dict[str, Any]] = []
        for index, run in enumerate(runs):
            item = _exact_keys(run, {"run", "artifacts"}, "build run")
            if item["run"] != ("A" if index == 0 else "B"):
                raise ContractError("build run order mismatch")
            artifacts = _exact_keys(
                item["artifacts"], set(bundle_contract.REQUIRED_ARTIFACT_ROLES),
                "build artifacts",
            )
            checked.append(
                {role: _hash_descriptor(artifacts[role], role)
                 for role in bundle_contract.REQUIRED_ARTIFACT_ROLES}
            )
        if checked[0] != checked[1]:
            raise ContractError("A/B artifact mismatch")
        normalized[side] = checked
    for role in bundle_contract.COMMON_ARTIFACT_ROLES:
        if normalized["candidate"][0][role] != normalized["control"][0][role]:
            raise ContractError("candidate/control common artifact mismatch")
    for role in bundle_contract.DISTINCT_LINKAGE_ROLES:
        if (normalized["candidate"][0][role]["sha256"]
                == normalized["control"][0][role]["sha256"]):
            raise ContractError("candidate/control linkage mismatch")
    runtime = _exact_keys(
        value["runtime"],
        {
            "bundle_validator", "protocol_transport", "coordinator",
            "hardware_adapter", "execution_authority_tool",
            "matched_resource_validator",
        },
        "runtime",
    )
    if runtime != _runtime_expected():
        raise ContractError("runtime binding mismatch")
    outputs = value["future_public_outputs"]
    if (
        type(outputs) is not dict
        or outputs.get("canonical_preparation")
        != {"path": PREPARATION_RELATIVE, "schema": PREPARATION_SCHEMA}
        or outputs.get("matched_resource_result")
        != {"path": RESOURCE_RESULT_RELATIVE, "schema": "OTMRAR1", "admitted": False}
        or outputs.get("json2_reports") != {
            "candidate": {
                "path": bundle_contract.CANDIDATE_REPORT_RELATIVE,
                "format": "esp_idf_size_json2_v1.2",
            },
            "control": {
                "path": bundle_contract.CONTROL_REPORT_RELATIVE,
                "format": "esp_idf_size_json2_v1.2",
            },
        }
    ):
        raise ContractError("future output boundary mismatch")
    images = _exact_keys(value["images"], {"future_benchmark_write", "restore"}, "images")
    benchmark = _exact_keys(
        images["future_benchmark_write"],
        {
            "role", "name", "bytes", "sha256", "application_offset",
            "future_writable_after_separate_authority_only",
            "control_application_writable", "other_artifacts_writable",
        },
        "benchmark image",
    )
    if (
        benchmark["role"] != "candidate_application_bin"
        or type(benchmark["name"]) is not str
        or SAFE_BIN.fullmatch(benchmark["name"]) is None
        or benchmark["application_offset"] != APPLICATION_OFFSET
        or benchmark["future_writable_after_separate_authority_only"] is not True
        or benchmark["control_application_writable"] is not False
        or benchmark["other_artifacts_writable"] is not False
        or benchmark["bytes"] != normalized["candidate"][0]["application_bin"]["bytes"]
        or benchmark["sha256"] != normalized["candidate"][0]["application_bin"]["sha256"]
    ):
        raise ContractError("benchmark image boundary mismatch")
    restore = images["restore"]
    if restore != {
        "name": RESTORE_NAME,
        "bytes": RESTORE_BYTES,
        "sha256": RESTORE_SHA256,
        "exact_readback_and_restore_required_by_future_authority": True,
    }:
        raise ContractError("restore image boundary mismatch")
    if value["privacy"] != {
        "build_artifacts_record_name_only": True,
        "runtime_and_fixed_bindings_repo_relative_only": True,
        "absolute_paths_recorded": False,
        "serial_ports_recorded": False,
        "device_identifiers_recorded": False,
        "raw_capture_recorded": False,
    }:
        raise ContractError("privacy boundary mismatch")
    if value["authority"] != bundle_contract._authority():
        raise ContractError("preparation authority drift")
    if value["claims"] != bundle_contract._claims():
        raise ContractError("preparation claim drift")
    return benchmark


def _resource_binding() -> dict[str, Any]:
    result_path = (ROOT / RESOURCE_RESULT_RELATIVE).resolve()
    raw = _safe_file(result_path, "resource result")
    try:
        result = resource_contract.load_json(result_path)
        contract_path = (ROOT / RESOURCE_CONTRACT_RELATIVE).resolve()
        contract = resource_contract.load_json(contract_path)
        verdict = resource_contract.validate_result(
            contract,
            result,
            root=ROOT,
            contract_path=contract_path,
        )
    except BaseException as exc:
        raise ContractError("resource result unavailable") from exc
    if verdict.get("candidate_id") != "esp_idf_mbedtls_psa" or verdict.get("verdict") != "pass":
        raise ContractError("resource result mismatch")
    return {
        "path": RESOURCE_RESULT_RELATIVE,
        "bytes": len(raw),
        "raw_sha256": _sha256(raw),
        "canonical_sha256": resource_contract.canonical_sha256(result),
    }


def _load_validated_preparation(
    benchmark_path: Path | None,
    restore_path: Path,
    adapter_path: Path,
    *,
    recovery: bool,
) -> tuple[dict[str, Any], bytes, dict[str, Any], dict[str, Any]]:
    if type(recovery) is not bool:
        raise ContractError("recovery mode mismatch")
    preparation, raw = _load_canonical(
        (ROOT / PREPARATION_RELATIVE).resolve(), "preparation"
    )
    benchmark = _validate_preparation_shape(preparation)
    if _descriptor(adapter_path, ADAPTER_RELATIVE, "adapter") != preparation["runtime"]["hardware_adapter"]:
        raise ContractError("adapter binding mismatch")
    restore = preparation["images"]["restore"]
    _image_descriptor(restore_path, restore, "restore image")
    if recovery:
        if benchmark_path is not None:
            raise ContractError("recovery benchmark must be absent")
    else:
        if benchmark_path is None:
            raise ContractError("benchmark unavailable")
        _image_descriptor(benchmark_path, benchmark, "benchmark image")
    return preparation, raw, benchmark, _resource_binding()


def load_preparation_binding(
    benchmark_path: Path | None,
    restore_path: Path,
    adapter_path: Path,
    *,
    recovery: bool,
) -> dict[str, Any]:
    unused_preparation, unused_raw, benchmark, unused_resource = (
        _load_validated_preparation(
            benchmark_path, restore_path, adapter_path, recovery=recovery
        )
    )
    return {
        "benchmark_name": benchmark["name"],
        "benchmark_bytes": benchmark["bytes"],
        "benchmark_sha256": benchmark["sha256"],
        "restore_name": RESTORE_NAME,
        "restore_bytes": RESTORE_BYTES,
        "restore_sha256": RESTORE_SHA256,
        "application_offset": APPLICATION_OFFSET,
        "baud": BAUD,
        "protocol_start": START,
        "protocol_ready": READY,
        "expected_frame_count": EXPECTED_FRAME_COUNT,
    }


def _execution_contract() -> dict[str, Any]:
    return {
        "attempt_count": 1,
        "node_count": 2,
        "application_offset": APPLICATION_OFFSET,
        "application_only_writes": True,
        "control_application_writable": False,
        "both_installed_trail_readbacks_before_journal": True,
        "both_installed_trail_readbacks_before_first_write": True,
        "all_preflight_devices_reset_before_journal": True,
        "all_preflight_devices_reset_on_preflight_failure": True,
        "display_reset_and_visual_preflight_required": True,
        "benchmark_readback_before_capture": True,
        "retry_exact_start_until_exact_ready": True,
        "capture_deadline_seconds": 180,
        "capture_deadline_starts_at_ready": True,
        "expected_frame_count": EXPECTED_FRAME_COUNT,
        "restore_each_touched_node": True,
        "restore_readback_and_hard_reset_required": True,
        "recovery_only_mode_required": True,
        "recovery_requires_benchmark_artifact": False,
        "recovery_retry_until_restored_required": True,
        "distinct_endpoint_values_required": True,
        "single_bound_flash_capture_backend_required": True,
        "radio_allowed": False,
        "selection_allowed": False,
    }


def build_authority(
    preparation: dict[str, Any],
    preparation_raw: bytes,
    benchmark_path: Path,
    restore_path: Path,
    adapter_path: Path,
    *,
    owner_authorization_granted: bool,
) -> dict[str, Any]:
    if owner_authorization_granted is not True:
        raise ContractError("owner authorization absent")
    actual_preparation, actual_raw, benchmark, resource = _load_validated_preparation(
        benchmark_path, restore_path, adapter_path, recovery=False
    )
    if preparation != actual_preparation or preparation_raw != actual_raw:
        raise ContractError("preparation boundary mismatch")
    return {
        "schema": AUTHORITY_SCHEMA,
        "version": 0,
        "artifact_kind": "mbedtls_psa_one_attempt_execution_authority",
        "authority_id": AUTHORITY_ID,
        "recorded_date": RECORDED_DATE,
        "status": "authorized_one_attempt_not_executed",
        "preparation": {
            "path": PREPARATION_RELATIVE,
            "bytes": len(actual_raw),
            "raw_sha256": _sha256(actual_raw),
            "canonical_sha256": _sha256(canonical_bytes(actual_preparation)),
        },
        "resource_result": resource,
        "candidate": actual_preparation["candidate"],
        "images": {
            "benchmark": {
                "name": benchmark["name"],
                "bytes": benchmark["bytes"],
                "sha256": benchmark["sha256"],
            },
            "restore": {
                "name": RESTORE_NAME,
                "bytes": RESTORE_BYTES,
                "sha256": RESTORE_SHA256,
            },
        },
        "execution": _execution_contract(),
        "owner_authorization": {
            "granted": True,
            "scope": "one_two_node_mbedtls_psa_application_only_attempt",
            "temporary_application_flash_and_exact_trail_restore": True,
            "visual_display_reset_confirmation_required": True,
            "permanent_firmware_decision": False,
        },
        "consumption": {
            "consumed_on_success_or_abort": True,
            "continuing_authority": False,
            "reusable": False,
        },
        "claims": {
            "benchmark_executed": False,
            "candidate_selected": False,
            "suite_selected": False,
            "phase_two_complete": False,
            "radio_used": False,
            "supported_target_proven": False,
            "regulatory_acceptance_proven": False,
            "score_credit_added": False,
        },
    }


def validate_authority(
    value: dict[str, Any],
    preparation: dict[str, Any],
    preparation_raw: bytes,
    benchmark_path: Path,
    restore_path: Path,
    adapter_path: Path,
) -> dict[str, Any]:
    expected = build_authority(
        preparation,
        preparation_raw,
        benchmark_path,
        restore_path,
        adapter_path,
        owner_authorization_granted=True,
    )
    if value != expected:
        raise ContractError("authority boundary mismatch")
    return {
        "schema": AUTHORITY_SCHEMA,
        "authority_id": AUTHORITY_ID,
        "authority_canonical_sha256": _sha256(canonical_bytes(value)),
        "attempt_count": 1,
        "reusable": False,
        "radio_allowed": False,
    }


def validate_execution_authority(
    authority_path: Path,
    benchmark_path: Path | None,
    restore_path: Path,
    adapter_path: Path,
    *,
    recovery: bool,
) -> str:
    preparation, preparation_raw, unused_benchmark, unused_resource = (
        _load_validated_preparation(
            benchmark_path, restore_path, adapter_path, recovery=recovery
        )
    )
    authority, authority_raw = _load_canonical(authority_path, "authority")
    if recovery:
        benchmark = preparation["images"]["future_benchmark_write"]
        expected = build_authority_from_validated_preparation(
            preparation, preparation_raw, benchmark, unused_resource
        )
        if authority != expected:
            raise ContractError("authority boundary mismatch")
    else:
        if benchmark_path is None:
            raise ContractError("benchmark unavailable")
        validate_authority(
            authority,
            preparation,
            preparation_raw,
            benchmark_path,
            restore_path,
            adapter_path,
        )
    return _sha256(authority_raw)


def build_authority_from_validated_preparation(
    preparation: dict[str, Any],
    preparation_raw: bytes,
    benchmark: dict[str, Any],
    resource: dict[str, Any],
) -> dict[str, Any]:
    """Reconstruct the authority during recovery without reading the candidate."""
    return {
        "schema": AUTHORITY_SCHEMA,
        "version": 0,
        "artifact_kind": "mbedtls_psa_one_attempt_execution_authority",
        "authority_id": AUTHORITY_ID,
        "recorded_date": RECORDED_DATE,
        "status": "authorized_one_attempt_not_executed",
        "preparation": {
            "path": PREPARATION_RELATIVE,
            "bytes": len(preparation_raw),
            "raw_sha256": _sha256(preparation_raw),
            "canonical_sha256": _sha256(canonical_bytes(preparation)),
        },
        "resource_result": resource,
        "candidate": preparation["candidate"],
        "images": {
            "benchmark": {
                "name": benchmark["name"],
                "bytes": benchmark["bytes"],
                "sha256": benchmark["sha256"],
            },
            "restore": {
                "name": RESTORE_NAME,
                "bytes": RESTORE_BYTES,
                "sha256": RESTORE_SHA256,
            },
        },
        "execution": _execution_contract(),
        "owner_authorization": {
            "granted": True,
            "scope": "one_two_node_mbedtls_psa_application_only_attempt",
            "temporary_application_flash_and_exact_trail_restore": True,
            "visual_display_reset_confirmation_required": True,
            "permanent_firmware_decision": False,
        },
        "consumption": {
            "consumed_on_success_or_abort": True,
            "continuing_authority": False,
            "reusable": False,
        },
        "claims": {
            "benchmark_executed": False,
            "candidate_selected": False,
            "suite_selected": False,
            "phase_two_complete": False,
            "radio_used": False,
            "supported_target_proven": False,
            "regulatory_acceptance_proven": False,
            "score_credit_added": False,
        },
    }


def write_new(path: Path, value: dict[str, Any], relative: str) -> None:
    if path.resolve() != (ROOT / relative).resolve():
        raise ContractError("output identity mismatch")
    payload = canonical_document(value)
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
    except FileExistsError as exc:
        raise ContractError("output already exists") from exc
    except OSError as exc:
        raise ContractError("output unavailable") from exc


def _parser() -> SafeArgumentParser:
    parser = SafeArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("authorize", "validate-authority"):
        command = commands.add_parser(name)
        command.add_argument("--benchmark-app", type=Path, required=True)
        command.add_argument("--restore-app", type=Path, required=True)
        command.add_argument("--adapter", type=Path, required=True)
    commands.choices["authorize"].add_argument(
        "--owner-authorization-granted", action="store_true"
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    try:
        args = _parser().parse_args(argv)
        preparation, preparation_raw = _load_canonical(
            (ROOT / PREPARATION_RELATIVE).resolve(), "preparation"
        )
        if args.command == "authorize":
            value = build_authority(
                preparation,
                preparation_raw,
                args.benchmark_app.resolve(),
                args.restore_app.resolve(),
                args.adapter.resolve(),
                owner_authorization_granted=args.owner_authorization_granted,
            )
            write_new((ROOT / AUTHORITY_RELATIVE).resolve(), value, AUTHORITY_RELATIVE)
            result = validate_authority(
                value,
                preparation,
                preparation_raw,
                args.benchmark_app.resolve(),
                args.restore_app.resolve(),
                args.adapter.resolve(),
            )
        else:
            authority, unused_raw = _load_canonical(
                (ROOT / AUTHORITY_RELATIVE).resolve(), "authority"
            )
            result = validate_authority(
                authority,
                preparation,
                preparation_raw,
                args.benchmark_app.resolve(),
                args.restore_app.resolve(),
                args.adapter.resolve(),
            )
        print(json.dumps(result, sort_keys=True, separators=(",", ":")))
        return 0
    except BaseException:
        print("ERROR: OT-150 execution authority validation failed", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
