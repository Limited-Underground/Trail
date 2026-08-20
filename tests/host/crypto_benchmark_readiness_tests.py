#!/usr/bin/env python3
"""Adversarial tests for the host-only OTCBR0 readiness boundary."""

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

import crypto_benchmark as otcb0  # noqa: E402
import crypto_benchmark_baseline as otcb_l0  # noqa: E402
import crypto_benchmark_readiness as readiness  # noqa: E402


READINESS_PATH = (
    ROOT
    / "tests"
    / "benchmarks"
    / "crypto"
    / "OT-094-OT005-CANDIDATE-READINESS-V0.json"
)
PLAN_PATH = (
    ROOT / "tests" / "benchmarks" / "crypto" / "OT-005-CRYPTO-BENCHMARK-PLAN-V0.json"
)
BASELINE_PATH = (
    ROOT
    / "tests"
    / "benchmarks"
    / "crypto"
    / "OT-093-OT005-BUILD-BASELINE-V0.json"
)
EXPECTED_SHA256 = "705b30693196e2f46d8bda7c17acb1e04d7b9092c4a3817286c14d189001b9d3"


def artifacts() -> tuple[dict, dict, dict]:
    return (
        readiness.load(READINESS_PATH),
        otcb0._load(PLAN_PATH),
        otcb_l0.load(BASELINE_PATH),
    )


def synthetic_ready_plan() -> dict:
    plan = otcb0._load(PLAN_PATH)
    plan["status"] = "ready"
    plan["target"] = {
        "manufacturer": "Synthetic fixture vendor",
        "board_model": "Synthetic fixture model",
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
        "sdkconfig_sha256": hashlib.sha256(b"resolved-fixture-config").hexdigest(),
    }
    plan["radio"] = {
        "mtu_bytes": 120,
        "frequency_hz": 910_525_000,
        "bandwidth_hz": 62_500,
        "spreading_factor": 7,
        "coding_rate_denominator": 5,
    }
    for candidate in plan["candidates"]:
        candidate["version"] = candidate["version"] or "4.1.0"
        candidate["source_commit"] = hashlib.sha1(
            f"source:{candidate['candidate_id']}".encode("ascii")
        ).hexdigest()
        candidate["lock_sha256"] = hashlib.sha256(
            f"lock:{candidate['candidate_id']}".encode("ascii")
        ).hexdigest()
    plan["blockers"] = []
    return plan


