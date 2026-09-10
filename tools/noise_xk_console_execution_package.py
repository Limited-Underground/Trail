"""Exact receipt-console package and isolated execution seam; no hardware CLI.

Building/verifying a package creates no grant and establishes no physical role
identity. Execution still requires a fresh verified caller and explicit authority.
"""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
from types import SimpleNamespace

import noise_xk_receipt_observation_execution as observed

previous, BundleError = observed.previous, observed.BundleError
require = previous._require
SCHEMA = "OT163-RECEIPT-CONSOLE-PACKAGE-1"
SOURCE_SCHEMA = "noise-xk-receipt-console-source-closure-v1"
BUILD_RECORD_PATH = "tests/benchmarks/crypto/OT-163-RECEIPT-CONSOLE-BUILD-2026-09-10.json"
HISTORICAL_BINDING_PATH = "tests/benchmarks/crypto/OT-163-OBSERVATION-EXECUTION-BINDING-4-2026-09-09.json"
HISTORICAL_BINDING_SHA256 = "cf1cdd5087ad65f29dae9fad6ea48cd5358f180d8f5732d4073add05a36b58f7"
BENCHMARK_NAME = "heltec_v4_noise_xk_receipts.bin"
TARGET = "firmware/targets/heltec_v4_noise_xk_receipts"
BUILD_SOURCE_PATHS = (
    "tools/noise_xk_console_firmware_source.py",
    "tools/noise_xk_receipt_firmware_source.py",
    "firmware/components/diagnostics/include/opentrail/bounded_console_writer.hpp",
    "firmware/components/diagnostics/include/opentrail/usb_fifo_console_transport.hpp",
    "firmware/components/diagnostics/include/opentrail/bounded_receipt_formatter.hpp",
    TARGET + "/CMakeLists.txt", TARGET + "/main/CMakeLists.txt",
    TARGET + "/main/opentrail_console.cpp", TARGET + "/main/opentrail_console.h",
    TARGET + "/sdkconfig.defaults",
)
SOURCE_PATHS = observed.SOURCE_PATHS + BUILD_SOURCE_PATHS + (
    "tools/noise_xk_console_execution_package.py", BUILD_RECORD_PATH, HISTORICAL_BINDING_PATH)


def _json(raw):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, "duplicate_json_key")
            result[key] = value
        return result
    try:
        return json.loads(raw, object_pairs_hook=unique)
    except (ValueError, UnicodeError) as error:
        if isinstance(error, BundleError): raise
        raise BundleError("json_invalid") from None


def _historical(root):
    raw = previous._file(root, HISTORICAL_BINDING_PATH)
    require(hashlib.sha256(raw).hexdigest() == HISTORICAL_BINDING_SHA256, "historical_binding_changed")
    record = _json(raw)
    observed.verify_sources(root, record["sources"])
    return record


def _sources(root):
    return {"schema": SOURCE_SCHEMA,
            "sources": [previous._descriptor(root, path) for path in SOURCE_PATHS]}


def _verify_sources(root, manifest):
    require(type(manifest) is dict and set(manifest) == {"schema", "sources"}
            and manifest["schema"] == SOURCE_SCHEMA and type(manifest["sources"]) is list,
            "source_manifest_invalid")
    for entry in manifest["sources"]: previous._valid_descriptor(entry)
    paths = [entry["path"] for entry in manifest["sources"]]
    require(len(paths) == len(set(paths)) and set(paths) == set(SOURCE_PATHS), "source_closure_mismatch")
    for entry in manifest["sources"]:
        require(previous._descriptor(root, entry["path"]) == entry, "source_digest_mismatch")
    _historical(root)


def _build(root, descriptor, benchmark):
    previous._valid_descriptor(descriptor)
    require(descriptor["path"] == BUILD_RECORD_PATH and previous._descriptor(root, BUILD_RECORD_PATH) == descriptor,
            "build_record_changed")
    record = _json(previous._file(root, BUILD_RECORD_PATH))
    require(record.get("schema") == "OT163-RECEIPT-CONSOLE-BUILD-1"
            and record.get("project_version") == "nxk-receipts-v1" and record.get("target") == TARGET,
            "build_record_invalid")
    require(type(record.get("source_inputs")) is dict
            and set(record["source_inputs"]) == set(BUILD_SOURCE_PATHS), "build_sources_invalid")
    for path, value in record["source_inputs"].items():
        actual = previous._descriptor(root, path)
        require(value == {key: actual[key] for key in ("bytes", "sha256")}, "build_source_changed")
    builds = record.get("builds")
    require(type(builds) is list and len(builds) == 2 and all(type(b) is dict
            and b.get("initially_absent") is True and type(b.get("exit_code")) is int
            and b["exit_code"] == 0 for b in builds), "build_pair_invalid")
    directories = [previous._relative(b.get("directory")) for b in builds]
    require(len(set(directories)) == 2, "build_pair_invalid")
    require(type(record.get("tests")) is dict and record["tests"].get("full_host_matrix") == "passed",
            "host_validation_missing")
    artifacts = record.get("artifacts")
    require(type(artifacts) is dict and BENCHMARK_NAME in artifacts, "build_artifacts_invalid")
    value = artifacts[BENCHMARK_NAME]
    require(type(value) is dict and set(value) == {"bytes", "sha256", "comparison"}
            and value["comparison"] == "exact", "benchmark_build_mismatch")
    require(benchmark == {"name": BENCHMARK_NAME, "bytes": value["bytes"], "sha256": value["sha256"]},
            "benchmark_build_mismatch")


