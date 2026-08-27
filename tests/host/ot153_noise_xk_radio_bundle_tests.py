#!/usr/bin/env python3
"""Focused adversarial tests for the host-only OT-153 bundle preparation."""

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

import ot153_noise_xk_radio_bundle as bundle  # noqa: E402


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


class Fixture:
    def __init__(self, directory: str) -> None:
        self.root = Path(directory).resolve() / "repo"
        self.root.mkdir()

        parent = {"schema": "OTNXRP0", "version": 0, "record_id": "OT-152"}
        self.parent_raw = (json.dumps(parent, indent=2) + "\n").encode("ascii")
        self._write(self.root / bundle.OT152_RELATIVE, self.parent_raw)
        self.parent_canonical = sha256(bundle.canonical_bytes(parent))

        target_payloads = {
            bundle.TARGET_BINDINGS[0]: b"build*/\n",
            bundle.TARGET_BINDINGS[1]: b"project(ot153_noise_xk_radio_cost)\n",
            bundle.TARGET_BINDINGS[2]: (
                "dependencies:\n"
                "  idf:\n    version: 6.0.2\n"
                "  jgromes/radiolib:\n"
                f"    component_hash: {bundle.RADIOLIB_COMPONENT_SHA256}\n"
                f"    version: {bundle.RADIOLIB_VERSION}\n"
            ).encode("ascii"),
            bundle.TARGET_BINDINGS[3]: b"CONFIG_APP_REPRODUCIBLE_BUILD=y\n",
            bundle.TARGET_BINDINGS[4]: b"idf_component_register(SRCS app_main.cpp)\n",
            bundle.TARGET_BINDINGS[5]: b"dependencies: {}\n",
            bundle.TARGET_BINDINGS[6]: b"// raw OTNXK0 messages only\n",
        }
        for relative, payload in target_payloads.items():
            self._write(self.root / relative, payload)

        self.sources: tuple[tuple[str, str, str | None], ...] = tuple(
            (
                role,
                relative,
                sha256(f"// {role}\n".encode("ascii")),
            )
            for role, relative, unused in bundle.SOURCE_BINDINGS
        )
        for role, relative, expected in self.sources:
            self._write(self.root / relative, f"// {role}\n".encode("ascii"))

        self.libsodium_lock_relative = (
            "tests/benchmarks/crypto/esp_idf/"
            "espressif_libsodium_1_0_22/dependencies.lock"
        )
        self._write(
            self.root / self.libsodium_lock_relative,
            (
                "dependencies:\n  espressif/libsodium:\n"
                f"    component_hash: {bundle.LIBSODIUM_COMPONENT_SHA256}\n"
                f"    version: {bundle.LIBSODIUM_VERSION}\n"
            ).encode("ascii"),
        )

        self.runtime = {
            role: self.root / relative for role, relative in bundle.RUNTIME_BINDINGS
        }
        for role, path in self.runtime.items():
            self._write(path, f"# {role}\n".encode("ascii"))

        self.restore_payload = b"exact-trail-restore"
        self.restore = self.root / "private" / bundle.RESTORE_NAME
        self._write(self.restore, self.restore_payload)

        self.builds: dict[str, dict[str, Path]] = {}
        for run in ("A", "B"):
            artifacts: dict[str, Path] = {}
            for role, name in bundle.BUILD_ARTIFACTS:
                path = self.root / "private-builds" / run / name
                self._write(path, f"{role}-identical\n".encode("ascii"))
                artifacts[role] = path
            self.builds[run] = artifacts

    @staticmethod
    def _write(path: Path, payload: bytes) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)

    def patches(self) -> tuple[mock._patch, ...]:
        return (
            mock.patch.object(bundle, "ROOT", self.root),
            mock.patch.object(bundle, "OT152_RAW_SHA256", sha256(self.parent_raw)),
            mock.patch.object(
                bundle, "OT152_CANONICAL_SHA256", self.parent_canonical
            ),
            mock.patch.object(bundle, "SOURCE_BINDINGS", self.sources),
            mock.patch.object(bundle, "RESTORE_BYTES", len(self.restore_payload)),
            mock.patch.object(
                bundle, "RESTORE_SHA256", sha256(self.restore_payload)
            ),
        )


