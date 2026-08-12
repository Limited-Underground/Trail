# Recoverable update checkpoint store v0

Status: abstract two-slot host composition with deterministic fault injection,
a caller-supplied trusted-generation contract, and a separate host-tested
key/value target boundary. No ESP-IDF backend, partition/security
configuration, hardware-backed trusted source, authenticated integrity,
physical interruption, or endurance evidence exists.

## Purpose

The store wraps canonical 64-byte [`OTU0/v0`](UPDATE_STATE_CHECKPOINT_V0.md)
records so a new lifecycle decision does not overwrite the only known-good
copy. It owns normal generation allocation and alternates between two exact
slots.

## Save rules

1. Inspect both slots without changing them.
2. Fail closed if either read fails, or equal generations contain different
   valid bytes.
3. Select the newest unique valid record.
4. Normal `save` allocates generation 1 on empty media, otherwise newest plus
   one. `save_next_after` instead allocates one beyond the greater of newest
   local and last-trusted generation. Both refuse 64-bit exhaustion before
   export or write.
5. Ask the running guard to export its current persistent lifecycle state with
   that generation.
6. Write an empty/invalid peer slot, or the slot opposite the newest record.
7. Read back the entire slot and require exact bytes, successful canonical
   decode, and the intended generation before reporting success.

A write error or failed readback is `commit_uncertain`; callers must not assume
the intended generation was absent or committed. They reboot and inspect both
slots. The store never overwrites the sole valid record during that attempt.

## Restore rules

- both empty: no checkpoint;
- no valid record plus any invalid record: service required;
- one valid plus empty/invalid peer: restore the valid record as degraded and
  mark recovery required;
- two valid unique generations: restore the newest;
- two byte-different records at the same generation: conflict, no restore;
- any unreadable slot: fail closed because it could hide newer committed state;
  and
- policy/hardware/version rejection by the guard: no live mutation.

The explicit `restore_at_or_above` path additionally rejects missing media or a
newest valid checkpoint below the caller-supplied trusted minimum before guard
restore. Exact or newer generations continue through normal validation. See the
[trusted-generation contract](UPDATE_TRUSTED_GENERATION_FLOOR_V0.md).

After a degraded restore, a later save targets the missing/invalid peer and
reports that it repaired the redundant copy. Reset requires both abstract slot
erases to succeed.

## Read-only inspection

`inspect` applies the same two-slot selection and conflict rules without
restoring a guard or writing storage. It reports slot states, selected source
and generation, checkpoint availability, and whether known degradation requires
recovery. An unreadable peer remains a storage failure even when the other slot
is visible, because that peer could conceal a newer committed generation.

## Evidence and limitations

Twenty deterministic groups plus 100 focused repeats cover empty/first save,
rotation/newest selection, partial-write preservation, corrupt-readback
preservation, invalid-peer repair, unreadable-slot fail-close, equal-generation
conflict, atomic policy rejection, generation exhaustion, reset, and rejection
of nonpersistent guard state, plus missing/stale trusted-floor rejection,
boundary acceptance, allocation beyond local/trusted state, and trusted-floor
exhaustion. Four inspection groups additionally cover empty, healthy/degraded,
unreadable-peer, invalid-only, and conflict observations without mutation. The
complete OpenTrail host matrix passes.

The slot interface does not define flash erase, atomic program units, wear
leveling, namespace protection, encryption, anti-rollback, or commit markers.
The separate [key/value adapter](UPDATE_CHECKPOINT_KV_TARGET_ADAPTER_V0.md)
fixes exact names, 64-byte values, and explicit backend commit ordering, but
the concrete target backend still must define and physically test those
properties.
The caller-supplied floor does not make its source protected and cannot prevent
forged newer state without authenticated checkpoint integrity. CRC remains
accidental-corruption evidence only.
