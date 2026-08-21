#!/usr/bin/env python3
"""Strict successor gate for the frozen OT-093 build baseline."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import re

import subprocess
import sys

from pathlib import Path
from types import ModuleType
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
RECORD = ROOT / "tests/benchmarks/crypto/OT-093-HISTORICAL-SUCCESSOR-V0.json"
RECORD_SHA256 = "1e3c03820cba83b18bcb1a3a7b8fecad9425a3397d759e1bf173d374f329296a"
BASE_COMMIT = "0afac6b1cf3d142aca2f2cae98264f80ee801989"
ACCEPTANCE_COMMIT = "e144e683d5a07fb4e305f95895f4f07cffb2d869"
SCOPES = ("firmware/components", "firmware/targets/heltec_v4_bench")
TREE_COUNT = 307
TREE_SHA256 = "6738195a7da53eb3d03c4a47552f6c0b6489559a2d81c0ba068489fe9faf7bc3"
CHECKOUT_SHA256 = "c84ba0e3baf334134a60bf753cc951824b9ac21edf1390b143ce21e1194a0c45"
HISTORICAL_SHA256 = "3837dbce866a3fc7cef76fd374bf242bb0125c042e8de15273a9e44bafff3324"
FROZEN_FILES = (
    ("record", "tests/benchmarks/crypto/OT-093-OT005-BUILD-BASELINE-V0.json", 7585, "240906d62926048e6f55b1bb11ce21538e24edbeb8956439ffeb35f3b49b3c83"),
    ("helper", "tools/Build-HeltecV4BenchTarget.ps1", 48249, "6f3ffcb724e2eb52f3d553596481983330fdb2484871b0aab5f793763f2d07fe"),
    ("validator", "tools/crypto_benchmark_baseline.py", 26497, "84e441141708d839d6cb13117476068a7c36570fdafd880173196205c778c747"),
    ("tests", "tests/host/crypto_benchmark_baseline_tests.py", 22742, "83dff18b1b73f3567368e6e2cc582718eae7437e1a8a535c060ccaa1634730d4"),
    ("evidence_note", "tests/hardware/OT-093-2026-08-20.md", 7161, "d4b504b34ec731b287ef24276861bd7ce105fd2b613ce00cdc3d22cb84ce8938"),
)
ORIGINAL_TESTS = (
    "test_accepted_build_lock_is_exact_and_still_blocked",
    "test_two_clean_cache_disabled_receipts_are_seven_artifact_equal",
    "test_source_toolchain_and_generated_defaults_are_bound",
    "test_firmware_input_manifest_is_current_and_ordered",
    "test_headroom_equation_and_bounds_fail_closed",
    "test_candidate_specific_claim_does_not_deny_framework_crypto",
    "test_historical_otcb0_plan_is_unchanged_and_ineligible",
    "test_ot094_preserves_historical_plan_and_baseline_bytes",
    "test_exact_types_unknown_fields_cycles_and_depth_are_rejected",
    "test_two_run_forgeries_partial_receipts_and_swaps_fail_closed",
    "test_private_content_and_cli_errors_are_sanitized",
    "test_validator_and_build_helper_add_no_execution_authority",
    "test_ot093_environment_is_restored_after_success_and_failure",
)
SKIPPED_TESTS = (
    "test_source_toolchain_and_generated_defaults_are_bound",
    "test_firmware_input_manifest_is_current_and_ordered",
)
EXECUTED_TESTS = tuple(name for name in ORIGINAL_TESTS if name not in SKIPPED_TESTS)
REPLACEMENT_TESTS = (
    "test_source_toolchain_and_generated_defaults_are_historically_bound",
    "test_firmware_input_manifest_is_historically_reconstructed",
)
HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
TREE_ENTRY = re.compile(rb"^([0-7]{6}) blob ([0-9a-f]{40})\t(.+)$")
INDEX_ENTRY = re.compile(rb"^([0-7]{6}) ([0-9a-f]{40}) ([0-3])\t(.+)$")


class HarnessError(RuntimeError):
    """A deliberately path-free validation failure."""


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _exact(value: Any, keys: set[str], label: str) -> dict[str, Any]:
    if type(value) is not dict or set(value) != keys:
        raise HarnessError(f"{label} shape mismatch")
    return value


def _safe_path(value: Any, label: str) -> str:
    if type(value) is not str or not value or value.startswith(("/", "\\", "-")):
        raise HarnessError(f"{label} path rejected")
    if "\\" in value or ":" in value or any(ord(char) < 32 for char in value):
        raise HarnessError(f"{label} path rejected")
    parts = value.split("/")
    if any(part in {"", ".", ".."} for part in parts):
        raise HarnessError(f"{label} path rejected")
    return value


def _hex(value: Any, length: int, label: str) -> str:
    pattern = HEX40 if length == 40 else HEX64
    if type(value) is not str or pattern.fullmatch(value) is None:
        raise HarnessError(f"{label} digest mismatch")
    return value


def load_record(*, verify_file_hash: bool = True) -> dict[str, Any]:
    try:
        raw = RECORD.read_bytes()
    except OSError as exc:
        raise HarnessError("successor record unavailable") from exc
    if verify_file_hash and _sha256(raw) != RECORD_SHA256:
        raise HarnessError("successor record hash mismatch")
    try:
        value = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise HarnessError("successor record encoding mismatch") from exc
    if type(value) is not dict:
        raise HarnessError("successor record shape mismatch")
    return value


def validate_record(value: dict[str, Any]) -> None:
    root = _exact(value, {"schema", "version", "record_id", "status", "frozen_suite", "source_snapshot", "inputs", "test_plan", "policy", "reconstruction"}, "record")
    if root["schema"] != "OTHBL0" or type(root["version"]) is not int or root["version"] != 0:
        raise HarnessError("record identity mismatch")
    if root["record_id"] != "OT-093-HISTORICAL-SUCCESSOR-V0" or root["status"] != "historical_successor_gate":
        raise HarnessError("record identity mismatch")
    frozen = _exact(root["frozen_suite"], {"raw_files"}, "frozen suite")["raw_files"]
    if type(frozen) is not list or len(frozen) != len(FROZEN_FILES):
        raise HarnessError("frozen suite list mismatch")
    actual_frozen = []
    for item in frozen:
        item = _exact(item, {"role", "path", "bytes", "sha256"}, "frozen file")
        if type(item["bytes"]) is not int or item["bytes"] < 1:
            raise HarnessError("frozen file size mismatch")
        actual_frozen.append((item["role"], _safe_path(item["path"], "frozen file"), item["bytes"], _hex(item["sha256"], 64, "frozen file")))
    if tuple(actual_frozen) != FROZEN_FILES:
        raise HarnessError("frozen suite lock mismatch")
    source = _exact(root["source_snapshot"], {"firmware_base_commit", "ot093_acceptance_commit", "acceptance_parent_commit", "scopes", "commit_tree_file_count", "commit_tree_manifest_sha256", "checkout_core_autocrlf", "checkout_manifest_sha256", "checkout_derivation", "historical_working_manifest_sha256", "historical_working_manifest_status", "historical_working_manifest_git_reconstructible", "historical_mixed_eol_per_file_map_available", "historical_limitation"}, "source snapshot")
    expected_source = {
        "firmware_base_commit": BASE_COMMIT,
        "ot093_acceptance_commit": ACCEPTANCE_COMMIT,
        "acceptance_parent_commit": BASE_COMMIT,
        "scopes": list(SCOPES),
        "commit_tree_file_count": TREE_COUNT,
        "commit_tree_manifest_sha256": TREE_SHA256,
        "checkout_core_autocrlf": True,
        "checkout_manifest_sha256": CHECKOUT_SHA256,
        "checkout_derivation": "one-time-isolated-autocrlf-checkout; reproduced-by-explicit-git-blob-transforms",
        "historical_working_manifest_sha256": HISTORICAL_SHA256,
        "historical_working_manifest_status": "accepted-one-time-historical-digest",
        "historical_working_manifest_git_reconstructible": False,
        "historical_mixed_eol_per_file_map_available": False,
        "historical_limitation": "exact mixed-EOL per-file map unavailable/non-reconstructible",
    }
    if source != expected_source or source["checkout_manifest_sha256"] == source["historical_working_manifest_sha256"]:
        raise HarnessError("source snapshot lock mismatch")
    inputs = _exact(root["inputs"], {"target_contract", "sdkconfig_defaults", "build_helper"}, "inputs")
    expected_inputs = {
        "target_contract": {"path": "firmware/targets/heltec_v4_bench/target-contract.json", "commit": BASE_COMMIT, "mode": "100644", "blob_oid": "ad09ee666ad56f3926ab189ed4adea6cabe98ba8", "canonical_sha256": "8f263e9d5fd756e5b80dbed33dbbc4e264a66763ce6088bca3d894cc42025615"},
        "sdkconfig_defaults": {"path": "firmware/targets/heltec_v4_bench/sdkconfig.defaults", "commit": BASE_COMMIT, "mode": "100644", "blob_oid": "bc413557f07e8389ae73cd7bea379c23944265bd", "canonical_sha256": "84d54e5d730ba28ceba0c97831a477303db128d63ede7575bec52013770d70c0"},
        "build_helper": {"path": "tools/Build-HeltecV4BenchTarget.ps1", "commit": ACCEPTANCE_COMMIT, "mode": "100644", "blob_oid": "0e5506f6d63c2fbb9cbeb1360575c0d37f0bde76", "canonical_sha256": "c36a58e0529a7628b66cc4674f4207804ae2d0a709b9d3a20b4973de6c585214", "frozen_working_raw_sha256": "6f3ffcb724e2eb52f3d553596481983330fdb2484871b0aab5f793763f2d07fe", "checkout_raw_sha256": "24e142dcf21a9709200f14b6c6f1578023f96e71946f584ac00a3bbc8f1835a6", "checkout_transform": "lf-to-crlf-v1"},
    }
    if inputs != expected_inputs:
        raise HarnessError("historical input lock mismatch")
    plan = _exact(root["test_plan"], {"discovered_original_tests", "skipped_original_tests", "executed_original_tests", "replacement_tests"}, "test plan")
    if plan != {"discovered_original_tests": list(ORIGINAL_TESTS), "skipped_original_tests": list(SKIPPED_TESTS), "executed_original_tests": list(EXECUTED_TESTS), "replacement_tests": list(REPLACEMENT_TESTS)}:
        raise HarnessError("test plan lock mismatch")
    policy = _exact(root["policy"], {"commit_fallback_allowed", "working_tree_fallback_allowed", "git_error_echo_allowed", "temporary_checkout_performed", "checkout_digest_equivalent_to_historical_digest"}, "policy")
    if policy != {"commit_fallback_allowed": False, "working_tree_fallback_allowed": False, "git_error_echo_allowed": False, "temporary_checkout_performed": False, "checkout_digest_equivalent_to_historical_digest": False}:
        raise HarnessError("historical policy mismatch")
    reconstruction = _exact(root["reconstruction"], {"kind", "entries"}, "reconstruction")
    if reconstruction["kind"] != "acceptance-commit-isolated-autocrlf-derived-git-blob-transform-v1":
        raise HarnessError("reconstruction kind mismatch")
    entries = reconstruction["entries"]
    if type(entries) is not list or len(entries) != TREE_COUNT:
        raise HarnessError("reconstruction count mismatch")
    previous = ""
    seen = set()
    for item in entries:
        item = _exact(item, {"path", "mode", "blob_oid", "checkout_raw_sha256", "checkout_transform"}, "reconstruction entry")
        path = _safe_path(item["path"], "reconstruction")
        if path <= previous or path in seen or not any(path.startswith(f"{scope}/") for scope in SCOPES):
            raise HarnessError("reconstruction path order mismatch")
        if item["mode"] not in {"100644", "100755"}:
            raise HarnessError("reconstruction mode mismatch")
        _hex(item["blob_oid"], 40, "reconstruction blob")
        _hex(item["checkout_raw_sha256"], 64, "reconstruction raw")
        if item["checkout_transform"] not in {"identity", "lf-to-crlf-v1"}:
            raise HarnessError("reconstruction transform mismatch")
        previous = path
        seen.add(path)


def verify_frozen_files(value: dict[str, Any]) -> None:
    validate_record(value)
    for expected, item in zip(FROZEN_FILES, value["frozen_suite"]["raw_files"]):
        _, relative, size, digest = expected
        path = ROOT.joinpath(*relative.split("/"))
        try:
            if path.is_symlink():
                raise HarnessError("frozen OT-093 file type mismatch")
            resolved = path.resolve(strict=True)
            resolved.relative_to(ROOT.resolve(strict=True))
            raw = resolved.read_bytes()
        except (OSError, ValueError) as exc:
            raise HarnessError("frozen OT-093 file unavailable") from exc
        if resolved.is_symlink() or len(raw) != size or _sha256(raw) != digest or item["sha256"] != digest:
            raise HarnessError("frozen OT-093 file hash mismatch")


def _git_bytes(
    *args: str,
    env: dict[str, str] | None = None,
    input_data: bytes | None = None,
) -> bytes:
    try:
        effective_env = {**os.environ, "GIT_NO_REPLACE_OBJECTS": "1"}
        if env is not None:
            effective_env.update(env)
        result = subprocess.run(
            ["git", *args], cwd=ROOT, env=effective_env, input=input_data,
            check=False, capture_output=True,
        )
    except OSError as exc:
        raise HarnessError("historical Git operation unavailable") from exc
    if result.returncode != 0:
        raise HarnessError("historical Git operation failed")
    return result.stdout


def _require_commit(commit: str) -> None:
    if HEX40.fullmatch(commit) is None or _git_bytes("cat-file", "-t", commit) != b"commit\n":
        raise HarnessError("historical commit mismatch")
    if _git_bytes("rev-parse", "--verify", f"{commit}^{{commit}}").strip() != commit.encode("ascii"):
        raise HarnessError("historical commit mismatch")


def _tree(commit: str, scopes: tuple[str, ...]) -> list[tuple[str, str, str]]:
    raw = _git_bytes("ls-tree", "-r", "-z", "--full-tree", commit, "--", *scopes)
    rows = []
    for encoded in raw.split(b"\0"):
        if not encoded:
            continue
        match = TREE_ENTRY.fullmatch(encoded)
        if match is None:
            raise HarnessError("historical tree entry mismatch")
        mode, oid = match.group(1).decode("ascii"), match.group(2).decode("ascii")
        try:
            path = match.group(3).decode("utf-8")
        except UnicodeDecodeError as exc:
            raise HarnessError("historical tree path mismatch") from exc
        _safe_path(path, "historical tree")
        if mode not in {"100644", "100755"} or not any(
            path == scope or path.startswith(f"{scope}/") for scope in scopes
        ):
            raise HarnessError("historical tree scope mismatch")
        rows.append((mode, oid, path))
    if [row[2] for row in rows] != sorted(row[2] for row in rows) or len({row[2] for row in rows}) != len(rows):
        raise HarnessError("historical tree order mismatch")
    return rows


def _canonical(data: bytes) -> str:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise HarnessError("historical input encoding mismatch") from exc
    text = text.replace("\r\n", "\n")
    if "\r" in text or text.startswith("\ufeff"):
        raise HarnessError("historical input encoding mismatch")
    return _sha256(text.encode("utf-8"))


def _checkout_transform(data: bytes, mode: str) -> bytes:
    if mode == "identity":
        return data
    if mode == "lf-to-crlf-v1" and b"\r" not in data:
        return data.replace(b"\n", b"\r\n")
    raise HarnessError("checkout transform mismatch")


def verify_historical_inputs(value: dict[str, Any]) -> None:
    validate_record(value)
    _require_commit(BASE_COMMIT)
    _require_commit(ACCEPTANCE_COMMIT)
    if _git_bytes("rev-parse", "--verify", f"{ACCEPTANCE_COMMIT}^").strip() != BASE_COMMIT.encode("ascii"):
        raise HarnessError("acceptance parent mismatch")
    base_rows = _tree(BASE_COMMIT, SCOPES)
    lookup = {path: (mode, oid) for mode, oid, path in base_rows}
    for field in ("target_contract", "sdkconfig_defaults"):
        item = value["inputs"][field]
        if lookup.get(item["path"]) != (item["mode"], item["blob_oid"]):
            raise HarnessError("historical input tree mismatch")
        if _canonical(_git_bytes("cat-file", "blob", item["blob_oid"])) != item["canonical_sha256"]:
            raise HarnessError("historical input hash mismatch")
    helper = value["inputs"]["build_helper"]
    helper_rows = _tree(ACCEPTANCE_COMMIT, (helper["path"],))
    if helper_rows != [(helper["mode"], helper["blob_oid"], helper["path"])]:
        raise HarnessError("historical helper tree mismatch")
    if _canonical(_git_bytes("cat-file", "blob", helper["blob_oid"])) != helper["canonical_sha256"]:
        raise HarnessError("historical helper hash mismatch")


def verify_historical_manifest(value: dict[str, Any]) -> None:
    validate_record(value)
    _require_commit(BASE_COMMIT)
    _require_commit(ACCEPTANCE_COMMIT)
    rows = _tree(BASE_COMMIT, SCOPES)
    if len(rows) != TREE_COUNT:
        raise HarnessError("historical tree count mismatch")
    manifest = "".join(f"{mode} {oid} {path}\n" for mode, oid, path in rows).encode("utf-8")
    if _sha256(manifest) != TREE_SHA256:
        raise HarnessError("historical tree manifest mismatch")
    recorded = [(item["mode"], item["blob_oid"], item["path"]) for item in value["reconstruction"]["entries"]]
    if rows != recorded:
        raise HarnessError("reconstruction tree mismatch")
    lines = []
    for item in value["reconstruction"]["entries"]:
        blob = _git_bytes("cat-file", "blob", item["blob_oid"])
        transformed = _checkout_transform(blob, item["checkout_transform"])
        if _sha256(transformed) != item["checkout_raw_sha256"]:
            raise HarnessError("reconstruction transform hash mismatch")
        lines.append(f"{item['checkout_raw_sha256']} {item['path']}")
    checkout_digest = _sha256(("\n".join(lines) + "\n").encode("utf-8"))
    if checkout_digest != CHECKOUT_SHA256 or checkout_digest == HISTORICAL_SHA256:
        raise HarnessError("reconstruction manifest mismatch")
    helper = value["inputs"]["build_helper"]
    helper_blob = _git_bytes("cat-file", "blob", helper["blob_oid"])
    transformed_helper = _checkout_transform(helper_blob, helper["checkout_transform"])
    if _sha256(transformed_helper) != helper["checkout_raw_sha256"]:
        raise HarnessError("helper reconstruction mismatch")

def _load_original_tests() -> ModuleType:
    path = ROOT / FROZEN_FILES[3][1]
    spec = importlib.util.spec_from_file_location("ot093_frozen_tests", path)
    if spec is None or spec.loader is None:
        raise HarnessError("frozen tests unavailable")
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
    except Exception as exc:
        raise HarnessError("frozen tests import failed") from exc
    discovered = {name for name, candidate in vars(module).items() if name.startswith("test_") and callable(candidate)}
    if discovered != set(ORIGINAL_TESTS):
        raise HarnessError("frozen test discovery mismatch")
    return module


def main() -> int:
    current = "historical harness"
    try:
        value = load_record()
        verify_frozen_files(value)
        module = _load_original_tests()
        for name in EXECUTED_TESTS:
            current = name
            getattr(module, name)()
        current = REPLACEMENT_TESTS[0]
        verify_historical_inputs(value)
        current = REPLACEMENT_TESTS[1]
        verify_historical_manifest(value)
    except Exception:
        print(f"FAIL: OT-093 historical successor ({current})", file=sys.stderr)
        return 1
    print(f"PASS: {len(EXECUTED_TESTS) + len(REPLACEMENT_TESTS)} OTCBL0 historical build-lock scenario groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