def _scope(images):
    def span(size): return ((size + 4095) // 4096) * 4096
    sizes = {role: value["bytes"] for role, value in images["images"].items()}
    require(all(type(size) is int and size > 0 for size in sizes.values()), "image_size_invalid")
    require(span(sizes["benchmark"]) <= min(span(sizes[r]) for r in ("restore_a", "restore_b")),
            "benchmark_exceeds_restore_span")
    return {"attempt_ordinal": 5, "application_offset": 65536,
            "write_bytes": sizes, "erase_span_bytes": {role: span(size) for role, size in sizes.items()},
            "grant_issued": False, "hardware_executed": False,
            "preserve_regions": ["bootloader", "partition", "ota", "nvs"],
            "required_gates": ["fresh_source_verified_caller", "fresh_role_identity_and_installed_readbacks",
                               "independent_role_recovery", "fresh_one_use_authority"]}


def freeze_package(root, benchmark_path, expected_build_record_sha256):
    root = Path(root)
    historical = _historical(root)
    paths = {**historical["image_paths"], "benchmark": previous._relative(benchmark_path)}
    images = previous.freeze_images(root, paths)
    descriptor = previous._descriptor(root, BUILD_RECORD_PATH)
    require(descriptor["sha256"] == expected_build_record_sha256, "build_record_pin_mismatch")
    package = {"schema": SCHEMA, "snapshot": {"sources": _sources(root), "images": images},
               "image_paths": paths, "build_record": descriptor, "scope": _scope(images)}
    verify_package(root, package)
    return package


def verify_package(root, package, *, recovery=False):
    require(type(recovery) is bool, "recovery_mode_invalid")
    require(type(package) is dict and set(package) == {"schema", "snapshot", "image_paths", "build_record", "scope"}
            and package["schema"] == SCHEMA, "package_invalid")
    snapshot = package["snapshot"]
    require(type(snapshot) is dict and set(snapshot) == {"sources", "images"}, "snapshot_invalid")
    _verify_sources(root, snapshot["sources"])
    previous.verify_images(root, snapshot["images"], package["image_paths"], recovery=recovery)
    historical = _historical(root)
    for role in ("restore_a", "restore_b"):
        require(snapshot["images"]["images"][role] == historical["images"]["images"][role]
                and package["image_paths"][role] == historical["image_paths"][role], "original_restore_changed")
    require(type(package["scope"]) is dict, "scope_changed")
    expected_scope = _scope(snapshot["images"])
    require(json.dumps(package["scope"], sort_keys=True) == json.dumps(expected_scope, sort_keys=True),
            "scope_changed")
    # Build/source metadata stays mandatory during recovery; benchmark bytes may be absent.
    _build(root, package["build_record"], snapshot["images"]["images"]["benchmark"])


class ReceiptConsoleSession(observed.ReceiptObservationSession):
    def __init__(self, package, capacity=128):
        require(type(package) is dict and package.get("schema") == SCHEMA, "package_invalid")
        self.package = deepcopy(package)
        super().__init__(5, capacity)
        c = self.coordinator
        prefix = "noise-xk-receipt-console-5-recovery-"
        c.JOURNAL_NAME, c.EXECUTION_NAME, c.RECOVERY_NAME = (
            prefix + suffix for suffix in ("journal.json", "execution.json", "receipt.json"))
        c.frozen.JOURNAL_PATH = c.frozen.PRIVATE_ROOT / c.JOURNAL_NAME
        c.frozen.EXECUTION_RECEIPT_PATH = c.frozen.PRIVATE_ROOT / c.EXECUTION_NAME
        c.frozen.RECOVERY_RECEIPT_PATH = c.frozen.PRIVATE_ROOT / c.RECOVERY_NAME
        c.frozen.JOURNAL_SCHEMA, c.frozen.RECEIPT_SCHEMA = "OTNXRCJ0", "OTNXRCCR0"
        adapter = SimpleNamespace(**vars(self._execution.bundle))
        adapter.verify_build_provenance = self._verify_build
        self._execution.bundle = adapter

    def _verify_build(self, root, benchmark):
        _build(root, self.package["build_record"], benchmark)

    def _invoke(self, method, root, snapshot, paths, config, backend, authority):
        require(snapshot == self.package["snapshot"] and paths == self.package["image_paths"], "package_call_mismatch")
        verify_package(root, self.package, recovery=method is self._execution.recover)
        admitted = {"sources": {"schema": observed.SOURCE_SCHEMA,
            "sources": [item for item in snapshot["sources"]["sources"] if item["path"] in observed.SOURCE_PATHS]},
            "images": snapshot["images"]}
        return super()._invoke(method, root, admitted, paths, config, backend, authority)
