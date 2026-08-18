# Decision 0027: Reject ESP32-S3 SECURE_VERSION as the authorization floor

- Status: accepted
- Date: 2026-08-18
- Increment: OT-084

## Decision

ESP32-S3 `SECURE_VERSION` is rejected as OpenTrail's independent companion-
authorization generation floor. No rollback-floor provider or descriptor is
selected.

External authenticated monotonic hardware remains a technically plausible
class only for a future hardware revision. No part is selected, present, or
claimed compatible with the current Heltec target.

## Evidence

Pinned ESP-IDF 6.0.2 sources establish that `SECURE_VERSION` is EFUSE_BLK0
bits 142 through 157, uses no Reed-Solomon coding, and represents its value as
the population count of irreversibly burned bits. It can advance at most 16
times.

The same field is ESP-IDF's application-firmware anti-rollback version. It is
carried in the application image descriptor and consumed by bootloader
selection, OTA validation, and OTA confirmation. ESP-IDF's
native anti-rollback partition model requires `ota_0` and `ota_1` without a
factory or test application. OpenTrail's accepted current layout and OT-077
recovery route retain and restore a factory application.

Reusing the field for companion authorization would therefore share a scarce
firmware version budget, create two meanings for one irreversible field, and
couple authorization recovery to a different partition, firmware-authenticity,
and update architecture. That is not the independent floor required by the
authorization record contract.

The exact source paths, lengths, and SHA-256 values are frozen in
`protected-root-secure-version-floor-plan.json`. A pure evaluator accepts only
the exact reviewed incompatibility and emits
`REVIEWED-SECURE-VERSION-COUPLED-NOT-ADMITTED`; drift, selection, authority,
or disclosure claims deny.

## Consequences

- The custom USER_DATA and `SECURE_VERSION` on-chip branches are both closed.
- The current Heltec target has no accepted independent rollback-floor
  provider.
- No inventory compositor, floor reader, provisioning path, or runtime edge is
  added.
- No device, eFuse, partition, application, configuration, or runtime state is
  read or changed.
- External monotonic hardware requires an owner-approved hardware revision and
  a new authenticated-binding, transaction, power-loss, replacement, recovery,
  and provisioning design before any part can be selected.

The next gate is a product-direction decision: approve that hardware work,
defer rollback-protected authorization beyond the current V1 scope, or
explicitly revise the rollback threat requirement.
