#!/usr/bin/env python3
"""Deterministic tests for the OTAR0 Android release-plan boundary."""

from __future__ import annotations

import ast
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import android_release_admission as admission  # noqa: E402


PLAN_PATH = (
    ROOT
    / "tests"
    / "release-plans"
    / "OT-086-ANDROID-OPERATIONAL-RELEASE-PLAN-V0.json"
)


def plan() -> dict:
    return json.loads(PLAN_PATH.read_text(encoding="utf-8"))


def expect_error(value: dict, contains: str) -> None:
    try:
        admission.validate_plan(value)
    except admission.AdmissionError as exc:
        assert contains in str(exc), (contains, str(exc))
        return
    raise AssertionError(f"expected AdmissionError containing {contains!r}")


def ready_plan() -> dict:
    value = plan()
    value["application"]["candidate_version_code"] = 1
    value["application"]["candidate_version_name"] = "1.0.0"
    value["application"]["production_variant_configured"] = True
    value["release_identity"]["clearance"] = "professionally_cleared"
    value["release_identity"]["owner_approved_for_candidate"] = True
    value["signing"]["custody_model"] = "owner_offline"
    value["signing"]["certificate_sha256"] = "1" * 64
    value["signing"]["custody_approved"] = True
    value["supported_platforms"]["matrix_approved"] = True
    for key in value["operational_policy"]:
        value["operational_policy"][key] = True
    value["blockers"] = []
    value["plan_status"] = "ready_for_separate_execution_authorization"
    return value


def test_checked_in_plan_is_accepted_but_execution_blocked() -> None:
    report = admission.validate_plan(plan())
    assert report["plan_status"] == "PLAN-ACCEPTED-EXECUTION-BLOCKED"
    assert report["release_gate_status"] == "NOT-EVALUATED"
    assert report["execution_authority_granted"] is False
    assert report["blockers"] == [
        "physical_acceptance_matrix_not_approved",
        "privacy_data_safety_not_approved",
        "release_identity_not_approved",
        "rollback_policy_not_approved",
        "signer_and_custody_not_approved",
        "support_policy_not_approved",
    ]
    application = plan()["application"]
    assert application["candidate_version_code"] == 1
    assert application["candidate_version_name"] == "1.0.0"
    assert application["production_variant_configured"] is True


def test_canonical_digest_is_key_order_independent() -> None:
    value = plan()
    reordered = dict(reversed(list(value.items())))
    assert admission.canonical_sha256(value) == admission.canonical_sha256(reordered)


def test_exact_shape_rejects_missing_and_extra_fields() -> None:
    value = plan()
    del value["application"]
    expect_error(value, "keys differ")
    value = plan()
    value["unexpected"] = False
    expect_error(value, "keys differ")


def test_release_baseline_and_version_coherence_fail_closed() -> None:
    value = plan()
    value["application"]["target_sdk"] = 36
    expect_error(value, "accepted baseline")
    value = plan()
    value["application"]["candidate_version_code"] = 2
    expect_error(value, "accepted source baseline")
    value = plan()
    value["application"]["candidate_version_name"] = "1.0.1"
    expect_error(value, "accepted source baseline")
    value = ready_plan()
    value["application"]["candidate_version_name"] = "1.0.0-debug"
    expect_error(value, "stable X.Y.Z")
    value = ready_plan()
    value["application"]["candidate_version_name"] = "1.0.0-rc.1"
    expect_error(value, "stable X.Y.Z")
    value = plan()
    value["upgrade_mode"] = "in-place-upgrade-required"
    expect_error(value, "first supported release")
    checks = value["required_acceptance_checks"]
    assert "in_place_upgrade_and_state_boundary" not in checks
    assert "first_release_uninstall_reinstall_and_state_boundary" in checks
    assert "uninstall_and_data_removal" in checks


def test_private_sideload_scope_is_frozen_without_store_authority() -> None:
    value = plan()
    value["distribution"]["scope"] = "google-play"
    expect_error(value, "private-sideload-v1-pilot")
    value = plan()
    value["distribution"]["artifact_format"] = "aab"
    expect_error(value, "private-sideload-v1-pilot")
    value = plan()
    value["distribution"]["external_account_authority_included"] = True
    expect_error(value, "external account authority")


def test_signing_shape_and_custody_fail_closed() -> None:
    value = ready_plan()
    value["signing"]["certificate_sha256"] = "A" * 64
    expect_error(value, "lowercase SHA-256")
    value = plan()
    value["signing"]["custody_model"] = "managed_store"
    expect_error(value, "not valid for private sideload")
    value = plan()
    value["signing"]["sensitive_material_included"] = True
    expect_error(value, "cannot include signing material")


