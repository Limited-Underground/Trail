# Decision 0103: Adopt OT-168 V1 factory reset and boot pairing

- **Status:** Accepted design; partial host implementation evidence; physical acceptance pending
- **Date:** 2026-09-02
- **Work item:** OT-168

## Decision

OpenTrail adopts the [Device Factory Reset Contract v1](../platform/DEVICE_FACTORY_RESET_V1.md)
as the current V1 phone-recovery and whole-device reset authority.

1. V1 has no phone-replacement or lost-phone ownership-transfer flow. An owned
   device accepts only its saved authorized phone and remains PIN-free on an
   ordinary boot.
2. A verified unowned boot automatically opens exactly one 60-second pairing
   window with one fresh locally displayed six-decimal-digit PIN. Expiry closes
   pairing and conceals the PIN. A later unowned boot generates a new PIN and
   opens one new window.
3. First-time Add Device discovery accepts only the `D1` pairable marker during
   that window. A returning authorized phone instead discovers the protected
   normal `D0` service among Android's currently bonded devices, must resolve
   exactly one observed candidate, never calls `createBond` or uses a PIN, and
   must pass protected normal `ProtocolInfo` plus current device-owner
   authorization before `Ready`. An owned ordinary boot exposes no `D1` or PIN.
4. The currently authorized phone may request factory reset only through an
   encrypted, authenticated, application-authorized session and an explicit
   destructive confirmation in the app. Once accepted by the device, that path
   requires no button press or other Heltec confirmation.
5. When the authorized phone is unavailable, physical factory reset requires a
   continuous 10-second hold, the LCD warning `Erase all Trail data?`, release,
   and one short press within the following 10 seconds.
6. Both reset paths converge on the same complete user-data wipe. That includes
   phone ownership and application authorization, every BLE bond, editable
   configuration, device/group cryptographic state, contacts, messages, queues,
   acknowledgements, replay state, saved location data, and user-selected or
   transferred maps plus their selectors, indexes, history, and recovery
   metadata.
7. Cancellation or power loss before the durable reset commit preserves the
   prior state. After verified intent, uncertainty fails closed; boot resumes
   cleanup until every required user domain and BLE bond is verified absent.
   Only verified completion may reboot unowned and open the new 60-second
   pairing window.

Installed firmware and bootloader state, immutable hardware identity/profile,
and non-user board calibration remain. The reset does not downgrade firmware,
alter eFuses, or claim forensic erasure or protection against invasive access
or restoration of an older flash image.

## Supersession boundary

This decision supersedes only the current-product behavior that previously
allowed an owned device to open a replacement window, the 3-second/30-second
manual opening rule as current V1 pairing UX, and the policy that ordinary
factory reset preserves user map state. Historical OT-090, OT-164, OT-165, and
OT-166 decisions, tests, receipts, and observed results remain immutable
evidence of what those increments actually proved.

In particular:

- Decision 0033 remains the V1/V1.5 topology and security-scope authority except
  for its replaced pairing/replacement clauses.
- Decision 0034 and `OTBP0/v0` remain historical host-test evidence, not current
  replacement authority.
- Decision 0005 and its map reset/replacement v0 contract remain historical
  host-test evidence; whole-device V1 factory reset now erases user map state.
- OT-167 remains a separate in-progress implementation investigation. Its
  earlier replacement-oriented wording does not authorize a V1 replacement
  flow.

## Acceptance boundary

This decision accepts the product contract only. It does not claim that the
factory-reset executor, Heltec storage/gesture integration, Android command or
confirmation flow, D1 enrollment, D0 returning-owner reconnect, boot pairing
behavior, power-interruption recovery, either physical device, or the two-phone
V1 path has passed. Those remain OT-168 implementation, validation, and
physical-acceptance work.

## Implementation-evidence addendum (2026-09-02)

This addendum records the current implementation boundary without changing the
accepted decision or any historical observation above.

- App reset uses a random nonzero u64 receipt in the existing `OTA0/v0`
  subject field, encoded little-endian and echoed exactly by `OTR0/v0`.
  `ADMITTED` means durable intent acceptance only, not completed erasure.
- After verified cleanup, only the next unowned D1 pairing window exposes
  the exact `OTRR` + version `0x01` + receipt service-data payload. The receipt
  correlates reset verification only; it is not identity or authorization.
  Unknown outcomes retain the bounded receipt record and verify without
  resubmitting reset.
- Android persists only the receipt plus one coherent bounded issued/expiry
  window of at most 120 seconds, never a MAC, endpoint, private pairing
  identifier, device/user identity, or PIN. Clock rollback, incoherent bounds,
  or expiry clear it fail closed. A stale Android bond is handled through Bluetooth settings,
  not hidden `removeBond` access.
- The Heltec implementation stores reset intent/state/receipt as one versioned
  16-byte `OTFR` blob at `ot_reset_v1/record_v1`. Whole-record
  `intent_committed` to `receipt_pending` transitions, exact readback, and
  erase/commit/read-absent consumption ensure the receipt is published from
  RAM only after the pending record is durably absent. Transient consumption
  uncertainty reboots into fresh reconciliation; it does not open normal BLE.
- Final focused firmware evidence is green: reset storage passes, Heltec target
  admission passes 15/15, companion semantics passes 14/14, the reset executor
  passes 17 scenario groups, the physical gesture passes 9 groups, and reset
  authority passes 7 groups. The pinned ESP-IDF v6.0.2 application is 549,184
  bytes with SHA-256
  `90FE9479653F16000D71BC7AA76EA3ECB41F6FF0B4CF7A263E6056E054AC0454`
  and 89% app-slot free.
- The post-audit Android foundation gate is green: `BUILD SUCCESSFUL` in 57
  seconds with 137 actionable tasks, and 347 protocol/debug/release unit tests
  report zero failures, errors, or skipped tests. Debug/release lint and
  assembly, instrumentation assembly, and unsigned-release inspection pass.
  The final 8,508,592-byte unsigned release APK has SHA-256
  `56897297b7512ef4a3fdc35a256d3a2e59fddd7b78b9efa2fc8ee69f1e15e1d3`.
- The complete Host matrix is not claimed by this addendum and remains pending.

OT-168 remains partial. Power-interruption cases, both-device physical
acceptance, and coherent two-phone evidence remain open. Android remains 60%,
and V1 remains exact 43.75%/displayed 44%. Website work is owner-deferred until
the two-device/two-phone milestone and is not required for this checkpoint.
