#!/usr/bin/env python3
"""Verify the public firmware-bundle signature vector with independent tools."""

from __future__ import annotations

import argparse
import base64
import hashlib
import importlib.metadata
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


VECTOR_SCHEMA = "ot_firmware_bundle_signature_vector_v0"
SIGNATURE_ALGORITHM = "rsa_pss_3072_sha256"
ESPSECURE_VERSION = "5.3.1"
CRYPTOGRAPHY_VERSION = "50.0.0"
EXPECTED_VECTOR_PROPERTIES = (
    "schema",
    "signature_algorithm",
    "pss_salt_bytes",
    "signer_id",
    "manifest_sha256",
    "image_pattern",
    "image_bytes",
    "signed_manifest_utf8_base64",
    "public_key_spki_der_base64",
    "signature_base64",
)
EXPECTED_MANIFEST_PROPERTIES = (
    "schema",
    "canonical_manifest_bytes",
    "hardware_profile_id",
    "processor",
    "target_role",
    "minimum_board_revision",
    "maximum_board_revision",
    "minimum_bootloader_schema",
    "release_generation",
    "image_bytes",
    "image_sha256",
    "signer_id",
    "signature_algorithm",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def decode_base64(value: object, label: str) -> bytes:
    require(isinstance(value, str) and value, f"{label} must be nonempty text")
    try:
        return base64.b64decode(value, validate=True)
    except (ValueError, TypeError) as error:
        raise ValueError(f"{label} is not canonical base64") from error


def load_vector(path: Path) -> tuple[dict[str, object], bytes, bytes, bytes]:
    raw = path.read_bytes()
    require(b"\r" not in raw, "vector must use canonical LF line endings")
    vector = json.loads(raw)
    require(isinstance(vector, dict), "vector root must be an object")
    require(tuple(vector) == EXPECTED_VECTOR_PROPERTIES, "vector properties are not canonical")
    require(vector["schema"] == VECTOR_SCHEMA, "vector schema is not accepted")
    require(vector["signature_algorithm"] == SIGNATURE_ALGORITHM, "algorithm is not accepted")
    require(vector["pss_salt_bytes"] == 32, "RSA-PSS salt must be exactly 32 bytes")
    require(vector["image_pattern"] == "index_mod_251", "image pattern is not accepted")
    require(vector["image_bytes"] == 1024, "image length is not accepted")

    manifest = decode_base64(vector["signed_manifest_utf8_base64"], "manifest")
    public_spki = decode_base64(vector["public_key_spki_der_base64"], "public key")
    signature = decode_base64(vector["signature_base64"], "signature")
    require(len(signature) == 384, "RSA-3072 signature must be exactly 384 bytes")
    require(
        hashlib.sha256(manifest).hexdigest() == vector["manifest_sha256"],
        "manifest digest does not match vector",
    )
    signer_id = hashlib.sha256(public_spki).digest()[:8].hex()
    require(signer_id == vector["signer_id"], "signer ID does not match public key")

    manifest_object = json.loads(manifest)
    require(isinstance(manifest_object, dict), "manifest root must be an object")
    require(
        tuple(manifest_object) == EXPECTED_MANIFEST_PROPERTIES,
        "manifest properties are not canonical",
    )
    canonical = json.dumps(
        manifest_object,
        separators=(",", ":"),
        ensure_ascii=True,
    ).encode("ascii")
    require(canonical == manifest, "manifest bytes are not canonical")
    require(
        manifest_object["canonical_manifest_bytes"] == len(manifest),
        "manifest byte count is not exact",
    )
    require(manifest_object["signer_id"] == signer_id, "manifest signer ID is not exact")
    require(
        manifest_object["signature_algorithm"] == SIGNATURE_ALGORITHM,
        "manifest signature algorithm is not exact",
    )
    require(
        manifest_object["image_bytes"] == vector["image_bytes"],
        "vector and manifest image lengths differ",
    )
    image = bytes(index % 251 for index in range(int(vector["image_bytes"])))
    require(
        manifest_object["image_sha256"] == hashlib.sha256(image).hexdigest(),
        "manifest image digest does not match the deterministic fixture image",
    )
    return vector, manifest, public_spki, signature


def find_openssl(explicit: str | None) -> Path:
    candidates: list[str] = []
    if explicit:
        candidates.append(explicit)
    configured = os.environ.get("OPENTRAIL_OPENSSL")
    if configured:
        candidates.append(configured)
    discovered = shutil.which("openssl")
    if discovered:
        candidates.append(discovered)
    if os.name == "nt":
        candidates.append(r"C:\Program Files\Git\usr\bin\openssl.exe")
    for candidate in candidates:
        path = Path(candidate).resolve()
        if path.is_file():
            return path
    raise FileNotFoundError("OpenSSL was not found; set OPENTRAIL_OPENSSL to its executable")


def run(command: list[str], expected_success: bool, label: str) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(command, capture_output=True, text=True, timeout=30)
    succeeded = completed.returncode == 0
    if succeeded != expected_success:
        detail = (completed.stdout + completed.stderr).strip()
        if len(detail) > 1000:
            detail = detail[:1000] + "..."
        raise RuntimeError(f"{label} produced an unexpected result: {detail}")
    return completed


def main() -> int:
    parser = argparse.ArgumentParser()
    default_vector = (
        Path(__file__).resolve().parents[1]
        / "tests"
        / "fixtures"
        / "firmware_bundle_signature_vector_v0.json"
    )
    parser.add_argument("--vector", type=Path, default=default_vector)
    parser.add_argument("--openssl")
    args = parser.parse_args()

    vector, manifest, public_spki, signature = load_vector(args.vector.resolve())
    openssl = find_openssl(args.openssl)
    openssl_version = run([str(openssl), "version"], True, "OpenSSL version").stdout.strip()
    require(openssl_version.startswith("OpenSSL 3."), "OpenSSL 3.x is required")

    installed_espsecure = importlib.metadata.version("esptool")
    require(
        installed_espsecure == ESPSECURE_VERSION,
        f"espsecure/esptool {ESPSECURE_VERSION} is required",
    )
    installed_cryptography = importlib.metadata.version("cryptography")
    require(
        installed_cryptography == CRYPTOGRAPHY_VERSION,
        f"cryptography {CRYPTOGRAPHY_VERSION} is required",
    )
    import espsecure

    with tempfile.TemporaryDirectory(prefix="OpenTrail.SignatureVector.") as temporary:
        root = Path(temporary).resolve()
        manifest_path = root / "manifest.bin"
        public_der_path = root / "public.der"
        public_pem_path = root / "public.pem"
        signature_path = root / "manifest.sig"
        espsecure_image_path = root / "espsecure-image.bin"
        tampered_manifest_path = root / "tampered-manifest.bin"
        tampered_espsecure_path = root / "tampered-espsecure-image.bin"

        manifest_path.write_bytes(manifest)
        public_der_path.write_bytes(public_spki)
        signature_path.write_bytes(signature)
        run(
            [
                str(openssl),
                "pkey",
                "-pubin",
                "-inform",
                "DER",
                "-in",
                str(public_der_path),
                "-out",
                str(public_pem_path),
            ],
            True,
            "OpenSSL public-key conversion",
        )
        openssl_verify = [
            str(openssl),
            "dgst",
            "-sha256",
            "-verify",
            str(public_pem_path),
            "-signature",
            str(signature_path),
            "-sigopt",
            "rsa_padding_mode:pss",
            "-sigopt",
            "rsa_pss_saltlen:32",
            str(manifest_path),
        ]
        run(openssl_verify, True, "OpenSSL vector verification")

        tampered_manifest = bytearray(manifest)
        tampered_manifest[len(tampered_manifest) // 2] ^= 0x01
        tampered_manifest_path.write_bytes(tampered_manifest)
        openssl_tamper = [*openssl_verify[:-1], str(tampered_manifest_path)]
        run(openssl_tamper, False, "OpenSSL tamper rejection")

        with io.BytesIO(signature) as signature_input, io.BytesIO(
            public_pem_path.read_bytes()
        ) as public_input:
            signature_block = espsecure.generate_signature_block_using_pre_calculated_signature(
                [signature_input],
                [public_input],
                manifest,
            )
        require(len(signature_block) == espsecure.SIG_BLOCK_SIZE, "Espressif block size is not exact")
        signature_sector = signature_block + b"\xff" * (
            espsecure.SECTOR_SIZE - len(signature_block)
        )
        espsecure_image = manifest + signature_sector
        espsecure_image_path.write_bytes(espsecure_image)
        espsecure_verify = [
            sys.executable,
            "-m",
            "espsecure",
            "verify-signature",
            "--version",
            "2",
            "--keyfile",
            str(public_pem_path),
            "--skip-padding",
            str(espsecure_image_path),
        ]
        run(espsecure_verify, True, "Espressif vector verification")

        tampered_espsecure_path.write_bytes(bytes(tampered_manifest) + signature_sector)
        espsecure_tamper = [*espsecure_verify[:-1], str(tampered_espsecure_path)]
        run(espsecure_tamper, False, "Espressif tamper rejection")

    print(
        "PASS: fixed RSA-PSS-3072/SHA-256 manifest vector verified by "
        f"{openssl_version}, espsecure {installed_espsecure}, and "
        f"cryptography {installed_cryptography}; tampering rejected"
    )
    print(
        "INFO: public vector "
        f"signer={vector['signer_id']} manifest_sha256={vector['manifest_sha256']} "
        "private_key_stored=false"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
