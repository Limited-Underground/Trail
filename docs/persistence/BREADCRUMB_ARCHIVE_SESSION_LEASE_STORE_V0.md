# Breadcrumb Archive Session Lease Store v0

Status: **deterministic host-tested restart-safe range reservation; no ESP-IDF
backend, protected storage, rollback resistance, or on-device claim**,
2026-08-12.

## Purpose

An archive session ID must not silently repeat after a reboot. Persisting every
Start would increase flash traffic and would still leave an ambiguous result
when power fails during the write. `BreadcrumbArchiveSessionLeaseStore`
instead commits one nonoverlapping range before the boot-lived consent
controller may use its first ID. A later boot reserves the range immediately
after the last committed range; any unused IDs from the earlier range are
deliberately abandoned.

The allocation carries only ordering state. It contains no device, user,
participant, group, location, endpoint, account, credential, key, or remotely
accepted authorization identity.

## Durable record

Each record occupies one exact 64-byte storage slot:

| Offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 4 | magic `OTBL` |
| 4 | 1 | envelope version 1 |
| 5 | 1 | schema version 1 |
| 6 | 2 | reserved zero plus header length 16 |
| 8 | 4 | nonzero storage generation |
| 12 | 4 | payload length 16 plus reserved zero |
| 16 | 8 | first session ID in the reserved range |
| 24 | 8 | final session ID in the reserved range, inclusive |
| 32 | 24 | reserved zero |
| 56 | 4 | CRC-32 over bytes 0 through 55 |
| 60 | 4 | commit-last marker `0xB7EA5E55` |

Two alternating slots retain the newest valid generation. The store writes and
syncs the body first, writes and syncs the marker last, then reads back the
entire record before returning the range. A valid committed range that becomes
visible despite a reported final-sync failure is treated as consumed on the
next boot, so the uncertain IDs are skipped rather than reused.

Blank storage requires a caller-provided nonzero initial seed and lease size.
After initialization, the durable final ID—not a later caller seed—determines
the next range. Generation or ID exhaustion fails closed. Conflicting equal
generations, unsupported versions, malformed records, CRC failure, read
failure, and an uncommitted peer require explicit recovery instead of silently
choosing possibly reused state. No reset or erase API is exposed by this
component.

## Storage composition

The general key/value target adapter assigns archive lease state its own
`breadcrumb_archive_state` domain, partition `ot_state`, namespace
`ot_archive`, and keys `slot_a` and `slot_b`. It does not share configuration,
secret, protocol, or outbound-counter keys.

This separation prevents accidental namespace collision. Ordinary CRC and
commit-last ordering detect accidental corruption and incomplete writes; they
do not authenticate state, prevent hostile replacement, or detect rollback to
an older otherwise valid pair of records.

## Consent integration

The lease result supplies explicit inclusive first/final bounds to
`BreadcrumbArchiveConsentController` and the complete archive workflow. Start
can consume only IDs inside that range. When the final ID has been consumed,
the boot-lived controller refuses another Start; target composition must obtain
a new durable lease rather than wrap, reset, or reuse an ID.

The initial seed for a genuinely blank target must eventually come from an
approved cryptographically secure random source. The host tests use a fixed
value for determinism and do not establish target entropy.

## Host evidence

Nine lease-store scenario groups plus 100/100 focused repeats cover:

1. first commit, exact record/domain binding, and commit-last ordering;
2. restart range abandonment, slot rotation, changed lease size, and ignoring
   a later seed once durable state exists;
3. failed first commit returning no range and no false initialization claim;
4. corruption of either valid peer failing closed;
5. equal-generation conflict and future-version rejection;
6. generation and session-ID exhaustion;
7. invalid input and storage read failure without mutation;
8. five injected mutation/power-loss boundaries, including skipping a
   committed-but-uncertain range; and
9. uncommitted and malformed records requiring recovery.

Five key/value composition groups plus 100/100 focused repeats cover the exact
`ot_archive` binding, restart rotation/nonoverlap, failed-unapplied retry,
applied-then-failed range abandonment, and wrong-sized value rejection. The
consent suite adds an explicit inclusive-range exhaustion case.

The complete 108-executable C++ host matrix and Python publication checks pass.
This is not evidence of ESP-IDF/NVS binding, flash encryption, authenticated
integrity, anti-rollback storage, endurance, brownout behavior, secure initial
entropy, target boot composition, physical interruption, or server identity.

## Next gates

- Preserve the host-tested
  [lease-to-workflow bootstrap](../location/BREADCRUMB_ARCHIVE_WORKFLOW_BOOTSTRAP_V0.md)
  when binding one selected target, and make allocation failure leave archive
  Start unavailable while base messaging remains usable.
- Define an owner-approved recovery flow for blank, corrupt, uncommitted,
  exhausted, or rollback-suspect archive state without an automatic reset.
- Bind `ot_archive` to the reviewed ESP-IDF storage backend and inject reset,
  brownout, contention, and wear scenarios on real hardware.
- Decide whether stronger authenticated/rollback-resistant persistence is
  required once the remote archive protocol and threat model are selected.
