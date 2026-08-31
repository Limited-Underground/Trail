# Decision 0102: accept the OT-166 Heltec V1 bond-owner integration

- **Status:** Accepted
- **Date:** 2026-08-31
- **Scope:** Build-linked device-side durable ownership for initial V1 BLE pairing

## Context

OT-164 physically accepted the local six-digit pairing window and OT-165
host-validates Android's OS-owned initial bonding flow. The target still needed
a durable, privacy-preserving way to bind exactly one NimBLE bond to the one
V1 owner and to admit the protected GATT controller after reboot.

## Decision

1. Use a dedicated ordinary NVS partition for the initial V1 owner. This path
   is distinct from and does not activate persistent rollback-floor `OAP0`
   authority.
2. Persist one fixed 32-byte `OTV1/v0` record containing state, generation, an
   opaque 128-bit private bond reference, reserved zeroes, and CRC. Commit is
   absence-only and successful only after byte-exact readback.
3. Enable NimBLE Secure Connections bond persistence. Derive the private
   reference through domain-separated SHA-256 over the authenticated 16-byte SC
   LTK; addresses and raw keys never leave the target adapter and are not logged.
4. Before the NimBLE host starts, accept only exact empty record/empty inventory
   or one valid owner record/one matching bond. Recheck the exact inventory
   after initial commit and before every controller authorization.
5. Keep normal GATT commands closed until the exact encrypted, authenticated,
   bonded live connection completes the authorization indication and is
   promoted. Pin the boot challenge, require strictly increasing boot-local
   session challenges, and accept only the exact active duplicate.
6. Fail closed on storage ambiguity, inventory drift, extra bonds, invalid
   record, owner mismatch, replay, stale connection, or callback reentry. Do not
   erase, repair, replace, or clean orphaned bonds automatically.
7. Keep bridge calls externally serialized on the NimBLE host context.
   Replacement and old-bond removal remain OT-167 work.

## Consequences

The device firmware now contains a coherent initial one-owner persistence and
protected-GATT promotion path that matches the Android system-bond coordinator.
This is host-test and target-build evidence only. Physical pairing, power-cut
persistence, reconnect, replacement cleanup, and two-phone end-to-end operation
remain unproved. V1 remains exact 43.75% and displayed 44%.

## Evidence

- [OT-166 evidence](../../tests/hardware/OT-166-2026-08-31.md)
- Focused owner bridge: 9 scenario groups passed
- Focused runtime owner: 16 scenario groups passed
- Focused target admission: 15 groups passed
- Deterministic boot self-check: 100/100 passed
- ESP-IDF v6.0.2 target build: 539,232-byte application, 90% of smallest app
  partition free
- Complete Windows Host validation matrix: passed with exit code 0
- Historical/live target dependency boundary: 6/6 passed; raw-byte checkout
  audit: 291/291 passed
