# Position-Sharing UI Diagnostic Event v0

Status: **host-tested fixed public event with a separate strict offline
decoder; no target log binding, retention, export, persistence, or physical
service claim**

## Purpose

The position-sharing UI coordinator returns typed service results for initial
presentation, observed refresh, actions, rejected input, and failures. Target
code needs a bounded way to retain useful outcomes without copying revisions,
runtime snapshots, coordinates, identifiers, packet data, or arbitrary text.

`OTPD0/v0` converts one validated coordinator result into one canonical 32-bit
event. The existing logger records it as exactly `OTPD0=XXXXXXXX` under the
fixed `position-ui` component tag.

## Event layout

| Bits | Field | Meaning |
| --- | --- | --- |
| 0-2 | event | presentation, observed refresh, action, input, or failure |
| 3-5 | outcome | succeeded, deferred, rejected, contained, or failed |
| 6-8 | notice | none, stopped, active, waiting for fix, deferred, or failed |
| 9-12 | reason | normalized clock, outbound, input, display, revision, presentation, command, or containment category |
| 13 | frame presented | a new position frame committed |
| 14 | state changed | a successful action changed sharing state |
| 15 | sharing contained | Stop containment was invoked |
| 16 | sensitive detail redacted | required canonical flag |
| 17-23 | reserved | must be zero |
| 24-27 | version | zero |
| 28-31 | magic | `0xC` |

The canonical stopped initial presentation is `0xC0012040`, logged as
`OTPD0=C0012040`.

## Validation and mapping

Encoding first validates all coordinator-result enums and their basic shape:
presented frames require a nonzero revision and one of the five position
notices; idle requires input-not-ready; temporary Start deferral requires the
typed outbound-not-ready result; action and input outcomes must agree with
their errors; failed service cannot claim a committed frame or state change.

Decoding verifies magic, version, reserved bits, known values, and coherence
between event, outcome, notice, reason, and flags. Unknown or contradictory
words fail closed.

Successful stopped/active presentation is informational. Waiting-for-fix,
sink-deferred, temporary clock deferral, and rejected input/action are warnings.
Containment, permanent outbound fault, input-source failure, failed display,
unavailable presentation, invalid initial revision, and other service failures
are errors. Initial display-not-ready remains a warning because that unused
revision can retry.

## Idle suppression and logger behavior

An ordinary idle poll produces `no_event` and never calls the logger. This
prevents a cooperative target loop from filling a bounded log with no-op
records.

For an encoded event, runtime log-level filtering is an accepted non-write.
Sink rejection is reported separately and is not described as accepted. The
logger timestamp is caller-supplied record metadata; it is not part of the
`OTPD0` word.

## Privacy boundary

The decoded structure and 32-bit word contain no:

- UI revision or action slot;
- service timestamp, retry deadline, scheduler/runtime counter, or raw enum
  detail beyond the normalized categories;
- coordinate, fix, accuracy, speed, heading, or UTC value;
- packet, payload, message, or free text;
- peer/device identity, address, key, credential, or transport identifier; or
- renderer, control, GPIO, or board detail.

The event is marked public only because those fields are structurally absent.
Target export still requires a deliberate retention and privacy policy.

## Host evidence

Ten deterministic groups cover:

1. exact stopped-presentation word, round trip, and message;
2. all visible refresh notices without runtime detail;
3. successful, deferred, and rejected action separation;
4. stale/invalid input and input-source failure;
5. display-not-ready, display-failed, and presentation-unavailable categories;
6. display/presentation containment, generic latched containment, and revision
   exhaustion;
7. permanent outbound fault severity even when the critical frame commits;
8. fixed logger tag/message and info/warn/error selection;
9. filtering, sink rejection, and idle suppression; and
10. malformed words, incoherent results, fixed size, and excluded fields.

The focused executable passes 100/100 repeats. The complete 58-executable
OpenTrail host matrix plus all Python and publication-safety checks pass.

The separate
[strict offline decoder](POSITION_SHARING_UI_DIAGNOSTIC_CLI_V0.md) accepts only
the canonical uppercase logger message and preserves these versioned category
names. Its ten operator groups and canonical/invalid CLI smoke checks also pass.

## Remaining gates

- bind the adapter and logger to one exact target task without adding a second
  clock read or leaking source detail;
- select RAM retention, overwrite, clear, export, and optional persistence
  policy with explicit privacy controls;
- test sink pressure, restart, low power, long sessions, and service capture on
  exact hardware; and
- decide whether any field artifact may include these public events and under
  what consent, deletion, and sharing rules.
