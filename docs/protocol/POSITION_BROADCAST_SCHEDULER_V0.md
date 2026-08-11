# Position broadcast scheduler v0

Status: host-tested fixed-memory scheduling boundary, 2026-08-11. No
authenticated packet, radio transmission, selected field cadence, physical GPS,
or regulatory result is claimed.

## Purpose

`PositionBroadcastScheduler` turns caller-supplied location snapshots into the
existing canonical 16-byte position payload at an explicit start/stop cadence.
It sends payloads only to an injected application sink. The sink may later
connect to authenticated packet composition and priority admission. The
separate experimental packet-v0 admission sink proves an unauthenticated host
composition, but this scheduler itself does not build packet v0/v1, queue radio
frames, or call a board driver.

The scheduler starts stopped. A successful explicit `start(now_ms)` makes one
current fix immediately due. `stop()` prevents further sink calls immediately.
Calling start again while already active is idempotent and cannot reset the
next deadline or force an extra position update.

## Cadence and coalescing

Target composition supplies two nonzero boot-local monotonic intervals:

- accepted-send cadence; and
- retry delay for missing fixes or sink pressure/failure.

After an accepted payload, the next deadline is calculated from the actual
acceptance time. If service is delayed past several theoretical intervals, the
scheduler submits only the newest caller snapshot once and schedules the next
deadline from that work. It never accumulates or replays missed position slots.

After a deferred attempt, it retries from the actual attempt time. The
scheduler exposes the next attempt, active state, latest error, and saturating
counters for submissions, suppressed fixes, backpressure, and failures.

No 30/60/300-second profile is selected here. Final moving/stationary cadence
must follow measured contention, power, privacy, and regional review.

## Location and privacy behavior

Only `FixState::valid` snapshots may reach the sink. Unavailable, stale, and
invalid snapshots are counted and deferred without encoding coordinates. A
snapshot marked valid but internally inconsistent must also fail encoding
before the sink is touched.

This deliberate first policy avoids queueing or repeatedly broadcasting old
coordinates after GPS loss. Remote peers must age the last authenticated
position independently. A later explicit no-fix heartbeat policy would require
separate airtime, privacy, UI, and protocol review.

The sink receives a temporary view of exactly one canonical 16-byte payload
plus the exact service-attempt monotonic time and must copy or consume the view
before returning success. Not-ready/full pressure and sink failure remain
distinct and cannot become false submission success.

## Time failure

The scheduler accepts equal monotonic readings. A reading earlier than any
previous start/service call latches clock failure and disables the scheduler
for that boot object. Deadline addition is checked; when a future deadline can
no longer be represented, time exhaustion disables the scheduler instead of
wrapping into an immediate-send loop.

The common checked-monotonic-clock boundary remains the target's primary time
authority. This local check prevents misuse if a scheduler is composed around
an unchecked source.

## Host evidence

Ten deterministic groups cover:

1. invalid cadence/retry policy without sink access;
2. explicit start and immediate canonical current-position output;
3. exact cadence, idempotent start, and delayed-service coalescing;
4. unavailable/stale/invalid suppression and exact retry boundary;
5. not-ready/full pressure without false submission;
6. typed sink failure and unknown-error handling;
7. immediate stop plus immediate explicit restart;
8. latched monotonic rollback;
9. malformed-current encoding refusal before sink access; and
10. checked time horizon plus fixed payload/script capacities.

The focused executable passes 100/100 repeats. The complete 57-executable
OpenTrail host matrix plus all Python and publication-safety checks pass.

The separate [semantic privacy-control adapter](../platform/POSITION_SHARING_CONTROL_V0.md)
now maps scheduler state to explicit local start/stop actions without granting
the UI sink, radio, GPS, or emergency authority.

The [experimental packet-admission sink](POSITION_PACKET_ADMISSION_V0.md) now
proves exact-time packet-v0/background-queue composition without claiming
authentication, delivery, radio transmission, or safe handling of real
coordinates.

The host-tested [outbound service coordinator](../platform/OUTBOUND_SERVICE_COORDINATOR_V0.md)
now supplies one checked monotonic value to active-sharing location, this
scheduler, priority handoff, delivery, and radio service. It also proves that
stopped sharing performs no GPS read and that a permanent clock fault stops
sharing and latches the cooperative outbound cycle closed.

## Remaining gates

- select cadence only after measured four-client contention, privacy, and power
  review;
- replace the experimental packet-v0 admission sink with authenticated packet
  composition and the fake transport with one exact direct-radio adapter;
- decide remote position expiry and whether an explicit no-fix heartbeat is
  justified;
- render and physically validate the host-tested local start/stop states and
  make their wording, layout, and interaction unmistakable;
- bind one exact GPS and monotonic-clock target implementation; and
- measure motion, GPS loss/recovery, queue pressure, airtime, power, range, and
  regional compliance on physical units.
