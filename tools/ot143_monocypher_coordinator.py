#!/usr/bin/env python3
"""Restoration-safe OT-143 two-node Monocypher execution coordinator.

The module is dependency injected and intentionally has no command-line entry
point or concrete device backend.  A separately accepted authority validator
must authorize the exact artifact binding before either private endpoint is
queried.  Private endpoints, raw captures, filesystem paths, and backend error
text never enter the journal, receipts, returned errors, or public diagnostics.
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
JOURNAL_PATH = PRIVATE_ROOT / "ot143-monocypher-execution-journal.json"
EXECUTION_RECEIPT_PATH = PRIVATE_ROOT / "ot143-monocypher-execution-receipt.json"
RECOVERY_RECEIPT_PATH = PRIVATE_ROOT / "ot143-monocypher-recovery-receipt.json"

BENCHMARK_NAME = "ot139_monocypher_quiet_bench.bin"
BENCHMARK_BYTES = 149_920
BENCHMARK_SHA256 = "29eee8c7294064d772770e2b4591c352eb0a9068b63f5a1fc62d89481ec5f204"
RESTORE_NAME = "opentrail_heltec_v4_bench.bin"
RESTORE_BYTES = 473_152
RESTORE_SHA256 = "0c40aeb6c95ade9940aa21065cbc73a72dcd82e96ade9d13126693147feb5741"
APPLICATION_OFFSET = 0x10000
FACTORY_SLOT_BYTES = 0x500000 - APPLICATION_OFFSET
BAUD = 115_200
JOURNAL_SCHEMA = "OT143MCJ0"
RECEIPT_SCHEMA = "OT143MCR0"
HASH64 = re.compile(r"^[0-9a-f]{64}$")

_PROTOCOL_PATH = ROOT / "tools" / "ot135_monocypher_protocol_runner.py"
_PROTOCOL_SPEC = importlib.util.spec_from_file_location(
    "_ot143_ot135_protocol", _PROTOCOL_PATH
)
if _PROTOCOL_SPEC is None or _PROTOCOL_SPEC.loader is None:
    raise RuntimeError("protocol_contract_unavailable")
protocol = importlib.util.module_from_spec(_PROTOCOL_SPEC)
sys.modules[_PROTOCOL_SPEC.name] = protocol
_PROTOCOL_SPEC.loader.exec_module(protocol)


class FailureCode(str, enum.Enum):
    INVALID_CONFIGURATION = "invalid_configuration"
    AUTHORITY_REJECTED = "authority_rejected"
    ARTIFACT_INVALID = "artifact_invalid"
    AUTHORITY_ALREADY_CONSUMED = "authority_already_consumed"
    PREFLIGHT_FAILED = "preflight_failed"
    JOURNAL_FAILED = "journal_failed"
    BENCHMARK_WRITE_FAILED = "benchmark_write_failed"
    BENCHMARK_VERIFY_FAILED = "benchmark_verify_failed"
    CAPTURE_FAILED = "capture_failed"
    RESTORE_FAILED = "restore_failed"
    RECEIPT_FAILED = "receipt_failed"
    RECOVERY_JOURNAL_ABSENT = "recovery_journal_absent"
    RECOVERY_NOT_REQUIRED = "recovery_not_required"


class CoordinatorError(RuntimeError):
    """Closed, privacy-safe coordinator failure."""

    def __init__(
        self,
        code: FailureCode,
        *,
        capture_code: str | None = None,
    ) -> None:
        super().__init__(code.value)
        self.code = code
        self.capture_code = capture_code


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
    protocol_start: str
    protocol_ready: str
    expected_frame_count: int


@dataclass(frozen=True)
class AuthorityGrant:
    raw_sha256: str
    attempt_count: int
    reusable: bool
    radio_allowed: bool


@dataclass(frozen=True, repr=False)
class RunConfig:
    private_endpoints: tuple[object, object]
    benchmark_path: Path
    restore_path: Path


class FlashTransport(Protocol):
    """Application-slot-only mutation boundary supplied by a later runner."""

    def write_application(
        self, private_endpoint: object, offset: int, image: Image
    ) -> None: ...

    def verify_application(
        self, private_endpoint: object, offset: int, image: Image
    ) -> None: ...

    def hard_reset(self, private_endpoint: object) -> None: ...


class DeviceBackend(FlashTransport, protocol.Provider, Protocol):
    """One backend binds application mutation and capture to the same endpoints."""


class AuthorityGate(Protocol):
    """A later immutable authority implementation must satisfy this seam."""

    def validate(
        self, binding: ExecutionBinding, *, recovery: bool
    ) -> AuthorityGrant: ...


@dataclass(frozen=True)
class _Failure:
    code: FailureCode
    capture_code: str | None = None
    capture_diagnostics: dict[str, object] | None = None


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _canonical_bytes(value: Any) -> bytes:
    return json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=True,
        allow_nan=False,
    ).encode("ascii")


def _attempt(operation: Callable[[], Any]) -> tuple[bool, Any | None]:
    try:
        return True, operation()
    except BaseException:
        return False, None


def _capture_attempt(
    operation: Callable[[], object],
) -> tuple[object | None, _Failure | None]:
    try:
        return operation(), None
    except protocol.CaptureError as error:
        return None, _Failure(
            FailureCode.CAPTURE_FAILED,
            capture_code=error.code.value,
            capture_diagnostics=_capture_diagnostics(error.diagnostics),
        )
    except BaseException:
        return None, _Failure(FailureCode.CAPTURE_FAILED)


def _binding() -> ExecutionBinding:
    return ExecutionBinding(
        benchmark_name=BENCHMARK_NAME,
        benchmark_bytes=BENCHMARK_BYTES,
        benchmark_sha256=BENCHMARK_SHA256,
        restore_name=RESTORE_NAME,
        restore_bytes=RESTORE_BYTES,
        restore_sha256=RESTORE_SHA256,
        application_offset=APPLICATION_OFFSET,
        baud=BAUD,
        protocol_start=protocol.START.decode("ascii"),
        protocol_ready=protocol.READY.decode("ascii"),
        expected_frame_count=protocol.frame_contract.EXPECTED_FRAME_COUNT,
    )


def _valid_grant(value: object) -> bool:
    return (
        isinstance(value, AuthorityGrant)
        and HASH64.fullmatch(value.raw_sha256) is not None
        and type(value.attempt_count) is int
        and value.attempt_count == 1
        and value.reusable is False
        and value.radio_allowed is False
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
        JOURNAL_PATH: "ot143-monocypher-execution-journal.json",
        EXECUTION_RECEIPT_PATH: "ot143-monocypher-execution-receipt.json",
        RECOVERY_RECEIPT_PATH: "ot143-monocypher-recovery-receipt.json",
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
    if not isinstance(config, RunConfig) or not _private_paths_valid():
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
        or not HASH64.fullmatch(expected_sha256)
        or expected_bytes <= 0
        or expected_bytes > FACTORY_SLOT_BYTES
    ):
        return None
    ok, payload_value = _attempt(path.read_bytes)
    if not ok or not isinstance(payload_value, bytes):
        return None
    payload = payload_value
    if len(payload) != expected_bytes or _sha256(payload) != expected_sha256:
        return None
    return Image(expected_name, payload, expected_sha256)


def _node_state() -> dict[str, bool]:
    return {
        "installed_app_readback_verified": False,
        "preflight_reset_completed": False,
        "benchmark_write_started": False,
        "benchmark_readback_verified": False,
        "capture_validated": False,
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
        "radio_used": False,
        "nodes": {"A": _node_state(), "B": _node_state()},
    }


def _journal_valid(
    value: object, binding: ExecutionBinding, grant: AuthorityGrant
) -> bool:
    if type(value) is not dict or set(value) != {
        "schema", "version", "state", "authority_raw_sha256", "binding",
        "radio_used", "nodes",
    }:
        return False
    if (
        value["schema"] != JOURNAL_SCHEMA
        or value["version"] != 0
        or value["state"] not in {"started", "restored", "aborted"}
        or value["authority_raw_sha256"] != grant.raw_sha256
        or value["binding"] != dataclasses.asdict(binding)
        or value["radio_used"] is not False
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
            state["installed_app_readback_verified"]
            and state["preflight_reset_completed"]
        ):
            return False
        if state["benchmark_readback_verified"] and not state["benchmark_write_started"]:
            return False
        if state["capture_validated"] and not state["benchmark_readback_verified"]:
            return False
        if state["restore_readback_verified"] and not state["restore_write_started"]:
            return False
        if state["restore_reset_completed"] and not state["restore_readback_verified"]:
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


def _load_journal(
    binding: ExecutionBinding, grant: AuthorityGrant
) -> dict[str, Any] | None:
    ok, raw_value = _attempt(JOURNAL_PATH.read_bytes)
    if not ok or not isinstance(raw_value, bytes):
        return None
    raw = raw_value
    if not raw or len(raw) > 65_536 or not raw.endswith(b"\n"):
        return None
    try:
        value = json.loads(raw.decode("ascii"))
    except (UnicodeError, json.JSONDecodeError):
        return None
    if _canonical_bytes(value) + b"\n" != raw:
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
    config: RunConfig,
    transport: FlashTransport,
    restore: Image,
    journal: dict[str, Any],
) -> bool:
    complete = True
    for label, endpoint in zip(("A", "B"), config.private_endpoints):
        state = journal["nodes"][label]
        if not state["benchmark_write_started"] or state["restore_reset_completed"]:
            continue
        state["restore_write_started"] = True
        if not _persist(journal):
            complete = False
        write_ok, _ = _attempt(
            lambda endpoint=endpoint: transport.write_application(
                endpoint, APPLICATION_OFFSET, restore
            )
        )
        if not write_ok:
            complete = False
            continue
        verify_ok, _ = _attempt(
            lambda endpoint=endpoint: transport.verify_application(
                endpoint, APPLICATION_OFFSET, restore
            )
        )
        if not verify_ok:
            complete = False
            continue
        state["restore_readback_verified"] = True
        if not _persist(journal):
            complete = False
            continue
        reset_ok, _ = _attempt(lambda endpoint=endpoint: transport.hard_reset(endpoint))
        if not reset_ok:
            complete = False
            continue
        state["restore_reset_completed"] = True
        if not _persist(journal):
            complete = False
    return complete and _restoration_complete(journal)


def _preflight(
    config: RunConfig, transport: FlashTransport, restore: Image
) -> bool:
    results: list[bool] = []
    for endpoint in config.private_endpoints:
        ok, _ = _attempt(
            lambda endpoint=endpoint: transport.verify_application(
                endpoint, APPLICATION_OFFSET, restore
            )
        )
        results.append(ok)
    for endpoint in config.private_endpoints:
        ok, _ = _attempt(lambda endpoint=endpoint: transport.hard_reset(endpoint))
        results.append(ok)
    return all(results)


def _capture_diagnostics(value: object) -> dict[str, object] | None:
    if not dataclasses.is_dataclass(value):
        return None
    raw = dataclasses.asdict(value)
    allowed = set(protocol.CaptureDiagnostics.__dataclass_fields__)
    if set(raw) != allowed:
        return None
    if raw["lifecycle"] not in {"unverified", "reenumerated", "stable_continuous"}:
        return None
    for key in allowed - {"lifecycle"}:
        item = raw[key]
        if type(item) is not int or item < 0 or item > 10_000_000:
            return None
    return raw


def _receipt(
    journal: dict[str, Any],
    result: str,
    restoration_complete: bool,
    captures: dict[str, dict[str, object]],
    diagnostics: dict[str, dict[str, object]],
    failure: _Failure | None,
) -> dict[str, Any]:
    nodes: list[dict[str, object]] = []
    for label in ("A", "B"):
        state = journal["nodes"][label]
        entry: dict[str, object] = {
            "node": label,
            "installed_app_readback_verified": state["installed_app_readback_verified"],
            "preflight_reset_completed": state["preflight_reset_completed"],
            "benchmark_readback_verified": state["benchmark_readback_verified"],
            "capture_validated": state["capture_validated"],
            "restore_readback_verified": state["restore_readback_verified"],
            "restore_reset_completed": state["restore_reset_completed"],
        }
        if label in captures:
            entry["result"] = captures[label]
            entry["result_sha256"] = _sha256(_canonical_bytes(captures[label]))
            entry["capture_diagnostics"] = diagnostics[label]
        nodes.append(entry)
    failure_value: dict[str, object] | None = None
    if failure is not None:
        failure_value = {"code": failure.code.value}
        if failure.capture_code is not None:
            failure_value["capture_code"] = failure.capture_code
        if failure.capture_diagnostics is not None:
            failure_value["capture_diagnostics"] = failure.capture_diagnostics
    return {
        "schema": RECEIPT_SCHEMA,
        "version": 0,
        "result": result,
        "authority_raw_sha256": journal["authority_raw_sha256"],
        "binding": journal["binding"],
        "node_count": 2,
        "restoration_complete": restoration_complete,
        "nodes": nodes,
        "failure": failure_value,
        "privacy": {
            "private_endpoints_recorded": False,
            "device_identifiers_recorded": False,
            "filesystem_paths_recorded": False,
            "raw_capture_recorded": False,
            "backend_error_text_recorded": False,
        },
        "claims": {
            "phase_two_complete": False,
            "radio_used": False,
            "candidate_selected": False,
            "suite_selected": False,
            "supported_target_proven": False,
            "regulatory_acceptance_proven": False,
            "score_credit_added": False,
        },
    }


def _raise(failure: _Failure) -> None:
    raise CoordinatorError(
        failure.code, capture_code=failure.capture_code
    ) from None


def _prepare(
    config: RunConfig,
    authority: AuthorityGate,
    *,
    recovery: bool,
) -> tuple[ExecutionBinding, AuthorityGrant, Image, Image]:
    if not _config_valid(config, recovery=recovery):
        _raise(_Failure(FailureCode.INVALID_CONFIGURATION))
    binding = _binding()
    grant_ok, grant_value = _attempt(
        lambda: authority.validate(binding, recovery=recovery)
    )
    if not grant_ok or not _valid_grant(grant_value):
        _raise(_Failure(FailureCode.AUTHORITY_REJECTED))
    grant = grant_value
    benchmark = _read_exact_image(
        config.benchmark_path, BENCHMARK_NAME, BENCHMARK_BYTES, BENCHMARK_SHA256
    )
    restore = _read_exact_image(
        config.restore_path, RESTORE_NAME, RESTORE_BYTES, RESTORE_SHA256
    )
    if benchmark is None or restore is None:
        _raise(_Failure(FailureCode.ARTIFACT_INVALID))
    return binding, grant, benchmark, restore


def _prepare_recovery(
    config: RunConfig,
    authority: AuthorityGate,
) -> tuple[ExecutionBinding, AuthorityGrant, Image]:
    if not _config_valid(config, recovery=True):
        _raise(_Failure(FailureCode.INVALID_CONFIGURATION))
    binding = _binding()
    grant_ok, grant_value = _attempt(
        lambda: authority.validate(binding, recovery=True)
    )
    if not grant_ok or not _valid_grant(grant_value):
        _raise(_Failure(FailureCode.AUTHORITY_REJECTED))
    restore = _read_exact_image(
        config.restore_path, RESTORE_NAME, RESTORE_BYTES, RESTORE_SHA256
    )
    if restore is None:
        _raise(_Failure(FailureCode.ARTIFACT_INVALID))
    return binding, grant_value, restore


def execute(
    config: RunConfig,
    backend: DeviceBackend,
    authority: AuthorityGate,
) -> dict[str, Any]:
    """Execute one bounded two-node run; tests inject every external boundary."""
    binding, grant, benchmark, restore = _prepare(config, authority, recovery=False)
    if JOURNAL_PATH.exists():
        _raise(_Failure(FailureCode.AUTHORITY_ALREADY_CONSUMED))
    if not _preflight(config, backend, restore):
        _raise(_Failure(FailureCode.PREFLIGHT_FAILED))

    journal = _new_journal(binding, grant)
    for label in ("A", "B"):
        journal["nodes"][label]["installed_app_readback_verified"] = True
        journal["nodes"][label]["preflight_reset_completed"] = True
    if not _write_new(JOURNAL_PATH, journal):
        _raise(_Failure(FailureCode.JOURNAL_FAILED))

    captures: dict[str, dict[str, object]] = {}
    diagnostics: dict[str, dict[str, object]] = {}
    failure: _Failure | None = None

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

        capture_value, capture_failure = _capture_attempt(
            lambda endpoint=endpoint: protocol.capture_local_primitives(
                backend, endpoint
            )
        )
        if capture_failure is not None:
            failure = capture_failure
            break
        if not isinstance(capture_value, protocol.CaptureResult):
            failure = _Failure(FailureCode.CAPTURE_FAILED)
            break
        if type(capture_value.parsed) is not dict:
            failure = _Failure(FailureCode.CAPTURE_FAILED)
            break
        safe_diagnostics = _capture_diagnostics(capture_value.diagnostics)
        if safe_diagnostics is None:
            failure = _Failure(FailureCode.CAPTURE_FAILED)
            break
        captures[label] = capture_value.parsed
        diagnostics[label] = safe_diagnostics
        state["capture_validated"] = True
        if not _persist(journal):
            failure = _Failure(FailureCode.JOURNAL_FAILED)
            break
        if not _restore_touched(config, backend, restore, journal):
            failure = _Failure(FailureCode.RESTORE_FAILED)
            break

    restored = _restore_touched(config, backend, restore, journal)
    success = failure is None and restored and len(captures) == 2
    if failure is None and not restored:
        failure = _Failure(FailureCode.RESTORE_FAILED)
    journal["state"] = "restored" if success else "aborted"
    if not _persist(journal) and failure is None:
        failure = _Failure(FailureCode.JOURNAL_FAILED)
        success = False

    receipt = _receipt(
        journal,
        "two_node_monocypher_passed_and_restored" if success
        else "monocypher_execution_aborted",
        restored,
        captures,
        diagnostics,
        failure,
    )
    if not _write_new(EXECUTION_RECEIPT_PATH, receipt):
        _raise(_Failure(FailureCode.RECEIPT_FAILED))
    if not success:
        _raise(failure or _Failure(FailureCode.RESTORE_FAILED))
    return receipt


def recover(
    config: RunConfig,
    backend: DeviceBackend,
    authority: AuthorityGate,
) -> dict[str, Any]:
    """Restore benchmark-touched nodes only; never capture or benchmark-write."""
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
    failure = None if restored and persist_ok else _Failure(FailureCode.RESTORE_FAILED)
    if failure is not None:
        # Keep the sole terminal recovery receipt path free so restoration-only
        # recovery can be retried after a transient backend failure.
        _raise(failure)
    receipt = _receipt(
        journal,
        "recovery_only_restored",
        True,
        {},
        {},
        None,
    )
    if not _write_new(RECOVERY_RECEIPT_PATH, receipt):
        _raise(_Failure(FailureCode.RECEIPT_FAILED))
    return receipt





