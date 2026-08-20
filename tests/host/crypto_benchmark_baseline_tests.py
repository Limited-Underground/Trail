#!/usr/bin/env python3
"""Deterministic governance tests for the OTCBL0 two-run build lock."""

from __future__ import annotations

import copy
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import crypto_benchmark as otcb0  # noqa: E402
import crypto_benchmark_baseline as baseline  # noqa: E402


BASELINE_PATH = (
    ROOT / "tests" / "benchmarks" / "crypto" / "OT-093-OT005-BUILD-BASELINE-V0.json"
)
OTCB0_PATH = (
    ROOT / "tests" / "benchmarks" / "crypto" / "OT-005-CRYPTO-BENCHMARK-PLAN-V0.json"
)
BUILD_HELPER = ROOT / "tools" / "Build-HeltecV4BenchTarget.ps1"


def checked() -> dict:
    return json.loads(BASELINE_PATH.read_text(encoding="utf-8"))


def expect_error(action, contains: str) -> None:
    try:
        action()
    except baseline.ValidationError as exc:
        assert contains in str(exc), (contains, str(exc))
        return
    raise AssertionError(f"expected ValidationError containing {contains!r}")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical_text_sha256(path: Path) -> str:
    text = path.read_bytes().decode("utf-8")
    text = text.replace("\r\n", "\n")
    assert "\r" not in text and not text.startswith("\ufeff")
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def test_accepted_build_lock_is_exact_and_still_blocked() -> None:
    value = checked()
    result = baseline.validate(value)
    assert result["schema"] == "OTCBL0"
    if value["status"] == baseline.PENDING_STATUS:
        assert result["status"] == baseline.PENDING_STATUS
        assert result["public_result"] == baseline.PENDING_PUBLIC_RESULT
        assert result["ordered_artifacts_equal"] is False
        assert result["normalized_receipts_equal"] is False
        assert result["normalized_receipt_sha256"] is None
    else:
        assert result["status"] == baseline.FROZEN_STATUS
        assert result["public_result"] == baseline.FROZEN_PUBLIC_RESULT
        assert result["ordered_artifacts_equal"] is True
        assert result["normalized_receipts_equal"] is True
    assert result["otcb0_status"] == "draft_blocked"
    assert result["execution_authorized"] is False
    assert result["score_credit_added"] is False
    assert value["historical_otcb0_plan_blockers"] == list(baseline.BLOCKERS)
    assert "REMAINS-UNRESOLVED" in value["otcb0_plan_blockers_applicability"]
    assert len(result["baseline_sha256"]) == 64


def test_two_clean_cache_disabled_receipts_are_seven_artifact_equal() -> None:
    value = checked()
    evidence = value["build_reproducibility"]
    assert evidence["independent_build_directories"] is True
    assert evidence["shared_compiler_cache_disabled"] is True
    assert evidence["reproducible_build_paths_normalized"] is True
    runs = evidence["runs"]
    if value["status"] == baseline.PENDING_STATUS:
        assert evidence["clean_run_count"] == 0
        assert runs == []
        assert all(item is None for item in value["app_slot_headroom"].values())
        return
    assert evidence["clean_run_count"] == 2
    assert [run["profile"] for run in runs] == ["ot093-a", "ot093-b"]
    assert [run["build_exit_code"] for run in runs] == [0, 0]
    assert [run["compiler_warning_count"] for run in runs] == [0, 0]
    assert [run["raw_build_evidence_sha256"] for run in runs] == list(
        baseline.EXPECTED_RAW_BUILD_EVIDENCE_SHA256
    )
    artifact_sets = []
    for run in runs:
        artifact_sets.append([(item["role"], item["name"], item["bytes"], item["sha256"]) for item in run["public_artifacts"]])
        assert all(set(item) == {"role", "name", "bytes", "sha256"} for item in run["public_artifacts"])
        assert run["normalized_receipt_sha256"] == baseline.canonical_sha256(
            baseline.normalized_receipt(value, run)
        )
    assert artifact_sets[0] == artifact_sets[1] == list(baseline.EXPECTED_ARTIFACTS)
    assert runs[0]["normalized_receipt_sha256"] == runs[1]["normalized_receipt_sha256"]


