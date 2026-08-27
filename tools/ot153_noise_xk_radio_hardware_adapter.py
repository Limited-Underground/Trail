#!/usr/bin/env python3
"""Concrete privacy-safe adapter for the future OT-154-authorized OT-153 run.

The adapter pins esptool 5.3.1, exposes only application-slot write/readback and
hard reset, and provides two endpoint-bound serial command channels to the exact
OT-153 runner.  It intentionally lazy-loads the future OT-154 authority tool and
refuses all composition modes until that separately accepted, fixed authority
record exists.  Private CLI values and backend text are never printed.
"""

from __future__ import annotations

import argparse
import importlib.metadata
import importlib.util
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Callable


ROOT = Path(__file__).resolve().parents[1]
ADAPTER_PATH = Path(__file__).resolve()
COORDINATOR_PATH = ROOT / "tools" / "ot153_noise_xk_radio_coordinator.py"
RUNNER_PATH = ROOT / "tools" / "ot153_noise_xk_radio_runner.py"
FUTURE_AUTHORITY_TOOL_PATH = ROOT / "tools" / "ot153_noise_xk_radio_execution_authority.py"
FUTURE_AUTHORITY_PATH = (
    ROOT
    / "tests"
    / "benchmarks"
    / "crypto"
    / "OT-154-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0.json"
)
FUTURE_AUTHORITY_RELATIVE = (
    "tests/benchmarks/crypto/"
    "OT-154-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0.json"
)
ESPTOOL_VERSION = "5.3.1"
SERIAL_TIMEOUT_SECONDS = 0.25
MAX_RECEIPT_LINES = 4_096
MAX_LINE_BYTES = 2_048


def _load_module(name: str, path: Path) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError("runtime_contract_unavailable")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


coordinator = _load_module("_ot153_radio_coordinator", COORDINATOR_PATH)


class AdapterError(RuntimeError):
    """Closed adapter failure with no private diagnostic payload."""


class ArgumentError(AdapterError):
    """Closed CLI-validation failure."""


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        del message
        raise ArgumentError("invalid_arguments")


def _load_authority_contract() -> Any:
    """Fail closed until both future accepted authority artifacts exist."""
    if not FUTURE_AUTHORITY_TOOL_PATH.is_file() or not FUTURE_AUTHORITY_PATH.is_file():
        raise AdapterError("authority_unavailable")
    try:
        contract = _load_module("_ot153_radio_execution_authority", FUTURE_AUTHORITY_TOOL_PATH)
    except BaseException as exc:
        raise AdapterError("authority_unavailable") from exc
    if getattr(contract, "AUTHORITY_RELATIVE", None) != FUTURE_AUTHORITY_RELATIVE:
        raise AdapterError("authority_unavailable")
    if not callable(getattr(contract, "load_preparation_binding", None)) or not callable(
        getattr(contract, "validate_execution_authority", None)
    ):
        raise AdapterError("authority_unavailable")
    return contract


class ExecutionAuthorityGate:
    """Validate the exact future OT-154 authority before coordinator I/O."""

    def __init__(
        self,
        authority_contract: Any,
        binding: object,
        benchmark_path: Path | None,
        restore_path: Path,
    ) -> None:
        self._authority_contract = authority_contract
        self._binding = binding
        self._benchmark_path = benchmark_path
        self._restore_path = restore_path

    def validate(self, binding: object, *, recovery: bool) -> object:
        if binding != self._binding:
            raise AdapterError("binding_mismatch")
        raw_sha256 = self._authority_contract.validate_execution_authority(
            FUTURE_AUTHORITY_PATH,
            self._benchmark_path,
            self._restore_path,
            ADAPTER_PATH,
            recovery=recovery,
        )
        return coordinator.AuthorityGrant(
            raw_sha256=raw_sha256,
            attempt_count=1,
            reusable=False,
            radio_allowed=True,
        )


