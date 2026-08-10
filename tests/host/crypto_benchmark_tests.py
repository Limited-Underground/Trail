#!/usr/bin/env python3
"""Deterministic tests for the OTCB0 crypto benchmark evidence boundary."""

from __future__ import annotations

import copy
import json
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import crypto_benchmark as benchmark  # noqa: E402


PLAN_PATH = (
    ROOT
    / "tests"
    / "benchmarks"
    / "crypto"
    / "OT-005-CRYPTO-BENCHMARK-PLAN-V0.json"
)
HEX40 = "1" * 40
HEX64 = "2" * 64


def expect_error(action, contains: str) -> None:
    try:
        action()
    except benchmark.ValidationError as exc:
        assert contains in str(exc), (contains, str(exc))
        return
    raise AssertionError(f"expected ValidationError containing {contains!r}")


def blocked_plan() -> dict:
    return json.loads(PLAN_PATH.read_text(encoding="utf-8"))


def ready_plan() -> dict:
    plan = blocked_plan()
    plan["status"] = "ready"
    plan["target"] = {
        "manufacturer": "Example Board Vendor",
        "board_model": "Frozen ESP32-S3 Client",
        "board_revision": "rev-a",
        "mcu": "ESP32-S3",
        "flash_bytes": 16 * 1024 * 1024,
        "psram_bytes": 2 * 1024 * 1024,
    }
    plan["toolchain"] = {
        "esp_idf_version": "v5.5.1",
        "esp_idf_commit": HEX40,
        "compiler": "xtensa-esp-elf-gcc",
        "compiler_version": "14.2.0",
        "sdkconfig_sha256": HEX64,
    }
    plan["radio"] = {
        "mtu_bytes": 120,
        "frequency_hz": 910_525_000,
        "bandwidth_hz": 62_500,
        "spreading_factor": 7,
        "coding_rate_denominator": 5,
    }
    for index, candidate in enumerate(plan["candidates"], start=3):
        if not candidate["version"]:
            candidate["version"] = "pinned-with-idf"
        candidate["source_commit"] = format(index, "x") * 40
        candidate["lock_sha256"] = format(index + 3, "x") * 64
    plan["blockers"] = []
    return plan


def passing_result(plan: dict) -> dict:
    result = benchmark.result_template(plan, "espressif_libsodium")
    result["completed_utc"] = "2026-08-10T09:00:00Z"
    result["repetitions"] = {"cold": 100, "warm": 100}
    for index, operation in enumerate(benchmark.OPERATIONS, start=1):
        result["timings"][operation]["cold"] = {
            "min_us": index,
            "median_us": index + 1,
            "p95_us": index + 2,
            "max_us": index + 3,
        }
        result["timings"][operation]["warm"] = {
            "min_us": index,
            "median_us": index,
            "p95_us": index + 1,
            "max_us": index + 2,
        }
    result["build"] = {"passed": True, "compiler_warnings": 0}
    result["resources"] = {
        "linked_flash_delta_bytes": 123_456,
        "static_ram_bytes": 4_096,
        "peak_dynamic_ram_bytes": 8_192,
        "max_stack_used_bytes": 3_072,
        "watchdog_resets": 0,
    }
    result["radio_cost"] = {
        "handshake_bytes": 384,
        "fragments": 4,
        "airtime_us": 500_000,
        "retries_tested": 3,
    }
    result["evidence"] = {
        "binary_sha256": "7" * 64,
        "sdkconfig_sha256": plan["toolchain"]["sdkconfig_sha256"],
        "sbom_sha256": "8" * 64,
        "raw_evidence_retained_privately": True,
    }
    result["gates"] = {gate: True for gate in benchmark.GATES}
    result["notes"] = "Aggregate public result; raw traces retained privately."
    return result


def test_blocked_public_plan_is_valid_but_ineligible() -> None:
    plan = blocked_plan()
    info = benchmark.validate_plan(plan)
    assert info["status"] == "draft_blocked"
    assert len(info["plan_sha256"]) == 64
    expect_error(
        lambda: benchmark.result_template(plan, "espressif_libsodium"),
        "requires a ready plan",
    )


