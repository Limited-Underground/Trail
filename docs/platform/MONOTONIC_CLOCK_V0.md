# Monotonic Clock Boundary v0

Status: deterministic host boundary and test fake, 2026-08-10. No ESP-IDF
timer adapter, target-task composition, deep-sleep behavior, or physical timing
evidence is claimed.

## Purpose

OpenTrail delivery, retry, expiry, replay, rate-limit, freshness, logging, and
recovery components already accept explicit monotonic milliseconds. They must
not each read a board timer independently or confuse elapsed time with UTC.
This boundary gives a target application one checked source for the boot-local
timestamp passed into those components.

The production-facing interface lives in
`firmware/components/time/include/opentrail/monotonic_clock.hpp`. Its
deterministic source is isolated under `test_support`.

## Time domains

- Monotonic milliseconds are elapsed boot-local process time. Zero is valid.
- UTC, GNSS time, MeshCore clock synchronization, and human calendar time are
  separate data with separate validity. None may replace this counter.
- Equal readings are valid because multiple operations may occur within one
  millisecond.
- A decreasing reading is a rollback even if a wall clock moved or was
  corrected.
- A deliberate reboot creates a new clock composition and boot/session domain.
  Persistent components reconstruct remaining durations against that new
  origin; they do not compare raw pre- and post-boot counter values.

## Source and checked boundary

`MonotonicCounterSource` returns one of three raw outcomes:

- `none` with one 64-bit millisecond value;
- `not_ready` when no usable sample is currently available; or
- `source_failed` when the adapter cannot safely provide time.

`CheckedMonotonicClock` applies the policy:

- the first valid value initializes the boot-local baseline;
- equal or increasing values succeed and update the last value;
- `not_ready` returns no timestamp, preserves continuity, and may recover on a
  later equal/increasing sample;
- source failure latches the boundary closed;
- rollback latches the boundary closed without accepting the bad value;
- a latched guard refuses future calls without consuming more source samples;
- failures return a canonical zero value plus a typed error, so zero is usable
  only when the result itself reports success; and
- fixed saturating counters expose attempts, successes, not-ready reads, source
  failures, rollbacks, and latched refusals without free-form data.

There is no ordinary in-place reset. Intentional reboot/reinitialization must
construct a new guard alongside a new boot/session identity and the appropriate
persistent-state restore path.

## Target composition rule

A cooperative target cycle should call `now()` once and pass the same successful
value to radio service, delivery, duplicate, priority, location, alert, logging,
and persistence work performed in that cycle. This prevents intra-cycle ordering
from depending on timer-read timing.

When a read is not ready or failed, the target must not invoke time-dependent
state machines with an invented or stale value. It should retain bounded work,
surface a coarse diagnostic through the existing redacted logger, and follow
the role-specific safe-mode/restart policy. A timer failure must never make an
expired message current or bypass a retry/rate/freshness boundary.

The host-tested [outbound service coordinator](OUTBOUND_SERVICE_COORDINATOR_V0.md)
now proves this rule for the location/scheduler/priority/delivery/radio slice:
one successful sample is shared across the complete cycle, not-ready invokes no
downstream service, and rollback/source failure stops position sharing and
latches the coordinator closed. Remaining target boot, persistence, inbound,
UI, and concurrency composition still require their own evidence.

## Deterministic fake and host evidence

`FakeMonotonicCounterSource` is a fixed 16-sample FIFO that scripts successful
values, not-ready reads, and failures. It defaults to not ready and refuses
over-capacity insertion.

Eight scenario groups cover:

1. default not-ready behavior followed by a valid zero origin;
2. equal millisecond readings;
3. forward progress through the full 64-bit value boundary;
4. transient not-ready recovery without lost continuity;
5. rollback latching and refusal without consuming later samples;
6. source-failure latching;
7. explicit new-boot composition after a latched fault; and
8. bounded FIFO capacity/order plus empty behavior.

The focused executable passes 100 consecutive repeats, and the complete 30 C++
executable host matrix plus the Python evidence suites pass locally. This is
interface and failure-ordering evidence, not timer accuracy or target evidence.

## Target acceptance still required

The target adapter must freeze the exact board, ESP-IDF version, timer API,
conversion, task ownership, and boot/deep-sleep policy. It must demonstrate
64-bit extension or explicit failure before any native counter wrap/saturation,
thread-safe reads across the selected core/task model, startup readiness,
watchdog and load behavior, reboot/deep-sleep/brownout handling, and stable
elapsed-time behavior while radio, display, storage, and GNSS work are active.

Physical acceptance must measure resolution, latency, drift relevant to the
policy windows, repeated cold boots, long-run continuity, and injected timer or
adapter failures. Until then, no target timer or board composition is supported.
