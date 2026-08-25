#!/usr/bin/env python3
"""Pure host inspection of the public OT-137 boot/control failure boundary.

The module has no command-line or device surface.  It imports the frozen OT-135
runner by its accepted digest, reproduces only the published counter shape with
fabricated bytes, and inspects public source/configuration evidence.  It neither
loads nor infers the unretained physical stream.
"""

from __future__ import annotations

import hashlib
import importlib.util
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
RUNNER_PATH = ROOT / "tools" / "ot135_monocypher_protocol_runner.py"
APP_SOURCE_PATH = (
    ROOT
    / "tests"
    / "benchmarks"
    / "crypto"
    / "esp_idf"
    / "ot121_candidate_benchmarks"
    / "monocypher_ot129"
    / "main"
    / "app_main.c"
)
COMMON_CONFIG_PATH = ROOT / "firmware" / "targets" / "heltec_v4_bench" / "sdkconfig.defaults"
BUILD_RECIPE_PATH = (
    ROOT / "tests" / "benchmarks" / "crypto" / "OT-123-OT005-MONOCYPHER-BUILD-RECIPE-V0.json"
)
IMMUTABLE_BUNDLE_PATH = (
    ROOT
    / "tests"
    / "benchmarks"
    / "crypto"
    / "OT-130-OT005-MONOCYPHER-IMMUTABLE-EXECUTION-BUNDLE-PREPARATION-V0.json"
)
ABORT_RECEIPT_PATH = (
    ROOT
    / "tests"
    / "benchmarks"
    / "crypto"
    / "OT-137-OT005-MONOCYPHER-EXECUTION-ABORT-RECEIPT-V0.json"
)

RUNNER_SHA256 = "e06fa00ccef1aeea167286698d64bdd546a09406921dd074009d7174a5366993"
APP_SOURCE_SHA256 = "fac7a9375a5dba5366215dc0eab0a03a83cfd22fd50a2ac563f1c378cb7aae2b"
COMMON_CONFIG_SHA256 = "a747ed37ec7be4dd1199f52af43395ff58ac92f897b2c35ac73b0a0ed6cf6ecb"
BUILD_RECIPE_SHA256 = "c109392296cf313f276bf0121629eeec2db8bf75773f76ca8086fa2f1a04e5cb"
IMMUTABLE_BUNDLE_SHA256 = "cc1d88aa9f5e45c3b13a1f229b767fcbf8e7dd383a75309e335f5397ddc780f7"
ABORT_RECEIPT_SHA256 = "1f6a75e2941045eb3585161769bb2a3ae544b1192d04594dc9d9bec53d77212c"
GENERATED_SDKCONFIG_SHA256 = "4260688e6323cfda7a50912b4cc9c77a7b6f5133b6970b543bf0ce822ffd023f"
BENCHMARK_SHA256 = "d94f2b2a823f23500d3592aa4e6aa1004b2f13c0865ea071f2598023321bf268"

CLASSIFICATION = "pre_ready_queued_bytes_exhaust_preamble_before_start_retry"
PHYSICAL_CONTENT = "unconfirmed_not_retained"
SUCCESSOR_DIRECTION = "console_isolated_quiet_target"
EXPECTED_DIAGNOSTICS = {
    "lifecycle": "stable_continuous",
    "reset_attempts": 1,
    "lifecycle_polls": 4,
    "stable_presence_polls": 3,
    "open_attempts": 1,
    "start_write_attempts": 1,
    "read_calls": 2,
    "empty_reads": 0,
    "bytes_observed": 1024,
    "preamble_lines_ignored": 11,
    "complete_lines": 11,
    "frame_lines_buffered": 0,
}
QUIET_TARGET_SETTINGS = (
    "CONFIG_ESP_CONSOLE_NONE=y",
    "CONFIG_ESP_CONSOLE_SECONDARY_NONE=y",
    "CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y",
    "CONFIG_LOG_DEFAULT_LEVEL_NONE=y",
    "CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y",
    "CONFIG_LOG_DYNAMIC_LEVEL_CONTROL=y",
)


class InspectionError(RuntimeError):
    """Closed failure for a mismatched public inspection input."""


@dataclass(frozen=True)
class Reproduction:
    classification: str
    failure_code: str
    diagnostics: dict[str, object]


@dataclass(frozen=True)
class Inspection:
    classification: str
    physical_content: str
    exact_public_shape_reproduced: bool
    nonempty_fragmentation_invariant: bool
    generated_sdkconfig_sha256: str
    usb_console_and_info_conflict: bool
    suppression_occurs_after_boot: bool
    successor_direction: str
    successor_settings: tuple[str, ...]
    successor_built: bool
    max_preamble_bytes: int
    exact_ready: str
    frame_before_ready_rejected: bool
    raw_capture_retained: bool


