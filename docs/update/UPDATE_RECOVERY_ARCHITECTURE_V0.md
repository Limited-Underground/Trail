# Update and recovery architecture v0

Status: architecture plus a pure host-tested lifecycle guard; no updater, target
partition table, signing key, persistent trial adapter, or physical recovery
result exists.

## Host lifecycle evidence

`UpdateBootGuard` is deliberately unable to verify signatures, write flash,
select boot slots, persist state, or execute rollback. It accepts only adapter-
supplied verification/write/boot evidence and enforces the lifecycle around it.
Eight deterministic groups cover policy/candidate refusal, staging cancellation,
complete-write evidence, stable-time and health confirmation, the exact
confirmation deadline, trial-boot exhaustion, boot mismatch plus exact rollback
completion, explicit health failure, duplicate boot sessions, and monotonic-
clock regression.

## Safety objective

A failed, interrupted, incompatible, or unhealthy update must not silently leave
a client unusable. Base radio operation remains the recovery priority. Updating
is an explicit maintenance action, never an automatic dependency for joining or
using a group.

## Release artifact

Every candidate release is a versioned bundle containing:

- an immutable hardware-family identifier and minimum bootloader/schema level;
- monotonically increasing firmware version and security epoch;
- exact image length and cryptographic digest;
- signed manifest plus signed image;
- declared application slot and required partition-table version;
- release notes and a compatibility statement; and
- separately verified recovery-image identity.

The target rejects unknown hardware, downgrade, wrong partition layout, length
or digest mismatch, missing/invalid signature, unsupported schema, and an image
larger than its inactive slot. Transport security does not replace artifact
signature verification.

## A/B installation sequence

1. Download or receive only into bounded staging storage.
2. Verify the complete manifest and image before erasing the inactive slot.
3. Write only the inactive application slot.
4. Read back and verify the complete written image.
5. Persist the pending trial record before selecting the trial slot.
6. Reboot into the trial image with a bounded boot-attempt count.
7. Confirm only after the minimum stable interval and every required health gate.
8. If confirmation is missing, health fails, or boot attempts are exhausted,
   select the previously confirmed slot and record a typed rollback reason.

Power loss at any step must leave either the prior confirmed image bootable or a
bootloader/USB recovery path. A partially written candidate is never bootable.

## Trial health

The first portable-client policy requires all of these independent signals:

- runtime scheduler started;
- watchdog remains healthy;
- non-secret configuration loaded or safely defaulted;
- protected state opened without rollback/conflict;
- radio initialized and can enter cooperative service;
- local display/input initialized or declared safely unavailable; and
- power status is readable enough to avoid confirming during critical power.

A GNSS fix, peer presence, internet service, map package, OpenGauge connection,
or successful field transmission is not required to confirm an otherwise
healthy base client. Missing optional hardware remains visible but must not
cause a permanent update loop.

## Version and rollback policy

Normal installation accepts only a version newer than the confirmed version and
a security epoch at least as new as the trusted floor. Developer downgrade is a
separate physical maintenance mode that erases group secrets and cannot be
entered remotely. Ordinary factory reset does not lower the trusted firmware
floor.

The trial record, confirmed slot/version, boot-attempt count, and trusted floor
need authenticated, interruption-safe target storage. CRC alone is insufficient
against deliberate rollback.

## Recovery paths

Recovery is layered and must be tested separately:

1. automatic rollback to the last confirmed application;
2. bootloader selection using documented physical controls;
3. USB serial/DFU/ROM recovery with an exact supported host procedure; and
4. factory recovery image, if the selected target can protect and verify one.

Recovery documentation must identify what is erased, what can be preserved, and
how group membership/secrets are revoked after an untrusted or developer-mode
recovery. No remote recovery command may bypass physical authorization.

## Update transports

USB is the first supported transport. A later local browser, companion, or
optional server may deliver the same signed bundle, but none may weaken the
verification or recovery rules. LoRa is not an image-transfer transport.
Metadata announcements over radio are advisory and never authorize installation.

## Evidence required before implementation is accepted

- exact target, bootloader, partition table, secure-boot/flash-encryption state,
  toolchain, and signing implementation;
- valid, corrupt, truncated, wrong-target, downgrade, and wrong-epoch bundles;
- power interruption during staging, erase, write, readback, pending-record,
  boot-selection, trial, confirmation, and rollback;
- boot-loop and maximum-attempt enforcement;
- each required and optional health signal, including partial availability;
- automatic, physical-button, and USB recovery from documented failure states;
- preservation/erasure behavior for configuration, identity, and group secrets;
- repeat/endurance counts, timings, power conditions, and artifact hashes.

Until those target results exist, this architecture and host guard are design
evidence rather than a supported OTA or recovery claim.
