#!/usr/bin/env python3
"""Focused adversarial tests for the host-only OT-157 bundle."""

from __future__ import annotations

import ast
import copy
import hashlib
import json
import sys
import tempfile
import unittest
from contextlib import ExitStack
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import ot157_noise_xk_radio_bundle as bundle  # noqa: E402


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


class Fixture:
    def __init__(self, directory: str) -> None:
        self.root = Path(directory).resolve() / "repo"
        self.root.mkdir()
        self.restore_payload = b"exact-trail-restore"
        self.restore = self.root / "private" / "opentrail_heltec_v4_bench.bin"
        self._write(self.restore, self.restore_payload)

        self.builds: dict[str, dict[str, Path]] = {}
        artifact_descriptors: dict[str, dict[str, object]] = {}
        for run in ("A", "B"):
            artifacts: dict[str, Path] = {}
            descriptors: dict[str, object] = {}
            for role, name in bundle.BUILD_ARTIFACTS:
                payload = f"{role}-identical\n".encode("ascii")
                path = self.root / "private-builds" / run / name
                self._write(path, payload)
                artifacts[role] = path
                descriptors[role] = {
                    "name": name, "bytes": len(payload), "sha256": sha256(payload)
                }
            self.builds[run] = artifacts
            artifact_descriptors[run] = descriptors

        self.parent = {
            "schema": bundle.PARENT_SCHEMA,
            "version": 0,
            "preparation_id": bundle.PARENT_ID,
            "status": "host_only_immutable_bundle_frozen_fresh_owner_authority_required",
            "authority": {"owner_one_attempt_authority_accepted": False},
            "claims": {"immutable_executable_bundle_frozen": True},
            "firmware": {
                "target": "heltec_wifi_lora_32_v4_2",
                "project": "ot153_noise_xk_radio_cost",
                "build_policy": {
                    "fixed_project_version": "ot153-noise-xk-radio-v0",
                    "ccache_enabled": False,
                },
                "build_evidence": {
                    "status": "reproduced", "run_order": ["A", "B"],
                    "independent_build_roots_required": True,
                    "byte_identical_output_tuple_required": True,
                    "output_roles": [role for role, unused in bundle.BUILD_ARTIFACTS],
                    "runs": [
                        {"run": "A", "artifacts": artifact_descriptors["A"]},
                        {"run": "B", "artifacts": artifact_descriptors["B"]},
                    ],
                },
                "future_application_write": {
                    "artifact_role": "application_bin", "application_offset": 65536,
                    "writable_only_after_separate_authority": True,
                    "bootloader_writable": False, "partition_table_writable": False,
                    "nvs_writable": False,
                },
            },
            "execution_contract": {
                "node_count": 2,
                "both_nodes_present_simultaneously_required": True,
                "role_cycles": [
                    {"cycle": 1, "initiator": "A", "responder": "B"},
                    {"cycle": 2, "initiator": "B", "responder": "A"},
                ],
                "message_wire_bytes": [48, 48, 64],
                "raw_message_schema": "OTNXK0/v0",
                "packet_v1_wrapper_used": False,
                "ota1_ack_wrapper_used": False,
                "radio_payload_wire_bytes": 736,
                "transmissions": 14,
                "theoretical_airtime_us": 1_447_424,
                "one_bounded_whole_handshake_restart_per_direction": True,
                "success_or_abort_consumes_future_authority": True,
            },
            "images": {
                "restore": {
                    "name": self.restore.name,
                    "bytes": len(self.restore_payload),
                    "sha256": sha256(self.restore_payload),
                    "application_offset": 65536,
                    "exact_readback_and_restore_required": True,
                }
            },
        }
        self.parent_raw = bundle.canonical_document(self.parent)
        self._write(self.root / bundle.PARENT_RELATIVE, self.parent_raw)

        self.runtime: dict[str, Path] = {}
        self.runtime_sha: dict[str, str] = {}
        for role, relative in bundle.RUNTIME_BINDINGS:
            payload = f"# {role}\n".encode("ascii")
            path = self.root / relative
            self._write(path, payload)
            self.runtime[role] = path
            self.runtime_sha[role] = sha256(payload)

    @staticmethod
    def _write(path: Path, payload: bytes) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)

    def patches(self) -> tuple[mock._patch, ...]:
        return (
            mock.patch.object(bundle, "ROOT", self.root),
            mock.patch.object(bundle, "PARENT_RAW_SHA256", sha256(self.parent_raw)),
            mock.patch.object(
                bundle, "PARENT_CANONICAL_SHA256",
                bundle.canonical_sha256(self.parent),
            ),
            mock.patch.object(bundle, "RUNTIME_SHA256", self.runtime_sha),
        )


