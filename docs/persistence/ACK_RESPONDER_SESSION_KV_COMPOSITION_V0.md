# ACK Responder Session Key/Value Composition v0

Status: deterministic host-tested composition evidence, 2026-08-12. No
ESP-IDF backend, trusted anti-rollback, authenticated storage, physical
interruption, endurance, rendered reset workflow, or on-device result is
claimed.

This evidence composes the existing commit-last [`OTAS` boot-session
allocator](ACK_RESPONDER_SESSION_STORE_V0.md) with the isolated
[`PersistentStorageKv`](PERSISTENT_STORAGE_KV_TARGET_ADAPTER_V0.md) adapter. It
exercises the real erase, prepared-write, marker-write, full-blob sync, backend
commit, exact readback, restart, slot-rotation, and explicit reset paths.

## Fixed binding

The composition reaches only:

- partition `ot_state`;
- namespace `ot_proto`;
- keys `slot_a` and `slot_b`; and
- exact 64-byte values.

The configuration, secret-material, and outbound-counter namespaces are never
opened by the ACK session allocator.

## Restart, uncertain commits, and reset

A boot-session ID is returned only after the complete committed `OTAS` record
is read back through the key/value adapter. A fresh adapter instance represents
a restart and observes only backend-durable blobs.

Six deterministic groups prove:

1. the first session commits through the exact protocol-state namespace;
2. successive restarts increment the session and rotate the two slots;
3. a failed prepared-record commit that was not applied returns no session and
   permits the same next session only after restart confirms it was absent;
4. an applied marker commit that reported failure returns no session, is
   discovered after restart, and forces the following allocation to skip it;
5. explicit reset durably erases both keys before a different consumer and
   authorization epoch can start again at generation 1; and
6. a wrong-sized backend value fails closed without any erase, write, or
   commit attempt.

The focused suite passes 100/100 repeats and the complete 90-executable host
matrix passes under strict C++17 warnings-as-errors including publication
safety.

## Remaining target obligations

The target still owns ESP-IDF initialization, exclusive locking, native error
translation, partition and namespace policy, authenticated integrity, trusted
rollback resistance, initial-session entropy, authorized reset/reseed UI,
coordination with OpenGauge rebind, latency and wear evidence, and physical
reset/power-loss testing. The `ot_proto` namespace is structural isolation,
not protected session storage.
