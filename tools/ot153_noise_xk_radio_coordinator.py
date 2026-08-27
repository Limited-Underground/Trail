#!/usr/bin/env python3
"""Restoration-safe coordinator for the future OT-154-authorized OT-153 run.

The coordinator is dependency injected and has no CLI or concrete hardware
backend.  It verifies the exact installed Trail application on both anonymous
nodes and hard-resets both before it atomically consumes the future authority.
It then application-flashes and readback-verifies both benchmark nodes before
invoking only the bound OT-153 Noise XK radio runner.  Every benchmark-touched
node is restored independently after success, failure, or ``BaseException``.

Private endpoints, filesystem paths, backend text, and device identifiers are
never serialized into the journal or sanitized execution/recovery receipts.
"""

from __future__ import annotations

import dataclasses
import enum
import hashlib
import importlib.util
import json
import os
import re
import secrets
import stat
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Protocol


ROOT = Path(__file__).resolve().parents[1]
PRIVATE_ROOT = ROOT / ".private"
JOURNAL_PATH = PRIVATE_ROOT / "ot153-noise-xk-radio-execution-journal.json"
EXECUTION_RECEIPT_PATH = PRIVATE_ROOT / "ot153-noise-xk-radio-execution-receipt.json"
RECOVERY_RECEIPT_PATH = PRIVATE_ROOT / "ot153-noise-xk-radio-recovery-receipt.json"

RESTORE_NAME = "opentrail_heltec_v4_bench.bin"
RESTORE_BYTES = 500_944
RESTORE_SHA256 = "f2a58414f82eed585ba90bf7671b06c8ebfaa82e8b23f6ac2093de95143b4e0e"
APPLICATION_OFFSET = 0x10000
FACTORY_SLOT_BYTES = 0x500000 - APPLICATION_OFFSET
BAUD = 115_200
RUNNER_NAME = "ot153_noise_xk_radio_runner.py"
JOURNAL_SCHEMA = "OT153NXJ0"
RECEIPT_SCHEMA = "OT153NXCR0"
HASH64 = re.compile(r"^[0-9a-f]{64}$")
SAFE_IMAGE_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}\.bin$")

_RUNNER_PATH = ROOT / "tools" / RUNNER_NAME
_RUNNER_SPEC = importlib.util.spec_from_file_location("_ot153_noise_xk_runner", _RUNNER_PATH)
if _RUNNER_SPEC is None or _RUNNER_SPEC.loader is None:
    raise RuntimeError("runner_contract_unavailable")
runner = importlib.util.module_from_spec(_RUNNER_SPEC)
sys.modules[_RUNNER_SPEC.name] = runner
_RUNNER_SPEC.loader.exec_module(runner)


class FailureCode(str, enum.Enum):
    INVALID_CONFIGURATION = "invalid_configuration"
    AUTHORITY_REJECTED = "authority_rejected"
    ARTIFACT_INVALID = "artifact_invalid"
    AUTHORITY_ALREADY_CONSUMED = "authority_already_consumed"
    PREFLIGHT_FAILED = "preflight_failed"
    JOURNAL_FAILED = "journal_failed"
    BENCHMARK_WRITE_FAILED = "benchmark_write_failed"
    BENCHMARK_VERIFY_FAILED = "benchmark_verify_failed"
    BENCHMARK_RESET_FAILED = "benchmark_reset_failed"
    RADIO_RUN_FAILED = "radio_run_failed"
    RESTORE_FAILED = "restore_failed"
    RECEIPT_FAILED = "receipt_failed"
    RECOVERY_JOURNAL_ABSENT = "recovery_journal_absent"
    RECOVERY_NOT_REQUIRED = "recovery_not_required"


class CoordinatorError(RuntimeError):
    """Closed coordinator failure carrying no private diagnostic payload."""

    def __init__(self, code: FailureCode) -> None:
        super().__init__(code.value)
        self.code = code


@dataclass(frozen=True, repr=False)
class Image:
    name: str
    payload: bytes
    sha256: str

    @property
    def size(self) -> int:
        return len(self.payload)


@dataclass(frozen=True)
class ExecutionBinding:
    benchmark_name: str
    benchmark_bytes: int
    benchmark_sha256: str
    restore_name: str
    restore_bytes: int
    restore_sha256: str
    application_offset: int
    baud: int
    runner_name: str
    runner_sha256: str
    runner_schema: str


