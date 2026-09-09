"""Exact contained build/source admission; no hardware or authority issuance."""
from __future__ import annotations

import hashlib
import json
import noise_xk_startup_execution as startup

previous = startup.previous
BundleError = previous.BundleError
freeze_images = previous.freeze_images
verify_images = previous.verify_images
SOURCE_SCHEMA = "noise-xk-contained-source-closure-v1"
BUILD_RECORD_PATH = "tests/benchmarks/crypto/OT-163-RADIO-CONTAINMENT-BUILD-2026-09-09.json"
BUILD_RECORD_SHA256 = "d2d4c6f47629b294c161444f4b626f484f5953ae5ef079a4c7d0feea98c56f29"
BENCHMARK_NAME = "heltec_v4_noise_xk_contained.bin"
BUILD_SOURCE_PATHS = (
    "tools/radiolib_busy_source.py",
    "tools/noise_xk_contained_firmware_source.py",
    "firmware/targets/heltec_v4_noise_xk_contained/CMakeLists.txt",
    "firmware/targets/heltec_v4_noise_xk_contained/main/CMakeLists.txt",
    "tests/benchmarks/crypto/esp_idf/noise_xk_ready_radio/CMakeLists.txt",
    "tests/benchmarks/crypto/esp_idf/noise_xk_ready_radio/main/CMakeLists.txt",
    "tests/benchmarks/crypto/esp_idf/noise_xk_ready_radio/main/idf_component.yml",
    "tests/benchmarks/crypto/esp_idf/noise_xk_ready_radio/dependencies.lock",
    "tools/noise_xk_ready_firmware_source.py",
    "tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/main/app_main.cpp",
    "tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/sdkconfig.defaults",
    "tests/benchmarks/crypto/adapters/libsodium_noise_xk_v0/noise_xk_libsodium.c",
    "firmware/targets/heltec_v4_radio_diag/main/esp32_radiolib_hal.cpp",
)
SOURCE_PATHS = tuple(dict.fromkeys(startup.SOURCE_PATHS + (
    "tools/noise_xk_contained_bundle.py", "tools/noise_xk_contained_execution.py",
    "tools/noise_xk_contained_runtime.py", "tools/noise_xk_contained_endpoint.py",
    BUILD_RECORD_PATH) + BUILD_SOURCE_PATHS))


def freeze_sources(root):
    return {"schema": SOURCE_SCHEMA,
            "sources": [previous._descriptor(root, path) for path in SOURCE_PATHS]}


def verify_sources(root, manifest):
    previous._require(type(manifest) is dict and set(manifest) == {"schema", "sources"}
                      and manifest["schema"] == SOURCE_SCHEMA
                      and type(manifest["sources"]) is list, "source_manifest_invalid")
    for entry in manifest["sources"]:
        previous._valid_descriptor(entry)
    paths = [entry["path"] for entry in manifest["sources"]]
    previous._require(len(paths) == len(set(paths)) and set(paths) == set(SOURCE_PATHS),
                      "source_closure_mismatch")
    for entry in manifest["sources"]:
        previous._require(previous._descriptor(root, entry["path"]) == entry,
                          "source_digest_mismatch")


def verify_build_provenance(root, benchmark):
    raw = previous._file(root, BUILD_RECORD_PATH)
    previous._require(hashlib.sha256(raw).hexdigest() == BUILD_RECORD_SHA256,
                      "build_record_digest_mismatch")
    record = json.loads(raw)
    previous._require(record.get("schema") == "OT163-RADIO-CONTAINMENT-BUILD-1"
                      and record.get("project_version") == "nxk-contained-v1"
                      and set(record["source_inputs"]) == set(BUILD_SOURCE_PATHS),
                      "build_record_invalid")
    for path, descriptor in record["source_inputs"].items():
        actual = previous._descriptor(root, path)
        previous._require({"bytes": actual["bytes"], "sha256": actual["sha256"]} == descriptor,
                          "build_source_mismatch")
    for artifact in record["artifacts"].values():
        previous._require(artifact["byte_identical"] is True and artifact["a"] == artifact["b"],
                          "build_artifact_mismatch")
    previous._require(benchmark == {"name": BENCHMARK_NAME,
                      **record["artifacts"][BENCHMARK_NAME]["a"]}, "benchmark_build_mismatch")