def structurally_resolved_readiness(plan: dict) -> dict:
    value = readiness.load(READINESS_PATH)
    value["status"] = readiness.READY_STATUS
    value["public_result"] = readiness.READY_PUBLIC_RESULT
    value["otcb0_snapshot"].update(
        {
            "plan_sha256": otcb0.canonical_sha256(plan),
            "plan_status": "ready",
            "execution_authorized": True,
        }
    )
    value["target_readiness"].update(
        {
            "state": "resolved",
            "manufacturer": plan["target"]["manufacturer"],
            "board_model": plan["target"]["board_model"],
            "exact_received_revision": plan["target"]["board_revision"],
            "rf_variant": "fixture-rf",
            "supported": True,
            "evidence_sha256": hashlib.sha256(b"target-evidence").hexdigest(),
        }
    )
    configuration = value["candidate_configuration"]
    configuration.update(
        {
            "state": "resolved",
            "final_common_sdkconfig_sha256": plan["toolchain"]["sdkconfig_sha256"],
            "evidence_sha256": hashlib.sha256(b"configuration-evidence").hexdigest(),
        }
    )
    for overlay in configuration["candidate_overlays"]:
        overlay["overlay_sha256"] = hashlib.sha256(
            f"overlay:{overlay['candidate_id']}".encode("ascii")
        ).hexdigest()
        overlay["generated_sdkconfig_sha256"] = plan["toolchain"][
            "sdkconfig_sha256"
        ]
    for candidate, plan_candidate in zip(value["candidates"], plan["candidates"]):
        candidate.update(
            {
                "observed_version": plan_candidate["version"],
                "source_state": "locked",
                "source_commit": plan_candidate["source_commit"],
                "dependency_lock_kind": "fixture-source-lock",
                "dependency_lock_sha256": plan_candidate["lock_sha256"],
                "source_evidence_sha256": hashlib.sha256(
                    f"source-evidence:{candidate['candidate_id']}".encode("ascii")
                ).hexdigest(),
                "imported": True,
                "benchmark_eligible": True,
            }
        )
    value["radio_readiness"].update(
        {
            "state": "resolved",
            "rf_variant": "fixture-rf",
            "region_code": "fixture-region",
            "frequency_hz": plan["radio"]["frequency_hz"],
            "bandwidth_hz": plan["radio"]["bandwidth_hz"],
            "spreading_factor": plan["radio"]["spreading_factor"],
            "coding_rate_denominator": plan["radio"][
                "coding_rate_denominator"
            ],
            "tx_power_dbm": 10,
            "preamble_symbols": 8,
            "explicit_header": True,
            "crc_enabled": True,
            "low_data_rate_optimization": False,
            "sync_word": 18,
            "direct_payload_ceiling_bytes": 255,
            "benchmark_mtu_bytes": plan["radio"]["mtu_bytes"],
            "evidence_sha256": hashlib.sha256(b"radio-evidence").hexdigest(),
        }
    )
    for requirement in value["readiness_requirements"]:
        requirement["state"] = "closed"
        requirement["closure_evidence_sha256"] = hashlib.sha256(
            f"closure:{requirement['blocker_id']}".encode("ascii")
        ).hexdigest()
    value["blockers"] = []
    ready_authority = {
        "dependency_acquisition_authorized",
        "candidate_import_authorized",
        "result_template_authorized",
        "benchmark_build_authorized",
        "benchmark_execution_authorized",
        "device_access_authorized",
        "radio_transmit_authorized",
        "key_or_entropy_operation_authorized",
    }
    for field in value["authority"]:
        value["authority"][field] = field in ready_authority
    ready_claims = {
        "ot005_candidate_imported",
        "radio_profile_selected",
        "supported_target",
        "hardware_or_device_accessed",
        "physical_evidence_added",
    }
    for field in value["claims"]:
        value["claims"][field] = field in ready_claims
    return value


def expect_error(action, contains: str) -> None:
    try:
        action()
    except (readiness.ValidationError, otcb_l0.ValidationError) as exc:
        assert contains in str(exc), (contains, str(exc))
        return
    raise AssertionError(f"expected validation failure containing {contains!r}")


def test_canonical_readiness_is_exactly_blocked_and_uncredited() -> None:
    value, plan, baseline = artifacts()
    result = readiness.validate(value, plan, baseline)
    assert result == {
        "schema": "OTCBR0",
        "version": 0,
        "readiness_id": "OT-094-OT005-CANDIDATE-READINESS-V0",
        "status": "readiness_blocked",
        "public_result": (
            "CANDIDATE-READINESS-CONTRACT-FROZEN-HOST-ONLY; "
            "OTCB0-EXECUTION-BLOCKED"
        ),
        "blocker_count": 6,
        "fully_resolved": False,
        "accepted_for_legacy_v0": False,
        "execution_authorized": False,
        "score_credit_added": False,
        "readiness_sha256": EXPECTED_SHA256,
    }
    assert value["blockers"] == list(readiness.BLOCKERS)
    assert not any(value["authority"].values())
    assert not any(value["claims"].values())
    assert readiness.ACCEPTED_READY_READINESS_SHA256 == frozenset()


