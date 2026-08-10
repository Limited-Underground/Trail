# Critical Alert ACK Responder v0

Status: deterministic host-tested OpenTrail ingress-to-ACK composition with a
bounded physical delivery and OpenGauge host-completion result, 2026-08-09. This is not an
authenticated transport binding, on-device application, cryptographic identity
proof, persistent per-session sequence store, or field delivery result.

## Boundary

`CriticalAlertAckResponder` accepts only a final `CriticalAlertIngress` result,
the exact ingress trust/time context, and a response monotonic timestamp. It
creates a 64-byte `OGK0` frame only after defensive decision checks.

Configuration contains a nonzero OpenTrail consumer ID, nonzero boot-session
ID, and initial 32-bit sequence. It contains no raw key, transport address,
PIN, channel secret, vehicle identity, free text, or location.

## Response policy

- a newly accepted alert produces accepted/none;
- a byte-identical duplicate produces accepted/none so a lost prior ACK can
  stop an OpenGauge retry;
- an authenticated but unauthorized producer produces rejected/unauthorized
  only when the authenticated producer ID still matches the decoded alert;
- stale and excessive-future-time policy failures produce rejected/stale;
- duplicate content/lifecycle conflict produces rejected/conflict;
- rate or producer-capacity rejection produces rejected/rate-limited; and
- codec failure, unauthenticated input, producer mismatch, or local monotonic
  rollback produces no response.

The responder never converts local processing failure into delivery success.
Rejected ACKs remain negative evidence in OpenGauge and do not remove its
outbox entry.

## Identity, age, and sequence

Before any response, authenticated context producer ID must equal the decoded
nonzero producer ID, and event/condition IDs must be nonzero. Accepted and
ordinary policy-rejected decisions must be consistent with the authorization
context.

Observed age is the alert's encoded age plus monotonic time spent inside
OpenTrail before response. Clock rollback and age above 24 hours fail closed.
The configured sequence is committed only after the ACK codec succeeds, so a
suppressed or invalid response cannot consume a number. Unsigned increment
supports normal 32-bit wrap; OpenGauge's ingress independently applies its
bounded replay window.

`AckResponderSessionStore` now allocates a commit-last, nonzero boot session
before the responder starts and binds it to the exact consumer ID and
authorization epoch. Each successful restart allocation increments the session;
OpenGauge must explicitly bind that new session. The next ACK sequence remains
RAM-only within the allocated session. Target persistence, secure rollback
resistance, and coordination with physical peer authorization/key rotation
remain production gates.

## Host evidence

`tests/host/critical_alert_ack_responder_tests.cpp` covers eight groups:

1. lifecycle and configuration;
2. accepted ingress decision, exact correlation fields, elapsed age, and
   independent ACK decode;
3. identical duplicate mapping to accepted/none;
4. authenticated unauthorized and stale negative responses;
5. conflict, rate, and producer-capacity reason mapping;
6. unauthenticated, malformed, and producer-mismatch suppression;
7. inconsistent decision/context rejection without sequence consumption; and
8. clock/age bounds plus sequence wrap and atomic advancement.

The complete OpenTrail host matrix passes, and this focused suite repeated 100
times with zero failures.

OT-017E also carried two later role-reversed responder outputs over the
three-radio bench and admitted each returned physical ACK through OpenGauge's
real peer authorization, session binding, replay/correlation ingress, and exact
reconstructed outbox completion. Both paths finished with one acknowledgement
and zero queued/in-flight entries. OpenGauge state was reconstructed after the
radio wait; this does not prove a persistent on-device pipeline.

OT-017F exercised the stale-policy branch in two role-reversed physical cycles.
Both exact rejections were processed by OpenGauge with zero delivery
acknowledgements, `outbox_completed=false`, no retry release, and terminal
failure. This confirms that the composed host path did not convert a correlated
negative response into success.

OT-017G then exercised the rate-limited branch in two role-reversed physical
cycles. Both exact rejections produced zero delivery acknowledgements or
completion, released one queued OpenGauge retry, and avoided terminal failure.
Durable retry state and a subsequent physical retry-to-accept sequence remain.

## Remaining gates

- bind response transmission to the same authenticated peer/session that
  supplied the alert;
- bind the host-tested boot-session allocator to target storage and define
  per-session sequence persistence or a reserved-range policy;
- connect typed redacted diagnostics and bounded response/rate queues;
- test loss, duplicate, delay, reorder, retry, restart, and negative reasons
  across the full OpenGauge-outbox to OpenTrail-ingress round trip; and
- replace the passed host-mediated two-Heltec/SenseCAP path with authenticated
  on-device delivery; the repeater itself does not supply application
  acknowledgement.
