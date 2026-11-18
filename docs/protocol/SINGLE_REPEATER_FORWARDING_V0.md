# Immutable Single-Repeater Forwarding Policy v0

Status: algorithm-neutral host policy, not cryptographic or target evidence,
2026-08-10

## Purpose

`SingleRepeaterForwarder` is the host-policy implementation of Decision 0004.
It replaces the production direction of decrementing separate TTL metadata with
a bounded first-release rule: one authorized repeater validates one immutable
protected frame and, when eligible, queues the exact same bytes once.

The existing `ForwardingController` remains useful v0 simulation evidence for
TTL, congestion, and multi-node behavior. Its metadata is outside packet v0 and
is not production authenticated-routing evidence.

## Adapter obligation

The forwarder does not parse a packet or perform cryptography. It accepts
`VerifiedForwardingMetadata`, which a future protected-packet adapter may create
only after verifying:

- the group security context and current epoch;
- the claimed source cryptographically;
- current source membership/authorization;
- the destination, message ID, and immutable forwarding permission; and
- the exact frame bytes covered by that evidence.

Host tests set those evidence fields directly to exercise policy. A production
caller cannot convert an untrusted parsed Boolean, display name, alias, or radio
metadata into verification evidence.

## Processing order

For every candidate frame, the policy:

1. validates nonzero bounded metadata and frame shape;
2. requires a valid local configuration with exactly one authorized repeater;
3. requires source authentication;
4. requires current source authorization;
5. matches the verified group context and epoch;
6. rejects a self-source reflection or a frame already addressed to the
   repeater;
7. requires the immutable forwarding-permission field;
8. rejects monotonic process-time regression before replay state;
9. observes the verified `(source alias, epoch, message ID)` replay key;
10. suppresses a duplicate/reflection;
11. applies fixed forwarding-rate and queue limits; and
12. copies the exact frame bytes into the bounded queue.

Authentication, authorization, wrong-context, local-destination, and denied-
permission failures occur before duplicate state changes. After an eligible
authenticated key is observed, a queue/rate failure is not rescued by replaying
the same frame later; the retry becomes a duplicate. This bounds replay-driven
amplification at the cost of a visible dropped forwarding opportunity.

No TTL, sender, destination, fragment, counter, ciphertext, or tag byte is
changed. The output intentionally contains only the exact protected frame, not
rewritable routing metadata.

## Queue and time boundaries

The implementation has eight compile-time queue slots. Runtime policy selects a
smaller active depth, forwards-per-window limit, monotonic rate window, and
maximum queue age.

A queued frame is expired when `age >= maximum_queue_age_ms`; the exact boundary
drops. A monotonic-clock regression below the enqueue time also drops rather
than transmitting with uncertain age. `next_forward()` discards all expired
front entries until it finds one still eligible, and reports the bounded number
discarded during that call.

## Host evidence

The original nine scenario groups cover:

1. exact byte-for-byte forwarding;
2. authentication refusal without replay-window poisoning;
3. authorization/context/permission refusal before replay state;
4. exactly one locally authorized repeater;
5. duplicate/reflection suppression;
6. self-source and local-destination refusal;
7. queue/rate limits and non-rescuable congestion replays;
8. exact expiry plus enqueue/process clock-regression drops; and
9. invalid frame/metadata rejection.

The companion [replay coordinator](SINGLE_REPEATER_REPLAY_COORDINATOR_V0.md)
persists every newly observed eligible key before permitting queue release and
restores or repairs that state before operation. It intentionally treats
queue/rate congestion as a consumed replay opportunity. Later host tests cover
admission ownership, exact age and a framing-shaped F5 input with synthetic
metadata; no pairing admission is enabled.

## Applying this boundary to current Trail frames

Design only. The Repeater is outside the current two-endpoint Trail V1 link.
There is no relay-enabled ingress in the P1-Pro target. The design uses one
local, deliberately enabled relay and independent endpoint authentication.
A relay never holds private, agreement, traffic, or ACK keys, and never opens
ciphertext. An independently reviewed producer may use public verification
material to bind permission to the exact opaque object; none is accepted here.

