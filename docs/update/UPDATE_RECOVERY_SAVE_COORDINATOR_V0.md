# Verified update recovery save coordinator v0

Status: pure host coordinator with deterministic fault injection. No ESP-IDF
storage adapter, hardware-backed trusted-generation source, authenticated
checkpoint backend, physical interruption, or endurance evidence exists.

## Purpose

Normal operation must not write a new `OTU0` checkpoint while local storage and
the independently trusted generation disagree. `UpdateRecoverySaveCoordinator`
checks that boundary, commits the next checkpoint, advances trust only after the
checkpoint is verified, and reports success only after exact trust readback.

## Required ordering

1. Require a running `UpdateBootGuard`.
2. Read a nonzero trusted generation.
3. Inspect both checkpoint slots without mutating storage or guard state.
4. Require one unambiguous readable checkpoint generation.
5. Require the local and trusted generations to agree exactly.
6. Write and exactly read back the next checkpoint generation.
7. Advance the trusted generation to that verified checkpoint.
8. Read trust back and require the exact committed generation.

A local generation below trust is rollback evidence and enters safe mode. A
local generation above trust means a prior checkpoint may have committed before
trust advanced, so the caller must reboot and reconcile. An uncertain write or
any post-write trust failure also requires reboot reconciliation and must not be
retried during the same boot.

## Typed outcomes

| State | Meaning |
| --- | --- |
| `committed` | checkpoint and trusted generation both verified at the new generation |
| `reboot_reconcile_required` | storage may be ahead of trust; stop normal persistence and reconcile at boot |
| `safe_mode` | rollback, missing/invalid/conflicted recovery state, or non-running guard |
| `service_required` | trusted-source, unreadable-storage, or exhaustion failure without a safe normal path |

The result retains the prior trusted generation, read-only inspection, store
save result, exact trust readback, and a bounded reason. It contains no key,
identity, radio address, or checkpoint payload.

## Composition boundary

This generic coordinator persists the current guard; it does not itself call
`confirm`, `request_rollback`, `tick`, or `report_health`. The host-tested
[transition coordinator](UPDATE_RECOVERY_TRANSITION_COORDINATOR_V0.md) now owns
those trial-time mutations on a private guard copy, publishes durable changes
only after this save returns `committed`, and stops the live guard on persistence
failure. Target scheduling, reboot execution, and bypass prevention remain
composition obligations.

## Evidence and limitations

Ten deterministic groups plus 100 focused repeats cover a successful ordered
commit, trusted read failure and zero trust, missing/invalid recovery, rollback,
local-ahead reconciliation, conflict, unreadable media, exhaustion, stopped or
idle guards, uncertain writes, and trust advance/readback failures. Four new
read-only inspection groups expand the checkpoint-store suite to 20 groups. The
complete 51-executable OpenTrail host matrix passes.

Target-facing consumers use the separate [redacted operator
status](UPDATE_RECOVERY_STATUS_V0.md), which verifies the save state, reason,
and commit generations before exposing a persistence or recovery action.

This is host state-machine evidence only. Target partition semantics, protected
and authenticated storage, trusted-source provisioning/reset authority, actual
power-cut recovery, wear, task scheduling, and physical recovery remain release
gates.