class SerialRadioEndpoint:
    """Bounded ASCII command/receipt channel with no diagnostic echo surface."""

    _PASSIVE_KINDS = frozenset({
        "BOOT", "PROFILE", "STATUS", "RX_START", "STAGE_ACCEPT", "TIMEOUT",
        "STALE_SELFTEST", "COMMANDS", "RESTART",
    })

    def __init__(self, serial_handle: Any) -> None:
        self._serial = serial_handle
        self._closed = False

    @staticmethod
    def _command_valid(command: object) -> bool:
        if type(command) is not str or not command.isascii() or len(command) > 160:
            return False
        parts = command.split(" ")
        if any(not part for part in parts):
            return False
        verb = parts[0]
        token = re_token = r"[0-9a-f]{16}"
        import re
        if verb == "prepare":
            return (
                len(parts) == 5
                and re.fullmatch(token, parts[1]) is not None
                and re.fullmatch(token, parts[2]) is not None
                and parts[3] in {"I", "R"}
                and parts[4] in {"baseline", "retry-m2-withheld", "retry-restart"}
            )
        if verb in {"arm-tx", "send"}:
            return (
                len(parts) == 4
                and re.fullmatch(token, parts[1]) is not None
                and re.fullmatch(token, parts[2]) is not None
                and parts[3] in {"m1", "m2", "m3"}
            )
        if verb in {"abort", "end"}:
            return (
                len(parts) == 3
                and re.fullmatch(token, parts[1]) is not None
                and re.fullmatch(token, parts[2]) is not None
            )
        if verb in {"restart", "profile", "status"}:
            return len(parts) == 1
        return False

    def write_command(self, command: str) -> None:
        if self._closed or not self._command_valid(command):
            raise AdapterError("command_rejected")
        try:
            self._serial.write(command.encode("ascii") + b"\n")
            self._serial.flush()
        except BaseException as exc:
            raise AdapterError("endpoint_write_failed") from exc

    @staticmethod
    def _receipt_from_line(raw: object) -> object | None:
        if type(raw) is not bytes or not raw or len(raw) > MAX_LINE_BYTES:
            return None
        try:
            line = raw.decode("ascii", errors="strict")
        except UnicodeError:
            return None
        marker = line.find("OT153 ")
        if marker < 0:
            return None
        return coordinator.runner.parse_receipt(line[marker:])

    def expect(self, kind: str, timeout_ms: int) -> object:
        if (
            self._closed
            or type(kind) is not str
            or not kind
            or type(timeout_ms) is not int
            or timeout_ms <= 0
            or timeout_ms > 60_000
        ):
            raise AdapterError("receipt_request_rejected")
        deadline = time.monotonic() + timeout_ms / 1_000.0
        lines = 0
        while time.monotonic() < deadline and lines < MAX_RECEIPT_LINES:
            lines += 1
            try:
                raw = self._serial.readline()
            except BaseException as exc:
                raise AdapterError("endpoint_read_failed") from exc
            receipt = self._receipt_from_line(raw)
            if receipt is None:
                continue
            if receipt.kind == kind:
                return receipt
            if receipt.kind in self._PASSIVE_KINDS:
                continue
            raise AdapterError("receipt_sequence_invalid")
        raise AdapterError("receipt_timeout")

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        for attribute in ("rts", "dtr"):
            try:
                setattr(self._serial, attribute, False)
            except BaseException:
                pass
        try:
            self._serial.close()
        except BaseException as exc:
            raise AdapterError("endpoint_close_failed") from exc


class EsptoolSerialBackend:
    """Two-endpoint backend for exact app-only esptool and OT-153 serial I/O."""

    def __init__(self, private_endpoints: tuple[str, str]) -> None:
        if (
            type(private_endpoints) is not tuple
            or len(private_endpoints) != 2
            or any(type(value) is not str or not value for value in private_endpoints)
            or private_endpoints[0].casefold() == private_endpoints[1].casefold()
        ):
            raise AdapterError("invalid_endpoints")
        if importlib.metadata.version("esptool") != ESPTOOL_VERSION:
            raise AdapterError("dependency_mismatch")
        try:
            import serial  # type: ignore
        except Exception as exc:
            raise AdapterError("dependency_mismatch") from exc
        self._private_endpoints = private_endpoints
        self._python = sys.executable
        self._serial = serial

    def _endpoint(self, value: object) -> str:
        if type(value) is not str or value not in self._private_endpoints:
            raise AdapterError("endpoint_rejected")
        return value

    @staticmethod
    def _require_offset(offset: int) -> None:
        if type(offset) is not int or offset != coordinator.APPLICATION_OFFSET:
            raise AdapterError("application_offset_mismatch")

    def _esptool(self, private_endpoint: object, operation: list[str]) -> None:
        endpoint = self._endpoint(private_endpoint)
        command = [
            self._python, "-m", "esptool", "--chip", "esp32s3", "--port", endpoint,
            "--baud", str(coordinator.BAUD), "--before", "default-reset", "--after",
            "no-reset", "--no-stub", *operation,
        ]
        try:
            completed = subprocess.run(
                command,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=180,
                check=False,
            )
        except BaseException as exc:
            raise AdapterError("transport_failed") from exc
        if completed.returncode != 0:
            raise AdapterError("transport_failed")

    def write_application(self, private_endpoint: object, offset: int, image: object) -> None:
        self._require_offset(offset)
        payload = getattr(image, "payload", None)
        if type(payload) is not bytes or not 0 < len(payload) <= coordinator.FACTORY_SLOT_BYTES:
            raise AdapterError("application_image_invalid")
        with tempfile.TemporaryDirectory(prefix="ot153-noise-xk-app-") as directory:
            image_path = Path(directory) / "application.bin"
            image_path.write_bytes(payload)
            self._esptool(
                private_endpoint,
                ["write-flash", "--flash-size", "16MB", "0x10000", str(image_path)],
            )

    def verify_application(self, private_endpoint: object, offset: int, image: object) -> None:
        self._require_offset(offset)
        size = getattr(image, "size", None)
        expected_sha256 = getattr(image, "sha256", None)
        if (
            type(size) is not int
            or not 0 < size <= coordinator.FACTORY_SLOT_BYTES
            or type(expected_sha256) is not str
            or coordinator.HASH64.fullmatch(expected_sha256) is None
        ):
            raise AdapterError("application_image_invalid")
        with tempfile.TemporaryDirectory(prefix="ot153-noise-xk-readback-") as directory:
            readback_path = Path(directory) / "application.bin"
            self._esptool(
                private_endpoint,
                ["read-flash", "0x10000", str(size), str(readback_path)],
            )
            try:
                readback = readback_path.read_bytes()
            except OSError as exc:
                raise AdapterError("readback_failed") from exc
            if len(readback) != size or coordinator._sha256(readback) != expected_sha256:
                raise AdapterError("readback_failed")

    def hard_reset(self, private_endpoint: object) -> None:
        endpoint = self._endpoint(private_endpoint)
        command = [
            self._python, "-m", "esptool", "--chip", "esp32s3", "--port", endpoint,
            "--baud", str(coordinator.BAUD), "--before", "default-reset", "--after",
            "hard-reset", "run",
        ]
        try:
            completed = subprocess.run(
                command,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=60,
                check=False,
            )
        except BaseException as exc:
            raise AdapterError("transport_failed") from exc
        if completed.returncode != 0:
            raise AdapterError("transport_failed")

    def open_radio_endpoint(self, private_endpoint: object) -> SerialRadioEndpoint:
        endpoint = self._endpoint(private_endpoint)
        serial_handle: Any | None = None
        try:
            serial_handle = self._serial.Serial(
                port=None, baudrate=coordinator.BAUD, timeout=SERIAL_TIMEOUT_SECONDS
            )
            serial_handle.dtr = False
            serial_handle.rts = False
            serial_handle.port = endpoint
            serial_handle.open()
            return SerialRadioEndpoint(serial_handle)
        except BaseException as exc:
            if serial_handle is not None:
                try:
                    serial_handle.close()
                except BaseException:
                    pass
            raise AdapterError("endpoint_open_failed") from exc