def test_historical_plan_and_preselection_baseline_are_exactly_bound() -> None:
    value, plan, baseline = artifacts()
    assert value["otcb0_snapshot"]["plan_sha256"] == otcb0.canonical_sha256(plan)
    assert value["preselection_baseline"]["baseline_sha256"] == otcb_l0.canonical_sha256(baseline)
    changed_plan = copy.deepcopy(plan)
    changed_plan["created_utc"] = "2026-08-20T00:00:00Z"
    expect_error(
        lambda: readiness.validate(value, changed_plan, baseline),
        "plan digest",
    )
    changed_baseline = copy.deepcopy(baseline)
    changed_baseline["target"]["supported"] = True
    expect_error(
        lambda: readiness.validate(value, plan, changed_baseline),
        "target.supported",
    )


def test_target_and_configuration_cannot_claim_partial_closure() -> None:
    value, plan, baseline = artifacts()
    target = copy.deepcopy(value)
    target["target_readiness"]["exact_received_revision"] = "invented-revision"
    expect_error(
        lambda: readiness.validate(target, plan, baseline),
        "unsupported closure evidence",
    )
    configured = copy.deepcopy(value)
    configured["candidate_configuration"]["final_common_sdkconfig_sha256"] = "1a" * 32
    expect_error(
        lambda: readiness.validate(configured, plan, baseline),
        "closure evidence",
    )
    overlay = copy.deepcopy(value)
    overlay["candidate_configuration"]["candidate_overlays"][0]["overlay_sha256"] = "2b" * 32
    expect_error(
        lambda: readiness.validate(overlay, plan, baseline),
        "overlay contains closure evidence",
    )


def test_candidate_order_locks_and_mbedtls_observation_fail_closed() -> None:
    value, plan, baseline = artifacts()
    reordered = copy.deepcopy(value)
    reordered["candidates"][0], reordered["candidates"][1] = (
        reordered["candidates"][1],
        reordered["candidates"][0],
    )
    expect_error(
        lambda: readiness.validate(reordered, plan, baseline),
        "identity, role, or order",
    )
    forged_lock = copy.deepcopy(value)
    forged_lock["candidates"][0]["dependency_lock_kind"] = "invented-lock"
    forged_lock["candidates"][0]["dependency_lock_sha256"] = "3c" * 32
    expect_error(
        lambda: readiness.validate(forged_lock, plan, baseline),
        "unsupported lock evidence",
    )
    mbedtls = copy.deepcopy(value)
    mbedtls["candidates"][1]["observed_version"] = "4.1.1"
    expect_error(
        lambda: readiness.validate(mbedtls, plan, baseline),
        "observation mismatch",
    )


def test_radio_history_or_example_values_do_not_become_readiness() -> None:
    value, plan, baseline = artifacts()
    radio = copy.deepcopy(value)
    radio["radio_readiness"].update(
        {
            "region_code": "historical-example",
            "frequency_hz": 910_525_000,
            "bandwidth_hz": 62_500,
            "spreading_factor": 7,
            "coding_rate_denominator": 5,
            "benchmark_mtu_bytes": 163,
        }
    )
    expect_error(
        lambda: readiness.validate(radio, plan, baseline),
        "only null evidence",
    )


def test_requirements_blockers_authority_and_claims_are_coherent() -> None:
    value, plan, baseline = artifacts()
    closed = copy.deepcopy(value)
    closed["readiness_requirements"][0]["state"] = "closed"
    expect_error(
        lambda: readiness.validate(closed, plan, baseline),
        "contains closure evidence",
    )
    needs = copy.deepcopy(value)
    needs["readiness_requirements"][0]["requires_physical_evidence"] = False
    expect_error(
        lambda: readiness.validate(needs, plan, baseline),
        "authority/evidence needs mismatch",
    )
    missing = copy.deepcopy(value)
    missing["blockers"].pop()
    expect_error(
        lambda: readiness.validate(missing, plan, baseline),
        "exact six blockers",
    )
    authority = copy.deepcopy(value)
    authority["authority"]["candidate_import_authorized"] = True
    expect_error(lambda: readiness.validate(authority, plan, baseline), "must be false")
    claim = copy.deepcopy(value)
    claim["claims"]["supported_target"] = True
    expect_error(lambda: readiness.validate(claim, plan, baseline), "must be false")