@dataclass(frozen=True)
class AuthorityGrant:
    raw_sha256: str
    attempt_count: int
    reusable: bool
    radio_allowed: bool


@dataclass(frozen=True, repr=False)
class RunConfig:
    private_endpoints: tuple[object, object]
    binding: ExecutionBinding
    benchmark_path: Path
    restore_path: Path


class RadioEndpoint(runner.Endpoint, Protocol):
    def close(self) -> None: ...


class DeviceBackend(Protocol):
    """Exact application mutation and serial-radio boundary."""

    def write_application(self, private_endpoint: object, offset: int, image: Image) -> None: ...
    def verify_application(self, private_endpoint: object, offset: int, image: Image) -> None: ...
    def hard_reset(self, private_endpoint: object) -> None: ...
    def open_radio_endpoint(self, private_endpoint: object) -> RadioEndpoint: ...


class AuthorityGate(Protocol):
    def validate(self, binding: ExecutionBinding, *, recovery: bool) -> AuthorityGrant: ...


@dataclass(frozen=True)
class _Failure:
    code: FailureCode


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _canonical_bytes(value: Any) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True, allow_nan=False
    ).encode("ascii")


def _attempt(operation: Callable[[], Any]) -> tuple[bool, Any | None]:
    try:
        return True, operation()
    except BaseException:
        return False, None


def _runner_digest() -> str | None:
    ok, payload = _attempt(_RUNNER_PATH.read_bytes)
    return _sha256(payload) if ok and isinstance(payload, bytes) else None


def _binding_valid(value: object) -> bool:
    return (
        isinstance(value, ExecutionBinding)
        and type(value.benchmark_name) is str
        and SAFE_IMAGE_NAME.fullmatch(value.benchmark_name) is not None
        and type(value.benchmark_bytes) is int
        and 0 < value.benchmark_bytes <= FACTORY_SLOT_BYTES
        and type(value.benchmark_sha256) is str
        and HASH64.fullmatch(value.benchmark_sha256) is not None
        and value.restore_name == RESTORE_NAME
        and value.restore_bytes == RESTORE_BYTES
        and value.restore_sha256 == RESTORE_SHA256
        and value.application_offset == APPLICATION_OFFSET
        and value.baud == BAUD
        and value.runner_name == RUNNER_NAME
        and type(value.runner_sha256) is str
        and HASH64.fullmatch(value.runner_sha256) is not None
        and value.runner_sha256 == _runner_digest()
        and value.runner_schema == runner.SCHEMA == "OT153NXR0"
    )


def _valid_grant(value: object) -> bool:
    return (
        isinstance(value, AuthorityGrant)
        and type(value.raw_sha256) is str
        and HASH64.fullmatch(value.raw_sha256) is not None
        and type(value.attempt_count) is int
        and value.attempt_count == 1
        and value.reusable is False
        and value.radio_allowed is True
    )


def _has_reparse_or_symlink_ancestry(path: Path, root: Path) -> bool:
    try:
        relative = path.relative_to(root)
    except ValueError:
        return True
    current = root
    for part in relative.parts:
        current /= part
        try:
            metadata = current.lstat()
        except FileNotFoundError:
            continue
        except OSError:
            return True
        attributes = getattr(metadata, "st_file_attributes", 0)
        if current.is_symlink() or (
            attributes & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
        ):
            return True
    return False


def _private_paths_valid() -> bool:
    expected = {
        JOURNAL_PATH: "ot153-noise-xk-radio-execution-journal.json",
        EXECUTION_RECEIPT_PATH: "ot153-noise-xk-radio-execution-receipt.json",
        RECOVERY_RECEIPT_PATH: "ot153-noise-xk-radio-recovery-receipt.json",
    }
    paths = tuple(expected)
    return (
        PRIVATE_ROOT.is_absolute()
        and PRIVATE_ROOT.is_dir()
        and not _has_reparse_or_symlink_ancestry(PRIVATE_ROOT, ROOT)
        and len({path.resolve() for path in paths}) == len(paths)
        and all(
            path.is_absolute()
            and path.parent.resolve() == PRIVATE_ROOT.resolve()
            and path.name == name
            for path, name in expected.items()
        )
    )


def _endpoints_distinct(endpoints: tuple[object, object]) -> bool:
    try:
        equal = endpoints[0] == endpoints[1]
    except BaseException:
        return False
    return type(equal) is bool and not equal


