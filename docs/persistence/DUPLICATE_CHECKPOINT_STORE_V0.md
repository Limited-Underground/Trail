# Duplicate Checkpoint Store v0

Status: superseded unbound host format, 2026-08-09. The active implementation
uses [context-bound v1](DUPLICATE_CHECKPOINT_STORE_V1.md). This document remains
as the exact legacy layout recognized for typed service refusal; it is not an
authorized migration source.

The original format was a deterministic host-tested two-slot boundary. This
is not an ESP-IDF/NVS binding, authenticated record, trusted monotonic counter,
secure anti-rollback mechanism, or physical power-loss result.

## Purpose and boundary

`DuplicateCheckpointStore` wraps the canonical 672-byte `OTD0` replay-window
checkpoint in a generated, checksummed `ODS0` record. Two independently named
slots allow a failed or partial write to leave the previously verified record
available.

The storage interface reads, writes, or erases one whole fixed-size slot. A
target adapter must define the actual NVS/filesystem partition, atomicity,
sync, wear, privacy, retention, and erase guarantees. No ESP32 adapter exists
yet.

## Fixed 704-byte record

All integers are little-endian.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII magic `ODS0` |
| 4 | 1 | stored-checkpoint version, currently 0 |
| 5 | 1 | header length, exactly 24 |
| 6 | 2 | checkpoint length, exactly 672 |
| 8 | 8 | nonzero storage generation |
| 16 | 8 | reserved zero |
| 24 | 672 | complete canonical `OTD0` checkpoint |
| 696 | 4 | reserved zero |
| 700 | 4 | outer CRC-32/ISO-HDLC over bytes 0 through 699 |

The inner `OTD0` CRC and semantic validation remain mandatory even when the
outer record validates. Both CRCs detect accidental corruption only.

## Save, restore, and recovery behavior

- A fresh save writes generation 1. Later saves increment the highest valid
  generation and alternate or repair slots.
- Every successful write is read back, byte-compared, decoded, and compared to
  the intended generation and inner checkpoint before success is returned.
- Restore selects the unique highest valid generation and applies remaining
  lifetimes to the caller's current monotonic time.
- A single valid slot may restore, but `recovery_required` stays visible when
  its peer is empty or invalid. A later save repairs an invalid peer.
- Any slot I/O failure prevents restore because it could conceal a newer
  record. Equal-generation records with different bytes also fail closed.
- Invalid state with no valid peer, generation exhaustion, and repaired-CRC
  inner semantic corruption do not mutate the caller's duplicate window.
- Reset attempts to erase both slots even if one erase fails.

Restoring an older surviving record can forget messages accepted after that
record was saved. The visible recovery flag allows the application to choose a
conservative degraded mode instead of presenting normal replay protection.

## Security and durability limits

Two slots and generations recover ordinary interrupted writes; they do not
prevent deliberate rollback of both valid blobs. Secure anti-rollback requires
a trusted monotonic primitive or authenticated protected storage. The record
contains opaque aliases, group epochs, and message IDs, so target retention and
erase policy must treat it as protocol metadata even though it contains no raw
key, PIN, location, or free text.

## Host evidence

`tests/host/duplicate_checkpoint_store_tests.cpp` covers nine groups:

1. blank restore plus first save and remaining-lifetime round trip;
2. generation increment, slot rotation, and newest selection;
3. partial-write recovery and peer repair;
4. corrupt-newer recovery with degradation visible;
5. inner semantic tamper rejection after both CRCs are repaired;
6. read and readback-verification failures without state mutation;
7. equal-generation conflict without restore or write;
8. invalid-only state and generation exhaustion; and
9. two-slot reset behavior and fresh generation after reset.

The full 24-executable OpenTrail host matrix passes. Store, codec, and duplicate
window suites each pass 100 consecutive repeat runs.
