# Position-Sharing UI Observation v0

Status: **host-tested semantic refresh ordering; no ESP-IDF synchronization,
renderer, physical input/display, or concurrent target claim**

## Purpose

The outbound service path can change the position-sharing state without a
local UI action. A scheduled attempt can begin waiting for GPS, encounter sink
pressure, recover, or stop after a permanent checked-clock fault. Leaving the
previous frame active would make its notice stale and could let queued input
resolve against an obsolete privacy state.

`PositionSharingUiCoordinator` therefore observes live presentation semantics
before it polls input. This is an extension of the existing single-owner
cooperative UI sequence, not a new task or a second runtime owner.

## Ordering contract

After the first frame has committed, one `service()` call follows this order:

1. refuse if the UI coordinator is latched or revision space is exhausted;
2. derive one candidate presentation from current outbound and scheduler
   owners using the next revision;
3. compare the candidate's user-visible meaning with the last successfully
   presented frame;
4. when meaning differs, commit the candidate and return without polling
   input; otherwise poll at most one input event; and
5. after an applied or permanently rejected action, commit its result frame as
   required by the base UI coordinator contract.

Returning immediately after an observed refresh is deliberate. An input event
already queued for the old revision remains unread until the new frame is
active. The checked local interface then rejects it as stale on a later call,
without issuing a position command.

## Semantic comparison

The comparison ignores `UiFrame::revision` and includes every field a renderer
may expose:

- screen, attention, and notice;
- radio, position, power, peer-count-validity, peer count, and unread count;
- action count; and
- the action and enabled flag in all four canonical action slots.

The implementation compares fields individually rather than comparing object
memory, so padding cannot create false changes. Outbound service-call counts,
clock timestamps, retry deadlines, scheduler attempts, and other internal
runtime evidence do not advance the display revision unless they alter the
mapped frame semantics.

## Failure containment

If an observed candidate is not safely presentable or its display commit
fails, the UI coordinator calls the clock-independent outbound Stop command and
latches `external_refresh_failed`. No later input is accepted by that
boot-local coordinator instance.

If no higher revision remains, the existing `revision_exhausted` containment
occurs before candidate publication or input polling. Initial display failure
remains retryable because no earlier frame has become active and no visible
state transition has been left uncertain.

## Privacy and bounds

The retained frame and public status are fixed-size semantic records. They
contain no coordinates, payloads, messages, peer/device identity, keys,
addresses, credentials, or free text. `state_refreshes` is a saturating count
of successfully committed observed changes.

## Host evidence

Ten deterministic groups cover:

1. missing GPS refreshing active sharing to waiting before input;
2. a fresh fix refreshing waiting back to active;
3. not-ready, full, and failed sink results refreshing to deferred;
4. an external permanent clock fault refreshing to a critical no-action frame;
5. exact refresh counters without an input read;
6. queued old-revision input remaining unread, then being rejected stale;
7. observed-state display failure stopping sharing and latching closed;
8. a preexisting outbound fault becoming the safe initial presentation;
9. revision exhaustion containing an observed change before queued input; and
10. runtime timestamp/counter progress without semantic revision churn.

The focused executable passes 100/100 repeats. The complete 56-executable
OpenTrail host matrix plus all Python and publication-safety checks pass.

## Remaining gates

- serialize outbound and UI service with one exact ESP-IDF task/lock and prove
  snapshot coherence under the selected cadence;
- define renderer transitions and retry/fault wording without flashing or
  distracting churn;
- bind the host-tested [privacy-safe diagnostic event](../diagnostics/POSITION_SHARING_UI_DIAGNOSTIC_EVENT_V0.md)
  to target retention/export and operator workflows;
- prove boot/reboot revision seeding and default sharing behavior; and
- test real display/input latency, queued physical input, GPS/radio
  concurrency, low power, failures, long sessions, glove/wet use, and
  distracted-use constraints.
