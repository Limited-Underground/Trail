# Multi-Domain Persistent Storage Key/Value Target Adapter v0

Status: backend-neutral target boundary under host validation, 2026-08-12. No
ESP-IDF backend, partition/security configuration, protected secret storage,
physical interruption, endurance, or on-device configuration save is claimed.

This adapter maps the existing flash-like `PersistentStorage` interface onto
isolated key/value namespaces while preserving its two-slot, erase-before-write,
partial-program, and explicit-sync behavior.

## Fixed domain binding

All domains use partition `ot_state`, keys `slot_a` and `slot_b`, and exact
64-byte values. Namespaces are distinct:

| `StorageDomain` | Namespace |
| --- | --- |
| configuration | `ot_config` |
| secret material | `ot_secret` |
| protocol state | `ot_proto` |
| outbound-counter state | `ot_counter` |

Every identifier fits the ESP-IDF NVS 15-character limit. This naming prevents
accidental key collision; it does not make any namespace confidential,
authenticated, access-controlled, or rollback-resistant.

## Flash-like behavior over blobs

- A missing key reads as one exact 64-byte erased slot filled with `0xFF`.
- `erase_slot` erases and commits a present key, or initializes missing state
  without a redundant commit.
- Partial writes are accepted only after that adapter instance has completed an
  erase for the exact domain and slot.
- A write may only clear bits. Any attempted zero-to-one change requires a new
  erase.
- Partial changes remain in a 64-byte RAM working image until `sync_slot` writes
  and commits the complete blob.
- Any failed or uncertain write/commit latches that working slot closed. A new
  erase or a fresh adapter instance is required before more mutation.
- Reads always observe the backend's durable value, never unsynchronized RAM.

The upper configuration store can therefore retain its body-sync, commit-marker,
second-sync ordering without depending on native NVS partial writes. The real
outbound counter lease store also composes through the counter namespace; its
[separate proof](../security/OUTBOUND_COUNTER_KV_COMPOSITION_V0.md) covers
restart rotation and both unapplied and applied-then-failed commits without
returning an uncertain counter range.

## Remaining target obligations

Twelve deterministic groups cover fixed domain names, public argument
rejection, missing/present/wrong-sized reads, erase/partial-write/sync ordering,
idempotent missing erase, erase-before-write and bit-clearing enforcement,
fail-latched backend errors, applied-then-failed commit discovery, physical
namespace separation, real configuration rotation, and final-commit restart
recovery. A separate five-group composition suite exercises real counter lease
records through this adapter and passes 100/100 repeats. The complete
89-executable host matrix passes under strict C++17 warnings-as-errors
including publication safety.

The backend owns initialization, handles, locking, native error translation,
namespace access policy, flash encryption, and evidence that commit survives
reset and power loss. Secret material requires a separately approved protected
storage design; this adapter name is not authorization to store production keys
in ordinary NVS. Authenticated integrity, trusted rollback, reset/migration,
wear, latency, and physical recovery remain open.
