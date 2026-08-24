#!/usr/bin/env python3
"""Fail-closed OT-125 two-node Monocypher corrective-retry coordinator.

Private serial-port values are accepted only as command-line inputs and are
never written to the journal, receipt, stdout, stderr, or child-process output.
Only an application image at the admitted factory slot is writable.  Every
touched node is restored to the exact OT-115 application, including after a
benchmark or capture failure.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import os
import re
import secrets
import stat
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Protocol

import ot125_monocypher_retry_authority as authority_contract
import ot123_monocypher_frames as frame_contract


ROOT = Path(__file__).resolve().parents[1]
AUTHORITY_PATH = (
    ROOT / "tests" / "benchmarks" / "crypto"
    / "OT-125-OT005-MONOCYPHER-CORRECTIVE-RETRY-AUTHORITY-V0.json"
)
AUTHORITY_RAW_SHA256 = authority_contract.AUTHORITY_RAW_SHA256
AUTHORITY_CANONICAL_SHA256 = authority_contract.AUTHORITY_CANONICAL_SHA256
CONTINUATION_PARENT_PATH = (
    ROOT / "tests" / "benchmarks" / "crypto"
    / "OT-124-OT005-MONOCYPHER-COMPARISON-EXECUTION-ABORT-RECEIPT-V0.json"
)
CONTINUATION_PARENT_RAW_SHA256 = "f638a6125a14d8fe28412ae0554c6958bdad3a6c2a0a1e83e3ae793bcad4e92c"
CONTINUATION_PARENT = {
    "path": "tests/benchmarks/crypto/OT-124-OT005-MONOCYPHER-COMPARISON-EXECUTION-ABORT-RECEIPT-V0.json",
    "raw_sha256": CONTINUATION_PARENT_RAW_SHA256,
}
JOURNAL_PATH = ROOT / ".private" / "ot125-monocypher-corrective-retry-journal.json"
EXECUTION_RECEIPT_PATH = (
    ROOT / ".private" / "ot125-monocypher-corrective-retry-execution-receipt.json"
)
RECOVERY_RECEIPT_PATH = (
    ROOT / ".private" / "ot125-monocypher-corrective-retry-recovery-receipt.json"
)
RESTORE_NAME = "opentrail_heltec_v4_bench.bin"
RESTORE_BYTES = 473_152
RESTORE_SHA256 = "0c40aeb6c95ade9940aa21065cbc73a72dcd82e96ade9d13126693147feb5741"
BENCHMARK_NAME = "ot123_monocypher_candidate_bench.bin"
BENCHMARK_BYTES = 186_640
BENCHMARK_SHA256 = "5e075fb791a658546fca714fc60de095ecbf14f7c443f414d3ac8642965a3b64"
APPLICATION_OFFSET = 0x10000
FLASH_BYTES = 16_777_216
FACTORY_SLOT_BYTES = 0x500000 - APPLICATION_OFFSET
ESPTOOL_VERSION = "5.3.1"
MAX_STARTUP_SYNC_BYTES = 4096
MAX_PACKED_FRAMES_PER_LINE = 8
MAX_PACKED_LINE_BYTES = MAX_PACKED_FRAMES_PER_LINE * frame_contract.MAX_FRAME_BYTES
SERIAL_READ_TIMEOUT_SECONDS = 0.25
CAPTURE_REENUMERATION_SECONDS = 1.50
CAPTURE_OPEN_ATTEMPTS = 5
CAPTURE_OPEN_RETRY_DELAY_SECONDS = 0.25
CAPTURE_EMPTY_READ_LIMIT = 8
CAPTURE_CYCLE_ATTEMPTS = 2
FIRMWARE_STARTUP_DELAY_SECONDS = 3.0
JOURNAL_SCHEMA = "OT125MCRJ0"
RECEIPT_SCHEMA = "OT125MCER0"
HASH64 = re.compile(r"^[0-9a-f]{64}$")
NONCE = re.compile(r"^[0-9a-f]{32}$")
COMPARISON_BOUNDARY = {
    "candidate_id": "monocypher",
    "candidate_role": "comparison",
    "selection_eligible": False,
    "operations": [
        "ed25519_sign",
        "ed25519_verify",
        "x25519",
        "chacha20poly1305_encrypt",
        "chacha20poly1305_decrypt",
    ],
    "unavailable_operations": [
        "sha256",
        "hkdf_sha256",
        "noise_xk_handshake",
    ],
    "operations_required": 5,
    "operations_total": 8,
    "partial_candidate_cannot_pass_selection_gate": True,
}


class RunnerError(RuntimeError):
    """A deliberately detail-free coordinator failure."""


class ArgumentError(RunnerError):
    """A sanitized command-line failure."""


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        del message
        raise ArgumentError("invalid arguments")


@dataclass(frozen=True)
class Image:
    name: str
    payload: bytes
    sha256: str

    @property
    def size(self) -> int:
        return len(self.payload)


@dataclass(frozen=True)
class RunConfig:
    private_ports: tuple[str, str]
    authority_path: Path
    authority_sha256: str
    benchmark_path: Path
    benchmark_sha256: str
    restore_path: Path
    receipt_path: Path
    baud: int = 115_200
    flash_baud: int = 115_200
    capture_timeout_seconds: float = 180.0
    recover: bool = False


class Transport(Protocol):
    """Narrow dependency-injection boundary; it cannot express broad writes."""

    def write_application(self, private_port: str, offset: int, image: Image) -> None: ...
    def verify_application(self, private_port: str, offset: int, image: Image) -> None: ...
    def hard_reset(self, private_port: str) -> None: ...
    def capture_local_primitives(
        self, private_port: str, baud: int, timeout_seconds: float
    ) -> bytes: ...


def _canonical_bytes(value: Any) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True,
        allow_nan=False,
    ).encode("ascii")


def _unique_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise RunnerError("duplicate key")
        value[key] = item
    return value


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _read_exact_image(
    path: Path, expected_name: str, expected_sha256: str,
    expected_size: int | None = None,
) -> Image:
    if not path.is_absolute() or path.name != expected_name or path.is_symlink():
        raise RunnerError("image identity mismatch")
    try:
        payload = path.read_bytes()
    except OSError as exc:
        raise RunnerError("image unavailable") from exc
    if not payload or len(payload) > FACTORY_SLOT_BYTES:
        raise RunnerError("image size mismatch")
    if expected_size is not None and len(payload) != expected_size:
        raise RunnerError("image size mismatch")
    if not HASH64.fullmatch(expected_sha256) or _sha256(payload) != expected_sha256:
        raise RunnerError("image digest mismatch")
    return Image(expected_name, payload, expected_sha256)


def _validate_authority(path: Path, supplied_hash: str) -> None:
    if (
        not path.is_absolute()
        or path.resolve() != AUTHORITY_PATH.resolve()
        or supplied_hash != AUTHORITY_RAW_SHA256
        or authority_contract.AUTHORITY_PIN
        != (AUTHORITY_RAW_SHA256, AUTHORITY_CANONICAL_SHA256)
    ):
        raise RunnerError("authority identity mismatch")
    parents = authority_contract.validate_parent_files()
    value = authority_contract.load(path, authority_contract.AUTHORITY_PIN)
    result = authority_contract.validate_authority(value, parents)
    if (
        _sha256(path.read_bytes()) != AUTHORITY_RAW_SHA256
        or result.get("canonical_sha256") != AUTHORITY_CANONICAL_SHA256
        or result.get("phase_two_execution_authorized") is not True
        or result.get("benchmark_executed") is not False
    ):
        raise RunnerError("authority validation failed")


def _reject_json_constant(_: str) -> None:
    raise RunnerError("continuation parent value mismatch")


def _validate_continuation_parent() -> None:
    path = CONTINUATION_PARENT_PATH
    expected = (
        ROOT / "tests" / "benchmarks" / "crypto"
        / "OT-124-OT005-MONOCYPHER-COMPARISON-EXECUTION-ABORT-RECEIPT-V0.json"
    )
    if (
        not path.is_absolute()
        or path.resolve() != expected.resolve()
        or path.is_symlink()
    ):
        raise RunnerError("continuation parent identity mismatch")
    try:
        raw = path.read_bytes()
        value = json.loads(
            raw.decode("ascii"), object_pairs_hook=_unique_pairs,
            parse_constant=_reject_json_constant,
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise RunnerError("continuation parent unavailable") from exc
    if (
        _sha256(raw) != CONTINUATION_PARENT_RAW_SHA256
        or type(value) is not dict
        or value.get("schema") != "OTMCAR0"
        or value.get("version") != 0
        or value.get("artifact_kind") != "monocypher_comparison_execution_abort_receipt"
        or value.get("result")
        != "monocypher_comparison_execution_aborted_all_touched_nodes_restored"
        or value.get("restoration_complete") is not True
        or value.get("node_count") != 2
        or value.get("touched_node_count") != 1
        or value.get("authority_raw_sha256")
        != authority_contract.CONSUMED_AUTHORITY_RAW_SHA256
        or type(value.get("authority")) is not dict
        or value["authority"].get("consumed_by_abort") is not True
        or value["authority"].get("continuing_authority") is not False
        or value["authority"].get("reusable") is not False
        or type(value.get("claims")) is not dict
        or value["claims"].get("benchmark_result_admitted") is not False
        or value["claims"].get("phase_two_complete") is not False
        or value["claims"].get("radio_used") is not False
        or value["claims"].get("candidate_selected") is not False
        or value["claims"].get("suite_selected") is not False
    ):
        raise RunnerError("continuation parent validation failed")


def _node_state() -> dict[str, Any]:
    return {
        "installed_app_readback_verified": False,
        "benchmark_write_started": False,
        "benchmark_readback_verified": False,
        "capture_validated": False,
        "restore_write_started": False,
        "restore_readback_verified": False,
        "restore_reset_completed": False,
    }


def _new_journal(benchmark: Image, nonce: str) -> dict[str, Any]:
    return {
        "schema": JOURNAL_SCHEMA,
        "version": 1,
        "state": "started",
        "run_nonce": nonce,
        "authority_raw_sha256": AUTHORITY_RAW_SHA256,
        "continuation_parent": CONTINUATION_PARENT,
        "comparison_boundary": COMPARISON_BOUNDARY,
        "benchmark_sha256": benchmark.sha256,
        "benchmark_bytes": benchmark.size,
        "restore_sha256": RESTORE_SHA256,
        "restore_bytes": RESTORE_BYTES,
        "application_offset": APPLICATION_OFFSET,
        "radio_used": False,
        "nodes": {"A": _node_state(), "B": _node_state()},
    }


def _validate_journal(value: Any) -> dict[str, Any]:
    expected_top = {
        "schema", "version", "state", "run_nonce", "authority_raw_sha256",
        "continuation_parent", "comparison_boundary",
        "benchmark_sha256", "benchmark_bytes", "restore_sha256", "restore_bytes",
        "application_offset", "radio_used", "nodes",
    }
    if type(value) is not dict or set(value) != expected_top:
        raise RunnerError("journal shape mismatch")
    if (
        value["schema"] != JOURNAL_SCHEMA
        or type(value["version"]) is not int or value["version"] != 1
        or value["state"] not in {"started", "restored", "aborted"}
        or type(value["run_nonce"]) is not str or not NONCE.fullmatch(value["run_nonce"])
        or value["authority_raw_sha256"] != AUTHORITY_RAW_SHA256
        or value["continuation_parent"] != CONTINUATION_PARENT
        or value["comparison_boundary"] != COMPARISON_BOUNDARY
        or type(value["benchmark_sha256"]) is not str
        or not HASH64.fullmatch(value["benchmark_sha256"])
        or type(value["benchmark_bytes"]) is not int or value["benchmark_bytes"] <= 0
        or value["restore_sha256"] != RESTORE_SHA256
        or type(value["restore_bytes"]) is not int or value["restore_bytes"] != RESTORE_BYTES
        or type(value["application_offset"]) is not int
        or value["application_offset"] != APPLICATION_OFFSET
        or value["radio_used"] is not False
        or type(value["nodes"]) is not dict or set(value["nodes"]) != {"A", "B"}
    ):
        raise RunnerError("journal identity mismatch")
    expected_node = set(_node_state())
    for node in ("A", "B"):
        state = value["nodes"][node]
        if type(state) is not dict or set(state) != expected_node:
            raise RunnerError("journal node shape mismatch")
        if any(type(state[key]) is not bool for key in expected_node):
            raise RunnerError("journal node value mismatch")
        if state["benchmark_readback_verified"] and not state["benchmark_write_started"]:
            raise RunnerError("journal transition mismatch")
        if state["benchmark_write_started"] and not state["installed_app_readback_verified"]:
            raise RunnerError("journal transition mismatch")
        if state["capture_validated"] and not state["benchmark_readback_verified"]:
            raise RunnerError("journal transition mismatch")
        if state["restore_readback_verified"] and not state["restore_write_started"]:
            raise RunnerError("journal transition mismatch")
        if state["restore_reset_completed"] and not state["restore_readback_verified"]:
            raise RunnerError("journal transition mismatch")
    return value


def _load_journal(path: Path) -> dict[str, Any]:
    try:
        raw = path.read_bytes()
        if not raw or len(raw) > 65_536 or not raw.endswith(b"\n"):
            raise RunnerError("journal framing mismatch")
        value = json.loads(raw.decode("ascii"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise RunnerError("journal unavailable") from exc
    if _canonical_bytes(value) + b"\n" != raw:
        raise RunnerError("journal is not canonical")
    return _validate_journal(value)


def _atomic_replace(path: Path, value: dict[str, Any]) -> None:
    payload = _canonical_bytes(value) + b"\n"
    temporary = path.parent / f".{path.name}.{secrets.token_hex(8)}.tmp"
    try:
        descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except OSError as exc:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass
        raise RunnerError("journal update failed") from exc


def _create_journal(path: Path, value: dict[str, Any]) -> None:
    payload = _canonical_bytes(value) + b"\n"
    try:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
    except OSError as exc:
        raise RunnerError("journal creation failed") from exc


def _write_new_receipt(path: Path, value: dict[str, Any]) -> None:
    payload = _canonical_bytes(value) + b"\n"
    try:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
    except OSError as exc:
        raise RunnerError("receipt write failed") from exc

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



def _restoration_complete(journal: dict[str, Any]) -> bool:
    return all(
        (not journal["nodes"][node]["benchmark_write_started"])
        or journal["nodes"][node]["restore_reset_completed"]
        for node in ("A", "B")
    )


def _restore_touched(
    config: RunConfig, transport: Transport, restore: Image,
    journal: dict[str, Any], persist: Callable[[], None],
) -> bool:
    complete = True
    for node, private_port in zip(("A", "B"), config.private_ports):
        state = journal["nodes"][node]
        if not state["benchmark_write_started"] or state["restore_reset_completed"]:
            continue
        try:
            state["restore_write_started"] = True
            persist()
            transport.write_application(private_port, APPLICATION_OFFSET, restore)
            transport.verify_application(
                private_port, APPLICATION_OFFSET, restore
            )
            state["restore_readback_verified"] = True
            persist()
            transport.hard_reset(private_port)
            state["restore_reset_completed"] = True
            persist()
        except BaseException:
            complete = False
    return complete and _restoration_complete(journal)


def _receipt(
    journal: dict[str, Any], result: str, restoration_complete: bool,
    captures: dict[str, dict[str, Any]], capture_hashes: dict[str, str],
) -> dict[str, Any]:
    nodes = []
    for node in ("A", "B"):
        entry: dict[str, Any] = {
            "node": node,
            "installed_app_readback_verified": journal["nodes"][node]["installed_app_readback_verified"],
            "benchmark_readback_verified": journal["nodes"][node]["benchmark_readback_verified"],
            "capture_validated": journal["nodes"][node]["capture_validated"],
            "restore_readback_verified": journal["nodes"][node]["restore_readback_verified"],
            "restore_reset_completed": journal["nodes"][node]["restore_reset_completed"],
        }
        if node in captures:
            entry["capture_sha256"] = capture_hashes[node]
            entry["local_primitive_result"] = captures[node]
        nodes.append(entry)
    return {
        "schema": RECEIPT_SCHEMA,
        "version": 1,
        "artifact_kind": "ot125_monocypher_corrective_retry_execution_receipt",
        "result": result,
        "run_nonce": journal["run_nonce"],
        "authority_raw_sha256": AUTHORITY_RAW_SHA256,
        "continuation_parent": CONTINUATION_PARENT,
        "comparison_boundary": COMPARISON_BOUNDARY,
        "benchmark_sha256": journal["benchmark_sha256"],
        "benchmark_bytes": journal["benchmark_bytes"],
        "restore_sha256": RESTORE_SHA256,
        "restore_bytes": RESTORE_BYTES,
        "application_offset": APPLICATION_OFFSET,
        "node_count": 2,
        "restoration_complete": restoration_complete,
        "nodes": nodes,
        "privacy": {
            "serial_ports_recorded": False,
            "device_identifiers_recorded": False,
            "filesystem_paths_recorded": False,
            "raw_capture_recorded": False,
        },
        "claims": {
            "monocypher_comparison_primitives_executed": result == "two_node_monocypher_corrective_retry_passed_and_restored",
            "phase_two_complete": False,
            "radio_used": False,
            "candidate_selected": False,
            "suite_selected": False,
            "supported_target_proven": False,
            "regulatory_acceptance_proven": False,
            "score_credit_added": False,
        },
    }


def _preflight_paths(config: RunConfig) -> None:
    if (
        len(config.private_ports) != 2
        or not all(type(value) is str and value for value in config.private_ports)
        or config.private_ports[0] == config.private_ports[1]
        or config.baud != 115_200
        or config.flash_baud != 115_200
        or not 5.0 <= config.capture_timeout_seconds <= 1800.0
        or config.benchmark_sha256 != BENCHMARK_SHA256
        or not config.receipt_path.is_absolute()
        or config.receipt_path.resolve()
        != (RECOVERY_RECEIPT_PATH if config.recover else EXECUTION_RECEIPT_PATH).resolve()
        or not JOURNAL_PATH.is_absolute()
        or JOURNAL_PATH.resolve() != (
            ROOT / ".private" / "ot125-monocypher-corrective-retry-journal.json"
        ).resolve()
        or _has_reparse_or_symlink_ancestry(JOURNAL_PATH.parent, ROOT)
        or not JOURNAL_PATH.parent.is_dir()
        or JOURNAL_PATH == config.receipt_path
        or not config.receipt_path.parent.is_dir()
        or config.receipt_path.exists()
    ):
        raise ArgumentError("invalid arguments")


def execute(
    config: RunConfig, transport: Transport,
    parser: Callable[[bytes], dict[str, Any]] = frame_contract.parse_capture_bytes,
) -> dict[str, Any]:
    """Execute or recover one bounded run. Tests inject a transport with no hardware."""
    _preflight_paths(config)
    _validate_authority(config.authority_path, config.authority_sha256)
    _validate_continuation_parent()
    benchmark = _read_exact_image(
        config.benchmark_path, BENCHMARK_NAME, config.benchmark_sha256
    )
    restore = _read_exact_image(
        config.restore_path, RESTORE_NAME, RESTORE_SHA256, RESTORE_BYTES
    )

    captures: dict[str, dict[str, Any]] = {}
    capture_hashes: dict[str, str] = {}
    if config.recover:
        if not JOURNAL_PATH.exists():
            raise RunnerError("recovery journal absent")
        journal = _load_journal(JOURNAL_PATH)
        if (
            journal["benchmark_sha256"] != benchmark.sha256
            or journal["benchmark_bytes"] != benchmark.size
            or journal["state"] not in {"started", "aborted"}
            or _restoration_complete(journal)
        ):
            raise RunnerError("authority already consumed")
    else:
        if JOURNAL_PATH.exists():
            raise RunnerError("authority already consumed")
        try:
            for private_port in config.private_ports:
                transport.verify_application(
                    private_port, APPLICATION_OFFSET, restore
                )
        except BaseException as exc:
            raise RunnerError("installed application preflight failed") from exc
        journal = _new_journal(benchmark, secrets.token_hex(16))
        for node in ("A", "B"):
            journal["nodes"][node]["installed_app_readback_verified"] = True
        _create_journal(JOURNAL_PATH, journal)

    def persist() -> None:
        _atomic_replace(JOURNAL_PATH, journal)

    if config.recover:
        restored = _restore_touched(config, transport, restore, journal, persist)
        journal["state"] = "aborted"
        persist()
        receipt = _receipt(
            journal, "corrective_retry_recovery_only_restored" if restored else "corrective_retry_recovery_failed",
            restored, captures, capture_hashes,
        )
        _write_new_receipt(config.receipt_path, receipt)
        if not restored:
            raise RunnerError("recovery failed")
        return receipt

    primary_error: BaseException | None = None
    try:
        if not all(
            journal["nodes"][node]["installed_app_readback_verified"]
            for node in ("A", "B")
        ):
            raise RunnerError("installed application preflight failed")
        for node, private_port in zip(("A", "B"), config.private_ports):
            state = journal["nodes"][node]
            state["benchmark_write_started"] = True
            persist()
            transport.write_application(
                private_port, APPLICATION_OFFSET, benchmark
            )
            transport.verify_application(
                private_port, APPLICATION_OFFSET, benchmark
            )
            state["benchmark_readback_verified"] = True
            persist()
            raw_capture = transport.capture_local_primitives(
                private_port, config.baud, config.capture_timeout_seconds
            )
            captures[node] = parser(raw_capture)
            capture_hashes[node] = _sha256(raw_capture)
            state["capture_validated"] = True
            persist()
            if not _restore_touched(config, transport, restore, journal, persist):
                raise RunnerError("restore failed")
    except BaseException as exc:
        primary_error = exc

    restored = _restore_touched(config, transport, restore, journal, persist)
    if primary_error is None and restored and len(captures) == 2:
        journal["state"] = "restored"
        persist()
        receipt = _receipt(
            journal, "two_node_monocypher_corrective_retry_passed_and_restored", True,
            captures, capture_hashes,
        )
        _write_new_receipt(config.receipt_path, receipt)
        return receipt

    journal["state"] = "aborted"
    persist()
    receipt = _receipt(
        journal, "monocypher_corrective_retry_execution_aborted", restored,
        captures, capture_hashes,
    )
    _write_new_receipt(config.receipt_path, receipt)
    raise RunnerError("execution aborted") from primary_error


def _allowed_discarded_serial_bytes(value: bytes) -> None:
    if len(value) > MAX_STARTUP_SYNC_BYTES:
        raise RunnerError("serial preamble too long")
    if any(byte not in (9, 13, 27) and not 32 <= byte <= 126 for byte in value):
        raise RunnerError("serial startup bytes mismatch")


def _frame_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise RunnerError("serial frame duplicate key")
        value[key] = item
    return value


def _capture_frames_from_line(line: bytes) -> list[bytes]:
    if not line.endswith(b"\n") or len(line) > MAX_PACKED_LINE_BYTES:
        raise RunnerError("serial packed line mismatch")
    clean = line.rstrip(b"\r\n")
    first = clean.find(frame_contract.PREFIX)
    if first < 0:
        _allowed_discarded_serial_bytes(clean)
        return []
    _allowed_discarded_serial_bytes(clean[:first])
    frames: list[bytes] = []
    position = first
    decoder = json.JSONDecoder(
        object_pairs_hook=_frame_pairs,
        parse_constant=lambda value: (_ for _ in ()).throw(
            RunnerError("serial frame non-finite value")
        ),
    )
    while position < len(clean):
        if len(frames) >= MAX_PACKED_FRAMES_PER_LINE:
            raise RunnerError("serial packed frame count exceeded")
        payload_start = position + len(frame_contract.PREFIX)
        try:
            suffix = clean[payload_start:].decode("ascii")
            value, consumed = decoder.raw_decode(suffix)
        except (UnicodeError, json.JSONDecodeError) as exc:
            raise RunnerError("serial packed frame malformed") from exc
        if type(value) is not dict:
            raise RunnerError("serial packed frame shape mismatch")
        payload = clean[payload_start:payload_start + consumed]
        canonical_frame = json.dumps(
            value, separators=(",", ":"), ensure_ascii=True, allow_nan=False
        ).encode("ascii")
        if canonical_frame != payload:
            raise RunnerError("serial packed frame noncanonical")
        frame = frame_contract.PREFIX + payload
        if len(frame) > frame_contract.MAX_FRAME_BYTES:
            raise RunnerError("serial packed frame too long")
        frames.append(frame)
        position = payload_start + consumed
        if position == len(clean):
            break
        next_prefix = clean.find(frame_contract.PREFIX, position)
        if next_prefix < 0:
            raise RunnerError("serial packed frame trailing bytes")
        _allowed_discarded_serial_bytes(clean[position:next_prefix])
        position = next_prefix
    return frames


class EsptoolSerialTransport:
    """Production transport constrained to app-only writes at 0x10000."""

    def __init__(self, python_executable: str = sys.executable) -> None:
        if importlib.metadata.version("esptool") != ESPTOOL_VERSION:
            raise RunnerError("tool dependency mismatch")
        try:
            import serial  # type: ignore  # noqa: F401
        except Exception as exc:
            raise RunnerError("tool dependency mismatch") from exc
        self._python = python_executable

    def _esptool(self, private_port: str, operation: list[str]) -> None:
        command = [
            self._python, "-m", "esptool", "--chip", "esp32s3", "--port",
            private_port, "--baud", "115200", "--before", "default-reset",
            "--after", "no-reset", "--no-stub", *operation,
        ]
        try:
            completed = subprocess.run(
                command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, timeout=180, check=False,
            )
        except Exception as exc:
            raise RunnerError("transport process failed") from exc
        if completed.returncode != 0:
            raise RunnerError("transport process failed")

    @staticmethod
    def _require_offset(offset: int) -> None:
        if type(offset) is not int or offset != APPLICATION_OFFSET:
            raise RunnerError("application offset mismatch")

    def write_application(self, private_port: str, offset: int, image: Image) -> None:
        self._require_offset(offset)
        with tempfile.TemporaryDirectory(prefix="ot125-monocypher-app-") as directory:
            image_path = Path(directory) / "application.bin"
            image_path.write_bytes(image.payload)
            self._esptool(
                private_port,
                ["write-flash", "--flash-size", "16MB", "0x10000", str(image_path)],
            )

    def verify_application(self, private_port: str, offset: int, image: Image) -> None:
        self._require_offset(offset)
        if not 0 < image.size <= FACTORY_SLOT_BYTES:
            raise RunnerError("application size mismatch")
        with tempfile.TemporaryDirectory(prefix="ot125-monocypher-readback-") as directory:
            readback_path = Path(directory) / "application.bin"
            self._esptool(
                private_port,
                ["read-flash", "0x10000", str(image.size), str(readback_path)],
            )
            try:
                readback = readback_path.read_bytes()
            except OSError as exc:
                raise RunnerError("application readback failed") from exc
            if len(readback) != image.size or _sha256(readback) != image.sha256:
                raise RunnerError("application readback mismatch")

    def hard_reset(self, private_port: str) -> None:
        command = [
            self._python, "-m", "esptool", "--chip", "esp32s3", "--port",
            private_port, "--baud", "115200", "--before", "default-reset",
            "--after", "hard-reset", "run",
        ]
        try:
            completed = subprocess.run(
                command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, timeout=60, check=False,
            )
        except Exception as exc:
            raise RunnerError("transport process failed") from exc
        if completed.returncode != 0:
            raise RunnerError("transport process failed")

    @staticmethod
    def _close_capture_endpoint(endpoint: Any | None) -> None:
        if endpoint is None:
            return
        for attribute, value in (("rts", False), ("dtr", False)):
            try:
                setattr(endpoint, attribute, value)
            except Exception:
                pass
        try:
            endpoint.close()
        except Exception:
            pass

    @classmethod
    def _open_capture_endpoint(
        cls, serial: Any, private_port: str, baud: int
    ) -> Any:
        endpoint = serial.Serial(
            port=None, baudrate=baud, timeout=SERIAL_READ_TIMEOUT_SECONDS
        )
        try:
            endpoint.dtr = False
            endpoint.rts = False
            endpoint.port = private_port
            endpoint.open()
            return endpoint
        except Exception:
            cls._close_capture_endpoint(endpoint)
            raise

    def _open_capture_cycle(
        self, serial: Any, private_port: str, baud: int
    ) -> Any:
        self.hard_reset(private_port)
        time.sleep(CAPTURE_REENUMERATION_SECONDS)
        for attempt in range(CAPTURE_OPEN_ATTEMPTS):
            try:
                return self._open_capture_endpoint(serial, private_port, baud)
            except Exception:
                if attempt + 1 == CAPTURE_OPEN_ATTEMPTS:
                    raise RunnerError("serial capture failed")
                time.sleep(CAPTURE_OPEN_RETRY_DELAY_SECONDS)
        raise RunnerError("serial capture failed")

    def capture_local_primitives(
        self, private_port: str, baud: int, timeout_seconds: float
    ) -> bytes:
        endpoint: Any | None = None
        frames: list[bytes] = []
        try:
            import serial  # type: ignore
            for cycle in range(CAPTURE_CYCLE_ATTEMPTS):
                try:
                    endpoint = self._open_capture_cycle(
                        serial, private_port, baud
                    )
                except RunnerError:
                    if cycle + 1 == CAPTURE_CYCLE_ATTEMPTS:
                        raise
                    continue

                deadline = time.monotonic() + timeout_seconds
                empty_reads = 0
                retry_fresh_cycle = False
                while (
                    time.monotonic() < deadline
                    and len(frames) < frame_contract.EXPECTED_FRAME_COUNT
                ):
                    try:
                        line = endpoint.readline(MAX_PACKED_LINE_BYTES + 1)
                    except Exception:
                        if frames or cycle + 1 == CAPTURE_CYCLE_ATTEMPTS:
                            raise RunnerError("serial capture failed")
                        retry_fresh_cycle = True
                        break
                    if not line:
                        empty_reads += 1
                        if (
                            not frames
                            and empty_reads >= CAPTURE_EMPTY_READ_LIMIT
                        ):
                            if cycle + 1 == CAPTURE_CYCLE_ATTEMPTS:
                                raise RunnerError("serial capture incomplete")
                            retry_fresh_cycle = True
                            break
                        continue
                    empty_reads = 0
                    extracted = _capture_frames_from_line(line)
                    if len(frames) + len(extracted) > frame_contract.EXPECTED_FRAME_COUNT:
                        raise RunnerError("serial capture frame count exceeded")
                    frames.extend(extracted)

                if len(frames) == frame_contract.EXPECTED_FRAME_COUNT:
                    return b"\n".join(frames) + b"\n"
                if frames or not retry_fresh_cycle:
                    raise RunnerError("serial capture incomplete")
                self._close_capture_endpoint(endpoint)
                endpoint = None
            raise RunnerError("serial capture incomplete")
        except RunnerError:
            raise
        except Exception as exc:
            raise RunnerError("serial capture failed") from exc
        finally:
            self._close_capture_endpoint(endpoint)


def _parser() -> SafeArgumentParser:
    parser = SafeArgumentParser(description=__doc__)
    parser.add_argument("--port-a", required=True)
    parser.add_argument("--port-b", required=True)
    parser.add_argument("--authority", type=Path, required=True)
    parser.add_argument("--authority-sha256", required=True)
    parser.add_argument("--benchmark-app", type=Path, required=True)

    parser.add_argument("--restore-app", type=Path, required=True)

    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("--baud", type=int, default=115_200)
    parser.add_argument("--capture-timeout-seconds", type=float, default=180.0)
    parser.add_argument("--recover", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    try:
        args = _parser().parse_args(argv)
        config = RunConfig(
            private_ports=(args.port_a, args.port_b),
            authority_path=args.authority,
            authority_sha256=args.authority_sha256,
            benchmark_path=args.benchmark_app,
            benchmark_sha256=BENCHMARK_SHA256,
            restore_path=args.restore_app,

            receipt_path=args.receipt,
            baud=args.baud,
            capture_timeout_seconds=args.capture_timeout_seconds,
            recover=args.recover,
        )
        transport = EsptoolSerialTransport()
        execute(config, transport)
        return 0
    except ArgumentError:
        print("ERROR: invalid arguments", file=sys.stderr)
        return 2
    except Exception:
        print("ERROR: execution failed", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