def _config_valid(config: object, *, recovery: bool) -> bool:
    if (
        not isinstance(config, RunConfig)
        or not _binding_valid(config.binding)
        or not _private_paths_valid()
    ):
        return False
    endpoints = config.private_endpoints
    receipt_path = RECOVERY_RECEIPT_PATH if recovery else EXECUTION_RECEIPT_PATH
    return (
        type(endpoints) is tuple
        and len(endpoints) == 2
        and endpoints[0] is not None
        and endpoints[1] is not None
        and _endpoints_distinct(endpoints)
        and isinstance(config.benchmark_path, Path)
        and isinstance(config.restore_path, Path)
        and config.benchmark_path.is_absolute()
        and config.restore_path.is_absolute()
        and JOURNAL_PATH != receipt_path
        and not receipt_path.exists()
    )


def _read_exact_image(
    path: Path, expected_name: str, expected_bytes: int, expected_sha256: str
) -> Image | None:
    if (
        path.name != expected_name
        or path.is_symlink()
        or type(expected_sha256) is not str
        or HASH64.fullmatch(expected_sha256) is None
        or type(expected_bytes) is not int
        or expected_bytes <= 0
        or expected_bytes > FACTORY_SLOT_BYTES
    ):
        return None
    ok, payload_value = _attempt(path.read_bytes)
    if not ok or not isinstance(payload_value, bytes):
        return None
    if len(payload_value) != expected_bytes or _sha256(payload_value) != expected_sha256:
        return None
    return Image(expected_name, payload_value, expected_sha256)


def _node_state() -> dict[str, bool]:
    return {
        "installed_app_readback_verified": False,
        "preflight_reset_completed": False,
        "benchmark_write_started": False,
        "benchmark_readback_verified": False,
        "benchmark_reset_completed": False,
        "restore_write_started": False,
        "restore_readback_verified": False,
        "restore_reset_completed": False,
    }


def _new_journal(binding: ExecutionBinding, grant: AuthorityGrant) -> dict[str, Any]:
    return {
        "schema": JOURNAL_SCHEMA,
        "version": 0,
        "state": "started",
        "authority_raw_sha256": grant.raw_sha256,
        "binding": dataclasses.asdict(binding),
        "radio_run_invoked": False,
        "radio_result_validated": False,
        "nodes": {"A": _node_state(), "B": _node_state()},
    }


def _journal_valid(value: object, binding: ExecutionBinding, grant: AuthorityGrant) -> bool:
    if type(value) is not dict or set(value) != {
        "schema", "version", "state", "authority_raw_sha256", "binding",
        "radio_run_invoked", "radio_result_validated", "nodes",
    }:
        return False
    if (
        value["schema"] != JOURNAL_SCHEMA
        or value["version"] != 0
        or value["state"] not in {"started", "restored", "aborted"}
        or value["authority_raw_sha256"] != grant.raw_sha256
        or value["binding"] != dataclasses.asdict(binding)
        or type(value["radio_run_invoked"]) is not bool
        or type(value["radio_result_validated"]) is not bool
        or (value["radio_result_validated"] and not value["radio_run_invoked"])
        or type(value["nodes"]) is not dict
        or set(value["nodes"]) != {"A", "B"}
    ):
        return False
    expected = set(_node_state())
    for label in ("A", "B"):
        state = value["nodes"][label]
        if type(state) is not dict or set(state) != expected:
            return False
        if any(type(state[key]) is not bool for key in expected):
            return False
        if state["benchmark_write_started"] and not (
            state["installed_app_readback_verified"] and state["preflight_reset_completed"]
        ):
            return False
        if state["benchmark_readback_verified"] and not state["benchmark_write_started"]:
            return False
        if state["benchmark_reset_completed"] and not state["benchmark_readback_verified"]:
            return False
        if state["restore_readback_verified"] and not state["restore_write_started"]:
            return False
        if state["restore_reset_completed"] and not state["restore_readback_verified"]:
            return False
    if value["radio_run_invoked"] and not all(
        value["nodes"][label]["benchmark_reset_completed"] for label in ("A", "B")
    ):
        return False
    return True


def _write_new(path: Path, value: dict[str, Any]) -> bool:
    payload = _canonical_bytes(value) + b"\n"
    try:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        return True
    except OSError:
        return False


def _atomic_replace(path: Path, value: dict[str, Any]) -> bool:
    payload = _canonical_bytes(value) + b"\n"
    temporary = path.parent / f".{path.name}.{secrets.token_hex(8)}.tmp"
    try:
        descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        return True
    except OSError:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass
        return False


