# Outbound Counter Lease Store v0

Status: host-tested algorithm-neutral persistence component, 2026-08-10

## Purpose

Authenticated encryption must never reuse a nonce under the same key. OpenTrail
does not yet have packet-v1 cryptography, but it now has a fixed-memory counter
allocator that can satisfy the persistence side of that rule without importing
or pretending to implement cryptography.

The store durably advances a high-water counter before returning a range. A
running allocator serves counters only from that committed range. After a
restart, it commits the next range before returning a counter, deliberately
wasting any unused portion of the previous range rather than risking reuse.

This component is not a nonce construction by itself. The eventual packet-v1
suite must define how the 64-bit counter combines with a fixed nonce domain and
sender-specific traffic key.

## Public contract

`OutboundCounterLeaseStore::reserve()` accepts:

- one nonzero 128-bit counter-domain identifier;
- one nonzero group/key epoch; and
- a lease size from 1 through 65,536 counters.

The domain identifier is not a short routing alias and is not secret. Its exact
derivation remains part of packet v1; it must bind the authoritative sender/key
purpose strongly enough that a new traffic key cannot accidentally reuse an old
domain.

On an empty store, reserving size `N` commits high-water `N` and returns
`[1, N]`. Each later reservation commits `previous + N` and returns the exact
nonoverlapping next range. Counter zero is never returned.

`OutboundCounterAllocator` reserves once during `start()`, serves the in-memory
range in order, and reserves another range before returning its first counter.
It cannot be restarted with a different request inside one process.

## Fixed record

Each `OTCN` slot is exactly 64 bytes and explicitly little-endian:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII `OTCN` |
| 4 | 1 | envelope version `1` |
| 5 | 1 | schema version `1` |
| 6 | 1 | reserved zero |
| 7 | 1 | header size `16` |
| 8 | 4 | nonzero generation |
| 12 | 2 | payload size `28` |
| 14 | 2 | reserved zero |
| 16 | 16 | nonzero counter-domain ID |
| 32 | 4 | nonzero group/key epoch |
| 36 | 8 | nonzero reserved-through high-water counter |
| 44 | 12 | reserved zero |
| 56 | 4 | CRC-32 over bytes 0 through 55 |
| 60 | 4 | commit marker written last |

The store owns two alternating slots in the separate
`outbound_counter_state` persistence domain. It erases the target, writes and
syncs bytes 0-59, writes and syncs the commit marker, then reads back the exact
bytes and decoded fields before returning a lease. It never writes into the
configuration, secret-material, or ACK protocol-state domains.

CRC and the commit marker detect accidental corruption/interruption; they do
not authenticate hostile storage. Production target storage still requires the
protected/integrity and rollback design from OT-005.

## Fail-closed behavior

No range is returned for:

- invalid or oversized requests;
- unreadable, uncommitted, malformed, corrupt, or future-version slots;
- two different records claiming one generation;
- counter-domain or group-epoch mismatch;
- generation or 64-bit counter exhaustion;
- erase/write/sync failure; or
- exact readback/decoding mismatch.

The store has no ordinary reset API. Erasing it while retaining the same traffic
key/domain could reuse counters. Factory reset or rekey must first make the old
key unusable and assign a new domain under a separately authorized target
procedure.

## Host evidence

Ten deterministic scenario groups cover:

1. first commit-last reservation and storage-domain isolation;
2. alternating nonoverlapping ranges and variable lease size;
3. allocator start, ordered use, and pre-reserved extension;
4. restart with unused-range discard and no reuse;
5. domain/epoch mismatch without writes;
6. corruption, equal-generation conflict, and future version;
7. invalid requests plus generation/counter exhaustion;
8. read failure without writes or a returned range;
9. all five persistence mutation boundaries, including committed-but-unsynced
   reconciliation by skipping the uncertain range; and
10. uncommitted/malformed record refusal.

The matrix uses the existing fixed 64-byte two-slot fake with strict erase-
before-write behavior. It is host state evidence, not ESP-IDF NVS, physical
power-cut/endurance, secure rollback, AEAD, radio, or packet-v1 evidence.
