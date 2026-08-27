#!/usr/bin/env python3
"""Cross-layer binding regression for the fresh OT-150 runtime."""

from __future__ import annotations

import hashlib
import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, relative: str):
    path = ROOT / relative
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"{relative} unavailable")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


bundle = load_module("_ot150_binding_bundle", "tools/ot150_mbedtls_psa_bundle.py")
authority = load_module(
    "_ot150_binding_authority", "tools/ot150_mbedtls_psa_execution_authority.py"
)
coordinator = load_module(
    "_ot150_binding_coordinator", "tools/ot150_mbedtls_psa_coordinator.py"
)
protocol = load_module(
    "_ot150_binding_protocol", "tools/ot150_mbedtls_psa_protocol_runner.py"
)


RUNTIME_PATHS = {
    "bundle_validator": ROOT / "tools/ot150_mbedtls_psa_bundle.py",
    "protocol_transport": ROOT / "tools/ot150_mbedtls_psa_protocol_runner.py",
    "coordinator": ROOT / "tools/ot150_mbedtls_psa_coordinator.py",
    "hardware_adapter": ROOT / "tools/ot150_mbedtls_psa_hardware_adapter.py",
    "execution_authority_tool": ROOT / "tools/ot150_mbedtls_psa_execution_authority.py",
    "matched_resource_validator": ROOT / "tools/crypto_matched_resource_accounting.py",
}


class BindingConsistencyTests(unittest.TestCase):
    def test_01_bundle_and_authority_bind_exact_runtime_files(self) -> None:
        expected = authority._runtime_expected()
        actual = bundle._runtime_descriptors(RUNTIME_PATHS)
        self.assertEqual(actual, expected)
        self.assertEqual(set(actual), set(RUNTIME_PATHS))
        self.assertEqual(len(actual), 6)
        self.assertEqual(
            {role: relative for role, relative in bundle.RUNTIME_BINDINGS},
            {
                role: path.relative_to(ROOT).as_posix()
                for role, path in RUNTIME_PATHS.items()
            },
        )
        for role, descriptor in actual.items():
            raw = RUNTIME_PATHS[role].read_bytes()
            self.assertEqual(descriptor["bytes"], len(raw))
            self.assertEqual(descriptor["raw_sha256"], hashlib.sha256(raw).hexdigest())
            self.assertFalse(Path(descriptor["path"]).is_absolute())

    def test_02_public_successor_and_restore_contracts_are_identical(self) -> None:
        self.assertEqual(authority.PREPARATION_RELATIVE, bundle.PREPARATION_RELATIVE)
        self.assertEqual(authority.PREPARATION_SCHEMA, bundle.PREPARATION_SCHEMA)
        self.assertEqual(authority.PROJECT_VER, bundle.PROJECT_VER)
        self.assertEqual(authority.RESOURCE_RESULT_RELATIVE, bundle.RESOURCE_RESULT_RELATIVE)
        self.assertEqual(authority.APPLICATION_OFFSET, coordinator.APPLICATION_OFFSET)
        self.assertEqual(authority.RESTORE_NAME, coordinator.RESTORE_NAME)
        self.assertEqual(authority.RESTORE_BYTES, coordinator.RESTORE_BYTES)
        self.assertEqual(authority.RESTORE_SHA256, coordinator.RESTORE_SHA256)
        self.assertEqual(authority.RESTORE_BYTES, 500_944)
        self.assertEqual(
            authority.RESTORE_SHA256,
            "f2a58414f82eed585ba90bf7671b06c8ebfaa82e8b23f6ac2093de95143b4e0e",
        )

    def test_03_protocol_and_frame_contract_are_the_ot149_five_operation_gate(self) -> None:
        self.assertEqual(coordinator.protocol.START, protocol.START)
        self.assertEqual(coordinator.protocol.READY, protocol.READY)
        self.assertEqual(protocol.frame_contract.EXPECTED_FRAME_COUNT, 1_015)
        self.assertEqual(
            tuple(protocol.frame_contract.OPERATIONS),
            tuple(bundle.OPERATIONS),
        )
        self.assertEqual(len(bundle.OPERATIONS), 5)
        source = (ROOT / "tools/ot150_mbedtls_psa_protocol_runner.py").read_text(
            encoding="utf-8"
        )
        self.assertIn("ot149_mbedtls_psa_frames.py", source)

    def test_04_control_and_radio_remain_closed_and_authority_is_canonical(self) -> None:
        execution = authority._execution_contract()
        self.assertFalse(execution["control_application_writable"])
        self.assertFalse(execution["radio_allowed"])
        self.assertTrue(execution["application_only_writes"])
        self.assertEqual(execution["attempt_count"], 1)
        self.assertEqual(execution["node_count"], 2)
        path = ROOT / authority.AUTHORITY_RELATIVE
        self.assertTrue(path.is_file())
        raw = path.read_bytes()
        value = authority.decode_canonical(raw, "authority")
        self.assertEqual(raw, authority.canonical_document(value))
        self.assertEqual(value["schema"], authority.AUTHORITY_SCHEMA)
        self.assertEqual(value["execution"], execution)

    def test_05_canonical_preparation_when_present_binds_current_runtime(self) -> None:
        path = ROOT / authority.PREPARATION_RELATIVE
        if not path.exists():
            self.assertFalse((ROOT / authority.AUTHORITY_RELATIVE).exists())
            return
        raw = path.read_bytes()
        value = authority.decode_canonical(raw, "preparation")
        self.assertEqual(raw, authority.canonical_document(value))
        self.assertEqual(value["runtime"], authority._runtime_expected())
        self.assertFalse(value["authority"]["one_attempt_authority_created"])
        self.assertFalse(value["claims"]["execution_authorized"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