def _load_journal(binding: ExecutionBinding, grant: AuthorityGrant) -> dict[str, Any] | None:
    ok, raw_value = _attempt(JOURNAL_PATH.read_bytes)
    if not ok or not isinstance(raw_value, bytes):
        return None
    if not raw_value or len(raw_value) > 65_536 or not raw_value.endswith(b"\n"):
        return None
    try:
        value = json.loads(raw_value.decode("ascii"))
    except (UnicodeError, json.JSONDecodeError):
        return None
    if _canonical_bytes(value) + b"\n" != raw_value:
        return None
    return value if _journal_valid(value, binding, grant) else None


def _persist(journal: dict[str, Any]) -> bool:
    return _atomic_replace(JOURNAL_PATH, journal)


def _restoration_complete(journal: dict[str, Any]) -> bool:
    return all(
        not journal["nodes"][label]["benchmark_write_started"]
        or journal["nodes"][label]["restore_reset_completed"]
        for label in ("A", "B")
    )


def _restore_touched(
    config: RunConfig, backend: DeviceBackend, restore: Image, journal: dict[str, Any]
) -> bool:
    """Attempt the complete restore sequence for every touched node independently."""
    complete = True
    for label, endpoint in zip(("A", "B"), config.private_endpoints):
        state = journal["nodes"][label]
        if not state["benchmark_write_started"] or state["restore_reset_completed"]:
            continue
        state["restore_write_started"] = True
        if not _persist(journal):
            complete = False
        write_ok, _ = _attempt(
            lambda endpoint=endpoint: backend.write_application(
                endpoint, APPLICATION_OFFSET, restore
            )
        )
        if not write_ok:
            complete = False
            continue
        verify_ok, _ = _attempt(
            lambda endpoint=endpoint: backend.verify_application(
                endpoint, APPLICATION_OFFSET, restore
            )
        )
        if not verify_ok:
            complete = False
            continue
        state["restore_readback_verified"] = True
        if not _persist(journal):
            complete = False
        reset_ok, _ = _attempt(lambda endpoint=endpoint: backend.hard_reset(endpoint))
        if not reset_ok:
            complete = False
            continue
        state["restore_reset_completed"] = True
        if not _persist(journal):
            complete = False
    return complete and _restoration_complete(journal)


def _preflight(config: RunConfig, backend: DeviceBackend, restore: Image) -> bool:
    results: list[bool] = []
    for endpoint in config.private_endpoints:
        ok, _ = _attempt(
            lambda endpoint=endpoint: backend.verify_application(
                endpoint, APPLICATION_OFFSET, restore
            )
        )
        results.append(ok)
    for endpoint in config.private_endpoints:
        ok, _ = _attempt(lambda endpoint=endpoint: backend.hard_reset(endpoint))
        results.append(ok)
    return all(results)


def _safe_radio_result(value: object) -> dict[str, Any] | None:
    if type(value) is not dict:
        return None
    try:
        validated = runner.validate_public_result(value)
        # Canonical round-trip rejects non-JSON objects and non-finite numbers.
        round_trip = json.loads(_canonical_bytes(validated).decode("ascii"))
    except BaseException:
        return None
    return round_trip if type(round_trip) is dict else None


def _receipt(
    journal: dict[str, Any],
    result: str,
    restoration_complete: bool,
    radio_result: dict[str, Any] | None,
    failure: _Failure | None,
) -> dict[str, Any]:
    nodes = []
    for label in ("A", "B"):
        state = journal["nodes"][label]
        nodes.append({
            "node": label,
            "installed_app_readback_verified": state["installed_app_readback_verified"],
            "preflight_reset_completed": state["preflight_reset_completed"],
            "benchmark_readback_verified": state["benchmark_readback_verified"],
            "benchmark_reset_completed": state["benchmark_reset_completed"],
            "restore_readback_verified": state["restore_readback_verified"],
            "restore_reset_completed": state["restore_reset_completed"],
        })
    receipt: dict[str, Any] = {
        "schema": RECEIPT_SCHEMA,
        "version": 0,
        "result": result,
        "authority_raw_sha256": journal["authority_raw_sha256"],
        "binding": journal["binding"],
        "node_count": 2,
        "radio_run_invoked": journal["radio_run_invoked"],
        "radio_result_validated": journal["radio_result_validated"],
        "restoration_complete": restoration_complete,
        "nodes": nodes,
        "failure": None if failure is None else {"code": failure.code.value},
        "privacy": {
            "private_endpoints_recorded": False,
            "device_identifiers_recorded": False,
            "filesystem_paths_recorded": False,
            "backend_error_text_recorded": False,
            "raw_serial_recorded": False,
        },
        "claims": {
            "packet_v1_selected": False,
            "candidate_selected": False,
            "suite_selected": False,
            "phase_two_complete": False,
            "supported_target_proven": False,
            "regulatory_acceptance_proven": False,
            "production_ready": False,
            "score_credit_added": False,
        },
    }
    if radio_result is not None:
        receipt["radio_result"] = radio_result
        receipt["radio_result_sha256"] = _sha256(_canonical_bytes(radio_result))
    return receipt


