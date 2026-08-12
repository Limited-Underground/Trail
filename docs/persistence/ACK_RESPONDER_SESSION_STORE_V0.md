# ACK Responder Session Store v0

Status: deterministic host-tested commit-last boot-session allocation,
2026-08-09. This is not an ESP-IDF/NVS binding, trusted monotonic counter,
encrypted record, secure anti-rollback mechanism, or physical brownout result.

## Purpose and boundary

`AckResponderSessionStore` allocates one nonzero OpenTrail consumer boot-session
ID durably before that ID is returned to `CriticalAlertAckResponder`. A restart
therefore uses a new session instead of silently resetting ACK sequence inside
an old session. OpenGauge still requires an explicit binding for the new
session.

The record contains no raw key, PIN, transport address, vehicle data, location,
or free text. It binds only consumer ID, opaque authorization epoch, boot
session, and storage generation. Identity or epoch change fails until an
explicit reset and freshly chosen nonzero seed.

## Fixed 64-byte record

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII magic `OTAS` |
| 4 | 1 | envelope version, currently 1 |
| 5 | 1 | schema version, currently 1 |
| 6 | 1 | reserved zero |
| 7 | 1 | header length, exactly 16 |
| 8 | 4 | nonzero generation |
| 12 | 2 | payload length, exactly 16 |
| 14 | 2 | reserved zero |
| 16 | 8 | nonzero consumer ID |
| 24 | 4 | nonzero authorization epoch |
| 28 | 4 | nonzero last allocated boot-session ID |
| 32 | 24 | reserved zero |
| 56 | 4 | CRC-32/ISO-HDLC over bytes 0 through 55 |
| 60 | 4 | commit-last marker `0xA6175E55` |

The record uses the dedicated `protocol_state` storage domain. Configuration
and secret-material domains are not read or modified.

## Allocation and recovery

With two blank slots, the caller-supplied first-session seed is committed as
generation 1. Later allocations select the unique highest valid generation,
require exact consumer/authorization identity, increment session and generation,
and write the other slot. Generation/session exhaustion fails closed.

Writing erases the target, writes bytes 0-59, syncs, writes the commit marker,
syncs again, then reads and compares every byte and decoded field. Nothing is
returned before verification succeeds.

An empty/failed write before a durable body may safely retry the same ID because
it was never returned. A durable uncommitted body is treated as uncertain and
requires explicit recovery instead of risking reuse. If the commit marker is
visible but final sync reports failure, the next allocation treats that session
as consumed and skips forward. Corruption, malformed/future versions, any
invalid peer slot, and different records with the same generation all fail
closed rather than falling back to a possibly reused session.

Two-slot generations prevent accidental rollback through the normal API, not a
deliberate rollback of both backend slots. Secure anti-rollback requires a
trusted monotonic primitive or authenticated protected storage.

## Host evidence

`tests/host/ack_responder_session_store_tests.cpp` covers ten groups:

1. explicit first record, domain isolation, and responder composition;
2. boot-session/generation increments and slot alternation;
3. identity/epoch mismatch plus explicit reset/reseed;
4. corrupt newer state failing closed without reuse;
5. equal-generation conflict and future version rejection;
6. generation and session exhaustion;
7. invalid requests/read failure without writes;
8. every erase/body-sync/commit/final-sync power-loss boundary;
9. uncommitted and malformed records requiring recovery; and
10. reset attempting both slots even when storage fails.

The full OpenTrail matrix passes. This suite and the affected responder and
configuration suites each repeated 100 times with zero failures. A separate
[key/value composition](ACK_RESPONDER_SESSION_KV_COMPOSITION_V0.md) runs the
real allocator through `PersistentStorageKv` and proves exact `ot_proto`
binding, restart rotation, failed-unapplied retry, applied-then-failed session
skipping, durable two-key reset/reseed, and wrong-sized-value refusal across
six groups and 100/100 repeats.

## Remaining gates

- bind `protocol_state` to exact ESP-IDF/NVS namespaces, ownership, erase,
  alignment, sync, and commit behavior;
- define initial session entropy and explicit reset/reseed operator UX;
- persist or reserve per-session ACK sequence ranges if same-session restart is
  ever permitted;
- coordinate authorization epoch/rebind with the physical transport; and
- validate real brownout, corrupt sector, endurance, rollback, and recovery.
