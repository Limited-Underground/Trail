#!/usr/bin/env python3
"""Adversarial host tests for the OT-131 executable bundle authority."""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = ROOT / "tools" / "ot131_monocypher_execution_authority.py"
SPEC = importlib.util.spec_from_file_location("ot131_authority", TOOL_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("OT-131 authority tool unavailable")
contract = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(contract)


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


class Fixture:
    def __init__(self, directory: str) -> None:
        self.root = Path(directory).resolve()
        self.runtime_payload = b"runtime-v0\n"
        self.ot130_preparation_payload = b"ot130-preparation-v0\n"
        self.ot130_authority_payload = b"ot130-authority-v0\n"
        self.adapter_payload = b"adapter-v0\n"
        self.tool_payload = b"authority-tool-v0\n"
        self.benchmark_payload = b"benchmark-v0"
        self.restore_payload = b"restore-v0"

        self.runtime_relative = "tools/runtime.py"
        self.adapter = self.root / contract.ADAPTER_RELATIVE
        self.tool = self.root / contract.CONTRACT_TOOL_RELATIVE
        self.benchmark = self.root / contract.BENCHMARK_NAME
        self.restore = self.root / contract.RESTORE_NAME
        self.preparation = self.root / contract.PREPARATION_RELATIVE
        self.authority = self.root / contract.AUTHORITY_RELATIVE

        self._write(self.root / self.runtime_relative, self.runtime_payload)
        self._write(self.adapter, self.adapter_payload)
        self._write(self.tool, self.tool_payload)
        self._write(
            self.root / contract.OT130_PREPARATION_RELATIVE,
            self.ot130_preparation_payload,
        )
        self._write(
            self.root / contract.OT130_AUTHORITY_RELATIVE,
            self.ot130_authority_payload,
        )
        self._write(self.benchmark, self.benchmark_payload)
        self._write(self.restore, self.restore_payload)

    @staticmethod
    def _write(path: Path, payload: bytes) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)

    def patches(self):
        return (
            mock.patch.object(contract, "ROOT", self.root),
            mock.patch.object(
                contract,
                "FIXED_RUNTIME_BINDINGS",
                (("runtime", self.runtime_relative, sha256(self.runtime_payload)),),
            ),
            mock.patch.object(
                contract,
                "OT130_PREPARATION_RAW_SHA256",
                sha256(self.ot130_preparation_payload),
            ),
            mock.patch.object(
                contract,
                "OT130_AUTHORITY_RAW_SHA256",
                sha256(self.ot130_authority_payload),
            ),
            mock.patch.object(contract, "BENCHMARK_BYTES", len(self.benchmark_payload)),
            mock.patch.object(contract, "BENCHMARK_SHA256", sha256(self.benchmark_payload)),
            mock.patch.object(contract, "RESTORE_BYTES", len(self.restore_payload)),
            mock.patch.object(contract, "RESTORE_SHA256", sha256(self.restore_payload)),
        )

    def prepare_and_authorize(self):
        preparation = contract.build_preparation(
            self.benchmark, self.restore, self.adapter
        )
        preparation_raw = contract.canonical_document(preparation)
        authority = contract.build_authority(
            preparation,
            preparation_raw,
            self.benchmark,
            self.restore,
            self.adapter,
            owner_authorization_granted=True,
        )
        return preparation, preparation_raw, authority

    def persist(self, preparation, authority) -> None:
        self._write(self.preparation, contract.canonical_document(preparation))
        self._write(self.authority, contract.canonical_document(authority))