def _raise(failure: _Failure) -> None:
    raise CoordinatorError(failure.code) from None


def _prepare(
    config: RunConfig, authority: AuthorityGate, *, recovery: bool
) -> tuple[ExecutionBinding, AuthorityGrant, Image, Image]:
    if not _config_valid(config, recovery=recovery):
        _raise(_Failure(FailureCode.INVALID_CONFIGURATION))
    binding = config.binding
    grant_ok, grant_value = _attempt(lambda: authority.validate(binding, recovery=recovery))
    if not grant_ok or not _valid_grant(grant_value):
        _raise(_Failure(FailureCode.AUTHORITY_REJECTED))
    benchmark = _read_exact_image(
        config.benchmark_path,
        binding.benchmark_name,
        binding.benchmark_bytes,
        binding.benchmark_sha256,
    )
    restore = _read_exact_image(
        config.restore_path,
        binding.restore_name,
        binding.restore_bytes,
        binding.restore_sha256,
    )
    if benchmark is None or restore is None:
        _raise(_Failure(FailureCode.ARTIFACT_INVALID))
    return binding, grant_value, benchmark, restore


def _prepare_recovery(
    config: RunConfig, authority: AuthorityGate
) -> tuple[ExecutionBinding, AuthorityGrant, Image]:
    if not _config_valid(config, recovery=True):
        _raise(_Failure(FailureCode.INVALID_CONFIGURATION))
    binding = config.binding
    grant_ok, grant_value = _attempt(lambda: authority.validate(binding, recovery=True))
    if not grant_ok or not _valid_grant(grant_value):
        _raise(_Failure(FailureCode.AUTHORITY_REJECTED))
    restore = _read_exact_image(
        config.restore_path,
        binding.restore_name,
        binding.restore_bytes,
        binding.restore_sha256,
    )
    if restore is None:
        _raise(_Failure(FailureCode.ARTIFACT_INVALID))
    return binding, grant_value, restore


