#!/usr/bin/env python3
"""Fail-closed OT-121 two-node local-primitive execution coordinator.

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
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Protocol

import crypto_benchmark_execution_authority as authority_contract
import ot121_local_primitive_frames as frame_contract


ROOT = Path(__file__).resolve().parents[1]
AUTHORITY_PATH = (
    ROOT / "tests" / "benchmarks" / "crypto"
    / "OT-121-OT005-PHASE-TWO-EXECUTION-AUTHORITY-V1.json"
)
AUTHORITY_RAW_SHA256 = "765aacd8a33862265b46da2d60333759cd96b72a8acae9e70b22c5bda2dbd90f"
AUTHORITY_CANONICAL_SHA256 = "a2e9bbea78282c3a0451654f39c0be49c875217933ef02b7bc384860f32f3105"
RESTORE_NAME = "opentrail_heltec_v4_bench.bin"
RESTORE_BYTES = 473_152
RESTORE_SHA256 = "0c40aeb6c95ade9940aa21065cbc73a72dcd82e96ade9d13126693147feb5741"
BENCHMARK_NAME = "ot121_libsodium_primitive_bench.bin"
APPLICATION_OFFSET = 0x10000
FLASH_BYTES = 16_777_216
FACTORY_SLOT_BYTES = 0x500000 - APPLICATION_OFFSET
ESPTOOL_VERSION = "5.3.1"
MAX_STARTUP_SYNC_BYTES = 4096
MAX_PACKED_FRAMES_PER_LINE = 8
MAX_PACKED_LINE_BYTES = MAX_PACKED_FRAMES_PER_LINE * frame_contract.MAX_FRAME_BYTES
SERIAL_READ_TIMEOUT_SECONDS = 0.25
CAPTURE_RESET_ASSERT_SECONDS = 0.10
CAPTURE_BOOT_CHATTER_SECONDS = 0.25
CAPTURE_REOPEN_ATTEMPTS = 3
CAPTURE_REOPEN_DELAY_SECONDS = 0.50
JOURNAL_SCHEMA = "OT121LPRJ1"
RECEIPT_SCHEMA = "OT121LPER1"
HASH64 = re.compile(r"^[0-9a-f]{64}$")
NONCE = re.compile(r"^[0-9a-f]{32}$")


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
    journal_path: Path
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


def _node_state() -> dict[str, Any]:
    return {
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
        except Exception:
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
        "artifact_kind": "ot121_local_primitive_execution_receipt",
        "result": result,
        "run_nonce": journal["run_nonce"],
        "authority_raw_sha256": AUTHORITY_RAW_SHA256,
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
            "local_primitives_executed": result == "two_node_local_primitives_passed_and_restored",
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
        or not config.journal_path.is_absolute()
        or not config.receipt_path.is_absolute()
        or config.journal_path == config.receipt_path
        or not config.journal_path.parent.is_dir()
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
    benchmark = _read_exact_image(
        config.benchmark_path, BENCHMARK_NAME, config.benchmark_sha256
    )
    restore = _read_exact_image(
        config.restore_path, RESTORE_NAME, RESTORE_SHA256, RESTORE_BYTES
    )

    captures: dict[str, dict[str, Any]] = {}
    capture_hashes: dict[str, str] = {}
    if config.recover:
        if not config.journal_path.exists():
            raise RunnerError("recovery journal absent")
        journal = _load_journal(config.journal_path)
        if (
            journal["benchmark_sha256"] != benchmark.sha256
            or journal["benchmark_bytes"] != benchmark.size
            or journal["state"] not in {"started", "aborted"}
            or _restoration_complete(journal)
        ):
            raise RunnerError("authority already consumed")
    else:
        if config.journal_path.exists():
            raise RunnerError("authority already consumed")
        journal = _new_journal(benchmark, secrets.token_hex(16))
        _create_journal(config.journal_path, journal)

    def persist() -> None:
        _atomic_replace(config.journal_path, journal)

    if config.recover:
        restored = _restore_touched(config, transport, restore, journal, persist)
        journal["state"] = "aborted"
        persist()
        receipt = _receipt(
            journal, "recovery_only_restored" if restored else "recovery_failed",
            restored, captures, capture_hashes,
        )
        _write_new_receipt(config.receipt_path, receipt)
        if not restored:
            raise RunnerError("recovery failed")
        return receipt

    primary_error: BaseException | None = None
    try:
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
            journal, "two_node_local_primitives_passed_and_restored", True,
            captures, capture_hashes,
        )
        _write_new_receipt(config.receipt_path, receipt)
        return receipt

    journal["state"] = "aborted"
    persist()
    receipt = _receipt(
        journal, "local_primitive_execution_aborted", restored,
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
        with tempfile.TemporaryDirectory(prefix="ot121-app-") as directory:
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
        with tempfile.TemporaryDirectory(prefix="ot121-verify-") as directory:
            image_path = Path(directory) / "application.bin"
            image_path.write_bytes(image.payload)
            self._esptool(
                private_port, ["verify-flash", "0x10000", str(image_path)]
            )

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

    def capture_local_primitives(
        self, private_port: str, baud: int, timeout_seconds: float
    ) -> bytes:
        endpoint: Any | None = None
        try:
            import serial  # type: ignore
            deadline = time.monotonic() + timeout_seconds
            frames: list[bytes] = []
            endpoint = self._open_capture_endpoint(serial, private_port, baud)
            endpoint.reset_input_buffer()
            endpoint.rts = True
            inactive_dtr = endpoint.dtr
            if inactive_dtr is not False:
                raise RunnerError("serial control state mismatch")
            endpoint.dtr = inactive_dtr
            time.sleep(CAPTURE_RESET_ASSERT_SECONDS)
            endpoint.rts = False
            inactive_dtr = endpoint.dtr
            if inactive_dtr is not False:
                raise RunnerError("serial control state mismatch")
            endpoint.dtr = inactive_dtr
            time.sleep(CAPTURE_BOOT_CHATTER_SECONDS)
            while time.monotonic() < deadline and len(frames) < frame_contract.EXPECTED_FRAME_COUNT:
                try:
                    line = endpoint.readline(MAX_PACKED_LINE_BYTES + 1)
                except Exception:
                    if frames:
                        raise RunnerError("serial capture failed")
                    self._close_capture_endpoint(endpoint)
                    endpoint = None
                    for _ in range(CAPTURE_REOPEN_ATTEMPTS):
                        time.sleep(CAPTURE_REOPEN_DELAY_SECONDS)
                        if time.monotonic() >= deadline:
                            break
                        try:
                            endpoint = self._open_capture_endpoint(
                                serial, private_port, baud
                            )
                            break
                        except Exception:
                            endpoint = None
                    if endpoint is None:
                        raise RunnerError("serial capture failed")
                    continue
                if not line:
                    continue
                extracted = _capture_frames_from_line(line)
                if len(frames) + len(extracted) > frame_contract.EXPECTED_FRAME_COUNT:
                    raise RunnerError("serial capture frame count exceeded")
                frames.extend(extracted)
            if len(frames) != frame_contract.EXPECTED_FRAME_COUNT:
                raise RunnerError("serial capture incomplete")
            return b"\n".join(frames) + b"\n"
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
    parser.add_argument("--benchmark-sha256", required=True)
    parser.add_argument("--restore-app", type=Path, required=True)
    parser.add_argument("--private-journal", type=Path, required=True)
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
            benchmark_sha256=args.benchmark_sha256,
            restore_path=args.restore_app,
            journal_path=args.private_journal,
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