class ContractTests(unittest.TestCase):
    def _patch(self, fixture: Fixture) -> None:
        for patcher in fixture.patches():
            self.enterContext(patcher)

    def test_01_current_ot130_and_runtime_bindings_match(self) -> None:
        for _, relative, digest in contract.FIXED_RUNTIME_BINDINGS:
            self.assertEqual(sha256((ROOT / relative).read_bytes()), digest)
        self.assertEqual(
            sha256((ROOT / contract.OT130_PREPARATION_RELATIVE).read_bytes()),
            contract.OT130_PREPARATION_RAW_SHA256,
        )
        self.assertEqual(
            sha256((ROOT / contract.OT130_AUTHORITY_RELATIVE).read_bytes()),
            contract.OT130_AUTHORITY_RAW_SHA256,
        )

    def test_02_preparation_binds_adapter_and_supersedes_unexecuted_ot130(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot131-authority-test-") as directory:
            fixture = Fixture(directory)
            self._patch(fixture)
            value = contract.build_preparation(
                fixture.benchmark, fixture.restore, fixture.adapter
            )
            self.assertEqual(
                value["runtime"]["hardware_adapter"]["raw_sha256"],
                sha256(fixture.adapter_payload),
            )
            self.assertEqual(
                value["runtime"]["authority_tool"]["raw_sha256"],
                sha256(fixture.tool_payload),
            )
            supersession = value["supersession"]
            self.assertFalse(supersession["ot130_authority_consumed"])
            self.assertTrue(supersession["ot130_authority_superseded_unexecuted"])
            self.assertFalse(supersession["replacement_attempt_inherited"])

    def test_03_adapter_tamper_invalidates_preparation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot131-authority-test-") as directory:
            fixture = Fixture(directory)
            self._patch(fixture)
            value = contract.build_preparation(
                fixture.benchmark, fixture.restore, fixture.adapter
            )
            fixture.adapter.write_bytes(b"adapter-v1\n")
            with self.assertRaisesRegex(contract.ContractError, "preparation boundary"):
                contract.validate_preparation(
                    value, fixture.benchmark, fixture.restore, fixture.adapter
                )

    def test_04_exact_images_and_adapter_identity_are_required(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot131-authority-test-") as directory:
            fixture = Fixture(directory)
            self._patch(fixture)
            wrong_adapter = fixture.root / "other.py"
            wrong_adapter.write_bytes(fixture.adapter_payload)
            with self.assertRaisesRegex(contract.ContractError, "adapter identity"):
                contract.build_preparation(
                    fixture.benchmark, fixture.restore, wrong_adapter
                )
            fixture.benchmark.write_bytes(b"changed")
            with self.assertRaisesRegex(contract.ContractError, "image digest"):
                contract.build_preparation(
                    fixture.benchmark, fixture.restore, fixture.adapter
                )

    def test_05_authority_is_one_attempt_visual_app_only_and_nonreusable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot131-authority-test-") as directory:
            fixture = Fixture(directory)
            self._patch(fixture)
            preparation = contract.build_preparation(
                fixture.benchmark, fixture.restore, fixture.adapter
            )
            raw = contract.canonical_document(preparation)
            with self.assertRaisesRegex(contract.ContractError, "owner authorization"):
                contract.build_authority(
                    preparation,
                    raw,
                    fixture.benchmark,
                    fixture.restore,
                    fixture.adapter,
                    owner_authorization_granted=False,
                )
            authority = contract.build_authority(
                preparation,
                raw,
                fixture.benchmark,
                fixture.restore,
                fixture.adapter,
                owner_authorization_granted=True,
            )
            self.assertEqual(authority["execution"]["attempt_count"], 1)
            self.assertEqual(authority["execution"]["node_count"], 2)
            self.assertEqual(authority["execution"]["application_offset"], 65536)
            self.assertTrue(authority["execution"]["application_only_writes"])
            self.assertTrue(
                authority["execution"]["display_reset_and_visual_preflight_required"]
            )
            self.assertTrue(authority["execution"]["restore_each_touched_node"])
            self.assertTrue(
                authority["execution"]["recovery_retry_until_restored_required"]
            )
            self.assertFalse(authority["execution"]["radio_allowed"])
            self.assertFalse(authority["consumption"]["reusable"])
            self.assertFalse(authority["consumption"]["continuing_authority"])
            self.assertTrue(authority["consumption"]["consumed_on_success_or_abort"])
            self.assertTrue(all(item is False for item in authority["claims"].values()))

    def test_06_authority_tamper_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot131-authority-test-") as directory:
            fixture = Fixture(directory)
            self._patch(fixture)
            preparation, raw, authority = fixture.prepare_and_authorize()
            authority["execution"]["radio_allowed"] = True
            with self.assertRaisesRegex(contract.ContractError, "authority boundary"):
                contract.validate_authority(
                    authority,
                    preparation,
                    raw,
                    fixture.benchmark,
                    fixture.restore,
                    fixture.adapter,
                )

    def test_07_runtime_validation_requires_benchmark_for_execution(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot131-authority-test-") as directory:
            fixture = Fixture(directory)
            self._patch(fixture)
            preparation, _, authority = fixture.prepare_and_authorize()
            fixture.persist(preparation, authority)
            digest = contract.validate_execution_authority(
                fixture.authority,
                fixture.benchmark,
                fixture.restore,
                fixture.adapter,
                recovery=False,
            )
            self.assertEqual(digest, sha256(fixture.authority.read_bytes()))
            with self.assertRaisesRegex(contract.ContractError, "benchmark unavailable"):
                contract.validate_execution_authority(
                    fixture.authority,
                    None,
                    fixture.restore,
                    fixture.adapter,
                    recovery=False,
                )

    def test_08_recovery_validates_without_benchmark_and_rejects_one(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot131-authority-test-") as directory:
            fixture = Fixture(directory)
            self._patch(fixture)
            preparation, _, authority = fixture.prepare_and_authorize()
            fixture.persist(preparation, authority)
            fixture.benchmark.unlink()
            digest = contract.validate_execution_authority(
                fixture.authority,
                None,
                fixture.restore,
                fixture.adapter,
                recovery=True,
            )
            self.assertEqual(digest, sha256(fixture.authority.read_bytes()))
            with self.assertRaisesRegex(contract.ContractError, "benchmark must be absent"):
                contract.validate_execution_authority(
                    fixture.authority,
                    fixture.root / contract.BENCHMARK_NAME,
                    fixture.restore,
                    fixture.adapter,
                    recovery=True,
                )

    def test_09_json_and_exclusive_output_are_fail_closed(self) -> None:
        value = {"schema": "x", "version": 0}
        self.assertEqual(
            contract.decode_canonical(contract.canonical_document(value), "fixture"),
            value,
        )
        with self.assertRaisesRegex(contract.ContractError, "not canonical"):
            contract.decode_canonical(b'{"schema": "x","version":0}\n', "fixture")
        with self.assertRaisesRegex(contract.ContractError, "duplicate key"):
            contract.decode_canonical(b'{"schema":"x","schema":"y"}\n', "fixture")
        with tempfile.TemporaryDirectory(prefix="ot131-authority-test-") as directory:
            root = Path(directory).resolve()
            relative = "evidence/output.json"
            output = root / relative
            with mock.patch.object(contract, "ROOT", root):
                contract.write_new(output, value, relative)
                with self.assertRaisesRegex(contract.ContractError, "already exists"):
                    contract.write_new(output, value, relative)

    def test_10_authority_tool_has_no_hardware_surface(self) -> None:
        source = TOOL_PATH.read_text(encoding="utf-8")
        for forbidden in (
            "import serial",
            "import subprocess",
            "write-flash",
            "read-flash",
            "--port",
        ):
            self.assertNotIn(forbidden, source)
        self.assertIn(
            'ADAPTER_RELATIVE = "tools/ot131_monocypher_hardware_adapter.py"',
            source,
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
