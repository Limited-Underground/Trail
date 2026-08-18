#!/usr/bin/env python3
"""Focused tests for the denied offline OT-075 partition artifact bundle."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import re
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = ROOT / "tools" / "build_heltec_v4_protected_storage_recovery_bundle.py"
HOST_RUNNER = ROOT / "tools" / "Test-Host.ps1"
TARGET = ROOT / "firmware" / "targets" / "heltec_v4_bench"
RECOVERY_PLAN = TARGET / "protected-storage-recovery-bundle-plan.json"
TRANSITION_PLAN = TARGET / "protected-storage-transition-plan.json"
PROVISIONING_PLAN = TARGET / "protected-storage-provisioning-plan.json"
EXPECTED_BINARY_SHA256 = (
    "F83EDE6D0F206D6032147A2AF0B526700BCB888A6C0CADDF6DD17724E5600E72"
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load_tool():
    spec = importlib.util.spec_from_file_location("ot075_bundle", TOOL_PATH)
    require(spec is not None and spec.loader is not None, "unable to load tool")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


TOOL = load_tool()


GENERATOR = r'''#!/usr/bin/env python3
import hashlib
import struct
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
mode = (root / "mode.txt").read_text(encoding="ascii").strip()
rows = [
    (1, 0, 0x9000, 0x2000, b"otadata", 0),
    (0, 0, 0x10000, 0x4f0000, b"factory", 0),
    (0, 0x10, 0x500000, 0x500000, b"ota_0", 0),
    (0, 0x11, 0xa00000, 0x500000, b"ota_1", 0),
    (1, 2, 0xf00000, 0x10000, b"ot_auth", 1),
    (0x40, 0, 0xf10000, 0xf0000, b"ot_state", 0),
]
if mode == "wrong-row":
    rows[-1] = (0x40, 0, 0xf20000, 0xe0000, b"ot_state", 0)
entries = b"".join(
    struct.pack("<HBBLL16sL", 0x50aa, t, s, o, z,
                n.ljust(16, bytes([0])), f)
    for t, s, o, z, n, f in rows
)
checksum = bytes.fromhex("EBEB") + bytes([255]) * 14 + hashlib.md5(entries).digest()
data = (entries + checksum).ljust(3072, bytes([255]))
if mode == "short":
    data = data[:-1]
elif mode == "bad-md5":
    data = data[:208] + bytes([data[208] ^ 1]) + data[209:]
elif mode == "bad-padding":
    data = data[:-1] + bytes([0])
Path(sys.argv[-1]).write_bytes(data)
'''


def fake_idf(root: Path, version: str = "ESP-IDF v6.0.2",
             mode: str = "exact") -> Path:
    (root / "tools").mkdir(parents=True)
    (root / "tools" / "cmake").mkdir(parents=True)
    (root / "components" / "partition_table").mkdir(parents=True)
    major, minor, patch = version.removeprefix("ESP-IDF v").split(".")
    (root / "tools" / "cmake" / "version.cmake").write_text(
        f"set(IDF_VERSION_MAJOR {major})\n"
        f"set(IDF_VERSION_MINOR {minor})\n"
        f"set(IDF_VERSION_PATCH {patch})\n",
        encoding="utf-8",
        newline="\n",
    )
    (root / "components" / "partition_table" / "gen_esp32part.py").write_text(
        GENERATOR, encoding="utf-8", newline="\n"
    )
    (root / "mode.txt").write_text(mode, encoding="ascii")
    return root


def test_exact_bundle_is_deterministic_and_denied() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        base = Path(temporary)
        idf = fake_idf(base / "idf")
        first = base / "first"
        second = base / "second"
        receipt = TOOL.build_bundle(idf, first)
        TOOL.build_bundle(idf, second)

        binary = (first / "protected-storage-partition-table.bin").read_bytes()
        require(len(binary) == 3072, "candidate binary must be exactly 3072 bytes")
        require(hashlib.sha256(binary).hexdigest().upper() == EXPECTED_BINARY_SHA256,
                "candidate binary hash mismatch")
        require((first / "protected-storage-partition-table.bin").read_bytes() ==
                (second / "protected-storage-partition-table.bin").read_bytes(),
                "candidate binary must reproduce exactly")
        require((first / "protected-storage-recovery-bundle.json").read_bytes() ==
                (second / "protected-storage-recovery-bundle.json").read_bytes(),
                "bundle receipt must reproduce exactly")
        require(receipt["schema"] == "OTPSTB0/v0" and
                receipt["status"] ==
                "OFFLINE-ONLY-NOT-AUTHORIZED-NOT-EXECUTED",
                "bundle must remain explicitly denied")
        require(receipt["candidate_binary"]["sha256"] == EXPECTED_BINARY_SHA256 and
                receipt["candidate_binary"]["entry_md5"] ==
                "2D318DA9C2F0566688277C3BA110A8E9",
                "receipt must bind exact binary and entry checksum")
        require(receipt["execution"] == {"commands": [], "attempts": 0} and
                not any(receipt["authority"].values()),
                "bundle must expose no execution or authority")
        require(sorted(path.name for path in first.iterdir()) == [
            "protected-storage-partition-table.bin",
            "protected-storage-recovery-bundle.json",
        ], "bundle must contain only the admitted offline artifacts")


def test_exact_rows_and_checksum_are_verified() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        base = Path(temporary)
        receipt = TOOL.build_bundle(fake_idf(base / "idf"), base / "bundle")
        rows = receipt["candidate_binary"]["rows"]
        require(rows[4] == {
            "name": "ot_auth", "type": 1, "subtype": 2,
            "offset": 0xF00000, "size_bytes": 0x10000, "flags": 1,
        }, "authorization partition row mismatch")
        require(rows[5] == {
            "name": "ot_state", "type": 0x40, "subtype": 0,
            "offset": 0xF10000, "size_bytes": 0xF0000, "flags": 0,
        }, "remaining state row mismatch")
        require(rows[-1]["offset"] + rows[-1]["size_bytes"] == 0x1000000,
                "candidate layout must end at exact 16 MiB boundary")


def test_wrong_framework_and_missing_tools_fail_closed() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        base = Path(temporary)
        cases = (
            (fake_idf(base / "wrong", version="ESP-IDF v6.0.1"), "wrong-out"),
            (base / "missing", "missing-out"),
        )
        for idf, name in cases:
            try:
                TOOL.build_bundle(idf, base / name)
            except TOOL.BundleError:
                pass
            else:
                raise AssertionError("invalid ESP-IDF environment was accepted")
            require(not (base / name).exists(),
                    "failed admission must publish no bundle")


def test_malformed_generated_artifacts_fail_without_output() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        base = Path(temporary)
        for mode in ("short", "wrong-row", "bad-md5", "bad-padding"):
            idf = fake_idf(base / f"idf-{mode}", mode=mode)
            output = base / f"out-{mode}"
            try:
                TOOL.build_bundle(idf, output)
            except TOOL.BundleError:
                pass
            else:
                raise AssertionError(f"malformed artifact was accepted: {mode}")
            require(not output.exists(),
                    "malformed artifact must publish no partial bundle")


def test_denied_recovery_plan_is_exact_and_coherent() -> None:
    recovery = json.loads(RECOVERY_PLAN.read_text(encoding="utf-8"))
    transition = json.loads(TRANSITION_PLAN.read_text(encoding="utf-8"))
    provisioning = json.loads(PROVISIONING_PLAN.read_text(encoding="utf-8"))

    require(recovery["schema"] == "OTPRB0/v0" and
            recovery["status"] ==
            "OFFLINE-CANDIDATE-APPLICATION-AND-ROUTE-FROZEN-PHYSICAL-ADMISSION-CLOSED",
            "recovery plan must remain incomplete and denied")
    require(recovery["application_capture_decision"] ==
            "docs/decisions/0019-retain-exact-installed-application-for-recovery.md",
            "recovery plan must bind the exact application capture decision")
    require(recovery["recovery_route_decision"] ==
            "docs/decisions/0020-offline-exact-rom-recovery-route.md",
            "recovery plan must bind the accepted offline route decision")
    candidate = recovery["candidate_partition_artifact"]
    require(candidate["binary_bytes"] == 3072 and
            candidate["binary_sha256"] == EXPECTED_BINARY_SHA256 and
            candidate["entry_md5"] ==
            "2D318DA9C2F0566688277C3BA110A8E9" and
            candidate["flash_offset"] == 32768 and
            candidate["physical_write_authorized"] is False,
            "recovery plan candidate artifact identity or authority changed")
    source = recovery["source_recovery_partition_artifact"]
    require(source["source_bytes"] == 452 and
            source["source_sha256"] ==
            "4F064C125AA641697E0539EAF9EDA9D1CDECAB46DD8FF387988B900F3EFE2389" and
            source["binary_bytes"] == 3072 and
            source["binary_sha256"] ==
            "84569AA2BADF3F7294042129B19D0B480784A93A550ADA3253B57BC92A0671AB" and
            source["builder"].endswith("gen_esp32part.py") and
            source["deterministically_buildable_from_public_source"] is True and
            source["retained_binary_required"] is False and
            source["recovery_write_authorized"] is False,
            "source recovery table must remain exact and reproducible offline")

    application = recovery["installed_application_recovery_artifact"]
    reconstruction = application["reconstruction_check"]
    require(application["required_sha256"] ==
            "A7D8E672CF9169F1D1D4E86EEFF80399C47A145E7D64904C207DD5F1B23F359B" and
            application["artifact_available"] is True and
            application["artifact_retention"] ==
            "PRIVATE-IGNORED-RECOVERY-ARTIFACT" and
            application["capture_result"] ==
            "OTPRBE1/v0/RECOVERY-APPLICATION-CAPTURED-ONLY" and
            application["captured_by"] ==
            "tests/hardware/OT-076-2026-08-17.md" and
            application["recovery_write_authorized"] is False and
            reconstruction["result"] == "NONMATCH-NOT-ACCEPTED-NOT-RETAINED" and
            reconstruction["produced_sha256"] ==
            "2B76593D5B797299DBB1A163C0152E0D463828C0B671887DA4AFAE11AFFE1D37" and
            reconstruction["produced_sha256"] != application["required_sha256"],
            "captured installed application must remain distinct from the rejected rebuild")

    custody = application["physical_admission_custody"]
    require(custody["proved_retained_copy_count"] == 1 and
            custody["independently_hashed_copy_count_required"] == 2 and
            custody["second_copy_must_be_separately_staged"] is True and
            custody["redundant_custody_currently_proved"] is False and
            custody["current_result"] == "DENY",
            "one private copy must not satisfy redundant physical custody")

    binding = recovery["fresh_execution_binding"]
    route = recovery["recovery_route"]
    admission = recovery["bundle_admission"]
    require(binding["operation_id"] is None and
            binding["evidence_set_id"] is None and
            binding["current_result"] == "DENY",
            "fresh operation/evidence binding must remain absent")
    require(route["schema"] == "OTRR0/v0" and
            route["exact_rom_route_id"] ==
            "OTRR0/v0/heltec-v4-ot064-source-restore" and
            route["accepted_offline"] is True and
            route["target_chip"] == "ESP32-S3" and
            route["flash_size_bytes"] == 16777216 and
            route["transport"] == "ESP32-S3-ROM-SERIAL" and
            route["tool"] == "esptool" and
            route["tool_version"] == "5.3.1" and
            route["baud"] == 115200 and
            route["before"] == "no-reset" and route["after"] == "no-reset" and
            route["ram_stub_allowed"] is False and
            route["connection_attempt_limit"] == 1 and
            route["automatic_retry_allowed"] is False and
            route["application_restore_policy"] == "UNCONDITIONAL" and
            route["offline_validation_result"] == "ACCEPT" and
            route["physical_execution_result"] == "DENY" and
            not route["partition_table_restore_physically_proved"] and
            not route["application_restore_physically_proved"] and
            not route["post_restore_boot_physically_proved"],
            "offline route identity/tooling must be exact while physical proof stays denied")
    security = route["security_state_preflight"]
    expected_security = security["expected"]
    observed_security = security["current_observation"]
    require(expected_security == {
                "secure_boot_enabled": False,
                "flash_encryption_enabled": False,
                "secure_download_mode_enabled": False,
            } and
            security["fresh_read_only_observation_required"] is True and
            security["same_nonzero_operation_id_required"] is True and
            security["same_nonzero_evidence_set_id_required"] is True and
            all(value is None for value in observed_security.values()) and
            security["unknown_result"] == "DENY" and
            security["mismatch_result"] == "DENY" and
            security["current_result"] == "DENY",
            "future security state must be fresh exact and currently denied")
    failure = route["post_first_write_failure_policy"]
    require(failure["applies_after_any_first_write_invocation"] is True and
            failure["close_connection_required"] is True and
            failure["remain_in_rom_required"] is True and
            failure["software_reset_allowed"] is False and
            failure["boot_success_claim_allowed"] is False and
            failure["recovery_state"] == "RECOVERY-UNCERTAIN" and
            failure["retain_private_artifacts_required"] is True and
            len(failure["minimum_private_journal_fields"]) == 6 and
            failure["sanitized_public_result"] ==
            "OTRR0/v0/RECOVERY-UNCERTAIN" and
            failure["transient_cleanup_only_after_evidence_preservation"] is True and
            failure["automatic_retry_allowed"] is False and
            failure["fresh_authorization_required"] is True and
            failure["current_result"] == "NOT-EXECUTED",
            "post-first-write failure must preserve uncertain recovery evidence")

    regions = route["ordered_restore_regions"]
    require([region["role"] for region in regions] ==
            ["installed-application", "source-partition-table"] and
            [region["offset"] for region in regions] == [65536, 32768] and
            [region["bytes"] for region in regions] == [470928, 3072],
            "recovery regions must remain application-first and source-table-last")
    require(admission["current_result"] == "DENY" and
            admission["exact_installed_application_artifact_present"] is True and
            admission["exact_recovery_route_accepted"] is True and
            admission["redundant_private_application_custody_proved"] is False and
            admission["bundle_complete"] is False and
            admission["partition_transition_authorized"] is False,
            "incomplete bundle must not authorize transition")
    require(recovery["execution_surface"]["commands"] == [] and
            recovery["execution_surface"]["attempts"] == 0 and
            recovery["execution_surface"]["device_port"] is None and
            recovery["execution_surface"]["device_access_authorized"] is False and
            not any(recovery["authorities"].values()),
            "recovery plan must expose no execution or authority")

    transition_binary = transition["transition"]["candidate_layout"][
        "partition_binary"]
    require(transition["recovery_bundle_plan"] == RECOVERY_PLAN.name and
            transition["status"] == "DESIGN-ONLY-ADMISSION-CLOSED" and
            transition["promotion_admission"]["current_result"] == "DENY" and
            transition_binary["sha256"] == candidate["binary_sha256"] and
            transition_binary["status"] == "OFFLINE-FROZEN-NOT-AUTHORIZED",
            "transition plan must reference and remain denied by the exact bundle")
    require(provisioning["recovery_bundle_plan"] == RECOVERY_PLAN.name and
            provisioning["status"] == "DESIGN-ONLY-NOT-ACTIVE-NOT-AUTHORIZED" and
            not any(provisioning["physical_authority"].values()),
            "provisioning plan must reference the denied recovery bundle")

    offline_route = transition["accepted_offline_recovery_route"]
    key_roles = transition["protected_key_role_requirements"]
    floor = transition["rollback_floor_requirements"]
    require(offline_route["route_id"] == route["exact_rom_route_id"] and
            offline_route["accepted_offline"] is True and
            offline_route["physically_executed"] is False and
            offline_route["physical_recovery_admitted"] is False and
            offline_route["expected_security_state"] == expected_security and
            offline_route["fresh_read_only_security_observation_required"] is True and
            offline_route["security_observation_requires_same_operation_and_evidence_set"] is True and
            offline_route["unknown_or_mismatched_security_state_result"] == "DENY" and
            offline_route["post_first_write_failure_policy"].endswith(
                "#recovery_route/post_first_write_failure_policy") and
            offline_route["current_result"] == "DENY-PHYSICAL-EXECUTION",
            "transition must consume only the accepted offline route")
    require(key_roles["requirements_defined"] is True and
            key_roles["distinct_keys_required"] is True and
            key_roles["provider_kind"] == "ESP32S3_HMAC_UP_EFUSE" and
            key_roles["nvs_encryption_hmac_key"]["provider_selected_offline"] is True and
            key_roles["nvs_encryption_hmac_key"]["physical_provider_admitted"] is False and
            key_roles["bond_binding_prf_hmac_key"]["provider_selected_offline"] is True and
            key_roles["bond_binding_prf_hmac_key"]["physical_provider_admitted"] is False and
            key_roles["current_result"] == "DENY",
            "protected key roles must select only provider types")
    require(floor["requirements_defined"] is True and
            floor["independent_from_all_rewritable_flash"] is True and
            floor["monotonic_non_rollbackable"] is True and
            floor["provider_class"] is None and
            floor["provider_selected_offline_conditionally"] is False and
            floor["reviewed_custom_user_efuse_candidate"] ==
            "REJECTED-RS-CODING-UNIT-SUPPORTS-ONE-WRITE-NOT-REPEATED-ADVANCES" and
            floor["exact_field_selected"] is False and
            floor["provisioned"] is False and
            floor["current_result"] == "DENY",
            "rollback floor must remain unselected after candidate rejection")


def test_no_physical_surface_and_registered_host_gate() -> None:
    source = TOOL_PATH.read_text(encoding="utf-8")
    for forbidden in (
        r"\besptool\b", r"\bserial\b", r"\bwrite_flash\b",
        r"\berase_flash\b", r"--port", r"\bCOM\d+\b",
    ):
        require(re.search(forbidden, source, re.IGNORECASE) is None,
                f"physical execution surface present: {forbidden}")
    require("gen_esp32part.py" in source and
            "version.cmake" in source and
            'REQUIRED_IDF_VERSION = "ESP-IDF v6.0.2"' in source,
            "tool must use the exact pinned ESP-IDF version and partition generator")
    runner = HOST_RUNNER.read_text(encoding="utf-8")
    require(r"tests\host\heltec_v4_protected_storage_recovery_bundle_tests.py" in
            runner, "focused bundle tests are not registered")


def main() -> int:
    tests = (
        test_exact_bundle_is_deterministic_and_denied,
        test_exact_rows_and_checksum_are_verified,
        test_wrong_framework_and_missing_tools_fail_closed,
        test_malformed_generated_artifacts_fail_without_output,
        test_denied_recovery_plan_is_exact_and_coherent,
        test_no_physical_surface_and_registered_host_gate,
    )
    for test in tests:
        test()
        print(f"PASS: {test.__name__}")
    print(f"{len(tests)} protected-storage recovery-bundle groups passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
