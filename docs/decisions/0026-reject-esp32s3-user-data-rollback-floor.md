# Decision 0026: Reject the ESP32-S3 USER_DATA rollback-floor candidate

- Status: accepted
- Date: 2026-08-18
- Increment: OT-083

## Decision

The conditional `ESP32S3_CUSTOM_USER_EFUSE_THERMOMETER` rollback-floor
candidate from Decision 0021 is rejected. No rollback-floor provider or exact
descriptor is selected.

The two distinct ESP32-S3 `HMAC_UP` roles accepted by Decision 0021 remain
unchanged. This decision supersedes only its rollback-floor candidate.

## Evidence

Pinned ESP-IDF 6.0.2 sources establish all of the following:

- ESP32-S3 `USER_DATA` occupies EFUSE_BLK3 bits 0 through 255;
- `MAC_CUSTOM` occupies bits 200 through 247 of the same block;
- ESP32-S3 blocks 1 through 10 use Reed-Solomon coding; and
- an already nonempty Reed-Solomon coding unit cannot be written again.

The authorization floor requires many independent one-bit advances over the
life of a unit. A block that can be written only once cannot implement those
semantics, even if a non-overlapping initial subrange were selected.

The exact source paths, byte lengths, and SHA-256 values are frozen in
`protected-root-rollback-floor-descriptor-plan.json`. A target-neutral pure
evaluator accepts only that exact reviewed evidence and returns the fixed
sanitized result `REVIEWED-NO-VIABLE-CUSTOM-THERMOMETER`. Source drift,
selection claims, authority, raw device data, or private identity/path material
deny.

## Consequences

- `IsPhysicallyAdmitted` admits no current rollback-floor provider class.
- The old thermometer observation/advance/reconciliation semantics remain as
  provider-neutral behavior for a future viable implementation.
- No inventory compositor, floor reader, provisioning path, or runtime edge is
  added.
- No device or eFuse operation is authorized or performed.
- The active target build and installed firmware are unchanged.

The next architecture gate is to decide whether to review the limited
ESP32-S3 `SECURE_VERSION` primitive despite its firmware anti-rollback role and
recovery constraints, or require external monotonic hardware. Rewritable flash
cannot serve as the independent floor.
