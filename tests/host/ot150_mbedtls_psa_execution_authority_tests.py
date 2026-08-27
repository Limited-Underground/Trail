#!/usr/bin/env python3
"""Adversarial host tests for the OT-150 future execution authority."""

from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = ROOT / "tools" / "ot150_mbedtls_psa_execution_authority.py"
SPEC = importlib.util.spec_from_file_location("ot150_authority_tests", TOOL_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("OT-150 authority tool unavailable")
contract = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(contract)


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def artifact(name: str, payload: bytes) -> dict[str, object]:
    return {"name": name, "bytes": len(payload), "sha256": sha256(payload)}


class Fixture:
    def __init__(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="ot150-authority-test-")
        self.root = Path(self.temporary.name).resolve()
        self.stack = contextlib.ExitStack()
        self.benchmark_payload = b"ot150 candidate fixture"
        self.restore_payload = b"ot150 exact current Trail fixture"
        self.benchmark = self.root / "ot149_mbedtls_psa_bench.bin"
        self.restore = self.root / contract.RESTORE_NAME
        self.adapter = self.root / contract.ADAPTER_RELATIVE
        self.authority_path = self.root / contract.AUTHORITY_RELATIVE
        self.preparation_path = self.root / contract.PREPARATION_RELATIVE
        self.runtime_payloads = {
            "protocol_transport": (contract.PROTOCOL_RELATIVE, b"protocol\n"),
            "coordinator": (contract.COORDINATOR_RELATIVE, b"coordinator\n"),
            "hardware_adapter": (contract.ADAPTER_RELATIVE, b"adapter\n"),
            "execution_authority_tool": (contract.CONTRACT_TOOL_RELATIVE, b"authority\n"),
            "bundle_validator": (contract.BUNDLE_TOOL_RELATIVE, b"bundle\n"),
            "matched_resource_validator": (
                contract.ACCOUNTING_TOOL_RELATIVE,
                b"accounting\n",
            ),
        }
        self._write(self.benchmark, self.benchmark_payload)
        self._write(self.restore, self.restore_payload)
        for relative, payload in self.runtime_payloads.values():
            self._write(self.root / relative, payload)
        self.resource_binding = {
            "path": contract.RESOURCE_RESULT_RELATIVE,
            "bytes": 321,
            "raw_sha256": "b" * 64,
            "canonical_sha256": "c" * 64,
        }
        self.fixed_bindings = {"fixture": {"path": "fixture", "bytes": 1, "raw_sha256": "d" * 64}}
        self.preparation = self._preparation()
        self.preparation_raw = contract.canonical_document(self.preparation)
        self._write(self.preparation_path, self.preparation_raw)

    @staticmethod
    def _write(path: Path, payload: bytes) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)

    def _runtime(self) -> dict[str, object]:
        return {
            role: {
                "path": relative,
                "bytes": len(payload),
                "raw_sha256": sha256(payload),
            }
            for role, (relative, payload) in self.runtime_payloads.items()
        }

    def _runs(self, side: str) -> list[dict[str, object]]:
        common = {
            "bootloader_bin": artifact("bootloader.bin", b"boot"),
            "partition_table_bin": artifact("partition-table.bin", b"partition"),
            "generated_sdkconfig": artifact("sdkconfig", b"config"),
        }
        linked = {
            "application_bin": artifact(
                self.benchmark.name if side == "candidate" else "control.bin",
                self.benchmark_payload if side == "candidate" else b"control",
            ),
            "application_elf": artifact(f"{side}.elf", f"{side}-elf".encode()),
            "linker_map": artifact(f"{side}.map", f"{side}-map".encode()),
            "size_report_json2": artifact(f"{side}.json", f"{side}-json".encode()),
            **common,
        }
        return [
            {"run": run, "artifacts": json.loads(json.dumps(linked))}
            for run in ("A", "B")
        ]

    def _preparation(self) -> dict[str, object]:
        candidate_runs = self._runs("candidate")
        benchmark = candidate_runs[0]["artifacts"]["application_bin"]
        return {
            "schema": contract.PREPARATION_SCHEMA,
            "version": 0,
            "artifact_kind": "mbedtls_psa_executable_resource_bundle_preparation",
            "preparation_id": contract.PREPARATION_ID,
            "recorded_date": contract.RECORDED_DATE,
            "status": "host_bundle_frozen_fresh_owner_authority_required",
            "bindings": self.fixed_bindings,
            "candidate": {
                "id": "esp_idf_mbedtls_psa",
                "version": "4.1.0",
                "role": "comparison",
                "selection_eligible": False,
                "operations": list(contract.bundle_contract.OPERATIONS),
                "unavailable_operations": list(contract.bundle_contract.UNAVAILABLE_OPERATIONS),
            },
            "build_policy": {
                "project_version": contract.PROJECT_VER,
                "run_order": ["A", "B"],
                "a_b_byte_and_hash_equality_required_per_side": True,
                "candidate_control_common_artifacts_equal": list(contract.bundle_contract.COMMON_ARTIFACT_ROLES),
                "candidate_control_linkage_artifacts_distinct": list(contract.bundle_contract.DISTINCT_LINKAGE_ROLES),
                "json2_format": "esp_idf_size_json2_v1.2",
                "resource_result_admitted_by_this_preparation": False,
            },
            "builds": {"candidate": candidate_runs, "control": self._runs("control")},
            "future_public_outputs": {
                "canonical_preparation": {"path": contract.PREPARATION_RELATIVE, "schema": contract.PREPARATION_SCHEMA},
                "matched_resource_result": {"path": contract.RESOURCE_RESULT_RELATIVE, "schema": "OTMRAR1", "admitted": False},
                "json2_reports": {
                    "candidate": {"path": contract.bundle_contract.CANDIDATE_REPORT_RELATIVE, "format": "esp_idf_size_json2_v1.2"},
                    "control": {"path": contract.bundle_contract.CONTROL_REPORT_RELATIVE, "format": "esp_idf_size_json2_v1.2"},
                },
            },
            "runtime": self._runtime(),
            "images": {
                "future_benchmark_write": {
                    "role": "candidate_application_bin",
                    "name": benchmark["name"],
                    "bytes": benchmark["bytes"],
                    "sha256": benchmark["sha256"],
                    "application_offset": contract.APPLICATION_OFFSET,
                    "future_writable_after_separate_authority_only": True,
                    "control_application_writable": False,
                    "other_artifacts_writable": False,
                },
                "restore": {
                    "name": contract.RESTORE_NAME,
                    "bytes": len(self.restore_payload),
                    "sha256": sha256(self.restore_payload),
                    "exact_readback_and_restore_required_by_future_authority": True,
                },
            },
            "privacy": {
                "build_artifacts_record_name_only": True,
                "runtime_and_fixed_bindings_repo_relative_only": True,
                "absolute_paths_recorded": False,
                "serial_ports_recorded": False,
                "device_identifiers_recorded": False,
                "raw_capture_recorded": False,
            },
            "authority": contract.bundle_contract._authority(),
            "claims": contract.bundle_contract._claims(),
        }

    def __enter__(self):
        self.stack.enter_context(mock.patch.object(contract, "ROOT", self.root))
        self.stack.enter_context(mock.patch.object(contract.bundle_contract, "ROOT", self.root))
        self.stack.enter_context(mock.patch.object(contract, "RESTORE_BYTES", len(self.restore_payload)))
        self.stack.enter_context(mock.patch.object(contract, "RESTORE_SHA256", sha256(self.restore_payload)))
        self.stack.enter_context(mock.patch.object(contract.bundle_contract, "fixed_bindings", return_value=self.fixed_bindings))
        self.stack.enter_context(mock.patch.object(contract, "_resource_binding", return_value=self.resource_binding))
        return self

    def __exit__(self, *args):
        self.stack.close()
        self.temporary.cleanup()

    def authority(self) -> dict[str, object]:
        return contract.build_authority(
            self.preparation,
            self.preparation_raw,
            self.benchmark,
            self.restore,
            self.adapter,
            owner_authorization_granted=True,
        )

    def persist_authority(self, value: dict[str, object]) -> None:
        self._write(self.authority_path, contract.canonical_document(value))