| Input | Current behavior | Condition for a future relay |
| --- | --- | --- |
| Protected endpoint traffic | Refuse; Packet-v1 / OTSL0 direct pairwise traffic does not prove relay eligibility. A successful parse, PHY CRC, alias or group identifier grants nothing. | Trail security must define a relay-eligible proof for exact bytes, source, destination, current membership/epoch, and one-relay permission. Specify whether replay identity covers a whole object or independent fragments; never collapse distinct fragments that share a message ID. |
| F5 R2a board pairing (OP v1) | Refuse relaying; endpoints continue their direct exchange. The six-byte header, public pairing code, frame type and exact lengths classify traffic only. A candidate is untrusted, and even a correctly shaped signature part does not grant relay authority. | A separate reviewed pairing relay authorization and repeat policy must preserve endpoint role/code from the owner's own phone, both full fingerprint confirmations, proof of possession, transcript confirmation and endpoint deadline. No relay-generated trust or extension of that deadline. |
| Unknown, malformed, legacy, wrong-context, expired, or revoked input | Refuse; parsing alone may not allocate queue, replay or airtime state. | No fallback or permissive mode. |

The F5 wire source is **Trail main at c486a42aed97852eac0d7d3826b1dd97340d01c5**:
[codec](https://github.com/Limited-Underground/Trail/blob/c486a42aed97852eac0d7d3826b1dd97340d01c5/firmware/components/protocol/include/opentrail/pairing_frame.hpp),
[pairing plan](https://github.com/Limited-Underground/Trail/blob/c486a42aed97852eac0d7d3826b1dd97340d01c5/docs/security/F5-R2-BOARD-PAIRING-PLAN.md), and
[repeat handling](https://github.com/Limited-Underground/Trail/blob/c486a42aed97852eac0d7d3826b1dd97340d01c5/firmware/components/protocol/include/opentrail/pairing_coordinator.hpp).
These sources are read-only inputs; the Repeater branch does not import the F5
implementation. The largest pairing frame is 194 bytes, within the 255-byte
forwarder storage ceiling. The invitation and signature are separate frames.
Do not combine, split, transcode or manufacture either part. A matching code/type
is not a duplicate key: distinct valid frames can share it. Endpoint pairing
allows a bounded repeat (at most two accepted sends per frame in 120 seconds);
the coordinator can answer an exact repeat to recover loss. Do not apply a
permanent pairing-frame duplicate rule that removes this recovery. Until ordered
loss cases and an authenticated attempt binding are reviewed, leave F5 direct.

For eligible protected traffic, store and retransmit the **complete on-air
octet string as received**. Never decrypt, alter, reseal, recalculate a tag,
invent a reply/ACK or frame, learn routes from unauthenticated input, or turn
an uncertain radio outcome into a retry. There is no mutable on-air hop counter:
maximum one relay is enforced by the authorized group topology; clients never
relay. TTL here is the original local monotonic queue age: refuse at equality,
wrong time, or invalid authority; a repeat never refreshes it. Multi-hop and
an outer envelope are separate specification tasks.

Reuse the existing forwarder for bounded duplicate observation and the sole
FIFO. Its physical storage is eight slots × 255 bytes of payload, plus metadata;
P2 pending custody and the driver copy also count toward memory budgeting.
Runtime queue, frame, per-source, global, airtime, age and diagnostic bounds
must be finite; production values are unselected. Full queue refuses newest.
Use the existing replay coordinator to save eligible observations before release,
including congestion refusals. The recent duplicate window is bounded, not
cryptographic replay authority. P2 owns original queue age, pending bytes and
final one-use admission; P3 owns irreversible accounting. Restart, revocation
or radio uncertainty never recreates the volatile queue or refunds airtime.
This work adds a wire-shaped FIFO test using synthetic authorization; it adds
no admission adapter, second queue, or transmitter.

## Remaining gates

This host policy plus coordinator does not prove source authentication, packet
v1, protected target replay persistence, direct SX1262 binding, target
scheduling, actual repeater-roster enforcement, physical power-loss recovery,
flash endurance, radio loss/range, or four/eight-client field behavior. The RAM
frame queue is not durable, so power loss after replay save and before transmit
can lose that forwarding opportunity. It supports one repeater only. Multi-
repeater forwarding requires a separate reviewed outer construction or new
packet version.