class _Clock:
    def __init__(self) -> None:
        self.now = 0.0

    def monotonic(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


class _Endpoint:
    def __init__(self, chunks: tuple[bytes, ...]) -> None:
        self._chunks = list(chunks)
        self.writes: list[bytes] = []
        self.flushes = 0
        self.read_sizes: list[int] = []
        self.closed = False

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        return len(data)

    def flush(self) -> None:
        self.flushes += 1

    def read(self, size: int) -> bytes:
        self.read_sizes.append(size)
        if not self._chunks:
            return b""
        chunk = self._chunks.pop(0)
        if len(chunk) > size:
            raise InspectionError("fabricated_chunk_too_large")
        return chunk

    def close(self) -> None:
        self.closed = True


class _Provider:
    def __init__(self, endpoint: _Endpoint) -> None:
        self.endpoint = endpoint

    def reset(self, unused_endpoint: object) -> None:
        del unused_endpoint

    def is_present(self, unused_endpoint: object) -> bool:
        del unused_endpoint
        return True

    def open(self, unused_endpoint: object) -> _Endpoint:
        del unused_endpoint
        return self.endpoint


def _sha256(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def _read_pinned(path: Path, expected_sha256: str) -> bytes:
    try:
        raw = path.read_bytes()
    except OSError as error:
        raise InspectionError("public_input_unavailable") from None
    if not raw or _sha256(raw) != expected_sha256:
        raise InspectionError("public_input_digest_mismatch")
    return raw


def _read_json(path: Path, expected_sha256: str) -> dict[str, Any]:
    raw = _read_pinned(path, expected_sha256)
    try:
        value = json.loads(raw.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise InspectionError("public_json_invalid") from None
    if type(value) is not dict:
        raise InspectionError("public_json_invalid")
    return value


def _load_runner() -> Any:
    _read_pinned(RUNNER_PATH, RUNNER_SHA256)
    spec = importlib.util.spec_from_file_location("_ot138_frozen_ot135_runner", RUNNER_PATH)
    if spec is None or spec.loader is None:
        raise InspectionError("runner_unavailable")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
    except BaseException as error:
        sys.modules.pop(spec.name, None)
        raise InspectionError("runner_unavailable") from None
    return module


def fabricated_ot137_shape() -> bytes:
    """Return fabricated bytes with the exact published line/byte shape."""
    complete = ((b"X" * 39) + b"\n") * 11
    fabricated = complete + (b"P" * (1024 - len(complete)))
    if len(fabricated) != 1024 or fabricated.count(b"\n") != 11:
        raise InspectionError("fabricated_shape_invalid")
    return fabricated


def reproduce(chunks: tuple[bytes, ...]) -> Reproduction:
    """Classify one fragmentation of the fixed fabricated stream."""
    if (
        type(chunks) is not tuple
        or not chunks
        or any(type(chunk) is not bytes or not chunk or len(chunk) > 512 for chunk in chunks)
        or b"".join(chunks) != fabricated_ot137_shape()
    ):
        raise InspectionError("fabricated_chunks_invalid")

    runner = _load_runner()
    endpoint = _Endpoint(chunks)
    provider = _Provider(endpoint)
    clock = _Clock()
    try:
        runner.capture_local_primitives(
            provider,
            object(),
            monotonic=clock.monotonic,
            sleep=clock.sleep,
            control_timeout=2.0,
            capture_timeout=2.0,
            presence_timeout=0.5,
        )
    except runner.CaptureError as error:
        diagnostics = dict(vars(error.diagnostics))
        classified = (
            error.code is runner.FailureCode.PREAMBLE_INVALID
            and diagnostics["start_write_attempts"] == 1
            and diagnostics["empty_reads"] == 0
            and diagnostics["bytes_observed"] > runner.MAX_PREAMBLE_BYTES
            and diagnostics["preamble_lines_ignored"] == 11
            and diagnostics["complete_lines"] == 11
            and diagnostics["frame_lines_buffered"] == 0
            and endpoint.writes == [runner.START]
            and endpoint.flushes == 1
            and endpoint.read_sizes == [runner.READ_SIZE] * diagnostics["read_calls"]
            and endpoint.closed
        )
        if not classified:
            raise InspectionError("fabricated_failure_unclassified") from None
        return Reproduction(CLASSIFICATION, error.code.value, diagnostics)
    raise InspectionError("fabricated_failure_not_observed")


def inspect() -> Inspection:
    """Validate public anchors and return the bounded OT-138 classification."""
    receipt = _read_json(ABORT_RECEIPT_PATH, ABORT_RECEIPT_SHA256)
    failure = receipt.get("failure")
    privacy = receipt.get("privacy")
    authority = receipt.get("authority")
    claims = receipt.get("claims")
    if (
        type(failure) is not dict
        or failure.get("code") != "capture_failed"
        or failure.get("capture_code") != "preamble_invalid"
        or failure.get("capture_diagnostics") != EXPECTED_DIAGNOSTICS
        or privacy != {
            "anonymous_role_labels_only": True,
            "backend_error_text_recorded": False,
            "device_identifiers_recorded": False,
            "filesystem_paths_recorded": False,
            "private_journal_recorded": False,
            "private_receipt_recorded": False,
            "raw_capture_recorded": False,
            "run_nonce_recorded": False,
            "serial_ports_recorded": False,
        }
        or authority != {
            "consumed_by_abort": True,
            "continuing_authority": False,
            "reusable": False,
        }
        or type(claims) is not dict or not claims
        or any(value is not False for value in claims.values())
    ):
        raise InspectionError("public_abort_boundary_mismatch")

    fabricated = fabricated_ot137_shape()
    exact = reproduce((fabricated[:512], fabricated[512:]))
    fragmented = reproduce((fabricated[:31], fabricated[31:512], fabricated[512:]))
    if exact.diagnostics != EXPECTED_DIAGNOSTICS:
        raise InspectionError("exact_public_shape_not_reproduced")
    if fragmented.classification != exact.classification:
        raise InspectionError("classification_depends_on_chunking")

    recipe = _read_json(BUILD_RECIPE_PATH, BUILD_RECIPE_SHA256)
    immutable_bundle = _read_json(IMMUTABLE_BUNDLE_PATH, IMMUTABLE_BUNDLE_SHA256)
    recipe_hash = recipe.get("execution", {}).get("generated_sdkconfig_sha256")
    bundle_build = immutable_bundle.get("build")
    source_inputs = immutable_bundle.get("source_inputs")
    bundle_images = immutable_bundle.get("images")
    if (
        recipe_hash != GENERATED_SDKCONFIG_SHA256
        or type(bundle_build) is not dict
        or bundle_build.get("generated_sdkconfig") != {
            "bytes": 106913,
            "sha256": GENERATED_SDKCONFIG_SHA256,
        }
        or type(source_inputs) is not list
        or {
            "path": (
                "tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/"
                "monocypher_ot129/main/app_main.c"
            ),
            "raw_sha256": APP_SOURCE_SHA256,
        } not in source_inputs
        or {
            "path": "firmware/targets/heltec_v4_bench/sdkconfig.defaults",
            "raw_sha256": COMMON_CONFIG_SHA256,
        } not in source_inputs
        or type(bundle_images) is not dict
        or bundle_images.get("benchmark") != {
            "bytes": 187680,
            "name": "ot129_monocypher_protocol_bench.bin",
            "sha256": BENCHMARK_SHA256,
        }
    ):
        raise InspectionError("generated_sdkconfig_anchor_mismatch")
    receipt_bindings = receipt.get("bindings")
    if (
        type(receipt_bindings) is not dict
        or receipt_bindings.get("runner") != {
            "name": "ot135_monocypher_protocol_runner.py",
            "sha256": RUNNER_SHA256,
        }
        or receipt_bindings.get("benchmark") != {
            "name": "ot129_monocypher_protocol_bench.bin",
            "bytes": 187680,
            "sha256": BENCHMARK_SHA256,
        }
    ):
        raise InspectionError("public_abort_lineage_mismatch")

    config = _read_pinned(COMMON_CONFIG_PATH, COMMON_CONFIG_SHA256).decode("utf-8")
    config_lines = set(config.splitlines())
    conflict = {
        "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y",
        "CONFIG_LOG_DEFAULT_LEVEL_INFO=y",
    }.issubset(config_lines) and "CONFIG_ESP_CONSOLE_NONE=y" not in config_lines
    if not conflict:
        raise InspectionError("console_configuration_mismatch")

    source = _read_pinned(APP_SOURCE_PATH, APP_SOURCE_SHA256).decode("utf-8")
    app_main = source.index("void app_main(void)")
    suppress_sink = source.index("esp_log_set_vprintf", app_main)
    suppress_level = source.index("esp_log_level_set", suppress_sink)
    install_driver = source.index("install_buffered_usb_serial_jtag_protocol();", suppress_level)
    start_task = source.index("xTaskCreatePinnedToCore", install_driver)
    wait_start = source.index("ot129_wait_for_start();", start_task)
    suppression_ordered = app_main < suppress_sink < suppress_level < install_driver < start_task < wait_start
    if not suppression_ordered:
        raise InspectionError("application_suppression_order_mismatch")

    runner = _load_runner()
    if (
        runner.MAX_PREAMBLE_BYTES != 512
        or runner.READY != b"OTCBXCTL1 READY\n"
        or runner.FailureCode.FRAME_BEFORE_READY.value != "frame_before_ready"
    ):
        raise InspectionError("frozen_runner_boundary_mismatch")

    return Inspection(
        classification=CLASSIFICATION,
        physical_content=PHYSICAL_CONTENT,
        exact_public_shape_reproduced=True,
        nonempty_fragmentation_invariant=True,
        generated_sdkconfig_sha256=GENERATED_SDKCONFIG_SHA256,
        usb_console_and_info_conflict=True,
        suppression_occurs_after_boot=True,
        successor_direction=SUCCESSOR_DIRECTION,
        successor_settings=QUIET_TARGET_SETTINGS,
        successor_built=False,
        max_preamble_bytes=runner.MAX_PREAMBLE_BYTES,
        exact_ready=runner.READY.decode("ascii"),
        frame_before_ready_rejected=True,
        raw_capture_retained=False,
    )