def test_source_toolchain_and_generated_defaults_are_bound() -> None:
    value = checked()
    inputs = value["inputs"]
    for field in ("target_contract", "sdkconfig_defaults", "build_helper"):
        assert canonical_text_sha256(ROOT / inputs[field]) == inputs[f"{field}_sha256"]
    assert inputs["reproducible_defaults_sha256"] == (
        "995ce0b6c1a557b0132208af3744fc6672b3a026719c47d1cd50580004373fa6"
    )
    assert value["toolchain"]["compiler_executable_sha256"] == (
        "20e70278d1fa041c1305e0e70e6f35dde01b7eb21f2c7bbc0013456493a011a5"
    )
    for field in ("cmake", "ninja", "python"):
        assert len(value["toolchain"][f"{field}_executable_sha256"]) == 64
    assert value["toolchain"]["idf_py_sha256"] == (
        "5f703be3a915433f63206a28260357ad807ec83ae0a8589c684c9c08516a7a40"
    )
    assert value["toolchain"]["independent_python_cache"] is True
    assert value["toolchain"]["python_user_site_disabled"] is True
    assert value["inputs"]["project_version"] == "ot093-precrypto-v0"
    assert value["inputs"]["text_digest_kind"] == baseline.TEXT_DIGEST_KIND
    if value["status"] == baseline.FROZEN_STATUS:
        sdkconfig = next(item for item in value["build_reproducibility"]["runs"][0]["public_artifacts"] if item["role"] == "generated_sdkconfig")
        assert sdkconfig["sha256"] == value["inputs"]["generated_sdkconfig_sha256"]
    assert value["inputs"]["generated_sdkconfig_role"] == (
        "PRE-SELECTION-BASELINE-NOT-FINAL-OTCB0"
    )
    assert value["inputs"]["future_candidate_builds_require_same_baseline_config"] is True


