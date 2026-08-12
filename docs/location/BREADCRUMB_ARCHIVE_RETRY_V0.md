# Checked-Time Breadcrumb Archive Retry v0

Status: host-tested optional-client boundary, 2026-08-12. This is not a network
adapter, server, delivery guarantee, or target task.

## Purpose

The RAM outbox preserves exact private breadcrumb records when a future remote
archive is unavailable. Calling its uploader on every target-loop iteration
would still be unsafe: it could hammer an unavailable service, consume power,
and hide a broken boot-local clock. `BreadcrumbArchiveRetryCoordinator` adds
one fixed-memory checked-time owner between that loop and the existing
one-record uploader.

The coordinator has no base-radio, GPS, display, session, or capture authority.
Its failure can stop optional uploads only. It cannot stop local messaging,
alerts, group position behavior, or USB recovery.

## Service rules

- An empty outbox returns idle without reading the clock or calling the remote.
- A nonempty outbox requires one successful `CheckedMonotonicClock` sample
  before an upload attempt or deadline comparison.
- The first queued head is eligible immediately after a successful time read.
- `not_ready` and transport `failed` retain the exact FIFO head and schedule a
  retry. The delay starts at the configured nonzero initial interval, doubles
  after each consecutive transient outcome, and stops at the configured
  maximum.
- Cooperative calls before the exact deadline read checked time but do not call
  the uploader. Equality with the deadline permits one attempt.
- A durable acknowledgement and exact local commit clear the schedule and
  restore the initial interval for the next FIFO head.
- Temporary clock not-ready retains the queue and performs no upload. Clock
  source failure, rollback, or an already latched clock fault closes this boot
  composition.
- Remote rejection closes this boot composition rather than repeatedly sending
  the same immutable record. Uploader ambiguity, invalid policy, and deadline
  overflow also latch closed. The FIFO remains for explicit operator/service
  reconciliation.
- Each cooperative service call attempts at most one record.

The current host-test policy uses 10 ms initial and 40 ms maximum intervals so
the behavior is fast to exercise. Those values are not target defaults, server
terms, or a field recommendation. Target intervals require power, network,
provider, and privacy review.

## Failure and privacy boundary

Status exposes only coarse scheduling/counter state. It does not expose record
bytes, coordinates, endpoints, credentials, participant identity, or upload
receipts. A scheduled deadline is boot-local monotonic time, not UTC.

The coordinator does not authenticate a server, define TLS or an HTTP request,
interpret a receipt, persist a retry deadline, or decide retention/deletion.
`durable_ack` remains an injected future-adapter promise, not proven remote
durability. Reset can lose the volatile FIFO and all retry state.

## Evidence

Ten deterministic host groups plus 100/100 focused repeats cover:

1. invalid-policy refusal without clock or network activity;
2. empty-queue idle behavior without a clock read;
3. immediate first attempt using the exact checked time;
4. exact-deadline retry without premature remote calls;
5. exponential delay growth and configured maximum;
6. delay reset after exact durable acknowledgement and commit;
7. temporary clock deferral and later recovery;
8. source failure and rollback latching;
9. remote-rejection containment without repeated upload; and
10. deadline-overflow containment with FIFO retention.

This is host-only evidence. ESP-IDF scheduling/locking, sleep/wake policy,
power measurement, connectivity awareness, a real remote adapter, target
configuration, rendered operator controls, physical interruption, and
on-device behavior remain open.

The separate
[privacy-safe presentation adapter](BREADCRUMB_ARCHIVE_PRESENTATION_V0.md)
now maps copied retry/session/outbox state into action-free semantic notices.
It does not add a renderer, recovery authority, or target composition.

## Next gates

Before real coordinates can use this path: define authenticated transport and
receipt semantics; decide whether protected persistent records and retry state
are justified; bind participant/group authority; define visible failure,
discard, retention, export, deletion, and lost-device workflows; select
reviewed target intervals; and validate concurrency, power, interruption, and
privacy behavior on frozen hardware.
