#!/usr/bin/env python3
"""Fail-fast boundary between mutable Heltec inputs and historical builds."""

from __future__ import annotations

import hashlib
import importlib.util
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ACTIVE_CONFIG = "firmware/targets/heltec_v4_bench/sdkconfig.defaults"
ACTIVE_CONFIG_WINDOWS = ACTIVE_CONFIG.replace("/", "\\")
FIXTURES = {
    "tests/benchmarks/crypto/esp_idf/ot120_candidate_builds/"
    "historical_common_sdkconfig_ot120.defaults": (
        1368,
        "84d54e5d730ba28ceba0c97831a477303db128d63ede7575bec52013770d70c0",
    ),
    "tests/benchmarks/crypto/esp_idf/ot120_candidate_builds/"
    "historical_common_sdkconfig_ot130.defaults": (
        1424,
        "c1fd94c8979e5a7bb9b19753beb633da99f6edf86dc932c93d281ff2b99aa8b9",
    ),
    "tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/common/"
    "heltec_v4_sdkconfig.defaults": (
        1368,
        "84d54e5d730ba28ceba0c97831a477303db128d63ede7575bec52013770d70c0",
    ),
}


def _text(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def _load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    try:
        spec.loader.exec_module(module)
    finally:
        sys.modules.pop(name, None)
    return module


def test_historical_config_fixtures_are_exact() -> None:
    for relative, (size, digest) in FIXTURES.items():
        raw = (ROOT / relative).read_bytes()
        assert len(raw) == size, relative
        assert hashlib.sha256(raw).hexdigest() == digest, relative
        assert not raw.startswith(b"\xef\xbb\xbf"), relative
        assert b"\r" not in raw and raw.endswith(b"\n"), relative


def test_historical_cmake_recipes_never_read_live_target_defaults() -> None:
    root = ROOT / "tests/benchmarks/crypto/esp_idf"
    offenders = []
    for path in root.rglob("CMakeLists.txt"):
        text = path.read_text(encoding="utf-8")
        if ACTIVE_CONFIG in text or ACTIVE_CONFIG_WINDOWS in text:
            offenders.append(path.relative_to(ROOT).as_posix())
    assert offenders == [], offenders


def test_historical_build_orchestrators_never_read_live_target_defaults() -> None:
    offenders = []
    for path in (ROOT / "tools").glob("Build-Ot*.ps1"):
        text = path.read_text(encoding="utf-8")
        if ACTIVE_CONFIG in text or ACTIVE_CONFIG_WINDOWS in text:
            offenders.append(path.relative_to(ROOT).as_posix())
    assert offenders == [], offenders


def test_historical_python_consumers_resolve_immutable_storage() -> None:
    ot138_path = ROOT / "tools/ot138_monocypher_boot_control_investigation.py"
    ot138 = _load("ot138_dependency_boundary", ot138_path)
    assert ot138.COMMON_CONFIG_PATH == ROOT / next(iter(FIXTURES))
    assert not hasattr(ot138, "CURRENT_COMMON_CONFIG_SHA256")

    ot130_path = ROOT / "tools/ot130_monocypher_bundle_authority.py"
    ot130 = _load("ot130_dependency_boundary", ot130_path)
    storage, digest = ot130.HISTORICAL_SOURCE_STORAGE[ACTIVE_CONFIG]
    assert storage in FIXTURES
    assert digest == FIXTURES[storage][1]
    logical = ot130._fixed_binding(
        ACTIVE_CONFIG,
        "9186abaa6bd99429bb6d7d32f52f772b02dc122145438dc1547d2b94b948fe4a",
    )
    assert logical == {
        "path": ACTIVE_CONFIG,
        "raw_sha256": "9186abaa6bd99429bb6d7d32f52f772b02dc122145438dc1547d2b94b948fe4a",
    }


def test_historical_record_tests_do_not_pin_current_target_bytes() -> None:
    for relative in (
        "tests/host/ot123_monocypher_preparation_tests.py",
        "tests/host/ot149_mbedtls_psa_preparation_tests.py",
    ):
        text = _text(relative)
        assert "CURRENT_COMMON_CONFIG" not in text, relative


def test_ot106_legacy_helper_fails_before_live_checkout_inspection() -> None:
    source = _text("tools/Build-HeltecV4BenchCompactFooter.ps1")
    refusal = "OT-106 historical build helper is archived."
    assert refusal in source
    assert "1099fd352f9ca7a330f340e97af910d0353054d7" in source
    assert source.index("throw (") < source.index("$projectRoot =")


def main() -> int:
    tests = (
        test_historical_config_fixtures_are_exact,
        test_historical_cmake_recipes_never_read_live_target_defaults,
        test_historical_build_orchestrators_never_read_live_target_defaults,
        test_historical_python_consumers_resolve_immutable_storage,
        test_historical_record_tests_do_not_pin_current_target_bytes,
        test_ot106_legacy_helper_fails_before_live_checkout_inspection,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} historical/live target dependency boundary groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
