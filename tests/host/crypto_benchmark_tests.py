#!/usr/bin/env python3
"""Deterministic tests for the fail-closed OTCB0 benchmark boundary."""

from __future__ import annotations

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import crypto_benchmark as benchmark  # noqa: E402
import crypto_benchmark_baseline as baseline_validator  # noqa: E402
import crypto_benchmark_readiness as readiness_validator  # noqa: E402


PLAN_PATH = (
    ROOT
    / "tests"
    / "benchmarks"
    / "crypto"
    / "OT-005-CRYPTO-BENCHMARK-PLAN-V0.json"
)
BASELINE_PATH = (
    ROOT
    / "tests"
    / "benchmarks"
    / "crypto"
    / "OT-093-OT005-BUILD-BASELINE-V0.json"
)
READINESS_PATH = (
    ROOT
    / "tests"
    / "benchmarks"
    / "crypto"
    / "OT-094-OT005-CANDIDATE-READINESS-V0.json"
)


def expect_error(action, contains: str) -> None:
    try:
        action()
    except benchmark.ValidationError as exc:
        assert contains in str(exc), (contains, str(exc))
        return
    raise AssertionError(f"expected ValidationError containing {contains!r}")


def blocked_plan() -> dict:
    return benchmark._load(PLAN_PATH)


def baseline() -> dict:
    return baseline_validator.load(BASELINE_PATH)


def blocked_readiness() -> dict:
    return readiness_validator.load(READINESS_PATH)


def ready_plan() -> dict:
    plan = blocked_plan()
    plan["status"] = "ready"
    plan["target"] = {
        "manufacturer": "Synthetic fixture vendor",
        "board_model": "Synthetic ESP32-S3 fixture",
        "board_revision": "fixture-revision",
        "mcu": "ESP32-S3",
        "flash_bytes": 16 * 1024 * 1024,
        "psram_bytes": 2 * 1024 * 1024,
    }
    plan["toolchain"] = {
        "esp_idf_version": "v6.0.2",
        "esp_idf_commit": "7101770dc6db2667b3c477cc31365dd1acd6db4e",
        "compiler": "xtensa-esp32s3-elf-gcc",
        "compiler_version": "15.2.0",
        "sdkconfig_sha256": hashlib.sha256(b"synthetic-candidate-config").hexdigest(),
    }
    plan["radio"] = {
        "mtu_bytes": 120,
        "frequency_hz": 910_525_000,
        "bandwidth_hz": 62_500,
        "spreading_factor": 7,
        "coding_rate_denominator": 5,
    }
    for candidate in plan["candidates"]:
        if not candidate["version"]:
            candidate["version"] = "4.1.0"
        candidate["source_commit"] = hashlib.sha1(
            f"source:{candidate['candidate_id']}".encode("ascii")
        ).hexdigest()
        candidate["lock_sha256"] = hashlib.sha256(
            f"lock:{candidate['candidate_id']}".encode("ascii")
        ).hexdigest()
    plan["blockers"] = []
    return plan


def complete_result(plan: dict) -> dict:
    timings: dict[str, dict] = {}
    for index, operation in enumerate(benchmark.OPERATIONS, start=1):
        timings[operation] = {
            "cold": {
                "min_us": index,
                "median_us": index + 1,
                "p95_us": index + 2,
                "max_us": index + 3,
            },
            "warm": {
                "min_us": index,
                "median_us": index,
                "p95_us": index + 1,
                "max_us": index + 2,
            },
        }
    return {
        "schema": "OTCB0",
        "version": 0,
        "artifact_kind": "result",
        "benchmark_id": plan["benchmark_id"],
        "plan_sha256": benchmark.canonical_sha256(plan),
        "candidate_id": "espressif_libsodium",
        "completed_utc": "2026-08-20T12:00:00Z",
        "repetitions": {"cold": 100, "warm": 100},
        "timings": timings,
        "build": {"passed": True, "compiler_warnings": 0},
        "resources": {
            "linked_flash_delta_bytes": 123_456,
            "static_ram_bytes": 4_096,
            "peak_dynamic_ram_bytes": 8_192,
            "max_stack_used_bytes": 3_072,
            "watchdog_resets": 0,
        },
        "radio_cost": {
            "handshake_bytes": 384,
            "fragments": 4,
            "airtime_us": 500_000,
            "retries_tested": 3,
        },
        "evidence": {
            "binary_sha256": hashlib.sha256(b"synthetic-binary").hexdigest(),
            "sdkconfig_sha256": plan["toolchain"]["sdkconfig_sha256"],
            "sbom_sha256": hashlib.sha256(b"synthetic-sbom").hexdigest(),
            "raw_evidence_retained_privately": True,
        },
        "gates": {gate: True for gate in benchmark.GATES},
        "notes": "Synthetic host fixture; no benchmark or selection claim.",
    }


