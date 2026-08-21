#!/usr/bin/env python3
"""Tamper and live-lock tests for OTFBL0/v0 OT-106 build evidence."""

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
import heltec_compact_footer_build_evidence as evidence  # noqa: E402

EVIDENCE = ROOT / "tests" / "benchmarks" / "display" / "OT-106-HELTEC-V4-COMPACT-FOOTER-BUILD-V0.json"
OT093_FILES = {
    "ot093_record_sha256": ROOT / "tests" / "benchmarks" / "crypto" / "OT-093-OT005-BUILD-BASELINE-V0.json",
    "ot093_helper_sha256": ROOT / "tools" / "Build-HeltecV4BenchTarget.ps1",
    "ot093_validator_sha256": ROOT / "tools" / "crypto_benchmark_baseline.py",
    "ot093_tests_sha256": ROOT / "tests" / "host" / "crypto_benchmark_baseline_tests.py",
    "ot093_evidence_note_sha256": ROOT / "tests" / "hardware" / "OT-093-2026-08-20.md",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked() -> dict:
    return evidence.load(EVIDENCE)


def rejects(value: dict, fragment: str) -> None:
    try:
        evidence.validate(value)
    except evidence.ValidationError as exc:
        assert fragment in str(exc), str(exc)
    else:
        raise AssertionError("tampered OTFBL0 evidence was accepted")


def test_accepted_exact_two_build_record() -> None:
    value = checked()
    assert value["record_id"] == evidence.RECORD_ID
    assert value["status"] == evidence.STATUS
    assert tuple(value["build_reproducibility"]["profiles"]) == evidence.PROFILES
    assert all(item is False for item in value["claims"].values())


def test_frozen_receipts_and_artifacts_are_exact() -> None:
    value = checked()
    for index, run in enumerate(value["build_reproducibility"]["runs"]):
        assert run["profile"] == evidence.PROFILES[index]
        assert run["raw_receipt_sha256"] == evidence.EXPECTED_RAW_RECEIPTS[index]
        assert run["normalized_receipt_sha256"] == evidence.EXPECTED_NORMALIZED_RECEIPT
        assert run["build"]["compiler_warning_count"] == 0
        assert tuple(
            (item["role"], item["name"], item["bytes"], item["sha256"])
            for item in run["build"]["public_artifacts"]
        ) == evidence.EXPECTED_ARTIFACTS
        normalized = {"profile": "ot106", "build": run["build"]}
        assert evidence.canonical_sha256(normalized) == evidence.EXPECTED_NORMALIZED_RECEIPT


def test_live_inputs_and_ot093_history_are_exact() -> None:
    value = checked()
    for field, (relative, expected) in evidence.EXPECTED_INPUTS.items():
        assert value["inputs"][field] == relative
        assert sha256(ROOT / relative) == expected == value["inputs"][f"{field}_sha256"]
    for field, path in OT093_FILES.items():
        assert sha256(path) == evidence.EXPECTED_OT093[field] == value["historical_preservation"][field]


def test_source_tool_input_and_receipt_forgery_fail_closed() -> None:
    base = checked()
    cases = (
        (("source", "firmware_input_manifest_sha256"), "source lock"),
        (("toolchain", "compiler_sha256"), "toolchain compiler_sha256"),
        (("inputs", "build_helper_sha256"), "build_helper lock"),
        (("build_reproducibility", "runs", 0, "raw_receipt_sha256"), "raw receipt lock"),
    )
    for path, fragment in cases:
        changed = copy.deepcopy(base)
        target = changed
        for key in path[:-1]:
            target = target[key]
        target[path[-1]] = "0" * 64
        rejects(changed, fragment)


def test_artifact_partition_and_headroom_tampering_fail_closed() -> None:
    base = checked()
    artifact = copy.deepcopy(base)
    artifact["build_reproducibility"]["runs"][0]["build"]["public_artifacts"][0]["bytes"] += 1
    rejects(artifact, "normalized receipt")
    partition = copy.deepcopy(base)
    partition["build_reproducibility"]["runs"][0]["build"]["partition_layout"] = "unverified"
    rejects(partition, "partition binary")
    headroom = copy.deepcopy(base)
    headroom["app_slot_headroom"]["headroom_bytes"] -= 1
    rejects(headroom, "headroom equation")


def test_every_authority_boundary_rejects_true() -> None:
    base = checked()
    for field in base["claims"]:
        changed = copy.deepcopy(base)
        changed["claims"][field] = True
        rejects(changed, field)
    assert base["claims"]["flash_or_erase_performed"] is False
    assert base["claims"]["physical_display_claimed"] is False
    assert base["claims"]["candidate_readiness_claimed"] is False
    assert base["claims"]["final_configuration_selected"] is False


def test_helper_is_build_only_and_restores_isolation() -> None:
    helper = (ROOT / evidence.EXPECTED_INPUTS["build_helper"][0]).read_text(encoding="utf-8").lower()
    for token in ("idf.py flash", "write-flash", "erase-flash", "serialport", "idf.py monitor"):
        assert token not in helper
    for token in (
        "ccache_disable", "pythonpycacheprefix", "pythonnousersite", "finally {",
        "refusing unsafe ot-106 python cache cleanup",
        "refusing unsafe ot-106 reproducible-defaults cleanup",
        "hardware_or_device_accessed = $false", "flash_or_erase_performed = $false",
    ):
        assert token in helper
    assert not (ROOT / "build" / "targets" / "ot106-python-cache-ot106-a").exists()
    assert not (ROOT / "build" / "targets" / "ot106-python-cache-ot106-b").exists()
    assert not (ROOT / "build" / "targets" / "ot106-reproducible-ot106-a.defaults").exists()
    assert not (ROOT / "build" / "targets" / "ot106-reproducible-ot106-b.defaults").exists()


def test_types_unknown_fields_privacy_and_cli_fail_closed() -> None:
    base = checked()
    unknown = copy.deepcopy(base)
    unknown["extra"] = False
    rejects(unknown, "exact canonical fields")
    missing = copy.deepcopy(base)
    del missing["claims"]
    rejects(missing, "exact canonical fields")
    reordered = {"version": base["version"], "schema": base["schema"]}
    reordered.update({key: value for key, value in base.items() if key not in reordered})
    rejects(reordered, "exact canonical fields")
    boolean_integer = copy.deepcopy(base)
    boolean_integer["version"] = False
    rejects(boolean_integer, "exact bounded integer")
    private = copy.deepcopy(base)
    private["inputs"]["build_helper"] = "C:\\Users\\person\\private.ps1"
    rejects(private, "bounded public text")
    with tempfile.TemporaryDirectory() as directory:
        missing = Path(directory) / "private-missing.json"
        completed = subprocess.run(
            [sys.executable, str(ROOT / "tools" / "heltec_compact_footer_build_evidence.py"), str(missing)],
            capture_output=True,
            text=True,
        )
        assert completed.returncode == 2 and completed.stdout == ""
        assert completed.stderr.strip() == "ERROR: evidence JSON is unreadable or invalid"
        assert str(missing) not in completed.stderr


def main() -> int:
    tests = (
        test_accepted_exact_two_build_record,
        test_frozen_receipts_and_artifacts_are_exact,
        test_live_inputs_and_ot093_history_are_exact,
        test_source_tool_input_and_receipt_forgery_fail_closed,
        test_artifact_partition_and_headroom_tampering_fail_closed,
        test_every_authority_boundary_rejects_true,
        test_helper_is_build_only_and_restores_isolation,
        test_types_unknown_fields_privacy_and_cli_fail_closed,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} OTFBL0 build-evidence scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
