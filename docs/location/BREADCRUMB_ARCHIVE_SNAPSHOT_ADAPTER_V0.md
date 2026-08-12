# Serialized Breadcrumb Archive Snapshot Adapter v0

Status: **host-tested target-shaped locking composition; no ESP-IDF mutex,
concurrent-writer, or on-device claim**, 2026-08-12.

## Purpose

Archive UI requires one coherent tuple from the capture session, bounded
outbox, and retry coordinator. `SerializedBreadcrumbArchiveSnapshotSource`
implements the target-facing source contract against those three concrete
owners and one injected nonblocking serialization boundary.

A successful call:

1. acquires the injected archive snapshot lock once;
2. copies all three concrete `status()` results while that lock is held;
3. releases the lock once; and
4. publishes the complete local tuple only after successful release.

It performs no heap allocation, creates no task, and contains no record bytes,
coordinates, endpoint, credential, or participant identity in its own status.

## Lock contract

`BreadcrumbArchiveSnapshotLock` has two explicit operations:

| Operation result | Adapter behavior |
| --- | --- |
| acquire `acquired` | Copy all three status owners, then release once |
| acquire `not_ready` | Redact caller output and return temporary snapshot not-ready; do not call release |
| acquire `failed` or unknown | Redact output and latch the optional snapshot source failed |
| release `released` | Publish the complete copied tuple as ready |
| release `failed` or unknown | Never publish the local copy; redact output and latch failed |

After a latched failure, later snapshot calls return failed without another
lock attempt. This prevents uncertain lock ownership from being treated as a
safe retry. Counters saturate instead of wrapping.

The eventual target must bind this interface to one mutex, task-owned critical
section, or equivalent primitive. Every writer that mutates the session,
outbox, or retry owner must use the same serialization domain. This common-code
adapter cannot enforce writer discipline outside its own call.

## Output and authority boundary

Caller output is zeroed before every attempt. Temporary contention, lock
failure, unlock failure, an unknown lock result, and a previously latched
failure never expose a prior or partially copied tuple. Only a successfully
released critical section publishes the candidate.

The adapter has read-only status access. It cannot start/stop capture, enqueue,
commit or discard a record, initiate upload, change retry policy, poll input,
or affect base radio service. Its failure is therefore confined to optional
archive observation.

## Host evidence

Ten deterministic scenario groups plus 100/100 focused repeats cover:

1. one balanced acquire/copy/release operation;
2. copying active concrete session state in the complete tuple;
3. temporary contention, output redaction, and later recovery;
4. lock failure latching with no later lock access;
5. unlock failure withholding a fully copied local candidate;
6. unknown acquire-state containment;
7. unknown release-state containment;
8. observing later retry-owner state in a new snapshot;
9. composition through the privacy-safe single-owner archive UI; and
10. fixed fake-lock script capacity.

The complete 98-executable host matrix and Python evidence checks pass. This is
common-code ordering evidence, not proof that a target mutex is correct or that
concurrent writers actually honor it.

## Next gates

- Select the concrete client target and bind the lock interface to its reviewed
  ESP-IDF synchronization primitive.
- Compose the host-tested
  [private serialized runtime owner](BREADCRUMB_ARCHIVE_RUNTIME_OWNER_V0.md),
  which routes every exposed session/outbox/retry mutation through this lock;
  then add concurrent target service/copy stress evidence.
- Measure lock hold time, contention, priority behavior, stack/RAM, UI latency,
  and power on the frozen target.
- Inject physical task stalls, display unavailability, reboot, and power faults
  while proving base messaging remains available.
