# Offline Map Trust-Domain Key/Value Target Adapter v0

Status: deterministic host-tested target boundary, 2026-08-12. No ESP-IDF
backend, protected rollback anchor, partition/security configuration, physical
interruption, endurance, or on-device domain persistence is claimed.

This adapter binds the non-erasable two-slot
[`OTMD/v0` trust-domain store](OFFLINE_MAP_SELECTOR_DOMAIN_STORE_V0.md) to
backend-neutral key/value operations without granting erase, reset,
provisioning, or domain-replacement authority.

## Fixed binding and commit

- partition: `ot_state`
- namespace: `ot_map_domain`
- slot keys: `otmd_a`, `otmd_b`
- value size: exactly 80 bytes
- commit byte: offset 75, marker `0xB6`

Prepared writes require the marker byte to be zero. Adapter success means the
complete prepared blob was written and committed. Marker commit rereads that
exact 80-byte prepared blob, refuses missing, wrong-sized, or already-committed
state, rewrites only byte 75 in its RAM copy, then writes and commits the full
blob. This preserves the abstract store's prepared-before-marker ordering even
when the backend cannot program one byte in place.

A failed commit remains uncertain: the upper store and boot coordinators must
inspect both slots and reconcile whichever generation is durably committed.
No rollback or retry is inferred from the backend error.

## Authority and remaining obligations

Five deterministic groups cover fixed binding and argument rejection, exact /
missing / wrong-sized / failed reads, prepared-write and commit failures,
full-blob marker rewrite and malformed/already-committed refusal, real store
rotation, and restart discovery after an applied-then-failed marker commit.
All twenty-seven map suites pass 100/100 repeats, and the complete
88-executable host matrix passes under strict C++17 warnings-as-errors
including publication safety.

There is deliberately no `erase_key` operation. Factory reset, replacement,
commissioning, and protected-history recovery remain separate authorized
workflows. The target owns initialization, exclusive locking, native error
translation, namespace access policy, flash encryption, authenticated
integrity, trusted anti-rollback, and evidence that commit survives physical
reset/power loss.
