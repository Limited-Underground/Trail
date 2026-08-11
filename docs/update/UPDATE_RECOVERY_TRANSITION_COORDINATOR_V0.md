# Durable update recovery lifecycle transitions v0

Status: pure host coordinator with deterministic fault injection. No ESP-IDF
task, protected checkpoint backend, hardware-backed trusted-generation source,
physical interruption, or reboot execution evidence exists.

## Purpose

A live `UpdateBootGuard` must not expose `confirmed` or `rollback_required`
unless the corresponding `OTU0` checkpoint and trusted generation are already
verified. `UpdateRecoveryTransitionCoordinator` owns that ordering for trial-
time health, clock, confirmation, and explicit rollback operations.

## Private-copy ordering

1. Snapshot the live guard and apply the requested operation to a private copy.
2. Compare only reboot-relevant state: running status, lifecycle state,
   rollback reason, candidate, and trial count.
3. If reboot-relevant state did not change, publish the private copy without a
   storage write. This retains boot-local health and monotonic-time behavior.
4. If reboot-relevant state changed, pass only the private copy through the
   [verified save coordinator](UPDATE_RECOVERY_SAVE_COORDINATOR_V0.md).
5. Publish the private copy to the live guard only after checkpoint write,
   checkpoint readback, trusted advance, and exact trusted readback succeed.
6. On any persistence failure, leave the attempted state unpublished, stop the
   live guard, and require safe mode, service, or reboot reconciliation exactly
   as reported by the save coordinator.

The stopped guard is a same-boot failure latch, not a recovery mechanism. Target
composition must stop normal operation and reboot or enter its authorized
service path; it must not call `start` to bypass reconciliation.

## Owned operations

| Operation | Durable behavior |
| --- | --- |
| `report_health` | health/time stays boot-local unless the deadline creates rollback intent |
| `tick` | ordinary time stays boot-local; exact deadline rollback is persisted |
| `confirm` | confirmation becomes live only after verified persistence |
| `request_rollback` | explicit rollback intent becomes live only after verified persistence |

A rejected confirmation can still advance the guard's checked boot-local clock,
matching the underlying guard contract, but it does not write a checkpoint when
no reboot-relevant state changed. A deadline error is different: it creates
rollback intent and therefore must commit before becoming live.

## Typed outcomes

| State | Meaning |
| --- | --- |
| `rejected` | guard refused the requested operation and no durable transition occurred |
| `applied_volatile` | boot-local health/time changed without reboot-relevant state |
| `committed` | durable transition verified and then published to the live guard |
| `reboot_reconcile_required` | persistence may be ahead of live/trusted state; guard stopped |
| `safe_mode` | rollback or invalid recovery evidence; guard stopped |
| `service_required` | storage/trust/exhaustion failure; guard stopped |

The result records only operation, typed state/error, coarse lifecycle states,
the existing bounded persistence result, and stop/persistence flags. It adds no
identity, key, address, raw checkpoint, or radio data.

## Evidence and limitations

Ten deterministic groups plus 100 focused repeats cover committed confirmation,
committed explicit rollback, volatile health/time, rejected confirmation with
clock retention, deadline rollback through both tick and health reporting,
invalid requests without storage/trust access, uncertain writes, local/trusted
generation mismatch, and post-write trust failures. The complete 56-executable
OpenTrail host matrix and publication-safety scans pass.

The separate [redacted operator status](UPDATE_RECOVERY_STATUS_V0.md) validates
operation, guard outcome, lifecycle-state publication, and nested persistence
coherence before producing a target-facing action.

This wrapper does not own staging, candidate image write/selection, boot restore,
rollback execution/completion, confirmed/rolled-back cleanup, authorized reset,
target scheduling, watchdog behavior, or reboot execution. Protected storage,
authenticated integrity, physical power-cut/endurance, and on-device evidence
remain release gates.
