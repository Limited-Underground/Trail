# Critical Alert ACK Responder v0

Status: deterministic host-tested OpenTrail ingress-to-ACK composition,
2026-08-09. This is not a physical transport, cryptographic identity proof,
persistent sequence store, or physical OpenGauge delivery result.

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

The boot session and next sequence are RAM-only. Production requires durable
rollback-resistant lifecycle policy coordinated with peer authorization and
key rotation.

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

## Remaining gates

- bind response transmission to the same authenticated peer/session that
  supplied the alert;
- persist consumer boot-session/next-sequence and authorization epoch with
  power-loss, corruption, rollback, revoke, rotation, and reset recovery;
- connect typed redacted diagnostics and bounded response/rate queues;
- test loss, duplicate, delay, reorder, retry, restart, and negative reasons
  across the full OpenGauge-outbox to OpenTrail-ingress round trip; and
- validate the physical two-Heltec path and repeater behavior without claiming
  the repeater itself supplies application acknowledgement.