def preflight_only(config: object, backend: object, authority: object) -> None:
    """Authority-check, exact Trail readback, and hard reset both nodes only."""
    if coordinator.JOURNAL_PATH.exists():
        raise AdapterError("authority_already_consumed")
    unused_binding, unused_grant, unused_benchmark, restore = coordinator._prepare(
        config, authority, recovery=False
    )
    if coordinator.JOURNAL_PATH.exists():
        raise AdapterError("authority_already_consumed")
    if not coordinator._preflight(config, backend, restore):
        raise AdapterError("preflight_failed")


def _parser() -> SafeArgumentParser:
    parser = SafeArgumentParser(description=__doc__, add_help=False)
    parser.add_argument("--port-a", required=True)
    parser.add_argument("--port-b", required=True)
    parser.add_argument("--benchmark-app", type=Path)
    parser.add_argument("--restore-app", type=Path, required=True)
    parser.add_argument("--visual-preflight-confirmed", action="store_true")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--preflight-only", action="store_true")
    mode.add_argument("--execute", action="store_true")
    mode.add_argument("--recover", action="store_true")
    return parser


def _paths(args: argparse.Namespace) -> tuple[Path | None, Path]:
    if args.recover:
        benchmark = args.benchmark_app.resolve() if args.benchmark_app else None
    else:
        if args.benchmark_app is None:
            raise ArgumentError("benchmark_required")
        if args.execute and args.visual_preflight_confirmed is not True:
            raise ArgumentError("preflight_not_confirmed")
        benchmark = args.benchmark_app.resolve()
    return benchmark, args.restore_app.resolve()


def main(
    argv: list[str] | None = None,
    *,
    backend_factory: Callable[[tuple[str, str]], object] = EsptoolSerialBackend,
    authority_loader: Callable[[], Any] = _load_authority_contract,
) -> int:
    try:
        args = _parser().parse_args(argv)
        if (
            type(args.port_a) is not str
            or type(args.port_b) is not str
            or not args.port_a
            or not args.port_b
            or args.port_a.casefold() == args.port_b.casefold()
        ):
            raise ArgumentError("invalid_endpoints")
        benchmark_path, restore_path = _paths(args)
        authority_contract = authority_loader()
        binding_fields = authority_contract.load_preparation_binding(
            benchmark_path, restore_path, ADAPTER_PATH, recovery=args.recover
        )
        binding = coordinator.ExecutionBinding(**binding_fields)
        placeholder_benchmark = (ROOT / binding.benchmark_name).resolve()
        config = coordinator.RunConfig(
            private_endpoints=(args.port_a, args.port_b),
            binding=binding,
            benchmark_path=benchmark_path or placeholder_benchmark,
            restore_path=restore_path,
        )
        gate = ExecutionAuthorityGate(
            authority_contract, binding, benchmark_path, restore_path
        )
        backend = backend_factory(config.private_endpoints)
        if args.preflight_only:
            preflight_only(config, backend, gate)
            print("OK: OT-153 preflight reset completed on both nodes")
        elif args.recover:
            coordinator.recover(config, backend, gate)
            print("OK: OT-153 recovery restored all touched nodes")
        else:
            coordinator.execute(config, backend, gate)
            print("OK: OT-153 two-node radio attempt completed and restored")
        return 0
    except BaseException:
        print("ERROR: OT-153 hardware operation failed", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