def test_blocked_public_plan_is_exact_valid_and_ineligible() -> None:
    plan = blocked_plan()
    info = benchmark.validate_plan(plan)
    assert info == {
        "status": "draft_blocked",
        "benchmark_id": "OT-005-CRYPTO-ESP32S3-V0",
        "plan_sha256": (
            "49792b585286823ffa9b7589704d57e8393b3dbf3d514917ffd7b5970301edb7"
        ),
        "readiness_verified": False,
        "execution_authorized": False,
    }
    expect_error(
        lambda: benchmark.result_template(plan, "espressif_libsodium"),
        "requires a ready plan",
    )


def test_declared_ready_v0_is_structural_only_without_accepted_readiness() -> None:
    plan = ready_plan()
    info = benchmark.validate_plan(plan)
    assert info["status"] == "ready"
    assert info["readiness_verified"] is False
    assert info["execution_authorized"] is False
    expect_error(
        lambda: benchmark.result_template(plan, "espressif_libsodium"),
        "separately verified fully resolved readiness",
    )


def test_blocked_readiness_cannot_be_reused_for_declared_ready_plan() -> None:
    plan = ready_plan()
    expect_error(
        lambda: benchmark.result_template(
            plan,
            "espressif_libsodium",
            blocked_readiness(),
            baseline(),
        ),
        "invalid or unaccepted",
    )
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "result.json"
        completed = subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" / "crypto_benchmark.py"),
                "create-result-template",
                str(PLAN_PATH),
                "espressif_libsodium",
                str(output),
                "--readiness",
                str(READINESS_PATH),
                "--baseline",
                str(BASELINE_PATH),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        assert completed.returncode == 2
        assert completed.stdout == "" and not output.exists()
        assert completed.stderr.strip() == (
            "ERROR: crypto benchmark evidence is invalid or unaccepted"
        )


def test_complete_synthetic_result_cannot_yield_pass_without_readiness() -> None:
    plan = ready_plan()
    verdict = benchmark.validate_result(plan, complete_result(plan))
    assert verdict["verdict"] == "fail"
    assert verdict["candidate_id"] == "espressif_libsodium"
    assert verdict["failures"] == ["candidate_readiness_not_verified"]


def test_candidate_order_placeholders_mcu_and_radio_ranges_fail_closed() -> None:
    plan = ready_plan()
    reordered = copy.deepcopy(plan)
    reordered["candidates"][0], reordered["candidates"][1] = (
        reordered["candidates"][1],
        reordered["candidates"][0],
    )
    expect_error(lambda: benchmark.validate_plan(reordered), "order")
    placeholder = copy.deepcopy(plan)
    placeholder["candidates"][0]["lock_sha256"] = "0" * 64
    expect_error(lambda: benchmark.validate_plan(placeholder), "placeholder")
    mcu = copy.deepcopy(plan)
    mcu["target"]["mcu"] = "ESP32"
    expect_error(lambda: benchmark.validate_plan(mcu), "ESP32-S3")
    radio = copy.deepcopy(plan)
    radio["radio"]["frequency_hz"] = 1
    expect_error(lambda: benchmark.validate_plan(radio), "integer in range")


def test_measured_failures_remain_visible_below_readiness_gate() -> None:
    plan = ready_plan()
    result = complete_result(plan)
    result["repetitions"]["cold"] = 99
    result["build"]["compiler_warnings"] = 1
    result["resources"]["watchdog_resets"] = 1
    result["evidence"]["raw_evidence_retained_privately"] = False
    result["gates"]["entropy_and_cold_start_uniqueness"] = False
    failures = benchmark.validate_result(plan, result)["failures"]
    assert "candidate_readiness_not_verified" in failures
    assert "insufficient_cold_repetitions" in failures
    assert "compiler_warnings_observed" in failures
    assert "watchdog_reset_observed" in failures
    assert "raw_evidence_not_retained" in failures
    assert "gate_failed:entropy_and_cold_start_uniqueness" in failures


