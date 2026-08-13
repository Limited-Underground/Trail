# Decision 0006: Firmware Bundle Release Signatures

Status: accepted host architecture; production key provisioning and target
update evidence pending, 2026-08-13

## Decision

The Windows device utility verifies the exact canonical firmware-bundle
manifest with RSA-PSS using a 3072-bit RSA public key and SHA-256. The manifest
binds the complete firmware image length and SHA-256 digest plus the hardware
profile, processor, target role, revision range, bootloader schema, and release
generation. The detached `manifest.sig` is exactly 384 bytes.

The v0 manifest declares the exact algorithm identifier
`rsa_pss_3072_sha256`. Alternative algorithms, RSA key sizes, manifest
encodings, signature lengths, or implicit algorithm selection fail closed.

The loader's signer catalog:

- contains public SubjectPublicKeyInfo bytes only;
- accepts RSA-3072 keys with exponent 65537;
- is capped at three pinned public keys;
- derives the 16-character signer lookup ID from the first eight bytes of
  SHA-256 over the complete public SubjectPublicKeyInfo; and
- verifies against the complete pinned public key, so the shortened signer ID
  is a lookup value rather than the cryptographic authority.

The packaged catalog is intentionally empty. No production release signer has
been approved or embedded, so a selected candidate cannot reach firmware
admission or enable a device write.

## Separate key roles

The firmware release-signing key is separate from:

- radio/member identity keys;
- group-access or transport keys; and
- any target Secure Boot key.

These roles must never reuse private key material. The Windows package will
contain only approved release-verification public keys. Private signing keys
must never enter the repository, application package, build log, device
inspection result, or test artifact.

## Why

RSA-PSS is the modern RSA signature construction recommended for new
applications by [RFC 8017](https://www.rfc-editor.org/rfc/rfc8017). The ESP32-S3
[Secure Boot v2 documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/security/secure-boot-v2.html)
uses RSA-PSS with RSA-3072 and describes up to three key blocks plus revocation.
Using the same algorithm family and key size reduces unnecessary implementation
variation without treating a Windows release signature as a substitute for
target Secure Boot. .NET provides the required host verification through
[`RSA.VerifyData`](https://learn.microsoft.com/en-us/dotnet/api/system.security.cryptography.rsa.verifydata?view=net-9.0),
so the packaged utility needs no external cryptography runtime.

The three-key catalog limit mirrors a bounded rotation/revocation shape. It
does not claim that the host catalog and ESP32 eFuses share keys, storage, or
security authority.

## Current evidence

Host tests create ephemeral RSA-3072 key pairs, sign the exact canonical
manifest with RSA-PSS/SHA-256, and prove that:

- the matching pinned public key verifies the signature;
- an empty catalog and a different catalog remain untrusted;
- a changed signature or changed canonical manifest fails closed; and
- an RSA key of the wrong size cannot enter the catalog.

One additional fixed public-only vector contains the exact canonical manifest,
public SubjectPublicKeyInfo, and 384-byte signature. No private key is retained.
The same manifest/signature passes the real Windows loader, OpenSSL 3.5.6,
Espressif `espsecure` 5.3.1 Secure Boot v2 verification, and `cryptography`
50.0.0 with a 32-byte PSS salt. OpenSSL and Espressif both reject a changed
manifest. The vector is included in the Windows host matrix and its pinned tool
requirements are explicit. This remains host interoperability evidence—not a
production key, target Secure Boot configuration, public release, or physical
update result.

## Remaining gates

- obtain independent security review of the fixed vector, 32-byte PSS salt,
  signer-ID construction, and toolchain pinning;
- approve offline private-key custody, access, backup, audit, rotation,
  revocation, compromise response, and release ceremony;
- pin approved production public keys and protect signer-generation state;
- connect verified signature evidence to the existing fail-closed bundle
  admission policy and authoritative exact-board policy;
- produce reproducible signed release bundles without exposing private keys;
  and
- prove target write/readback, trial boot, rollback, ROM recovery, power-loss,
  and wrong-target rejection before enabling any writer.