class AuthorityTests(unittest.TestCase):
    def test_01_preparation_supplies_exact_execution_binding(self) -> None:
        with Fixture() as fixture:
            binding = contract.load_preparation_binding(
                fixture.benchmark, fixture.restore, fixture.adapter, recovery=False
            )
            self.assertEqual(binding["benchmark_name"], fixture.benchmark.name)
            self.assertEqual(binding["benchmark_bytes"], len(fixture.benchmark_payload))
            self.assertEqual(binding["expected_frame_count"], 1_015)
            self.assertEqual(binding["protocol_start"], contract.START)
            self.assertEqual(binding["restore_sha256"], sha256(fixture.restore_payload))

    def test_02_exact_candidate_restore_and_runtime_are_required(self) -> None:
        with Fixture() as fixture:
            fixture.benchmark.write_bytes(b"tampered")
            with self.assertRaisesRegex(contract.ContractError, "benchmark image mismatch"):
                contract.load_preparation_binding(
                    fixture.benchmark, fixture.restore, fixture.adapter, recovery=False
                )
        with Fixture() as fixture:
            fixture.adapter.write_bytes(b"tampered")
            with self.assertRaisesRegex(contract.ContractError, "runtime binding mismatch|adapter"):
                contract.load_preparation_binding(
                    fixture.benchmark, fixture.restore, fixture.adapter, recovery=False
                )
        for role in ("bundle_validator", "matched_resource_validator"):
            with self.subTest(role=role), Fixture() as fixture:
                relative, unused_payload = fixture.runtime_payloads[role]
                fixture._write(fixture.root / relative, b"tampered\n")
                with self.assertRaisesRegex(contract.ContractError, "runtime binding mismatch"):
                    contract.load_preparation_binding(
                        fixture.benchmark,
                        fixture.restore,
                        fixture.adapter,
                        recovery=False,
                    )

    def test_03_explicit_owner_authorization_is_mandatory(self) -> None:
        with Fixture() as fixture:
            with self.assertRaisesRegex(contract.ContractError, "owner authorization"):
                contract.build_authority(
                    fixture.preparation, fixture.preparation_raw,
                    fixture.benchmark, fixture.restore, fixture.adapter,
                    owner_authorization_granted=False,
                )

    def test_04_authority_is_one_attempt_app_only_and_control_never_writable(self) -> None:
        with Fixture() as fixture:
            authority = fixture.authority()
            self.assertEqual(authority["execution"]["attempt_count"], 1)
            self.assertEqual(authority["execution"]["application_offset"], 0x10000)
            self.assertTrue(authority["execution"]["application_only_writes"])
            self.assertFalse(authority["execution"]["control_application_writable"])
            self.assertFalse(authority["execution"]["radio_allowed"])
            self.assertTrue(authority["consumption"]["consumed_on_success_or_abort"])
            self.assertFalse(authority["consumption"]["reusable"])
            self.assertEqual(authority["resource_result"], fixture.resource_binding)
            self.assertTrue(all(value is False for value in authority["claims"].values()))

    def test_05_authority_tamper_fails_closed(self) -> None:
        with Fixture() as fixture:
            authority = fixture.authority()
            authority["execution"]["radio_allowed"] = True
            with self.assertRaisesRegex(contract.ContractError, "authority boundary"):
                contract.validate_authority(
                    authority, fixture.preparation, fixture.preparation_raw,
                    fixture.benchmark, fixture.restore, fixture.adapter,
                )

    def test_06_execution_requires_candidate_but_recovery_forbids_it(self) -> None:
        with Fixture() as fixture:
            authority = fixture.authority()
            fixture.persist_authority(authority)
            digest = contract.validate_execution_authority(
                fixture.authority_path, fixture.benchmark, fixture.restore,
                fixture.adapter, recovery=False,
            )
            self.assertEqual(digest, sha256(fixture.authority_path.read_bytes()))
            with self.assertRaisesRegex(contract.ContractError, "benchmark unavailable"):
                contract.validate_execution_authority(
                    fixture.authority_path, None, fixture.restore,
                    fixture.adapter, recovery=False,
                )
            fixture.benchmark.unlink()
            self.assertEqual(
                contract.validate_execution_authority(
                    fixture.authority_path, None, fixture.restore,
                    fixture.adapter, recovery=True,
                ),
                digest,
            )
            with self.assertRaisesRegex(contract.ContractError, "recovery benchmark must be absent"):
                contract.validate_execution_authority(
                    fixture.authority_path, fixture.root / fixture.benchmark.name,
                    fixture.restore, fixture.adapter, recovery=True,
                )

    def test_07_preparation_and_resource_are_both_hash_bound(self) -> None:
        with Fixture() as fixture:
            authority = fixture.authority()
            self.assertEqual(authority["preparation"]["raw_sha256"], sha256(fixture.preparation_raw))
            self.assertEqual(authority["resource_result"]["raw_sha256"], "b" * 64)
            changed = json.loads(json.dumps(fixture.preparation))
            changed["images"]["future_benchmark_write"]["control_application_writable"] = True
            with self.assertRaises(contract.ContractError):
                contract.build_authority(
                    changed, contract.canonical_document(changed),
                    fixture.benchmark, fixture.restore, fixture.adapter,
                    owner_authorization_granted=True,
                )

    def test_08_json_and_exclusive_output_are_fail_closed(self) -> None:
        value = {"schema": "x", "version": 0}
        self.assertEqual(contract.decode_canonical(contract.canonical_document(value), "fixture"), value)
        with self.assertRaisesRegex(contract.ContractError, "not canonical"):
            contract.decode_canonical(b'{"schema": "x","version":0}\n', "fixture")
        with self.assertRaisesRegex(contract.ContractError, "duplicate key"):
            contract.decode_canonical(b'{"schema":"x","schema":"y"}\n', "fixture")
        with Fixture() as fixture:
            output = fixture.root / contract.AUTHORITY_RELATIVE
            contract.write_new(output, value, contract.AUTHORITY_RELATIVE)
            with self.assertRaisesRegex(contract.ContractError, "already exists"):
                contract.write_new(output, value, contract.AUTHORITY_RELATIVE)

    def test_09_current_trail_restore_identity_is_frozen(self) -> None:
        self.assertEqual(contract.RESTORE_BYTES, 500_944)
        self.assertEqual(
            contract.RESTORE_SHA256,
            "f2a58414f82eed585ba90bf7671b06c8ebfaa82e8b23f6ac2093de95143b4e0e",
        )

    def test_10_tool_has_no_hardware_or_implicit_authority_surface(self) -> None:
        source = TOOL_PATH.read_text(encoding="utf-8")
        for forbidden in (
            "import serial", "import subprocess", "write-flash", "read-flash",
            "erase-flash", "list_ports", "owner_authorization_granted=True\n        )",
        ):
            self.assertNotIn(forbidden, source)
        self.assertIn('ADAPTER_RELATIVE = "tools/ot150_mbedtls_psa_hardware_adapter.py"', source)
        self.assertIn('BUNDLE_TOOL_RELATIVE = "tools/ot150_mbedtls_psa_bundle.py"', source)
        self.assertIn(
            'ACCOUNTING_TOOL_RELATIVE = "tools/crypto_matched_resource_accounting.py"',
            source,
        )
        self.assertIn("resource_result", source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
