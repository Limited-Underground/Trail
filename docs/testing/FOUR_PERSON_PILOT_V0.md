# Four-Person Standalone Pilot v0

Status date: 2026-08-10

Plan ID: `four-person-pilot-v0`

Status: **draft, blocked on the exact standalone client hardware and firmware freeze**

## Outcome this pilot must demonstrate

Four people can carry one identical OpenTrail client each and retain useful
group awareness for one hour without a repeater, server, internet connection,
phone, laptop, or vehicle connection during the session. This is the smallest
public pilot. It is deliberately not a maximum-device or production-reliability
claim.

The machine-validated plan is
[`OT-023-FOUR_PERSON_PILOT_V0.json`](../../tests/field-plans/OT-023-FOUR_PERSON_PILOT_V0.json).
Completed aggregate sessions are evaluated under the
[`OTPR0/v0` result contract](FOUR_PERSON_PILOT_RESULT_V0.md).
Run its fail-closed validation with:

```powershell
python tools/field_test_log.py validate-plan --input tests/field-plans/OT-023-FOUR_PERSON_PILOT_V0.json
```

## Zero-dependency boundary

| Item | Required during session? |
| --- | --- |
| Four identical standalone clients | Yes |
| Repeater | No |
| Server or internet | No |
| Phone | No |
| Laptop | No |
| Vehicle connection | No |

A laptop may prepare units before departure and collect raw logs afterward. It
must not be required to keep the group operating during the test.

## Hardware freeze gate

The plan cannot become `ready` until one exact client model and one exact
firmware version are recorded and four identical units each provide:

- self-contained battery power and a protective enclosure;
- GNSS position input;
- a locally readable display;
- a local input for quick status and critical alert actions; and
- a documented USB recovery path.

The host-tested [local-interface contract](../platform/LOCAL_INTERFACE_V0.md)
defines revision-bound semantic display/action behavior and hold-only critical
confirmation without selecting pixels, controls, or hardware. It satisfies a
software-boundary prerequisite only; it does not make this gate ready until all
four frozen physical units prove readable output and usable local input.

The two current Heltec USB Companion nodes plus the SenseCAP repeater do not
satisfy this gate: they are valuable transport evidence, but they are not four
identical self-contained pilot clients. The arrived Wio Tracker L1 Pro is also
not selected: its first USB/runtime/configuration experiment does not prove its
exact capabilities, over-air behavior, GNSS fix/loss, power/endurance, recovery,
or regulatory fit. No model is supported by this document yet.

## Minimum sessions

Run at least three one-hour sessions in materially different broad conditions:

1. open-field movement;
2. wooded-trail movement; and
3. rolling-road movement.

For any moving-road session, the driver must not operate or inspect a device.
Use a passenger or stationary observer for scripted actions and recording.
Repeat an affected session after any radio, protocol, power, enclosure, or
antenna change that could alter results.

## Scripted one-hour traffic

| Traffic class | Per client | Four-client total |
| --- | ---: | ---: |
| Position | every 60 seconds | 240 origins |
| Status | every 300 seconds | 48 origins |
| Quick status | 2 manual actions | 8 origins |
| Critical alert | 1 manual action | 4 origins |
| **Total** |  | **300 origins** |
| **Peer-delivery opportunities** | 3 peers per origin | **900** |

An origin is one message created by one client. A peer-delivery opportunity is
one expected observation by another client. The validator derives these totals
from duration, intervals, and member count so a hand-edited mismatch fails.

## Session procedure

### Preflight

1. Label roles only as `client-1` through `client-4`; do not publish participant
   names, radio addresses, serials, precise coordinates, channels, ports, keys,
   PINs, or secrets.
2. Verify the frozen model and firmware, attached antenna, enclosure, battery,
   display/input, GNSS, and USB recovery for all four units.
3. Record the common radio/profile configuration and verify the group contains
   exactly four clients and no repeater.
4. Synchronize the scripted start, clear or snapshot counters, capture starting
   battery state and uptime, and confirm there is no required external service.

### Run

1. Start the one-hour timer only after all four units are ready.
2. Allow periodic position and status traffic to run at the frozen cadence.
3. Distribute the two quick-status and one critical-alert actions per client
   across the hour rather than sending them as one burst.
4. Record visible stale/no-fix state, missing messages, duplicate presentation,
   false delivery success, resets, queue/rate-limit/preemption events, and any
   operator confusion when they occur. Preserve failures; do not tune them out
   of the record.

### Postflight

1. Capture final counters, uptime, queue state, GNSS state, and battery state
   before reconnecting support equipment.
2. Reconcile attempted, delivered, lost, duplicate, and latency observations by
   traffic class and peer role.
3. Keep raw captures local. Publish only an `OTFL0/v0` aggregate record after it
   passes `field_test_log.py validate`.
4. Verify temporary configuration and local recovery artifacts are cleaned up.

## Provisional pilot gates

| Gate | v0 threshold |
| --- | ---: |
| Critical-alert losses | 0 |
| False delivery successes | 0 |
| Operator-visible duplicates | 0 |
| Queue overflows | 0 |
| Device resets | 0 |
| Position delivery | at least 950,000 ppm (95%) |
| End-to-end p95 latency | at most 2,000 ms |
| Critical-alert maximum latency | at most 5,000 ms |
| Peer-stale indication | at most 180,000 ms |
| GNSS first fix | at most 300,000 ms |
| Ending battery | at least 20% |

A session fails immediately on any critical-alert loss, false delivery success,
visible duplicate, queue overflow, reset, or incomplete evidence. Falling below
a numeric threshold also fails the gate, but the evidence remains part of the
test history. These thresholds are provisional first-pilot decision gates, not
published product guarantees.

Status and quick-status delivery remain required evidence for review, but v0
does not assign either an independent numeric pass threshold. Any future change
to that decision requires a new plan version rather than rewriting v0 after
results are available.

## Evidence and interpretation boundary

The public record must include aggregate traffic totals by class, min/median/
p95/max latency, source/radio counter deltas, queue and rate-limit behavior,
reset/uptime evidence, GNSS current/stale/no-fix behavior, starting/ending power,
frozen configuration, and cleanup. The `OTFL0` privacy contract keeps raw
captures local and rejects identity-, transport-, precise-location-, and
secret-bearing public fields.

Passing three sessions would justify moving to the four-client-plus-repeater
phase. It would not prove the first-release ceiling, every terrain,
emergency-service fitness, regulatory acceptance, production security, or a
range specification.
