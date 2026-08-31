#!/usr/bin/env python3
"""Adversarial host tests for the OT-130 immutable bundle and authority."""

from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = ROOT / "tools" / "ot130_monocypher_bundle_authority.py"
SPEC = importlib.util.spec_from_file_location("ot130_contract", TOOL_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("OT-130 contract unavailable")
contract = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(contract)


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


class Fixture:
    def __init__(self, directory: str) -> None:
        self.root = Path(directory)
        self.source_payload = b"source-v0\n"
        self.runtime_payload = b"transport-v0\n"
        self.lineage_payload = b"lineage-v0\n"
        self.coordinator_payload = b"coordinator-v0\n"
        self.benchmark_payload = b"benchmark-v0"
        self.restore_payload = b"restore-v0"

        self.source = (("source.txt", sha256(self.source_payload)),)
        self.runtime = (
            ("protocol_transport", "transport.py", sha256(self.runtime_payload)),
        )
        self.lineage = (
            ("consumed_ot127_authority", "old-authority.json", sha256(self.lineage_payload)),
            ("ot128_abort_receipt", "abort.json", sha256(self.lineage_payload)),
        )
        self.coordinator_relative = "tools/ot130_monocypher_coordinator.py"
        self.contract_tool_relative = "tools/ot130_monocypher_bundle_authority.py"
        self.coordinator = self.root / self.coordinator_relative
        self.benchmark = self.root / "ot129_monocypher_protocol_bench.bin"
        self.restore = self.root / "opentrail_heltec_v4_bench.bin"

        self._write(self.root / "source.txt", self.source_payload)
        self._write(self.root / "transport.py", self.runtime_payload)
        self._write(self.root / "old-authority.json", self.lineage_payload)
        self._write(self.root / "abort.json", self.lineage_payload)
        self._write(self.coordinator, self.coordinator_payload)
        self._write(self.root / self.contract_tool_relative, b"contract-tool-v0\n")
        self._write(self.benchmark, self.benchmark_payload)
        self._write(self.restore, self.restore_payload)

    @staticmethod
    def _write(path: Path, payload: bytes) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)

    def patches(self):
        return (
            mock.patch.object(contract, "ROOT", self.root),
            mock.patch.object(contract, "SOURCE_BINDINGS", self.source),
            mock.patch.object(contract, "HISTORICAL_SOURCE_STORAGE", {}),
            mock.patch.object(contract, "RUNTIME_BINDINGS", self.runtime),
            mock.patch.object(contract, "LINEAGE_BINDINGS", self.lineage),
            mock.patch.object(contract, "COORDINATOR_RELATIVE", self.coordinator_relative),
            mock.patch.object(contract, "CONTRACT_TOOL_RELATIVE", self.contract_tool_relative),
            mock.patch.object(contract, "BENCHMARK_BYTES", len(self.benchmark_payload)),
            mock.patch.object(contract, "BENCHMARK_SHA256", sha256(self.benchmark_payload)),
            mock.patch.object(contract, "RESTORE_BYTES", len(self.restore_payload)),
            mock.patch.object(contract, "RESTORE_SHA256", sha256(self.restore_payload)),
        )


