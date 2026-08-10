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
8. observes the verified `(source alias, epoch, message ID)` replay key;
9. rejects monotonic process-time regression before replay state;
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

Nine scenario groups cover:

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
queue/rate congestion as a consumed replay opportunity.

## Remaining gates

This host policy plus coordinator does not prove source authentication, packet
v1, protected target replay persistence, direct SX1262 binding, target
scheduling, actual repeater-roster enforcement, physical power-loss recovery,
flash endurance, radio loss/range, or four/eight-client field behavior. The RAM
frame queue is not durable, so power loss after replay save and before transmit
can lose that forwarding opportunity. It supports one repeater only. Multi-
repeater forwarding requires a separate reviewed outer construction or new
packet version.