class Tests(unittest.TestCase):
    def fixture(self, directory: str) -> tuple[Fixture, ExitStack]:
        fixture = Fixture(directory)
        stack = ExitStack()
        self.addCleanup(stack.close)
        for patcher in fixture.patches():
            stack.enter_context(patcher)
        return fixture, stack

    def test_01_current_parent_adapter_and_dependency_pins_are_exact(self) -> None:
        parent = bundle._ot152_binding()
        self.assertEqual(parent["raw_sha256"], bundle.OT152_RAW_SHA256)
        self.assertEqual(parent["canonical_sha256"], bundle.OT152_CANONICAL_SHA256)
        sources = bundle._source_bindings()
        self.assertEqual(
            sources["noise_adapter_header"]["raw_sha256"],
            "b7c649434cdffe648e467bb117849ae0296a73fa041d614d3d4ba32578e40c45",
        )
        self.assertEqual(
            sources["noise_adapter_source"]["raw_sha256"],
            "8534fe1a6a4b68cd37e491ebd0f564dd38fd3935fb21d8f2d45aa8333ae442b8",
        )
        self.assertEqual(bundle.ESP_IDF_COMMIT, "7101770dc6db2667b3c477cc31365dd1acd6db4e")
        self.assertEqual(bundle.RADIOLIB_VERSION, "7.7.1")
        self.assertEqual(bundle.LIBSODIUM_VERSION, "1.0.22")

    def test_02_pending_preparation_is_deterministic_and_has_no_authority(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot153-bundle-") as directory:
            fixture, unused = self.fixture(directory)
            value = bundle.build_preparation(
                fixture.restore, runtime_bindings=fixture.runtime
            )
            again = bundle.build_preparation(
                fixture.restore, runtime_bindings=fixture.runtime
            )
            self.assertEqual(value, again)
            self.assertEqual(value["schema"], "OT153NXBP0")
            self.assertEqual(value["firmware"]["build_evidence"]["status"], "pending")
            self.assertEqual(
                value["firmware"]["build_policy"],
                {
                    "fixed_project_version": "ot153-noise-xk-radio-v0",
                    "ccache_enabled": False,
                },
            )
            self.assertFalse(value["claims"]["immutable_executable_bundle_frozen"])
            self.assertTrue(all(item is False for item in value["authority"].values()))
            verdict = bundle.validate_preparation(
                value, fixture.restore, runtime_bindings=fixture.runtime
            )
            self.assertTrue(verdict["bundle_preparation_generated"])
            self.assertFalse(verdict["execution_authorized"])

    def test_03_two_independent_identical_builds_freeze_exact_output_tuple(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot153-bundle-") as directory:
            fixture, unused = self.fixture(directory)
            value = bundle.build_preparation(
                fixture.restore, fixture.builds, fixture.runtime
            )
            build = value["firmware"]["build_evidence"]
            self.assertEqual(build["status"], "reproduced")
            self.assertEqual([item["run"] for item in build["runs"]], ["A", "B"])
            self.assertEqual(build["runs"][0]["artifacts"], build["runs"][1]["artifacts"])
            self.assertTrue(value["claims"]["immutable_executable_bundle_frozen"])
            execution = value["execution_contract"]
            self.assertEqual(execution["message_wire_bytes"], [48, 48, 64])
            self.assertEqual(execution["radio_payload_wire_bytes"], 736)
            self.assertEqual(execution["transmissions"], 14)
            self.assertEqual(execution["theoretical_airtime_us"], 1_447_424)
            self.assertTrue(execution["both_nodes_present_simultaneously_required"])

    def test_04_build_evidence_rejects_reused_paths_and_output_drift(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot153-bundle-") as directory:
            fixture, unused = self.fixture(directory)
            reused = copy.deepcopy(fixture.builds)
            reused["B"]["application_bin"] = reused["A"]["application_bin"]
            with self.assertRaisesRegex(bundle.ContractError, "not independent"):
                bundle.build_preparation(fixture.restore, reused, fixture.runtime)
            fixture.builds["B"]["application_elf"].write_bytes(b"drift")
            with self.assertRaisesRegex(bundle.ContractError, "outputs differ"):
                bundle.build_preparation(
                    fixture.restore, fixture.builds, fixture.runtime
                )

    def test_05_restore_source_and_runtime_drift_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot153-bundle-") as directory:
            fixture, unused = self.fixture(directory)
            value = bundle.build_preparation(
                fixture.restore, fixture.builds, fixture.runtime
            )
            fixture.restore.write_bytes(b"wrong")
            with self.assertRaisesRegex(bundle.ContractError, "restoration image"):
                bundle.validate_preparation(
                    value, fixture.restore, fixture.builds, fixture.runtime
                )

        with tempfile.TemporaryDirectory(prefix="ot153-bundle-") as directory:
            fixture, unused = self.fixture(directory)
            value = bundle.build_preparation(
                fixture.restore, fixture.builds, fixture.runtime
            )
            (fixture.root / bundle.TARGET_BINDINGS[3]).write_bytes(b"drift\n")
            with self.assertRaisesRegex(bundle.ContractError, "boundary mismatch"):
                bundle.validate_preparation(
                    value, fixture.restore, fixture.builds, fixture.runtime
                )

        with tempfile.TemporaryDirectory(prefix="ot153-bundle-") as directory:
            fixture, unused = self.fixture(directory)
            value = bundle.build_preparation(
                fixture.restore, fixture.builds, fixture.runtime
            )
            fixture.runtime["runner"].write_bytes(b"# runtime drift\n")
            with self.assertRaisesRegex(bundle.ContractError, "boundary mismatch"):
                bundle.validate_preparation(
                    value, fixture.restore, fixture.builds, fixture.runtime
                )

    def test_06_packet_v1_and_ota1_wrappers_are_prohibited(self) -> None:
        for token in (b"packet_v1 wrapper\n", b"OTA1 ACK wrapper\n"):
            with self.subTest(token=token):
                with tempfile.TemporaryDirectory(prefix="ot153-bundle-") as directory:
                    fixture, unused = self.fixture(directory)
                    (fixture.root / bundle.TARGET_BINDINGS[-1]).write_bytes(token)
                    with self.assertRaisesRegex(bundle.ContractError, "wrapper prohibited"):
                        bundle.build_preparation(
                            fixture.restore, fixture.builds, fixture.runtime
                        )

    def test_07_dependency_lock_drift_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot153-bundle-") as directory:
            fixture, unused = self.fixture(directory)
            lock = fixture.root / bundle.TARGET_BINDINGS[2]
            lock.write_text("version: 7.7.2\n", encoding="ascii")
            with self.assertRaisesRegex(bundle.ContractError, "dependency lock"):
                bundle.build_preparation(
                    fixture.restore, fixture.builds, fixture.runtime
                )

    def test_08_privacy_authority_and_preparation_tampering_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot153-bundle-") as directory:
            fixture, unused = self.fixture(directory)
            value = bundle.build_preparation(
                fixture.restore, fixture.builds, fixture.runtime
            )
            raw = bundle.canonical_document(value).decode("ascii")
            self.assertNotIn(str(fixture.root), raw)
            self.assertNotIn("private-builds", raw)
            self.assertNotIn("COM", raw)

            for path, item in (
                (("authority", "radio_transmit_authorized"), True),
                (("execution_contract", "packet_v1_wrapper_used"), True),
                (("execution_contract", "ota1_ack_wrapper_used"), True),
                (("execution_contract", "transmissions"), 15),
                (("firmware", "build_policy"), {
                    "fixed_project_version": "ot153-noise-xk-radio-v0",
                    "ccache_enabled": True,
                }),
            ):
                with self.subTest(path=path):
                    mutated = copy.deepcopy(value)
                    mutated[path[0]][path[1]] = item
                    with self.assertRaisesRegex(bundle.ContractError, "boundary mismatch"):
                        bundle.validate_preparation(
                            mutated, fixture.restore, fixture.builds, fixture.runtime
                        )

            private = copy.deepcopy(value)
            private["serial_port"] = "COM7"
            with self.assertRaisesRegex(bundle.ContractError, "private"):
                bundle.canonical_bytes(private)

    def test_09_duplicate_nonfinite_and_noncanonical_json_are_rejected(self) -> None:
        with self.assertRaisesRegex(bundle.ContractError, "duplicate key"):
            bundle.decode_canonical(b'{"schema":"x","schema":"y"}\n')
        with self.assertRaisesRegex(bundle.ContractError, "non-finite"):
            bundle.decode_canonical(b'{"value":NaN}\n')
        with self.assertRaisesRegex(bundle.ContractError, "not canonical"):
            bundle.decode_canonical(b'{"schema": "x"}\n')
        with self.assertRaisesRegex(bundle.ContractError, "non-finite"):
            bundle.canonical_bytes({"value": float("inf")})

    def test_10_checked_in_record_and_bindings_match_current_tree(self) -> None:
        raw = (ROOT / bundle.PREPARATION_RELATIVE).read_bytes()
        self.assertEqual(sha256(raw), bundle.EXPECTED_RECORD_RAW_SHA256)
        record = bundle.decode_canonical(raw)
        self.assertEqual(
            bundle.canonical_sha256(record),
            bundle.EXPECTED_RECORD_CANONICAL_SHA256,
        )
        self.assertEqual(
            record["bindings"]["ot152_preparation"], bundle._ot152_binding()
        )
        self.assertEqual(record["bindings"]["sources"], bundle._source_bindings())
        self.assertEqual(
            record["bindings"]["runtime"], bundle._runtime_bindings(None)
        )

    def test_11_record_file_pins_can_be_filled_after_generation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ot153-bundle-") as directory:
            fixture, unused = self.fixture(directory)
            value = bundle.build_preparation(
                fixture.restore, fixture.builds, fixture.runtime
            )
            record = fixture.root / "record.json"
            record.write_bytes(bundle.canonical_document(value))
            raw_pin = sha256(record.read_bytes())
            canonical_pin = bundle.canonical_sha256(value)
            with mock.patch.object(bundle, "EXPECTED_RECORD_RAW_SHA256", raw_pin), \
                 mock.patch.object(
                     bundle, "EXPECTED_RECORD_CANONICAL_SHA256", canonical_pin
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

    def test_12_source_surface_has_no_execution_or_persistence_capability(self) -> None:
        source_path = ROOT / "tools/ot153_noise_xk_radio_bundle.py"
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
