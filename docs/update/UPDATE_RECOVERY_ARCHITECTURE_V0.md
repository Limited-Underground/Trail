# Update and recovery architecture v0

Status: architecture plus host-tested lifecycle, reboot-state, and fail-closed
install-preflight policies; no updater, target partition table, signing key,
approved board profile, protected target store, or physical recovery result
exists.

## Host lifecycle evidence

`UpdateBootGuard` is deliberately unable to verify signatures, write flash,
select boot slots, persist state, or execute rollback. It accepts only adapter-
supplied verification/write/boot evidence and enforces the lifecycle around it.
Eight deterministic groups cover policy/candidate refusal, staging cancellation,
complete-write evidence, stable-time and health confirmation, the exact
confirmation deadline, trial-boot exhaustion, boot mismatch plus exact rollback
completion, explicit health failure, duplicate boot sessions, and monotonic-
clock regression.

The separate [firmware-bundle admission policy](FIRMWARE_BUNDLE_ADMISSION_V0.md)
now defines the pure fail-closed evidence gate for a signed candidate before
board inspection. It has no container parser or cryptographic adapter and does
not authorize a write.

The fixed 64-byte [`OTU0/v0` checkpoint](UPDATE_STATE_CHECKPOINT_V0.md)
captures only reboot-relevant lifecycle facts and a caller-owned generation.
Eight additional groups plus 100 focused repeats cover canonical encoding, corruption, exact policy
binding, trial/rollback restoration, atomic failure, and deliberate removal of
boot-local health/time/session evidence. CRC is accidental-corruption evidence,
not authentication or rollback protection.

The abstract [two-slot store](UPDATE_CHECKPOINT_STORE_V0.md) then owns normal
generation allocation, preserves the newest unique valid record across partial
or corrupt writes, readback-verifies every successful save, repairs a known
invalid peer, and fails closed when an unreadable slot could conceal newer
state. Its caller-supplied [trusted-generation
contract](UPDATE_TRUSTED_GENERATION_FLOOR_V0.md) also rejects missing or stale
checkpoint generations before live restore and allocates beyond both local and
trusted state. Twenty store groups plus 100 focused repeats pass. Target
atomicity, authenticated integrity, a hardware-backed trusted source, wear, and
physical power cuts remain.

The [typed boot coordinator](UPDATE_RECOVERY_BOOT_COORDINATOR_V0.md) fixes the
next ordering layer. It starts and restores only a private guard, validates the
observed image, commits trial-count or rollback transitions, advances and
exactly reads back trust, and exposes live state only after the sequence
succeeds. Fifteen groups plus 100 focused repeats pass. This remains host state-
machine evidence; no target boot task, protected backend, or physical restart
result exists.

The [verified save coordinator](UPDATE_RECOVERY_SAVE_COORDINATOR_V0.md) applies
the corresponding normal-operation ordering. It requires exact local/trusted
generation agreement, verifies the next checkpoint before advancing trust, and
requires exact trust readback before reporting committed. Ten groups plus 100
focused repeats pass.

The [transition coordinator](UPDATE_RECOVERY_TRANSITION_COORDINATOR_V0.md) now
owns trial-time health, tick, confirmation, and explicit rollback calls on a
private guard copy. Reboot-relevant state becomes live only after the verified
save succeeds; any persistence failure stops the original guard without
publishing the attempted state. Ten groups plus 100 focused repeats pass.
Target scheduling, reboot execution, staging/install composition, terminal
cleanup, protected backends, and physical evidence remain open.

The [redacted operator-status boundary](UPDATE_RECOVERY_STATUS_V0.md) maps boot,
save, and transition results into one fixed record containing only coarse
state/reason/action, generation evidence, and recovery flags. It verifies source
coherence before mapping; unknown, incomplete, or contradictory input becomes
service-required and blocked. Eight groups plus 100 focused repeats pass. No
target logger, renderer, persistent audit, or physical operator workflow exists.

The versioned [`OTRD0` diagnostics adapter](../diagnostics/UPDATE_RECOVERY_DIAGNOSTIC_EVENT_V0.md)
then removes both generations and writes one canonical 32-bit status through the
existing logger. It validates the word independently and preserves logger
filtering/backpressure behavior. Eight groups plus 100 focused repeats pass.
Target sink binding, retained audit/export, and physical service capture remain.

The separate [firmware-install preflight](FIRMWARE_INSTALL_PREFLIGHT_V0.md)
defines the first host/loader gate. It allows inspection of a connected device
without authorizing a write, requires exact processor/memory/profile/revision/
bootloader/image agreement, and adds explicit destructive and physical-recovery
confirmation where applicable. Thirteen groups pass. Runtime model strings and
USB identities remain evidence, never exact compatibility authority. No USB
adapter, bundle-signature verifier, writer, Windows UI, approved profile, or
physical recovery claim exists.

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

The canonical host checkpoint fixes the trial record shape, confirmed
slot/version binding, boot-attempt count, policy, and generation. It still needs
authenticated, interruption-safe target storage and a separately protected
trusted generation source. The host store can enforce a supplied floor but does
not protect, advance, or reset that source. CRC alone is insufficient against
deliberate rollback or forged newer state.

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