def test_firmware_input_manifest_is_current_and_ordered() -> None:
    powershell = shutil.which("pwsh") or shutil.which("powershell")
    assert powershell is not None
    completed = subprocess.run(
        [
            powershell,
            "-NoProfile",
            "-Command",
            (
                "& git ls-files --stage -- firmware/components firmware/targets/heltec_v4_bench"
            ),
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    entries = [line for line in completed.stdout.splitlines() if line]
    manifest_lines = []
    raw_manifest_lines = []
    for entry in entries:
        metadata, path = entry.split("\t", 1)
        mode, blob, stage = metadata.split(" ")
        assert stage == "0"
        manifest_lines.append(f"{mode} {blob} {path}")
        raw_manifest_lines.append(f"{sha256(ROOT / Path(path))} {path}")
    manifest = ("\n".join(manifest_lines) + "\n").encode("utf-8")
    source = checked()["source"]
    assert source["input_manifest_kind"] == "git-index-stage-zero-v1"
    assert len(entries) == source["input_manifest_file_count"] == 307
    assert hashlib.sha256(manifest).hexdigest() == source["input_manifest_sha256"]
    raw_manifest = ("\n".join(raw_manifest_lines) + "\n").encode("utf-8")
    assert source["working_tree_manifest_kind"] == "sha256-raw-bytes-path-v1"
    assert hashlib.sha256(raw_manifest).hexdigest() == source["working_tree_manifest_sha256"]
    autocrlf = subprocess.run(
        ["git", "config", "--get", "core.autocrlf"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip().lower()
    assert source["git_core_autocrlf"] == autocrlf == "true"


def test_headroom_equation_and_bounds_fail_closed() -> None:
    value = checked()
    headroom = value["app_slot_headroom"]
    if value["status"] == baseline.PENDING_STATUS:
        assert all(item is None for item in headroom.values())
        wrong = copy.deepcopy(value)
        wrong["app_slot_headroom"]["application_image_bytes"] = 1
        expect_error(lambda: baseline.validate(wrong), "pending headroom")
        return
    assert headroom["smallest_app_slot_bytes"] - headroom["application_image_bytes"] == (
        headroom["headroom_bytes"]
    )
    assert all(
        headroom["application_image_bytes"] == run["public_artifacts"][0]["bytes"]
        for run in value["build_reproducibility"]["runs"]
    )
    wrong = copy.deepcopy(value)
    wrong["app_slot_headroom"]["headroom_bytes"] += 1
    expect_error(lambda: baseline.validate(wrong), "headroom equation")
    oversized = copy.deepcopy(value)
    oversized["build_reproducibility"]["runs"][0]["public_artifacts"][0]["bytes"] = 33_554_433
    expect_error(lambda: baseline.validate(oversized), "integer in range")


def test_candidate_specific_claim_does_not_deny_framework_crypto() -> None:
    value = checked()
    assert value["claim_scope"] == (
        "NO-OT005-CANDIDATE-OR-SECURE-LORA-ADAPTER-IMPORTED-OR-EXECUTED; "
        "EXISTING-ESP-IDF-NIMBLE-CRYPTOGRAPHIC-OBJECTS-ARE-NOT-AN-OT005-SELECTION"
    )
    assert all(entry is False for entry in value["claims"].values())
    helper = BUILD_HELPER.read_text(encoding="utf-8").lower()
    for token in ("libsodium", "sodium_", "monocypher", "noise_xk", "secure_lora", "otsl0", "otcb0"):
        assert f"'{token}'" in helper
    assert "mbedtls" not in value["claim_scope"].lower()
    assert "all-crypto" not in value["claim_scope"].lower()


def test_historical_otcb0_plan_is_unchanged_and_ineligible() -> None:
    plan = json.loads(OTCB0_PATH.read_text(encoding="utf-8"))
    info = otcb0.validate_plan(plan)
    assert info["status"] == "draft_blocked"
    assert info["plan_sha256"] == (
        "49792b585286823ffa9b7589704d57e8393b3dbf3d514917ffd7b5970301edb7"
    )
    assert checked()["otcb0_gate"]["plan_sha256"] == info["plan_sha256"]
    try:
        otcb0.result_template(plan, "espressif_libsodium")
    except otcb0.ValidationError:
        pass
    else:
        raise AssertionError("blocked OTCB0 plan unexpectedly created a result template")


def test_exact_types_unknown_fields_cycles_and_depth_are_rejected() -> None:
    value = checked()
    unknown = copy.deepcopy(value)
    unknown["extra"] = False
    expect_error(lambda: baseline.validate(unknown), "exact canonical fields")

    class DictSubclass(dict):
        pass

    expect_error(lambda: baseline.validate(DictSubclass(value)), "noncanonical JSON type")
    boolean_integer = copy.deepcopy(value)
    if value["status"] == baseline.PENDING_STATUS:
        boolean_integer["build_reproducibility"]["clean_run_count"] = True
    else:
        boolean_integer["app_slot_headroom"]["application_image_bytes"] = True
    expect_error(lambda: baseline.validate(boolean_integer), "exact integer")
    root_version_boolean = copy.deepcopy(value)
    root_version_boolean["version"] = False
    expect_error(lambda: baseline.validate(root_version_boolean), "exact integer")
    gate_version_boolean = copy.deepcopy(value)
    gate_version_boolean["otcb0_gate"]["version"] = False
    expect_error(lambda: baseline.validate(gate_version_boolean), "exact integer")
    cycle: dict = {}
    cycle["cycle"] = cycle
    expect_error(lambda: baseline._bounded(cycle), "cycle")
    nested: object = "end"
    for _ in range(12):
        nested = [nested]
    expect_error(lambda: baseline._bounded(nested), "structural bounds")


def test_two_run_forgeries_partial_receipts_and_swaps_fail_closed() -> None:
    value = checked()
    if value["status"] == baseline.PENDING_STATUS:
        stale = copy.deepcopy(value)
        stale["build_reproducibility"]["runs"] = [{}]
        expect_error(lambda: baseline.validate(stale), "must not contain run receipts")
        false_frozen = copy.deepcopy(value)
        false_frozen["status"] = baseline.FROZEN_STATUS
        false_frozen["public_result"] = baseline.FROZEN_PUBLIC_RESULT
        expect_error(lambda: baseline.validate(false_frozen), "exact integer")
        wrong_result = copy.deepcopy(value)
        wrong_result["public_result"] = baseline.FROZEN_PUBLIC_RESULT
        expect_error(lambda: baseline.validate(wrong_result), "pending public_result")
        return
    missing_run = copy.deepcopy(value)
    missing_run["build_reproducibility"]["runs"].pop()
    expect_error(lambda: baseline.validate(missing_run), "exactly two")
    forged_helper = copy.deepcopy(value)
    forged_helper["inputs"]["build_helper_sha256"] = "0" * 64
    for run in forged_helper["build_reproducibility"]["runs"]:
        run["normalized_receipt_sha256"] = baseline.canonical_sha256(
            baseline.normalized_receipt(forged_helper, run)
        )
    expect_error(lambda: baseline.validate(forged_helper), "build_helper_sha256 mismatch")
    forged_artifact = copy.deepcopy(value)
    forged_artifact["build_reproducibility"]["runs"][0]["public_artifacts"][0]["bytes"] = 1
    forged_artifact["app_slot_headroom"]["application_image_bytes"] = 1
    forged_artifact["app_slot_headroom"]["headroom_bytes"] = 5_177_343
    forged_artifact["build_reproducibility"]["runs"][0]["normalized_receipt_sha256"] = baseline.canonical_sha256(
        baseline.normalized_receipt(forged_artifact, forged_artifact["build_reproducibility"]["runs"][0])
    )
    expect_error(lambda: baseline.validate(forged_artifact), "immutable tuple")
    swapped = copy.deepcopy(value)
    runs = swapped["build_reproducibility"]["runs"]
    runs[0]["raw_build_evidence_sha256"], runs[1]["raw_build_evidence_sha256"] = (
        runs[1]["raw_build_evidence_sha256"], runs[0]["raw_build_evidence_sha256"]
    )
    expect_error(lambda: baseline.validate(swapped), "raw build-evidence")
    unequal = copy.deepcopy(value)
    unequal["build_reproducibility"]["runs"][1]["public_artifacts"][6]["sha256"] = "0" * 64
    unequal["build_reproducibility"]["runs"][1]["normalized_receipt_sha256"] = baseline.canonical_sha256(
        baseline.normalized_receipt(unequal, unequal["build_reproducibility"]["runs"][1])
    )
    expect_error(lambda: baseline.validate(unequal), "immutable tuple")


def test_private_content_and_cli_errors_are_sanitized() -> None:
    value = checked()
    private = copy.deepcopy(value)
    private["claim_scope"] = "captured from C:" + "\\Users\\person\\trace.txt"
    expect_error(lambda: baseline.validate(private), "private")
    if value["status"] == baseline.FROZEN_STATUS:
        raw_map = copy.deepcopy(value)
        raw_map["build_reproducibility"]["runs"][0]["public_artifacts"][2]["content"] = "raw linker map"
        expect_error(lambda: baseline.validate(raw_map), "exact canonical fields")
    with tempfile.TemporaryDirectory() as directory:
        missing = Path(directory) / "C-Users-person-secret.json"
        completed = subprocess.run(
            [sys.executable, str(ROOT / "tools" / "crypto_benchmark_baseline.py"), str(missing)],
            capture_output=True,
            text=True,
        )
        assert completed.returncode == 2
        assert completed.stdout == ""
        assert completed.stderr.strip() == "ERROR: baseline JSON is unreadable or invalid"
        assert str(missing) not in completed.stderr
        for name, content in (
            ("deep-private.json", '{"value":' + "[" * 30000 + "0" + "]" * 30000 + "}"),
            ("huge-integer-private.json", '{"value":' + "9" * 10000 + "}"),
        ):
            malformed = Path(directory) / name
            malformed.write_text(content, encoding="utf-8")
            completed = subprocess.run(
                [sys.executable, str(ROOT / "tools" / "crypto_benchmark_baseline.py"), str(malformed)],
                capture_output=True,
                text=True,
            )
            assert completed.returncode == 2
            assert completed.stdout == ""
            assert completed.stderr.strip() == "ERROR: baseline JSON is unreadable or invalid"
            assert "Traceback" not in completed.stderr and str(malformed) not in completed.stderr
        oversized_file = Path(directory) / "oversized-private.json"
        oversized_file.write_bytes(b"x" * (baseline.MAX_BYTES + 1))
        completed = subprocess.run(
            [sys.executable, str(ROOT / "tools" / "crypto_benchmark_baseline.py"), str(oversized_file)],
            capture_output=True,
            text=True,
        )
        assert completed.returncode == 2 and completed.stdout == ""
        assert completed.stderr.strip() == "ERROR: baseline exceeds the size limit"
        assert str(oversized_file) not in completed.stderr
    completed = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "crypto_benchmark_baseline.py"), "--private=C:\\Users\\person"],
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 2
    assert completed.stderr.strip() == "ERROR: invalid command line"
    assert "Users" not in completed.stderr


def test_validator_and_build_helper_add_no_execution_authority() -> None:
    validator_source = (ROOT / "tools" / "crypto_benchmark_baseline.py").read_text(
        encoding="utf-8"
    )
    for token in ("import socket", "import requests", "import urllib", "import subprocess"):
        assert token not in validator_source
    for token in (".write_text(", ".write_bytes(", 'open("w', "open('w"):
        assert token not in validator_source
    helper = BUILD_HELPER.read_text(encoding="utf-8").lower()
    for token in ("idf.py flash", "write-flash", "erase-flash", "serialport", "esptool.py"):
        assert token not in helper
    assert "hardware_or_device_accessed = $false" in helper
    assert "key_or_entropy_operation = $false" in helper
    assert "score_credit_added = $false" in helper
    evidence_start = helper.index("$evidence = [ordered]@{")
    ot093_receipt = helper.index("$evidence['ot005_pre_selection_baseline']")
    assert "build-baseline-frozen" not in helper[evidence_start:ot093_receipt]
    denylist = helper.index("$forbiddenot005linktokens")
    scoped_guard = helper.rfind("if ($ot093cleanbaseline)", 0, denylist)
    assert scoped_guard != -1
    assert "@ot093projectversionargs" in helper


def test_ot093_environment_is_restored_after_success_and_failure() -> None:
    powershell = shutil.which("pwsh") or shutil.which("powershell")
    assert powershell is not None
    command = r'''
$ErrorActionPreference = 'Stop'
$names = @('CCACHE_DISABLE', 'PYTHONPYCACHEPREFIX', 'PYTHONNOUSERSITE')
[Environment]::SetEnvironmentVariable('CCACHE_DISABLE', 'sentinel-cache', 'Process')
[Environment]::SetEnvironmentVariable('PYTHONPYCACHEPREFIX', $null, 'Process')
[Environment]::SetEnvironmentVariable('PYTHONNOUSERSITE', 'sentinel-site', 'Process')
$before = @{}
foreach ($name in $names) {
    $before[$name] = @{
        Exists = Test-Path -LiteralPath "Env:$name"
        Value = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
}
function Assert-Restored {
    foreach ($name in $names) {
        $exists = Test-Path -LiteralPath "Env:$name"
        $value = [Environment]::GetEnvironmentVariable($name, 'Process')
        if ($exists -ne $before[$name].Exists -or $value -ne $before[$name].Value) {
            throw "environment restoration failed"
        }
    }
}
$preexistingCache = Join-Path $env:OT093_ROOT 'build\targets\ot093-python-cache-ot093-a'
if (Test-Path -LiteralPath $preexistingCache) { throw 'probe cache unexpectedly exists' }
[IO.Directory]::CreateDirectory($preexistingCache) | Out-Null
$sentinel = Join-Path $preexistingCache 'preexisting.sentinel'
[IO.File]::WriteAllText($sentinel, 'preserve')
try {
    & $env:OT093_HELPER_PATH -BuildProfile ot093-a -EnvironmentIsolationProbe success | Out-Null
    Assert-Restored
    if (-not (Test-Path -LiteralPath $sentinel -PathType Leaf)) {
        throw 'preexisting cache was deleted after success probe'
    }
    $failed = $false
    try {
        & $env:OT093_HELPER_PATH -BuildProfile ot093-b -EnvironmentIsolationProbe failure | Out-Null
    } catch {
        $failed = $true
    }
    if (-not $failed) { throw 'failure probe did not fail' }
    Assert-Restored
    if (-not (Test-Path -LiteralPath $sentinel -PathType Leaf)) {
        throw 'preexisting cache was deleted after failure probe'
    }
} finally {
    if (Test-Path -LiteralPath $preexistingCache) {
        Remove-Item -LiteralPath $preexistingCache -Recurse -Force
    }
}
'''
    completed = subprocess.run(
        [powershell, "-NoProfile", "-Command", command],
        cwd=ROOT,
        capture_output=True,
        text=True,
        env={
            **os.environ,
            "OT093_HELPER_PATH": str(BUILD_HELPER),
            "OT093_ROOT": str(ROOT),
        },
    )
    assert completed.returncode == 0, completed.stderr
    helper = BUILD_HELPER.read_text(encoding="utf-8")
    assert "finally {" in helper
    assert "OT-093-ENVIRONMENT-ISOLATION-PROBE-ONLY" in helper
    assert "$ot093PythonCacheOwnedByRun" in helper
    assert "$nestedReparsePoints.Count -ne 0" in helper


def main() -> int:
    tests = (
        test_accepted_build_lock_is_exact_and_still_blocked,
        test_two_clean_cache_disabled_receipts_are_seven_artifact_equal,
        test_source_toolchain_and_generated_defaults_are_bound,
        test_firmware_input_manifest_is_current_and_ordered,
        test_headroom_equation_and_bounds_fail_closed,
        test_candidate_specific_claim_does_not_deny_framework_crypto,
        test_historical_otcb0_plan_is_unchanged_and_ineligible,
        test_exact_types_unknown_fields_cycles_and_depth_are_rejected,
        test_two_run_forgeries_partial_receipts_and_swaps_fail_closed,
        test_private_content_and_cli_errors_are_sanitized,
        test_validator_and_build_helper_add_no_execution_authority,
        test_ot093_environment_is_restored_after_success_and_failure,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} OTCBL0 build-lock scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
