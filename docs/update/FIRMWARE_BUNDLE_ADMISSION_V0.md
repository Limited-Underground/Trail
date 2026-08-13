# Firmware bundle admission v0

Status: pure fail-closed host policy with twelve deterministic scenario groups;
no container parser, SHA-256 adapter, signature implementation, trusted signer
store, release bundle, writer, or physical update result exists.

## Purpose

The Windows loader must reject a firmware file before it considers a connected
board. `FirmwareBundleAdmissionV0` evaluates evidence supplied by future
bounded parsing and cryptographic adapters and permits a bundle to proceed only
when its complete manifest, signer, image, target binding, and rollback state
are coherent.

Admission is not flash permission. A successfully admitted bundle must still
pass the separate board/install preflight, local authorization, write/readback,
trial boot, health confirmation, and recovery flow.

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
verification. Those adapters do not exist yet, and the host tests use explicit
fake evidence rather than claiming real cryptography.

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

- freeze a canonical on-disk manifest/container encoding and size;
- select and implement reviewed SHA-256 and signature-verification adapters;
- define offline signing, key custody, rotation, revocation, and emergency
  recovery procedures;
- bind a protected trusted-signer store and release-generation floor;
- generate reproducible exact-board bundles in CI without exposing private
  signing keys;
- compose admitted evidence with the physical board/install preflight; and
- validate wrong-target, corrupt/truncated bundle, power interruption,
  readback, trial, rollback, and recovery on every supported revision.