def test_legacy_declared_ready_plan_cannot_reuse_blocked_readiness() -> None:
    value, plan, baseline = artifacts()
    declared_ready = copy.deepcopy(plan)
    declared_ready["status"] = "ready"
    declared_ready["target"] = {
        "manufacturer": "Synthetic fixture vendor",
        "board_model": "Synthetic fixture model",
        "board_revision": "fixture-revision",
        "mcu": "ESP32-S3",
        "flash_bytes": 16 * 1024 * 1024,
        "psram_bytes": 2 * 1024 * 1024,
    }
    declared_ready["toolchain"] = {
        "esp_idf_version": "v6.0.2",
        "esp_idf_commit": "7101770dc6db2667b3c477cc31365dd1acd6db4e",
        "compiler": "xtensa-esp32s3-elf-gcc",
        "compiler_version": "15.2.0",
        "sdkconfig_sha256": "0123456789abcdef" * 4,
    }
    declared_ready["radio"] = {
        "mtu_bytes": 120,
        "frequency_hz": 910_525_000,
        "bandwidth_hz": 62_500,
        "spreading_factor": 7,
        "coding_rate_denominator": 5,
    }
    commits = (
        "0123456789abcdef0123456789abcdef01234567",
        "123456789abcdef0123456789abcdef012345678",
        "23456789abcdef0123456789abcdef0123456789",
    )
    locks = (
        "3456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef012",
        "456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123",
        "56789abcdef0123456789abcdef0123456789abcdef0123456789abcdef01234",
    )
    for candidate, source_commit, lock_sha in zip(
        declared_ready["candidates"], commits, locks
    ):
        candidate["version"] = candidate["version"] or "4.1.0"
        candidate["source_commit"] = source_commit
        candidate["lock_sha256"] = lock_sha
    declared_ready["blockers"] = []
    info = otcb0.validate_plan(declared_ready)
    assert info["status"] == "ready"
    assert info["readiness_verified"] is False
    assert info["execution_authorized"] is False
    expect_error(
        lambda: readiness.validate(value, declared_ready, baseline),
        "plan digest",
    )


def test_structurally_resolved_but_unaccepted_digest_is_rejected() -> None:
    plan = synthetic_ready_plan()
    value = structurally_resolved_readiness(plan)
    _, _, baseline = artifacts()
    expect_error(
        lambda: readiness.validate(value, plan, baseline),
        "digest is not independently accepted",
    )
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        plan_path = root / "ready-plan.json"
        readiness_path = root / "resolved-readiness.json"
        plan_path.write_text(json.dumps(plan), encoding="utf-8")
        readiness_path.write_text(json.dumps(value), encoding="utf-8")
        completed = subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" / "crypto_benchmark_readiness.py"),
                "--readiness",
                str(readiness_path),
                "--plan",
                str(plan_path),
                "--baseline",
                str(BASELINE_PATH),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        assert completed.returncode == 2 and completed.stdout == ""
        assert completed.stderr.strip() == (
            "ERROR: candidate readiness is invalid or unaccepted"
        )
        assert readiness.READY_PUBLIC_RESULT not in completed.stderr


def test_exact_types_cycles_depth_and_oversized_values_are_rejected() -> None:
    value, plan, baseline = artifacts()

    class Text(str):
        pass

    subtype = copy.deepcopy(value)
    subtype["status"] = Text("readiness_blocked")
    expect_error(
        lambda: readiness.validate(subtype, plan, baseline),
        "noncanonical JSON type",
    )
    integer = copy.deepcopy(value)
    integer["target_readiness"]["flash_bytes"] = True
    expect_error(lambda: readiness.validate(integer, plan, baseline), "integer in range")
    cyclic = copy.deepcopy(value)
    cyclic["cycle"] = cyclic
    expect_error(lambda: readiness.validate(cyclic, plan, baseline), "cycle")
    deep: dict = {}
    cursor = deep
    for _ in range(readiness.MAX_DEPTH + 2):
        cursor["next"] = {}
        cursor = cursor["next"]
    expect_error(lambda: readiness.validate(deep, plan, baseline), "structural bounds")


