# Duplicate Checkpoint Codec v0

Status: deterministic host-tested serialization boundary, 2026-08-09. This is
not a durable-store binding, authenticated record, secure anti-rollback
mechanism, or physical power-loss result.

## Purpose and boundary

The `OTD0` record serializes the existing duplicate-window checkpoint without
persisting pre-reboot monotonic timestamps. Each active entry carries its
remaining lifetime, allowing restore to apply that duration to the new boot's
monotonic clock.

The record may contain opaque network aliases, group epochs, and message IDs.
It contains no raw key, PIN, transport address, location, or free text. A
storage adapter must still define privacy, retention, wear, atomic commit,
rollback, and erase behavior.

## Fixed 672-byte record

All integers are little-endian.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII magic `OTD0` |
| 4 | 1 | envelope version, currently 0 |
| 5 | 1 | duplicate-checkpoint version, currently 1 |
| 6 | 2 | header length, exactly 16 |
| 8 | 1 | active-entry count, 0 through 32 |
| 9 | 3 | reserved zero |
| 12 | 2 | entry length, exactly 20 |
| 14 | 2 | entry capacity, exactly 32 |
| 16 | 640 | 32 fixed entry slots |
| 656 | 12 | reserved zero |
| 668 | 4 | CRC-32/ISO-HDLC over bytes 0 through 667 |

Each 20-byte active entry contains an 8-byte source alias, 4-byte group epoch,
4-byte message ID, and 4-byte nonzero remaining lifetime in milliseconds.
Unused entry slots are all zero.

## Canonical and failure behavior

Encoding and decoding reject unsupported versions, more than 32 entries,
zero-valued key fields, zero remaining lifetime, and duplicate keys. Decode
also rejects nonzero reserved bytes, nonzero unused entries, wrong fixed
lengths, bad magic, and CRC mismatch. Caller output changes only after the
entire record validates.

CRC-32 detects accidental corruption only. It does not authenticate the
record. Restoring an older valid record can extend or reintroduce a replay
window, so a future durable store must bind this payload to an atomic,
rollback-aware envelope before it is used as a security boundary.

## Host evidence

`tests/host/duplicate_checkpoint_codec_tests.cpp` covers seven groups:

1. round-trip bytes and explicit offsets;
2. canonical empty checkpoints;
3. invalid state, duplicate keys, lifetime, and capacity;
4. argument, magic, version, and atomic-output failures;
5. reserved and unused-byte canonicality;
6. CRC and repaired-CRC semantic tampering; and
7. serialized remaining-lifetime restoration through `DuplicateWindow`.

The full 23-executable OpenTrail host matrix passes. The codec and existing
duplicate-window suites each pass 100 consecutive repeat runs.
