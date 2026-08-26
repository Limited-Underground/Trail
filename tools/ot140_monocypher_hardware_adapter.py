#!/usr/bin/env python3
"""Privacy-safe hardware adapter for the OT-140-authorized OT-140 coordinator.

This is the only concrete composition boundary for the two-node attempt.  It
binds one backend instance to application flash/readback, reset, endpoint
presence, and serial capture.  Device endpoints and backend diagnostics remain
private and are never printed or written by this module.
"""

from __future__ import annotations

import argparse
import importlib.metadata
import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Callable


ROOT = Path(__file__).resolve().parents[1]
ADAPTER_PATH = Path(__file__).resolve()
COORDINATOR_PATH = ROOT / "tools" / "ot140_monocypher_coordinator.py"
AUTHORITY_TOOL_PATH = ROOT / "tools" / "ot140_monocypher_execution_authority.py"
ESPTOOL_VERSION = "5.3.1"
SERIAL_TIMEOUT_SECONDS = 0.25


def _load_module(name: str, path: Path) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError("runtime_contract_unavailable")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


coordinator = _load_module("_ot140_coordinator", COORDINATOR_PATH)
authority_contract = _load_module("_ot140_execution_authority", AUTHORITY_TOOL_PATH)


class AdapterError(RuntimeError):
    """Closed adapter failure with no private diagnostic payload."""


class ArgumentError(AdapterError):
    """Closed CLI-validation failure."""


class SafeArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        raise ArgumentError("invalid_arguments")


class ExecutionAuthorityGate:
    """Validate the immutable OT-140 authority before coordinator device I/O."""

    def __init__(
        self,
        authority_path: Path,
        benchmark_path: Path | None,
        restore_path: Path,
    ) -> None:
        self._authority_path = authority_path
        self._benchmark_path = benchmark_path
        self._restore_path = restore_path

    def validate(self, binding: object, *, recovery: bool) -> object:
        if binding != coordinator._binding():
            raise AdapterError("binding_mismatch")
        raw_sha256 = authority_contract.validate_execution_authority(
            self._authority_path,
            self._benchmark_path,
            self._restore_path,
            ADAPTER_PATH,
            recovery=recovery,
        )
        return coordinator.AuthorityGrant(
            raw_sha256=raw_sha256,
            attempt_count=1,
            reusable=False,
            radio_allowed=False,
        )


class EsptoolSerialBackend:
    """One endpoint-bound backend for app-only esptool and OT-139 capture."""

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
            from serial.tools import list_ports  # type: ignore
        except Exception as exc:
            raise AdapterError("dependency_mismatch") from exc
        self._private_endpoints = private_endpoints
        self._python = sys.executable
        self._serial = serial
        self._list_ports = list_ports

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
            self._python,
            "-m",
            "esptool",
            "--chip",
            "esp32s3",
            "--port",
            endpoint,
            "--baud",
            str(coordinator.BAUD),
            "--before",
            "default-reset",
            "--after",
            "no-reset",
            "--no-stub",
            *operation,
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
        with tempfile.TemporaryDirectory(prefix="ot140-monocypher-app-") as directory:
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
        with tempfile.TemporaryDirectory(prefix="ot140-monocypher-readback-") as directory:
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
            self._python,
            "-m",
            "esptool",
            "--chip",
            "esp32s3",
            "--port",
            endpoint,
            "--baud",
            str(coordinator.BAUD),
            "--before",
            "default-reset",
            "--after",
            "hard-reset",
            "run",
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

    def reset(self, private_endpoint: object) -> None:
        self.hard_reset(private_endpoint)

    def is_present(self, private_endpoint: object) -> bool:
        endpoint = self._endpoint(private_endpoint)
        try:
            return any(port.device == endpoint for port in self._list_ports.comports())
        except BaseException as exc:
            raise AdapterError("endpoint_enumeration_failed") from exc

    def open(self, private_endpoint: object) -> object:
        endpoint_value = self._endpoint(private_endpoint)
        serial_endpoint: Any | None = None
        try:
            serial_endpoint = self._serial.Serial(
                port=None,
                baudrate=coordinator.BAUD,
                timeout=SERIAL_TIMEOUT_SECONDS,
            )
            serial_endpoint.dtr = False
            serial_endpoint.rts = False
            serial_endpoint.port = endpoint_value
            serial_endpoint.open()
            return serial_endpoint
        except BaseException as exc:
            if serial_endpoint is not None:
                for attribute in ("rts", "dtr"):
                    try:
                        setattr(serial_endpoint, attribute, False)
                    except BaseException:
                        pass
                try:
                    serial_endpoint.close()
                except BaseException:
                    pass
            raise AdapterError("endpoint_open_failed") from exc


def preflight_only(
    config: object,
    backend: object,
    authority: object,
) -> None:
    """Validate all bindings, then read back and reset both Trail nodes only."""
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
        authority_path = (ROOT / authority_contract.AUTHORITY_RELATIVE).resolve()
        placeholder_benchmark = (ROOT / coordinator.BENCHMARK_NAME).resolve()
        config = coordinator.RunConfig(
            private_endpoints=(args.port_a, args.port_b),
            benchmark_path=benchmark_path or placeholder_benchmark,
            restore_path=restore_path,
        )
        gate = ExecutionAuthorityGate(
            authority_path,
            benchmark_path,
            restore_path,
        )
        backend = backend_factory(config.private_endpoints)
        if args.preflight_only:
            preflight_only(config, backend, gate)
            print("OK: OT-140 preflight reset completed on both nodes")
        elif args.recover:
            coordinator.recover(config, backend, gate)
            print("OK: OT-140 recovery restored all touched nodes")
        else:
            coordinator.execute(config, backend, gate)
            print("OK: OT-140 two-node attempt completed and restored")
        return 0
    except BaseException:
        print("ERROR: OT-140 hardware operation failed", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())



