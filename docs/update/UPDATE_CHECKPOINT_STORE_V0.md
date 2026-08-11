# Recoverable update checkpoint store v0

Status: abstract two-slot host composition with deterministic fault injection.
No ESP-IDF/NVS/partition adapter, authenticated integrity, protected generation
floor, physical interruption, or endurance evidence exists.

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
4. Allocate generation 1 on empty media, otherwise newest plus one; refuse
   64-bit exhaustion before export or write.
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

After a degraded restore, a later save targets the missing/invalid peer and
reports that it repaired the redundant copy. Reset requires both abstract slot
erases to succeed.

## Evidence and limitations

Ten deterministic groups plus 100 focused repeats cover empty/first save,
rotation/newest selection, partial-write preservation, corrupt-readback
preservation, invalid-peer repair, unreadable-slot fail-close, equal-generation
conflict, atomic policy rejection, generation exhaustion, reset, and rejection
of nonpersistent guard state. The complete OpenTrail host matrix passes.

The slot interface does not define flash erase, atomic program units, sync,
wear leveling, namespace protection, encryption, anti-rollback, or commit
markers. The target adapter must define and physically test those properties.
CRC remains accidental-corruption evidence only.