def test_loader_rejects_duplicates_invalid_depth_and_oversized_input() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        duplicate = root / "duplicate.json"
        duplicate.write_text('{"schema":"OTCBR0","schema":"OTCBR0"}', encoding="utf-8")
        expect_error(lambda: readiness.load(duplicate), "duplicate key")
        malformed = root / "malformed.json"
        malformed.write_text("[", encoding="utf-8")
        expect_error(lambda: readiness.load(malformed), "unreadable or invalid")
        oversized = root / "oversized.json"
        oversized.write_bytes(b"x" * (readiness.MAX_BYTES + 1))
        expect_error(lambda: readiness.load(oversized), "size limit")


def test_private_fields_and_machine_text_are_rejected() -> None:
    value, plan, baseline = artifacts()
    private = copy.deepcopy(value)
    private["target_readiness"]["processor_revision"] = "C:" + "\\Users\\operator\\capture.txt"
    expect_error(
        lambda: readiness.validate(private, plan, baseline),
        "private machine or device text",
    )
    secret = copy.deepcopy(value)
    secret["public_result"] = "secret=do-not-publish"
    expect_error(
        lambda: readiness.validate(secret, plan, baseline),
        "private machine or device text",
    )


def test_cli_is_sanitized_and_emits_only_public_result() -> None:
    completed = subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools" / "crypto_benchmark_readiness.py"),
            "--readiness",
            str(READINESS_PATH),
            "--plan",
            str(PLAN_PATH),
            "--baseline",
            str(BASELINE_PATH),
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 0, completed.stderr
    payload = json.loads(completed.stdout)
    assert payload["readiness_sha256"] == EXPECTED_SHA256
    assert payload["execution_authorized"] is False
    hostile = subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools" / "crypto_benchmark_readiness.py"),
            "--private=C:" + "\\Users\\operator\\secret.json",
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    assert hostile.returncode == 2 and hostile.stdout == ""
    assert hostile.stderr.strip() == "ERROR: invalid command line"
    assert "Users" not in hostile.stderr and "Traceback" not in hostile.stderr


def test_validator_has_no_execution_or_dependency_acquisition_capability() -> None:
    source = (ROOT / "tools" / "crypto_benchmark_readiness.py").read_text(
        encoding="utf-8"
    )
    for token in (
        "import socket",
        "import requests",
        "import urllib",
        "import subprocess",
        "os.system",
        "Start-Process",
    ):
        assert token not in source
    assert "ACCEPTED_READY_READINESS_SHA256: frozenset[str] = frozenset()" in source


def main() -> int:
    tests = (
        test_canonical_readiness_is_exactly_blocked_and_uncredited,
        test_historical_plan_and_preselection_baseline_are_exactly_bound,
        test_target_and_configuration_cannot_claim_partial_closure,
        test_candidate_order_locks_and_mbedtls_observation_fail_closed,
        test_radio_history_or_example_values_do_not_become_readiness,
        test_requirements_blockers_authority_and_claims_are_coherent,
        test_legacy_declared_ready_plan_cannot_reuse_blocked_readiness,
        test_structurally_resolved_but_unaccepted_digest_is_rejected,
        test_exact_types_cycles_depth_and_oversized_values_are_rejected,
        test_loader_rejects_duplicates_invalid_depth_and_oversized_input,
        test_private_fields_and_machine_text_are_rejected,
        test_cli_is_sanitized_and_emits_only_public_result,
        test_validator_has_no_execution_or_dependency_acquisition_capability,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} OTCBR0 candidate-readiness scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