def test_ready_plan_requires_exact_candidate_and_gate_sets() -> None:
    plan = ready_plan()
    assert benchmark.validate_plan(plan)["status"] == "ready"
    duplicate = copy.deepcopy(plan)
    duplicate["candidates"][2]["candidate_id"] = "espressif_libsodium"
    expect_error(lambda: benchmark.validate_plan(duplicate), "invalid or duplicated")
    reordered = copy.deepcopy(plan)
    reordered["required_gates"] = list(reversed(reordered["required_gates"]))
    expect_error(lambda: benchmark.validate_plan(reordered), "canonical ordered")


def test_template_binds_plan_and_refuses_overwrite() -> None:
    plan = ready_plan()
    template = benchmark.result_template(plan, "monocypher")
    assert template["plan_sha256"] == benchmark.canonical_sha256(plan)
    assert not template["build"]["passed"]
    assert not any(template["gates"].values())
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "result.json"
        benchmark._write_new(output, template)
        try:
            benchmark._write_new(output, template)
        except FileExistsError:
            pass
        else:
            raise AssertionError("template overwrite was not refused")


def test_complete_result_passes_without_selecting_a_library() -> None:
    plan = ready_plan()
    verdict = benchmark.validate_result(plan, passing_result(plan))
    assert verdict["verdict"] == "pass"
    assert verdict["candidate_id"] == "espressif_libsodium"
    assert verdict["failures"] == []


def test_measured_failures_cannot_be_reported_as_pass() -> None:
    plan = ready_plan()
    result = passing_result(plan)
    result["repetitions"]["cold"] = 99
    result["build"]["compiler_warnings"] = 1
    result["resources"]["watchdog_resets"] = 1
    result["evidence"]["raw_evidence_retained_privately"] = False
    result["gates"]["entropy_and_cold_start_uniqueness"] = False
    failures = benchmark.validate_result(plan, result)["failures"]
    assert "insufficient_cold_repetitions" in failures
    assert "compiler_warnings_observed" in failures
    assert "watchdog_reset_observed" in failures
    assert "raw_evidence_not_retained" in failures
    assert "gate_failed:entropy_and_cold_start_uniqueness" in failures


def test_plan_hash_and_sdkconfig_mismatch_fail_closed() -> None:
    plan = ready_plan()
    wrong_hash = passing_result(plan)
    wrong_hash["plan_sha256"] = "0" * 64
    expect_error(lambda: benchmark.validate_result(plan, wrong_hash), "plan_sha256")
    wrong_config = passing_result(plan)
    wrong_config["evidence"]["sdkconfig_sha256"] = "9" * 64
    expect_error(lambda: benchmark.validate_result(plan, wrong_config), "sdkconfig")


def test_private_machine_and_device_text_is_rejected() -> None:
    plan = ready_plan()
    private_plan = copy.deepcopy(plan)
    private_plan["target"]["board_revision"] = "C:\\Users\\person\\board.txt"
    expect_error(lambda: benchmark.validate_plan(private_plan), "private machine/device")
    result = passing_result(plan)
    result["notes"] = "captured on COM17"
    expect_error(lambda: benchmark.validate_result(plan, result), "private machine/device")


def test_incomplete_or_noncanonical_measurements_are_invalid() -> None:
    plan = ready_plan()
    incomplete = benchmark.result_template(plan, "espressif_libsodium")
    expect_error(lambda: benchmark.validate_result(plan, incomplete), "nonempty string")
    result = passing_result(plan)
    result["timings"]["x25519"]["warm"]["p95_us"] = 1
    result["timings"]["x25519"]["warm"]["max_us"] = 0
    expect_error(lambda: benchmark.validate_result(plan, result), "integer >= 1")


def main() -> int:
    tests = (
        test_blocked_public_plan_is_valid_but_ineligible,
        test_ready_plan_requires_exact_candidate_and_gate_sets,
        test_template_binds_plan_and_refuses_overwrite,
        test_complete_result_passes_without_selecting_a_library,
        test_measured_failures_cannot_be_reported_as_pass,
        test_plan_hash_and_sdkconfig_mismatch_fail_closed,
        test_private_machine_and_device_text_is_rejected,
        test_incomplete_or_noncanonical_measurements_are_invalid,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} crypto benchmark evidence scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