def test_blocker_list_must_be_exact_sorted_and_current() -> None:
    value = plan()
    value["blockers"] = value["blockers"][1:]
    expect_error(value, "derived blocker set")
    value = plan()
    value["blockers"] = list(reversed(value["blockers"]))
    expect_error(value, "derived blocker set")
    value = ready_plan()
    value["plan_status"] = "accepted_execution_blocked"
    expect_error(value, "does not match")


def test_synthetic_ready_plan_is_not_release_acceptance_or_authority() -> None:
    report = admission.validate_plan(ready_plan())
    assert (
        report["plan_status"]
        == "PLAN-ACCEPTED-READY-FOR-SEPARATE-AUTHORIZATION"
    )
    assert report["release_gate_status"] == "NOT-EVALUATED"
    assert report["execution_authority_granted"] is False
    assert report["blockers"] == []
    assert "PASS" not in json.dumps(report)


def test_permission_checklist_platform_and_policy_drift_fail_closed() -> None:
    value = plan()
    value["artifact_policy"]["authored_permissions"].append(
        "android.permission.INTERNET"
    )
    expect_error(value, "authored_permissions")
    value = plan()
    assert value["artifact_policy"]["generated_permissions"] == [
        "io.github.nbjelanovic.otclient.DYNAMIC_RECEIVER_NOT_EXPORTED_PERMISSION"
    ]
    value["artifact_policy"]["generated_permissions"] = []
    expect_error(value, "generated_permissions")
    value = plan()
    value["required_acceptance_checks"] = value["required_acceptance_checks"][:-1]
    expect_error(value, "required_acceptance_checks")
    value = plan()
    value["supported_platforms"]["required_api_levels"] = [33, 36]
    expect_error(value, "canonical matrix")
    value = plan()
    value["artifact_policy"]["require_test_components_absent"] = False
    expect_error(value, "must be true")


def test_plan_baseline_matches_android_source() -> None:
    build = (ROOT / "android" / "app" / "build.gradle.kts").read_text(
        encoding="utf-8"
    )
    manifest = (
        ROOT / "android" / "app" / "src" / "main" / "AndroidManifest.xml"
    ).read_text(encoding="utf-8")
    application = plan()["application"]

    def build_value(pattern: str) -> str:
        match = re.search(pattern, build)
        assert match is not None, pattern
        return match.group(1)

    source_application_id = build_value(r'applicationId\s*=\s*"([^"]+)"')
    assert build_value(r'namespace\s*=\s*"([^"]+)"') == source_application_id
    assert source_application_id == admission.APPLICATION_ID
    assert application["application_id"] == source_application_id

    source_version_code = int(build_value(r"versionCode\s*=\s*([0-9]+)"))
    source_version_name = build_value(r'versionName\s*=\s*"([^"]+)"')
    source_min_sdk = int(build_value(r"minSdk\s*=\s*([0-9]+)"))
    source_target_sdk = int(build_value(r"targetSdk\s*=\s*([0-9]+)"))
    assert source_version_code == admission.CURRENT_VERSION_CODE
    assert application["current_version_code"] == source_version_code
    assert source_version_name == admission.CURRENT_VERSION_NAME
    assert application["current_version_name"] == source_version_name
    assert source_min_sdk == admission.MIN_SDK
    assert application["min_sdk"] == source_min_sdk
    assert source_target_sdk == admission.TARGET_SDK
    assert application["target_sdk"] == source_target_sdk

    source_permissions = re.findall(
        r'<uses-permission\b[^>]*android:name="([^"]+)"', manifest
    )
    authored_permissions = plan()["artifact_policy"]["authored_permissions"]
    assert len(source_permissions) == len(set(source_permissions)) == 5
    assert set(source_permissions) == set(admission.AUTHORED_PERMISSIONS)
    assert authored_permissions == admission.AUTHORED_PERMISSIONS
    assert set(source_permissions).isdisjoint(admission.GENERATED_PERMISSIONS)


