# Typed update-recovery boot coordinator v0

Status: pure host coordinator with deterministic fault injection. No ESP-IDF
boot task, partition adapter, authenticated checkpoint backend, hardware-backed
trusted-generation source, physical interruption, or recovery-button evidence
exists.

## Purpose

The lifecycle guard, `OTU0` codec, two-slot store, and trusted-generation floor
are individually fail-closed, but a target can still become unsafe by calling
them in the wrong order. This coordinator fixes the boot ordering and returns a
typed decision before application state is exposed.

It owns no storage or hardware. Callers inject:

- the expected hardware/version/slot and trial-health policy;
- the actual boot session, version, slot, and boot-local time observation;
- the two-slot checkpoint store; and
- an independently protected trusted-generation source.

The output `UpdateBootGuard` must not already be running.

## Private-before-live ordering

1. Start a private guard with the supplied policy.
2. Read the trusted generation before inspecting update media.
3. Treat `not_initialized` as a clean baseline only when both checkpoint slots
   are exactly empty and the observed image exactly matches the baseline.
4. Otherwise require a nonzero trusted generation and restore the newest valid
   checkpoint at or above that floor into the private guard.
5. Validate the observed image against the restored lifecycle state.
6. For pending/trial state, record the new trial boot on the private guard. A
   boot mismatch or exhausted attempt limit becomes persisted rollback intent.
7. For rollback-required state, verify and complete the exact baseline rollback
   on the private guard.
8. Save and fully read back any trial-count or rollback transition before
   advancing trust.
9. Advance the trusted generation and require exact readback.
10. Copy the private guard to the live output only after every required step
    succeeds.

A successfully saved checkpoint followed by a failed trust update/readback is
not retried in the same boot. The result requires reboot reconciliation so the
newest committed generation can be inspected without double-counting a trial.

## Typed outcomes

| State | Application allowed | Meaning |
| --- | --- | --- |
| `baseline_ready` | yes | independently uninitialized trust, exactly empty media, and exact baseline observation |
| `trial_ready` | yes | candidate boot accepted and its incremented attempt durably committed/trusted |
| `rollback_required` | no | mismatch or attempt limit durably requires a reboot to the baseline |
| `baseline_recovered` | yes | exact rollback completion or a previously persisted rolled-back terminal state |
| `confirmed_cleanup_required` | yes | confirmed candidate matches the observation; policy/checkpoint cleanup remains |
| `safe_mode` | no | rollback, conflict, invalid checkpoint, or image/state mismatch evidence |
| `service_required` | no | trusted-source, unreadable-storage, exhaustion, or uncertain persistence failure |

`application_allowed` is not a claim that radio, GPS, UI, secrets, or other
trial-health signals have passed. A `trial_ready` application must still report
every required health signal and confirm within the bounded lifecycle policy.

## Evidence and limitations

Fifteen deterministic groups plus 100 focused repeats cover clean baseline,
wrong image, uninitialized-media conflict, trusted-source failure, zero trust,
missing/stale rollback, pending and resumed trials, boot mismatch, attempt
exhaustion, rollback completion/mismatch, confirmed/rolled-back cleanup, trust
advance/readback failure, uncertain checkpoint commit, equal-generation
conflict, and unreadable peer media. The complete 57-executable OpenTrail host
matrix passes.

Runtime persistence after boot uses the separate [verified save
coordinator](UPDATE_RECOVERY_SAVE_COORDINATOR_V0.md), with trial-time mutation
owned by the [transition
coordinator](UPDATE_RECOVERY_TRANSITION_COORDINATOR_V0.md). Target scheduling,
reboot execution, and terminal cleanup/reset remain open.

Target-facing consumers use the separate [redacted operator
status](UPDATE_RECOVERY_STATUS_V0.md), which refuses incoherent boot results
before exposing any continue, reboot, cleanup, or service action.

The host contract cannot prove that `not_initialized` is authentic, that the
trusted value resists erasure/rollback, that checkpoint contents cannot be
forged, or that target flash writes survive power loss. Authorized cleanup,
factory reset, device replacement, terminal-state rebasing, signer/key custody,
and physical recovery remain explicit target gates.
