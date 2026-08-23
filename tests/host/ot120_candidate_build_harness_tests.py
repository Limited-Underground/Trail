#!/usr/bin/env python3
"""Static host checks for the bounded OT-120 ESP-IDF build harnesses."""

from __future__ import annotations

import hashlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tests/benchmarks/crypto/esp_idf/ot120_candidate_builds"
SCRIPT = ROOT / "tools/Build-Ot120CandidateImportEvidence.ps1"


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def normalized(path: Path) -> str:
    return path.read_text(encoding="utf-8").replace("\r\n", "\n").replace("\r", "\n")


def test_exact_file_set_and_overlays() -> None:
    expected = {
        "README.md",
        "reproducible.defaults",
        "espressif_libsodium/CMakeLists.txt",
        "espressif_libsodium/sdkconfig.overlay",
        "espressif_libsodium/partitions.csv",
        "espressif_libsodium/main/CMakeLists.txt",
        "espressif_libsodium/main/candidate_anchor.c",
        "esp_idf_mbedtls_psa/CMakeLists.txt",
        "esp_idf_mbedtls_psa/sdkconfig.overlay",
        "esp_idf_mbedtls_psa/partitions.csv",
        "esp_idf_mbedtls_psa/main/CMakeLists.txt",
        "esp_idf_mbedtls_psa/main/candidate_anchor.c",
        "monocypher/CMakeLists.txt",
        "monocypher/partitions.csv",
        "monocypher/main/CMakeLists.txt",
        "monocypher/main/candidate_anchor.c",
    }
    actual = {path.relative_to(HARNESS).as_posix() for path in HARNESS.rglob("*") if path.is_file()}
    assert actual == expected
    assert sha(HARNESS / "espressif_libsodium/sdkconfig.overlay") == (
        "b7b722dc1bcc2c5917bee365f2123171ec398b0a0f295d61e7a7e8c26b99c832"
    )
    assert sha(HARNESS / "esp_idf_mbedtls_psa/sdkconfig.overlay") == (
        "e8ec2842ad1aebe6c912970af54a28dc6de7e87e3b2554dfb2b40775626e388d"
    )
    assert sha(HARNESS / "reproducible.defaults") == (
        "995ce0b6c1a557b0132208af3744fc6672b3a026719c47d1cd50580004373fa6"
    )
    target_partition = normalized(ROOT / "firmware/targets/heltec_v4_bench/partitions.csv")
    for candidate in ("espressif_libsodium", "esp_idf_mbedtls_psa", "monocypher"):
        assert normalized(HARNESS / candidate / "partitions.csv") == target_partition


def test_exact_operation_anchor_partitions() -> None:
    libsodium = (HARNESS / "espressif_libsodium/main/candidate_anchor.c").read_text(encoding="utf-8")
    mbedtls = (HARNESS / "esp_idf_mbedtls_psa/main/candidate_anchor.c").read_text(encoding="utf-8")
    monocypher = (HARNESS / "monocypher/main/candidate_anchor.c").read_text(encoding="utf-8")
    for symbol in (
        "crypto_sign_detached", "crypto_sign_verify_detached", "crypto_scalarmult_curve25519",
        "crypto_hash_sha256", "crypto_kdf_hkdf_sha256_extract",
        "crypto_aead_chacha20poly1305_ietf_encrypt",
        "crypto_aead_chacha20poly1305_ietf_decrypt", "ot_noise_xk_init_initiator",
    ):
        assert f"OT_LINK_ANCHOR({symbol})" in libsodium
    for symbol in (
        "psa_raw_key_agreement", "psa_hash_compute", "psa_key_derivation_setup",
        "psa_aead_encrypt", "psa_aead_decrypt",
    ):
        assert f"OT_LINK_ANCHOR({symbol})" in mbedtls
    for symbol in (
        "ot_monocypher_ed25519_sign", "ot_monocypher_ed25519_verify", "ot_monocypher_x25519",
        "ot_monocypher_chacha20poly1305_ietf_encrypt",
        "ot_monocypher_chacha20poly1305_ietf_decrypt",
    ):
        assert f"OT_LINK_ANCHOR({symbol})" in monocypher
    for unavailable in ("sha256", "hkdf", "noise_xk"):
        assert unavailable not in monocypher.lower()
    for unavailable in ("ed25519", "noise_xk"):
        assert unavailable not in mbedtls.lower()
    monocypher_cmake = normalized(HARNESS / "monocypher/main/CMakeLists.txt")
    assert "PRIV_REQUIRES\n        esp_system" in monocypher_cmake


def test_orchestrator_is_fail_closed_and_host_only() -> None:
    source = SCRIPT.read_text(encoding="utf-8")
    validator = "crypto_candidate_import_build_admission.py"
    assert validator in source
    assert source.index("& $python.Source $ContractValidatorPath --contract $ContractPath") < source.index("if (-not $Execute)")
    assert "$env:IDF_COMPONENT_MANAGER = '0'" in source
    assert "'--no-ccache'" in source
    assert "$commonDefaults;$reproducibleDefaults" in source
    assert "'PROJECT_VER=ot107-config-v0'" in source
    assert "foreach ($run in @('A', 'B'))" in source
    assert "Get-Command xtensa-esp32s3-elf-nm" in source
    assert "normalized_receipt_sha256" in source
    assert "one_time_authority" in source
    assert "OT-120-OT005-CANDIDATE-IMPORT-BUILD-ADMISSION-DELTA-V1.json" in source
    assert "accepted_candidate_imports" in source
    assert "fresh_benchmark_execution_authority_absent" in source
    assert "benchmark_execution_scope_included = $false" in source
    assert "consumed = $true" in source
    assert "foreach ($item in $generatedArtifacts)" in source
    assert "$validationArguments += @('--graph', $item.graph_path)" in source
    assert "$validationArguments += @('--evidence', $item.evidence_path)" in source
    assert "$validationArguments += @('--admission', $admissionPath)" in source
    for forbidden in ("idf.py flash", "idf.py monitor", "esptool", "write-flash", "erase-flash"):
        assert forbidden not in source.lower()


def main() -> int:
    tests = (
        test_exact_file_set_and_overlays,
        test_exact_operation_anchor_partitions,
        test_orchestrator_is_fail_closed_and_host_only,
    )
    for test in tests:
        test()
    print(f"PASS: {len(tests)} OT-120 candidate build-harness groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