class ContractTests(unittest.TestCase):
    def _with_fixture(self, fixture: Fixture):
        stack = self.enterContext
        for patcher in fixture.patches():
            stack(patcher)

    def test_01_all_current_fixed_repository_bindings_match(self) -> None:
        historical_config = (
            "firmware/targets/heltec_v4_bench/sdkconfig.defaults",
            "9186abaa6bd99429bb6d7d32f52f772b02dc122145438dc1547d2b94b948fe4a",
        )
        self.assertIn(historical_config, contract.SOURCE_BINDINGS)
        storage_path, storage_sha256 = contract.HISTORICAL_SOURCE_STORAGE[
            historical_config[0]
        ]
        self.assertEqual(
            sha256((ROOT / storage_path).read_bytes()),
            storage_sha256,
        )
        sources = contract._ordered_bindings(contract.SOURCE_BINDINGS)
        runtime = contract._named_bindings(contract.RUNTIME_BINDINGS)
        lineage = contract._named_bindings(contract.LINEAGE_BINDINGS)
        self.assertEqual(len(sources), 15)
        self.assertIn(
            {"path": historical_config[0], "raw_sha256": historical_config[1]},
            sources,
        )
        self.assertEqual(
            set(runtime), {"protocol_transport", "frame_parser", "frame_schema"}
        )
        self.assertEqual(
            set(lineage),
            {
                "ot123_preparation",
                "consumed_ot127_authority",
                "ot128_abort_receipt",
                "ot129_decision",
                "ot129_evidence",
            },
        )

    def test_02_current_coordinator_is_runtime_bound(self) -> None:
        coordinator = ROOT / contract.COORDINATOR_RELATIVE
        self.assertTrue(coordinator.is_file())
        binding = contract._derived_binding(contract.COORDINATOR_RELATIVE)
        self.assertEqual(binding["raw_sha256"], sha256(coordinator.read_bytes()))
        tool_binding = contract._derived_binding(contract.CONTRACT_TOOL_RELATIVE)
        self.assertEqual(tool_binding["raw_sha256"], sha256(TOOL_PATH.read_bytes()))

    def test_03_coordinator_digest_is_derived_and_then_immutable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot130-contract-test-") as directory:
            fixture = Fixture(directory)
            self._with_fixture(fixture)
            prepared = contract.build_preparation(fixture.benchmark, fixture.restore)
            expected = sha256(fixture.coordinator_payload)
            self.assertEqual(prepared["runtime"]["coordinator"]["raw_sha256"], expected)
            self.assertEqual(
                prepared["runtime"]["coordinator"]["digest_source"],
                "derived_from_final_file_at_preparation",
            )
            contract.validate_preparation(prepared, fixture.benchmark, fixture.restore)
            fixture.coordinator.write_bytes(b"coordinator-v1\n")
            with self.assertRaisesRegex(
                contract.ContractError, "preparation boundary mismatch"
            ):
                contract.validate_preparation(
                    prepared, fixture.benchmark, fixture.restore
                )

    def test_04_exact_images_and_names_are_required(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot130-contract-test-") as directory:
            fixture = Fixture(directory)
            self._with_fixture(fixture)
            wrong_name = fixture.root / "wrong.bin"
            wrong_name.write_bytes(fixture.benchmark_payload)
            with self.assertRaisesRegex(contract.ContractError, "image identity mismatch"):
                contract.build_preparation(wrong_name, fixture.restore)
            fixture.benchmark.write_bytes(b"changed")
            with self.assertRaisesRegex(contract.ContractError, "image digest mismatch"):
                contract.build_preparation(fixture.benchmark, fixture.restore)

    def test_05_preparation_closes_execution_privacy_and_claims(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot130-contract-test-") as directory:
            fixture = Fixture(directory)
            self._with_fixture(fixture)
            value = contract.build_preparation(fixture.benchmark, fixture.restore)
            self.assertEqual(
                value["build"]["accepted_compile_only_build_reference"],
                "ot129_evidence",
            )
            self.assertEqual(value["build"]["fresh_independent_builds_verified"], 2)
            self.assertEqual(value["build"]["compiler_warning_count_each"], 0)
            self.assertTrue(value["build"]["artifact_tuple_equality_verified"])
            self.assertEqual(
                value["build"]["artifact_tuple"]["application_bin"]["sha256"],
                "d94f2b2a823f23500d3592aa4e6aa1004b2f13c0865ea071f2598023321bf268",
            )
            self.assertEqual(
                value["build"]["artifact_tuple"]["sdkconfig"]["sha256"],
                contract.SDKCONFIG_SHA256,
            )
            self.assertTrue(value["build"]["application_digest_bound"])
            execution = value["execution"]
            self.assertEqual(execution["attempt_count"], 1)
            self.assertEqual(execution["node_count"], 2)
            self.assertEqual(execution["application_offset"], 65536)
            self.assertTrue(execution["application_only_writes"])
            self.assertTrue(execution["both_installed_trail_readbacks_before_journal"])
            self.assertTrue(execution["all_preflight_devices_reset_before_journal"])
            self.assertTrue(execution["benchmark_readback_before_capture"])
            self.assertTrue(execution["display_reset_and_visual_preflight_required"])
            self.assertTrue(execution["capture_transport_owns_exactly_one_pre_start_reset"])
            self.assertFalse(execution["reset_or_reopen_after_start_allowed"])
            self.assertTrue(execution["restore_each_touched_node"])
            self.assertFalse(execution["recovery_requires_benchmark_artifact"])
            self.assertTrue(execution["recovery_retry_until_restored_required"])
            self.assertTrue(execution["distinct_endpoint_values_required"])
            self.assertTrue(execution["single_bound_flash_capture_backend_required"])
            self.assertFalse(execution["radio_allowed"])
            self.assertFalse(execution["selection_allowed"])
            self.assertTrue(value["privacy"]["failure_code_and_bounded_counters_only"])
            self.assertFalse(value["privacy"]["raw_capture_recorded"])
            self.assertFalse(value["privacy"]["exception_text_recorded"])
            self.assertTrue(value["claims"]["immutable_bundle_prepared"])
            self.assertFalse(value["claims"]["execution_authorized"])
            self.assertFalse(value["claims"]["hardware_accessed"])

    def test_06_authority_requires_explicit_owner_grant_and_binds_preparation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot130-contract-test-") as directory:
            fixture = Fixture(directory)
            self._with_fixture(fixture)
            preparation = contract.build_preparation(
                fixture.benchmark, fixture.restore
            )
            raw = contract.canonical_document(preparation)
            with self.assertRaisesRegex(
                contract.ContractError, "owner authorization absent"
            ):
                contract.build_authority(
                    preparation,
                    raw,
                    fixture.benchmark,
                    fixture.restore,
                    owner_authorization_granted=False,
                )
            authority = contract.build_authority(
                preparation,
                raw,
                fixture.benchmark,
                fixture.restore,
                owner_authorization_granted=True,
            )
            result = contract.validate_authority(
                authority,
                preparation,
                raw,
                fixture.benchmark,
                fixture.restore,
            )
            self.assertTrue(result["phase_two_execution_authorized"])
            self.assertEqual(authority["preparation"]["raw_sha256"], sha256(raw))
            self.assertEqual(
                authority["preparation"]["coordinator_sha256"],
                sha256(fixture.coordinator_payload),
            )
            self.assertFalse(authority["consumption"]["reusable"])
            self.assertFalse(authority["consumption"]["continuing_authority"])
            self.assertTrue(authority["consumption"]["consumed_on_success_or_abort"])
            self.assertTrue(all(value is False for value in authority["claims"].values()))

    def test_07_authority_revalidates_live_bundle_and_rejects_tampering(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot130-contract-test-") as directory:
            fixture = Fixture(directory)
            self._with_fixture(fixture)
            preparation = contract.build_preparation(
                fixture.benchmark, fixture.restore
            )
            raw = contract.canonical_document(preparation)
            authority = contract.build_authority(
                preparation,
                raw,
                fixture.benchmark,
                fixture.restore,
                owner_authorization_granted=True,
            )
            authority["execution"]["radio_allowed"] = True
            with self.assertRaisesRegex(contract.ContractError, "authority boundary mismatch"):
                contract.validate_authority(
                    authority,
                    preparation,
                    raw,
                    fixture.benchmark,
                    fixture.restore,
                )
            fixture.coordinator.write_bytes(b"coordinator-v1\n")
            with self.assertRaisesRegex(
                contract.ContractError, "preparation boundary mismatch"
            ):
                contract.build_authority(
                    preparation,
                    raw,
                    fixture.benchmark,
                    fixture.restore,
                    owner_authorization_granted=True,
                )

    def test_08_json_is_canonical_duplicate_safe_and_nonfinite_safe(self) -> None:
        value = {"schema": "x", "version": 0}
        raw = contract.canonical_document(value)
        self.assertEqual(contract.decode_canonical(raw, "fixture"), value)
        with self.assertRaisesRegex(contract.ContractError, "not canonical"):
            contract.decode_canonical(b'{"schema": "x","version":0}\n', "fixture")
        with self.assertRaisesRegex(contract.ContractError, "duplicate key"):
            contract.decode_canonical(b'{"schema":"x","schema":"y"}\n', "fixture")
        with self.assertRaises(contract.ContractError):
            contract.decode_canonical(b'{"value":NaN}\n', "fixture")

    def test_09_new_file_write_is_exclusive_and_path_bound(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot130-contract-test-") as directory:
            root = Path(directory)
            relative = "evidence/preparation.json"
            output = root / relative
            with mock.patch.object(contract, "ROOT", root):
                contract.write_new(output, {"value": 1}, relative)
                self.assertEqual(output.read_bytes(), b'{"value":1}\n')
                with self.assertRaisesRegex(contract.ContractError, "already exists"):
                    contract.write_new(output, {"value": 1}, relative)
                with self.assertRaisesRegex(contract.ContractError, "identity mismatch"):
                    contract.write_new(root / "other.json", {"value": 1}, relative)

    def test_10_tool_has_no_hardware_execution_surface(self) -> None:
        source = TOOL_PATH.read_text(encoding="utf-8")
        for forbidden in (
            "import serial",
            "import subprocess",
            "-m\", \"esptool",
            "write-flash",
            "read-flash",
            "--port",
        ):
            self.assertNotIn(forbidden, source)
        self.assertIn(
            'COORDINATOR_RELATIVE = "tools/ot130_monocypher_coordinator.py"',
            source,
        )
        self.assertIn(
            'CONTRACT_TOOL_RELATIVE = "tools/ot130_monocypher_bundle_authority.py"',
            source,
        )
        self.assertNotIn("placeholder", source.lower().split("\n", 12)[-1])

    def test_11_main_fails_closed_while_coordinator_is_absent(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot130-contract-test-") as directory:
            root = Path(directory)
            benchmark = root / contract.BENCHMARK_NAME
            restore = root / contract.RESTORE_NAME
            benchmark.write_bytes(b"x")
            restore.write_bytes(b"y")
            with mock.patch.object(contract, "ROOT", root):
                output = io.StringIO()
                with contextlib.redirect_stdout(output):
                    code = contract.main(
                        [
                            "prepare",
                            "--benchmark-app",
                            str(benchmark),
                            "--restore-app",
                            str(restore),
                        ]
                    )
            self.assertEqual(code, 2)
            self.assertEqual(
                output.getvalue().strip(),
                "ERROR: OT-130 immutable bundle/authority validation failed",
            )
            self.assertFalse((root / contract.PREPARATION_RELATIVE).exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
