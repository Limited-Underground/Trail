# Trusted update-generation floor v0

Status: caller-supplied host contract with deterministic rollback tests. No
hardware-backed trusted counter, authenticated checkpoint integrity, target
adapter, reset authority, or physical interruption evidence exists.

## Purpose

The generation inside an `OTU0/v0` checkpoint is ordinary media data. It cannot
prove its own freshness. This contract lets a future boot/save coordinator
compare that record with a minimum generation obtained independently from a
protected source.

The two values have deliberately separate ownership:

- the checkpoint store owns generations written into its two ordinary slots;
- a target adapter supplies the last trusted generation; and
- a separately authorized workflow owns initialization, advancement, reset,
  device replacement, and recovery of that trusted value.

## Restore contract

`restore_at_or_above(guard, trusted_minimum_generation)` performs the normal
two-slot inspection first. Storage failure, invalid-only media, or an equal-
generation conflict retains its existing fail-closed result.

After selecting the newest unique valid checkpoint:

- no checkpoint with a nonzero trusted minimum is
  `generation_below_floor`;
- a valid generation below the minimum is `generation_below_floor`;
- a generation exactly at or above the minimum may proceed to the guard's
  complete policy/hardware/version validation; and
- every below-floor result requires recovery and leaves the live guard
  unchanged.

The accepted generation is still not authenticated merely because it is newer.
The target checkpoint backend must separately provide authenticated integrity
and namespace/device binding.

## Save contract

`save_next_after(guard, last_trusted_generation)` selects the greater of the
newest valid local generation and the supplied trusted generation, then writes
exactly the next generation through the existing alternating-slot and exact-
readback path. This permits a known local checkpoint that is behind trust to be
replaced without reusing a generation.

If either value is `UINT64_MAX`, allocation fails with
`generation_exhausted` before checkpoint export or storage write. Existing
unreadable, invalid-only, conflicted, write-error, and readback-error behavior
does not become less strict.

The store does not advance the trusted source. Its coordinator must:

1. read the trusted generation;
2. commit and verify the next checkpoint;
3. advance the trusted value only to that verified generation;
4. read back the trusted value exactly; and
5. route any uncertain checkpoint or trust result through boot reconciliation
   before enabling normal operation.

The host-tested [boot coordinator](UPDATE_RECOVERY_BOOT_COORDINATOR_V0.md) now
implements that ordering for boot restoration, trial-attempt persistence, and
rollback completion. The injected trusted source and target storage still do
not exist.

The host-tested [save coordinator](UPDATE_RECOVERY_SAVE_COORDINATOR_V0.md) now
implements the normal-operation half. It requires exact local/trusted agreement,
verifies the next checkpoint, advances trust, and verifies exact trust readback.
Local-ahead or uncertain post-write results require reboot reconciliation. The
[transition coordinator](UPDATE_RECOVERY_TRANSITION_COORDINATOR_V0.md) now owns
trial-time mutation on a private guard copy, publishes it only after the save
commits, and stops the live guard on persistence failure. Target scheduling,
reboot execution, protected trust, and bypass prevention remain open.

## Evidence and limitations

Six new scenario groups cover absent media below a trusted floor, stale valid
media rejection without live mutation, exact/newer boundary acceptance, first
save beyond trust, allocation beyond the greater of local/trusted state, and
trusted-counter exhaustion without a write. Together with the ten original
groups and four read-only inspection groups, all 20 store groups pass in the
complete OpenTrail host matrix and across 100 focused repeats. Ten save-
coordinator groups and 100 focused repeats separately verify exact normal-
Ten transition-coordinator groups and 100 focused repeats verify that durable
trial-time mutations cannot remain live only in RAM.

This is an interface and state-machine result. It does not prove that any ESP32
target can keep the supplied generation secret, authentic, monotonic, durable,
or resistant to rollback, flash replacement, factory reset, or physical
attack. It also does not authenticate `OTU0` contents. Those remain target and
physical acceptance gates.