def test_plan_hash_and_sdkconfig_mismatch_fail_closed() -> None:
    plan = ready_plan()
    wrong_hash = complete_result(plan)
    wrong_hash["plan_sha256"] = "0" * 64
    expect_error(lambda: benchmark.validate_result(plan, wrong_hash), "plan_sha256")
    wrong_config = complete_result(plan)
    wrong_config["evidence"]["sdkconfig_sha256"] = "9" * 64
    expect_error(lambda: benchmark.validate_result(plan, wrong_config), "sdkconfig")


def test_private_machine_and_device_text_is_rejected() -> None:
    plan = ready_plan()
    private_plan = copy.deepcopy(plan)
    private_plan["target"]["board_revision"] = (
        "C:" + "\\Users\\person\\board.txt"
    )
    expect_error(
        lambda: benchmark.validate_plan(private_plan), "private machine/device"
    )
    result = complete_result(plan)
    result["notes"] = "captured on COM17"
    expect_error(
        lambda: benchmark.validate_result(plan, result), "private machine/device"
    )


def test_exact_types_cycles_depth_and_loader_limits_are_enforced() -> None:
    plan = ready_plan()

    class Text(str):
        pass

    subtype = copy.deepcopy(plan)
    subtype["status"] = Text("ready")
    expect_error(lambda: benchmark.validate_plan(subtype), "noncanonical JSON type")
    integer = copy.deepcopy(plan)
    integer["target"]["flash_bytes"] = True
    expect_error(lambda: benchmark.validate_plan(integer), "integer in range")
    cyclic = copy.deepcopy(plan)
    cyclic["cycle"] = cyclic
    expect_error(lambda: benchmark.validate_plan(cyclic), "cycle")
    deep: dict = {}
    cursor = deep
    for _ in range(benchmark.MAX_DEPTH + 2):
        cursor["next"] = {}
        cursor = cursor["next"]
    expect_error(lambda: benchmark.validate_plan(deep), "structural bounds")
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        duplicate = root / "duplicate.json"
        duplicate.write_text('{"schema":"OTCB0","schema":"OTCB0"}', encoding="utf-8")
        expect_error(lambda: benchmark._load(duplicate), "duplicate key")
        oversized = root / "oversized.json"
        oversized.write_bytes(b"x" * (benchmark.MAX_BYTES + 1))
        expect_error(lambda: benchmark._load(oversized), "size limit")


def test_cli_errors_are_sanitized_and_template_write_is_exclusive() -> None:
    hostile = subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools" / "crypto_benchmark.py"),
            "--private=C:" + "\\Users\\person\\secret.json",
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    assert hostile.returncode == 2 and hostile.stdout == ""
    assert hostile.stderr.strip() == "ERROR: invalid command line"
    assert "Users" not in hostile.stderr and "Traceback" not in hostile.stderr
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "exclusive.json"
        benchmark._write_new(output, {"safe": True})
        try:
            benchmark._write_new(output, {"safe": True})
        except FileExistsError:
            pass
        else:
            raise AssertionError("template overwrite was not refused")


def main() -> int:
    tests = (
        test_blocked_public_plan_is_exact_valid_and_ineligible,
        test_declared_ready_v0_is_structural_only_without_accepted_readiness,
        test_blocked_readiness_cannot_be_reused_for_declared_ready_plan,
        test_complete_synthetic_result_cannot_yield_pass_without_readiness,
        test_candidate_order_placeholders_mcu_and_radio_ranges_fail_closed,
        test_measured_failures_remain_visible_below_readiness_gate,
        test_plan_hash_and_sdkconfig_mismatch_fail_closed,
        test_private_machine_and_device_text_is_rejected,
        test_exact_types_cycles_depth_and_loader_limits_are_enforced,
        test_cli_errors_are_sanitized_and_template_write_is_exclusive,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} crypto benchmark evidence scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
