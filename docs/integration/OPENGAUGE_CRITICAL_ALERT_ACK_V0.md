# OpenGauge Critical Alert Acknowledgement v0

Status: experimental mirrored interoperability contract, 2026-08-09. This is
not transport authentication, proof of delivery, a retry policy, or physical
Heltec/MeshCore evidence.

## Boundary

After OpenTrail independently validates and accepts or rejects one 64-byte
`OGA0` alert, it may encode a 64-byte `OGK0` acknowledgement. OpenGauge
decodes the same bytes and correlates them to its retained event lifecycle.

The frame carries no key, MAC address, VIN, location, free text, or raw vehicle
data. CRC detects accidental corruption only. The selected transport must
authenticate/authorize the OpenTrail consumer and protect session/sequence
replay before a decoded acknowledgement can change outbox state.

## Fixed frame

All multibyte integers are unsigned little-endian. Every reserved byte must be
zero.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII magic `OGK0` |
| 4 | 1 | schema version, currently 0 |
| 5 | 1 | total frame length, exactly 64 |
| 6 | 1 | disposition: 1 accepted, 2 rejected |
| 7 | 1 | reason |
| 8 | 1 | original alert lifecycle: 1 asserted, 2 cleared |
| 9 | 3 | reserved zero |
| 12 | 8 | nonzero OpenTrail consumer ID |
| 20 | 8 | nonzero OpenGauge producer ID |
| 28 | 8 | nonzero original event ID |
| 36 | 8 | nonzero original condition ID |
| 44 | 4 | nonzero consumer boot-session ID |
| 48 | 4 | ACK sequence, wrap policy belongs to ingress |
| 52 | 4 | observed alert age in milliseconds, 0 through 86400000 |
| 56 | 4 | reserved zero |
| 60 | 4 | CRC-32/ISO-HDLC over bytes 0 through 59 |

The CRC parameters match `OGA0`: reflected polynomial `0xEDB88320`, initial
`0xFFFFFFFF`, final XOR `0xFFFFFFFF`.

## Disposition and reason

Accepted disposition requires reason 0 (`none`). Rejected disposition requires
exactly one nonzero reason:

| Value | Meaning |
| ---: | --- |
| 1 | producer/authorization rejected |
| 2 | alert stale |
| 3 | event already accepted |
| 4 | event ID conflicts with different content/lifecycle |
| 5 | producer/rate capacity rejected |
| 6 | malformed semantic alert |
| 7 | unsupported version/type/policy |
| 8 | internal processing failure |

A rejected acknowledgement is explicit negative evidence, not success. An
accepted duplicate response may use accepted/none when OpenTrail can prove the
same event content was already accepted; the reason 3 rejection remains
available when policy intentionally refuses the duplicate. The final ingress
policy must choose one deterministic behavior.

## Identity, session, replay, and correlation

Before OpenGauge removes an outbox event, the adapter must prove all of:

- transport-authenticated consumer matches the configured nonzero consumer ID;
- producer ID matches the local producer identity;
- event ID, condition ID, and asserted/cleared state match the retained frame;
- disposition is accepted with reason none;
- consumer boot-session/ACK sequence passes a bounded replay window;
- observed age is within local event lifetime/freshness policy.

A new consumer boot session cannot silently reset trust. Pairing/authorization
epoch and restart policy must decide whether it is accepted. CRC alone proves
none of these facts.

## Normative evidence

`tests/fixtures/critical_alert_ack_v0_vectors.csv` contains three normative
vectors:

1. accepted asserted transition with nontrivial IDs/sequence;
2. rejected stale cleared transition;
3. accepted cleared transition at the exact maximum observed age.

OpenGauge and OpenTrail have independent C++ implementations. Each encodes the
same fields to the exact fixture bytes, decodes the fixture, rejects malformed,
version, reserved, CRC, identity, disposition, age, and enum failures, and
preserves caller output on invalid encode. Each four-group suite repeated 100
times with zero failures.

## Remaining gates

- bind the completed final-ingress ACK responder to the same authenticated
  physical peer/session that supplied the alert;
- define authenticated framed serial or local-wireless transport and peer/key
  lifecycle;
- implement bounded session/sequence replay tracking, restart, wrap, persistence,
  and rollback protection;
- compose OpenGauge outbox correlation with accepted/rejected policy and
  terminal failure diagnostics;
- test loss, duplicate, corruption, reordering, delayed ACK, restart, revoke,
  wrong producer/consumer, wrong channel, and relay behavior;
- validate physical two-Heltec delivery and document that a SenseCAP repeater
  forwards traffic but does not itself supply application acknowledgement.
