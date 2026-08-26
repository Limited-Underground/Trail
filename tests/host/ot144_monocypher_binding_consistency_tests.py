#!/usr/bin/env python3
"""Real-file cross-layer binding regression for the OT-144 successor."""

from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path
import sys
import unittest


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


ot144_coordinator = load_module(
    "_ot144_binding_coordinator", "tools/ot144_monocypher_coordinator.py"
)
ot144_authority = load_module(
    "_ot144_binding_authority", "tools/ot144_monocypher_execution_authority.py"
)
ot143_coordinator = load_module(
    "_ot143_binding_coordinator", "tools/ot143_monocypher_coordinator.py"
)
ot143_authority = load_module(
    "_ot143_binding_authority", "tools/ot143_monocypher_execution_authority.py"
)


def binding_mismatches(coordinator, authority) -> set[str]:
    binding = coordinator._binding()
    values = {
        "benchmark_name": (
            binding.benchmark_name,
            authority.BENCHMARK_NAME,
        ),
        "benchmark_bytes": (
            binding.benchmark_bytes,
            authority.BENCHMARK_BYTES,
        ),
        "benchmark_sha256": (
            binding.benchmark_sha256,
            authority.BENCHMARK_SHA256,
        ),
        "restore_name": (
            binding.restore_name,
            authority.RESTORE_NAME,
        ),
        "restore_bytes": (
            binding.restore_bytes,
            authority.RESTORE_BYTES,
        ),
        "restore_sha256": (
            binding.restore_sha256,
            authority.RESTORE_SHA256,
        ),
        "application_offset": (
            binding.application_offset,
            authority._execution_contract()["application_offset"],
        ),
    }
    return {name for name, (left, right) in values.items() if left != right}


class BindingConsistencyTests(unittest.TestCase):
    def test_ot144_real_coordinator_and_authority_bind_same_tuple(self) -> None:
        self.assertEqual(binding_mismatches(ot144_coordinator, ot144_authority), set())
        self.assertEqual(ot144_authority.BENCHMARK_NAME, "ot142_monocypher_corrected_bench.bin")
        self.assertEqual(ot144_authority.BENCHMARK_BYTES, 149_824)
        self.assertEqual(
            ot144_authority.BENCHMARK_SHA256,
            "8e345d41e869cf781e3d6eb3b2269f26882b7c1d9f43b856ff5795c6cc56a034",
        )

    def test_ot144_authority_hash_binds_real_coordinator_bytes(self) -> None:
        coordinator_bindings = [
            (relative, digest)
            for role, relative, digest in ot144_authority.FIXED_RUNTIME_BINDINGS
            if role == "coordinator"
        ]
        self.assertEqual(len(coordinator_bindings), 1)
        relative, digest = coordinator_bindings[0]
        self.assertEqual(relative, "tools/ot144_monocypher_coordinator.py")
        self.assertEqual(
            hashlib.sha256((ROOT / relative).read_bytes()).hexdigest(),
            digest,
        )

    def test_canonical_preparation_binds_real_coordinator_and_benchmark(self) -> None:
        preparation, raw = ot144_authority.load_canonical(
            ROOT / ot144_authority.PREPARATION_RELATIVE,
            "OT-144 preparation",
        )
        self.assertEqual(raw, ot144_authority.canonical_document(preparation))
        coordinator = preparation["runtime"]["coordinator"]
        self.assertEqual(
            coordinator["path"],
            "tools/ot144_monocypher_coordinator.py",
        )
        self.assertEqual(
            coordinator["raw_sha256"],
            hashlib.sha256(
                (ROOT / coordinator["path"]).read_bytes()
            ).hexdigest(),
        )
        benchmark = preparation["images"]["benchmark"]
        self.assertEqual(benchmark["name"], ot144_coordinator.BENCHMARK_NAME)
        self.assertEqual(benchmark["bytes"], ot144_coordinator.BENCHMARK_BYTES)
        self.assertEqual(benchmark["sha256"], ot144_coordinator.BENCHMARK_SHA256)
        self.assertFalse(preparation["claims"]["execution_authorized"])
    def test_regression_detects_immutable_ot143_binding_mismatch(self) -> None:
        self.assertEqual(
            binding_mismatches(ot143_coordinator, ot143_authority),
            {"benchmark_name", "benchmark_bytes", "benchmark_sha256"},
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
