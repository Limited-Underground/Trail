#!/usr/bin/env python3
"""Adversarial host tests for the OT-154 one-attempt radio authority."""

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
TOOL_PATH = ROOT / "tools" / "ot153_noise_xk_radio_execution_authority.py"
SPEC = importlib.util.spec_from_file_location("ot154_authority_tests", TOOL_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("OT-154 authority tool unavailable")
contract = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(contract)


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


class Fixture:
    def __init__(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="ot154-authority-test-")
        self.root = Path(self.temporary.name).resolve()
        self.stack = contextlib.ExitStack()
        self.preparation, self.preparation_raw, accepted = (
            contract._load_validated_preparation()
        )
        self.validated = copy.deepcopy(accepted)
        self.benchmark_payload = b"ot154 exact benchmark fixture"
        self.restore_payload = b"ot154 exact Trail restoration fixture"
        self.validated["benchmark"] = {
            "name": accepted["benchmark"]["name"],
            "bytes": len(self.benchmark_payload),
            "sha256": sha256(self.benchmark_payload),
        }
        self.validated["restore"] = {
            **accepted["restore"],
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
    def test_01_exact_accepted_ot153_preparation_is_bound(self) -> None:
        preparation, raw, validated = contract._load_validated_preparation()
        self.assertEqual(sha256(raw), contract.PREPARATION_RAW_SHA256)
        self.assertEqual(
            sha256(contract.canonical_bytes(preparation)),
            contract.PREPARATION_CANONICAL_SHA256,
        )
        self.assertEqual(preparation["schema"], "OT153NXBP0")
        self.assertEqual(preparation["preparation_id"], contract.PREPARATION_ID)
        self.assertTrue(all(value is False for value in preparation["authority"].values()))
        self.assertEqual(validated["execution"]["role_cycles"], [
            {"cycle": 1, "initiator": "A", "responder": "B"},
            {"cycle": 2, "initiator": "B", "responder": "A"},
        ])
        self.assertEqual(validated["execution"]["message_wire_bytes"], [48, 48, 64])
        self.assertEqual(validated["execution"]["transmissions"], 14)
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

    def test_03_authority_is_one_use_two_role_radio_scope_only(self) -> None:
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
            self.assertEqual(authority["execution"]["node_count"], 2)
            self.assertEqual(authority["execution"]["role_cycles"], [
                {"cycle": 1, "initiator": "A", "responder": "B"},
                {"cycle": 2, "initiator": "B", "responder": "A"},
            ])
            self.assertTrue(authority["execution"]["application_only_writes"])
            self.assertTrue(authority["execution"]["radio_allowed"])
            self.assertEqual(authority["radio_profile"], contract.RADIO_PROFILE)
            self.assertFalse(authority["execution"]["packet_v1_allowed"])
            self.assertFalse(authority["execution"]["ota1_wrapper_allowed"])
            self.assertFalse(authority["execution"]["selection_allowed"])
            self.assertTrue(authority["consumption"]["consumed_on_success_or_abort"])
            self.assertFalse(authority["consumption"]["continuing_authority"])
            self.assertFalse(authority["consumption"]["reusable"])
            self.assertTrue(all(value is False for value in authority["claims"].values()))

    def test_04_binding_requires_exact_benchmark_restore_and_adapter(self) -> None:
        with Fixture() as fixture:
            binding = contract.load_preparation_binding(
                fixture.benchmark,
                fixture.restore,
                fixture.adapter,
                recovery=False,
            )
            self.assertEqual(set(binding), {
                "benchmark_name", "benchmark_bytes", "benchmark_sha256",
                "restore_name", "restore_bytes", "restore_sha256",
                "application_offset", "baud", "runner_name", "runner_sha256",
                "runner_schema",
            })
            self.assertEqual(binding["application_offset"], 0x10000)
            self.assertEqual(binding["baud"], 115_200)
            self.assertEqual(binding["runner_schema"], "OT153NXR0")
            fixture.benchmark.write_bytes(b"tampered")
            with self.assertRaisesRegex(contract.ContractError, "benchmark image mismatch"):
                contract.load_preparation_binding(
                    fixture.benchmark,
                    fixture.restore,
                    fixture.adapter,
                    recovery=False,
                )

    def test_05_authority_and_preparation_tamper_fail_closed(self) -> None:
        with Fixture() as fixture:
            authority = fixture.authority()
            for field, value in (
                ("attempt_count", 2),
                ("radio_payload_wire_bytes", 735),
                ("packet_v1_allowed", True),
                ("selection_allowed", True),
            ):
                with self.subTest(field=field):
                    changed = copy.deepcopy(authority)
                    changed["execution"][field] = value
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

    def test_06_execution_requires_benchmark_and_recovery_forbids_it(self) -> None:
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
                with self.assertRaisesRegex(
                    contract.ContractError, "benchmark unavailable"
                ):
                    contract.validate_execution_authority(
                        canonical_path,
                        None,
                        fixture.restore,
                        fixture.adapter,
                        recovery=False,
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
                with self.assertRaisesRegex(
                    contract.ContractError, "recovery benchmark must be absent"
                ):
                    contract.validate_execution_authority(
                        canonical_path,
                        fixture.root / fixture.validated["benchmark"]["name"],
                        fixture.restore,
                        fixture.adapter,
                        recovery=True,
                    )

    def test_07_authority_is_canonical_and_privacy_safe(self) -> None:
        with Fixture() as fixture:
            authority = fixture.authority()
            raw = contract.canonical_document(authority)
            self.assertEqual(contract.decode_canonical(raw, "authority"), authority)
            text = raw.decode("ascii")
            self.assertNotIn(str(fixture.root), text)
            self.assertNotIn("COM", text)
            self.assertNotIn("serial_port", text)
            self.assertNotIn("device_identifier", text)
            self.assertNotIn("private_endpoint", text)
            self.assertNotIn("raw_payload", text)
            self.assertNotIn("secret", text)
        with self.assertRaisesRegex(contract.ContractError, "duplicate key"):
            contract.decode_canonical(b'{"schema":"x","schema":"y"}\n')
        with self.assertRaisesRegex(contract.ContractError, "not canonical"):
            contract.decode_canonical(b'{"schema": "x"}\n')

    def test_08_tool_has_no_hardware_or_implicit_execution_surface(self) -> None:
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
            "open_radio_endpoint", "execute(", "recover(",
        ):
            self.assertNotIn(forbidden, source)
        self.assertEqual(
            contract.AUTHORITY_RELATIVE,
            "tests/benchmarks/crypto/"
            "OT-154-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0.json",
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
