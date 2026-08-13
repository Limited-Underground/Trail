# Serialized Breadcrumb Archive Runtime Owner v0

Status: **host-tested private writer ownership; no ESP-IDF task, authorization
UI, concurrent-target, or physical claim**, 2026-08-12.

## Purpose

The serialized snapshot adapter proves that three status records can be copied
inside one injected lock. Atomic observation is still incomplete if target code
can mutate the capture session, outbox, uploader, or retry coordinator without
using that same serialization domain.

`SerializedBreadcrumbArchiveRuntimeOwner` closes that common-code structural
gap by constructing and retaining those concrete mutable objects privately. Its
public surface contains only:

- start capture with an opaque nonzero session ID and boot-local time;
- stop capture;
- service one current-position capture opportunity;
- service at most one retry-controlled upload opportunity; and
- obtain the already defined privacy-safe complete status snapshot.

Every operation passes through one injected `BreadcrumbArchiveSnapshotLock`.
No reference to a mutable archive component leaves the owner.

## Serialized operation behavior

| Lock/operation state | Owner behavior |
| --- | --- |
| Lock acquired | Attempt exactly the requested component operation, then release once |
| Lock not ready | Defer without attempting the component operation or calling release |
| Lock failed or unknown | Attempt nothing, latch the optional runtime closed |
| Component rejects input | Release normally and return a completed gateway operation carrying the typed component rejection |
| Release failed or unknown after operation | Mark the operation attempted and its outcome uncertain; latch closed |
| Snapshot adapter latches | Propagate a typed snapshot failure and block later writer operations without another lock attempt |

Lock errors and component errors remain separate. For example, session ID zero
is a completed serialized call with `invalid_session`, not a lock failure.
Likewise, remote rejection and retry policy failure stay in the existing retry
result rather than being misreported as synchronization faults.

## Privacy and authority boundary

This owner has optional archive execution authority, so target composition must
place start and stop behind explicit local consent and approved policy. No such
UI or authorization is implemented here.

The owner deliberately exposes no whole-queue discard, record inspection,
coordinate query, export, deletion, retention change, endpoint configuration,
credential, account, remote retrieval, or base-radio operation. It accepts only
the already minimized current-position snapshot for capture. Optional runtime
failure cannot directly stop or reconfigure group messaging.

## Failure containment

Temporary lock contention is retryable and guarantees no component call.
Acquire failure means mutation was not attempted. Release uncertainty occurs
after a component call and therefore sets `outcome_uncertain`; the runtime then
blocks every writer and snapshot call for operator/reboot reconciliation.

The owner does not attempt an unlocked compensating stop after unlock failure.
Whether the original mutation applied cannot be proven safely, and issuing a
second mutation outside known lock ownership would worsen the uncertainty.

## Host evidence

Ten deterministic scenario groups plus 100/100 focused repeats cover:

1. start and snapshot sharing the same lock domain;
2. contention deferral without mutation;
3. current-position capture into the private outbox;
4. durable-ACK upload commit through the private retry path;
5. serialized stop reflected in the next snapshot;
6. component rejection remaining distinct from lock failure;
7. writer lock failure blocking later snapshots;
8. unlock failure producing an uncertain outcome and latching;
9. snapshot lock failure propagating to the writer gate; and
10. unknown writer acquire/release states failing closed.

The complete 109-executable host matrix and Python evidence checks pass. This
proves common-code object ownership and ordering only. It does not prove a real
ESP-IDF primitive, preemptive concurrency, task priorities, physical storage,
network durability, or hardware behavior.

## Next gates

- Keep the host-tested
  [complete local workflow](BREADCRUMB_ARCHIVE_WORKFLOW_COORDINATOR_V0.md) as
  the target control path without adding remote control or making archive
  service a base-client requirement.
- Bind the lock to a reviewed ESP-IDF primitive and run all archive scheduling,
  upload, and UI calls through the private owner on one selected target.
- Add concurrent task stress and injected contention/stall/reboot tests.
- Measure lock hold time, stack/RAM, queue pressure, UI latency, power, and base
  messaging independence on frozen hardware.
