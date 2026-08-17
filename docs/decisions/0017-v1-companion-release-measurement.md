# Decision 0017: Measure V1 Companion as a distinct release track

Date: 2026-08-17

Status: Accepted

## Decision

V1 Companion is the current phone-assisted Trail release goal and has its own
evidence-weighted measurement. The historical phone-independent calculation is
retained as a standalone evidence baseline; it is not relabeled or reused as
the V1 Companion total. V2 Integrated remains unmeasured until its own
touchscreen, input, power, enclosure, recovery, and field milestones are
approved.

The V1 Companion track totals 100 weight points:

| Milestone | Weight | Accepted completion |
| --- | ---: | ---: |
| Architecture and safety contracts | 15 | 85% |
| Core OpenTrail firmware | 20 | 65% |
| Firmware loading app | 15 | 15% |
| Heltec V4 / Trail Essential target | 15 | 25% |
| Android companion application | 20 | 40% |
| Four-person Companion field proof | 15 | 0% |

The exact weighted result is 39.75%. Public presentation rounds the result to
40%.

Android completion is an explicit two-of-five equal-gate judgment. Accepted
gates are the tested application with bounded physical install/lifecycle/
artwork observations and exact-service-filtered physical discovery. Open gates
are physical GATT connection and negotiation, protected one-phone
authorization and Ready, and complete operational/release acceptance.

## Consequences

- Shared architecture, core, loader, and target evidence is counted once in
  the V1 Companion total.
- Host or build evidence does not become physical connection, authorization,
  Ready, radio, GNSS, recovery, or field evidence.
- V1 Companion cannot reach release acceptance without four frozen
  device-phone pairs and a Companion-specific field-pilot amendment.
- The website must calculate from the canonical track and must not hardcode the
  displayed percentage.
