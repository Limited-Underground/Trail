#!/usr/bin/env python3
"""Validate fail-closed OpenTrail target-crypto benchmark evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


SCHEMA = "OTCB0"
VERSION = 0
PLAN_STATUSES = {"draft_blocked", "ready"}
MAX_BYTES = 65_536
MAX_DEPTH = 12
MAX_NODES = 2_048
MAX_STRING = 500
CANDIDATES = {
    "espressif_libsodium": "primary",
    "esp_idf_mbedtls_psa": "comparison",
    "monocypher": "comparison",
}
BLOCKERS = (
    "exact_client_board_and_revision_not_frozen",
    "esp_idf_toolchain_and_sdkconfig_not_pinned",
    "candidate_source_commits_and_dependency_locks_not_pinned",
    "direct_radio_mtu_and_phy_profile_not_frozen",
)
GATES = (
    "primitive_vectors_and_negative_cases",
    "noise_xk_independent_interoperability",
    "invitation_replay_reorder_timeout_refusal",
    "entropy_and_cold_start_uniqueness",
    "temporary_secret_wipe_and_log_redaction",
    "rollback_safe_counter_interruption",
    "two_device_join_revoke_reset_recovery",
    "license_sbom_and_reproducible_lock",
)
OPERATIONS = (
    "ed25519_sign",
    "ed25519_verify",
    "x25519",
    "sha256",
    "hkdf_sha256",
    "chacha20poly1305_encrypt",
    "chacha20poly1305_decrypt",
    "noise_xk_handshake",
)
HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
UTC = re.compile(r"^20[0-9]{2}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$")
ID = re.compile(r"^OT-005-CRYPTO-[A-Z0-9-]{1,48}$")
PRIVATE_TEXT = (
    re.compile(r"[A-Za-z]:\\"),
    re.compile(r"/(?:home|users)/", re.IGNORECASE),
    re.compile(r"\bCOM[0-9]+\b", re.IGNORECASE),
    re.compile(r"\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", re.IGNORECASE),
    re.compile(r"\b(?:pin|password|private[_ -]?key|secret)\s*[:=]", re.IGNORECASE),
)


class ValidationError(ValueError):
    pass


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        del message
        print("ERROR: invalid command line", file=sys.stderr)
        raise SystemExit(2)


def _pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in items:
        if key in value:
            raise ValidationError("JSON contains a duplicate key")
        value[key] = item
    return value


def _object(value: Any, path: str) -> dict[str, Any]:
    if type(value) is not dict:
        raise ValidationError(f"{path} must be an exact object")
    return value


def _list(value: Any, path: str) -> list[Any]:
    if type(value) is not list:
        raise ValidationError(f"{path} must be an exact list")
    return value


def _exact_keys(value: dict[str, Any], expected: set[str], path: str) -> None:
    missing = sorted(expected - set(value))
    extra = sorted(set(value) - expected)
    if missing or extra:
        raise ValidationError(f"{path} keys differ; missing={missing}, extra={extra}")


def _string(value: Any, path: str, *, allow_empty: bool = False) -> str:
    if (
        type(value) is not str
        or len(value) > MAX_STRING
        or (not allow_empty and not value)
    ):
        raise ValidationError(f"{path} must be a{' possibly empty' if allow_empty else ' nonempty'} string")
    return value


def _integer(
    value: Any, path: str, *, minimum: int = 0, maximum: int = (1 << 63) - 1
) -> int:
    if type(value) is not int or value < minimum or value > maximum:
        raise ValidationError(f"{path} must be an exact integer in range")
    return value


def _boolean(value: Any, path: str) -> bool:
    if type(value) is not bool:
        raise ValidationError(f"{path} must be an exact Boolean")
    return value


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
            if identity in seen or len(child) > 64:
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
            if identity in seen or len(child) > 64:
                raise ValidationError("artifact contains a cycle or oversized list")
            seen.add(identity)
            for item in child:
                visit(item, depth + 1)
            seen.remove(identity)
        elif type(child) is str:
            if len(child) > MAX_STRING:
                raise ValidationError("artifact contains oversized text")
        elif child is not None and type(child) not in (int, bool):
            raise ValidationError("artifact contains a noncanonical JSON type")

    visit(value, 0)


def _scan_public(value: Any, path: str = "artifact") -> None:
    if type(value) is dict:
        forbidden = {
            "serial_number",
            "mac_address",
            "transport_port",
            "pairing_pin",
            "password",
            "private_key",
            "secret_value",
            "local_path",
        }
        found = forbidden.intersection(value)
        if found:
            raise ValidationError(f"{path} contains prohibited field(s): {sorted(found)}")
        for key, item in value.items():
            _scan_public(item, f"{path}.{key}")
    elif type(value) is list:
        for index, item in enumerate(value):
            _scan_public(item, f"{path}[{index}]")
    elif type(value) is str:
        for pattern in PRIVATE_TEXT:
            if pattern.search(value):
                raise ValidationError(f"{path} contains private machine/device text")


def _load(path: Path) -> dict[str, Any]:
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
    return _object(value, "artifact")


def canonical_sha256(value: dict[str, Any]) -> str:
    try:
        encoded = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    except (TypeError, ValueError, RecursionError) as exc:
        raise ValidationError("artifact is not canonically serializable") from exc
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest()


def _readiness_info(
    plan: dict[str, Any],
    readiness: dict[str, Any] | None,
    baseline: dict[str, Any] | None,
) -> dict[str, Any] | None:
    if readiness is None and baseline is None:
        return None
    if readiness is None or baseline is None:
        raise ValidationError("readiness and baseline must be supplied together")
    try:
        import crypto_benchmark_readiness as readiness_validator

        return readiness_validator.validate(readiness, plan, baseline)
    except (ImportError, ValueError, TypeError, RecursionError) as exc:
        raise ValidationError("candidate readiness is invalid or unaccepted") from exc


def validate_plan(
    plan: dict[str, Any],
    readiness: dict[str, Any] | None = None,
    baseline: dict[str, Any] | None = None,
) -> dict[str, Any]:
    _scan_structure(plan)
    _scan_public(plan)
    _exact_keys(
        plan,
        {
            "schema",
            "version",
            "artifact_kind",
            "benchmark_id",
            "status",
            "created_utc",
            "target",
            "toolchain",
            "radio",
            "candidates",
            "required_gates",
            "minimum_repetitions",
            "blockers",
        },
        "plan",
    )
    if plan["schema"] != SCHEMA or plan["version"] != VERSION or plan["artifact_kind"] != "plan":
        raise ValidationError("plan schema/version/artifact_kind mismatch")
    benchmark_id = _string(plan["benchmark_id"], "plan.benchmark_id")
    if not ID.fullmatch(benchmark_id):
        raise ValidationError("plan.benchmark_id is not canonical")
    status = _string(plan["status"], "plan.status")
    if status not in PLAN_STATUSES:
        raise ValidationError("plan.status must be draft_blocked or ready")
    if not UTC.fullmatch(_string(plan["created_utc"], "plan.created_utc")):
        raise ValidationError("plan.created_utc must be whole-second UTC")

    target = _object(plan["target"], "plan.target")
    _exact_keys(target, {"manufacturer", "board_model", "board_revision", "mcu", "flash_bytes", "psram_bytes"}, "plan.target")
    for field in ("manufacturer", "board_model", "board_revision", "mcu"):
        _string(target[field], f"plan.target.{field}", allow_empty=status == "draft_blocked")
    if target["mcu"] != "ESP32-S3":
        raise ValidationError("plan.target.mcu must remain ESP32-S3")
    _integer(target["flash_bytes"], "plan.target.flash_bytes", minimum=0 if status == "draft_blocked" else 1)
    _integer(target["psram_bytes"], "plan.target.psram_bytes")

    toolchain = _object(plan["toolchain"], "plan.toolchain")
    _exact_keys(toolchain, {"esp_idf_version", "esp_idf_commit", "compiler", "compiler_version", "sdkconfig_sha256"}, "plan.toolchain")
    for field in ("esp_idf_version", "compiler", "compiler_version"):
        _string(toolchain[field], f"plan.toolchain.{field}", allow_empty=status == "draft_blocked")
    for field, pattern in (("esp_idf_commit", HEX40), ("sdkconfig_sha256", HEX64)):
        value = _string(toolchain[field], f"plan.toolchain.{field}", allow_empty=status == "draft_blocked")
        if value and not pattern.fullmatch(value):
            raise ValidationError(f"plan.toolchain.{field} must be lowercase hexadecimal")

    radio = _object(plan["radio"], "plan.radio")
    _exact_keys(radio, {"mtu_bytes", "frequency_hz", "bandwidth_hz", "spreading_factor", "coding_rate_denominator"}, "plan.radio")
    radio_ranges = {
        "mtu_bytes": (1, 255),
        "frequency_hz": (137_000_000, 1_020_000_000),
        "bandwidth_hz": (7_800, 500_000),
        "spreading_factor": (5, 12),
        "coding_rate_denominator": (5, 8),
    }
    for field in radio:
        if status == "draft_blocked":
            _integer(radio[field], f"plan.radio.{field}", minimum=0)
        else:
            minimum, maximum = radio_ranges[field]
            _integer(
                radio[field],
                f"plan.radio.{field}",
                minimum=minimum,
                maximum=maximum,
            )

    candidates = _list(plan["candidates"], "plan.candidates")
    if len(candidates) != len(CANDIDATES):
        raise ValidationError("plan.candidates must contain the three fixed candidates")
    seen: set[str] = set()
    for index, raw in enumerate(candidates):
        candidate = _object(raw, f"plan.candidates[{index}]")
        _exact_keys(candidate, {"candidate_id", "role", "version", "source_commit", "lock_sha256", "license_spdx"}, f"plan.candidates[{index}]")
        candidate_id = _string(candidate["candidate_id"], f"plan.candidates[{index}].candidate_id")
        if candidate_id not in CANDIDATES or candidate_id in seen:
            raise ValidationError("plan candidate set is invalid or duplicated")
        seen.add(candidate_id)
        if candidate["role"] != CANDIDATES[candidate_id]:
            raise ValidationError(f"{candidate_id} role mismatch")
        if candidate_id != tuple(CANDIDATES)[index]:
            raise ValidationError("plan candidate order must remain canonical")
        for field in ("version", "license_spdx"):
            _string(candidate[field], f"{candidate_id}.{field}", allow_empty=status == "draft_blocked")
        for field, pattern in (("source_commit", HEX40), ("lock_sha256", HEX64)):
            value = _string(candidate[field], f"{candidate_id}.{field}", allow_empty=status == "draft_blocked")
            if value and not pattern.fullmatch(value):
                raise ValidationError(f"{candidate_id}.{field} must be lowercase hexadecimal")
            if status == "ready" and len(set(value)) == 1:
                raise ValidationError(f"{candidate_id}.{field} cannot be a placeholder digest")
    if seen != set(CANDIDATES):
        raise ValidationError("plan candidate set is incomplete")

    if plan["required_gates"] != list(GATES):
        raise ValidationError("plan.required_gates must match the canonical ordered gate set")
    repetitions = _object(plan["minimum_repetitions"], "plan.minimum_repetitions")
    _exact_keys(repetitions, {"cold", "warm"}, "plan.minimum_repetitions")
    if repetitions["cold"] != 100 or repetitions["warm"] != 100:
        raise ValidationError("minimum repetitions must be exactly 100 cold and 100 warm")
    blockers = _list(plan["blockers"], "plan.blockers")
    if any(type(item) is not str or not item for item in blockers):
        raise ValidationError("plan.blockers must be a list of nonempty strings")
    if status == "ready" and blockers:
        raise ValidationError("ready plan cannot contain blockers")
    if status == "draft_blocked" and not blockers:
        raise ValidationError("draft_blocked plan must name at least one blocker")
    if status == "draft_blocked" and blockers != list(BLOCKERS):
        raise ValidationError("OTCB0/v0 draft must preserve the canonical blockers")
    readiness_info = _readiness_info(plan, readiness, baseline)
    readiness_verified = bool(
        readiness_info
        and readiness_info["fully_resolved"] is True
        and readiness_info["accepted_for_legacy_v0"] is True
        and readiness_info["execution_authorized"] is True
    )
    return {
        "status": status,
        "benchmark_id": benchmark_id,
        "plan_sha256": canonical_sha256(plan),
        "readiness_verified": readiness_verified,
        "execution_authorized": readiness_verified,
    }


def _stats(value: Any, path: str) -> None:
    stats = _object(value, path)
    _exact_keys(stats, {"min_us", "median_us", "p95_us", "max_us"}, path)
    ordered = [_integer(stats[name], f"{path}.{name}", minimum=1) for name in ("min_us", "median_us", "p95_us", "max_us")]
    if ordered != sorted(ordered):
        raise ValidationError(f"{path} timing values must be nondecreasing")


def validate_result(
    plan: dict[str, Any],
    result: dict[str, Any],
    readiness: dict[str, Any] | None = None,
    baseline: dict[str, Any] | None = None,
) -> dict[str, Any]:
    plan_info = validate_plan(plan, readiness, baseline)
    _scan_structure(result)
    _scan_public(result)
    _exact_keys(
        result,
        {
            "schema",
            "version",
            "artifact_kind",
            "benchmark_id",
            "plan_sha256",
            "candidate_id",
            "completed_utc",
            "repetitions",
            "timings",
            "build",
            "resources",
            "radio_cost",
            "evidence",
            "gates",
            "notes",
        },
        "result",
    )
    if result["schema"] != SCHEMA or result["version"] != VERSION or result["artifact_kind"] != "result":
        raise ValidationError("result schema/version/artifact_kind mismatch")
    if result["benchmark_id"] != plan_info["benchmark_id"]:
        raise ValidationError("result benchmark_id does not match plan")
    if result["plan_sha256"] != plan_info["plan_sha256"]:
        raise ValidationError("result plan_sha256 does not match canonical plan")
    candidate_id = _string(result["candidate_id"], "result.candidate_id")
    if candidate_id not in CANDIDATES:
        raise ValidationError("result candidate_id is not in the plan")
    if not UTC.fullmatch(_string(result["completed_utc"], "result.completed_utc")):
        raise ValidationError("result.completed_utc must be whole-second UTC")

    repetitions = _object(result["repetitions"], "result.repetitions")
    _exact_keys(repetitions, {"cold", "warm"}, "result.repetitions")
    for mode in ("cold", "warm"):
        _integer(repetitions[mode], f"result.repetitions.{mode}")

    timings = _object(result["timings"], "result.timings")
    _exact_keys(timings, set(OPERATIONS), "result.timings")
    for operation in OPERATIONS:
        modes = _object(timings[operation], f"result.timings.{operation}")
        _exact_keys(modes, {"cold", "warm"}, f"result.timings.{operation}")
        _stats(modes["cold"], f"result.timings.{operation}.cold")
        _stats(modes["warm"], f"result.timings.{operation}.warm")

    build = _object(result["build"], "result.build")
    _exact_keys(build, {"passed", "compiler_warnings"}, "result.build")
    _boolean(build["passed"], "result.build.passed")
    _integer(build["compiler_warnings"], "result.build.compiler_warnings")

    resources = _object(result["resources"], "result.resources")
    _exact_keys(resources, {"linked_flash_delta_bytes", "static_ram_bytes", "peak_dynamic_ram_bytes", "max_stack_used_bytes", "watchdog_resets"}, "result.resources")
    for field in ("linked_flash_delta_bytes", "static_ram_bytes", "peak_dynamic_ram_bytes", "max_stack_used_bytes"):
        _integer(resources[field], f"result.resources.{field}", minimum=1)
    _integer(resources["watchdog_resets"], "result.resources.watchdog_resets")

    radio = _object(result["radio_cost"], "result.radio_cost")
    _exact_keys(radio, {"handshake_bytes", "fragments", "airtime_us", "retries_tested"}, "result.radio_cost")
    for field in radio:
        _integer(radio[field], f"result.radio_cost.{field}", minimum=1)

    evidence = _object(result["evidence"], "result.evidence")
    _exact_keys(evidence, {"binary_sha256", "sdkconfig_sha256", "sbom_sha256", "raw_evidence_retained_privately"}, "result.evidence")
    for field in ("binary_sha256", "sdkconfig_sha256", "sbom_sha256"):
        if not HEX64.fullmatch(_string(evidence[field], f"result.evidence.{field}")):
            raise ValidationError(f"result.evidence.{field} must be lowercase SHA-256")
    if evidence["sdkconfig_sha256"] != plan["toolchain"]["sdkconfig_sha256"]:
        raise ValidationError("result sdkconfig does not match plan")
    _boolean(evidence["raw_evidence_retained_privately"], "result.evidence.raw_evidence_retained_privately")

    gates = _object(result["gates"], "result.gates")
    _exact_keys(gates, set(GATES), "result.gates")
    gate_values = {gate: _boolean(gates[gate], f"result.gates.{gate}") for gate in GATES}
    notes = _string(result["notes"], "result.notes", allow_empty=True)
    if len(notes) > 500:
        raise ValidationError("result.notes exceeds 500 characters")

    failures: list[str] = []
    if plan_info["status"] != "ready":
        failures.append("plan_not_ready")
    if not plan_info["readiness_verified"]:
        failures.append("candidate_readiness_not_verified")
    if not build["passed"]:
        failures.append("build_failed")
    if build["compiler_warnings"] != 0:
        failures.append("compiler_warnings_observed")
    for mode in ("cold", "warm"):
        if repetitions[mode] < plan["minimum_repetitions"][mode]:
            failures.append(f"insufficient_{mode}_repetitions")
    if resources["watchdog_resets"] != 0:
        failures.append("watchdog_reset_observed")
    if not evidence["raw_evidence_retained_privately"]:
        failures.append("raw_evidence_not_retained")
    failures.extend(f"gate_failed:{gate}" for gate, passed in gate_values.items() if not passed)
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "artifact_kind": "verdict",
        "benchmark_id": plan_info["benchmark_id"],
        "candidate_id": candidate_id,
        "verdict": "pass" if not failures else "fail",
        "failures": failures,
    }


def result_template(
    plan: dict[str, Any],
    candidate_id: str,
    readiness: dict[str, Any] | None = None,
    baseline: dict[str, Any] | None = None,
) -> dict[str, Any]:
    info = validate_plan(plan, readiness, baseline)
    if info["status"] != "ready":
        raise ValidationError("result template requires a ready plan")
    if not info["readiness_verified"]:
        raise ValidationError(
            "result template requires separately verified fully resolved readiness"
        )
    if candidate_id not in CANDIDATES:
        raise ValidationError("candidate_id is not canonical")
    zero_stats = {"min_us": 0, "median_us": 0, "p95_us": 0, "max_us": 0}
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "artifact_kind": "result",
        "benchmark_id": info["benchmark_id"],
        "plan_sha256": info["plan_sha256"],
        "candidate_id": candidate_id,
        "completed_utc": "",
        "repetitions": {"cold": 0, "warm": 0},
        "timings": {operation: {"cold": dict(zero_stats), "warm": dict(zero_stats)} for operation in OPERATIONS},
        "build": {"passed": False, "compiler_warnings": 0},
        "resources": {"linked_flash_delta_bytes": 0, "static_ram_bytes": 0, "peak_dynamic_ram_bytes": 0, "max_stack_used_bytes": 0, "watchdog_resets": 0},
        "radio_cost": {"handshake_bytes": 0, "fragments": 0, "airtime_us": 0, "retries_tested": 0},
        "evidence": {"binary_sha256": "", "sdkconfig_sha256": plan["toolchain"]["sdkconfig_sha256"], "sbom_sha256": "", "raw_evidence_retained_privately": False},
        "gates": {gate: False for gate in GATES},
        "notes": "",
    }


def _write_new(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as handle:
        json.dump(value, handle, indent=2, sort_keys=False)
        handle.write("\n")


def main(argv: list[str] | None = None) -> int:
    parser = SafeArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    validate = sub.add_parser("validate-plan")
    validate.add_argument("plan", type=Path)
    template = sub.add_parser("create-result-template")
    template.add_argument("plan", type=Path)
    template.add_argument("candidate_id", choices=tuple(CANDIDATES))
    template.add_argument("output", type=Path)
    evaluate = sub.add_parser("evaluate")
    evaluate.add_argument("plan", type=Path)
    evaluate.add_argument("result", type=Path)
    for command in (validate, template, evaluate):
        command.add_argument("--readiness", type=Path)
        command.add_argument("--baseline", type=Path)
    args = parser.parse_args(argv)
    try:
        plan = _load(args.plan)
        if (args.readiness is None) != (args.baseline is None):
            raise ValidationError("readiness and baseline must be supplied together")
        readiness = _load(args.readiness) if args.readiness is not None else None
        baseline = _load(args.baseline) if args.baseline is not None else None
        if args.command == "validate-plan":
            print(json.dumps(validate_plan(plan, readiness, baseline), sort_keys=True))
        elif args.command == "create-result-template":
            _write_new(
                args.output,
                result_template(plan, args.candidate_id, readiness, baseline),
            )
            print("CREATED")
        else:
            result = _load(args.result)
            print(
                json.dumps(
                    validate_result(plan, result, readiness, baseline), sort_keys=True
                )
            )
        return 0
    except (ValidationError, FileExistsError):
        print("ERROR: crypto benchmark evidence is invalid or unaccepted", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
