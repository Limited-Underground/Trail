# OpenTrail GPS/Location Abstraction

Status: host-tested OT-011 foundation, 2026-08-08

The location component separates a board-specific GPS receiver/driver from the
logic that decides whether a fix is usable. It does not select a GPS module,
parse NMEA or UBX, estimate position, or claim hardware accuracy.

## Provider contract

`GpsProvider::latest_fix()` returns either no available fix or the provider's
latest observation. A fix uses explicit integer units:

| Field | Unit / representation |
| --- | --- |
| Latitude, longitude | signed degrees multiplied by 10,000,000 |
| Altitude | signed centimetres |
| Horizontal accuracy | centimetres |
| Speed | centimetres per second |
| Heading | unsigned centidegrees in `[0, 36000)` |
| Receive time | local monotonic milliseconds |
| UTC | seconds since the Unix epoch when independently marked valid |

Altitude, horizontal accuracy, speed, heading, and UTC each have an explicit
validity flag. A numeric zero therefore remains distinguishable from an absent
measurement. Providers report observations; they do not set application stale
policy or bypass validation.

## Tracker semantics

`LocationTracker` reads one observation and returns exactly one state:

- `unavailable`: the provider has no fix;
- `invalid`: coordinates, present optional fields, or the monotonic timestamp
  violate the contract;
- `stale`: the fix is structurally valid but its age is greater than or equal
  to the configured stale threshold; or
- `valid`: the fix is structurally valid and fresh enough for local use.

Latitude is limited to `[-90, 90]` degrees and longitude to `[-180, 180]`
degrees. A present heading must be below 360 degrees, and present horizontal
accuracy must be nonzero. A receive timestamp later than the caller's current
monotonic time is invalid, avoiding unsigned-age underflow after clock misuse or
reset.

Position validity never depends on UTC. A node may boot, obtain a fresh spatial
fix, and continue messaging before GPS UTC becomes available. Protocol logic
that needs wall-clock time must check `utc_valid` separately and must not infer
UTC from monotonic fix age.

## Failure boundaries and later gates

An unavailable, invalid, or stale fix must affect location presentation and
position broadcasts without stopping chat, alert, delivery, or forwarding
services. The snapshot exposes stale fixes for clearly labelled UI/history use,
but only `valid` returns `usable()`.

The fake provider and nine host scenarios cover unavailable, fully valid,
no-UTC, coordinate-invalid, optional-field-invalid, absent-optional, exact
stale-boundary, recovery with a newer fix, and future-timestamp behavior.
Hardware driver compatibility, antenna placement, acquisition time, accuracy,
power draw, loss/recovery behavior, and field evidence remain open hardware
gates.
