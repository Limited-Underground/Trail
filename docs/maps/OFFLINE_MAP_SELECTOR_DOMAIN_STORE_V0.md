# Offline Map Selector Trust-Domain Store v0

Status: deterministic host-tested abstract two-slot store, 2026-08-11. No
physical backend, protected rollback anchor, or on-device persistence exists.

This store gives the canonical 80-byte
[`OTMD/v0` lifecycle record](OFFLINE_MAP_SELECTOR_DOMAIN_RECORD_V0.md) a
recoverable persistence boundary without changing or sharing the separate
64-byte `OTM0/v0` selector store. Its injected storage interface can read and
write two exact 80-byte slots and commit only byte 75. It deliberately exposes
no erase or reset operation.

## Selection and generation rules

Every inspection reads both slots before selecting anything:

- two valid unequal generations select the newer record;
- two byte-identical valid records at the same generation are acceptable;
- two different valid records at the same generation are a conflict;
- one valid record plus an empty, invalid, or uncommitted peer selects the
  valid record and reports recovery required;
- an unreadable peer fails closed even if the other slot is valid, because it
  could conceal a newer committed generation; conflict and I/O results expose
  no selected domain record; and
- invalid/uncommitted-only media is never reinterpreted as first use.

Empty readable media accepts only record generation 1 in fresh-device
`pending_first_baseline` state. Existing media accepts only the exact next
record generation. A stale, skipped, zero, wrapped, or exhausted generation is
rejected before any write. The caller supplies the exact successor record, but
the store re-inspects both slots and proves that generation is immediately
next; this is a stale-writer check, not a lock or anti-rollback primitive.

## Allowed lifecycle successors

After canonical record validation, an existing record may advance only through
one of these forms:

1. an exact lifecycle maintenance rewrite with only record generation changed,
   used to restore a known degraded peer;
2. pending first baseline or pending replacement reseed to active with the
   exact same domain, retirement, floor, origin, and epoch binding;
3. active to active with the same binding and a strictly greater accepted
   selector generation; or
4. active to pending replacement with a new current domain, the prior current
   domain named as retired, epoch advanced by exactly one, accepted generation
   cleared, and a retired selector floor no lower than the prior accepted
   generation.

Replacement also rejects immediate reuse of the current record's retired
domain. The two-record format cannot prove lifetime domain uniqueness, entropy,
or physical-device continuity. The permit-consuming provisioner requires those
values as exact authorized inputs, while proof and generation remain
obligations of protected target composition.

Backward state, binding mutation inside one domain, skipped epochs, domain
reuse, a lowered quarantine floor, or commissioning over existing records is
rejected without storage access.

## Commit-last protocol

Saving one accepted successor uses this order:

1. inspect both slots and select the unique newest valid record;
2. validate the exact generation and lifecycle successor;
3. canonically encode the complete 80-byte record with CRC calculated for the
   committed marker `0xB6`;
4. choose an empty/degraded peer, or alternate away from the newest valid slot;
5. write the complete record with byte 75 cleared to zero;
6. commit only marker byte 75 as `0xB6`; and
7. read back, decode, and compare every byte and the exact generation.

The prior committed slot is not overwritten while its successor is prepared.
A prepared-write failure is known not committed. A commit-call failure is
reported as uncertain because the marker may have reached media. Exact boot
inspection then selects whichever committed generation actually exists; the
store never guesses from the call result.

CRC detects accidental corruption only. It is not authentication, encryption,
protected continuity, or rollback resistance.

## Authority and data limits

This is a persistence mechanism, not a provisioning authority. Calling
`save()` does not prove that a protected-domain permit was issued, device
continuity was established, selector media was reviewed, a domain was securely
generated, or protected history was reset. The separate
[provisioner](OFFLINE_MAP_SELECTOR_DOMAIN_PROVISIONER_V0.md) is the only common
component that assembles authorized domain-changing records, and its caller
must serialize access across domain, protected-generation, and selector
operations.

The store contains only typed fixed-size records and coarse result enums. It
has no device or participant identifier, credential, key, path, package name,
geographic content, URL, timestamp, free text, radio, network, map rendering,
selector erase, protected reset, or factory-reset authority. Domain values must
remain absent from public logs and UI.

## Current evidence and limits

Ten deterministic host groups cover empty generation-1 commissioning,
pending-to-active and monotonic selector acceptance, exact maintenance
rotation, linked multi-epoch replacement, stale/backward/binding/floor/domain-
reuse rejection, twelve interrupted prepared-write boundaries, commit-before-
error and commit-after-error outcomes, corrupt readback, degraded-peer repair,
unreadable and invalid-only media, equal-generation conflict and identical
copies, codec rejection, and generation exhaustion.

The focused suite passes 100/100 repeats under strict C++17 warnings-as-errors.
All twenty-two map suites pass 100/100 repeats, and the complete 80-executable
host matrix passes including publication-safety checks.

No key/value or ESP-IDF backend, partition/namespace binding, authenticated
integrity, protected rollback anchor, physical atomicity or power-cut evidence,
wear/endurance result, target lock/task, transactional rollback across stores,
or on-device behavior is claimed. The provisioner and stable activation
coordinator supply fixed recoverable ordering rather than a cross-store atomic
transaction.
