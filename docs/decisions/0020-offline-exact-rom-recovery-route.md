# Decision 0020: Accept an exact ROM recovery route offline

## Status

Accepted on 2026-08-18 for offline contract and fail-closed validation only.

## Decision

OpenTrail accepts `OTRR0/v0/heltec-v4-ot064-source-restore` as the one exact
pre-authorization recovery route for the current `heltec_v4_bench` transition.
The route uses the ESP32-S3 ROM serial loader with esptool 5.3.1 at 115,200
baud, `no-reset` before and after, no RAM stub, no software-issued reset, and
one separately authorized connection attempt. It restores only two exact
regions, in this order:

1. the privately retained 470,928-byte OT-064 factory application at
   `0x010000`, SHA-256
   `A7D8E672CF9169F1D1D4E86EEFF80399C47A145E7D64904C207DD5F1B23F359B`;
2. the accepted 3,072-byte `OTHP0/v0` source partition table at `0x008000`,
   SHA-256
   `84569AA2BADF3F7294042129B19D0B480784A93A550ADA3253B57BC92A0671AB`.

Writing the application is unconditional for every admitted recovery
invocation; an already matching installed digest does not create a skip branch.
Writing the application first and the source partition table last prevents the
source table from becoming the final accepted table until its matching factory
application has been restored. The future executor must close the ROM
connection before an independent readback verifies both complete ranges. A
manual reset must then produce boot self-check PASS, the Trail logo followed by
`BLE ADVERTISING`, and at least two expected USB heartbeat records within a
bounded 12-second observation. Those observations are separate physical
evidence; they are not part of offline acceptance.

The source-table recipe is public: the pinned ESP-IDF v6.0.2
`gen_esp32part.py` utility must deterministically convert the accepted
452-byte `partitions.csv` source into the exact 3,072-byte recovery binary.
The binary is admitted only after its complete digest matches the value above.
The application remains private and Git-ignored. Physical admission requires
the retained copy plus a separately staged second copy, each independently
hashed before device access; one retained copy is currently proved, but
redundant custody is not yet accepted.

The route denies a chip or 16 MiB flash-profile mismatch, missing or mismatched
artifact, wrong offset/order/range, overlap, full erase, bootloader/otadata/OTA/
state/authorization writes, any initialized authorization record, any source
region other than the complete accepted all-`0xFF` image, stale or zero
operation/evidence identity, software reset, RAM stub, key operation, or eFuse
operation. Failure never widens the allowed regions or automatically retries.

## Expected security-state admission

The future route expects secure boot, flash encryption, and secure download
mode all disabled. Before any write, one fresh read-only observation must prove
those three values and bind them to the same nonzero operation ID and evidence-
set ID used by the admitted artifacts and route. Unknown, unavailable, stale,
zero-bound, or mismatched security state denies before the first write.

## Post-first-write failure

After the first write invocation, any incomplete, failed, ambiguous, or
unverified outcome requires the connection to close while the unit remains in
ROM mode. Software reset and a boot-success claim are forbidden. The operation
becomes `RECOVERY-UNCERTAIN`; it does not fall back to the pre-write denial
state and it never retries automatically.

The exact private recovery artifacts and a minimum private journal must be
preserved before transient cleanup. That journal binds the private unit,
operation/evidence identities, last attempted restore region, admitted artifact
and security-state identities, and connection-close/failure category. Public
evidence exposes only `OTRR0/v0/RECOVERY-UNCERTAIN`. A retry requires fresh
owner authorization and a new accepted operation/evidence binding.

## Protected-key roles

Two future device-secret roles are required and must remain distinct:

- an NVS-encryption HMAC key used only by the accepted ESP-IDF HMAC security
  provider for the `ot_auth` partition; and
- a bond-binding PRF HMAC key used only to derive the opaque owner token from
  a private bond-store reference and generation.

Both roles require opaque identifiers, purpose checking, read protection, and
a private operational self-test. Neither key may serve as a firmware-release
signing key, recovery approval, operation identity, or rollback floor. No key
identifier, key bytes, eFuse block, provider, or provisioning sequence is
selected by this decision.

## Independent rollback-floor requirement

The authorization generation floor must be a monotonic, non-rollbackable
authority independent of `ot_auth`, `ot_state`, the partition table, and every
rewritable flash image. It must support exact fresh read, compare-and-advance,
post-advance reread, exhaustion handling, and uncertain-result recovery without
ever lowering the accepted generation. Ordinary NVS, redundant records, CRCs,
encrypted snapshots, and the two protected-key roles cannot satisfy it.

An ESP32-S3 irreversible hardware primitive may be evaluated later, but no
exact field, provider, capacity, endurance, replacement behavior, key/eFuse
provisioning, or rollback authority is selected or authorized here.

## Authority boundary

- This decision adds no command, port, private device identity, or executable
  writer and performs no hardware access.
- The exact route is accepted offline but has not been physically executed or
  proved on the selected unit.
- The recovery bundle remains incomplete until fresh same-operation evidence
  and exact-unit recovery validation exist. The currently unproved redundant
  private application custody is also a physical-admission blocker.
- Partition, application, authorization, state, full-erase, recovery, device,
  key, eFuse, and rollback authorities all remain false.
- Active partition/configuration/runtime/device state is unchanged.

## Next gate

Select and independently review the two concrete protected-key providers and
one concrete rollback-floor provider. Then a separate owner-authorized
operation may collect fresh unified evidence and request a bounded physical
candidate-transition/recovery rehearsal. No write is authorized by this
decision.
