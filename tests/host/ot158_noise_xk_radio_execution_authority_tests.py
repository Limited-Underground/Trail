#!/usr/bin/env python3
"""Adversarial host tests for the OT-158 one-attempt radio authority."""

from __future__ import annotations

import ast
import contextlib
import copy
import hashlib
import importlib.util
import inspect
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = ROOT / "tools" / "ot158_noise_xk_radio_execution_authority.py"
SPEC = importlib.util.spec_from_file_location("ot158_authority_tests", TOOL_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("OT-158 authority tool unavailable")
contract = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(contract)


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


class Fixture:
    def __init__(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="ot158-authority-test-")
        self.root = Path(self.temporary.name).resolve()
        self.stack = contextlib.ExitStack()
        self.preparation, self.preparation_raw, accepted = (
            contract._load_validated_preparation()
        )
        self.validated = copy.deepcopy(accepted)
        self.benchmark_payload = b"ot158 exact benchmark fixture"
        self.restore_payload = b"ot158 exact Trail restoration fixture"
        self.validated["benchmark"] = {
            "name": accepted["benchmark"]["name"],
            "bytes": len(self.benchmark_payload),
            "sha256": sha256(self.benchmark_payload),
        }
        self.validated["restore"] = {
            "name": accepted["restore"]["name"],
            "bytes": len(self.restore_payload),
            "sha256": sha256(self.restore_payload),
        }
        self.benchmark = self.root / self.validated["benchmark"]["name"]
        self.restore = self.root / self.validated["restore"]["name"]
        self.adapter = ROOT / contract.ADAPTER_RELATIVE
        self.authority_path = self.root / "authority.json"
        self.benchmark.write_bytes(self.benchmark_payload)
        self.restore.write_bytes(self.restore_payload)

    def __enter__(self) -> "Fixture":
        self.stack.enter_context(
            mock.patch.object(
                contract,
                "_load_validated_preparation",
                return_value=(
                    self.preparation,
                    self.preparation_raw,
                    self.validated,
                ),
            )
        )
        return self

    def __exit__(self, *args: object) -> None:
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

    def persist(self, value: dict[str, object]) -> str:
        self.authority_path.write_bytes(contract.canonical_document(value))
        return sha256(self.authority_path.read_bytes())


class AuthorityTests(unittest.TestCase):
    def test_01_exact_ot157_preparation_and_reset_contract_are_bound(self) -> None:
        preparation, raw, validated = contract._load_validated_preparation()
        self.assertEqual(len(raw), 6113)
        self.assertEqual(sha256(raw), contract.PREPARATION_RAW_SHA256)
        self.assertEqual(
            sha256(contract.canonical_bytes(preparation)),
            contract.PREPARATION_CANONICAL_SHA256,
        )
        self.assertEqual(preparation["schema"], "OT157NXBP0")
        self.assertEqual(preparation["preparation_id"], contract.PREPARATION_ID)
        self.assertTrue(all(value is False for value in preparation["authority"].values()))
        execution = validated["execution"]
        self.assertEqual(execution["restart_ack_order"], ["A", "B"])
        self.assertEqual(execution["reopen_order"], ["A", "B"])
        self.assertEqual(execution["failure_stage_allowlist"], list(contract.STAGE_CODES))
        self.assertEqual(execution["failure_stage_count"], 16)
        self.assertTrue(execution["both_post_restart_contracts_before_radio"])
        self.assertTrue(execution["recovery_never_requires_or_writes_benchmark"])
        self.assertEqual(validated["radio_profile"], contract.RADIO_PROFILE)

    def test_02_explicit_owner_grant_is_required(self) -> None:
        with Fixture() as fixture:
            with self.assertRaisesRegex(contract.ContractError, "owner authorization"):
                contract.build_authority(
                    fixture.preparation,
                    fixture.preparation_raw,
                    fixture.benchmark,
                    fixture.restore,
                    fixture.adapter,
                    owner_authorization_granted=False,
                )
        parameter = inspect.signature(contract.build_authority).parameters[
            "owner_authorization_granted"
        ]
        self.assertIs(parameter.default, inspect.Parameter.empty)

    def test_03_authority_is_one_use_reset_aware_radio_scope_only(self) -> None:
        with Fixture() as fixture:
            authority = fixture.authority()
            result = contract.validate_authority(
                authority,
                fixture.preparation,
                fixture.preparation_raw,
                fixture.benchmark,
                fixture.restore,
                fixture.adapter,
            )
            self.assertEqual(result["attempt_count"], 1)
            self.assertFalse(result["reusable"])
            self.assertTrue(result["radio_allowed"])
            execution = authority["execution"]
            self.assertEqual(execution["node_count"], 2)
            self.assertEqual(execution["transmissions"], 14)
            self.assertEqual(execution["radio_payload_wire_bytes"], 736)
            self.assertEqual(execution["theoretical_airtime_us"], 1_447_424)
            self.assertTrue(execution["application_only_writes"])
            self.assertTrue(execution["radio_allowed"])
            self.assertFalse(execution["packet_v1_allowed"])
            self.assertFalse(execution["ota1_wrapper_allowed"])
            self.assertFalse(execution["selection_allowed"])
            self.assertTrue(execution["fresh_handle_dtr_false_before_open"])
            self.assertTrue(execution["fresh_handle_rts_false_before_open"])
            self.assertTrue(authority["consumption"]["consumed_on_success_or_abort"])
            self.assertFalse(authority["consumption"]["continuing_authority"])
            self.assertFalse(authority["consumption"]["reusable"])
            self.assertTrue(all(value is False for value in authority["claims"].values()))

    def test_04_exact_images_adapter_and_recovery_boundary_are_required(self) -> None:
        with Fixture() as fixture:
            binding = contract.load_preparation_binding(
                fixture.benchmark,
                fixture.restore,
                fixture.adapter,
                recovery=False,
            )
            self.assertEqual(binding["application_offset"], 0x10000)
            self.assertEqual(binding["baud"], 115_200)
            self.assertEqual(binding["runner_name"], "ot156_noise_xk_radio_runner.py")
            self.assertEqual(binding["runner_schema"], "OT153NXR0")
            fixture.benchmark.write_bytes(b"tampered")
            with self.assertRaisesRegex(contract.ContractError, "benchmark image mismatch"):
                contract.load_preparation_binding(
                    fixture.benchmark,
                    fixture.restore,
                    fixture.adapter,
                    recovery=False,
                )
            fixture.benchmark.unlink()
            contract.load_preparation_binding(
                None, fixture.restore, fixture.adapter, recovery=True
            )
            with self.assertRaisesRegex(
                contract.ContractError, "recovery benchmark must be absent"
            ):
                contract.load_preparation_binding(
                    fixture.root / contract.BENCHMARK["name"],
                    fixture.restore,
                    fixture.adapter,
                    recovery=True,
                )

    def test_05_authority_and_preparation_tamper_fail_closed(self) -> None:
        with Fixture() as fixture:
            authority = fixture.authority()
            for section, field, value in (
                ("execution", "attempt_count", 2),
                ("execution", "reopen_timeout_ms", 15_001),
                ("execution", "radio_payload_wire_bytes", 735),
                ("execution", "packet_v1_allowed", True),
                ("consumption", "reusable", True),
                ("claims", "phase_two_complete", True),
            ):
                changed = copy.deepcopy(authority)
                changed[section][field] = value
                with self.assertRaisesRegex(
                    contract.ContractError, "authority boundary mismatch"
                ):
                    contract.validate_authority(
                        changed,
                        fixture.preparation,
                        fixture.preparation_raw,
                        fixture.benchmark,
                        fixture.restore,
                        fixture.adapter,
                    )
            changed_preparation = copy.deepcopy(fixture.preparation)
            changed_preparation["execution_contract"]["transmissions"] = 15
            with self.assertRaisesRegex(contract.ContractError, "preparation boundary"):
                contract.build_authority(
                    changed_preparation,
                    fixture.preparation_raw,
                    fixture.benchmark,
                    fixture.restore,
                    fixture.adapter,
                    owner_authorization_granted=True,
                )

    def test_06_execution_identity_and_recovery_validation_fail_closed(self) -> None:
        with Fixture() as fixture:
            authority = fixture.authority()
            digest = fixture.persist(authority)
            canonical_path = ROOT / contract.AUTHORITY_RELATIVE
            with self.assertRaisesRegex(contract.ContractError, "authority identity"):
                contract.validate_execution_authority(
                    fixture.authority_path,
                    fixture.benchmark,
                    fixture.restore,
                    fixture.adapter,
                    recovery=False,
                )
            with mock.patch.object(
                contract,
                "_load_canonical",
                return_value=(authority, contract.canonical_document(authority)),
            ):
                self.assertEqual(
                    contract.validate_execution_authority(
                        canonical_path,
                        fixture.benchmark,
                        fixture.restore,
                        fixture.adapter,
                        recovery=False,
                    ),
                    digest,
                )
                fixture.benchmark.unlink()
                self.assertEqual(
                    contract.validate_execution_authority(
                        canonical_path,
                        None,
                        fixture.restore,
                        fixture.adapter,
                        recovery=True,
                    ),
                    digest,
                )

    def test_07_authority_is_canonical_and_privacy_safe(self) -> None:
        with Fixture() as fixture:
            authority = fixture.authority()
            raw = contract.canonical_document(authority)
            self.assertEqual(contract.decode_canonical(raw, "authority"), authority)
            text = raw.decode("ascii")
            self.assertNotIn(str(fixture.root), text)
            for marker in (
                "COM7", "serial_port", "device_identifier", "private_endpoint",
                "raw_payload", "secret",
            ):
                self.assertNotIn(marker, text)
        with self.assertRaisesRegex(contract.ContractError, "duplicate key"):
            contract.decode_canonical(b'{"schema":"x","schema":"y"}\n')
        with self.assertRaisesRegex(contract.ContractError, "not canonical"):
            contract.decode_canonical(b'{"schema": "x"}\n')
        with self.assertRaisesRegex(contract.ContractError, "private field"):
            contract.canonical_bytes({"serial_port": "redacted"})
        with self.assertRaisesRegex(contract.ContractError, "non-finite"):
            contract.canonical_bytes({"value": float("nan")})

    def test_08_exclusive_output_never_overwrites(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot158-output-") as directory:
            root = Path(directory).resolve()
            relative = "authority.json"
            path = root / relative
            with mock.patch.object(contract, "ROOT", root):
                contract.write_new(path, {"schema": "fixture"}, relative)
                first = path.read_bytes()
                with self.assertRaisesRegex(contract.ContractError, "already exists"):
                    contract.write_new(path, {"schema": "changed"}, relative)
                self.assertEqual(path.read_bytes(), first)

    def test_09_checked_in_authority_is_exact_for_current_tool(self) -> None:
        preparation, raw, validated = contract._load_validated_preparation()
        expected = contract._authority_value(preparation, raw, validated)
        authority_raw = (ROOT / contract.AUTHORITY_RELATIVE).read_bytes()
        authority = contract.decode_canonical(authority_raw, "authority")
        self.assertEqual(authority, expected)
        self.assertEqual(authority["schema"], "OT158NXRA0")
        self.assertEqual(authority["authority_id"], contract.AUTHORITY_ID)

    def test_10_tool_has_no_hardware_or_implicit_execution_surface(self) -> None:
        source = TOOL_PATH.read_text(encoding="utf-8")
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
        self.assertTrue(
            {"serial", "subprocess", "esptool", "socket"}.isdisjoint(imports)
        )
        for forbidden in (
            "write-flash", "read-flash", "erase-flash", "list_ports",
            "open_radio_endpoint", "coordinator.execute", "coordinator.recover",
        ):
            self.assertNotIn(forbidden, source)
        self.assertEqual(
            contract.AUTHORITY_RELATIVE,
            "tests/benchmarks/crypto/"
            "OT-158-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0.json",
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
