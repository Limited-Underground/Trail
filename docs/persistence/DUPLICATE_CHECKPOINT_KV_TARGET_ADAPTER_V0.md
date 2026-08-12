# Duplicate Checkpoint Key/Value Target Adapter v0

Status: deterministic host-tested target boundary, 2026-08-12. No ESP-IDF
backend, protected namespace, partition/security configuration, authenticated
integrity, trusted rollback, physical interruption, endurance, or on-device
save is claimed.

This adapter binds the abstract two-slot
[`DuplicateCheckpointStorage`](DUPLICATE_CHECKPOINT_STORE_V1.md) surface to
backend-neutral key/value operations while leaving the context-bound
`ODS0/v1` codec and two-slot recovery policy unchanged.

## Fixed binding

- partition label: `ot_state`
- namespace: `ot_replay`
- slot A key: `ods_dup_a`
- slot B key: `ods_dup_b`
- value size: exactly 704 bytes (`ODS0/v1`)

Every identifier fits the ESP-IDF NVS 15-character limit. Invalid slots, null
buffers, and non-704-byte operations fail before backend I/O. A successful read
must report exactly 704 bytes; wrong-sized values remain storage failures and
never reach record decoding.

## Durability contract

`write_blob` and `erase_key` stage one mutation, and adapter success requires a
following backend `commit`. Missing-key erase is idempotent without a redundant
commit. A commit failure stays visible even if the target may have applied the
mutation. The existing two-slot store then discovers any valid durable record
on restart instead of retrying or rolling back blindly.

The target must exclusively own the handle and namespace during the staged
operation and must not mix unrelated pending writes. It owns initialization,
locking, native error translation, namespace access control, flash encryption,
wear policy, and physical durability evidence.

## Evidence and limits

Nine deterministic groups cover fixed names, public argument rejection, exact
and missing reads, wrong-size and I/O failures, committed writes, failed and
applied-then-failed commits, idempotent committed erase, real context-bound
store rotation/restore/reset, and restart discovery after uncertain commit.

The focused suite passes 100/100 repeats, and the complete 90-executable host
matrix passes under strict C++17 warnings-as-errors including publication
safety.

This boundary does not authenticate `ODS0`, protect its generation against
rollback, authorize reset/migration, persist queued frames, or prove privacy
retention policy. Group context and epoch validation remain owned by the upper
store and codec.
