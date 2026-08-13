# Firmware bundle candidate format v0

Status: bounded Windows structural/image inspection plus an RSA-PSS-3072/
SHA-256 verifier and immutable public-key trust boundary. The packaged signer
catalog is deliberately empty; release admission, board matching, and device
writes remain absent.

## Purpose

The Windows device utility now lets an operator select a local candidate bundle
without granting that file any device authority. The first adapter answers only:

- is the archive small enough to inspect safely;
- does it contain the exact expected entry set;
- is its manifest canonically encoded and internally bounded;
- does the complete image length match the manifest; and
- does the complete image SHA-256 match the manifest; and
- when the signer is pinned, does RSA-PSS-3072/SHA-256 verify the exact
  canonical manifest?

A positive result from the packaged utility is currently limited to structural
and image-digest evidence because it has no production signer. Injected host
tests can also produce signature evidence through an ephemeral trusted public
key. Neither result is firmware admission, board matching, or Flash permission.

## Container

The working extension is `.fwbundle`. The file is a ZIP archive capped at 20
MiB and must contain exactly three root entries with ordinal names:

1. `manifest.json`
2. `image.bin`
3. `manifest.sig`

Directories, duplicate names, alternate case, and extra entries are rejected.
The manifest is capped at 4,096 bytes, the image at 16 MiB, and the detached
RSA-3072 signature is exactly 384 bytes. An all-zero signature is rejected, but
mere presence never counts as cryptographic verification.

## Canonical manifest

The manifest is compact UTF-8 JSON with no BOM, whitespace, comments, trailing
commas, unknown properties, or alternate property order. Its exact property
sequence is:

1. `schema` = `firmware_bundle_candidate_v0`
2. `canonical_manifest_bytes`
3. `hardware_profile_id`
4. `processor`
5. `target_role`
6. `minimum_board_revision`
7. `maximum_board_revision`
8. `minimum_bootloader_schema`
9. `release_generation`
10. `image_bytes`
11. `image_sha256`
12. `signer_id`
13. `signature_algorithm` = `rsa_pss_3072_sha256`

Numeric fields use JSON integers. Hardware profile, minimum board revision,
release generation, and image length must be nonzero; the maximum board
revision cannot precede the minimum. Processor is exactly `esp32_s3` or
`nrf52840`. Target role is exactly `bench_client`, `complete_client`, or
`packaged_repeater`. SHA-256 is 64 lowercase hexadecimal characters. Signer ID
is 16 lowercase hexadecimal characters and cannot be all zero. It is derived
from the first eight bytes of SHA-256 over the complete SubjectPublicKeyInfo
and is used only to find the pinned full public key.

The parser reserializes these fields and requires byte-for-byte equality with
the selected manifest. The manifest's declared byte count must equal the bytes
actually read.

## Security and privacy boundary

- The selected path and filename are never copied into the view model or UI.
- Archive contents are opened read-only and are not extracted to disk.
- The image is streamed through SHA-256 and is never written by the utility.
- Raw manifest, signature, and image bytes are discarded after inspection.
- Exact RSA-PSS-3072/SHA-256 verification uses only pinned public
  SubjectPublicKeyInfo. The catalog accepts at most three RSA-3072 keys with
  exponent 65537.
- The shortened signer ID is not authority by itself; verification uses the
  complete matching public key.
- The production catalog is empty. No private or production public signing key
  exists in the application, package, repository, or evidence artifact.
- There is no protected revocation/generation store, exact-device policy, final
  admission result, writer, erase, reset, reboot, DFU, or recovery adapter.

The screen therefore keeps `Flash selected device` disabled after a successful
candidate inspection and states that no trusted release signer is configured.
Even an injected host-verified signature keeps Flash blocked because exact-
device matching and release policy are not composed.

## Host evidence

Thirteen candidate/signature scenario groups cover an ephemeral trusted
RSA-3072 signer, the packaged empty catalog, an unrelated catalog, and
rejection of the wrong extension, extra archive entries, noncanonical manifest
bytes, image-digest mismatch, an all-zero signature, image-length mismatch,
signature tampering, post-signing manifest change, a non-RSA-3072 trust key,
and the fixed public cross-tool vector. They run inside the 44-group warning-
free Windows-loader validation suite alongside the live read-only three-device
inspection. The fixed vector also passes OpenSSL 3.5.6 and Espressif
`espsecure` 5.3.1 verification with an explicit 32-byte salt; both reject a
changed manifest. No private key is stored.

## Remaining gates

- obtain independent security review of the fixed cross-tool vector, 32-byte
  PSS salt, signer fingerprint, and toolchain pins;
- approve signing-key custody, recovery, rotation, revocation, and compromise
  procedures, then pin only approved production public keys;
- bind protected signer revocation and release-generation state;
- create reproducible signed releases without exposing private keys;
- bind the admitted manifest to an authoritative connected-board profile; and
- separately implement and validate one-use write authorization, readback,
  trial boot, rollback, and recovery.

The selected algorithm and trust separation are recorded in
[Decision 0006](../decisions/0006-firmware-bundle-signature.md).
