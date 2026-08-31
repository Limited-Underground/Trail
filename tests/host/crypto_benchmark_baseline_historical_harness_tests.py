#!/usr/bin/env python3
"""Tamper tests for the strict OT-093 historical successor gate."""

from __future__ import annotations

import copy
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/host"))
import crypto_benchmark_baseline_historical_tests as harness  # noqa: E402


def rejects(action, fragment: str) -> None:
    try:
        action()
    except harness.HarnessError as exc:
        assert fragment in str(exc), str(exc)
        return
    raise AssertionError(f"expected HarnessError containing {fragment!r}")


def test_exact_record_frozen_files_and_test_discovery() -> None:
    value = harness.load_record()
    harness.validate_record(value)
    harness.verify_frozen_files(value)
    with harness.historical_checkout() as checkout:
        module = harness._load_original_tests(Path(checkout))
    discovered = {
        name for name, candidate in vars(module).items()
        if name.startswith("test_") and callable(candidate)
    }
    assert discovered == set(harness.ORIGINAL_TESTS)
    assert len(harness.ORIGINAL_TESTS) == 13
    assert len(harness.EXECUTED_TESTS) == 11
    assert set(harness.SKIPPED_TESTS) == {
        "test_source_toolchain_and_generated_defaults_are_bound",
        "test_firmware_input_manifest_is_current_and_ordered",
    }
    assert len(harness.REPLACEMENT_TESTS) == 2


def test_frozen_suite_and_commit_tampering_is_rejected() -> None:
    original = harness.load_record()
    for index in range(5):
        value = copy.deepcopy(original)
        value["frozen_suite"]["raw_files"][index]["sha256"] = "0" * 64
        rejects(lambda value=value: harness.validate_record(value), "frozen suite")
    for field, bad in (
        ("firmware_base_commit", "0" * 40),
        ("ot093_acceptance_commit", harness.ACCEPTANCE_COMMIT.upper()),
        ("acceptance_parent_commit", "abc"),
    ):
        value = copy.deepcopy(original)
        value["source_snapshot"][field] = bad
        rejects(lambda value=value: harness.validate_record(value), "source snapshot")


def test_historical_limitation_and_input_locks_are_fail_closed() -> None:
    original = harness.load_record()
    value = copy.deepcopy(original)
    value["source_snapshot"]["checkout_manifest_sha256"] = "0" * 64
    rejects(lambda: harness.validate_record(value), "source snapshot")
    value = copy.deepcopy(original)
    value["source_snapshot"]["checkout_manifest_sha256"] = harness.HISTORICAL_SHA256
    rejects(lambda: harness.validate_record(value), "source snapshot")
    value = copy.deepcopy(original)
    value["source_snapshot"]["historical_working_manifest_git_reconstructible"] = True
    rejects(lambda: harness.validate_record(value), "source snapshot")
    value = copy.deepcopy(original)
    value["policy"]["checkout_digest_equivalent_to_historical_digest"] = True
    rejects(lambda: harness.validate_record(value), "policy")
    for field in ("target_contract", "sdkconfig_defaults", "build_helper"):
        value = copy.deepcopy(original)
        value["inputs"][field]["canonical_sha256"] = "0" * 64
        rejects(lambda value=value: harness.validate_record(value), "input lock")


def test_reconstruction_structure_tampering_is_rejected() -> None:
    original = harness.load_record()
    cases = []
    value = copy.deepcopy(original)
    value.pop("status")
    rejects(lambda: harness.validate_record(value), "record shape")
    value = copy.deepcopy(original)
    value["extra"] = False
    rejects(lambda: harness.validate_record(value), "record shape")
    value = copy.deepcopy(original)
    value["reconstruction"]["entries"].pop()
    cases.append((value, "count"))
    value = copy.deepcopy(original)
    value["reconstruction"]["entries"].append(copy.deepcopy(value["reconstruction"]["entries"][-1]))
    cases.append((value, "count"))
    value = copy.deepcopy(original)
    value["reconstruction"]["entries"][0], value["reconstruction"]["entries"][1] = value["reconstruction"]["entries"][1], value["reconstruction"]["entries"][0]
    cases.append((value, "order"))
    value = copy.deepcopy(original)
    value["reconstruction"]["entries"][1] = copy.deepcopy(value["reconstruction"]["entries"][0])
    cases.append((value, "order"))
    for field, bad, fragment in (
        ("path", "../escape", "path"),
        ("path", "C:/escape", "path"),
        ("mode", "120000", "mode"),
        ("checkout_transform", "autocrlf", "transform"),
    ):
        value = copy.deepcopy(original)
        value["reconstruction"]["entries"][0][field] = bad
        cases.append((value, fragment))
    for value, fragment in cases:
        rejects(lambda value=value: harness.validate_record(value), fragment)
    value = copy.deepcopy(original)
    value["reconstruction"]["entries"][0]["blob_oid"] = "0" * 40
    rejects(lambda: harness.verify_historical_manifest(value), "reconstruction tree")
    value = copy.deepcopy(original)
    value["reconstruction"]["entries"][0]["checkout_raw_sha256"] = "0" * 64
    rejects(lambda: harness.verify_historical_manifest(value), "transform hash")


def test_blob_transforms_and_git_errors_are_sanitized() -> None:
    value = harness.load_record()
    identity = next(item for item in value["reconstruction"]["entries"] if item["checkout_transform"] == "identity")
    converted = next(item for item in value["reconstruction"]["entries"] if item["checkout_transform"] == "lf-to-crlf-v1")
    for item in (identity, converted):
        blob = harness._git_bytes("cat-file", "blob", item["blob_oid"])
        raw = harness._checkout_transform(blob, item["checkout_transform"])
        assert harness._sha256(raw) == item["checkout_raw_sha256"]
    rejects(lambda: harness._checkout_transform(b"bad\rinput\n", "lf-to-crlf-v1"), "transform")

    original_run = subprocess.run
    seen = {}

    def failed_run(*args, **kwargs):
        seen.update(kwargs["env"])
        return subprocess.CompletedProcess(args[0], 1, b"", b"C:\\Users\\private\\secret")

    subprocess.run = failed_run
    try:
        rejects(lambda: harness._git_bytes("cat-file", "-t", harness.BASE_COMMIT), "Git operation failed")
    finally:
        subprocess.run = original_run
    assert seen["GIT_NO_REPLACE_OBJECTS"] == "1"


def main() -> int:
    tests = (
        test_exact_record_frozen_files_and_test_discovery,
        test_frozen_suite_and_commit_tampering_is_rejected,
        test_historical_limitation_and_input_locks_are_fail_closed,
        test_reconstruction_structure_tampering_is_rejected,
        test_blob_transforms_and_git_errors_are_sanitized,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} OT-093 historical-successor tamper groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
