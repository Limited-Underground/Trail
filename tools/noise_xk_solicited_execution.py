"""Exact-bundle admission around the concrete solicited execution path.

No CLI or authority issuance. The caller supplies admitted roles and an exact
one-use grant, after a fresh source-checked process and physical preflight.
Recovery intentionally needs no benchmark file or build output.
"""
from __future__ import annotations

from pathlib import Path
import noise_xk_solicited_bundle as bundle
import noise_xk_solicited_coordinator as coordinator


def _admit(root: Path, snapshot: dict, paths: dict, config, *, recovery: bool) -> None:
    bundle.verify_sources(root, snapshot["sources"])
    bundle.verify_images(root, snapshot["images"], paths, recovery=recovery)
    # Source provenance is an execution gate; an absent build artifact must not
    # prevent restoration using already bound recovery payloads.
    if not recovery:
        bundle.verify_build_provenance(root, snapshot["images"]["images"]["benchmark"])
    expected = snapshot["images"]["images"]
    binding = config.binding
    actual = {
        "benchmark": {"name": binding.benchmark_name, "bytes": binding.benchmark_bytes,
                      "sha256": binding.benchmark_sha256},
        "restore_a": vars(binding.restore_a), "restore_b": vars(binding.restore_b),
    }
    bundle.previous._require(actual == expected, "configuration_image_mismatch")
    for role, path in zip(("restore_a", "restore_b"), config.restore_paths):
        bundle.previous._require(path.resolve() == (root / paths[role]).resolve(),
                                 "configuration_path_mismatch")
    bundle.previous._require(config.benchmark_path.resolve() == (root / paths["benchmark"]).resolve(),
                             "configuration_path_mismatch")


def execute(root: Path, snapshot: dict, paths: dict, config, backend, authority):
    _admit(root, snapshot, paths, config, recovery=False)
    return coordinator.execute(config, backend, authority)


def recover(root: Path, snapshot: dict, paths: dict, config, backend, authority):
    _admit(root, snapshot, paths, config, recovery=True)
    return coordinator.recover(config, backend, authority)
