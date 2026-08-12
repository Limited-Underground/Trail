# Outbound Counter Key/Value Composition v0

Status: deterministic host-tested composition evidence, 2026-08-12. No
ESP-IDF backend, protected storage, authenticated integrity, rollback anchor,
physical interruption, endurance, packet-v1, or on-device result is claimed.

This evidence composes the existing rollback-safe [`OTCN` lease
store](OUTBOUND_COUNTER_LEASE_V0.md) with the isolated
[`PersistentStorageKv`](../persistence/PERSISTENT_STORAGE_KV_TARGET_ADAPTER_V0.md)
adapter. It verifies the actual erase, prepared-write, marker-write, full-blob
sync, backend commit, exact readback, restart, and slot-rotation path rather
than testing either component through its simpler fake alone.

## Fixed binding

The composition reaches only:

- partition `ot_state`;
- namespace `ot_counter`;
- keys `slot_a` and `slot_b`; and
- exact 64-byte values.

The configuration, secret-material, and protocol-state namespaces are never
opened by the counter lease path.

## Restart and uncertain commits

The lease store returns a range only after its complete committed record is
read back through the key/value adapter. A fresh adapter instance represents a
restart and observes only backend-durable blobs.

Five deterministic groups prove:

1. the first range commits through the exact counter namespace;
2. successive restarts discard unused counters and rotate the two slots;
3. a failed prepared-record commit that was not applied returns no range and
   permits the same next range only after restart confirms it was absent;
4. an applied marker commit that reported failure returns no range, is
   discovered after restart, and forces the following reservation to skip the
   uncertain range; and
5. a wrong-sized backend value fails closed without any erase, write, or
   commit attempt.

The focused suite passes 100/100 repeats and the complete 89-executable host
matrix passes under strict C++17 warnings-as-errors including publication
safety.

## Remaining target obligations

The target still owns ESP-IDF initialization, exclusive locking, native error
translation, partition and namespace policy, authenticated integrity,
hardware-backed rollback resistance, coordinated rekey/reset authority,
latency and wear evidence, and physical reset/power-loss testing. The
`ot_counter` namespace is structural isolation only and is not approved for
production cryptographic counters until those protections are designed and
validated.
