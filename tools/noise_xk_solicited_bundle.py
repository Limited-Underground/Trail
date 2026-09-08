"""Exact solicited execution sources and images; no discovery or device access."""
from __future__ import annotations

from pathlib import Path
import hashlib
import noise_xk_source_bundle as previous

SOURCE_SCHEMA = "noise-xk-solicited-source-closure-v1"
BUILD_RECORD_PATH = "tests/benchmarks/crypto/OT-163-SOLICITED-READINESS-BUILD-2026-09-08.json"
BUILD_RECORD_SHA256 = "88f9541eeb03b63e8fc77629b4b0b97afe8de631cad4f495811af6c23eb98acf"
BENCHMARK_NAME = "noise_xk_ready_radio.bin"
SOURCE_PATHS = previous.SOURCE_PATHS + (
    "tools/noise_xk_solicited_coordinator.py",
    "tools/noise_xk_solicited_backend.py",
    "tools/noise_xk_solicited_runner.py",
    "tools/noise_xk_solicited_runtime.py",
    "tools/noise_xk_solicited_endpoint.py",
    "tools/noise_xk_solicited_bundle.py",
    "tools/noise_xk_solicited_execution.py",
    "tools/noise_xk_ready_firmware_source.py",
    "tests/benchmarks/crypto/OT-163-SOLICITED-READINESS-BUILD-2026-09-08.json",
    "tests/benchmarks/crypto/esp_idf/noise_xk_ready_radio/CMakeLists.txt",
    "tests/benchmarks/crypto/esp_idf/noise_xk_ready_radio/main/CMakeLists.txt",
    "tests/benchmarks/crypto/esp_idf/noise_xk_ready_radio/main/idf_component.yml",
    "tests/benchmarks/crypto/esp_idf/noise_xk_ready_radio/dependencies.lock",
)
BundleError = previous.BundleError
freeze_images = previous.freeze_images
verify_images = previous.verify_images


def freeze_sources(root: Path) -> dict:
    return {"schema": SOURCE_SCHEMA,
            "sources": [previous._descriptor(root, path) for path in SOURCE_PATHS]}


def verify_sources(root: Path, manifest: object) -> None:
    previous._require(type(manifest) is dict and set(manifest) == {"schema", "sources"}
                      and manifest["schema"] == SOURCE_SCHEMA
                      and type(manifest["sources"]) is list, "source_manifest_invalid")
    for value in manifest["sources"]:
        previous._valid_descriptor(value)
    paths = [value["path"] for value in manifest["sources"]]
    previous._require(len(paths) == len(set(paths)) and set(paths) == set(SOURCE_PATHS),
                      "source_closure_mismatch")
    for value in manifest["sources"]:
        previous._require(previous._descriptor(root, value["path"]) == value,
                          "source_digest_mismatch")


def verify_build_provenance(root: Path, benchmark: object) -> None:
    """Verify the accepted build's source records independently of its images."""
    import json
    raw = previous._file(root, BUILD_RECORD_PATH)
    previous._require(hashlib.sha256(raw).hexdigest() == BUILD_RECORD_SHA256,
                      "build_record_digest_mismatch")
    record = json.loads(raw)
    previous._require(record.get("schema") == "OT163-SOLICITED-READINESS-BUILD-1",
                      "build_record_invalid")
    for path, descriptor in record["source_inputs"].items():
        actual = previous._descriptor(root, path)
        previous._require({"bytes": actual["bytes"], "sha256": actual["sha256"]} == descriptor,
                          "build_source_mismatch")
    artifact = record["artifacts"][BENCHMARK_NAME]
    previous._require(artifact["byte_identical"] is True and artifact["a"] == artifact["b"]
                      and benchmark == {"name": BENCHMARK_NAME, **artifact["a"]},
                      "benchmark_build_mismatch")