def execute(config: RunConfig, backend: DeviceBackend, authority: AuthorityGate) -> dict[str, Any]:
    """Consume one authority, run once, and restore every benchmark-touched node."""
    binding, grant, benchmark, restore = _prepare(config, authority, recovery=False)
    if JOURNAL_PATH.exists():
        _raise(_Failure(FailureCode.AUTHORITY_ALREADY_CONSUMED))
    if not _preflight(config, backend, restore):
        _raise(_Failure(FailureCode.PREFLIGHT_FAILED))

    journal = _new_journal(binding, grant)
    for label in ("A", "B"):
        journal["nodes"][label]["installed_app_readback_verified"] = True
        journal["nodes"][label]["preflight_reset_completed"] = True
    # O_EXCL is the irreversible consumption boundary and precedes first write.
    if not _write_new(JOURNAL_PATH, journal):
        _raise(_Failure(FailureCode.JOURNAL_FAILED))

    failure: _Failure | None = None
    radio_result: dict[str, Any] | None = None
    opened: list[RadioEndpoint] = []
    try:
        for label, endpoint in zip(("A", "B"), config.private_endpoints):
            state = journal["nodes"][label]
            state["benchmark_write_started"] = True
            if not _persist(journal):
                failure = _Failure(FailureCode.JOURNAL_FAILED)
                break
            write_ok, _ = _attempt(
                lambda endpoint=endpoint: backend.write_application(
                    endpoint, APPLICATION_OFFSET, benchmark
                )
            )
            if not write_ok:
                failure = _Failure(FailureCode.BENCHMARK_WRITE_FAILED)
                break
            verify_ok, _ = _attempt(
                lambda endpoint=endpoint: backend.verify_application(
                    endpoint, APPLICATION_OFFSET, benchmark
                )
            )
            if not verify_ok:
                failure = _Failure(FailureCode.BENCHMARK_VERIFY_FAILED)
                break
            state["benchmark_readback_verified"] = True
            if not _persist(journal):
                failure = _Failure(FailureCode.JOURNAL_FAILED)
                break

        if failure is None:
            reset_results: list[bool] = []
            for label, endpoint in zip(("A", "B"), config.private_endpoints):
                reset_ok, _ = _attempt(lambda endpoint=endpoint: backend.hard_reset(endpoint))
                reset_results.append(reset_ok)
                if reset_ok:
                    journal["nodes"][label]["benchmark_reset_completed"] = True
                    if not _persist(journal):
                        reset_results[-1] = False
            if not all(reset_results):
                failure = _Failure(FailureCode.BENCHMARK_RESET_FAILED)

        if failure is None:
            # The runner is not entered until both benchmark readbacks and resets pass.
            journal["radio_run_invoked"] = True
            if not _persist(journal):
                failure = _Failure(FailureCode.JOURNAL_FAILED)
            else:
                for endpoint in config.private_endpoints:
                    ok, opened_value = _attempt(
                        lambda endpoint=endpoint: backend.open_radio_endpoint(endpoint)
                    )
                    if not ok or opened_value is None:
                        failure = _Failure(FailureCode.RADIO_RUN_FAILED)
                        break
                    opened.append(opened_value)
                if failure is None and len(opened) == 2:
                    run_ok, run_value = _attempt(lambda: runner.run(opened[0], opened[1]))
                    if not run_ok:
                        failure = _Failure(FailureCode.RADIO_RUN_FAILED)
                    else:
                        radio_result = _safe_radio_result(run_value)
                        if radio_result is None:
                            failure = _Failure(FailureCode.RADIO_RUN_FAILED)
                        else:
                            journal["radio_result_validated"] = True
                            if not _persist(journal):
                                failure = _Failure(FailureCode.JOURNAL_FAILED)
    except BaseException:
        # No interrupt or backend exception may bypass the restoration boundary.
        failure = _Failure(FailureCode.RADIO_RUN_FAILED)
    finally:
        for endpoint in reversed(opened):
            _attempt(endpoint.close)

    restored = _restore_touched(config, backend, restore, journal)
    success = failure is None and radio_result is not None and restored
    if failure is None and not restored:
        failure = _Failure(FailureCode.RESTORE_FAILED)
    journal["state"] = "restored" if success else "aborted"
    if not _persist(journal) and failure is None:
        failure = _Failure(FailureCode.JOURNAL_FAILED)
        success = False

    receipt = _receipt(
        journal,
        "noise_xk_radio_run_passed_and_restored" if success
        else "noise_xk_radio_execution_aborted",
        restored,
        radio_result if success else None,
        failure,
    )
    if not _write_new(EXECUTION_RECEIPT_PATH, receipt):
        _raise(_Failure(FailureCode.RECEIPT_FAILED))
    if not success:
        _raise(failure or _Failure(FailureCode.RESTORE_FAILED))
    return receipt


def recover(config: RunConfig, backend: DeviceBackend, authority: AuthorityGate) -> dict[str, Any]:
    """Retry exact Trail restoration only; never load or write benchmark bytes."""
    binding, grant, restore = _prepare_recovery(config, authority)
    if not JOURNAL_PATH.exists():
        _raise(_Failure(FailureCode.RECOVERY_JOURNAL_ABSENT))
    journal = _load_journal(binding, grant)
    if journal is None:
        _raise(_Failure(FailureCode.JOURNAL_FAILED))
    if journal["state"] == "restored" or _restoration_complete(journal):
        _raise(_Failure(FailureCode.RECOVERY_NOT_REQUIRED))

    restored = _restore_touched(config, backend, restore, journal)
    journal["state"] = "aborted"
    persist_ok = _persist(journal)
    if not restored or not persist_ok:
        # Leave the one fixed recovery receipt path unused so retry remains possible.
        _raise(_Failure(FailureCode.RESTORE_FAILED))
    receipt = _receipt(journal, "recovery_only_restored", True, None, None)
    if not _write_new(RECOVERY_RECEIPT_PATH, receipt):
        _raise(_Failure(FailureCode.RECEIPT_FAILED))
    return receipt
