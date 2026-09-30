# OT-0256a Tracker evidence and ownership

Publication version, 2026-09-29: local workspace paths removed; dated technical
facts and evidence limits are unchanged. Original evidence is retained privately.

Observed 2026-09-23. Planning deliverable for owner review, not implementation,
hardware support or a release commitment.

## In plain English

Trail Tracker is the tracking-only product. It reports location; it is not a
two-way messaging product. Existing Trail software can supply parts of that
job, but a complete Tracker device has not been established by this review.
No device or attendance is needed to review this report.

## Authority and ownership

The current approved task, revision 1, and the owner's tracking-only description
govern this inventory. [OT-0256 through OT-0259](../../tasks/BACKLOG.md) already
reserve its remaining planning outcomes. Engineering ownership remains in
OpenTrail, with this review at baseline commit
`46ca4af65d59377a3c068dfe69ae62182c99cbd9`. No new repository is implied.

[Decision 0033](../decisions/0033-permanent-v1-v1-5-scope-and-security-boundary.md)
keeps base V1 as two phones and two Heltecs using protected BLE and direct LoRa.
Tracker is optional and does not become a condition for that path to work.

## Reusable evidence map

These are source and historical host-evidence observations, not new test runs.
The linked tests show the applicable boundary; their existence alone does not
prove a complete Tracker product.

| Candidate for reuse | Exact source / evidence | Layer and limit |
| --- | --- | --- |
| Current, stale, invalid and unavailable location | [LocationTracker](../../firmware/components/location/include/opentrail/location_tracker.hpp), [tests](../../tests/host/location_tracker_tests.cpp) | Hardware-independent fix validation and age classification. The class name is a software state helper, not Trail Tracker firmware. Requires a real selected GPS provider and clock. |
| Compact location encoding | [position codec](../../firmware/components/location/include/opentrail/position_codec.hpp), [tests](../../tests/host/position_codec_tests.cpp) | Host encoding/decoding contract; does not grant radio, privacy or recipient authority. |
| Bounded reporting cadence | [scheduler](../../firmware/components/location/include/opentrail/position_broadcast_scheduler.hpp), [tests](../../tests/host/position_broadcast_scheduler_tests.cpp) | Start/stop, current-fix-only submission, coalesced delayed work and backpressure. A sink submission is not RF transmission or delivered location. Actual cadence is still a product/power/radio decision. |
| Stop and privacy control | [position-sharing contract](../platform/POSITION_SHARING_CONTROL_V0.md), [tests](../../tests/host/position_sharing_control_tests.cpp) | Host semantic UI/control boundary. Tracker's actual control surface is undecided; do not assume it has a touchscreen or phone. |
| Clock-fault containment | [outbound safety](../platform/OUTBOUND_POSITION_SAFETY_V0.md), [tests](../../tests/host/outbound_position_safety_tests.cpp) | Runtime-aware checks prevent a stopped-looking failed scheduler from offering an unsafe restart. Lower-level scheduler status alone is insufficient for integration. |
| Outbound action authority | [command contract](../platform/OUTBOUND_POSITION_COMMAND_V0.md), [tests](../../tests/host/outbound_position_command_tests.cpp) | Fresh checked clock and live owner control, rather than copied UI status or caller-supplied time. No on-device Tracker composition established. |
| Existing hardware investigation | [inventory](../../hardware/INVENTORY.md), [Wio bring-up](../../hardware/WIO_TRACKER_L1_PRO_BRINGUP.md), [GNSS lifecycle tests](../../tests/host/wio_tracker_l1_gnss_lifecycle_tests.py) | Dated vendor-device/host-tool evidence. Wio Tracker L1 is a third-party product name, not an accepted Trail Tracker target or proof that it runs this product. Current possession, connection, revision and suitability require fresh verification when hardware work is separately approved. |

The source inventory contains reusable location and transport components, but
this review found no accepted dedicated Trail Tracker target/boot composition,
approved Tracker power profile or complete tracking-only physical acceptance.
Existing Heltec GNSS and radio results remain attached to their exact targets;
they do not transfer automatically to a new product or board.

## Complete flow and missing decisions

The future product flow to specify is: authorized setup -> explicit tracking
state -> current fix -> bounded report -> admitted protected transport ->
recipient observation -> stop/loss/restart/recovery. Each arrow needs a named
owner and truthful result. No fix must not become a fabricated location; an old
fix must not appear current; queue acceptance must not appear delivered.
Stopped tracking must not resume because stale input or a reconnect arrived.
Location privacy, logging and retention need explicit policy before storage or
transport is composed. These are requirements for later scope review, not newly
implemented behavior or defaults selected here.

Existing successor tasks cover the unresolved choices:

- **OT-0256b:** what is tracked, who may receive reports, and how the owner starts
  and stops tracking. Preserve the tracking-only boundary; distinguish necessary
  protocol acknowledgements/setup from a user messaging feature.
- **OT-0257a:** reporting cadence, stale/no-fix handling, consent/authorization,
  retention and safe diagnostic contents.
- **OT-0257b:** exact board, GNSS, radio region, controls and power requirements.
  No hardware choice, purchase or supported model is made in this report.
- **OT-0258a/b and OT-0259a/b:** only after those decisions, map implementation,
  interruption/recovery, physical measurement and operator/release acceptance.

## Validation and closeout

Read-only reconciliation inspected the listed source, tests and decisions and
checked relative links. Historical host results are reused; no source, test
matrix or hardware was changed or run for this planning task. Repository
documentation checks apply to the final report. No V1 completion credit,
public website status, repository structure, Git publication or successor
approval changes. This local report is ready for review only after independent
review and those document checks; owner acceptance remains separate.
