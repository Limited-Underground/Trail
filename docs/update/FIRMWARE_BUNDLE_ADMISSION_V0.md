# Firmware bundle admission v0

Status: pure fail-closed host policy with twelve deterministic scenario groups.
A separate Windows candidate parser now verifies bounded container structure,
canonical manifest bytes, image length, image SHA-256, and—when a signer is
pinned—RSA-PSS-3072/SHA-256 over the exact manifest. The packaged trust catalog
is deliberately empty and no rollback-policy, release-admission, writer, or
physical update evidence exists.

## Purpose

The Windows loader must reject a firmware file before it considers a connected
board. `FirmwareBundleAdmissionV0` evaluates evidence supplied by future
bounded parsing and cryptographic adapters and permits a bundle to proceed only
when its complete manifest, signer, image, target binding, and rollback state
are coherent.

Admission is not flash permission. A successfully admitted bundle must still
pass the separate board/install preflight, local authorization, write/readback,
trial boot, health confirmation, and recovery flow. The
[final write-admission composition](FIRMWARE_WRITE_ADMISSION_V0.md) requires the
two policy domains and their shared fields to agree.

## Bound manifest fields

The v0 manifest model contains:

- exact schema and canonical byte count;
- exact nonzero hardware-profile identifier;
- processor and firmware target role;
- exact supported board-revision range;
- minimum bootloader schema;
- nonzero monotonically trusted release generation;
- exact nonzero image length and SHA-256 digest; and
- exact opaque signer-key identifier.

The manifest carries no device serial number, participant/group data,
credentials, location, USB path, or human-entered device identity.

## Required external evidence

The admission policy requires separate evidence that:

- the whole container and image were read;
- the manifest parsed and used the exact canonical encoding;
- the manifest digest was independently verified;
- its signature was cryptographically verified;
- the signer is currently trusted and has the exact expected key identifier;
- the observed image length equals the signed length and fits the profile; and
- the observed image digest equals the signed digest.

A present digest, key identifier, or signature-shaped field never counts as
verification. The Windows adapter can now supply real .NET host signature
evidence to a future composition, but the admission tests continue to inject
explicit evidence and the packaged app has no approved signer.

## Fail-closed binding

- Hardware profile, processor, and target role must match exact policy. A
  bench-client image cannot be admitted for a complete client or packaged
  repeater.
- The signed board-revision range must be valid and exactly equal to the
  owner-approved policy range.
- The signed minimum bootloader schema cannot weaken policy.
- Release generation zero is invalid; a generation below the protected trusted
  floor is rejected as rollback.
- Missing, zero-length, truncated, longer-than-signed, or oversized images are
  rejected independently of digest evidence.
- Unknown processor/role values fail closed.

## Host evidence

Twelve groups cover an exact admitted candidate, invalid policy, incomplete
container/parse, schema/canonical/length errors, independent manifest digest and
signature evidence, signer trust/ID mismatch, hardware/processor/role crossing,
revision range, bootloader/generation rollback, image read/length/capacity,
digest presence versus verification, and unknown enum values.

## What remains

- review and freeze the current bounded candidate container as a production
  format or replace it before signed releases;
- connect its image/signature result to the admission owner only after approved
  signer and policy evidence are also available;
- obtain independent security review of the now-passing fixed RSA-PSS vector,
  signer fingerprint, explicit salt length, and pinned toolchain;
- define offline signing, key custody, rotation, revocation, and emergency
  recovery procedures;
- pin approved public signers and bind protected revocation and release-
  generation state;
- generate reproducible exact-board bundles in CI without exposing private
  signing keys;
- compose admitted evidence with the physical board/install preflight; and
- validate wrong-target, corrupt/truncated bundle, power interruption,
  readback, trial, rollback, and recovery on every supported revision.