def test_ot087_release_variant_and_artifact_gate_are_explicit() -> None:
    build = (ROOT / "android" / "app" / "build.gradle.kts").read_text(
        encoding="utf-8"
    )
    foundation = (ROOT / "android" / "Test-AndroidFoundation.ps1").read_text(
        encoding="utf-8"
    )
    inspector = (
        ROOT / "android" / "Test-AndroidUnsignedReleaseArtifact.ps1"
    ).read_text(encoding="utf-8")

    release = re.search(
        r'getByName\("release"\)\s*\{(?P<body>.*?)\n\s*\}',
        build,
        re.DOTALL,
    )
    assert release is not None
    release_body = release.group("body")
    assert "isDebuggable = false" in release_body
    assert "isJniDebuggable = false" in release_body
    assert "isMinifyEnabled = false" in release_body
    assert "signingConfig = null" in release_body
    assert 'versionNameSuffix = "-dev"' in build

    for task in (
        ":app:testReleaseUnitTest",
        ":app:lintRelease",
        ":app:assembleRelease",
    ):
        assert task in foundation
    assert "Test-AndroidUnsignedReleaseArtifact.ps1" in foundation
    assert "$apksigner verify --verbose" in inspector
    assert "$apksigner sign " not in inspector
    assert "uses-permission(?:-sdk-[0-9]+)?" in inspector
    assert "256MB" in inspector
    assert "64MB" in inspector
    assert "$cloudRules" in inspector
    assert "$transferRules" in inspector
    assert "OT087_SIGNATURE=UNSIGNED" in inspector
    assert "OT087_TEMP_CLEANUP=PASS" in inspector
    assert inspector.rstrip().endswith("exit 0")

    common_test = (
        ROOT
        / "android"
        / "app"
        / "src"
        / "test"
        / "kotlin"
        / "io"
        / "github"
        / "nbjelanovic"
        / "otclient"
        / "PublicLinkAutomaticTerminationPolicyTest.kt"
    )
    debug_test = (
        ROOT
        / "android"
        / "app"
        / "src"
        / "testDebug"
        / "kotlin"
        / "io"
        / "github"
        / "nbjelanovic"
        / "otclient"
        / "PublicLinkAutomaticTerminationPolicyTest.kt"
    )
    assert not common_test.exists()
    assert debug_test.is_file()


def test_ot085_test_only_components_are_explicitly_forbidden() -> None:
    components = plan()["artifact_policy"]["forbidden_release_components"]
    assert components == [
        "io.github.nbjelanovic.otclient.PublicLinkAutomaticTerminationPolicy",
        "io.github.nbjelanovic.otclient.PublicLinkProbeInstrumentation",
    ]
    value = plan()
    value["artifact_policy"]["forbidden_release_components"] = components[1:]
    expect_error(value, "forbidden_release_components")


def test_privacy_and_execution_authority_cannot_be_relaxed() -> None:
    value = plan()
    value["privacy"]["device_identifiers_included"] = True
    expect_error(value, "must be false")
    value = plan()
    value["execution_authority"]["store_upload"] = True
    expect_error(value, "must be false")
    value = plan()
    value["execution_authority"]["device_install"] = True
    expect_error(value, "must be false")


def test_private_fields_and_values_are_rejected_without_echo() -> None:
    value = plan()
    value["signing"]["private_key"] = "not-public"
    expect_error(value, "prohibited field name")
    value = plan()
    value["release_identity"]["public_name"] = "captured on COM44"
    try:
        admission.validate_plan(value)
    except admission.AdmissionError as exc:
        assert "COM44" not in str(exc)
    else:
        raise AssertionError("private transport value should fail")


def test_cli_reports_blocked_plan_and_invalid_input_deterministically() -> None:
    command = [
        sys.executable,
        str(ROOT / "tools" / "android_release_admission.py"),
        "validate-plan",
        "--input",
        str(PLAN_PATH),
    ]
    first = subprocess.run(command, check=False, capture_output=True, text=True)
    second = subprocess.run(command, check=False, capture_output=True, text=True)
    assert first.returncode == 0
    assert first.stdout == second.stdout
    assert (
        json.loads(first.stdout)["plan_status"]
        == "PLAN-ACCEPTED-EXECUTION-BLOCKED"
    )
    with tempfile.TemporaryDirectory() as directory:
        bad = Path(directory) / "bad.json"
        bad.write_text("{}\n", encoding="utf-8")
        failed = subprocess.run(
            command[:-1] + [str(bad)],
            check=False,
            capture_output=True,
            text=True,
        )
        assert failed.returncode == 2
        assert json.loads(failed.stderr)["plan_status"] == "PLAN-INVALID"

        private_field = "\\".join(("C:", "Users", "private-owner", "COM44"))
        private_plan = plan()
        private_plan[private_field] = False
        private_input = Path(directory) / "private-field.json"
        private_input.write_text(
            json.dumps(private_plan) + "\n", encoding="utf-8"
        )
        private_failed = subprocess.run(
            command[:-1] + [str(private_input)],
            check=False,
            capture_output=True,
            text=True,
        )
        assert private_failed.returncode == 2
        assert private_field not in private_failed.stderr
        assert "private-owner" not in private_failed.stderr
        assert "COM44" not in private_failed.stderr

        ancestor_field = "unknown_ancestor_marker"
        private_value = f"captured on {'COM'}{44}"
        ancestor_plan = plan()
        ancestor_plan[ancestor_field] = {"child": private_value}
        ancestor_input = Path(directory) / "private-child.json"
        ancestor_input.write_text(
            json.dumps(ancestor_plan) + "\n", encoding="utf-8"
        )
        ancestor_failed = subprocess.run(
            command[:-1] + [str(ancestor_input)],
            check=False,
            capture_output=True,
            text=True,
        )
        assert ancestor_failed.returncode == 2
        assert ancestor_field not in ancestor_failed.stderr
        assert private_value not in ancestor_failed.stderr
        assert "COM44" not in ancestor_failed.stderr


