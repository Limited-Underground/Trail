# Update Checkpoint Key/Value Target Adapter v0

Status: deterministic host-tested target boundary, 2026-08-12. No ESP-IDF
backend, partition table, encryption configuration, hardware-backed trusted
generation, physical interruption result, endurance result, or on-device save
is claimed.

This adapter binds the abstract two-slot
[`UpdateCheckpointStorage`](UPDATE_CHECKPOINT_STORE_V0.md) interface to four
backend-neutral key/value operations. It deliberately contains no ESP-IDF
headers so the checkpoint lifecycle remains host-testable and the eventual
target binding stays small and reviewable.

## Fixed binding

- partition label: `ot_state`
- namespace: `ot_update`
- slot A key: `otu_chk_a`
- slot B key: `otu_chk_b`
- value size: exactly 64 bytes (`OTU0/v0`)

Every name is bounded to the ESP-IDF NVS 15-character identifier limit. The
adapter rejects invalid slots, null buffers, and non-64-byte operations before
calling the backend. A successful read must report exactly 64 bytes; a shorter
or longer value is an I/O failure, not a valid checkpoint.

## Durability contract

`write_blob` and `erase_key` stage one change. The adapter calls `commit` before
reporting success. A commit error stays visible even if the backend may have
applied the change; the upper checkpoint store already reports such a save as
commit-uncertain and requires restart inspection. Missing-key erase is
idempotent and does not issue a redundant commit.

The target backend must provide exclusive access for the complete staged
operation and must not share a handle carrying unrelated pending mutations.
It owns NVS initialization, handle lifetime, synchronization, native error
translation, security configuration, and the physical evidence that successful
commit survives reset and power loss.

## Evidence and limits

Nine deterministic groups cover fixed names, public argument rejection, exact
and missing reads, wrong-size and I/O failures, durable writes, write/commit
failures, idempotent committed erase, real store save/rotation/restore/reset,
and restart recovery when a commit applied before returning failure.

The focused suite passes 100/100 repeats, and the complete 90-executable host
matrix passes under strict C++17 warnings-as-errors including publication
safety.

CRC remains accidental-corruption detection only. This adapter does not supply
authentication, anti-rollback, protected key storage, a trusted-generation
source, update-image storage, partition selection, reset authority, or target
task locking.