class Tests(unittest.TestCase):
    def fixture(self, directory: str) -> Fixture:
        fixture = Fixture(directory)
        stack = ExitStack()
        self.addCleanup(stack.close)
        for patcher in fixture.patches():
            stack.enter_context(patcher)
        return fixture

    def test_01_current_parent_and_runtime_pins_are_exact(self) -> None:
        parent, binding = bundle._accepted_parent()
        self.assertEqual(binding["raw_sha256"], bundle.PARENT_RAW_SHA256)
        self.assertEqual(parent["schema"], "OT153NXBP0")
        runtime = bundle._runtime_bindings(None)
        for role, descriptor in runtime.items():
            self.assertEqual(descriptor["raw_sha256"], bundle.RUNTIME_SHA256[role])

    def test_02_preparation_is_deterministic_frozen_and_grants_no_authority(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot157-bundle-") as directory:
            fixture = self.fixture(directory)
            value = bundle.build_preparation(
                fixture.restore, fixture.builds, fixture.runtime
            )
            again = bundle.build_preparation(
                fixture.restore, fixture.builds, fixture.runtime
            )
            self.assertEqual(value, again)
            self.assertEqual(value["schema"], "OT157NXBP0")
            self.assertTrue(value["claims"]["immutable_executable_bundle_frozen"])
            self.assertTrue(all(item is False for item in value["authority"].values()))
            verdict = bundle.validate_preparation(
                value, fixture.restore, fixture.builds, fixture.runtime
            )
            self.assertFalse(verdict["execution_authorized"])
            self.assertFalse(verdict["radio_transmit_authorized"])

    def test_03_exact_parent_build_and_restore_are_reverified(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot157-bundle-") as directory:
            fixture = self.fixture(directory)
            value = bundle.build_preparation(
                fixture.restore, fixture.builds, fixture.runtime
            )
            self.assertEqual(
                value["firmware"]["build_evidence"],
                fixture.parent["firmware"]["build_evidence"],
            )
            self.assertFalse(value["firmware"]["firmware_bytes_changed"])
            self.assertFalse(value["firmware"]["firmware_rebuild_required"])
            self.assertEqual(
                value["images"]["restore"], fixture.parent["images"]["restore"]
            )
            fixture.builds["B"]["application_bin"].write_bytes(b"drift")
            with self.assertRaisesRegex(bundle.ContractError, "output tuple"):
                bundle.build_preparation(
                    fixture.restore, fixture.builds, fixture.runtime
                )

    def test_04_reused_build_path_restore_drift_and_runtime_drift_fail(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot157-bundle-") as directory:
            fixture = self.fixture(directory)
            reused = copy.deepcopy(fixture.builds)
            reused["B"]["application_elf"] = reused["A"]["application_elf"]
            with self.assertRaisesRegex(bundle.ContractError, "not independent"):
                bundle.build_preparation(fixture.restore, reused, fixture.runtime)
            fixture.restore.write_bytes(b"wrong")
            with self.assertRaisesRegex(bundle.ContractError, "restoration image"):
                bundle.build_preparation(
                    fixture.restore, fixture.builds, fixture.runtime
                )

        with tempfile.TemporaryDirectory(prefix="ot157-bundle-") as directory:
            fixture = self.fixture(directory)
            fixture.runtime["runner"].write_bytes(b"drift")
            with self.assertRaisesRegex(bundle.ContractError, "digest mismatch"):
                bundle.build_preparation(
                    fixture.restore, fixture.builds, fixture.runtime
                )

    def test_05_reset_aware_and_radio_invariants_are_exact(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot157-bundle-") as directory:
            fixture = self.fixture(directory)
            contract = bundle.build_preparation(
                fixture.restore, fixture.builds, fixture.runtime
            )["execution_contract"]
            self.assertEqual(contract["restart_ack_order"], ["A", "B"])
            self.assertEqual(contract["reopen_order"], ["A", "B"])
            self.assertEqual(contract["post_restart_settle_ms"], 150)
            self.assertEqual(contract["reopen_retry_ms"], 250)
            self.assertEqual(contract["initial_open_timeout_ms"], 10_000)
            self.assertEqual(contract["reopen_timeout_ms"], 15_000)
            self.assertEqual(contract["failure_stage_allowlist"], list(bundle.STAGE_CODES))
            self.assertEqual(contract["failure_stage_count"], 16)
            self.assertEqual(contract["transmissions"], 14)
            self.assertEqual(contract["radio_payload_wire_bytes"], 736)
            self.assertEqual(contract["theoretical_airtime_us"], 1_447_424)
            self.assertTrue(contract["recovery_never_requires_or_writes_benchmark"])

    def test_06_tampering_and_private_fields_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot157-bundle-") as directory:
            fixture = self.fixture(directory)
            value = bundle.build_preparation(
                fixture.restore, fixture.builds, fixture.runtime
            )
            raw = bundle.canonical_document(value).decode("ascii")
            self.assertNotIn(str(fixture.root), raw)
            for path, replacement in (
                (("authority", "radio_transmit_authorized"), True),
                (("execution_contract", "reopen_timeout_ms"), 15_001),
                (("execution_contract", "transmissions"), 15),
                (("claims", "phase_two_complete"), True),
            ):
                changed = copy.deepcopy(value)
                changed[path[0]][path[1]] = replacement
                with self.assertRaisesRegex(bundle.ContractError, "boundary mismatch"):
                    bundle.validate_preparation(
                        changed, fixture.restore, fixture.builds, fixture.runtime
                    )
            private = copy.deepcopy(value)
            private["serial_port"] = "COM7"
            with self.assertRaisesRegex(bundle.ContractError, "private"):
                bundle.canonical_bytes(private)

    def test_07_duplicate_nonfinite_and_noncanonical_json_are_rejected(self) -> None:
        with self.assertRaisesRegex(bundle.ContractError, "duplicate key"):
            bundle.decode_canonical(b'{"schema":"x","schema":"y"}\n')
        with self.assertRaisesRegex(bundle.ContractError, "non-finite"):
            bundle.decode_canonical(b'{"value":NaN}\n')
        with self.assertRaisesRegex(bundle.ContractError, "not canonical"):
            bundle.decode_canonical(b'{"schema": "x"}\n')
        with self.assertRaisesRegex(bundle.ContractError, "non-finite"):
            bundle.canonical_bytes({"value": float("inf")})

    def test_08_checked_in_record_and_bindings_match_current_tree(self) -> None:
        raw = (ROOT / bundle.PREPARATION_RELATIVE).read_bytes()
        self.assertEqual(sha256(raw), bundle.EXPECTED_RECORD_RAW_SHA256)
        record = bundle.decode_canonical(raw)
        self.assertEqual(
            bundle.canonical_sha256(record),
            bundle.EXPECTED_RECORD_CANONICAL_SHA256,
        )
        self.assertEqual(
            record["bindings"]["runtime"], bundle._runtime_bindings(None)
        )
        self.assertEqual(
            record["bindings"]["ot153_preparation"], bundle._accepted_parent()[1]
        )

    def test_09_record_pins_reject_raw_or_semantic_drift(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot157-bundle-") as directory:
            fixture = self.fixture(directory)
            value = bundle.build_preparation(
                fixture.restore, fixture.builds, fixture.runtime
            )
            record = fixture.root / "record.json"
            record.write_bytes(bundle.canonical_document(value))
            with (
                mock.patch.object(
                    bundle, "EXPECTED_RECORD_RAW_SHA256", sha256(record.read_bytes())
                ),
                mock.patch.object(
                    bundle, "EXPECTED_RECORD_CANONICAL_SHA256",
                    bundle.canonical_sha256(value),
                ),
            ):
                verdict = bundle.validate_record_file(
                    record, fixture.restore, fixture.builds, fixture.runtime
                )
                self.assertTrue(verdict["immutable_executable_bundle_frozen"])
                record.write_bytes(record.read_bytes() + b" ")
                with self.assertRaisesRegex(bundle.ContractError, "raw digest"):
                    bundle.validate_record_file(
                        record, fixture.restore, fixture.builds, fixture.runtime
                    )

    def test_10_source_has_no_execution_persistence_or_authority_surface(self) -> None:
        source_path = ROOT / "tools/ot157_noise_xk_radio_bundle.py"
        tree = ast.parse(source_path.read_text(encoding="utf-8"))
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
        self.assertTrue(
            {"serial", "subprocess", "esptool", "socket"}.isdisjoint(imports)
        )
        self.assertFalse(hasattr(bundle, "write_new"))
        self.assertFalse(hasattr(bundle, "build_authority"))
        self.assertFalse(hasattr(bundle, "main"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