def test_load_is_bounded_duplicate_safe_utf8_and_sanitized() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        cases = {
            "private-COM44-duplicate.json": b'{"schema":"OTAR0","schema":"OTAR0"}',
            "private-COM44-utf8.json": b"\xff\xfe",
            "private-COM44-large.json": b" " * (admission.MAX_PLAN_BYTES + 1),
        }
        for name, encoded in cases.items():
            path = root / name
            path.write_bytes(encoded)
            try:
                admission.load_plan(path)
            except admission.AdmissionError as exc:
                message = str(exc)
                assert "COM44" not in message
                assert str(root) not in message
            else:
                raise AssertionError(f"{name} should have failed")

        deep = root / "private-COM44-deep.json"
        deep.write_text("[" * 2000 + "0" + "]" * 2000, encoding="utf-8")
        assert deep.stat().st_size <= admission.MAX_PLAN_BYTES
        try:
            admission.load_plan(deep)
        except admission.AdmissionError as exc:
            assert str(exc) == (
                "plan JSON is malformed or contains duplicate fields"
            )
            assert "COM44" not in str(exc)
            assert str(root) not in str(exc)
        else:
            raise AssertionError("deeply nested plan should have failed")

        missing = root / "private-COM44-missing.json"
        try:
            admission.load_plan(missing)
        except admission.AdmissionError as exc:
            assert str(exc) == "plan JSON could not be read"
            assert "COM44" not in str(exc)
            assert str(root) not in str(exc)
        else:
            raise AssertionError("missing plan should have failed")

        command = [
            sys.executable,
            str(ROOT / "tools" / "android_release_admission.py"),
            "validate-plan",
            "--input",
            str(missing),
        ]
        failed = subprocess.run(
            command, check=False, capture_output=True, text=True
        )
        assert failed.returncode == 2
        assert "COM44" not in failed.stderr
        assert str(root) not in failed.stderr

        deep_failed = subprocess.run(
            command[:-1] + [str(deep)],
            check=False,
            capture_output=True,
            text=True,
        )
        assert deep_failed.returncode == 2
        deep_report = json.loads(deep_failed.stderr)
        assert deep_report["plan_status"] == "PLAN-INVALID"
        assert deep_report["error"] == (
            "plan JSON is malformed or contains duplicate fields"
        )
        assert "Traceback" not in deep_failed.stderr
        assert "COM44" not in deep_failed.stderr
        assert str(root) not in deep_failed.stderr


def test_validator_has_no_execution_or_output_creation_surface() -> None:
    source = (ROOT / "tools" / "android_release_admission.py").read_text(
        encoding="utf-8"
    )
    tree = ast.parse(source)
    imports = {
        alias.name.split(".")[0]
        for node in ast.walk(tree)
        if isinstance(node, ast.Import)
        for alias in node.names
    }
    imports.update(
        node.module.split(".")[0]
        for node in ast.walk(tree)
        if isinstance(node, ast.ImportFrom) and node.module
    )
    assert imports <= {
        "__future__",
        "argparse",
        "hashlib",
        "json",
        "pathlib",
        "re",
        "sys",
        "typing",
    }
    for forbidden in (
        "subprocess",
        "socket",
        "requests",
        "urllib",
        "write_text",
        "write_bytes",
        "adb",
        "apksigner",
        "keytool",
    ):
        assert forbidden not in source


def main() -> None:
    tests = [
        test_checked_in_plan_is_accepted_but_execution_blocked,
        test_canonical_digest_is_key_order_independent,
        test_exact_shape_rejects_missing_and_extra_fields,
        test_release_baseline_and_version_coherence_fail_closed,
        test_private_sideload_scope_is_frozen_without_store_authority,
        test_signing_shape_and_custody_fail_closed,
        test_blocker_list_must_be_exact_sorted_and_current,
        test_synthetic_ready_plan_is_not_release_acceptance_or_authority,
        test_permission_checklist_platform_and_policy_drift_fail_closed,
        test_plan_baseline_matches_android_source,
        test_ot087_release_variant_and_artifact_gate_are_explicit,
        test_ot085_test_only_components_are_explicitly_forbidden,
        test_privacy_and_execution_authority_cannot_be_relaxed,
        test_private_fields_and_values_are_rejected_without_echo,
        test_cli_reports_blocked_plan_and_invalid_input_deterministically,
        test_load_is_bounded_duplicate_safe_utf8_and_sanitized,
        test_validator_has_no_execution_or_output_creation_surface,
    ]
    for test in tests:
        test()
    print(
        f"PASS: {len(tests)} Android operational-release admission scenario groups"
    )


if __name__ == "__main__":
    main()
